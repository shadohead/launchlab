// The Battle Pass profile's packets (docs/BLE_BATTLE_PASS.md), read back the
// way the apps written for the real BEYBATTLE PASS read them: 17 bytes, the
// type first, little-endian 16-bit fields, the latest launch in slot n-1, the
// checksum over B0-B6, and the curve as turn periods in 8 us steps.
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>
#include "ble_battle_pass.h"

using namespace BattlePass;

// An app: collects packets by type and analyses the set when it is complete,
// as the open clients do. Nothing here uses the firmware's own builders.
struct App {
  std::map<uint8_t,Packet> seen;
  unsigned statuses=0,sets=0,errors=0;
  bool attached=false,released=false;
  unsigned battery=0,best=0,total=0,latest=0,listed=0;
  double curveRpm[32]={};unsigned curvePoints=0;
  static unsigned u16(const Packet &p,unsigned at){return p[at]|(p[at+1]<<8);}
  void notify(const Packet &p) {
    if(p[0]==0xa0) {
      ++statuses;const bool now=p[3]&0x04;
      if(attached && !now)released=true;
      attached=now;battery=p[4]*100u/250u;best=u16(p,7);total=u16(p,9);return;
    }
    seen[p[0]]=p;
    if(p[0]!=0x73)return;
    for(uint8_t type=0xb0;type<=0xb7;++type)if(!seen.count(type)){++errors;return;}
    for(uint8_t type=0x70;type<=0x73;++type)if(!seen.count(type)){++errors;return;}
    unsigned sum=0;
    for(uint8_t type=0xb0;type<=0xb6;++type)for(unsigned i=1;i<17;++i)sum+=seen[type][i];
    if((sum&0xff)!=seen[0xb7][16]){++errors;seen.clear();return;}
    const unsigned n=seen[0xb6][11];
    if(n<1 || n>50){++errors;seen.clear();return;}
    listed=n;latest=u16(seen[uint8_t(0xb0+(n-1)/8)],1+((n-1)%8)*2);
    curvePoints=0;
    for(unsigned i=0;i<32;++i) {
      const unsigned value=u16(seen[uint8_t(0x70+i/8)],1+(i%8)*2);
      if(!value)break;
      curveRpm[curvePoints++]=7500000.0/value;
    }
    ++sets;seen.clear();
  }
};

