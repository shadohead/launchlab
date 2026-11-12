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
    uint16_t rpm[40];for(unsigned i=0;i<40;++i)rpm[i]=uint16_t(i<12?500+i*600:7100-(i-12)*150);
    curve(rpm,40,turns);
    App app;
    for(unsigned k=0;k<8;++k){assert(list[k][0]==0xb0+k);app.notify(list[k]);}
    for(unsigned k=0;k<4;++k){assert(turns[k][0]==0x70+k);app.notify(turns[k]);}
    assert(app.sets==1 && !app.errors && app.listed==count && app.latest==3000+(count-1)*37);
    // The first 32 turns, in order, each within the 8 us step's rounding.
    assert(app.curvePoints==32);
    for(unsigned i=0;i<32;++i)assert(std::fabs(app.curveRpm[i]-rpm[i])<=rpm[i]*rpm[i]/7500000.0+1);
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
    Packet list[8],turns[4];shots(s,list);const uint16_t rpm[3]={1000,2000,3000};curve(rpm,3,turns);
    list[2][5]^=0x40;
    App app;for(auto &p:list)app.notify(p);for(auto &p:turns)app.notify(p);
    assert(!app.sets && app.errors==1);
  }
  // A short curve ends at its last turn, and no launch at all makes no valid set.
  {
    Shots s;s.add(5000);Packet list[8],turns[4];shots(s,list);const uint16_t rpm[3]={1000,2000,3000};curve(rpm,3,turns);
    App app;for(auto &p:list)app.notify(p);for(auto &p:turns)app.notify(p);
    assert(app.sets==1 && app.curvePoints==3 && std::fabs(app.curveRpm[2]-3000)<2);
    Shots none;shots(none,list);App empty;for(auto &p:list)empty.notify(p);for(auto &p:turns)empty.notify(p);
    assert(!empty.sets && empty.errors==1);
  }
  // After a launch: attached, released, then the whole set, in that order, one
  // packet every GAP_MS at the fastest and never two at once.
  {
    Outbox box;App app;uint32_t now=1000;
    Shots s;s.add(6500);Packet list[8],turns[4];shots(s,list);const uint16_t rpm[8]={900,2000,3500,5000,6000,6500,6200,5000};curve(rpm,8,turns);
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
    for(unsigned byte=0;byte<256;++byte)if(byte!=ASK_STATUS && byte!=ASK_SHOTS)r.command(uint8_t(byte));
    assert(!r.takeStatus(0) && !r.takeShots());  // 0x75 and 0x61 among them: never acted on
    r.subscribed(500);assert(!r.takeStatus(500+GREETING_MS-1) && r.takeStatus(500+GREETING_MS) && !r.takeStatus(5000));
    r.command(ASK_SHOTS);r.subscribed(1);r.clear();assert(!r.takeShots() && !r.takeStatus(1000));
  }
  std::cout<<"Battle Pass profile: status, the last 50 launches with their checksum, the curve, the order and pace after a launch, and the commands, as an app reads them\n";
}
