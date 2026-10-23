// The Celebrate mode and RPM milestone table: validation, the stored record
// and its fallback, the USB request, the celebration rule and the web
// editor's shared vectors.
#include "../LaunchLabMini/appearance_store.h"
#include "../LaunchLabMini/milestones.h"
#include "../LaunchLabMini/milestone_store.h"
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
  // v3 (0.18): twelve rows of RPM and a 128-bit pool; v2 (0.16, 0.17): a
  // 64-bit pool; v1 (0.15): eight rows of RPM and one effect.
  static_assert(Config::IMAGE_BYTES==232 && Request::BYTES==232 && Config::V2_IMAGE_BYTES==136 && Request::V2_BYTES==136 &&
                Config::V1_IMAGE_BYTES==40 && Request::V1_BYTES==40 && MAX_ROWS==12 && V1_ROWS==8);
  // Defaults: DEFAULT_MODE, 9K..15K escalating to Slash.
  {
    Config d;
    assert(d.mode()==DEFAULT_MODE && d.count()==7 && d.isDefault() && d.revision==0);
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
    const auto bit=[](Effect e){return EffectSet::flag(e);};
    const Row pooled[]={{9000,bit(Effect::Tidal)|bit(Effect::Frost)|bit(Effect::Bloom)}};
    assert(Config::check(Mode::Both,pooled,1)==Problem::None && pooled[0].size()==3 && pooled[0].first()==Effect::Tidal && pooled[0].has(Effect::Bloom));
    for(Pool bad:{Pool(),bit(Effect::Orbit)|bit(Effect::Off),bit(Effect::Random),EffectSet::flag(Appearance::EFFECTS),EffectSet::flag(127)}) {
      const Row r[]={{9000,bad}};assert(Config::check(Mode::Both,r,1,&at)==Problem::Effect && at==0);
    }
    // Effects past value 63 (0.18) are pool members like any other.
    {const Row wide[]={{9000,bit(Effect::Volley)|bit(Effect::Starfall)|bit(Effect::Sharkfin)}};
     assert(Config::check(Mode::Both,wide,1)==Problem::None && wide[0].size()==3 && wide[0].first()==Effect::Volley && wide[0].has(Effect::Sharkfin) && !wide[0].fits64());}
    // A table 0.15's record holds: at most eight single-effect rows of effects 0-7.
    {Config v;assert(v.set(Mode::Both,many,8)==Problem::None && v.fitsV1());
     assert(v.set(Mode::Both,many,9)==Problem::None && !v.fitsV1());
     assert(v.set(Mode::Both,pooled,1)==Problem::None && !v.fitsV1());
     const Row tidal[]={{9000,Effect::Tidal}};assert(v.set(Mode::Both,tidal,1)==Problem::None && !v.fitsV1() && v.fitsV2());
     // 0.16's and 0.17's record holds pools within effects 0-63 only.
     const Row edge[]={{9000,Effect::Volley}},past[]={{9000,Effect::Orbit},{9500,bit(Effect::Starfall)|bit(Effect::Orbit)}};
     assert(v.set(Mode::Both,edge,1)==Problem::None && v.fitsV2() && v.set(Mode::Both,past,2)==Problem::None && !v.fitsV2() && !v.fitsV1());}
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
      const Row rows[]={{9000,Effect::Sparkles},{12000,EffectSet::flag(Effect::Tidal)|EffectSet::flag(Effect::Eclipse)}};
      Config c;assert(c.set(Mode::Milestones,rows,2)==Problem::None);
      Effect next[MAX_ROWS]={Effect::Sparkles,Effect::Eclipse};
      auto d=decide(c,true,12500,false,Effect::Orbit,next);assert(d.show && d.row==1 && d.milestone==12000 && d.effect==Effect::Eclipse);
      next[1]=Effect::Orbit;d=decide(c,true,12500,false,Effect::Orbit,next);assert(d.effect==Effect::Tidal);
      d=decide(c,true,12500,false,Effect::Orbit);assert(d.effect==Effect::Tidal);
      d=decide(c,true,9100,false,Effect::Orbit,next);assert(d.row==0 && d.effect==Effect::Sparkles);
    }
    // pick(): uniform over the pool, never the previous pick while the pool has another.
    {
      const Pool three=EffectSet::flag(Effect::Tidal)|EffectSet::flag(Effect::Frost)|EffectSet::flag(Effect::Bloom);
      unsigned seen[Appearance::EFFECTS]={};
      for(uint32_t bits=0;bits<3000;++bits){const Effect e=pick(three,bits*2654435761u,Effect::Frost);assert(e==Effect::Tidal || e==Effect::Bloom);++seen[unsigned(e)];}
      assert(seen[unsigned(Effect::Tidal)]>1300 && seen[unsigned(Effect::Bloom)]>1300);
      Effect previous=Effect::Off;
      for(uint32_t bits=0;bits<500;++bits){const Effect e=pick(three,bits*40503u,previous);assert(e!=previous && three.has(e));previous=e;}
      assert(pick(EffectSet::flag(Effect::Slash),7,Effect::Slash)==Effect::Slash && pick(Pool(),7,Effect::Off)==Effect::Off);
      for(uint32_t bits=0;bits<50;++bits)assert(three.has(pick(three,bits,Effect::Orbit)));
      // Across the old 64-bit limit: uniform over both words.
      const Pool wide=EffectSet::flag(Effect::Orbit)|EffectSet::flag(Effect::Volley)|EffectSet::flag(Effect::Starfall)|EffectSet::flag(Effect::Sharkfin);
      unsigned hits[Pool::BITS]={};for(uint32_t bits=0;bits<4000;++bits)++hits[unsigned(pick(wide,bits*2654435761u,Effect::Off))];
      for(Effect e:{Effect::Orbit,Effect::Volley,Effect::Starfall,Effect::Sharkfin})assert(hits[unsigned(e)]>850);
    }
    char text[24];milestoneText(text,sizeof(text),12000);assert(!std::strcmp(text,"MILESTONE 12K"));
    milestoneText(text,sizeof(text),12500);assert(!std::strcmp(text,"MILESTONE 12.5K"));
    milestoneText(text,sizeof(text),12345);assert(!std::strcmp(text,"MILESTONE 12345"));
    milestoneText(text,sizeof(text),30000);assert(!std::strcmp(text,"MILESTONE 30K"));
  }
  // Record: exact round trip; any damage, a checksummed invalid table or
  // nonzero unused bytes is rejected. v3 rows are RPM and a 16-byte pool.
  {
    Config c;const Row rows[]={{8000,Effect::Orbit},{10500,Effect::Lightning},{13000,EffectSet::flag(Effect::Slash)|EffectSet::flag(Effect::Storm)|EffectSet::flag(Effect::Sharkfin)}};
    assert(c.set(Mode::Milestones,rows,3)==Problem::None);c.revision=0x01020304;
    Config::Image image;c.encode(image);
    assert(Appearance::get32(image.data())==Config::MAGIC && image[8]==1 && image[9]==3 && image[12]==0x40 && image[13]==0x1f && image[14]==1 && image[15]==0);
    // Row 2 at 12+36: Slash bit 7, Storm bit 14 in the low word, Sharkfin (72) bit 8 of the high word.
    assert(image[48+2]==0x80 && image[48+3]==0x40 && image[48+2+9]==0x01 && image[48+2+8]==0);
    Config back;assert(back.decode(image.data(),image.size()) && back.same(c) && back.revision==c.revision);
    for(size_t i=0;i<image.size();++i)for(uint8_t bit:{1,0x80}){auto bad=image;bad[i]^=bit;Config x;assert(!x.decode(bad.data(),bad.size()));}
    auto reseal=[](Config::Image &b){Appearance::put32(b.data()+Config::IMAGE_BYTES-4,Appearance::hash(b.data(),Config::IMAGE_BYTES-4));};
    for(unsigned offset:{10u,11u,8u+4+18*3,8u+4+18*11+17}){auto bad=image;bad[offset]=1;reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;bad[30]=bad[12];bad[31]=bad[13];reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;bad[9]=0;std::memset(bad.data()+12,0,54);reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;bad[14]=0;reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));} // an empty pool
    {auto bad=image;bad[48+2+11]=0x80;reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));} // bit 95: no such effect (83-95 are reserved)
    Config x;assert(!x.decode(image.data(),image.size()-1));
    // 0.16's and 0.17's v2 record still decodes; a table within effects 0-63
    // writes it byte for byte as they did.
    Config narrow;const Row v2rows[]={{8000,Effect::Orbit},{13000,EffectSet::flag(Effect::Slash)|EffectSet::flag(Effect::Storm)|EffectSet::flag(Effect::Ravage)}};
    assert(narrow.set(Mode::Milestones,v2rows,2)==Problem::None && narrow.fitsV2());narrow.revision=5;
    Config::V2Image v2;narrow.encodeV2(v2);
    assert(Appearance::get32(v2.data())==Config::V2_MAGIC && v2[22]==0xc8 && v2[23]==0x32 && v2[24]==0x80 && v2[25]==0x40 && v2[29]==0x04);
    Config fromV2;assert(fromV2.decode(v2.data(),v2.size()) && fromV2.same(narrow) && fromV2.revision==5);
    for(size_t i=0;i<v2.size();++i){auto bad=v2;bad[i]^=2;Config y;assert(!y.decode(bad.data(),bad.size()));}
    {auto bad=v2;Appearance::put32(bad.data(),Config::MAGIC);Appearance::put32(bad.data()+132,Appearance::hash(bad.data(),132));assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;Appearance::put32(bad.data(),Config::V2_MAGIC);reseal(bad);assert(!x.decode(bad.data(),bad.size()));}
    // 0.15's v1 record still decodes, as single-effect rows; a newer image
    // under the v1 magic (or the reverse) does not.
    Config single;const Row v1rows[]={{8000,Effect::Orbit},{10500,Effect::Lightning},{13000,Effect::Slash}};
    assert(single.set(Mode::Milestones,v1rows,3)==Problem::None && single.fitsV1());single.revision=9;
    Config::V1Image v1;single.encodeV1(v1);
    assert(Appearance::get32(v1.data())==Config::V1_MAGIC && v1[12]==0x40 && v1[13]==0x1f && v1[14]==0 && v1[20]==7);
    Config fromV1;assert(fromV1.decode(v1.data(),v1.size()) && fromV1.same(single) && fromV1.revision==9);
    {auto bad=v1;Appearance::put32(bad.data(),Config::MAGIC);Appearance::put32(bad.data()+36,Appearance::hash(bad.data(),36));assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;Appearance::put32(bad.data(),Config::V1_MAGIC);reseal(bad);assert(!x.decode(bad.data(),bad.size()));}
    // Older firmware rejects the v3 image: wrong length for its records, and it never reads the v3 keys.
    static_assert(Config::IMAGE_BYTES!=Config::V1_IMAGE_BYTES && Config::IMAGE_BYTES!=Config::V2_IMAGE_BYTES);
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
    assert(fallback.select(Mode::Milestones) && damaged.save(fallback) && !damaged.damaged());
    Config fixed;AppearanceStore<Config> again;assert(again.begin(fixed,"ll-appearance","mile0","mile1",true) && !again.damaged() && fixed.mode()==Mode::Milestones);
    // A failed write leaves the change pending.
    Preferences::failWrite=true;assert(fixed.select(Mode::Best) && !again.save(fixed) && again.pending(fixed));Preferences::failWrite=false;
    assert(again.save(fixed));
  }
  // The packed record, a v3 table in NVS: every table of this firmware fits,
  // a round trip is exact, and any damage refuses it.
  {
    auto reseal=[](Config::PackedImage &x){Appearance::put32(x.data()+x.size()-4,Appearance::hash(x.data(),x.size()-4));};
    Config worst;Row rows[MAX_ROWS];
    for(unsigned i=0;i<MAX_ROWS;++i)rows[i]=Row(uint16_t(MAX_RPM-(MAX_ROWS-1-i)*997),EffectSet::ALL);
    assert(worst.set(Mode::Both,rows,MAX_ROWS)==Problem::None);worst.revision=0xfedcba98;
    Config::PackedImage b;assert(worst.encodePacked(b) && worst.packedWidth()==Appearance::EFFECTS);
    assert(unsigned(b[0]|b[1]<<8)==Config::PACKED_MAGIC && b[6]==(2|MAX_ROWS<<2) && b[7]==Appearance::EFFECTS);
    Config back;assert(back.decodePacked(b.data(),b.size()) && back.same(worst) && back.revision==worst.revision);
    for(size_t i=0;i<b.size();++i)for(unsigned bit=0;bit<8;++bit){auto bad=b;bad[i]^=uint8_t(1u<<bit);Config x;assert(!x.decodePacked(bad.data(),bad.size()));}
    {Config x;assert(!x.decodePacked(b.data(),b.size()-1));}
    // Resealed: a bit past the rows, a width past 128 bits, thirteen rows, no such mode, rows out of order.
    static_assert(MAX_ROWS*(Config::RPM_BITS+Appearance::EFFECTS)<Config::PACKED_ROW_BITS,"the last bit is unused");
    {auto bad=b;bad[Config::PACKED_IMAGE_BYTES-5]|=0x80;reseal(bad);Config x;assert(!x.decodePacked(bad.data(),bad.size()));}
    {auto bad=b;bad[7]=129;reseal(bad);Config x;assert(!x.decodePacked(bad.data(),bad.size()));}
    {auto bad=b;bad[6]=uint8_t(2|13<<2);reseal(bad);Config x;assert(!x.decodePacked(bad.data(),bad.size()));}
    {auto bad=b;bad[6]=uint8_t(3|12<<2);reseal(bad);Config x;assert(!x.decodePacked(bad.data(),bad.size()));}
    {Config ordered;const Row up[]={{9000,Effect::Slash},{9001,Effect::Orbit}};assert(ordered.set(Mode::Both,up,2)==Problem::None);
     Config::PackedImage two;assert(ordered.encodePacked(two));
     const unsigned at=Config::RPM_BITS+ordered.packedWidth();  // row 1's RPM, bit 0: 9001 becomes 9000
     two[8+at/8]&=uint8_t(~(1u<<(at%8)));reseal(two);Config z;assert(!z.decodePacked(two.data(),two.size()));}
    // A table of low effects packs narrower.
    {Config small;const Row few[]={{9000,EffectSet::flag(Effect::Blink)},{12000,Effect::Slash}};assert(small.set(Mode::Milestones,few,2)==Problem::None);
     Config::PackedImage x;assert(small.encodePacked(x) && small.packedWidth()==unsigned(Effect::Blink)+1);
     Config y;assert(y.decodePacked(x.data(),x.size()) && y.same(small));}
    // It takes the NVS entries of 0.16's and 0.17's slot.
    static_assert(NvsBudget::blobEntries(Config::PACKED_IMAGE_BYTES)==NvsBudget::blobEntries(Config::V2_IMAGE_BYTES) &&
                  NvsBudget::blobEntries(Config::V2_IMAGE_BYTES)==8);
  }
  // What 0.16 reads, and what a rolled-back unit shows for a table it cannot.
  {
    Config t;const Row r[]={{9000,EffectSet::flag(Effect::Sparkles)|EffectSet::flag(Effect::Blink)},{11500,Effect::Crimson},{15500,Effect::Petalfall}};
    assert(t.set(Mode::Both,r,3)==Problem::None && !t.fitsV016());t.revision=7;
    const Config o=t.forV016();
    assert(o.fitsV016() && o.fitsV2() && o.revision==6 && o.mode()==Mode::Both && o.count()==3 && Config::check(o.mode(),&o.row(0),o.count())==Problem::None);
    assert(o.row(0).rpm==9000 && o.row(0).pool==EffectSet::flag(Effect::Sparkles));
    assert(o.row(1).rpm==11500 && o.row(1).pool==EffectSet::flag(Effect::Orbit));   // the defaults at 11K
    assert(o.row(2).rpm==15500 && o.row(2).pool==EffectSet::flag(Effect::Slash));   // and from 15K
    Config low;const Row l[]={{1500,Effect::Crimson}};assert(low.set(Mode::Milestones,l,1)==Problem::None);
    assert(low.forV016().row(0).pool==EffectSet::flag(Effect::Sparkles));            // below the defaults: their first
    Config old;const Row k[]={{9000,EffectSet::flag(Effect::Orbit)|EffectSet::flag(Effect::Slash)}};assert(old.set(Mode::Both,k,1)==Problem::None && old.fitsV016());
    assert(Config().fitsV016());
  }
  // Requests: Set carries a checked body, Reset none; shape and checksum.
  // v3 ("LLU\3") carries 128-bit pools; 0.16's v2 ("LLU\2") and 0.15's v1
  // ("LLU\1") still work.
  {
    Config t;const Row rows[]={{9500,Effect::Shockwave},{14500,EffectSet::flag(Effect::Slash)|EffectSet::flag(Effect::Bloom)|EffectSet::flag(Effect::Meadow)}};
    assert(t.set(Mode::Both,rows,2)==Problem::None);
    uint8_t b[Request::BYTES];Request::encode(Request::Op::Set,&t,b);
    Request q;Problem p;unsigned row=9;
    assert(Appearance::get32(b)==Request::MAGIC && q.decode(b,sizeof(b),p,row) && p==Problem::None && q.op==Request::Op::Set && q.mode==Mode::Both && q.count==2 && q.version==3);
    assert(q.rows[0].rpm==9500 && q.rows[1]==rows[1]);
    for(size_t i=0;i<sizeof(b);++i){uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[i]^=4;Request x;assert(!x.decode(bad,sizeof(bad),p,row) || p!=Problem::None);}
    auto reseal=[](uint8_t *x,size_t n){Appearance::put32(x+n-4,Appearance::hash(x,n-4));};
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[30]=0x10;bad[31]=0x27;reseal(bad,sizeof(bad)); // row 1 at 10000: fine
     Request x;assert(x.decode(bad,sizeof(bad),p,row) && p==Problem::None);
     bad[30]=0x0c;bad[31]=0x25;reseal(bad,sizeof(bad));assert(x.decode(bad,sizeof(bad),p,row) && p==Problem::Order && row==1);}
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[5]=1;reseal(bad,sizeof(bad));Request x;assert(!x.decode(bad,sizeof(bad),p,row));}
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[4]=3;reseal(bad,sizeof(bad));Request x;assert(!x.decode(bad,sizeof(bad),p,row));}
    uint8_t r[Request::BYTES];Request::encode(Request::Op::Reset,nullptr,r);
    assert(q.decode(r,sizeof(r),p,row) && q.op==Request::Op::Reset && p==Problem::None);
    r[20]=1;reseal(r,sizeof(r));assert(!q.decode(r,sizeof(r),p,row));
    assert(!q.decode(b,sizeof(b)-1,p,row));
    // v2: 0.16's and 0.17's web editor's request decodes, 64-bit pools.
    Config narrow;const Row v2rows[]={{9500,Effect::Shockwave},{14500,EffectSet::flag(Effect::Slash)|EffectSet::flag(Effect::Bloom)}};
    assert(narrow.set(Mode::Both,v2rows,2)==Problem::None && narrow.fitsV2());
    uint8_t v2[Request::V2_BYTES];Request::encode(Request::Op::Set,&narrow,v2,2);
    assert(Appearance::get32(v2)==Request::V2_MAGIC && q.decode(v2,sizeof(v2),p,row) && p==Problem::None && q.version==2 && q.count==2 && q.rows[1]==v2rows[1]);
    uint8_t v2reset[Request::V2_BYTES];Request::encode(Request::Op::Reset,nullptr,v2reset,2);assert(q.decode(v2reset,sizeof(v2reset),p,row) && q.op==Request::Op::Reset && q.version==2);
    // v1: 0.15's web editor's request decodes as single-effect rows.
    Config single;const Row v1rows[]={{9500,Effect::Shockwave},{14500,Effect::Slash}};assert(single.set(Mode::Both,v1rows,2)==Problem::None);
    uint8_t v1[Request::V1_BYTES];Request::encode(Request::Op::Set,&single,v1,1);
    assert(Appearance::get32(v1)==Request::V1_MAGIC && q.decode(v1,sizeof(v1),p,row) && p==Problem::None && q.version==1 && q.count==2 && q.rows[1]==v1rows[1]);
    uint8_t v1reset[Request::V1_BYTES];Request::encode(Request::Op::Reset,nullptr,v1reset,1);assert(q.decode(v1reset,sizeof(v1reset),p,row) && q.op==Request::Op::Reset);
    // A body under another version's magic is refused.
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));Appearance::put32(bad,Request::V2_MAGIC);reseal(bad,sizeof(bad));assert(!q.decode(bad,sizeof(bad),p,row));}
    {uint8_t bad[Request::V2_BYTES];std::memcpy(bad,v2,sizeof(v2));Appearance::put32(bad,Request::MAGIC);reseal(bad,sizeof(bad));assert(!q.decode(bad,sizeof(bad),p,row));}
    {uint8_t bad[Request::V2_BYTES];std::memcpy(bad,v2,sizeof(v2));Appearance::put32(bad,Request::V1_MAGIC);reseal(bad,sizeof(bad));assert(!q.decode(bad,sizeof(bad),p,row));}
    // Older firmware decodes only its own lengths, so a v3 request is refused
    // there as invalid_request before anything is written (test_milestone_compat
    // checks the frozen sources themselves).
    static_assert(Request::BYTES!=Request::V1_BYTES && Request::BYTES!=Request::V2_BYTES && Request::MAGIC!=Request::V2_MAGIC);
  }
  // The shared vectors the web editor tests against are current.
  {
    std::ifstream file("firmware/tests/fixtures/milestone_vectors.json");std::stringstream text;text<<file.rdbuf();
    if(text.str()!=MilestoneVectors::json()) {
      std::cerr<<"firmware/tests/fixtures/milestone_vectors.json is stale: regenerate it with milestone_fixture.cpp\n";return 1;
    }
  }
  // Loaded effects (slots 96-127) in pools: a new table names only slots that
  // play; the "L4" record keeps them in the same 160 bytes; a slot deleted
  // since still loads and its fallback plays; the other slot gets the view
  // 0.18 reads, each slot replaced by its stock fallback.
  {
    auto &h=Appearance::loadedHooks();
    h.playable=[](unsigned e){return e==96 || e==120;};
    h.fallback=[](unsigned e)->unsigned{return e==96?unsigned(Effect::Tidal):e==120?unsigned(Effect::Blink):e==110?unsigned(Effect::Frost):~0u;};
    using EffectSet::flag;
    const Row rows[]={{9000,Effect::Orbit},{12000,flag(Effect::Slash)|flag(96)},{15000,flag(120)}};
    Config c;assert(c.set(Mode::Both,rows,3)==Problem::None && c.namesLoaded() && !c.fitsV2() && c.fits());
    const Row missing[]={{9000,flag(110)}};
    {Config d;assert(d.set(Mode::Both,missing,1)==Problem::Effect);}  // slot 110 does not play
    Config::PackedImage b;assert(c.encodePacked(b) && unsigned(b[0]|b[1]<<8)==Config::PACKED_LOADED_MAGIC && Appearance::get32(b.data()+8)==((1u<<0)|(1u<<24)));
    Config back;assert(back.decodePacked(b.data(),b.size()) && back.same(c));
    // The view for 0.18: Tidal for 96, Blink for 120, in the same rows.
    const Config view=c.forOlder();
    assert(view.revision==c.revision-1 && !view.namesLoaded() && view.row(1).pool==(flag(Effect::Slash)|flag(Effect::Tidal)) && view.row(2).pool==flag(Effect::Blink));
    Config::PackedImage vb;assert(view.encodePacked(vb) && unsigned(vb[0]|vb[1]<<8)==Config::PACKED_MAGIC);
    // Slot 120 deleted: the stored table still loads, and its row plays Blink.
    h.playable=[](unsigned e){return e==96;};
    Config later;assert(later.decodePacked(b.data(),b.size()) && later.same(c));
    assert(playable(later.row(2).pool)==flag(Effect::Blink) && playable(later.row(1).pool)==(flag(Effect::Slash)|flag(96)));
    assert(later.select(Mode::Milestones) && later.mode()==Mode::Milestones);  // the mode still changes
    // A table naming every slot in every row does not fit its record.
    h.playable=[](unsigned e){return Appearance::loaded(e);};
    Row wide[MAX_ROWS];for(unsigned i=0;i<MAX_ROWS;++i)wide[i]={uint16_t(9000+i*500),EffectSet::ALL|EffectSet::LOADED};
    Config w;assert(w.set(Mode::Both,wide,MAX_ROWS)==Problem::None && !w.fits() && !w.encodePacked(b));
    // The store: the table in one slot, the view in the other, both read back.
    Preferences::data.clear();
    h.playable=[](unsigned e){return e==96 || e==120;};
    MilestoneStore store;Config fresh;assert(!store.begin(fresh));
    c.revision+=2;assert(store.save(c));
    Config reread;MilestoneStore again;assert(again.begin(reread) && reread.same(c) && reread.revision==c.revision);
    h=Appearance::LoadedHooks();
  }
  std::cout<<"PASS: milestone defaults, table rules with failing rows, effect pools and picks across 128 bits, v2 and v1 import and compatibility, shown-RPM thresholds, one-animation rule, record round trip and damage, damaged-record fallback, NVS budget, USB requests, shared web vectors, loaded slots and their 0.18 view\n";
}