int main() {
  const uint8_t id[6]={0x7c,0xe8,0xb1,0x12,0x34,0x56};
  // The names and UUIDs the apps filter on, exactly.
  assert(std::string(NAME)=="BEYBLADE_TOOL01");
  assert(std::string(SERVICE)=="55c40000-f8eb-11ec-b939-0242ac120002");
  assert(std::string(NOTIFY)=="55c4f002-f8eb-11ec-b939-0242ac120002" && std::string(WRITE)=="55c4f001-f8eb-11ec-b939-0242ac120002");

  // Status: the state at byte 3, the battery as percent * 2.5, the best and
  // the count little-endian, then the unit's ID; every other byte 0.
  {
    const Packet p=status(true,80,12345,678,id);
    assert(p[0]==0xa0 && p[1]==0 && p[2]==0 && p[3]==0x04 && p[4]==200 && p[5]==0 && p[6]==0);
    assert(p[7]==0x39 && p[8]==0x30 && p[9]==0xa6 && p[10]==0x02 && std::memcmp(p.data()+11,id,6)==0);
    assert(status(false,100,0,0,id)[3]==0x00 && status(false,100,0,0,id)[4]==250);
    assert(status(false,-1,0,0,id)[4]==0 && status(false,250,0,0,id)[4]==250);  // unknown reads empty; never past full
    App app;app.notify(p);assert(app.attached && app.battery==80 && app.best==12345 && app.total==678);
    // The button, held: 0x10 whether or not a top is on, the rest unchanged.
    const Packet held=status(false,80,12345,678,id,true);
    assert(held[3]==0x10 && status(true,80,12345,678,id,true)[3]==0x10);
    for(unsigned i=0;i<BYTES;++i)if(i!=3)assert(held[i]==p[i]);
  }
  // Shoot power is RPM in 16 bits, and a curve point is the turn's period.
  assert(shootPower(6512.4f)==6512 && shootPower(6512.6f)==6513 && shootPower(0)==0 && shootPower(-5)==0 && shootPower(1e9f)==65535);
  assert(curveValue(0)==0 && curveValue(7500)==1000 && curveValue(6000)==1250 && curveValue(100)==65535 && curveValue(65535)==114);

  // A launch's set for every count of launches kept: the app finds the latest
  // in slot n-1 and the checksum agrees.
  for(unsigned count=1;count<=SHOTS;++count) {
    Shots s;for(unsigned i=0;i<count;++i)s.add(uint16_t(3000+i*37));
    s.best=9999;s.total=uint16_t(1000+count);
    Packet list[8],turns[4];shots(s,list);
    uint16_t ms[40],rpm[40];for(unsigned i=0;i<40;++i){ms[i]=uint16_t((i+1)*10);rpm[i]=uint16_t(i<12?500+i*600:7100-(i-12)*150);}
    curve(ms,rpm,40,turns);
    App app;
    for(unsigned k=0;k<8;++k){assert(list[k][0]==0xb0+k);app.notify(list[k]);}
    for(unsigned k=0;k<4;++k){assert(turns[k][0]==0x70+k);app.notify(turns[k]);}
    assert(app.sets==1 && !app.errors && app.listed==count && app.latest==3000+(count-1)*37);
    // 400 ms in 32 spans of 12.5 ms: each point lies between the turns its
    // span covers, within the 8 us step's rounding.
    assert(app.curvePoints==32);
    for(unsigned i=0;i<32;++i) {
      const unsigned first=i*125/100,last=std::min(39u,((i+1)*125+99)/100-1);
      double low=1e9,high=0;for(unsigned j=first;j<=last;++j){low=std::min<double>(low,rpm[j]);high=std::max<double>(high,rpm[j]);}
      assert(app.curveRpm[i]>=low-high*high/7500000.0-1 && app.curveRpm[i]<=high+high*high/7500000.0+1);
    }
    // B6 also carries the best and the count; slots past the launches are 0.
    assert(get16(list[6],7)==9999 && get16(list[6],9)==1000+count && list[6][11]==count);
    for(unsigned slot=count;slot<SHOTS;++slot)assert(get16(list[slot/8],1+(slot%8)*2)==0);
    for(unsigned i=5;i<7;++i)assert(list[6][i]==0);
    for(unsigned i=12;i<17;++i)assert(list[6][i]==0);
    for(unsigned i=1;i<16;++i)assert(list[7][i]==0);
  }
  // More than 50 launches are never listed, and a flipped byte fails the checksum.
  {
    Shots s;for(unsigned i=0;i<60;++i)s.add(uint16_t(4000+i));
    assert(s.count==50 && s.power[49]==4049);
    Packet list[8],turns[4];shots(s,list);const uint16_t ms[3]={100,200,300},rpm[3]={1000,2000,3000};curve(ms,rpm,3,turns);
    list[2][5]^=0x40;
    App app;for(auto &p:list)app.notify(p);for(auto &p:turns)app.notify(p);
    assert(!app.sets && app.errors==1);
  }
  // A short curve still spans 32 points, its first turn first and its last
  // last, and no launch at all makes no valid set.
  {
    Shots s;s.add(5000);Packet list[8],turns[4];shots(s,list);const uint16_t ms[3]={100,200,300},rpm[3]={1000,2000,3000};curve(ms,rpm,3,turns);
    App app;for(auto &p:list)app.notify(p);for(auto &p:turns)app.notify(p);
    assert(app.sets==1 && app.curvePoints==32 && std::fabs(app.curveRpm[0]-1000)<2 && std::fabs(app.curveRpm[31]-3000)<2);
    Shots none;shots(none,list);App empty;for(auto &p:list)empty.notify(p);for(auto &p:turns)empty.notify(p);
    assert(!empty.sets && empty.errors==1);
  }
  // After a launch: attached, released, then the whole set, in that order, one
  // packet every GAP_MS at the fastest and never two at once.
  {
    Outbox box;App app;uint32_t now=1000;
    Shots s;s.add(6500);Packet list[8],turns[4];shots(s,list);const uint16_t ms[8]={70,100,120,135,147,157,167,179},rpm[8]={900,2000,3500,5000,6000,6500,6200,5000};curve(ms,rpm,8,turns);
    assert(box.push(status(true,90,6500,1,id)) && box.push(status(false,90,6500,1,id)));
    for(auto &p:list)assert(box.push(p));for(auto &p:turns)assert(box.push(p));
    assert(box.size()==14);
    Packet p;unsigned sent=0;uint32_t last=0;
    for(;now<2000;++now)if(box.take(now,p)){assert(!sent || now-last>=GAP_MS);last=now;++sent;app.notify(p);}
    assert(sent==14 && box.empty() && app.statuses==2 && app.released && !app.attached && app.sets==1 && app.latest==6500);
    // Full: a 17th packet is refused rather than overwriting one.
    for(unsigned i=0;i<Outbox::CAPACITY;++i)assert(box.push(p));
    assert(!box.push(p));box.clear();assert(box.empty() && !box.take(now,p));
    // The gap holds across the millisecond counter's wrap.
    Outbox wrap;wrap.push(p);wrap.push(p);
    assert(wrap.take(UINT32_MAX-3,p) && !wrap.take(UINT32_MAX,p) && !wrap.take(5,p) && wrap.take(6,p));
  }
  // What an app asks: 0x51 a status, 0x74 the set, anything else nothing. A
  // new subscription is greeted with a status GREETING_MS later.
  {
    Requests r;
    assert(!r.takeStatus(0) && !r.takeShots());
    r.command(ASK_STATUS);assert(r.takeStatus(0) && !r.takeStatus(0) && !r.takeShots());
    r.command(ASK_SHOTS);assert(r.takeShots() && !r.takeShots() && !r.takeStatus(0));
    for(unsigned byte=0;byte<256;++byte)if(byte!=ASK_STATUS && byte!=ASK_SHOTS && byte!=CLEAR && byte!=POWER_OFF)r.command(uint8_t(byte));
    assert(!r.takeStatus(0) && !r.takeShots() && !r.takeClear() && !r.takeOff());  // 0x61 among them: never acted on
    // 0x75 the clear, 0x63 the power-off, each taken once; a link's end forgets both.
    r.command(CLEAR);assert(r.takeClear() && !r.takeClear() && !r.takeShots() && !r.takeOff());
    r.command(POWER_OFF);assert(r.takeOff() && !r.takeOff() && !r.takeClear());
    r.command(CLEAR);r.command(POWER_OFF);r.clear();assert(!r.takeClear() && !r.takeOff());
    r.subscribed(500);assert(!r.takeStatus(500+GREETING_MS-1) && r.takeStatus(500+GREETING_MS) && !r.takeStatus(5000));
    r.command(ASK_SHOTS);r.subscribed(1);r.clear();assert(!r.takeShots() && !r.takeStatus(1000));
  }
  // The curve by time: 32 equal spans of the pull, each the RPM averaged over it.
  {
    Packet turns[4];
    // 64 turns of 10 ms: two turns a span, averaged.
    uint16_t ms[64],rpm[64];for(unsigned i=0;i<64;++i){ms[i]=uint16_t((i+1)*10);rpm[i]=uint16_t(1000+i*100);}
    curve(ms,rpm,64,turns);
    assert(get16(turns[0],1)==curveValue(1050) && get16(turns[0],3)==curveValue(1250) && get16(turns[3],15)==curveValue(7250));
    // Time, not turns: a slow first turn of 100 ms and a fast one ending at 400
    // ms fill a quarter and three quarters of the curve, not half each.
    const uint16_t twoMs[2]={100,400},twoRpm[2]={1000,4000};curve(twoMs,twoRpm,2,turns);
    assert(get16(turns[0],15)==curveValue(1000) && get16(turns[1],1)==curveValue(4000) && get16(turns[3],15)==curveValue(4000));
    // A span split between two turns takes each by its share: 50 ms of 3000, 50 of 1000.
    const uint16_t splitMs[2]={50,100},splitRpm[2]={3000,1000};curve(splitMs,splitRpm,2,turns);
    assert(get16(turns[1],15)==curveValue(3000) && get16(turns[2],1)==curveValue(1000));
    const uint16_t threeMs[3]={33,66,100},threeRpm[3]={3000,3000,1000};curve(threeMs,threeRpm,3,turns);
    // Span 21 (65.625-68.75 ms) is 0.375 ms of 3000 and 2.75 of 1000: 1240 RPM.
    assert(get16(turns[2],11)==curveValue(1240));
    // A turn of 0 RPM sends 0; no time at all is the last turn throughout; no turn, no points.
    const uint16_t zeroMs[3]={10,20,30},zero[3]={50,0,100};curve(zeroMs,zero,3,turns);
    assert(get16(turns[0],1)==65535 && get16(turns[1],15)==0);
    const uint16_t still[2]={0,0},stillRpm[2]={2000,3000};curve(still,stillRpm,2,turns);
    for(unsigned i=0;i<32;++i)assert(get16(turns[i/8],1+(i%8)*2)==curveValue(3000));
    curve(still,stillRpm,0,turns);
    for(unsigned k=0;k<4;++k){assert(turns[k][0]==0x70+k);for(unsigned i=1;i<BYTES;++i)assert(!turns[k][i]);}
  }
  // A clear's answer: A1 alone, then the status and the list the app reads.
  {
    const Packet p=cleared();assert(p[0]==0xa1);for(unsigned i=1;i<BYTES;++i)assert(!p[i]);
    App app;app.notify(p);assert(!app.statuses && !app.sets && !app.errors);
  }
  // What an app has not confirmed: the launches since its last clear's number.
  assert(unconfirmed(196,0)==196 && unconfirmed(196,190)==6 && unconfirmed(196,196)==0);
  assert(unconfirmed(4,196)==4);  // the history wiped: its launches count again
  std::cout<<"Battle Pass profile: status, the last 50 launches with their checksum, the curve, the order and pace after a launch, and the commands, as an app reads them\n";
}
