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
  static_assert(Config::IMAGE_BYTES==40 && Request::BYTES==40 && MAX_ROWS==8);
  // Defaults: today's behaviour (New best), 9K..15K escalating to Slash.
  {
    Config d;
    assert(d.mode()==Mode::Best && d.count()==7 && d.isDefault() && d.revision==0);
    const Effect expected[]={Effect::Sparkles,Effect::Sparkles,Effect::Orbit,Effect::Flames,Effect::Shockwave,Effect::Lightning,Effect::Slash};
    for(unsigned i=0;i<7;++i)assert(d.row(i).rpm==9000+1000*i && d.row(i).effect==expected[i]);
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
    Row nine[9];for(unsigned i=0;i<9;++i)nine[i]={uint16_t(9000+i*100),Effect::Orbit};
    assert(Config::check(Mode::Both,nine,9)==Problem::Count && Config::check(Mode::Both,nine,8)==Problem::None);
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
    assert(c.reached(29999)->rpm==15000 && c.reached(29999)->effect==Effect::Slash);
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
    char text[24];milestoneText(text,sizeof(text),12000);assert(!std::strcmp(text,"MILESTONE 12K"));
    milestoneText(text,sizeof(text),12500);assert(!std::strcmp(text,"MILESTONE 12.5K"));
    milestoneText(text,sizeof(text),12345);assert(!std::strcmp(text,"MILESTONE 12345"));
    milestoneText(text,sizeof(text),30000);assert(!std::strcmp(text,"MILESTONE 30K"));
  }
  // Record: exact round trip; any damage, a checksummed invalid table or
  // nonzero unused bytes is rejected.
  {
    Config c;const Row rows[]={{8000,Effect::Orbit},{10500,Effect::Lightning},{13000,Effect::Slash}};
    assert(c.set(Mode::Milestones,rows,3)==Problem::None);c.revision=0x01020304;
    Config::Image image;c.encode(image);
    assert(Appearance::get32(image.data())==Config::MAGIC && image[8]==1 && image[9]==3 && image[12]==0x40 && image[13]==0x1f && image[14]==0);
    Config back;assert(back.decode(image.data(),image.size()) && back.same(c) && back.revision==c.revision);
    for(size_t i=0;i<image.size();++i)for(uint8_t bit:{1,0x80}){auto bad=image;bad[i]^=bit;Config x;assert(!x.decode(bad.data(),bad.size()));}
    auto reseal=[](Config::Image &b){Appearance::put32(b.data()+Config::IMAGE_BYTES-4,Appearance::hash(b.data(),Config::IMAGE_BYTES-4));};
    for(unsigned offset:{10u,11u,8u+4+3*3,8u+4+3*7+2}){auto bad=image;bad[offset]=1;reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;bad[12+3]=bad[12];bad[12+4]=bad[13];reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));}
    {auto bad=image;bad[9]=0;std::memset(bad.data()+12,0,24);reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));}
    Config x;assert(!x.decode(image.data(),image.size()-1));
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
  // NVS: a rewrite of the record costs a few entries, beside a history rewrite.
  static_assert(NvsBudget::blobEntries(Config::IMAGE_BYTES)==5 && NvsBudget::alternatingStoreEntries(Config::IMAGE_BYTES)==15);
  static_assert(NvsBudget::blobEntries(Config::IMAGE_BYTES)+NvsBudget::blobEntries(PracticeHistory::IMAGE_BYTES)<126,"fits in the free page");
  // Requests: Set carries a checked body, Reset none; shape and checksum.
  {
    Config t;const Row rows[]={{9500,Effect::Shockwave},{14500,Effect::Slash}};assert(t.set(Mode::Both,rows,2)==Problem::None);
    uint8_t b[Request::BYTES];Request::encode(Request::Op::Set,&t,b);
    Request q;Problem p;unsigned row=9;
    assert(q.decode(b,sizeof(b),p,row) && p==Problem::None && q.op==Request::Op::Set && q.mode==Mode::Both && q.count==2);
    assert(q.rows[0].rpm==9500 && q.rows[1].effect==Effect::Slash);
    for(size_t i=0;i<sizeof(b);++i){uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[i]^=4;Request x;assert(!x.decode(bad,sizeof(bad),p,row) || p!=Problem::None);}
    auto reseal=[](uint8_t *x){Appearance::put32(x+Request::BYTES-4,Appearance::hash(x,Request::BYTES-4));};
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[8+4+3]=0x10;bad[8+4+4]=0x27;reseal(bad); // row 1 at 10000: fine
     Request x;assert(x.decode(bad,sizeof(bad),p,row) && p==Problem::None);
     bad[8+4+3]=0x0c;bad[8+4+4]=0x25;reseal(bad);assert(x.decode(bad,sizeof(bad),p,row) && p==Problem::Order && row==1);}
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[5]=1;reseal(bad);Request x;assert(!x.decode(bad,sizeof(bad),p,row));}
    {uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[4]=3;reseal(bad);Request x;assert(!x.decode(bad,sizeof(bad),p,row));}
    uint8_t r[Request::BYTES];Request::encode(Request::Op::Reset,nullptr,r);
    assert(q.decode(r,sizeof(r),p,row) && q.op==Request::Op::Reset && p==Problem::None);
    r[20]=1;reseal(r);assert(!q.decode(r,sizeof(r),p,row));
    assert(!q.decode(b,sizeof(b)-1,p,row));
  }
  // The shared vectors the web editor tests against are current.
  {
    std::ifstream file("firmware/tests/fixtures/milestone_vectors.json");std::stringstream text;text<<file.rdbuf();
    if(text.str()!=MilestoneVectors::json()) {
      std::cerr<<"firmware/tests/fixtures/milestone_vectors.json is stale: regenerate it with milestone_fixture.cpp\n";return 1;
    }
  }
  std::cout<<"PASS: milestone defaults, table rules with failing rows, shown-RPM thresholds, one-animation rule, record round trip and damage, damaged-record fallback, NVS budget, USB requests, shared web vectors\n";
}
