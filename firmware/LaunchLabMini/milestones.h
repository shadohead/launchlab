#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "appearance.h"

// RPM milestone celebrations: which pulls celebrate (Celebrate setting) and
// the milestone table, a few RPM thresholds each with a pool of effects, one
// of which plays. Pure data: validation, the stored records, the USB request
// and the rule that picks a pull's celebration. The table is edited on the
// web (USB `U`), the mode also on the device.
namespace Milestones {
// Best is today's behaviour and the default.
enum class Mode:uint8_t {Best,Milestones,Both};
// Twelve rows since 0.16 (eight before); a 0.15 table holds at most eight.
constexpr unsigned MODES=3,MAX_ROWS=12,V1_ROWS=8;
constexpr uint16_t MIN_RPM=1000,MAX_RPM=30000;
inline const char *label(Mode m){static const char *s[]={"New best","Milestones","Both"};return unsigned(m)<MODES?s[unsigned(m)]:s[0];}
inline const char *name(Mode m){static const char *s[]={"best","milestones","both"};return unsigned(m)<MODES?s[unsigned(m)]:s[0];}
inline bool milestones(Mode m){return m==Mode::Milestones || m==Mode::Both;}

// A threshold and its effect pool: bit n is the effect stored as n. A pull
// that reaches the row plays one effect of the pool.
struct Row {
  uint16_t rpm=0;uint64_t pool=0;
  constexpr Row()=default;
  constexpr Row(uint16_t r,Appearance::Effect e):rpm(r),pool(uint64_t(1)<<unsigned(e)){}
  constexpr Row(uint16_t r,uint64_t p):rpm(r),pool(p){}
  unsigned size()const{unsigned n=0;for(uint64_t p=pool;p;p&=p-1)++n;return n;}
  bool has(Appearance::Effect e)const{return unsigned(e)<64 && ((pool>>unsigned(e))&1u);}
  // The lowest effect in the pool (0.15's single effect when it has one).
  Appearance::Effect first()const{unsigned i=0;while(i<64 && !((pool>>i)&1u))++i;return Appearance::Effect(i<64?i:unsigned(Appearance::Effect::Off));}
  bool operator==(const Row &o)const{return rpm==o.rpm && pool==o.pool;}
};
// Bit n set for every drawn effect of this firmware.
inline uint64_t drawnMask(){uint64_t m=0;for(unsigned e=0;e<Appearance::EFFECTS;++e)if(Appearance::drawn(e))m|=uint64_t(1)<<e;return m;}
// One effect of the pool, uniformly, never `previous` unless it is all the
// pool holds (as Random never repeats its previous pick).
inline Appearance::Effect pick(uint64_t pool,uint32_t bits,Appearance::Effect previous) {
  const uint64_t without=unsigned(previous)<64?pool&~(uint64_t(1)<<unsigned(previous)):pool,choices=without?without:pool;
  unsigned n=0;for(uint64_t p=choices;p;p&=p-1)++n;
  if(!n)return Appearance::Effect::Off;
  unsigned k=bits%n;
  for(unsigned e=0;e<64;++e)if((choices>>e)&1u){if(!k)return Appearance::Effect(e);--k;}
  return Appearance::Effect::Off;
}
// The first problem found in a table, as the USB reply names it.
enum class Problem:uint8_t {None,Mode,Count,Range,Order,Effect,Unused};
inline const char *problemName(Problem p) {
  static const char *s[]={"none","mode","count","range","order","effect","unused"};
  return unsigned(p)<sizeof(s)/sizeof(s[0])?s[unsigned(p)]:"unknown";
}
// The defaults escalate from low-key to dramatic: Sparkles at 9k and 10k,
// then Orbit, Flames, Shockwave, Lightning and Slash at 15k.
constexpr Row DEFAULT_ROWS[]={
  {9000,Appearance::Effect::Sparkles},{10000,Appearance::Effect::Sparkles},{11000,Appearance::Effect::Orbit},
  {12000,Appearance::Effect::Flames},{13000,Appearance::Effect::Shockwave},{14000,Appearance::Effect::Lightning},
  {15000,Appearance::Effect::Slash}};
constexpr unsigned DEFAULT_COUNT=sizeof(DEFAULT_ROWS)/sizeof(DEFAULT_ROWS[0]);
static_assert(DEFAULT_COUNT<=V1_ROWS,"the default table fits a 0.15 record");

// The RPM a pull shows: the recap prints "%.0f RPM", which rounds half to
// even like nearbyint, so a pull that reads 9000 reaches 9000.
inline long shown(float rpm){return std::isfinite(rpm)?long(std::nearbyint(rpm)):0;}

// Two record versions share one layout: magic, revision, a body (mode,
// count, two zero bytes, rows, unused rows zero) and FNV-1a over everything
// before it. The USB request carries the same body.
// v1 ("LLM\1", 0.15): eight rows of RPM (LE) and one effect.
// v2 ("LLM\2", 0.16): twelve rows of RPM (LE) and a 64-bit pool (LE).
// The v2 record lives in its own NVS keys, so 0.15 never sees it: a unit
// rolled back to 0.15 keeps the last table 0.15 itself wrote.
constexpr unsigned V1_BODY_BYTES=4+3*V1_ROWS,V2_BODY_BYTES=4+10*MAX_ROWS;
constexpr unsigned BODY_BYTES=V1_BODY_BYTES;  // 0.15's name for the v1 body
class Config {
public:
  static constexpr uint32_t MAGIC=0x024d4c4c,V1_MAGIC=0x014d4c4c; // "LLM\2", "LLM\1"
  static constexpr unsigned IMAGE_BYTES=8+V2_BODY_BYTES+4,V1_IMAGE_BYTES=8+V1_BODY_BYTES+4;
  using Image=std::array<uint8_t,IMAGE_BYTES>;
  uint32_t revision=0;
  Config(){std::copy(DEFAULT_ROWS,DEFAULT_ROWS+DEFAULT_COUNT,rows_);count_=DEFAULT_COUNT;}
  uint32_t generation()const{return revision;}
  Mode mode()const{return mode_;}
  unsigned count()const{return count_;}
  const Row &row(unsigned i)const{return rows_[i<MAX_ROWS?i:0];}

