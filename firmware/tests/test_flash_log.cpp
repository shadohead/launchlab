// The history log in the spiffs partition (flash_log.h, history_log.h) on a
// NOR flash model: program only clears bits, erase sets whole sectors to
// 0xFF, and a power loss can stop any write after any byte.
#include "../LaunchLabMini/flash_chunks.h"
#include "../LaunchLabMini/history_log.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

#include "nor_flash.h"

// Counts default-heap allocations while `counting` is set.
static bool counting=false;static unsigned allocations=0;
void *operator new(std::size_t n){if(counting)++allocations;if(void *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](std::size_t n){return operator new(n);}
void *operator new(std::size_t n,const std::nothrow_t &)noexcept{if(counting)++allocations;return std::malloc(n?n:1);}
void *operator new[](std::size_t n,const std::nothrow_t &t)noexcept{return operator new(n,t);}
void operator delete(void *p)noexcept{std::free(p);}
void operator delete[](void *p)noexcept{std::free(p);}
void operator delete(void *p,std::size_t)noexcept{std::free(p);}
void operator delete[](void *p,std::size_t)noexcept{std::free(p);}

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
    using HistoryLog::Nvs;using HistoryLog::NOT_MIRRORED;
    // No log yet (a first boot of 0.18 after 0.15-0.17, data only in NVS): NVS. Neither: none.
    assert(pick(false,0,Nvs::Found,5)==Source::Nvs && pick(false,0,Nvs::Absent,0)==Source::None && pick(false,0,Nvs::Unreadable,0)==Source::None);
    // NVS still at what was last mirrored: the log holds newer pulls (an unclean power loss after the mirror,
    // or NVS full and refusing the next copy).
    assert(pick(true,40,Nvs::Found,40)==Source::Log);
    // 0.18 -> 0.17 -> pulls -> 0.18: 0.17 saved into NVS after the last mirror, so NVS is newer,
    // whichever generation each firmware reached.
    assert(pick(true,40,Nvs::Found,41)==Source::Nvs && pick(true,40,Nvs::Found,12)==Source::Nvs);
    // NVS without the record. The log says NVS held none: NVS refused every copy (full), the log is the
    // only one. The log says NVS held one: NVS was erased behind this firmware's back (Wipe all on
    // 0.15-0.17 after a rollback), so the wiped record stays wiped. NVS that cannot be read proves nothing.
    assert(pick(true,NOT_MIRRORED,Nvs::Absent,0)==Source::Log && pick(true,40,Nvs::Absent,0)==Source::None);
    assert(pick(true,40,Nvs::Unreadable,0)==Source::Log && pick(true,NOT_MIRRORED,Nvs::Unreadable,0)==Source::Log);
    // The whole sequence through the stores: 0.18 mirrors generation G, saves more to the log,
    // a rollback adds pulls in NVS only, then 0.18 again.
    Nor f;LogStore s;assert(s.open(f)==Log::Open::Formatted);
    PracticeHistory nvs=pulls(10);const uint32_t mirroredGen=nvs.generation();
    PracticeHistory logged=nvs;assert(logged.accept(50,true,9500,99000));assert(s.saveHistory(logged,mirroredGen));
    PracticeHistory back;uint32_t m=0;assert(s.loadHistory(back,m) && pick(true,m,Nvs::Found,nvs.generation())==Source::Log && same(back,logged));
    PracticeHistory older=nvs;assert(older.accept(60,true,8800,120000));  // the rollback's own save, NVS only
    LogStore reboot;assert(reboot.open(f)==Log::Open::Ready && reboot.loadHistory(back,m));
    assert(pick(true,m,Nvs::Found,older.generation())==Source::Nvs);
  }
  // Wipe all on a rolled-back 0.15-0.17 erases NVS only (they do not know the log); upgraded again
  // before any pull, boot must not bring back the wiped history, bests or power log. Each record was
  // logged after NVS held its copy, as every save after the first sleep is.
  {
    using HistoryLog::Nvs;using HistoryLog::NOT_MIRRORED;
    Nor f;LogStore s;assert(s.open(f)==Log::Open::Formatted);
    const PracticeHistory h=pulls(12);Appearance::Bests bests;bests.observe(0,0,true,9300);bests.observe(1,0,true,8100);
    PowerDiagnostics::Image power;PowerDiagnostics::fresh(power);PowerDiagnostics::append(power,PowerDiagnostics::Sample,3900,0,30,80);
    power.checksum=PowerDiagnostics::hash(power);
    assert(s.saveHistory(h,h.generation()) && s.saveBests(bests,bests.generation()) && s.savePower(power,power.checksum));
    // The rollback, Wipe all there, the upgrade: NVS opens and holds none of the three.
    LogStore boot;assert(boot.open(f)==Log::Open::Ready);
    PracticeHistory back;Appearance::Bests b;PowerDiagnostics::Image p;uint32_t mh=0,mb=0,mp=0;
    assert(boot.loadHistory(back,mh) && boot.loadBests(b,mb) && boot.loadPower(p,mp));
    if(pick(true,mh,Nvs::Absent,0)!=Source::None)std::cerr<<"wiped history resurrected: NVS erased, log mirrored="<<mh<<"\n";
    assert(pick(true,mh,Nvs::Absent,0)==Source::None && pick(true,mb,Nvs::Absent,0)==Source::None && pick(true,mp,Nvs::Absent,0)==Source::None);
    // Boot logs the empty records over them (mirrored: NVS holds none), so the next boot, and 0.18-0.20.10
    // after another rollback (which loads the log when NVS holds none), load nothing.
    PowerDiagnostics::Image fresh;PowerDiagnostics::fresh(fresh);fresh.checksum=PowerDiagnostics::hash(fresh);
    assert(boot.saveHistory(PracticeHistory(),NOT_MIRRORED) && boot.saveBests(Appearance::Bests(),NOT_MIRRORED) && boot.savePower(fresh,NOT_MIRRORED));
    LogStore again;assert(again.open(f)==Log::Open::Ready && again.loadHistory(back,mh) && again.loadBests(b,mb) && again.loadPower(p,mp));
    assert(pick(true,mh,Nvs::Absent,0)==Source::Log && back.size()==0 && b.generation()==0 && b.rpm[0]==0 && p.count==0);
    assert(pick(true,mb,Nvs::Absent,0)==Source::Log && pick(true,mp,Nvs::Absent,0)==Source::Log);
    // A pull on the old firmware after the wipe (NVS holds a new record) was already safe: NVS differs.
    assert(pick(true,h.generation(),Nvs::Found,1)==Source::Nvs);
  }
  // NVS refused the copies (full, as one StickS3 was): the log is newer and loads, whether NVS held an
  // older copy, held none, or could not even open the namespace.
  {
    using HistoryLog::Nvs;using HistoryLog::NOT_MIRRORED;
    Nor f;LogStore s;assert(s.open(f)==Log::Open::Formatted);
    // Never a copy in NVS: every record says NOT_MIRRORED (what 0.18-0.20.10 wrote too: an empty store's generation).
    PracticeHistory h=pulls(5);assert(s.saveHistory(h,NOT_MIRRORED));
    PracticeHistory back;uint32_t m=1;LogStore r;assert(r.open(f)==Log::Open::Ready && r.loadHistory(back,m) && m==NOT_MIRRORED);
    assert(pick(true,m,Nvs::Absent,0)==Source::Log && same(back,h) && pick(true,m,Nvs::Unreadable,0)==Source::Log);
    // An older copy G in NVS, the next refused: the record says G, NVS still holds G.
    const uint32_t g=h.generation();assert(h.accept(90,true,9900,500000) && s.saveHistory(h,g));
    LogStore r2;assert(r2.open(f)==Log::Open::Ready && r2.loadHistory(back,m) && m==g && pick(true,m,Nvs::Found,g)==Source::Log && same(back,h));
  }
  // A record logged before NVS took its first copy says NOT_MIRRORED, and would still say so after
  // that copy: an erase then could not be told from NVS refusing every copy. So the first copy NVS
  // takes is logged again at once (flushForShutdown, savePowerLog; check_save_paths runs the sketch's).
  {
    using HistoryLog::Nvs;using HistoryLog::NOT_MIRRORED;
    Nor f;LogStore s;assert(s.open(f)==Log::Open::Formatted);
    const PracticeHistory h=pulls(3);assert(s.saveHistory(h,NOT_MIRRORED));  // first session, NVS empty
    PracticeHistory back;uint32_t m=0;
    {LogStore r;assert(r.open(f)==Log::Open::Ready && r.loadHistory(back,m) && pick(true,m,Nvs::Absent,0)==Source::Log);}  // without the re-log
    assert(s.saveHistory(h,h.generation()));  // NVS took generation 3: logged again
    LogStore r;assert(r.open(f)==Log::Open::Ready && r.loadHistory(back,m) && m==h.generation());
    assert(pick(true,m,Nvs::Found,h.generation())==Source::Log && pick(true,m,Nvs::Absent,0)==Source::None);
  }
  // NVS lost, not wiped (the core erased the partition at boot: no namespace left): disown() logs each
  // record again saying NOT_MIRRORED, payload unchanged, so they load. Records already saying it are not
  // logged again, and a power cut part way leaves each record whole: the next boot finishes the job.
  {
    using HistoryLog::Nvs;using HistoryLog::NOT_MIRRORED;
    Nor base;LogStore s;assert(s.open(base)==Log::Open::Formatted);
    const PracticeHistory h=pulls(9);Appearance::Bests bests;bests.observe(0,0,true,9700);
    PowerDiagnostics::Image power;PowerDiagnostics::fresh(power);PowerDiagnostics::append(power,PowerDiagnostics::Sample,3900,0,30,80);
    power.checksum=PowerDiagnostics::hash(power);
    assert(s.saveHistory(h,h.generation()) && s.saveBests(bests,bests.generation()) && s.savePower(power,NOT_MIRRORED));
    unsigned cuts=0;
    for(long budget=0;budget<long(HISTORY_BYTES)+400;budget+=53,++cuts) {
      Nor f=base;LogStore t;assert(t.open(f)==Log::Open::Ready);
      f.budget=budget;t.disown();f.restore();
      LogStore r;assert(r.open(f)==Log::Open::Ready);
      const int n=r.disown();assert(n>=0 && n<=2);
      LogStore again;assert(again.open(f)==Log::Open::Ready && again.disown()==0);
      PracticeHistory back;Appearance::Bests b;PowerDiagnostics::Image p;uint32_t mh=1,mb=1,mp=1;
      assert(again.loadHistory(back,mh) && again.loadBests(b,mb) && again.loadPower(p,mp));
      assert(mh==NOT_MIRRORED && mb==NOT_MIRRORED && mp==NOT_MIRRORED && same(back,h) && b.rpm[0]==bests.rpm[0] && p.count==power.count);
      assert(pick(true,mh,Nvs::Absent,0)==Source::Log && pick(true,mb,Nvs::Absent,0)==Source::Log);
    }
    {Nor f=base;LogStore t;assert(t.open(f)==Log::Open::Ready && t.disown()==2);}  // the power record already said NOT_MIRRORED
    std::cout<<"  "<<cuts<<" power-loss points in a disown: every record whole, the next disown finishes\n";
  }
  // Saves take nothing from the default heap: on the device that is internal
  // RAM for buffers this size, and a history save while a song streamed took
  // 5.4 KB of it, below the 32 KB DMA floor (0.20 HIL songs, 2026-10-23).
  // Their buffers come from PSRAM there. (Loads run at boot and restore.)
  {
    Nor f;LogStore s;assert(s.open(f)==Log::Open::Formatted);
    const auto h=pulls(40);Appearance::Bests b;PowerDiagnostics::Image power;PowerDiagnostics::fresh(power);
    counting=true;allocations=0;
    const bool ok=s.saveHistory(h,1) && s.saveBests(b,2) && s.savePower(power,3);
    counting=false;
    if(allocations)std::cerr<<allocations<<" default-heap allocations in the saves\n";
    assert(ok && allocations==0);
    PracticeHistory h2;PowerDiagnostics::Image p2;uint32_t m=0;
    assert(s.loadHistory(h2,m) && m==1 && same(h,h2) && s.loadPower(p2,m) && m==3 && p2.count==power.count);
  }
  // A write from outside internal RAM goes through the internal chunk, in
  // pieces that end on 256-byte pages (so ESP-IDF writes each directly with
  // whole programs, not 32 bytes at a time with the cache off for each: a
  // history save from PSRAM took 30 ms instead of 13, 0.20 gate 2026-10-23);
  // one from internal RAM is written as it is.
  {
    std::vector<uint8_t> src(2672),flash(8192,0xff);
    for(size_t i=0;i<src.size();++i)src[i]=uint8_t(i*37+11);
    struct Call {uint32_t at;const void *p;size_t n;};std::vector<Call> calls;
    auto write=[&](uint32_t at,const void *p,size_t n){calls.push_back({at,p,n});std::memcpy(flash.data()+at,p,n);return true;};
    const uint32_t at=4096+48;  // a record's payload: 16-byte aligned, not page aligned
    assert(FlashChunks::write(at,src.data(),src.size(),false,write));
    assert(std::equal(src.begin(),src.end(),flash.begin()+at));
    size_t covered=0;
    for(const auto &c:calls) {
      assert(c.p!=src.data()+(c.at-at) && c.n<=FlashChunks::BYTES && c.at/256==(c.at+c.n-1)/256 && c.at==at+covered);
      covered+=c.n;
    }
    assert(covered==src.size() && calls.size()==11);
    calls.clear();
    assert(FlashChunks::write(at,src.data(),src.size(),true,write) && calls.size()==1 && calls[0].p==src.data() && calls[0].n==src.size());
    // A failed piece stops the write and is reported.
    unsigned pieces=0;
    assert(!FlashChunks::write(at,src.data(),src.size(),false,[&](uint32_t,const void *,size_t){return ++pieces<3;}) && pieces==3);
  }
  // Battle records (rival_book.h): a fourth record type, kept in the log
  // alone. Its save is program-only and takes no default heap, it loads after
  // any number of other saves (its sector is never erased), and a record that
  // does not read as one leaves the book as it was.
  {
    Nor f;LogStore s;assert(s.open(f)==Log::Open::Formatted);
    Battle::Book book,none;
    assert(!s.loadRivals(none) && !none.count());
    const uint32_t kai=0x11110001;
    book.begin(kai,1,2,6420,5980);book.rename(kai,"Kai");book.claim(kai,Battle::Outcome::Won,Battle::Finish::Xtreme);
    Battle::Book::Image saved;book.encode(saved);
    counting=true;allocations=0;const unsigned erases=f.erases;
    const bool ok=s.saveRivals(book);
    counting=false;
    assert(ok && !allocations && f.erases==erases);
    PracticeHistory h;uint32_t next=1;
    for(unsigned save=0;save<200;++save) {
      assert(h.accept(next,true,7000+float(next%500),next*1000));++next;
      assert(s.saveHistory(h,3));
      while(s.readyAhead()){}
    }
    {
      LogStore r;assert(r.open(f)==Log::Open::Ready);
      Battle::Book back;Battle::Book::Image image;assert(r.loadRivals(back));back.encode(image);assert(image==saved);
      PracticeHistory history;uint32_t m=0;assert(r.loadHistory(history,m) && same(history,h));
    }
    // A newer save replaces it.
    book.begin(kai,3,4,6000,6100);assert(s.saveRivals(book));
    book.encode(saved);
    // The firmware before battles (three record types) on this log: it reads
    // its own records, takes the battle record for a torn one and carries on
    // in a new sector, and what it saves is what the newer firmware loads.
    using Earlier=FlashLog<Nor,4>;
    std::vector<uint8_t> payload(HISTORY_BYTES);
    {
      Earlier old;assert(old.open(f)==Earlier::Open::Ready);
      const auto found=old.newest(HistoryLog::History);
      assert(found.found && found.length==HISTORY_BYTES && !old.newest(HistoryLog::Rivals).found);
      assert(old.readPayload(found,payload.data(),HISTORY_BYTES));
      PracticeHistory theirs;assert(theirs.decode(payload.data()+4,PracticeHistory::IMAGE_BYTES) && same(theirs,h));
      assert(!old.append(HistoryLog::Rivals,saved.data(),saved.size()));
      // One more pull, saved by the earlier firmware.
      assert(h.accept(next,true,7100,next*1000));
      PracticeHistory::Image image;h.encode(image);std::memcpy(payload.data()+4,image.data(),image.size());
      assert(old.append(HistoryLog::History,payload.data(),HISTORY_BYTES));
    }
    {
      LogStore r;assert(r.open(f)==Log::Open::Ready);
      PracticeHistory history;uint32_t m=0;assert(r.loadHistory(history,m) && same(history,h));
      Battle::Book back;Battle::Book::Image image;assert(r.loadRivals(back));back.encode(image);assert(image==saved);
    }
    // It does not keep the battle record's sector out of its rotation: after
    // the log has gone round under it the record is gone. Nothing else is
    // lost, and the newer firmware starts an empty book and saves again.
    {
      Earlier old;assert(old.open(f)==Earlier::Open::Ready);
      for(unsigned save=0;save<40;++save){assert(old.append(HistoryLog::History,payload.data(),HISTORY_BYTES));while(old.readyAhead()){}}
    }
    {
      LogStore r;assert(r.open(f)==Log::Open::Ready);
      PracticeHistory history;uint32_t m=0;assert(r.loadHistory(history,m) && same(history,h));
      Battle::Book back;back.begin(7,1,1,1,1);
      assert(!r.loadRivals(back) && back.count()==1 && back.find(7));
      assert(r.saveRivals(book));
      LogStore again;assert(again.open(f)==Log::Open::Ready);
      Battle::Book reloaded;Battle::Book::Image image;assert(again.loadRivals(reloaded));reloaded.encode(image);assert(image==saved);
    }
    // A record of another size or with a byte changed is not a book.
    {
      Nor g;LogStore t;assert(t.open(g)==Log::Open::Formatted);
      Battle::Book::Image bad=saved;bad[40]^=1;
      assert(t.log().append(HistoryLog::Rivals,bad.data(),bad.size()));
      Battle::Book back;back.begin(7,1,1,1,1);
      assert(!t.loadRivals(back) && back.count()==1);
      assert(t.log().append(HistoryLog::Rivals,saved.data(),saved.size()-16) && !t.loadRivals(back) && back.count()==1);
      assert(t.saveRivals(book) && t.loadRivals(back) && back.count()==1 && back.find(kai));
    }
  }
  std::cout<<"PASS: the history log formats a blank or foreign partition touching only its own sectors, leaves a newer layout alone, "
             "wraps evenly with program-only saves, survives a power loss anywhere in a save or erase, newest wins against the NVS mirror, "
             "a record NVS lost to an old firmware's Wipe all stays wiped while one NVS refused or the core erased still loads, "
             "saves take no default (internal) heap, writes from PSRAM go through page-aligned internal chunks, and battle records "
             "load after any number of saves and are passed over by the firmware before them\n";
}
