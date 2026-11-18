// The history log between this firmware and the releases that wrote it
// before Wipe all on 0.15-0.17 was told apart from a refused NVS copy
// (history_log.h), using the frozen 0.18.0 and 0.20.8 sources themselves
// (0.20.10's history log code is 0.20.8's) compiled beside this firmware in
// their own namespaces. The record format is unchanged:
//  - rolled back to 0.18-0.20.10, a unit reads every record this firmware
//    writes and picks as it did, and the empty records this firmware logs
//    over wiped ones keep the wiped data from coming back there too;
//  - records those releases wrote in the field load here as there: with
//    NVS intact, with NVS refusing every copy (they wrote NOT_MIRRORED), and
//    after a rollback that saved in NVS. Only an NVS that lost a record they
//    said it held now loads none.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>
// Needs clang: GCC's #pragma once also skips a frozen header that is
// byte-identical to one already included, which would compile the frozen
// release against today's types instead.
#if defined(__GNUC__) && !defined(__clang__)
#error "test_history_log_compat needs clang (GCC merges byte-identical #pragma once headers)"
#endif
#include "nor_flash.h"
#include "../LaunchLabMini/history_log.h"
namespace Released018 {
#include "../releases/0.18.0-sticks3-anime-pm/source/history_log.h"
}
namespace Released0208 {
#include "../releases/0.20.8-sticks3-songs-pm/source/history_log.h"
}

namespace {
using HistoryLog::Nvs;using HistoryLog::NOT_MIRRORED;using HistoryLog::Source;
using Store=HistoryLog::Store<NorFlash>;using Log=FlashLog<NorFlash>;

PracticeHistory pulls(unsigned n,uint32_t from=1) {
  PracticeHistory h;
  for(unsigned i=0;i<n;++i)assert(h.accept(from+i,true,8000+37.0f*float(i),1000*(from+i)));
  return h;
}
PracticeHistory::Image image(const PracticeHistory &h){PracticeHistory::Image b;h.encode(b);return b;}
Appearance::Bests::Image image(const Appearance::Bests &b){Appearance::Bests::Image i;b.encode(i);return i;}
PowerDiagnostics::Image powerLog(unsigned samples) {
  PowerDiagnostics::Image p;PowerDiagnostics::fresh(p);
  for(unsigned i=0;i<samples;++i)PowerDiagnostics::append(p,PowerDiagnostics::Sample,3900,0,30,80);
  p.checksum=PowerDiagnostics::hash(p);return p;
}

// A frozen release's history log types and boot choice.
#define RELEASE(Name,Space) struct Name { \
  using Store=Space::HistoryLog::Store<NorFlash>;using Open=Space::FlashLog<NorFlash>::Open; \
  using History=Space::PracticeHistory;using Bests=Space::Appearance::Bests;using Power=Space::PowerDiagnostics::Image; \
  static Source pick(bool inLog,uint32_t mirrored,bool inNvs,uint32_t value) { \
    const auto x=Space::HistoryLog::pick(inLog,mirrored,inNvs,value); \
    return x==Space::HistoryLog::Source::Log?Source::Log:x==Space::HistoryLog::Source::Nvs?Source::Nvs:Source::None; \
  } \
};
RELEASE(R018,Released018)
RELEASE(R0208,Released0208)
#undef RELEASE

// What a release's own boot loads from the log, with NVS as given (its pick
// takes "NVS holds a record" and that record's value).
struct Loaded {
  Source history,bests,power;std::vector<uint8_t> historyImage,bestsImage;uint32_t powerCount=0;
};
template<class R> Loaded releasedBoot(NorFlash &f,bool inNvs,uint32_t nvsHistory,uint32_t nvsBests,uint32_t nvsPower) {
  typename R::Store s;assert(s.open(f)==R::Open::Ready);
  auto h=std::make_unique<typename R::History>();typename R::Bests b;typename R::Power p;
  uint32_t mh=0,mb=0,mp=0;
  const bool hl=s.loadHistory(*h,mh),bl=s.loadBests(b,mb),pl=s.loadPower(p,mp);
  Loaded out{R::pick(hl,mh,inNvs,nvsHistory),R::pick(bl,mb,inNvs,nvsBests),R::pick(pl,mp,inNvs,nvsPower)};
  if(hl){typename R::History::Image i;h->encode(i);out.historyImage.assign(i.begin(),i.end());}
  if(bl){typename R::Bests::Image i;b.encode(i);out.bestsImage.assign(i.begin(),i.end());}
  if(pl)out.powerCount=p.count;
  return out;
}
template<class A> std::vector<uint8_t> bytes(const A &a){return {a.begin(),a.end()};}