  // 1..MAX_ROWS rows, MIN_RPM..MAX_RPM, strictly rising, each a non-empty
  // pool of drawn effects; `at` gets the failing row.
  static Problem check(Mode mode,const Row *rows,unsigned count,unsigned *at=nullptr) {
    if(at)*at=0;
    if(unsigned(mode)>=MODES)return Problem::Mode;
    if(count<1 || count>MAX_ROWS)return Problem::Count;
    for(unsigned i=0;i<count;++i) {
      if(at)*at=i;
      if(rows[i].rpm<MIN_RPM || rows[i].rpm>MAX_RPM)return Problem::Range;
      if(i && rows[i].rpm<=rows[i-1].rpm)return Problem::Order;
      if(!rows[i].pool || (rows[i].pool&~drawnMask()))return Problem::Effect;
    }
    if(at)*at=0;
    return Problem::None;
  }
  // Takes a checked mode and table; a change bumps the revision.
  Problem set(Mode mode,const Row *rows,unsigned count,unsigned *at=nullptr) {
    const Problem p=check(mode,rows,count,at);
    if(p!=Problem::None)return p;
    Config next=*this;next.mode_=mode;next.count_=uint8_t(count);
    std::fill(next.rows_,next.rows_+MAX_ROWS,Row());
    std::copy(rows,rows+count,next.rows_);
    if(!next.same(*this)){*this=next;++revision;}
    return Problem::None;
  }
  bool select(Mode mode){return set(mode,rows_,count_)==Problem::None;}
  // Default mode and table, as a fresh device has.
  void reset(){Config d;set(d.mode_,d.rows_,d.count_);}
  bool same(const Config &o)const {
    if(mode_!=o.mode_ || count_!=o.count_)return false;
    for(unsigned i=0;i<count_;++i)if(!(rows_[i]==o.rows_[i]))return false;
    return true;
  }
  bool isDefault()const{return same(Config());}
  // Whether 0.15's record can hold the table: at most eight single-effect
  // rows, each an effect 0.15 knows. Backups keep such a table in format 3.
  bool fitsV1()const {
    if(count_>V1_ROWS)return false;
    for(unsigned i=0;i<count_;++i)if(rows_[i].size()!=1 || unsigned(rows_[i].first())>=8)return false;
    return true;
  }
  // The index of the highest threshold the shown RPM reaches, or -1.
  int reachedIndex(float rpm)const {
    const long value=shown(rpm);int hit=-1;
    for(unsigned i=0;i<count_;++i)if(value>=rows_[i].rpm)hit=int(i);
    return hit;
  }
  const Row *reached(float rpm)const{const int i=reachedIndex(rpm);return i<0?nullptr:&rows_[i];}

