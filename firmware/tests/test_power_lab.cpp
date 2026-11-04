// Battery lab run log (power_lab.h) on the NOR flash model: profiles parse
// and round-trip, records survive reboots and power loss mid-write, and a
// previous run's records never read as the new run's.
#include "../LaunchLabMini/power_lab.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

#include "nor_flash.h"

using namespace PowerLab;

static Header run(uint32_t id,unsigned profiles=1) {
  Header h;h.runId=id;h.segmentS=600;h.count=uint8_t(profiles);
  for(unsigned i=0;i<profiles;++i){h.profiles[i].backlight=int8_t(10*(i+1));h.profiles[i].adc=uint8_t(i%3);}
  return h;
}
static Record sample(uint32_t s,uint16_t mv,uint8_t flags=ChargeKnown|Sampling|DisplayOn) {
  Record r;r.seconds=s;r.batteryMv=mv;r.frames=uint16_t(s%500);r.profile=uint8_t(s/600%4);r.flags=flags;
  r.backlight=77;r.cpuLowPct=40;r.tempC=-3;r.boots=1;r.loops=s/RECORD_S;r.rows=s*7;r.minMv=uint16_t(mv-3);r.maxMv=uint16_t(mv+4);r.vinMv=5012;return r;
}

int main() {
  {  // Profiles: the text form, ranges, unknown letters, round trip.
    Knobs k;
    assert(parse("b30 a1 c1 f10",k) && k.backlight==30 && k.adc==1 && k.cpu==1 && k.frameMs==10 && k.imuMs==0);
    assert(parse("b- a2",k) && k.backlight==-1 && k.adc==2);
    assert(parse("",k) && k==Knobs());
    assert(parse("b0,a0",k) && k.backlight==0);
    assert(parse("c2 i40 d20 l5 r10 p1",k) && k.cpu==2 && k.imuMs==40 && k.dimS==20 && k.dimLevel==5 && k.restS==10 && k.push==1);
    assert(parse("i0",k) && k.imuMs==0);
    Knobs before=k;
    for(const char *bad:{"b101","a3","c3","x1","b","f256","b-5x","i10","i41","l27","p2","s1","d256"})assert(!parse(bad,k) && k==before);
    char text[64];Knobs back;
    assert(parse("b45 a1 c2 f7 i30 d15 l4 r12 p1",k) && format(k,text,sizeof(text))>0 && parse(text,back) && back==k);
    assert(!std::strcmp(text,"b45 a1 c2 f7 i30 d15 l4 r12 p1"));
    format(Knobs(),text,sizeof(text));assert(!std::strcmp(text,"b- a0 c0 f0 i0 d0 l0 r0 p0"));
  }
  {  // Every knob of every profile survives the header round trip.
    Header h=run(9,MAX_PROFILES);
    for(unsigned i=0;i<MAX_PROFILES;++i)assert(parse("c2 i40 d20 l5 r10 p1",h.profiles[i]) && (h.profiles[i].dimS=uint8_t(i+1)));
    uint8_t b[Header::BYTES];h.encode(b);Header back;
    assert(back.decode(b) && back.count==MAX_PROFILES);
    for(unsigned i=0;i<MAX_PROFILES;++i)assert(back.profiles[i]==h.profiles[i] && back.profiles[i].dimS==i+1);
    b[4]=1;put32(b+Header::BYTES-4,crc32(b,Header::BYTES-4));
    assert(!Header().decode(b));  // a version 1 header (8-byte knobs) is never read as version 2
  }
  {  // The rotation moves on every segment and wraps.
    Header h=run(1,3);
    assert(h.profileAt(0)==0 && h.profileAt(599)==0 && h.profileAt(600)==1 && h.profileAt(1799)==2 && h.profileAt(1800)==0);
    Header one=run(1,1);assert(one.profileAt(123456)==0);
    Header bad=run(1,1);bad.segmentS=5;assert(!bad.valid());
    bad=run(0,1);assert(!bad.valid());
    bad=run(1,1);bad.count=MAX_PROFILES+1;assert(!bad.valid());
  }
  {  // Blank flash: no run. A run survives a reboot with every record, in order.
    NorFlash f;Log<NorFlash> log(f);
    assert(!log.begin() && !log.hasRun() && !log.open());
    assert(log.start(run(1762200000,4)) && log.open());
    for(uint32_t i=0;i<300;++i)assert(log.append(sample(i*RECORD_S,uint16_t(4150-i))));
    Log<NorFlash> again(f);
    assert(again.begin() && again.open() && again.count()==300 && again.header().count==4 &&
      again.header().profiles[1].backlight==20 && again.header().profiles[2].adc==2);
    Record r,last;uint32_t slot=0;
    for(uint32_t i=0;i<300;++i)assert(again.next(slot,r) && r.seconds==i*RECORD_S && r.batteryMv==4150-i && r.tempC==-3 && r.cpuLowPct==40 && r.loops==i && r.rows==i*RECORD_S*7);
    assert(!again.next(slot,r) && again.last(last) && last.seconds==299*RECORD_S);
    // An End record closes the run; a reboot finds it closed.
    Record end=sample(3000,3300,ChargeKnown|End);
    assert(again.append(end) && !again.open() && !again.append(sample(3010,3300)));
    Log<NorFlash> closed(f);assert(!closed.begin() && closed.hasRun() && closed.count()==301);
  }
  {  // Power loss in the middle of a record: the torn record is dropped, the run goes on.
    for(uint32_t tornAt:{10u,0u,SECTOR/RECORD_BYTES}) {
      NorFlash f;Log<NorFlash> log(f);
      assert(log.start(run(77)));
      for(uint32_t i=0;i<tornAt;++i)assert(log.append(sample(i*RECORD_S,4000)));
      f.budget=7;assert(!log.append(sample(tornAt*RECORD_S,3999)));f.restore();
      Log<NorFlash> after(f);assert(after.begin() && after.count()==tornAt);
      Record r;assert(after.last(r)==(tornAt>0) && (!tornAt || r.seconds==(tornAt-1)*RECORD_S));
      // The next records go past the torn slot (or erase its sector) and survive the next reboot.
      for(uint32_t i=0;i<3;++i)assert(after.append(sample(5000+i,3998)));
      Log<NorFlash> check(f);assert(check.begin() && check.count()==tornAt+3);
      uint32_t slot=0,n=0;while(check.next(slot,r))++n;
      assert(n==tornAt+3 && r.seconds==5002);
    }
  }
  {  // A new run over an old one: the old records further on never count.
    NorFlash f;Log<NorFlash> log(f);
    assert(log.start(run(1)));
    const uint32_t many=3*(SECTOR/RECORD_BYTES);
    for(uint32_t i=0;i<many;++i)assert(log.append(sample(i*RECORD_S,4100)));
    assert(log.start(run(2,2)) && log.count()==0);
    for(uint32_t i=0;i<SECTOR/RECORD_BYTES;++i)assert(log.append(sample(i*RECORD_S,4000)));  // fills sector 1 exactly
    Log<NorFlash> again(f);assert(again.begin() && again.count()==SECTOR/RECORD_BYTES && again.header().runId==2);
    // The next append erases sector 2 before writing into it.
    assert(again.append(sample(5000,3990)));
    Log<NorFlash> third(f);assert(third.begin() && third.count()==SECTOR/RECORD_BYTES+1);
    Record r;assert(third.last(r) && r.seconds==5000);
  }
  {  // The log fills: the last slot closes the run.
    NorFlash f;Log<NorFlash> log(f);assert(log.start(run(9)));
    for(uint32_t i=0;i<RECORDS;++i)assert(log.append(sample(i,3900)));
    assert(!log.open() && !log.append(sample(RECORDS,3900)));
    Log<NorFlash> again(f);assert(!again.begin() && again.count()==RECORDS);
  }
  {  // A corrupt header is no run.
    NorFlash f;Log<NorFlash> log(f);assert(log.start(run(5)) && log.append(sample(0,4000)));
    f.bytes[8]&=0xfe;  // run id 5 -> 4: the header CRC fails
    Log<NorFlash> bad(f);assert(!bad.begin() && !bad.hasRun());
  }
  {  // IDF 5.5's esp_pm_dump_locks() mode table.
    const char *dump=
      "Time since bootup: 1000000 us\nLock stats:\n  ll_ui  APB_FREQ_MAX  0  0  10  20  2%\n"
      "Mode stats:\nMode      CPU_freq    Time(us)    Time(%)\n"
      "SLEEP     0  M        0           0 %\n"
      "APB_MIN   40 M        734512      73%\n"
      "APB_MAX   80 M        265488      26%\n"
      "CPU_MAX   80 M        0           0 %\n";
    assert(minFrequencyUs(dump)==734512);
    assert(minFrequencyUs("Mode stats:\nAPB_MAX   80 M  5  0%\n")==-1);
    assert(minFrequencyUs("APB_MIN   40 X 5\n")==-1);
    assert(minFrequencyUs("")==-1);
  }
  std::cout<<"PASS: power lab profiles, rotation, flash run log across reboot, torn write, stale run, full log, corrupt header and PM mode parse\n";
}
