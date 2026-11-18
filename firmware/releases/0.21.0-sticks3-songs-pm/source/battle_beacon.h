#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Battles (docs/FIRMWARE_BATTLES.md): what two sticks say to each other over
// Bluetooth advertisements, with no connection and no pairing. Host-testable:
// bytes only, nothing of the stack.
//
// After a launch a stick broadcasts a launch frame for a few seconds and
// listens for others. Two sticks whose pulls began together name each other in
// their frames, which is the whole handshake. The battle's result is one small
// register both hold (Claim): either player sets it with a button, the frames
// carry it across, and both sticks end up with the same value.
namespace Battle {
// A result, from the point of view of the stick that holds it.
enum class Outcome:uint8_t {None,Won,Lost,Void};
// How a Beyblade X battle ends, and what the winner scores for it.
enum class Finish:uint8_t {Spin,Over,Burst,Xtreme};
static constexpr unsigned FINISHES=4;
constexpr unsigned points(Finish f){return f==Finish::Spin?1:f==Finish::Xtreme?3:2;}
constexpr const char *label(Finish f){return f==Finish::Spin?"Spin":f==Finish::Over?"Over":f==Finish::Burst?"Burst":"Xtreme";}
constexpr const char *name(Outcome o){return o==Outcome::Won?"won":o==Outcome::Lost?"lost":o==Outcome::Void?"void":"none";}
// A match is won by the first to this many points: 4 as Beyblade X is played,
// 7 for a longer one (a final). A frame may carry any length up to MATCH_MAX,
// so a later firmware can offer another without older sticks refusing it.
static constexpr uint8_t MATCH_SHORT=4,MATCH_LONG=7,MATCH_MAX=15;
constexpr bool validMatch(unsigned to){return to>=1 && to<=MATCH_MAX;}

// A battle's result as one stick holds it. `rev` counts the changes: a button
// press takes the next one, and a stick that hears a higher one adopts it, so
// the last change made wins on both. Two different changes with the same rev
// (both pressed at once) agree when they name the same winner, whose finish
// then stands; otherwise the battle is void on both until one presses again.
struct Claim {
  static constexpr uint8_t MAX_REV=15;
  Outcome outcome=Outcome::None;Finish finish=Finish::Spin;uint8_t rev=0;
  constexpr uint8_t byte()const{return uint8_t(uint8_t(outcome)|uint8_t(finish)<<2|rev<<4);}
  // Only a win or a loss has a finish.
  static constexpr Claim from(uint8_t b){return Claim{Outcome(b&3),(b&3)==1 || (b&3)==2?Finish((b>>2)&3):Finish::Spin,uint8_t(b>>4)};}
  // The same result as the other stick sees it.
  constexpr Claim mirrored()const {
    return Claim{outcome==Outcome::Won?Outcome::Lost:outcome==Outcome::Lost?Outcome::Won:outcome,finish,rev};
  }
  constexpr bool operator==(const Claim &o)const{return outcome==o.outcome && finish==o.finish && rev==o.rev;}
  constexpr bool operator!=(const Claim &o)const{return !(*this==o);}
  constexpr bool scored()const{return outcome==Outcome::Won || outcome==Outcome::Lost;}
  // A press on this stick. False once the register cannot change any more.
  bool set(Outcome o,Finish f) {
    if(rev>=MAX_REV)return false;
    outcome=o;finish=o==Outcome::Won || o==Outcome::Lost?f:Finish::Spin;++rev;return true;
  }
  // What the other stick holds, already mirrored to this stick's view. True
  // when this register changed.
  bool merge(Claim theirs) {
    if(theirs.rev>rev){*this=theirs;return true;}
    if(theirs.rev<rev || !rev || theirs==*this)return false;
    if(theirs.outcome==outcome && scored()) {
      // Same winner, another finish: the winner's own press stands.
      if(outcome==Outcome::Won)return false;
      finish=theirs.finish;return true;
    }
    if(outcome==Outcome::Void)return false;
    outcome=Outcome::Void;finish=Finish::Spin;return true;
  }
};

static constexpr uint8_t MAGIC=0x4c,LAUNCH_FRAME=0xb1,NAME_FRAME=0xb2;
static constexpr size_t NAME_CHARS=16,LAUNCH_BYTES=23,NAME_HEAD=7,MAX_FRAME=NAME_HEAD+NAME_CHARS;
// Bluetooth's "no company" identifier: these frames belong to no registered maker.
static constexpr uint16_t COMPANY=0xffff;
static constexpr size_t ADVERTISEMENT_MAX=31;
// The age of a launch is sent in 10 ms steps; this one means too old to match.
static constexpr uint16_t AGE_OLD=0xffff;

inline void put16(uint8_t *p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline uint16_t get16(const uint8_t *p){return uint16_t(p[0]|p[1]<<8);}
inline uint32_t get32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}

// A stick's latest launch and what it knows of the battle around it.
struct LaunchFrame {
  uint32_t id=0;        // the sender
  uint16_t tag=0;       // a random number for this launch, never 0
  uint16_t age=0;       // 10 ms steps since the pull began
  uint16_t rpm=0;
  uint32_t peer=0;      // the stick it takes for its opponent, 0 for none yet
  uint16_t peerTag=0;   // that stick's tag
  Claim claim;          // this battle's result, as the sender sees it
  uint16_t prevTag=0;   // the sender's tag in its battle before this one, 0 for none
  Claim prevClaim;      // and that battle's result, for a stick that missed it
  uint8_t matchTo=MATCH_SHORT;  // the match length the sender's player has chosen
  void encode(uint8_t *out)const {
    out[0]=MAGIC;out[1]=LAUNCH_FRAME;put32(out+2,id);put16(out+6,tag);put16(out+8,age);put16(out+10,rpm);
    put32(out+12,peer);put16(out+16,peerTag);out[18]=claim.byte();put16(out+19,prevTag);out[21]=prevClaim.byte();out[22]=matchTo;
  }
  bool decode(const uint8_t *in,size_t n) {
    if(n!=LAUNCH_BYTES || in[0]!=MAGIC || in[1]!=LAUNCH_FRAME)return false;
    LaunchFrame f;
    f.id=get32(in+2);f.tag=get16(in+6);f.age=get16(in+8);f.rpm=get16(in+10);
    f.peer=get32(in+12);f.peerTag=get16(in+16);f.claim=Claim::from(in[18]);f.prevTag=get16(in+19);f.prevClaim=Claim::from(in[21]);f.matchTo=in[22];
    // A result needs a battle, and a battle both sticks.
    if(!f.id || !f.tag || (f.peer==0)!=(f.peerTag==0) || f.peer==f.id || (!f.peer && f.claim.rev) || (!f.prevTag && f.prevClaim.rev) || !validMatch(f.matchTo))return false;
    *this=f;return true;
  }
};

// Printable ASCII, so it can be drawn as it is.
inline bool validName(const char *name,size_t length) {
  if(length>NAME_CHARS)return false;
  for(size_t i=0;i<length;++i)if(name[i]<0x20 || name[i]>0x7e)return false;
  return true;
}
// Who a stick is: sent now and then between launch frames.
struct NameFrame {
  uint32_t id=0;char name[NAME_CHARS+1]={};
  size_t encode(uint8_t *out)const {
    const size_t n=strnlen(name,NAME_CHARS);
    out[0]=MAGIC;out[1]=NAME_FRAME;put32(out+2,id);out[6]=uint8_t(n);std::memcpy(out+NAME_HEAD,name,n);
    return NAME_HEAD+n;
  }
  bool decode(const uint8_t *in,size_t n) {
    if(n<NAME_HEAD || in[0]!=MAGIC || in[1]!=NAME_FRAME || size_t(in[6])!=n-NAME_HEAD)return false;
    const char *text=reinterpret_cast<const char *>(in)+NAME_HEAD;
    if(!get32(in+2) || !in[6] || !validName(text,in[6]))return false;
    id=get32(in+2);std::memset(name,0,sizeof(name));std::memcpy(name,text,in[6]);return true;
  }
};

// A frame as advertising data: flags, then manufacturer data. Returns its length.
inline size_t advertisement(const uint8_t *frame,size_t n,uint8_t *out) {
  if(n+7>ADVERTISEMENT_MAX)return 0;
  out[0]=2;out[1]=0x01;out[2]=0x04;  // BR/EDR not supported; not discoverable: there is nothing to connect to
  out[3]=uint8_t(n+3);out[4]=0xff;put16(out+5,COMPANY);std::memcpy(out+7,frame,n);
  return n+7;
}
// The frame inside received advertising data, if it carries one.
inline bool payload(const uint8_t *data,size_t length,const uint8_t *&frame,size_t &n) {
  for(size_t at=0;at+1<length;) {
    const size_t field=data[at];
    if(!field || at+1+field>length)return false;
    if(data[at+1]==0xff && field>=5 && get16(data+at+2)==COMPANY && data[at+4]==MAGIC) {
      frame=data+at+4;n=field-3;return true;
    }
    at+=1+field;
  }
  return false;
}
}