  void encodeBody(uint8_t *b,bool v1=false)const {
    std::memset(b,0,v1?V1_BODY_BYTES:V2_BODY_BYTES);b[0]=uint8_t(mode_);b[1]=count_;
    for(unsigned i=0;i<count_;++i) {
      if(v1){uint8_t *r=b+4+3*i;r[0]=uint8_t(rows_[i].rpm);r[1]=uint8_t(rows_[i].rpm>>8);r[2]=uint8_t(rows_[i].first());continue;}
      uint8_t *r=b+4+10*i;r[0]=uint8_t(rows_[i].rpm);r[1]=uint8_t(rows_[i].rpm>>8);
      for(unsigned k=0;k<8;++k)r[2+k]=uint8_t(rows_[i].pool>>(8*k));
    }
  }
  // A body that passes check() and has its reserved and unused bytes zero.
  static Problem decodeBody(const uint8_t *b,Mode &mode,Row *rows,unsigned &count,unsigned *at=nullptr,bool v1=false) {
    if(at)*at=0;
    if(b[2] || b[3])return Problem::Unused;
    mode=Mode(b[0]);count=b[1];
    const unsigned rowsIn=v1?V1_ROWS:MAX_ROWS,rowBytes=v1?3:10;
    if(count>rowsIn)return Problem::Count;
    for(unsigned i=0;i<rowsIn;++i) {
      const uint8_t *r=b+4+rowBytes*i;
      if(i>=count){for(unsigned k=0;k<rowBytes;++k)if(r[k]){if(at)*at=i;return Problem::Unused;}continue;}
      rows[i].rpm=uint16_t(r[0]|r[1]<<8);
      if(v1)rows[i].pool=r[2]<64?uint64_t(1)<<r[2]:0;
      else {rows[i].pool=0;for(unsigned k=0;k<8;++k)rows[i].pool|=uint64_t(r[2+k])<<(8*k);}
    }
    return check(mode,rows,count,at);
  }
  void encode(Image &b)const {
    b.fill(0);Appearance::put32(b.data(),MAGIC);Appearance::put32(b.data()+4,revision);encodeBody(b.data()+8);
    Appearance::put32(b.data()+IMAGE_BYTES-4,Appearance::hash(b.data(),IMAGE_BYTES-4));
  }
  // A v2 image, or a v1 image (0.15's record, a backup or an older table).
  bool decode(const uint8_t *b,size_t n) {
    const bool v1=n==V1_IMAGE_BYTES;
    if((n!=IMAGE_BYTES && !v1) || Appearance::get32(b)!=(v1?V1_MAGIC:MAGIC) || Appearance::get32(b+n-4)!=Appearance::hash(b,n-4))return false;
    Mode mode;Row rows[MAX_ROWS];unsigned count=0;
    if(decodeBody(b+8,mode,rows,count,nullptr,v1)!=Problem::None)return false;
    Config next;next.mode_=mode;next.count_=uint8_t(count);std::fill(next.rows_,next.rows_+MAX_ROWS,Row());
    std::copy(rows,rows+count,next.rows_);
    next.revision=Appearance::get32(b+4);*this=next;return true;
  }
  // 0.15's record, for a table that fitsV1() (backup format 3).
  using V1Image=std::array<uint8_t,V1_IMAGE_BYTES>;
  void encodeV1(V1Image &b)const {
    b.fill(0);Appearance::put32(b.data(),V1_MAGIC);Appearance::put32(b.data()+4,revision);encodeBody(b.data()+8,true);
    Appearance::put32(b.data()+V1_IMAGE_BYTES-4,Appearance::hash(b.data(),V1_IMAGE_BYTES-4));
  }
private:
  Mode mode_=Mode::Best;uint8_t count_=0;Row rows_[MAX_ROWS]={};
};
// 0.15's stored record in its own keys (mile0, mile1), read once on the first
// boot of 0.16 to carry the table over.
struct V1Record {
  using Image=Config::V1Image;
  Config table;
  uint32_t generation()const{return table.revision;}
  void encode(Image &b)const{table.encodeV1(b);}
  bool decode(const uint8_t *b,size_t n){return n==Config::V1_IMAGE_BYTES && table.decode(b,n);}
};

// One USB milestone request (the `U` transfer): magic, op, three zero bytes,
// the body (Set) or zeros (Reset to the defaults), FNV-1a over the rest.
// v1 ("LLU\1") carries a v1 body (0.15's web editor), v2 ("LLU\2") a v2 body.
struct Request {
  static constexpr uint32_t MAGIC=0x02554c4c,V1_MAGIC=0x01554c4c; // "LLU\2", "LLU\1"
  static constexpr unsigned BYTES=8+V2_BODY_BYTES+4,V1_BYTES=8+V1_BODY_BYTES+4;
  enum class Op:uint8_t {Set=1,Reset=2};
  Op op=Op::Set;
  Mode mode=Mode::Best;Row rows[MAX_ROWS]={};unsigned count=0;unsigned version=2;
  static const char *name(Op op){return op==Op::Set?"set":op==Op::Reset?"reset":"unknown";}
  // Shape and checksum; a Set body is judged with `problem`/`row`.
  bool decode(const uint8_t *b,size_t n,Problem &problem,unsigned &row) {
    problem=Problem::None;row=0;
    const bool v1=n==V1_BYTES;
    if((n!=BYTES && !v1) || Appearance::get32(b)!=(v1?V1_MAGIC:MAGIC) || Appearance::get32(b+n-4)!=Appearance::hash(b,n-4) || b[5] || b[6] || b[7])return false;
    if(b[4]!=uint8_t(Op::Set) && b[4]!=uint8_t(Op::Reset))return false;
    op=Op(b[4]);version=v1?1:2;
    if(op==Op::Reset){for(unsigned i=8;i<n-4;++i)if(b[i])return false;return true;}
    problem=Config::decodeBody(b+8,mode,rows,count,&row,v1);
    return true;
  }
  static void encode(Op op,const Config *table,uint8_t *b,bool v1=false) {
    const unsigned n=v1?V1_BYTES:BYTES;
    std::memset(b,0,n);Appearance::put32(b,v1?V1_MAGIC:MAGIC);b[4]=uint8_t(op);
    if(op==Op::Set && table)table->encodeBody(b+8,v1);
    Appearance::put32(b+n-4,Appearance::hash(b,n-4));
  }
};

// A pull's celebration. A personal best always shows its NEW BEST label. The
// animation is the highest milestone the pull reaches (Milestones, Both),
// otherwise the Best effect for a personal best (New best, Both): one
// animation per pull, never two. A milestone repeats on every pull that
// reaches it, with its row's next pick (`next`, one per row, chosen ahead so
// its sound can be rendered before the pull ends; see pick()). `bestEffect`
// is the Best effect setting, Random already picked.
struct Decision {bool show=false,best=false;uint16_t milestone=0;int row=-1;Appearance::Effect effect=Appearance::Effect::Off;};
inline Decision decide(const Config &c,bool valid,float rpm,bool beat,Appearance::Effect bestEffect,const Appearance::Effect *next=nullptr) {
  Decision d;d.best=beat;
  const int hit=valid && milestones(c.mode())?c.reachedIndex(rpm):-1;
  if(hit>=0) {
    const Row &r=c.row(unsigned(hit));
    d.show=true;d.milestone=r.rpm;d.row=hit;d.effect=next && r.has(next[hit])?next[hit]:r.first();return d;
  }
  if(beat){d.show=true;d.effect=c.mode()==Mode::Milestones?Appearance::Effect::Off:bestEffect;}
  return d;
}
// A threshold as the device writes it: "9K", "12.5K" or "12345".
inline void rpmText(char *out,size_t n,uint16_t rpm) {
  if(rpm%1000==0)snprintf(out,n,"%uK",unsigned(rpm/1000));
  else if(rpm%100==0)snprintf(out,n,"%u.%uK",unsigned(rpm/1000),unsigned(rpm%1000/100));
  else snprintf(out,n,"%u",unsigned(rpm));
}
// The recap's label for a milestone that is not a personal best.
inline void milestoneText(char *out,size_t n,uint16_t rpm){char v[12];rpmText(v,sizeof(v),rpm);snprintf(out,n,"MILESTONE %s",v);}
}
