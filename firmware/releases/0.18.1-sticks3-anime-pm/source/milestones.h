#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "appearance.h"
#include "effect_set.h"

// RPM milestone celebrations: which pulls celebrate (Celebrate setting) and
// the milestone table, a few RPM thresholds each with a pool of effects, one
// of which plays. Pure data: validation, the stored records, the USB request
// and the rule that picks a pull's celebration. The table is edited on the
// web (USB `U`), the mode also on the device.
namespace Milestones {
// Best is the behaviour before milestones. A unit with no saved mode (new,
// never changed, or after Wipe all) uses DEFAULT_MODE; a saved mode wins.
enum class Mode:uint8_t {Best,Milestones,Both};
constexpr Mode DEFAULT_MODE=Mode::Both;
// Twelve rows since 0.16 (eight before); a 0.15 table holds at most eight.
constexpr unsigned MODES=3,MAX_ROWS=12,V1_ROWS=8;
constexpr uint16_t MIN_RPM=1000,MAX_RPM=30000;
inline const char *label(Mode m){static const char *s[]={"New best","Milestones","Both"};return unsigned(m)<MODES?s[unsigned(m)]:s[0];}
inline const char *name(Mode m){static const char *s[]={"best","milestones","both"};return unsigned(m)<MODES?s[unsigned(m)]:s[0];}
inline bool milestones(Mode m){return m==Mode::Milestones || m==Mode::Both;}

// A threshold and its effect pool: bit n is the effect stored as n, 128
// bits since 0.18 (the effect set's mask; 0.16 and 0.17 pools held 64). A
// pull that reaches the row plays one effect of the pool.
using Pool=EffectSet::Mask;
struct Row {
  uint16_t rpm=0;Pool pool;
  constexpr Row()=default;
  constexpr Row(uint16_t r,Appearance::Effect e):rpm(r),pool(EffectSet::flag(e)){}
  constexpr Row(uint16_t r,Pool p):rpm(r),pool(p){}
  unsigned size()const{return pool.count();}
  bool has(Appearance::Effect e)const{return pool.has(unsigned(e));}
  // The lowest effect in the pool (0.15's single effect when it has one).
  Appearance::Effect first()const{for(unsigned i=0;i<Pool::BITS;++i)if(pool.has(i))return Appearance::Effect(i);return Appearance::Effect::Off;}
  // Whether 0.16's 64-bit pool holds it.
  bool fits64()const{return !pool.hi;}
  bool operator==(const Row &o)const{return rpm==o.rpm && pool==o.pool;}
};
// Bit n set for every drawn effect of this firmware.
inline Pool drawnMask(){return EffectSet::ALL;}
// One effect of the pool, uniformly, never `previous` unless it is all the
// pool holds (as Random never repeats its previous pick).
inline Appearance::Effect pick(Pool pool,uint32_t bits,Appearance::Effect previous) {
  const Pool without=pool&~EffectSet::flag(previous),choices=without?without:pool;
  const unsigned n=choices.count();
  if(!n)return Appearance::Effect::Off;
  unsigned k=bits%n;
  for(unsigned e=0;e<Pool::BITS;++e)if(choices.has(e)){if(!k)return Appearance::Effect(e);--k;}
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

// Three record versions share one layout: magic, revision, a body (mode,
// count, two zero bytes, rows, unused rows zero) and FNV-1a over everything
// before it. The USB request carries the same body.
// v1 ("LLM\1", 0.15): eight rows of RPM (LE) and one effect.
// v2 ("LLM\2", 0.16 and 0.17): twelve rows of RPM (LE) and a 64-bit pool (LE).
// v3 ("LLM\3", 0.18): twelve rows of RPM (LE) and a 128-bit pool (LE, low
// word first), for effects past value 63.
// v3 is the USB request's and the backup's form. In NVS a v3 table is the
// packed record (PACKED_IMAGE_BYTES), the same 8 entries as a v2 slot: see
// milestone_store.h.
constexpr unsigned V1_BODY_BYTES=4+3*V1_ROWS,V2_BODY_BYTES=4+10*MAX_ROWS,V3_BODY_BYTES=4+18*MAX_ROWS;
constexpr unsigned BODY_BYTES=V1_BODY_BYTES;  // 0.15's name for the v1 body
// Row bytes per record version 1..3: RPM and an effect, a 64-bit or a 128-bit pool.
constexpr unsigned rowBytes(unsigned version){return version==1?3:version==2?10:18;}
constexpr unsigned bodyBytes(unsigned version){return 4+rowBytes(version)*(version==1?V1_ROWS:MAX_ROWS);}
class Config {
public:
  static constexpr uint32_t MAGIC=0x034d4c4c,V2_MAGIC=0x024d4c4c,V1_MAGIC=0x014d4c4c; // "LLM\3", "LLM\2", "LLM\1"
  static constexpr unsigned IMAGE_BYTES=8+V3_BODY_BYTES+4,V2_IMAGE_BYTES=8+V2_BODY_BYTES+4,V1_IMAGE_BYTES=8+V1_BODY_BYTES+4;
  static constexpr uint32_t magic(unsigned version){return version==1?V1_MAGIC:version==2?V2_MAGIC:MAGIC;}
  static constexpr unsigned imageBytes(unsigned version){return 8+bodyBytes(version)+4;}
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
  // Whether the v2 record of 0.16 and 0.17 can hold the table: every pool
  // within effects 0-63. Backups keep such a table in format 4 or 5.
  bool fitsV2()const {
    for(unsigned i=0;i<count_;++i)if(!rows_[i].fits64())return false;
    return true;
  }
  // Whether 0.16 reads the table's v2 record: every pool within its sixteen
  // effects (0.17 reads every table 0.16 does).
  bool fitsV016()const {
    for(unsigned i=0;i<count_;++i)if(rows_[i].pool&~EffectSet::DEFAULT_MASK)return false;
    return true;
  }
  // What a unit rolled back to 0.16 or 0.17 shows for a table they cannot
  // read: the same mode and thresholds, each pool cut to 0.16's effects, or
  // the default table's effect at that RPM when nothing is left. One
  // revision older, so this firmware still loads the table itself.
  Config forV016()const {
    Config c=*this;c.revision=revision-1;
    for(unsigned i=0;i<count_;++i) {
      Row &r=c.rows_[i];r.pool&=EffectSet::DEFAULT_MASK;
      if(r.pool)continue;
      Appearance::Effect e=DEFAULT_ROWS[0].first();
      for(const Row &d:DEFAULT_ROWS)if(d.rpm<=r.rpm)e=d.first();
      r.pool=EffectSet::flag(e);
    }
    return c;
  }
  // The index of the highest threshold the shown RPM reaches, or -1.
  int reachedIndex(float rpm)const {
    const long value=shown(rpm);int hit=-1;
    for(unsigned i=0;i<count_;++i)if(value>=rows_[i].rpm)hit=int(i);
    return hit;
  }
  const Row *reached(float rpm)const{const int i=reachedIndex(rpm);return i<0?nullptr:&rows_[i];}

  // The body of record version 1, 2 or 3; the caller checks that the table
  // fits it (fitsV1, fitsV2).
  void encodeBody(uint8_t *b,unsigned version=3)const {
    std::memset(b,0,bodyBytes(version));b[0]=uint8_t(mode_);b[1]=count_;
    for(unsigned i=0;i<count_;++i) {
      uint8_t *r=b+4+rowBytes(version)*i;r[0]=uint8_t(rows_[i].rpm);r[1]=uint8_t(rows_[i].rpm>>8);
      if(version==1)r[2]=uint8_t(rows_[i].first());
      else if(version==2)EffectSet::put64(r+2,rows_[i].pool.lo);
      else EffectSet::putMask(r+2,rows_[i].pool);
    }
  }
  // A body that passes check() and has its reserved and unused bytes zero.
  static Problem decodeBody(const uint8_t *b,Mode &mode,Row *rows,unsigned &count,unsigned *at=nullptr,unsigned version=3) {
    if(at)*at=0;
    if(b[2] || b[3])return Problem::Unused;
    mode=Mode(b[0]);count=b[1];
    const unsigned rowsIn=version==1?V1_ROWS:MAX_ROWS,size=rowBytes(version);
    if(count>rowsIn)return Problem::Count;
    for(unsigned i=0;i<rowsIn;++i) {
      const uint8_t *r=b+4+size*i;
      if(i>=count){for(unsigned k=0;k<size;++k)if(r[k]){if(at)*at=i;return Problem::Unused;}continue;}
      rows[i].rpm=uint16_t(r[0]|r[1]<<8);
      rows[i].pool=version==1?EffectSet::flag(r[2]):version==2?Pool(EffectSet::get64(r+2)):EffectSet::getMask(r+2);
    }
    return check(mode,rows,count,at);
  }
  void encode(Image &b)const {
    b.fill(0);Appearance::put32(b.data(),MAGIC);Appearance::put32(b.data()+4,revision);encodeBody(b.data()+8);
    Appearance::put32(b.data()+IMAGE_BYTES-4,Appearance::hash(b.data(),IMAGE_BYTES-4));
  }
  // The record version an image of n bytes would be, or 0.
  static unsigned versionOf(size_t n){return n==IMAGE_BYTES?3:n==V2_IMAGE_BYTES?2:n==V1_IMAGE_BYTES?1:0;}
  // A v3 image, or an older one (0.15's or 0.16's record, a backup or an older table).
  bool decode(const uint8_t *b,size_t n) {
    const unsigned version=versionOf(n);
    if(!version || Appearance::get32(b)!=magic(version) || Appearance::get32(b+n-4)!=Appearance::hash(b,n-4))return false;
    Mode mode;Row rows[MAX_ROWS];unsigned count=0;
    if(decodeBody(b+8,mode,rows,count,nullptr,version)!=Problem::None)return false;
    Config next;next.mode_=mode;next.count_=uint8_t(count);std::fill(next.rows_,next.rows_+MAX_ROWS,Row());
    std::copy(rows,rows+count,next.rows_);
    next.revision=Appearance::get32(b+4);*this=next;return true;
  }
  // 0.15's record, for a table that fitsV1() (backup format 3).
  using V1Image=std::array<uint8_t,V1_IMAGE_BYTES>;
  void encodeV1(V1Image &b)const{encodeVersion(b.data(),1);}
  // 0.16's record, for a table that fitsV2() (backup formats 4 and 5).
  using V2Image=std::array<uint8_t,V2_IMAGE_BYTES>;
  void encodeV2(V2Image &b)const{encodeVersion(b.data(),2);}

  // The packed v3 record, 160 bytes: "L3", revision, mode | count<<2, the
  // pool width w (bits per pool: the highest effect in any pool plus one),
  // then each row as 15 bits of RPM and w bits of pool, LSB first, unused
  // bits zero, and FNV-1a over the rest. 160 bytes take the 8 NVS entries a
  // 136-byte v2 slot does, so the record replaces a v2 slot without growing.
  static constexpr uint16_t PACKED_MAGIC=0x334c;  // "L3"
  static constexpr unsigned PACKED_IMAGE_BYTES=160,PACKED_ROW_BITS=8*(PACKED_IMAGE_BYTES-12),RPM_BITS=15;
  static_assert(MAX_RPM<(1u<<RPM_BITS),"an RPM fits its bits");
  // Every table of this firmware packs: twelve rows of every effect.
  static_assert(MAX_ROWS*(RPM_BITS+Appearance::EFFECTS)<=PACKED_ROW_BITS,"every table fits the packed record");
  using PackedImage=std::array<uint8_t,PACKED_IMAGE_BYTES>;
  unsigned packedWidth()const {
    unsigned w=0;
    for(unsigned i=0;i<count_;++i)for(unsigned e=w;e<Pool::BITS;++e)if(rows_[i].pool.has(e))w=e+1;
    return w;
  }
  // False when the table does not fit (a later firmware with more effects).
  bool encodePacked(PackedImage &b)const {
    const unsigned w=packedWidth();
    b.fill(0);if(count_*(RPM_BITS+w)>PACKED_ROW_BITS)return false;
    b[0]=uint8_t(PACKED_MAGIC);b[1]=uint8_t(PACKED_MAGIC>>8);Appearance::put32(b.data()+2,revision);
    b[6]=uint8_t(unsigned(mode_)|count_<<2);b[7]=uint8_t(w);
    unsigned at=0;auto put=[&](unsigned bit){if(bit)b[8+at/8]|=uint8_t(1u<<(at%8));++at;};
    for(unsigned i=0;i<count_;++i) {
      for(unsigned k=0;k<RPM_BITS;++k)put((rows_[i].rpm>>k)&1);
      for(unsigned e=0;e<w;++e)put(rows_[i].pool.has(e));
    }
    Appearance::put32(b.data()+PACKED_IMAGE_BYTES-4,Appearance::hash(b.data(),PACKED_IMAGE_BYTES-4));
    return true;
  }
  // A packed record that passes check(), with every unused bit zero.
  bool decodePacked(const uint8_t *b,size_t n) {
    if(n!=PACKED_IMAGE_BYTES || unsigned(b[0]|b[1]<<8)!=PACKED_MAGIC ||
       Appearance::get32(b+n-4)!=Appearance::hash(b,n-4))return false;
    const unsigned mode=b[6]&3,count=b[6]>>2,w=b[7];
    if(w>Pool::BITS || count>MAX_ROWS || count*(RPM_BITS+w)>PACKED_ROW_BITS)return false;
    unsigned at=0;auto get=[&]{const bool bit=(b[8+at/8]>>(at%8))&1;++at;return bit;};
    Row rows[MAX_ROWS];
    for(unsigned i=0;i<count;++i) {
      unsigned rpm=0;for(unsigned k=0;k<RPM_BITS;++k)rpm|=unsigned(get())<<k;
      rows[i].rpm=uint16_t(rpm);
      for(unsigned e=0;e<w;++e)if(get())rows[i].pool|=EffectSet::flag(e);
    }
    for(;at<PACKED_ROW_BITS;)if(get())return false;
    if(check(Mode(mode),rows,count)!=Problem::None)return false;
    Config next;next.mode_=Mode(mode);next.count_=uint8_t(count);std::fill(next.rows_,next.rows_+MAX_ROWS,Row());
    std::copy(rows,rows+count,next.rows_);
    next.revision=Appearance::get32(b+2);*this=next;return true;
  }
private:
  void encodeVersion(uint8_t *b,unsigned version)const {
    const unsigned n=imageBytes(version);std::memset(b,0,n);
    Appearance::put32(b,magic(version));Appearance::put32(b+4,revision);encodeBody(b+8,version);
    Appearance::put32(b+n-4,Appearance::hash(b,n-4));
  }
  Mode mode_=DEFAULT_MODE;uint8_t count_=0;Row rows_[MAX_ROWS]={};
};
// The stored table's keys in ll-appearance: 0.16's, 0.17's and 0.18's
// (a v2 or a packed v3 record per slot), and 0.15's v1 record, which a
// first boot of 0.16 or later carries over and never writes again.
constexpr const char *KEY0="mpool0",*KEY1="mpool1",*V1_KEY0="mile0",*V1_KEY1="mile1";
// 0.15's stored record in its own keys (mile0, mile1), read once on the
// first boot of 0.16 or later to carry the table over.
struct V1Record {
  using Image=Config::V1Image;
  Config table;
  uint32_t generation()const{return table.revision;}
  void encode(Image &b)const{table.encodeV1(b);}
  bool decode(const uint8_t *b,size_t n){return n==Config::V1_IMAGE_BYTES && table.decode(b,n);}
};
// 0.16's and 0.17's stored record (keys mpool0, mpool1), as they write it.
struct V2Record {
  using Image=Config::V2Image;
  Config table;
  uint32_t generation()const{return table.revision;}
  void encode(Image &b)const{table.encodeV2(b);}
  bool decode(const uint8_t *b,size_t n){return n==Config::V2_IMAGE_BYTES && table.decode(b,n);}
};

// One USB milestone request (the `U` transfer): magic, op, three zero bytes,
// the body (Set) or zeros (Reset to the defaults), FNV-1a over the rest.
// v1 ("LLU\1") carries a v1 body (0.15's web editor), v2 ("LLU\2") a v2 body
// (0.16's and 0.17's), v3 ("LLU\3") a v3 body; firmware before 0.18 refuses
// a v3 request (too long for its receiver) without writing anything.
struct Request {
  static constexpr uint32_t MAGIC=0x03554c4c,V2_MAGIC=0x02554c4c,V1_MAGIC=0x01554c4c; // "LLU\3", "LLU\2", "LLU\1"
  static constexpr unsigned BYTES=8+V3_BODY_BYTES+4,V2_BYTES=8+V2_BODY_BYTES+4,V1_BYTES=8+V1_BODY_BYTES+4;
  static constexpr uint32_t magic(unsigned version){return version==1?V1_MAGIC:version==2?V2_MAGIC:MAGIC;}
  static constexpr unsigned bytes(unsigned version){return 8+bodyBytes(version)+4;}
  enum class Op:uint8_t {Set=1,Reset=2};
  Op op=Op::Set;
  Mode mode=DEFAULT_MODE;Row rows[MAX_ROWS]={};unsigned count=0;unsigned version=3;
  static const char *name(Op op){return op==Op::Set?"set":op==Op::Reset?"reset":"unknown";}
  // Shape and checksum; a Set body is judged with `problem`/`row`.
  bool decode(const uint8_t *b,size_t n,Problem &problem,unsigned &row) {
    problem=Problem::None;row=0;
    const unsigned v=n==BYTES?3:n==V2_BYTES?2:n==V1_BYTES?1:0;
    if(!v || Appearance::get32(b)!=magic(v) || Appearance::get32(b+n-4)!=Appearance::hash(b,n-4) || b[5] || b[6] || b[7])return false;
    if(b[4]!=uint8_t(Op::Set) && b[4]!=uint8_t(Op::Reset))return false;
    op=Op(b[4]);version=v;
    if(op==Op::Reset){for(unsigned i=8;i<n-4;++i)if(b[i])return false;return true;}
    problem=Config::decodeBody(b+8,mode,rows,count,&row,v);
    return true;
  }
  // A request of `version` (3 unless an older editor's frame is wanted; the
  // table must fit it).
  static void encode(Op op,const Config *table,uint8_t *b,unsigned version=3) {
    const unsigned n=bytes(version);
    std::memset(b,0,n);Appearance::put32(b,magic(version));b[4]=uint8_t(op);
    if(op==Op::Set && table)table->encodeBody(b+8,version);
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