// What a release writes: its own Store over the same flash.
template<class R> void releasedSave(NorFlash &f,const PracticeHistory &h,uint32_t mh,const Appearance::Bests &b,uint32_t mb,
                                    const PowerDiagnostics::Image &p,uint32_t mp) {
  typename R::Store s;const auto opened=s.open(f);
  assert(opened==R::Open::Ready || opened==R::Open::Formatted);
  auto oh=std::make_unique<typename R::History>();const auto hi=image(h);assert(oh->decode(hi.data(),hi.size()));
  typename R::Bests ob;const auto bi=image(b);assert(ob.decode(bi.data(),bi.size()));
  typename R::Power op;static_assert(sizeof(op)==sizeof(p),"same power log image");std::memcpy(&op,&p,sizeof(p));
  assert(s.saveHistory(*oh,mh) && s.saveBests(ob,mb) && s.savePower(op,mp));
}

template<class R> void rollback(const char *name) {
  // A record this firmware wrote after NVS held its copy G: the release loads it as before.
  {
    NorFlash f;Store s;assert(s.open(f)==Log::Open::Formatted);
    const auto h=pulls(20);Appearance::Bests b;b.observe(0,0,true,9400);const auto p=powerLog(3);
    assert(s.saveHistory(h,h.generation()-2) && s.saveBests(b,b.generation()) && s.savePower(p,p.checksum));
    const auto old=releasedBoot<R>(f,true,h.generation()-2,b.generation(),p.checksum);
    assert(old.history==Source::Log && old.bests==Source::Log && old.power==Source::Log);
    assert(old.historyImage==bytes(image(h)) && old.bestsImage==bytes(image(b)) && old.powerCount==p.count);
  }
  // Wipe all on 0.15-0.17, then this firmware: it loads none of the wiped records and logs the empty
  // ones over them (pickLogged*). Rolled back once more with NVS still empty, the release loads the
  // log (it does whenever NVS holds none): the empty records, not the wiped ones.
  {
    NorFlash f;Store s;assert(s.open(f)==Log::Open::Formatted);
    const auto h=pulls(15);Appearance::Bests b;b.observe(0,0,true,9500);const auto p=powerLog(4);
    assert(s.saveHistory(h,h.generation()) && s.saveBests(b,b.generation()) && s.savePower(p,p.checksum));
    {const auto unguarded=releasedBoot<R>(f,false,0,0,0);  // the release alone would bring them back
     assert(unguarded.history==Source::Log && unguarded.historyImage==bytes(image(h)) && unguarded.powerCount==p.count);}
    Store boot;assert(boot.open(f)==Log::Open::Ready);
    PracticeHistory back;Appearance::Bests bb;PowerDiagnostics::Image pb;uint32_t mh=0,mb=0,mp=0;
    assert(boot.loadHistory(back,mh) && boot.loadBests(bb,mb) && boot.loadPower(pb,mp));
    assert(HistoryLog::pick(true,mh,Nvs::Absent,0)==Source::None && HistoryLog::pick(true,mb,Nvs::Absent,0)==Source::None &&
           HistoryLog::pick(true,mp,Nvs::Absent,0)==Source::None);
    const auto empty=powerLog(0);
    assert(boot.saveHistory(PracticeHistory(),NOT_MIRRORED) && boot.saveBests(Appearance::Bests(),NOT_MIRRORED) && boot.savePower(empty,NOT_MIRRORED));
    const auto old=releasedBoot<R>(f,false,0,0,0);
    assert(old.history==Source::Log && old.historyImage==bytes(image(PracticeHistory())));
    assert(old.bests==Source::Log && old.bestsImage==bytes(image(Appearance::Bests())) && old.power==Source::Log && old.powerCount==0);
  }
  // NVS's first copy logged again at once (flushForShutdown): the release finds NVS at the mirrored
  // generation and loads the log's record, the same pulls NVS holds.
  {
    NorFlash f;Store s;assert(s.open(f)==Log::Open::Formatted);
    const auto h=pulls(4);assert(s.saveHistory(h,NOT_MIRRORED) && s.saveHistory(h,h.generation()));
    const auto old=releasedBoot<R>(f,true,h.generation(),0,0);
    assert(old.history==Source::Log && old.historyImage==bytes(image(h)));
  }
  // NVS lost to the core's erase: this firmware disowns the records (they say NOT_MIRRORED) and loads
  // them; rolled back with NVS still empty, the release loads them too.
  {
    NorFlash f;Store s;assert(s.open(f)==Log::Open::Formatted);
    const auto h=pulls(11);Appearance::Bests b;b.observe(0,0,true,9800);const auto p=powerLog(2);
    assert(s.saveHistory(h,h.generation()) && s.saveBests(b,b.generation()) && s.savePower(p,p.checksum) && s.disown()==3);
    const auto old=releasedBoot<R>(f,false,0,0,0);
    assert(old.history==Source::Log && old.historyImage==bytes(image(h)) && old.bests==Source::Log && old.bestsImage==bytes(image(b)) && old.powerCount==p.count);
  }
  std::cout<<"  rolled back to "<<name<<": this firmware's records load as before, and the empty ones over wiped records keep them wiped\n";
}

