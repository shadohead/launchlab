#pragma once
#include <memory>
#include <new>
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
    std::unique_ptr<uint8_t[]> b(new(std::nothrow) uint8_t[HISTORY_BYTES]);
    if(!b)return false;
    std::unique_ptr<PracticeHistory::Image> image(new(std::nothrow) PracticeHistory::Image);
    if(!image)return false;
    put32(b.get(),mirrored);h.encode(*image);std::memcpy(b.get()+4,image->data(),image->size());
    return log_.append(History,b.get(),HISTORY_BYTES);
  }
  bool loadHistory(PracticeHistory &h,uint32_t &mirrored)const {
    const auto r=log_.newest(History);
    std::unique_ptr<uint8_t[]> b(new(std::nothrow) uint8_t[HISTORY_BYTES]);
    if(!b || r.length!=HISTORY_BYTES || !log_.readPayload(r,b.get(),HISTORY_BYTES))return false;
    std::unique_ptr<PracticeHistory> decoded(new(std::nothrow) PracticeHistory);
    if(!decoded || !decoded->decode(b.get()+4,PracticeHistory::IMAGE_BYTES))return false;
    mirrored=get32(b.get());h=*decoded;return true;
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
    std::unique_ptr<uint8_t[]> b(new(std::nothrow) uint8_t[POWER_BYTES]);
    if(!b)return false;
    put32(b.get(),mirrored);std::memcpy(b.get()+4,&log,sizeof(log));return log_.append(Power,b.get(),POWER_BYTES);
  }
  // Reads straight into `log` (left unchanged unless the record is valid).
  bool loadPower(PowerDiagnostics::Image &log,uint32_t &mirrored)const {
    const auto r=log_.newest(Power);
    std::unique_ptr<uint8_t[]> b(new(std::nothrow) uint8_t[POWER_BYTES]);
    std::unique_ptr<PowerDiagnostics::Image> decoded(new(std::nothrow) PowerDiagnostics::Image);
    if(!b || !decoded || r.length!=POWER_BYTES || !log_.readPayload(r,b.get(),POWER_BYTES))return false;
    std::memcpy(decoded.get(),b.get()+4,sizeof(*decoded));
    if(!PowerDiagnostics::valid(*decoded))return false;
    mirrored=get32(b.get());log=*decoded;return true;
  }
  bool readyAhead(){return log_.readyAhead();}
private:
  static void put32(uint8_t *p,uint32_t v){FlashLayout::put32(p,v);}
  static uint32_t get32(const uint8_t *p){return FlashLayout::get32(p);}
  Log log_;
};
}
