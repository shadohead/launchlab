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
#include "rival_book.h"

// The automatic saves (practice history, personal bests, the power log and
// the battle records) as records of the flash log, so they never write NVS
// while the device is in use. NVS keeps 0.15-0.17's records (history0/1, best0/1, the power log) as
// a mirror written at sleep, power-off and restore, so a unit rolled back to
// them keeps its data and its saves still fit.
//
// Newest wins: each record carries the NVS value this firmware last mirrored
// (history and bests generation, power log checksum). At boot, NVS that still
// shows that value is the older copy and the log's record is used; NVS that
// changed since was written by an older firmware after a rollback, so NVS is
// used. Counters that each firmware advances on its own are never compared.
//
// NVS without the record, readable: when the log's record says NVS held none
// (NOT_MIRRORED, as 0.18 on has always written then: an empty store's
// generation and the unset power checksum are 0) NVS refused every copy and
// the log is the only one. When it says NVS held one, NVS was erased behind
// this firmware's back, by Wipe all on 0.15-0.17 after a rollback (they do
// not know the log), and the wiped record must not come back: none is
// loaded, and boot logs the empty one over it. NVS that cannot be read
// (damaged, or a namespace a full NVS could not open) proves nothing: the log.
// A record logged before NVS took its first copy would still say NOT_MIRRORED
// after that copy, so the first copy NVS takes is logged again at once.
//
// NVS lost, not wiped: the Arduino core erases the whole NVS partition at boot
// when nvs_flash_init() finds no free page or a newer format (initArduino), and
// setup() then finds no namespace at all. Wipe all on 0.15-0.17 clears its
// namespaces' keys (nvs_erase_all), never the namespaces, and that firmware
// restarts and opens them again, so its NVS is never without one. On a virgin
// NVS boot disowns the log's records (disown(): each logged again saying
// NOT_MIRRORED) before anything creates a namespace, and they load: the log is
// what brings history, bests and the power log back after such a loss.
//
// The battle records (rival_book.h) are in the log alone, with no NVS copy:
// NVS has no room to spare for them, and firmware before battles has no use
// for them. Such a firmware leaves the record in place but does not protect
// its sector, so a unit rolled back to it loses the record once the log has
// gone round (about 28 history saves).
namespace HistoryLog {
enum Type:uint16_t {History=1,Bests=2,Power=3,Rivals=4};
constexpr uint32_t HISTORY_BYTES=4+PracticeHistory::IMAGE_BYTES,BESTS_BYTES=4+32,POWER_BYTES=4+sizeof(PowerDiagnostics::Image);
constexpr uint32_t RIVALS_BYTES=Battle::Book::IMAGE_BYTES;
static_assert(Rivals<FlashLog<int>::TYPES && RIVALS_BYTES<=FlashLog<int>::MAX_PAYLOAD,"the log knows the battle record");
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
inline const char *name(Source s){return s==Source::Log?"log":s==Source::Nvs?"nvs":"none";}
// What boot found in NVS: the record, certainly none (the namespace opened
// and holds no key of it), or nothing it can tell (unreadable or unopened).
enum class Nvs:uint8_t {Found,Absent,Unreadable};
// The mirrored value of a record logged while NVS held none.
constexpr uint32_t NOT_MIRRORED=0;
// Which copy boot uses (see above). `mirrored` is the record's mirrored NVS
// value, `nvsValue` what NVS holds (with Found).
inline Source pick(bool inLog,uint32_t mirrored,Nvs nvs,uint32_t nvsValue) {
  if(nvs==Nvs::Found)return inLog && nvsValue==mirrored?Source::Log:Source::Nvs;
  if(!inLog || (nvs==Nvs::Absent && mirrored!=NOT_MIRRORED))return Source::None;
  return Source::Log;
}

template<class Flash> class Store {
public:
  using Log=FlashLog<Flash>;
  typename Log::Open open(Flash &flash){return log_.open(flash);}
  bool isOpen()const{return log_.isOpen();}
  void close(){log_.close();}
  Log &log(){return log_;}
  // A record of `type` reached the log without reading back (FlashLog::unverified).
  bool unverified(Type type)const{return log_.unverified(type);}

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
  // The battle records: the book's own checksummed image.
  bool saveRivals(const Battle::Book &book) {
    const auto image=scratch<Battle::Book::Image>();
    if(!image)return false;
    book.encode(*image);return log_.append(Rivals,image->data(),RIVALS_BYTES);
  }
  // `book` is left unchanged unless the record is valid.
  bool loadRivals(Battle::Book &book)const {
    const auto r=log_.newest(Rivals);const auto image=scratch<Battle::Book::Image>();
    return image && r.length==RIVALS_BYTES && log_.readPayload(r,image->data(),RIVALS_BYTES) && book.decode(image->data(),RIVALS_BYTES);
  }
  // Each type's newest record that says NVS held its copy, logged again
  // saying NOT_MIRRORED (the payload otherwise byte for byte). Returns how
  // many it logged, or -1 if a read or write failed (run it again: a record
  // already disowned is not logged twice).
  int disown() {
    const auto b=scratch<std::array<uint8_t,FlashLog<Flash>::MAX_PAYLOAD>>();
    if(!b)return -1;
    int n=0;
    for(uint16_t type:{uint16_t(History),uint16_t(Bests),uint16_t(Power)}) {
      const auto r=log_.newest(type);
      if(!r.found || r.length<4)continue;
      if(!log_.readPayload(r,b->data(),r.length))return -1;
      if(get32(b->data())==NOT_MIRRORED)continue;
      put32(b->data(),NOT_MIRRORED);
      if(!log_.append(type,b->data(),r.length))return -1;
      ++n;
    }
    return n;
  }
  bool readyAhead(){return log_.readyAhead();}
private:
  static void put32(uint8_t *p,uint32_t v){FlashLayout::put32(p,v);}
  static uint32_t get32(const uint8_t *p){return FlashLayout::get32(p);}
  Log log_;
};
}
