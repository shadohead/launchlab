// The effect set: its rules, the Best effect cycle and Random's pool, the
// stored record and its damaged-record fallback, the USB set_effects request,
// the NVS budget, Wipe all, and the web editor's shared vectors.
#include "../LaunchLabMini/appearance_store.h"
#include "../LaunchLabMini/controls.h"
#include "../LaunchLabMini/device_wipe.h"
#include "../LaunchLabMini/effect_set.h"
#include "../LaunchLabMini/milestones.h"
#include "../LaunchLabMini/nvs_budget.h"
#include "../LaunchLabMini/practice_history.h"
#include "effect_set_vectors.h"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main() {
  using namespace EffectSet;using Appearance::Effect;
  static_assert(Config::IMAGE_BYTES==28 && Request::BYTES==40 && Request::OP==3 && Config::MAGIC==0x01534c4c);
  // The request uses the milestone request's frame, so 0.15 and 0.16 read
  // its magic, size and op and refuse it.
  static_assert(Request::BYTES==Milestones::Request::V1_BYTES && Request::MAGIC==Milestones::Request::V1_MAGIC &&
                Request::BODY_BYTES==Milestones::V1_BODY_BYTES);
  // Masks: every drawn effect, and the default of the 16 effects of 0.16.
  {
    static_assert(DEFAULT_MASK==Mask(0x3ff9fULL) && Mask::BITS==128 && Appearance::EFFECTS<=Mask::BITS);
    assert(count(ALL)==Appearance::DRAWN && count(DEFAULT_MASK)==16);
    for(unsigned e=0;e<Mask::BITS+2;++e) {
      assert(bool(ALL&flag(e))==Appearance::drawn(e));
      assert(bool(DEFAULT_MASK&flag(e))==(Appearance::drawn(e) && e<18));
      assert(offers(DEFAULT_MASK,Effect(e))==(Appearance::drawn(e) && e<18));
    }
    assert(!offers(ALL,Effect::Off) && !offers(ALL,Effect::Random) && !offers(~Mask(),Effect::Off) && !offers(~Mask(),Effect(Appearance::EFFECTS)) && !offers(~Mask(),Effect(127)));
    unsigned at=99;
    assert(check(DEFAULT_MASK,&at)==Problem::None && at==0 && check(ALL)==Problem::None && check(flag(Effect::Slash))==Problem::None);
    assert(check(Mask(),&at)==Problem::Empty && at==0);
    assert(check(DEFAULT_MASK|flag(5),&at)==Problem::Effect && at==5);
    assert(check(flag(6),&at)==Problem::Effect && at==6);
    assert(check(flag(0)|flag(Appearance::EFFECTS),&at)==Problem::Effect && at==Appearance::EFFECTS);
    assert(check(flag(Effect::Volley)|flag(Effect::Starfall)|flag(Effect::Sharkfin))==Problem::None); // both words
    assert(check(flag(1)|flag(127),&at)==Problem::Effect && at==127);
    // The upper word: bits 64-127, low word first in the hex form.
    {char hex[33];(flag(0)|flag(64)|flag(127)).hex(hex);assert(!std::strcmp(hex,"80000000000000010000000000000001"));}
    assert(!std::strcmp(problemName(Problem::Empty),"empty") && !std::strcmp(problemName(Problem::Effect),"effect"));
  }
  // The Best effect cycle: Random, the offered effects in Settings order, Off.
  {
    // The default set is exactly the 0.16 cycle.
    Effect e=Effect::Random;Mask seen;unsigned steps=0;
    do{seen|=flag(e);e=next(e,DEFAULT_MASK);++steps;}while(e!=Effect::Random && steps<64);
    assert(seen==(DEFAULT_MASK|flag(Effect::Off)|flag(Effect::Random)) && steps==18);
    assert(next(Effect::Random,DEFAULT_MASK)==Effect::Orbit && next(Effect::Lightning,DEFAULT_MASK)==Effect::Slash &&
           next(Effect::Bloom,DEFAULT_MASK)==Effect::Off && next(Effect::Off,DEFAULT_MASK)==Effect::Random);
    // With every effect offered it is Appearance::nextChoice.
    for(unsigned v=0;v<=Appearance::EFFECTS;++v)assert(next(Effect(v),ALL)==Appearance::nextChoice(Effect(v)));
    // A picked set: Random, Slash, Crimson, Ravage, Off.
    const Mask mine=flag(Effect::Slash)|flag(Effect::Crimson)|flag(Effect::Ravage);
    assert(next(Effect::Random,mine)==Effect::Slash && next(Effect::Slash,mine)==Effect::Crimson &&
           next(Effect::Crimson,mine)==Effect::Ravage && next(Effect::Ravage,mine)==Effect::Off && next(Effect::Off,mine)==Effect::Random);
    // A saved effect the set no longer offers continues to the next offered one after its position.
    assert(next(Effect::Orbit,mine)==Effect::Slash && next(Effect::Tidal,mine)==Effect::Crimson && next(Effect::Bloom,mine)==Effect::Crimson &&
           next(Effect::Moonpetal,mine)==Effect::Ravage && next(Effect::Umbra,mine)==Effect::Ravage);
    assert(next(Effect::Ravage,flag(Effect::Orbit))==Effect::Off && next(Effect(200),mine)==Effect::Random);
    // Through the controls: the editor's B presses visit only offered effects,
    // starting from a saved one outside the set.
    Controls::State s;s.offeredEffects=mine;s.page=Controls::Page::Edit;s.setting=uint8_t(Controls::Setting::BestEffect);
    s.draft=uint8_t(Effect::Spin);
    const Effect expected[]={Effect::Crimson,Effect::Ravage,Effect::Off,Effect::Random,Effect::Slash,Effect::Crimson};
    for(Effect want:expected){assert(s.tapB()==Controls::Effect::Preview);assert(s.draft==uint8_t(want));}
    // A fresh state offers the default set.
    Controls::State fresh;assert(fresh.offeredEffects==DEFAULT_MASK);
  }
  // Random: only offered effects, never the previous pick while another is
  // offered; one offered effect always plays.
  {
    for(Mask mask:{DEFAULT_MASK,ALL,flag(Effect::Slash)|flag(Effect::Crimson),flag(Effect::Orbit)|flag(Effect::Crimson)|flag(Effect::Ravage),
                        EffectSetVectors::of({2,8,13,23,33,38})}) {
      Mask picked;
      for(uint32_t bits=0;bits<600;++bits)for(unsigned prev=0;prev<=Appearance::EFFECTS;++prev) {
        const Effect p=pick(bits*2654435761u,Effect(prev),mask);
        assert(offers(mask,p) && (p!=Effect(prev) || count(mask)==1));picked|=flag(p);
      }
      assert(picked==mask);
    }
    for(uint32_t bits=0;bits<100;++bits)for(unsigned prev=0;prev<=Appearance::EFFECTS;++prev) {
      assert(pick(bits,Effect(prev),flag(Effect::Tidal))==Effect::Tidal);
      // With every effect offered it is Appearance::pick.
      assert(pick(bits*2654435761u,Effect(prev),ALL)==Appearance::pick(bits*2654435761u,Effect(prev)));
    }
    // A previous pick outside the set leaves the whole set to draw from.
    assert(pick(0,Effect::Bloom,flag(Effect::Crimson)|flag(Effect::Bulwark))==Effect::Crimson && pick(1,Effect::Bloom,flag(Effect::Crimson)|flag(Effect::Bulwark))==Effect::Bulwark);
    // An impossible empty mask still picks a drawn effect.
    assert(offers(DEFAULT_MASK,pick(3,Effect::Off,Mask())));
  }
  // Record: defaults, a change bumps the revision, refused masks change
  // nothing, exact round trip, any damage rejected.
  {
    Config c;assert(c.isDefault() && c.mask()==DEFAULT_MASK && c.revision==0 && c.count()==16 && c.offers(Effect::Bloom) && !c.offers(Effect::Crimson));
    unsigned at=0;
    for(Mask bad:{Mask(),flag(5),flag(Appearance::EFFECTS),flag(100),DEFAULT_MASK|flag(6)})assert(c.set(bad,&at)!=Problem::None && c.isDefault() && c.revision==0);
    const Mask mine=flag(Effect::Slash)|flag(Effect::Crimson)|flag(Effect::Ravage);
    assert(c.set(mine)==Problem::None && c.revision==1 && c.mask()==mine && !c.isDefault() && c.count()==3);
    assert(c.set(mine)==Problem::None && c.revision==1);
    c.revision=0x01020304;Config::Image image;c.encode(image);
    assert(Appearance::get32(image.data())==Config::MAGIC && image[4]==4 && image[8]==0x80 && image[10]==0x04 && image[13]==0x04 && image[15]==0);
    for(unsigned i=16;i<24;++i)assert(!image[i]);
    Config back;assert(back.decode(image.data(),image.size()) && back.mask()==mine && back.revision==c.revision);
    for(size_t i=0;i<image.size();++i)for(uint8_t flip:{1,0x80}){auto bad=image;bad[i]^=flip;Config x;assert(!x.decode(bad.data(),bad.size()) && x.isDefault());}
    auto reseal=[](Config::Image &b){Appearance::put32(b.data()+24,Appearance::hash(b.data(),24));};
    for(Mask mask:{Mask(),flag(5),flag(6),flag(Appearance::EFFECTS),flag(Appearance::EFFECTS)|flag(0),flag(100),flag(127)|flag(0)}){auto bad=image;putMask(bad.data()+8,mask);reseal(bad);Config x;assert(!x.decode(bad.data(),bad.size()));}
    Config x;assert(!x.decode(image.data(),image.size()-1));
    c.reset();assert(c.isDefault() && c.revision==0x01020305);
  }
  // Store: alternating verified slots in ll-appearance beside the other
  // records; a damaged record (both slots bad) offers the default, stays
  // until a change, and the next save replaces it.
  {
    Preferences::data.clear();Preferences::zeroLengthKeys.clear();
    Preferences::data["ll-appearancemile0"]={1,2,3};Preferences::data["ll-appearancecfg0"]={4,5,6};
    Config c;AppearanceStore<Config> store;assert(store.begin(c,"ll-appearance","eset0","eset1",true) && !store.damaged() && c.isDefault());
    assert(!store.pending(c) && Preferences::data.size()==2);
    for(unsigned i=0;i<5;++i){assert(c.set(flag(Effect::Crimson)|flag(i))==Problem::None);assert(store.pending(c) && store.save(c));}
    assert((Preferences::data["ll-appearancemile0"]==std::vector<uint8_t>{1,2,3}) && (Preferences::data["ll-appearancecfg0"]==std::vector<uint8_t>{4,5,6}));
    assert(Preferences::data["ll-appearanceeset0"].size()==Config::IMAGE_BYTES && Preferences::data["ll-appearanceeset1"].size()==Config::IMAGE_BYTES);
    Config reread;AppearanceStore<Config> reader;assert(reader.begin(reread,"ll-appearance","eset0","eset1",true) && reread.mask()==c.mask() && reread.revision==c.revision);
    // One damaged slot: the other, older generation loads.
    Preferences::data["ll-appearanceeset1"][9]^=1;
    Config older;AppearanceStore<Config> partial;assert(partial.begin(older,"ll-appearance","eset0","eset1",true) && !partial.damaged() && older.revision==c.revision-1);
    // Both damaged: the default, and nothing is written until a change.
    Preferences::data["ll-appearanceeset0"][9]^=1;const auto kept=Preferences::data;
    Config fallback;AppearanceStore<Config> damaged;
    assert(!damaged.begin(fallback,"ll-appearance","eset0","eset1",true) && damaged.damaged() && damaged.ready() && fallback.isDefault());
    assert(!damaged.pending(fallback) && Preferences::data==kept);
    // A record from a firmware with more effects reads as damaged here too.
    {auto future=kept;Config::Image image;Config f;f.encode(image);putMask(image.data()+8,DEFAULT_MASK|flag(100));Appearance::put32(image.data()+24,Appearance::hash(image.data(),24));
     Preferences::data["ll-appearanceeset0"].assign(image.begin(),image.end());Preferences::data["ll-appearanceeset1"].assign(image.begin(),image.end());
     Config g;AppearanceStore<Config> newer;assert(!newer.begin(g,"ll-appearance","eset0","eset1",true) && newer.damaged() && g.isDefault());
     Preferences::data=kept;}
    // The next change replaces it, even with the default set (the sketch
    // bumps the revision when the set is unchanged).
    ++fallback.revision;assert(damaged.pending(fallback) && damaged.save(fallback) && !damaged.damaged());
    Config fixed;AppearanceStore<Config> again;assert(again.begin(fixed,"ll-appearance","eset0","eset1",true) && !again.damaged() && fixed.isDefault());
    // A failed write leaves the change pending.
    Preferences::failWrite=true;assert(fixed.set(flag(Effect::Orbit))==Problem::None && !again.save(fixed) && again.pending(fixed));Preferences::failWrite=false;
    assert(again.save(fixed));
    // Wipe all clears ll-appearance, so the set goes with it.
    assert(DeviceWipe::ownedNamespace("ll-appearance"));
  }
  // NVS: a rewrite costs four entries, beside a history rewrite.
  static_assert(NvsBudget::blobEntries(Config::IMAGE_BYTES)==4 && NvsBudget::alternatingStoreEntries(Config::IMAGE_BYTES)==12);
  static_assert(NvsBudget::blobEntries(Config::IMAGE_BYTES)+NvsBudget::blobEntries(PracticeHistory::IMAGE_BYTES)<126,"fits in the free page");
  // Requests: op 3 carries the mask in body bytes 0-15, the rest zero.
  {
    const Mask mine=flag(Effect::Orbit)|flag(Effect::Crimson)|flag(Effect::Ravage);
    uint8_t b[Request::BYTES];Request::encode(mine,b);
    assert(Appearance::get32(b)==0x01554c4c && b[4]==3 && !b[5] && !b[6] && !b[7] && getMask(b+8)==mine);
    for(unsigned i=24;i<36;++i)assert(!b[i]);
    Mask mask;Problem p;unsigned at=9;
    assert(Request::claims(b,sizeof(b)) && Request::decode(b,sizeof(b),mask,p,at) && mask==mine && p==Problem::None);
    // Any damage is refused or (a mask bit) judged by the rules.
    for(size_t i=0;i<sizeof(b);++i)for(uint8_t flip:{4,0x80}){uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[i]^=flip;
      assert(!Request::decode(bad,sizeof(bad),mask,p,at) || p!=Problem::None);}
    auto reseal=[](uint8_t *x){Appearance::put32(x+Request::BYTES-4,Appearance::hash(x,Request::BYTES-4));};
    for(unsigned offset:{5u,6u,7u,24u,30u,35u}){uint8_t bad[Request::BYTES];std::memcpy(bad,b,sizeof(b));bad[offset]=1;reseal(bad);assert(!Request::decode(bad,sizeof(bad),mask,p,at));}
    // A checksummed invalid mask is a well-formed request with a problem.
    for(Mask m:{Mask(),flag(5),flag(Appearance::EFFECTS),flag(100),flag(127)}){uint8_t bad[Request::BYTES];Request::encode(m,bad);
      assert(Request::decode(bad,sizeof(bad),mask,p,at) && p!=Problem::None && mask==m);}
    {uint8_t bad[Request::BYTES];Request::encode(flag(Appearance::EFFECTS)|1,bad);assert(Request::decode(bad,sizeof(bad),mask,p,at) && p==Problem::Effect && at==Appearance::EFFECTS);}
    assert(!Request::claims(b,sizeof(b)-1) && !Request::decode(b,sizeof(b)-1,mask,p,at));
    // Milestone requests are not claimed: they stay with the milestone code.
    Milestones::Config table;uint8_t set[Milestones::Request::BYTES],reset[Milestones::Request::BYTES];
    for(bool v1:{false,true}) {
      const size_t n=v1?Milestones::Request::V1_BYTES:Milestones::Request::BYTES;
      Milestones::Request::encode(Milestones::Request::Op::Set,&table,set,v1);Milestones::Request::encode(Milestones::Request::Op::Reset,nullptr,reset,v1);
      assert(!Request::claims(set,n) && !Request::claims(reset,n));
    }
    // The milestone decoder refuses op 3 in the v1 frame, which the sketch
    // reports as invalid_request (test_effect_set_compat checks the frozen
    // 0.15 and 0.16 sources themselves).
    Milestones::Request old;Milestones::Problem problem;unsigned row;
    for(Mask m:{mine,DEFAULT_MASK,ALL,flag(Effect::Slash)}){Request::encode(m,b);assert(!old.decode(b,sizeof(b),problem,row));}
  }
  // The shared vectors the web editor tests against are current.
  {
    std::ifstream file("firmware/tests/fixtures/effect_set_vectors.json");std::stringstream text;text<<file.rdbuf();
    if(text.str()!=EffectSetVectors::json()) {
      std::cerr<<"firmware/tests/fixtures/effect_set_vectors.json is stale: regenerate it with milestone_fixture.cpp effect-set\n";return 1;
    }
  }
  std::cout<<"PASS: effect set masks and default, the Best effect cycle (through Controls) and Random's pool, record round trip and damage, "
             "damaged-record fallback beside the other ll-appearance records, Wipe all, NVS budget, set_effects requests refused by the milestone decoder, shared web vectors\n";
}
