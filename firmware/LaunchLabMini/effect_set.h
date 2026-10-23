#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "appearance.h"

// The effect set: which drawn effects the device offers in Settings › Best
// effect and Random draws from. Every effect stays compiled in; the web picks
// the set (USB `U` op 3) and the default is the 16 effects of 0.16. Milestone
// rows may still name any drawn effect, offered or not. Pure data: the rules,
// the stored record and the USB request.
namespace EffectSet {
using Appearance::Effect;
// A set of stored effect values, bit n = value n. 128 bits, so the effects
// of later releases fit without a new record, request or backup format.
struct Mask {
  static constexpr unsigned BITS=128;
  uint64_t lo=0,hi=0;
  constexpr Mask()=default;
  constexpr Mask(uint64_t low,uint64_t high=0):lo(low),hi(high){}
  constexpr bool has(unsigned e)const{return e<64?(lo>>e)&1:e<BITS?(hi>>(e-64))&1:false;}
  constexpr bool has(Effect e)const{return has(unsigned(e));}
  constexpr Mask operator|(Mask m)const{return {lo|m.lo,hi|m.hi};}
  constexpr Mask operator&(Mask m)const{return {lo&m.lo,hi&m.hi};}
  constexpr Mask operator~()const{return {~lo,~hi};}
  Mask &operator|=(Mask m){lo|=m.lo;hi|=m.hi;return *this;}
  Mask &operator&=(Mask m){lo&=m.lo;hi&=m.hi;return *this;}
  constexpr bool operator==(Mask m)const{return lo==m.lo && hi==m.hi;}
  constexpr bool operator!=(Mask m)const{return !(*this==m);}
  constexpr explicit operator bool()const{return lo || hi;}
  unsigned count()const{unsigned n=0;for(uint64_t w:{lo,hi})for(;w;w&=w-1)++n;return n;}
  // 32 hex digits, most significant first, as `M` prints it.
  void hex(char *out)const{std::snprintf(out,33,"%016llx%016llx",(unsigned long long)hi,(unsigned long long)lo);}
};
static_assert(Appearance::EFFECTS<=Mask::BITS,"one mask bit per stored effect value");
constexpr Mask flag(unsigned e){return e<64?Mask(uint64_t(1)<<e):e<Mask::BITS?Mask(0,uint64_t(1)<<(e-64)):Mask();}
constexpr Mask flag(Effect e){return flag(unsigned(e));}
// Every value below n.
constexpr Mask below(unsigned n){return n>=Mask::BITS?~Mask():n>=64?Mask(~uint64_t(0),(uint64_t(1)<<(n-64))-1):Mask((uint64_t(1)<<n)-1);}
// Every drawn effect of this firmware.
constexpr Mask ALL=below(Appearance::EFFECTS)&~(flag(Effect::Off)|flag(Effect::Random));
// The 32 loaded-effect slots (stored values 96-127).
constexpr Mask LOADED=below(Appearance::FIRST_LOADED+Appearance::LOADED)&~below(Appearance::FIRST_LOADED);
static_assert(Appearance::FIRST_LOADED+Appearance::LOADED<=Mask::BITS,"the loaded slots fit the mask");
// What celebrates here now: every drawn effect and every playable loaded slot.
inline Mask celebrating() {
  Mask m=ALL;
  for(unsigned v=Appearance::FIRST_LOADED;v<Appearance::FIRST_LOADED+Appearance::LOADED;++v)if(Appearance::playable(v))m|=flag(v);
  return m;
}
// The default: every drawn effect below Crimson (stored value 18), the 16 of 0.16.
constexpr unsigned DEFAULT_BELOW=18;
constexpr Mask DEFAULT_MASK=ALL&below(DEFAULT_BELOW);
inline unsigned count(Mask mask){return mask.count();}
inline bool offers(Mask mask,Effect e){return Appearance::celebrates(unsigned(e)) && mask.has(unsigned(e));}
// What firmware without loaded effects (0.18 and older) can read: the drawn
// effects of the mask, or the default when it offers none of them.
inline Mask olderView(Mask mask){const Mask m=mask&ALL;return m?m:DEFAULT_MASK;}

// The first problem in a mask, as the USB reply names it: no effect at all,
// or a bit that is not a drawn effect of this firmware (`at` gets its value).
// A new set may offer a loaded slot only while it plays (`installed`); a
// stored one keeps slots deleted since, which simply are not offered.
enum class Problem:uint8_t {None,Empty,Effect};
inline const char *problemName(Problem p){static const char *s[]={"none","empty","effect"};return unsigned(p)<3?s[unsigned(p)]:"unknown";}
inline Problem check(Mask mask,unsigned *at=nullptr,bool installed=true) {
  if(at)*at=0;
  if(!mask)return Problem::Empty;
  const Mask extra=mask&~(installed?celebrating():ALL|LOADED);
  if(!extra)return Problem::None;
  if(at){unsigned e=0;while(!extra.has(e))++e;*at=e;}
  return Problem::Effect;
}

// Settings › Best effect: Random, then the offered effects in Settings order,
// then Off. A saved effect outside the set (the set changed after it was
// chosen) stays selected; the cycle goes on from its position to the next
// offered effect.
inline Effect next(Effect e,Mask mask) {
  // Off, and anything that is neither drawn nor a loaded slot, continue at Random.
  if(e!=Effect::Random && !Appearance::drawn(unsigned(e)) && !Appearance::loaded(unsigned(e)))return Effect::Random;
  for(Effect c=Appearance::nextChoice(e);c!=Effect::Off;c=Appearance::nextChoice(c))
    if(c!=Effect::Random && offers(mask,c))return c;
  return Effect::Off;
}
// A Random pick: an offered effect other than the previous pick, or the only
// one offered. With every drawn effect offered it equals Appearance::pick.
inline Effect pick(uint32_t bits,Effect previous,Mask mask) {
  mask&=celebrating();
  if(!mask)mask=DEFAULT_MASK;
  const Mask pool=count(mask)>1 && offers(mask,previous)?mask&~flag(previous):mask;
  unsigned k=bits%count(pool);
  for(unsigned v=0;v<Mask::BITS;++v) {
    if(!pool.has(v))continue;
    if(!k)return Effect(v);
    --k;
  }
  return Appearance::drawnEffect(0);
}

inline void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;++i)p[i]=uint8_t(v>>(8*i));}
inline uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;++i)v|=uint64_t(p[i])<<(8*i);return v;}

