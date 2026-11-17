#pragma once
#include <array>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#if defined(ARDUINO)
#include <esp_heap_caps.h>
#endif
#include "appearance.h"
#include "flash_log.h"
#include "power_diagnostics.h"
#include "practice_history.h"

// The automatic saves (practice history, personal bests, the power log) as
// records of the flash log, so they never write NVS while the device is in
// use. NVS keeps 0.15-0.17's records (history0/1, best0/1, the power log) as
// a mirror written at sleep, power-off and restore, so a unit rolled back to
// them keeps its data and its saves still fit.
//
// Newest wins: each record carries the NVS value this firmware last mirrored
// (history and bests generation, power log checksum). At boot, NVS that still
// shows that value is the older copy and the log's record is used; NVS that
// changed since was written by an older firmware after a rollback, so NVS is
// used. Counters that each firmware advances on its own are never compared.
namespace HistoryLog {
enum Type:uint16_t {History=1,Bests=2,Power=3};
constexpr uint32_t HISTORY_BYTES=4+PracticeHistory::IMAGE_BYTES,BESTS_BYTES=4+32,POWER_BYTES=4+sizeof(PowerDiagnostics::Image);
static_assert(HISTORY_BYTES<=FlashLog<int>::MAX_PAYLOAD && POWER_BYTES<=FlashLog<int>::MAX_PAYLOAD,"records fit a sector");

// Save and load buffers come from PSRAM on the device. Below 16 KB the
// default heap is internal RAM there, and a history save while a song
// streamed took 5.4 KB of it, below the 32 KB DMA floor (0.20 HIL songs,
// 2026-10-23).
struct ScratchFree {
  void operator()(void *p)const {
#if defined(ARDUINO)
    heap_caps_free(p);
#else
    std::free(p);
#endif
  }
};
template<class T> using Scratch=std::unique_ptr<T,ScratchFree>;
template<class T> Scratch<T> scratch() {
  static_assert(std::is_trivially_destructible<T>::value,"freed without a destructor");
#if defined(ARDUINO)
  void *p=heap_caps_malloc(sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  void *p=std::malloc(sizeof(T));
#endif
  return Scratch<T>(p?new(p) T:nullptr);
}

enum class Source:uint8_t {Log,Nvs,None};
// Which copy boot uses (see above). `mirrored` is the record's mirrored NVS value.
inline Source pick(bool inLog,uint32_t mirrored,bool inNvs,uint32_t nvsValue) {
  if(!inLog)return inNvs?Source::Nvs:Source::None;
  if(!inNvs)return Source::Log;
  return nvsValue==mirrored?Source::Log:Source::Nvs;
}

template<class Flash> class Store {
public:
  using Log=FlashLog<Flash>;
  typename Log::Open open(Flash &flash){return log_.open(flash);}
  bool isOpen()const{return log_.isOpen();}
  Log &log(){return log_;}

  bool saveHistory(const PracticeHistory &h,uint32_t mirrored) {
    const auto b=scratch<std::array<uint8_t,HISTORY_BYTES>>();const auto image=scratch<PracticeHistory::Image>();
    if(!b || !image)return false;
    put32(b->data(),mirrored);h.encode(*image);std::memcpy(b->data()+4,image->data(),image->size());
    return log_.append(History,b->data(),HISTORY_BYTES);
  }
  bool loadHistory(PracticeHistory &h,uint32_t &mirrored)const {
    const auto r=log_.newest(History);
    const auto b=scratch<std::array<uint8_t,HISTORY_BYTES>>();
    if(!b || r.length!=HISTORY_BYTES || !log_.readPayload(r,b->data(),HISTORY_BYTES))return false;
    const auto decoded=scratch<PracticeHistory>();
    if(!decoded || !decoded->decode(b->data()+4,PracticeHistory::IMAGE_BYTES))return false;
    mirrored=get32(b->data());h=*decoded;return true;
  }
  bool saveBests(const Appearance::Bests &bests,uint32_t mirrored) {
    uint8_t b[BESTS_BYTES];put32(b,mirrored);Appearance::Bests::Image image;bests.encode(image);
    std::memcpy(b+4,image.data(),image.size());return log_.append(Bests,b,sizeof(b));
  }
  bool loadBests(Appearance::Bests &bests,uint32_t &mirrored)const {
    const auto r=log_.newest(Bests);uint8_t b[BESTS_BYTES];Appearance::Bests decoded;
    if(r.length!=BESTS_BYTES || !log_.readPayload(r,b,sizeof(b)) || !decoded.decode(b+4,32))return false;
    mirrored=get32(b);bests=decoded;return true;
  }
  bool savePower(const PowerDiagnostics::Image &log,uint32_t mirrored) {
    const auto b=scratch<std::array<uint8_t,POWER_BYTES>>();
    if(!b)return false;
    put32(b->data(),mirrored);std::memcpy(b->data()+4,&log,sizeof(log));return log_.append(Power,b->data(),POWER_BYTES);
  }
  // Reads straight into `log` (left unchanged unless the record is valid).
  bool loadPower(PowerDiagnostics::Image &log,uint32_t &mirrored)const {
    const auto r=log_.newest(Power);
    const auto b=scratch<std::array<uint8_t,POWER_BYTES>>();const auto decoded=scratch<PowerDiagnostics::Image>();
    if(!b || !decoded || r.length!=POWER_BYTES || !log_.readPayload(r,b->data(),POWER_BYTES))return false;
    std::memcpy(decoded.get(),b->data()+4,sizeof(*decoded));
    if(!PowerDiagnostics::valid(*decoded))return false;
    mirrored=get32(b->data());log=*decoded;return true;
  }
  bool readyAhead(){return log_.readyAhead();}
private:
  static void put32(uint8_t *p,uint32_t v){FlashLayout::put32(p,v);}
  static uint32_t get32(const uint8_t *p){return FlashLayout::get32(p);}
  Log log_;
};
}
