#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "appearance.h"

// RPM milestone celebrations: which pulls celebrate (Celebrate setting) and
// the milestone table, a few RPM thresholds each with its own effect. Pure
// data: validation, the stored record, the USB request and the rule that
// picks a pull's celebration. The table is edited on the web (USB `U`), the
// mode also on the device.
namespace Milestones {
// Best is today's behaviour and the default.
enum class Mode:uint8_t {Best,Milestones,Both};
constexpr unsigned MODES=3,MAX_ROWS=8;
constexpr uint16_t MIN_RPM=1000,MAX_RPM=30000;
inline const char *label(Mode m){static const char *s[]={"New best","Milestones","Both"};return unsigned(m)<MODES?s[unsigned(m)]:s[0];}
inline const char *name(Mode m){static const char *s[]={"best","milestones","both"};return unsigned(m)<MODES?s[unsigned(m)]:s[0];}
inline bool milestones(Mode m){return m==Mode::Milestones || m==Mode::Both;}

struct Row {uint16_t rpm=0;Appearance::Effect effect=Appearance::Effect::Off;};
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
static_assert(DEFAULT_COUNT<=MAX_ROWS,"the default table fits the record");

// The RPM a pull shows: the recap prints "%.0f RPM", which rounds half to
// even like nearbyint, so a pull that reads 9000 reaches 9000.
inline long shown(float rpm){return std::isfinite(rpm)?long(std::nearbyint(rpm)):0;}

// Mode and table share one record: magic, revision, a 28-byte body (mode,
// count, two zero bytes, MAX_ROWS rows of RPM (LE) and effect, unused rows
// zero) and FNV-1a over everything before it. The USB request carries the
// same body.
constexpr unsigned BODY_BYTES=4+3*MAX_ROWS;
class Config {
public:
  static constexpr uint32_t MAGIC=0x014d4c4c; // "LLM\1"
  static constexpr unsigned IMAGE_BYTES=8+BODY_BYTES+4;
  using Image=std::array<uint8_t,IMAGE_BYTES>;
  uint32_t revision=0;
  Config(){std::memcpy(rows_,DEFAULT_ROWS,sizeof(DEFAULT_ROWS));count_=DEFAULT_COUNT;}
  uint32_t generation()const{return revision;}
  Mode mode()const{return mode_;}
  unsigned count()const{return count_;}
  const Row &row(unsigned i)const{return rows_[i<MAX_ROWS?i:0];}

  // 1..MAX_ROWS rows, MIN_RPM..MAX_RPM, strictly rising, each a drawn
  // effect; `at` gets the failing row.
  static Problem check(Mode mode,const Row *rows,unsigned count,unsigned *at=nullptr) {
    if(at)*at=0;
    if(unsigned(mode)>=MODES)return Problem::Mode;
    if(count<1 || count>MAX_ROWS)return Problem::Count;
    for(unsigned i=0;i<count;++i) {
      if(at)*at=i;
      if(rows[i].rpm<MIN_RPM || rows[i].rpm>MAX_RPM)return Problem::Range;
      if(i && rows[i].rpm<=rows[i-1].rpm)return Problem::Order;
      if(!Appearance::drawn(unsigned(rows[i].effect)))return Problem::Effect;
    }
    if(at)*at=0;
    return Problem::None;
  }
  // Takes a checked mode and table; a change bumps the revision.
  Problem set(Mode mode,const Row *rows,unsigned count,unsigned *at=nullptr) {
    const Problem p=check(mode,rows,count,at);
    if(p!=Problem::None)return p;
    Config next=*this;next.mode_=mode;next.count_=uint8_t(count);
    std::memset(next.rows_,0,sizeof(next.rows_));
    for(unsigned i=0;i<count;++i)next.rows_[i]=rows[i];
    if(!next.same(*this)){*this=next;++revision;}
    return Problem::None;
  }
  bool select(Mode mode){return set(mode,rows_,count_)==Problem::None;}
  // Default mode and table, as a fresh device has.
  void reset(){Config d;set(d.mode_,d.rows_,d.count_);}
  bool same(const Config &o)const {
    if(mode_!=o.mode_ || count_!=o.count_)return false;
    for(unsigned i=0;i<count_;++i)if(rows_[i].rpm!=o.rows_[i].rpm || rows_[i].effect!=o.rows_[i].effect)return false;
    return true;
  }
  bool isDefault()const{return same(Config());}
  // The highest threshold the shown RPM reaches, or null.
  const Row *reached(float rpm)const {
    const long value=shown(rpm);const Row *hit=nullptr;
    for(unsigned i=0;i<count_;++i)if(value>=rows_[i].rpm)hit=&rows_[i];
    return hit;
  }

