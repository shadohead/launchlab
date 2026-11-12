#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// The host-testable parts of docs/BLE_PROTOCOL.md's Name and Pairing.
namespace BleName {
static constexpr size_t MAX=16,FULL=10+MAX+1;
inline bool valid(const char *name,size_t length) {
  if(!length || length>MAX)return false;
  for(size_t i=0;i<length;++i) {
    const char c=name[i];
    if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_'))return false;
  }
  return true;
}
// The advertised name: a set name, or the default from the Bluetooth address.
inline void full(char *out,size_t size,const char *chosen,const uint8_t *mac) {
  if(chosen[0])snprintf(out,size,"LaunchLab-%s",chosen);
  else snprintf(out,size,"LaunchLab-%02X%02X",mac[4],mac[5]);
}
}
namespace BlePairing {
// Numeric comparison: the stick shows a number and A confirms it. A stranger
// cannot keep asking. A link may stay unpaired: a Battle Pass app never pairs,
// and unpaired it reaches only that profile (the prototype ended any link not
// paired within a minute).
static constexpr uint32_t CONFIRM_MS=30000,HOLD_MS=60000,AUTO_ACCEPT_MS=4000;
static constexpr unsigned FAILURES=3;
enum class Phase:uint8_t {None,Unsecured,Comparing,Secured,Dropping};
struct Link {
  Phase phase=Phase::None;
  uint32_t since=0,number=0;
  bool drop=false,automatic=false;
  // Survive the connection: rejected or timed-out requests, then a pause.
  unsigned failures=0;
  bool holding=false;
  uint32_t holdFrom=0;
  void fail(uint32_t now) {
    if(++failures>=FAILURES){failures=0;holding=true;holdFrom=now;}
  }
  void end(){phase=Phase::Dropping;drop=true;number=0;}
  void connected(uint32_t now){phase=Phase::Unsecured;since=now;number=0;drop=false;}
  void disconnected(uint32_t now) {
    if(phase==Phase::Comparing)fail(now);
    phase=Phase::None;number=0;drop=false;
  }
  bool holdingAt(uint32_t now) {
    if(holding && now-holdFrom>=HOLD_MS)holding=false;
    return holding;
  }
  // The stack asks to compare. `busy`: a pull, an editor, Wipe or tournament on the
  // stick. Refused requests are never queued, and do not count as failures. With no
  // bond yet the stick accepts by itself after a moment, B still rejecting.
  void comparison(uint32_t now,bool busy,uint32_t code,bool hasBond) {
    if(phase==Phase::None)return;
    if(phase!=Phase::Unsecured || busy || holdingAt(now)){end();return;}
    phase=Phase::Comparing;since=now;number=code;automatic=!hasBond;
  }
  // True once when the moment has passed: the loop then confirms to the stack.
  bool takeAutoAccept(uint32_t now) {
    if(phase!=Phase::Comparing || !automatic || now-since<AUTO_ACCEPT_MS)return false;
    phase=Phase::Unsecured;since=now;number=0;return true;
  }
  // Any other way to pair: never Just Works, nor a passkey.
  void refuse(){if(phase!=Phase::None)end();}
  // A or B. False if nothing was being asked.
  bool answer(uint32_t now,bool accept) {
    if(phase!=Phase::Comparing)return false;
    number=0;
    if(accept){phase=Phase::Unsecured;since=now;}
    else {fail(now);end();}
    return true;
  }
  // Every end of encryption: a bonded phone's restore, or a pairing's result.
  // Authenticated means both people compared; anything else drops the link.
  void encryption(uint32_t now,bool encrypted,bool authenticated) {
    if(phase==Phase::None)return;
    if(encrypted && authenticated){phase=Phase::Secured;number=0;failures=0;}
    else {fail(now);end();}
  }
  bool secured()const{return phase==Phase::Secured;}
  bool comparing()const{return phase==Phase::Comparing;}
  // True once when the link should be ended: a pairing refused, failed or out of time.
  bool takeDrop(uint32_t now) {
    if(phase==Phase::Comparing && now-since>=CONFIRM_MS){fail(now);end();}
    const bool out=drop;drop=false;return out;
  }
};
}
namespace BleBonds {
// A stick's bonds in flash: both key sets per peer, not subscriptions, which the
// phone repeats per connection and would otherwise write flash each time. The
// record is refused whole if the stack's struct size or any byte differs.
static constexpr unsigned MAX=3;
static constexpr uint8_t MAGIC=0x42,VERSION=1;
static constexpr size_t HEADER=4,CHECK=4;
inline uint32_t checksum(const uint8_t *p,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
template<class Value> struct Image {
  unsigned ours=0,peers=0;
  Value our[MAX],peer[MAX];
  size_t bytes()const{return HEADER+(ours+peers)*sizeof(Value)+CHECK;}
  bool add(bool isOur,const Value &v) {
    unsigned &n=isOur?ours:peers;
    if(n>=MAX)return false;
    (isOur?our:peer)[n++]=v;return true;
  }
  size_t encode(uint8_t *out,size_t capacity)const {
    if(capacity<bytes() || sizeof(Value)>255)return 0;
    out[0]=MAGIC;out[1]=VERSION;out[2]=uint8_t(sizeof(Value));out[3]=uint8_t(ours<<4|peers);
    uint8_t *p=out+HEADER;
    if(ours){std::memcpy(p,our,ours*sizeof(Value));p+=ours*sizeof(Value);}
    if(peers){std::memcpy(p,peer,peers*sizeof(Value));p+=peers*sizeof(Value);}
    const uint32_t c=checksum(out,size_t(p-out));std::memcpy(p,&c,CHECK);
    return size_t(p-out)+CHECK;
  }
  bool decode(const uint8_t *in,size_t n) {
    ours=peers=0;
    if(n<HEADER+CHECK || in[0]!=MAGIC || in[1]!=VERSION || in[2]!=sizeof(Value))return false;
    const unsigned o=in[3]>>4,e=in[3]&15;
    if(o>MAX || e>MAX || n!=HEADER+(o+e)*sizeof(Value)+CHECK)return false;
    uint32_t c;std::memcpy(&c,in+n-CHECK,CHECK);
    if(c!=checksum(in,n-CHECK))return false;
    if(o)std::memcpy(our,in+HEADER,o*sizeof(Value));
    if(e)std::memcpy(peer,in+HEADER+o*sizeof(Value),e*sizeof(Value));
    ours=o;peers=e;return true;
  }
};
}
