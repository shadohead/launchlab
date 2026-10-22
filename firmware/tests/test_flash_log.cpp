// The history log in the spiffs partition (flash_log.h, history_log.h) on a
// NOR flash model: program only clears bits, erase sets whole sectors to
// 0xFF, and a power loss can stop any write after any byte.
#include "../LaunchLabMini/history_log.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "nor_flash.h"

namespace {
using Nor=NorFlash;
using LogStore=HistoryLog::Store<Nor>;
using Log=FlashLog<Nor>;

PracticeHistory pulls(unsigned n,uint32_t from=1) {
  PracticeHistory h;
  for(unsigned i=0;i<n;++i)assert(h.accept(from+i,true,8000+37.0f*float(i),1000*(from+i)));
  return h;
}
bool same(const PracticeHistory &a,const PracticeHistory &b) {
  PracticeHistory::Image x,y;a.encode(x);b.encode(y);return x==y;
}
std::vector<uint8_t> region(const Nor &f,uint32_t at,uint32_t n){return {f.bytes.begin()+at,f.bytes.begin()+at+n};}
}

int main() {
  using HistoryLog::pick;using HistoryLog::Source;using HistoryLog::HISTORY_BYTES;
  // A blank partition is formatted: superblock 0 and the history sectors,
  // each with an erase count; nothing else is written or erased.
  {
    Nor f;LogStore s;assert(s.open(f)==Log::Open::Formatted && s.isOpen());
    FlashLayout::Superblock sb;assert(sb.decode(f.bytes.data()) && sb.count==1 && sb.find(FlashLayout::History) &&
      sb.find(FlashLayout::History)->offset==FlashLayout::HISTORY_OFFSET && !sb.find(FlashLayout::Effects));
    assert(s.log().readyCount()==s.log().sectors() && s.log().sectors()==32 && s.log().eraseCount(0)==1);
    for(uint32_t at=FlashLayout::PULLS_OFFSET;at<FlashLayout::PARTITION_BYTES;++at)assert(f.bytes[at]==0xff);
    // Saved, then read back after a reboot.
    const auto h=pulls(40);assert(s.saveHistory(h,7));
    LogStore again;assert(again.open(f)==Log::Open::Ready);
    PracticeHistory back;uint32_t mirrored=0;assert(again.loadHistory(back,mirrored) && same(back,h) && mirrored==7);
  }
  // Another firmware's leftovers (random bytes everywhere): formatted the
  // same way, and the regions this firmware does not own stay as they were.
  {
    Nor f;std::srand(18);for(auto &b:f.bytes)b=uint8_t(std::rand());
    const auto pullsBefore=region(f,FlashLayout::PULLS_OFFSET,FlashLayout::PULLS_BYTES+FlashLayout::EFFECTS_BYTES);
    LogStore s;assert(s.open(f)==Log::Open::Formatted);
    assert(region(f,FlashLayout::PULLS_OFFSET,FlashLayout::PULLS_BYTES+FlashLayout::EFFECTS_BYTES)==pullsBefore);
    PracticeHistory none;uint32_t m=0;assert(!s.loadHistory(none,m));
    assert(s.saveHistory(pulls(5),1));
  }
  // A newer layout version, or a valid table without a history region: left alone, the log closed.
  {
    for(int variant=0;variant<2;++variant) {
      Nor f;FlashLayout::Superblock sb=FlashLayout::historyLayout(4);
      if(variant==0)sb.version=2;else sb.regions[0]={FlashLayout::Effects,0,FlashLayout::EFFECTS_OFFSET,FlashLayout::EFFECTS_BYTES};
      FlashLayout::Superblock::Image b;sb.encode(b);std::copy(b.begin(),b.end(),f.bytes.begin()+FlashLayout::SUPER_OFFSET[1]);
      const auto before=f.bytes;LogStore s;
      assert(s.open(f)==Log::Open::Unsupported && !s.isOpen() && f.bytes==before && !s.saveHistory(pulls(1),0));
    }
  }
  // The newer of two valid superblocks wins; a torn copy is ignored.
  {
    Nor f;LogStore s;assert(s.open(f)==Log::Open::Formatted);
    FlashLayout::Superblock sb=FlashLayout::historyLayout(9);sb.count=2;sb.regions[1]={FlashLayout::Effects,0,FlashLayout::EFFECTS_OFFSET,FlashLayout::EFFECTS_BYTES};
    FlashLayout::Superblock::Image b;sb.encode(b);
    f.bytes[FlashLayout::SUPER_OFFSET[1]+20]=0;  // a stray byte in an erased copy is not a superblock either
    std::copy(b.begin(),b.end(),f.bytes.begin()+FlashLayout::SUPER_OFFSET[1]);
    LogStore t;assert(t.open(f)==Log::Open::Ready);
    b[30]^=1;std::copy(b.begin(),b.end(),f.bytes.begin()+FlashLayout::SUPER_OFFSET[1]);  // torn: CRC fails
    LogStore u;assert(u.open(f)==Log::Open::Ready);
  }
  // Months of saves: the log wraps through every sector many times, the
  // newest of each type always loads (a rarely written type included, its
  // sector never erased), wear stays even, and every save is program-only
  // while three sectors stay erased ahead.
  {
    Nor f;LogStore s;assert(s.open(f)==Log::Open::Formatted);
    Appearance::Bests bests;bests.observe(0,0,true,9100);assert(s.saveBests(bests,0));
    PowerDiagnostics::Image power;PowerDiagnostics::fresh(power);
    PracticeHistory h;uint32_t next=1;
    for(unsigned save=0;save<2000;++save) {
      for(unsigned k=0;k<3;++k)assert(h.accept(next,true,7000+float(next%500),next*1000)),++next;
      const unsigned erases=f.erases;
      assert(s.saveHistory(h,3));
      assert(f.erases==erases);  // never erases on the save path
      if(save%12==0){PowerDiagnostics::append(power,PowerDiagnostics::Sample,3900,0,30,80);assert(s.savePower(power,0));}
      while(s.readyAhead()){}
      assert(s.log().readyCount()>=Log::READY_AHEAD);
      if(save%250==0) {
        LogStore r;assert(r.open(f)==Log::Open::Ready);
        PracticeHistory back;uint32_t m=0;assert(r.loadHistory(back,m) && same(back,h) && m==3);
        Appearance::Bests b;assert(r.loadBests(b,m) && b.rpm[0]==9100);
        PowerDiagnostics::Image p;assert(r.loadPower(p,m) && p.count==power.count);
      }
    }
    unsigned low=~0u,high=0;
    for(unsigned i=0;i<s.log().sectors();++i){low=std::min(low,s.log().eraseCount(i));high=std::max(high,s.log().eraseCount(i));}
    // One sector keeps the only bests record; every other one wears evenly.
    unsigned others=0;for(unsigned i=0;i<s.log().sectors();++i)others+=s.log().eraseCount(i)>high-3;
    assert(high>=60 && others>=s.log().sectors()-1);
    std::cout<<"  2000 saves: erase counts "<<low<<"-"<<high<<" per sector\n";
  }
  // A power loss at every point of a save: the previous record loads, and
  // the next save works.
  {
    Nor base;LogStore s;assert(s.open(base)==Log::Open::Formatted);
    const auto first=pulls(20);assert(s.saveHistory(first,1));
    const auto second=pulls(30);
    unsigned cuts=0;
    for(long budget=0;budget<long(HISTORY_BYTES)+64;budget+=7,++cuts) {
      Nor f=base;LogStore t;assert(t.open(f)==Log::Open::Ready);
      f.budget=budget;const bool saved=t.saveHistory(second,1);f.restore();
      LogStore r;assert(r.open(f)==Log::Open::Ready);
      PracticeHistory back;uint32_t m=0;assert(r.loadHistory(back,m));
      assert(same(back,saved?second:first));
      const auto third=pulls(35);assert(r.saveHistory(third,1));
      LogStore again;assert(again.open(f)==Log::Open::Ready);assert(again.loadHistory(back,m) && same(back,third));
    }
    std::cout<<"  "<<cuts<<" power-loss points in a save: the previous record or the new one, never neither\n";
  }
  // A power loss during an erase or a sector header: that sector is erased
  // again before use, with its erase count carried on.
  {
    Nor base;LogStore s;assert(s.open(base)==Log::Open::Formatted);
    PracticeHistory h=pulls(10);
    for(unsigned i=0;i<40;++i){assert(h.accept(100+i,true,9000,i));assert(s.saveHistory(h,0));}
    // The oldest used sectors are now reclaimable.
    for(long budget:{0L,3L,9L}) {
      Nor f=base;LogStore t;assert(t.open(f)==Log::Open::Ready);
      f.budget=budget;t.readyAhead();f.restore();
      LogStore r;assert(r.open(f)==Log::Open::Ready);
      PracticeHistory back;uint32_t m=0;assert(r.loadHistory(back,m) && same(back,h));
      while(r.readyAhead()){}
      assert(r.log().readyCount()>=Log::READY_AHEAD && r.saveHistory(h,0));
    }
  }
  // Newest wins between the log and NVS (the mirror 0.15-0.17 read).
  {
    // No log yet (a first boot of 0.18): NVS. No NVS: the log.
    assert(pick(false,0,true,5)==Source::Nvs && pick(true,5,false,0)==Source::Log && pick(false,0,false,0)==Source::None);
    // NVS still at what was last mirrored: the log holds newer pulls (an unclean power loss after the mirror).
    assert(pick(true,40,true,40)==Source::Log);
    // 0.18 -> 0.17 -> pulls -> 0.18: 0.17 saved into NVS after the last mirror, so NVS is newer,
    // whichever generation each firmware reached.
    assert(pick(true,40,true,41)==Source::Nvs && pick(true,40,true,12)==Source::Nvs);
    // The whole sequence through the stores: 0.18 mirrors generation G, saves more to the log,
    // a rollback adds pulls in NVS only, then 0.18 again.
    Nor f;LogStore s;assert(s.open(f)==Log::Open::Formatted);
    PracticeHistory nvs=pulls(10);const uint32_t mirroredGen=nvs.generation();
    PracticeHistory logged=nvs;assert(logged.accept(50,true,9500,99000));assert(s.saveHistory(logged,mirroredGen));
    PracticeHistory back;uint32_t m=0;assert(s.loadHistory(back,m) && pick(true,m,true,nvs.generation())==Source::Log && same(back,logged));
    PracticeHistory older=nvs;assert(older.accept(60,true,8800,120000));  // the rollback's own save, NVS only
    LogStore reboot;assert(reboot.open(f)==Log::Open::Ready && reboot.loadHistory(back,m));
    assert(pick(true,m,true,older.generation())==Source::Nvs);
  }
  std::cout<<"PASS: the history log formats a blank or foreign partition touching only its own sectors, leaves a newer layout alone, "
             "wraps evenly with program-only saves, survives a power loss anywhere in a save or erase, and newest wins against the NVS mirror\n";
}
