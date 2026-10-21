// The Celebrate mode and RPM milestone table: validation, the stored record
// and its fallback, the USB request, the celebration rule and the web
// editor's shared vectors.
#include "../LaunchLabMini/appearance_store.h"
#include "../LaunchLabMini/milestones.h"
#include "../LaunchLabMini/nvs_budget.h"
#include "../LaunchLabMini/practice_history.h"
#include "milestone_vectors.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

int main() {
  using namespace Milestones;using Appearance::Effect;
  // v2 (0.16): twelve rows of RPM and a 64-bit pool; v1 (0.15): eight rows of RPM and one effect.
  static_assert(Config::IMAGE_BYTES==136 && Request::BYTES==136 && Config::V1_IMAGE_BYTES==40 && Request::V1_BYTES==40 && MAX_ROWS==12 && V1_ROWS==8);
  // Defaults: today's behaviour (New best), 9K..15K escalating to Slash.
  {
    Config d;
    assert(d.mode()==Mode::Best && d.count()==7 && d.isDefault() && d.revision==0);
    const Effect expected[]={Effect::Sparkles,Effect::Sparkles,Effect::Orbit,Effect::Flames,Effect::Shockwave,Effect::Lightning,Effect::Slash};
    for(unsigned i=0;i<7;++i)assert(d.row(i).rpm==9000+1000*i && d.row(i).first()==expected[i] && d.row(i).size()==1);
    assert(d.fitsV1());
    assert(!milestones(Mode::Best) && milestones(Mode::Milestones) && milestones(Mode::Both));
    assert(!std::strcmp(label(Mode::Best),"New best") && !std::strcmp(name(Mode::Both),"both"));
  }
  // Validation: every rule, with the failing row; a refused table changes nothing.
  {
    Config c;const auto before=c;unsigned at=99;
    const Row ok[]={{1000,Effect::Orbit},{30000,Effect::Slash}};
    assert(Config::check(Mode::Both,ok,2,&at)==Problem::None && at==0);
    const Row low[]={{999,Effect::Orbit}},high[]={{9000,Effect::Orbit},{30001,Effect::Slash}},same[]={{9000,Effect::Orbit},{9000,Effect::Slash}};
    const Row fall[]={{10000,Effect::Orbit},{9000,Effect::Slash}},off[]={{9000,Effect::Orbit},{9500,Effect::Off}},random[]={{9000,Effect::Random}};
    assert(Config::check(Mode::Both,low,1,&at)==Problem::Range && at==0);
    assert(Config::check(Mode::Both,high,2,&at)==Problem::Range && at==1);
    assert(Config::check(Mode::Both,same,2,&at)==Problem::Order && at==1);
    assert(Config::check(Mode::Both,fall,2,&at)==Problem::Order && at==1);
    assert(Config::check(Mode::Both,off,2,&at)==Problem::Effect && at==1);
    assert(Config::check(Mode::Both,random,1)==Problem::Effect);
    assert(Config::check(Mode(3),ok,2)==Problem::Mode && Config::check(Mode::Both,ok,0)==Problem::Count);
    Row many[13];for(unsigned i=0;i<13;++i)many[i]={uint16_t(9000+i*100),Effect::Orbit};
    assert(Config::check(Mode::Both,many,13)==Problem::Count && Config::check(Mode::Both,many,12)==Problem::None);
    // Pools: any non-empty set of drawn effects; Off, Random or an effect past the last fails.
    const auto bit=[](Effect e){return uint64_t(1)<<unsigned(e);};
    const Row pooled[]={{9000,bit(Effect::Tidal)|bit(Effect::Frost)|bit(Effect::Bloom)}};
    assert(Config::check(Mode::Both,pooled,1)==Problem::None && pooled[0].size()==3 && pooled[0].first()==Effect::Tidal && pooled[0].has(Effect::Bloom));
    for(uint64_t bad:{uint64_t(0),bit(Effect::Orbit)|bit(Effect::Off),bit(Effect::Random),uint64_t(1)<<Appearance::EFFECTS,uint64_t(1)<<63}) {
      const Row r[]={{9000,bad}};assert(Config::check(Mode::Both,r,1,&at)==Problem::Effect && at==0);
    }
    // A table 0.15's record holds: at most eight single-effect rows of effects 0-7.
    {Config v;assert(v.set(Mode::Both,many,8)==Problem::None && v.fitsV1());
     assert(v.set(Mode::Both,many,9)==Problem::None && !v.fitsV1());
     assert(v.set(Mode::Both,pooled,1)==Problem::None && !v.fitsV1());
     const Row tidal[]={{9000,Effect::Tidal}};assert(v.set(Mode::Both,tidal,1)==Problem::None && !v.fitsV1());}
    for(const auto *rows:{low,high,same,fall,off})assert(c.set(Mode::Both,rows,rows==low?1:2)!=Problem::None);
    assert(c.same(before) && c.revision==0);
    // A change bumps the revision once; the same table again does not.
    assert(c.set(Mode::Both,ok,2)==Problem::None && c.revision==1 && c.count()==2 && c.mode()==Mode::Both);
    assert(c.set(Mode::Both,ok,2)==Problem::None && c.revision==1);
    assert(c.select(Mode::Milestones) && c.revision==2 && c.count()==2 && !c.select(Mode(3)) && c.revision==2);
    c.reset();assert(c.isDefault() && c.revision==3);
  }
  // Reaching a threshold: the RPM as shown (rounded half to even), the
  // highest one reached.
  {
    Config c;
    assert(!c.reached(8999.4f) && c.reached(8999.5f)->rpm==9000 && c.reached(9000.4f)->rpm==9000 && !c.reached(8998.5f));
    assert(c.reached(12999.9f)->rpm==13000 && c.reached(13000)->rpm==13000 && c.reached(14999)->rpm==14000);
    assert(c.reached(29999)->rpm==15000 && c.reached(29999)->first()==Effect::Slash && c.reachedIndex(29999)==6 && c.reachedIndex(8000)==-1);
    assert(!c.reached(std::nanf("")) && !c.reached(-1.0f));
  }
  // The rule: one animation per pull. A milestone plays its effect (Milestones,
  // Both); otherwise a best plays the Best effect (New best, Both). A best
  // always keeps its NEW BEST label; Milestones alone marks it without moving.
  {
    for(unsigned m=0;m<MODES;++m) {
      Config c;assert(c.select(Mode(m)));const bool mile=m!=0,best=m!=1;
      auto d=decide(c,true,8000,false,Effect::Orbit);assert(!d.show);
      d=decide(c,true,8000,true,Effect::Orbit);
      assert(d.show && d.best && !d.milestone && d.effect==(best?Effect::Orbit:Effect::Off));
      d=decide(c,true,12500,false,Effect::Orbit);
      assert(d.show==mile && !d.best && (!mile || (d.milestone==12000 && d.effect==Effect::Flames)));
      d=decide(c,true,15500,true,Effect::Orbit);
      assert(d.show && d.best && d.milestone==(mile?15000:0) && d.effect==(mile?Effect::Slash:Effect::Orbit));
      // An invalid pull is never a milestone.
      d=decide(c,false,15500,false,Effect::Orbit);assert(!d.show);
    }
    // A pool plays the row's next pick; without one (or one not in the pool), its first effect.
    {
      const Row rows[]={{9000,Effect::Sparkles},{12000,uint64_t(1)<<unsigned(Effect::Tidal)|uint64_t(1)<<unsigned(Effect::Eclipse)}};
      Config c;assert(c.set(Mode::Milestones,rows,2)==Problem::None);
      Effect next[MAX_ROWS]={Effect::Sparkles,Effect::Eclipse};
      auto d=decide(c,true,12500,false,Effect::Orbit,next);assert(d.show && d.row==1 && d.milestone==12000 && d.effect==Effect::Eclipse);
      next[1]=Effect::Orbit;d=decide(c,true,12500,false,Effect::Orbit,next);assert(d.effect==Effect::Tidal);
      d=decide(c,true,12500,false,Effect::Orbit);assert(d.effect==Effect::Tidal);
      d=decide(c,true,9100,false,Effect::Orbit,next);assert(d.row==0 && d.effect==Effect::Sparkles);
    }
    // pick(): uniform over the pool, never the previous pick while the pool has another.
    {
      const uint64_t three=uint64_t(1)<<unsigned(Effect::Tidal)|uint64_t(1)<<unsigned(Effect::Frost)|uint64_t(1)<<unsigned(Effect::Bloom);
      unsigned seen[Appearance::EFFECTS]={};
      for(uint32_t bits=0;bits<3000;++bits){const Effect e=pick(three,bits*2654435761u,Effect::Frost);assert(e==Effect::Tidal || e==Effect::Bloom);++seen[unsigned(e)];}
      assert(seen[unsigned(Effect::Tidal)]>1300 && seen[unsigned(Effect::Bloom)]>1300);
      Effect previous=Effect::Off;
      for(uint32_t bits=0;bits<500;++bits){const Effect e=pick(three,bits*40503u,previous);assert(e!=previous && (three>>unsigned(e)&1u));previous=e;}
      assert(pick(uint64_t(1)<<unsigned(Effect::Slash),7,Effect::Slash)==Effect::Slash && pick(0,7,Effect::Off)==Effect::Off);
      for(uint32_t bits=0;bits<50;++bits)assert(three>>unsigned(pick(three,bits,Effect::Orbit))&1u);
    }
    char text[24];milestoneText(text,sizeof(text),12000);assert(!std::strcmp(text,"MILESTONE 12K"));
    milestoneText(text,sizeof(text),12500);assert(!std::strcmp(text,"MILESTONE 12.5K"));
    milestoneText(text,sizeof(text),12345);assert(!std::strcmp(text,"MILESTONE 12345"));
    milestoneText(text,sizeof(text),30000);assert(!std::strcmp(text,"MILESTONE 30K"));
  }
  // Record: exact round trip; any damage, a checksummed invalid table or
  // nonzero unused bytes is rejected. v2 rows are RPM and an 8-byte pool.
  {
    Config c;const Row rows[]={{8000,Effect::Orbit},{10500,Effect::Lightning},{13000,uint64_t(1)<<unsigned(Effect::Slash)|uint64_t(1)<<unsigned(Effect::Storm)}};
    assert(c.set(Mode::Milestones,rows,3)==Problem::None);c.revision=0x01020304;
    Config::Image image;c.encode(image);
    assert(Appearance::get32(image.data())==Config::MAGIC && image[8]==1 && image[9]==3 && image[12]==0x40 && image[13]==0x1f && image[14]==1 && image[15]==0);
    assert(image[32+2]==0x80 && image[32+3]==0x40); // Slash bit 7, Storm bit 14
    Config back;assert(back.decode(image.data(),image.size()) && back.same(c) && back.revision==c.revision);
    for(size_t i=0;i<image.size();++i)for(uint8_t bit:{1,0x80}){auto bad=image;bad[i]^=bit;Config x;assert(!x.decode(bad.data(),bad.size()));}
    auto reseal=[](Config::Image &b){Appearance::put32(b.data()+Config::IMAGE_BYTES-4,Appearance::hash(b.data(),Config::IMAGE_BYTES-4));};
    for(unsigned offset:{10u,11u,8u+4+10*3,8u+4+10*11+9}){auto bad=image;bad[offset]=1;reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;bad[22]=bad[12];bad[23]=bad[13];reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;bad[9]=0;std::memset(bad.data()+12,0,30);reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;bad[14]=0;reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));} // an empty pool
    Config x;assert(!x.decode(image.data(),image.size()-1));
    // 0.15's v1 record still decodes, as single-effect rows; a v2 image under
    // the v1 magic (or the reverse) does not.
    Config single;const Row v1rows[]={{8000,Effect::Orbit},{10500,Effect::Lightning},{13000,Effect::Slash}};
    assert(single.set(Mode::Milestones,v1rows,3)==Problem::None && single.fitsV1());single.revision=9;
    Config::V1Image v1;single.encodeV1(v1);
    assert(Appearance::get32(v1.data())==Config::V1_MAGIC && v1[12]==0x40 && v1[13]==0x1f && v1[14]==0 && v1[20]==7);
    Config fromV1;assert(fromV1.decode(v1.data(),v1.size()) && fromV1.same(single) && fromV1.revision==9);
    {auto bad=v1;Appearance::put32(bad.data(),Config::MAGIC);Appearance::put32(bad.data()+36,Appearance::hash(bad.data(),36));assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;Appearance::put32(bad.data(),Config::V1_MAGIC);reseal(bad);assert(!x.decode(bad.data(),bad.size()));}
    // 0.15 itself rejects the v2 image: wrong length for its 40-byte record, and it never reads the v2 keys.
    static_assert(Config::IMAGE_BYTES!=Config::V1_IMAGE_BYTES);
  }
  // Store: alternating verified slots; a damaged record (both slots bad)
  // shows the defaults, stays until a change, and the next save replaces it.
  {
    Preferences::data.clear();Preferences::zeroLengthKeys.clear();
    Config c;AppearanceStore<Config> store;assert(store.begin(c,"ll-appearance","mile0","mile1",true) && !store.damaged() && c.isDefault());
    assert(!store.pending(c));
    for(unsigned i=0;i<5;++i){assert(c.select(Mode(i%3==0?1:i%3==1?2:0)));assert(store.pending(c) && store.save(c));}
    Config reread;AppearanceStore<Config> reader;assert(reader.begin(reread,"ll-appearance","mile0","mile1",true) && reread.same(c) && reread.revision==c.revision);
    // One damaged slot: the other, older generation loads.
    Preferences::data["ll-appearancemile1"][20]^=1;
    Config older;AppearanceStore<Config> partial;assert(partial.begin(older,"ll-appearance","mile0","mile1",true) && !partial.damaged() && older.revision==c.revision-1);
    // Both damaged: the defaults, and nothing is written until a change.
    Preferences::data["ll-appearancemile0"][20]^=1;const auto kept=Preferences::data;
    Config fallback;AppearanceStore<Config> damaged;
    assert(!damaged.begin(fallback,"ll-appearance","mile0","mile1",true) && damaged.damaged() && damaged.ready() && fallback.isDefault());
    assert(!damaged.pending(fallback) && Preferences::data==kept);
    // The appearance and bests stores keep refusing an unreadable record.
    Config refuse;AppearanceStore<Config> strict;assert(!strict.begin(refuse,"ll-appearance","mile0","mile1") && !strict.ready() && !strict.save(refuse));
    assert(fallback.select(Mode::Both) && damaged.save(fallback) && !damaged.damaged());
    Config fixed;AppearanceStore<Config> again;assert(again.begin(fixed,"ll-appearance","mile0","mile1",true) && !again.damaged() && fixed.mode()==Mode::Both);
    // A failed write leaves the change pending.
    Preferences::failWrite=true;assert(fixed.select(Mode::Best) && !again.save(fixed) && again.pending(fixed));Preferences::failWrite=false;
    assert(again.save(fixed));
  }
  // 0.15's record carries over once: read from its keys while the v2 keys
  // are absent, never written; a damaged one gives the defaults.
  {
    Preferences::data.clear();Preferences::zeroLengthKeys.clear();
    V1Record old;const Row rows[]={{9500,Effect::Shockwave},{14500,Effect::Slash}};assert(old.table.set(Mode::Both,rows,2)==Problem::None);
    {AppearanceStore<V1Record> writer;V1Record blank;assert(writer.begin(blank,"ll-appearance","mile0","mile1",true) && !writer.present() && writer.save(old));}
    const auto legacyBytes=Preferences::data;
    Config c;AppearanceStore<Config> v2;assert(v2.begin(c,"ll-appearance","mpool0","mpool1",true) && !v2.present() && c.isDefault());
    V1Record legacy;AppearanceStore<V1Record> reader;assert(reader.begin(legacy,"ll-appearance","mile0","mile1",true) && reader.present());
    assert(legacy.table.same(old.table) && legacy.table.revision==old.table.revision);
    c=legacy.table;assert(c.select(Mode::Milestones) && v2.save(c));
    for(const auto &[key,value]:legacyBytes)assert(Preferences::data.at(key)==value); // 0.15's keys untouched
    Config again;AppearanceStore<Config> v2again;assert(v2again.begin(again,"ll-appearance","mpool0","mpool1",true) && v2again.present() && again.same(c));
    for(auto &[key,value]:Preferences::data)if(key.find("mile")!=std::string::npos)value[20]^=1;
    V1Record broken;AppearanceStore<V1Record> damaged;assert(!damaged.begin(broken,"ll-appearance","mile0","mile1",true) && damaged.damaged() && broken.table.isDefault());
  }
  // NVS: a rewrite of the record costs a few entries, beside a history rewrite.
  static_assert(NvsBudget::blobEntries(Config::IMAGE_BYTES)==8 && NvsBudget::alternatingStoreEntries(Config::IMAGE_BYTES)==24);
  static_assert(NvsBudget::blobEntries(Config::IMAGE_BYTES)+NvsBudget::blobEntries(PracticeHistory::IMAGE_BYTES)<126,"fits in the free page");
  // Requests: Set carries a checked body, Reset none; shape and checksum.
  // v2 ("LLU\2") carries pools; 0.15's v1 ("LLU\1") still works.
  {
    Config t;const Row rows[]={{9500,Effect::Shockwave},{14500,uint64_t(1)<<unsigned(Effect::Slash)|uint64_t(1)<<unsigned(Effect::Bloom)}};
    assert(t.set(Mode::Both,rows,2)==Problem::None);
    uint8_t b[Request::BYTES];Request::encode(Request::Op::Set,&t,b);
    Request q;Problem p;unsigned row=9;
    assert(q.decode(b,sizeof(b),p,row) && p==Problem::None && q.op==Request::Op::Set && q.mode==Mode::Both && q.count==2 && q.version==2);
    assert(q.rows[0].rpm==9500 && q.rows[1]==rows[1]);
    for(size_t i=0;i<sizeof(b);++i){uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[i]^=4;Request x;assert(!x.decode(bad,sizeof(bad),p,row) || p!=Problem::None);}
    auto reseal=[](uint8_t *x,size_t n){Appearance::put32(x+n-4,Appearance::hash(x,n-4));};
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[22]=0x10;bad[23]=0x27;reseal(bad,sizeof(bad)); // row 1 at 10000: fine
     Request x;assert(x.decode(bad,sizeof(bad),p,row) && p==Problem::None);
     bad[22]=0x0c;bad[23]=0x25;reseal(bad,sizeof(bad));assert(x.decode(bad,sizeof(bad),p,row) && p==Problem::Order && row==1);}
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[5]=1;reseal(bad,sizeof(bad));Request x;assert(!x.decode(bad,sizeof(bad),p,row));}
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[4]=3;reseal(bad,sizeof(bad));Request x;assert(!x.decode(bad,sizeof(bad),p,row));}
    uint8_t r[Request::BYTES];Request::encode(Request::Op::Reset,nullptr,r);
    assert(q.decode(r,sizeof(r),p,row) && q.op==Request::Op::Reset && p==Problem::None);
    r[20]=1;reseal(r,sizeof(r));assert(!q.decode(r,sizeof(r),p,row));
    assert(!q.decode(b,sizeof(b)-1,p,row));
    // v1: 0.15's web editor's request decodes as single-effect rows.
    Config single;const Row v1rows[]={{9500,Effect::Shockwave},{14500,Effect::Slash}};assert(single.set(Mode::Both,v1rows,2)==Problem::None);
    uint8_t v1[Request::V1_BYTES];Request::encode(Request::Op::Set,&single,v1,true);
    assert(Appearance::get32(v1)==Request::V1_MAGIC && q.decode(v1,sizeof(v1),p,row) && p==Problem::None && q.version==1 && q.count==2 && q.rows[1]==v1rows[1]);
    uint8_t v1reset[Request::V1_BYTES];Request::encode(Request::Op::Reset,nullptr,v1reset,true);assert(q.decode(v1reset,sizeof(v1reset),p,row) && q.op==Request::Op::Reset);
    // A v2 body under the v1 magic, or a v1 request one byte long, is refused.
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));Appearance::put32(bad,Request::V1_MAGIC);reseal(bad,sizeof(bad));assert(!q.decode(bad,sizeof(bad),p,row));}
    // 0.15 decodes only 40-byte requests with its v1 magic, so a v2 request is
    // refused there as invalid_request before anything is written.
    static_assert(Request::BYTES!=Request::V1_BYTES && Request::MAGIC!=Request::V1_MAGIC);
  }
  // The shared vectors the web editor tests against are current.
  {
    std::ifstream file("firmware/tests/fixtures/milestone_vectors.json");std::stringstream text;text<<file.rdbuf();
    if(text.str()!=MilestoneVectors::json()) {
      std::cerr<<"firmware/tests/fixtures/milestone_vectors.json is stale: regenerate it with milestone_fixture.cpp\n";return 1;
    }
  }
  std::cout<<"PASS: milestone defaults, table rules with failing rows, effect pools and picks, v1 import and compatibility, shown-RPM thresholds, one-animation rule, record round trip and damage, damaged-record fallback, NVS budget, USB requests, shared web vectors\n";
}