template<class R> void field(const char *name) {
  const auto h=pulls(30);Appearance::Bests b;b.observe(0,0,true,9600);const auto p=powerLog(5);
  const auto load=[&](NorFlash &f,Nvs nvs,uint32_t nh,uint32_t nb,uint32_t np,Source want) {
    Store s;assert(s.open(f)==Log::Open::Ready);
    PracticeHistory back;Appearance::Bests bb;PowerDiagnostics::Image pb;uint32_t mh=0,mb=0,mp=0;
    assert(s.loadHistory(back,mh) && s.loadBests(bb,mb) && s.loadPower(pb,mp));
    assert(image(back)==image(h) && image(bb)==image(b) && pb.count==p.count);
    assert(HistoryLog::pick(true,mh,nvs,nh)==want && HistoryLog::pick(true,mb,nvs,nb)==want && HistoryLog::pick(true,mp,nvs,np)==want);
  };
  // NVS intact at what the release last mirrored: the log, with every pull it holds.
  {NorFlash f;releasedSave<R>(f,h,h.generation()-3,b,b.generation(),p,p.checksum);
   load(f,Nvs::Found,h.generation()-3,b.generation(),p.checksum,Source::Log);}
  // NVS refused every copy (full): the release wrote NOT_MIRRORED, NVS holds none, the log loads. Nothing lost.
  {NorFlash f;releasedSave<R>(f,h,NOT_MIRRORED,b,NOT_MIRRORED,p,NOT_MIRRORED);
   load(f,Nvs::Absent,0,0,0,Source::Log);load(f,Nvs::Unreadable,0,0,0,Source::Log);}
  // NVS took a copy after the record (a sleep), or an older firmware saved there: NVS, as before.
  {NorFlash f;releasedSave<R>(f,h,h.generation()-3,b,b.generation()-1,p,p.checksum^1);
   load(f,Nvs::Found,h.generation(),b.generation(),p.checksum,Source::Nvs);}
  // The defect, on records already in the field: Wipe all on 0.15-0.17 erased NVS, which they said held its copy.
  {NorFlash f;releasedSave<R>(f,h,h.generation(),b,b.generation(),p,p.checksum);
   load(f,Nvs::Absent,0,0,0,Source::None);}
  std::cout<<"  records "<<name<<" wrote: load as there with NVS intact, refused or newer; none after an old firmware's Wipe all\n";
}
}

int main() {
  rollback<R018>("0.18.0");rollback<R0208>("0.20.8-0.20.10");
  field<R018>("0.18.0");field<R0208>("0.20.8-0.20.10");
  std::cout<<"PASS: the history log format is unchanged: 0.18.0 and 0.20.8-0.20.10 read this firmware's records and pick as before, "
             "the empty records logged over wiped ones keep them wiped there too, and their records in the field load here as there, "
             "except a record NVS lost to an old firmware's Wipe all\n";
}