  void encodeBody(uint8_t *b)const {
    std::memset(b,0,BODY_BYTES);b[0]=uint8_t(mode_);b[1]=count_;
    for(unsigned i=0;i<count_;++i){b[4+3*i]=uint8_t(rows_[i].rpm);b[5+3*i]=uint8_t(rows_[i].rpm>>8);b[6+3*i]=uint8_t(rows_[i].effect);}
  }
  // A body that passes check() and has its reserved and unused bytes zero.
  static Problem decodeBody(const uint8_t *b,Mode &mode,Row *rows,unsigned &count,unsigned *at=nullptr) {
    if(at)*at=0;
    if(b[2] || b[3])return Problem::Unused;
    mode=Mode(b[0]);count=b[1];
    if(count>MAX_ROWS)return Problem::Count;
    for(unsigned i=0;i<MAX_ROWS;++i) {
      const uint8_t *r=b+4+3*i;
      if(i>=count){if(r[0] || r[1] || r[2]){if(at)*at=i;return Problem::Unused;}continue;}
      rows[i].rpm=uint16_t(r[0]|r[1]<<8);rows[i].effect=Appearance::Effect(r[2]);
    }
    return check(mode,rows,count,at);
  }
  void encode(Image &b)const {
    b.fill(0);Appearance::put32(b.data(),MAGIC);Appearance::put32(b.data()+4,revision);encodeBody(b.data()+8);
    Appearance::put32(b.data()+IMAGE_BYTES-4,Appearance::hash(b.data(),IMAGE_BYTES-4));
  }
  bool decode(const uint8_t *b,size_t n) {
    if(n!=IMAGE_BYTES || Appearance::get32(b)!=MAGIC || Appearance::get32(b+IMAGE_BYTES-4)!=Appearance::hash(b,IMAGE_BYTES-4))return false;
    Mode mode;Row rows[MAX_ROWS];unsigned count=0;
    if(decodeBody(b+8,mode,rows,count)!=Problem::None)return false;
    Config next;next.mode_=mode;next.count_=uint8_t(count);std::memset(next.rows_,0,sizeof(next.rows_));
    for(unsigned i=0;i<count;++i)next.rows_[i]=rows[i];
    next.revision=Appearance::get32(b+4);*this=next;return true;
  }
private:
  Mode mode_=Mode::Best;uint8_t count_=0;Row rows_[MAX_ROWS]={};
};

// One USB milestone request (the `U` transfer): magic, op, three zero bytes,
// the body (Set) or zeros (Reset to the defaults), FNV-1a over the rest.
struct Request {
  static constexpr uint32_t MAGIC=0x01554c4c; // "LLU\1"
  static constexpr unsigned BYTES=8+BODY_BYTES+4;
  enum class Op:uint8_t {Set=1,Reset=2};
  Op op=Op::Set;
  Mode mode=Mode::Best;Row rows[MAX_ROWS]={};unsigned count=0;
  static const char *name(Op op){return op==Op::Set?"set":op==Op::Reset?"reset":"unknown";}
  // Shape and checksum; a Set body is judged with `problem`/`row`.
  bool decode(const uint8_t *b,size_t n,Problem &problem,unsigned &row) {
    problem=Problem::None;row=0;
    if(n!=BYTES || Appearance::get32(b)!=MAGIC || Appearance::get32(b+BYTES-4)!=Appearance::hash(b,BYTES-4) || b[5] || b[6] || b[7])return false;
    if(b[4]!=uint8_t(Op::Set) && b[4]!=uint8_t(Op::Reset))return false;
    op=Op(b[4]);
    if(op==Op::Reset){for(unsigned i=8;i<8+BODY_BYTES;++i)if(b[i])return false;return true;}
    problem=Config::decodeBody(b+8,mode,rows,count,&row);
    return true;
  }
  static void encode(Op op,const Config *table,uint8_t *b) {
    std::memset(b,0,BYTES);Appearance::put32(b,MAGIC);b[4]=uint8_t(op);
    if(op==Op::Set && table)table->encodeBody(b+8);
    Appearance::put32(b+BYTES-4,Appearance::hash(b,BYTES-4));
  }
};

// A pull's celebration. A personal best always shows its NEW BEST label. The
// animation is the highest milestone the pull reaches (Milestones, Both),
// otherwise the Best effect for a personal best (New best, Both): one
// animation per pull, never two. A milestone repeats on every pull that
// reaches it. `bestEffect` is the Best effect setting, Random already picked.
struct Decision {bool show=false,best=false;uint16_t milestone=0;Appearance::Effect effect=Appearance::Effect::Off;};
inline Decision decide(const Config &c,bool valid,float rpm,bool beat,Appearance::Effect bestEffect) {
  Decision d;d.best=beat;
  const Row *hit=valid && milestones(c.mode())?c.reached(rpm):nullptr;
  if(hit){d.show=true;d.milestone=hit->rpm;d.effect=hit->effect;return d;}
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