inline void putMask(uint8_t *p,Mask m){put64(p,m.lo);put64(p+8,m.hi);}
inline Mask getMask(const uint8_t *p){return {get64(p),get64(p+8)};}

// The stored record: magic, revision, the 128-bit mask (LE, low word first)
// and FNV-1a over the rest.
// Firmware that predates an effect rejects a record offering it and falls
// back to its other slot or the default.
class Config {
public:
  static constexpr uint32_t MAGIC=0x01534c4c; // "LLS\1"
  static constexpr unsigned IMAGE_BYTES=28;
  using Image=std::array<uint8_t,IMAGE_BYTES>;
  uint32_t revision=0;
  uint32_t generation()const{return revision;}
  Mask mask()const{return mask_;}
  bool offers(Effect e)const{return EffectSet::offers(mask_,e);}
  unsigned count()const{return EffectSet::count(mask_);}
  bool isDefault()const{return mask_==DEFAULT_MASK;}
  // Takes a checked mask; a change bumps the revision.
  Problem set(Mask mask,unsigned *at=nullptr) {
    const Problem p=check(mask,at);
    if(p!=Problem::None)return p;
    if(mask!=mask_){mask_=mask;++revision;}
    return Problem::None;
  }
  void reset(){set(DEFAULT_MASK);}
  // Whether firmware without loaded effects cannot read this record (it
  // offers a loaded slot), and the record such firmware reads instead: its
  // drawn effects, or the default, one revision below (AppearanceStore::saveWithOlderView).
  bool needsOlderView()const{return bool(mask_&LOADED);}
  Config olderView()const{Config c;c.mask_=EffectSet::olderView(mask_);c.revision=revision-1;return c;}
  void encode(Image &b)const {
    b.fill(0);Appearance::put32(b.data(),MAGIC);Appearance::put32(b.data()+4,revision);putMask(b.data()+8,mask_);
    Appearance::put32(b.data()+24,Appearance::hash(b.data(),24));
  }
  bool decode(const uint8_t *b,size_t n) {
    if(n!=IMAGE_BYTES || Appearance::get32(b)!=MAGIC || Appearance::get32(b+24)!=Appearance::hash(b,24))return false;
    const Mask mask=getMask(b+8);
    if(check(mask,nullptr,false)!=Problem::None)return false;
    mask_=mask;revision=Appearance::get32(b+4);return true;
  }
private:
  Mask mask_=DEFAULT_MASK;
};

// USB `U` op 3 (set_effects): the milestone request frame of 0.15 and 0.16
// (magic "LLU\1", op, three zero bytes, a 28-byte body, FNV-1a over the rest:
// 40 bytes) with op 3, the 128-bit mask (LE, low word first) in body bytes
// 0-15 and the other 12 body bytes zero. Firmware without the effect set accepts only ops 1 and 2 and
// answers MILESTONE_ERROR reason=invalid_request written=0.
struct Request {
  static constexpr uint32_t MAGIC=0x01554c4c; // "LLU\1"
  static constexpr unsigned BYTES=40,BODY_BYTES=28;
  static constexpr uint8_t OP=3;
  static constexpr const char *NAME="set_effects";
  // The frame and op of a set_effects request; decode() judges the rest.
  static bool claims(const uint8_t *b,size_t n){return n==BYTES && Appearance::get32(b)==MAGIC && b[4]==OP;}
  // Shape, zero bytes and checksum; the mask is judged with `problem`/`at`.
  static bool decode(const uint8_t *b,size_t n,Mask &mask,Problem &problem,unsigned &at) {
    problem=Problem::None;at=0;mask={};
    if(!claims(b,n) || b[5] || b[6] || b[7] || Appearance::get32(b+BYTES-4)!=Appearance::hash(b,BYTES-4))return false;
    for(unsigned i=24;i<8+BODY_BYTES;++i)if(b[i])return false;
    mask=getMask(b+8);problem=check(mask,&at);return true;
  }
  static void encode(Mask mask,uint8_t *b) {
    std::memset(b,0,BYTES);Appearance::put32(b,MAGIC);b[4]=OP;putMask(b+8,mask);
    Appearance::put32(b+BYTES-4,Appearance::hash(b,BYTES-4));
  }
};
}
