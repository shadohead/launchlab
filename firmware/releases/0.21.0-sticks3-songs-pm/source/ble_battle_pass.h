#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The Battle Pass profile (docs/BLE_BATTLE_PASS.md): what a stick has to say
// over Bluetooth so that apps written for Takara Tomy's BEYBATTLE PASS (BX-09)
// read its launches. Host-testable: packets only, nothing of the stack. It is
// one profile beside the LaunchLab service (ble_link.h); it is open, as the
// real device is, so it only ever reports launches and never changes anything.
namespace BattlePass {
static constexpr char NAME[]="BEYBLADE_TOOL01";
static constexpr char SERVICE[]="55c40000-f8eb-11ec-b939-0242ac120002";
static constexpr char WRITE[]="55c4f001-f8eb-11ec-b939-0242ac120002";
static constexpr char NOTIFY[]="55c4f002-f8eb-11ec-b939-0242ac120002";
static constexpr size_t BYTES=17;
static constexpr unsigned SHOTS=50,CURVE_POINTS=32;
// One-byte commands an app writes. The Beyblade X app ends a sync with
// CLEAR (0x75): it has the launches, and the pass forgets them and counts
// from 0 again; then POWER_OFF (0x63), and the pass leaves the link. Others the
// real device knows (0x61) are not understood well enough to act on, and are
// ignored. The pass answers a clear with A1, then its status and the empty list.
static constexpr uint8_t ASK_STATUS=0x51,ASK_SHOTS=0x74,CLEAR=0x75,POWER_OFF=0x63;
static constexpr uint8_t STATUS=0xa0,CLEARED=0xa1,LIST=0xb0,LIST_END=0xb6,CHECK=0xb7,CURVE=0x70,CURVE_END=0x73;
// Status byte 3. BUTTON: the pass's button is held, which apps ask for when a
// pass is registered; on this stick, A held on the Beyblade X page.
static constexpr uint8_t ATTACHED=0x04,DETACHED=0x00,BUTTON=0x10;
// Between packets, before the status a new subscription gets, and from a
// power-off to leaving the link once the packets before it have gone.
static constexpr uint32_t GAP_MS=10,GREETING_MS=100,POWER_OFF_MS=300;
using Packet=std::array<uint8_t,BYTES>;

inline void put16(Packet &p,unsigned offset,uint16_t v){p[offset]=uint8_t(v);p[offset+1]=uint8_t(v>>8);}
inline uint16_t get16(const Packet &p,unsigned offset){return uint16_t(p[offset]|(p[offset+1]<<8));}
// Shoot power is RPM, in 16 bits.
inline uint16_t shootPower(float rpm){return rpm>=65535?65535:rpm>0?uint16_t(rpm+.5f):0;}
// A turn in the curve: its period in 8 us steps, so SP = 7 500 000 / value. 0 is no point.
inline uint16_t curveValue(uint16_t rpm){return rpm?uint16_t(std::min<uint32_t>(65535,std::max<uint32_t>(1,(7500000u+rpm/2)/rpm))):0;}

// The launches an app has not yet confirmed: all since the launch number its
// last clear saw. A count that went back (the stick's history wiped) starts again.
inline uint32_t unconfirmed(uint32_t latest,uint32_t confirmed){return latest>=confirmed?latest-confirmed:latest;}

// What the stick knows of its launches, oldest first, the latest last.
struct Shots {
  uint16_t power[SHOTS]={};
  uint8_t count=0;
  uint16_t best=0,total=0;
  void add(uint16_t sp){if(count<SHOTS)power[count++]=sp;}
};

// A0: whether a top is on the launcher, the battery, the best and the count.
// The real device runs on two AAA cells and sends 0-250 for 2.00-3.00 V; apps
// show raw/250, so a percentage is sent as percent * 2.5. A held button is
// what the status says while it lasts.
inline Packet status(bool attached,int batteryPercent,uint16_t best,uint16_t total,const uint8_t *id,bool button=false) {
  Packet p{};p[0]=STATUS;p[3]=button?BUTTON:attached?ATTACHED:DETACHED;
  p[4]=uint8_t(std::min(250,std::max(0,batteryPercent)*5/2));
  put16(p,7,best);put16(p,9,total);std::memcpy(p.data()+11,id,6);
  return p;
}
// B0-B6 then B7: the last 50 launches, eight to a packet, and their checksum.
// The latest is slot `count-1`; apps refuse a set with no launch.
inline void shots(const Shots &s,Packet out[8]) {
  unsigned sum=0;
  for(unsigned k=0;k<7;++k) {
    Packet &p=out[k];p={};p[0]=uint8_t(LIST+k);
    for(unsigned i=0;i<8;++i){const unsigned slot=k*8+i;if(slot<s.count)put16(p,1+i*2,s.power[slot]);}
    if(k==6){put16(p,7,s.best);put16(p,9,s.total);p[11]=s.count;}
    for(unsigned i=1;i<BYTES;++i)sum+=p[i];
  }
  out[7]={};out[7][0]=CHECK;out[7][16]=uint8_t(sum);
}
// A1: a clear is done.
inline Packet cleared(){Packet p{};p[0]=CLEARED;return p;}
// 70-73: the latest launch over its whole time, from where the pull began to
// its last turn, in 32 equal spans. Each point is the RPM averaged over its
// span: turn j stands for the time since the one before, (ms[j-1], ms[j]],
// weighted by how much of the span it covers. The first 32 turns alone would
// be the first tenth of a second or so of a pull, its rise and none of its
// slowdown, and one odd turn would stand as a spike. No turn: no points.
inline void curve(const uint16_t *ms,const uint16_t *rpm,unsigned count,Packet out[4]) {
  for(unsigned k=0;k<4;++k){out[k]={};out[k][0]=uint8_t(CURVE+k);}
  if(!count)return;
  const uint64_t span=ms[count-1];
  for(unsigned k=0;k<CURVE_POINTS;++k) {
    uint32_t r=rpm[count-1];
    if(span) {
      // In ms x CURVE_POINTS, so every span's bounds are whole numbers.
      const uint64_t from=k*span,to=(k+1)*span;
      uint64_t sum=0,weight=0,lo=0;
      for(unsigned j=0;j<count;++j) {
        const uint64_t hi=uint64_t(ms[j])*CURVE_POINTS;
        const uint64_t a=std::max(lo,from),b=std::min(hi,to);
        if(b>a){sum+=uint64_t(rpm[j])*(b-a);weight+=b-a;}
        lo=std::max(lo,hi);
      }
      if(weight)r=uint32_t((sum+weight/2)/weight);
    }
    put16(out[k/8],1+(k%8)*2,curveValue(uint16_t(r)));
  }
}

// Packets waiting for the link, sent one at a time GAP_MS apart. A launch's
// set is 14 packets and is never mixed with another: a new set replaces what
// is still waiting.
class Outbox {
public:
  static constexpr unsigned CAPACITY=16;
  void clear(){head_=count_=0;}
  bool empty()const{return !count_;}
  unsigned size()const{return count_;}
  bool push(const Packet &p){if(count_==CAPACITY)return false;queue_[(head_+count_++)%CAPACITY]=p;return true;}
  // The next packet, once GAP_MS has passed since the last one left.
  bool take(uint32_t now,Packet &out) {
    if(!count_ || (sent_ && now-last_<GAP_MS))return false;
    out=queue_[head_];head_=(head_+1)%CAPACITY;--count_;last_=now;sent_=true;return true;
  }
private:
  Packet queue_[CAPACITY];
  unsigned head_=0,count_=0;
  uint32_t last_=0;bool sent_=false;
};

// What an app asked for, kept until the loop answers: set from the stack's
// task, taken by the loop. A new subscription is greeted with a status.
struct Requests {
  volatile bool status=false,shots=false,greet=false,forget=false,off=false;
  volatile uint32_t greetAt=0;
  void command(uint8_t byte) {
    if(byte==ASK_STATUS)status=true;else if(byte==ASK_SHOTS)shots=true;
    else if(byte==CLEAR)forget=true;else if(byte==POWER_OFF)off=true;
  }
  void subscribed(uint32_t now){greetAt=now;greet=true;}
  void clear(){status=shots=greet=forget=off=false;}
  bool takeStatus(uint32_t now) {
    bool due=status;status=false;
    if(greet && now-greetAt>=GREETING_MS){greet=false;due=true;}
    return due;
  }
  bool takeShots(){const bool due=shots;shots=false;return due;}
  bool takeClear(){const bool due=forget;forget=false;return due;}
  bool takeOff(){const bool due=off;off=false;return due;}
};
}
