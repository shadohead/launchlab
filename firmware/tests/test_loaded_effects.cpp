// The firmware's loaded effects (loaded_effects.h) on the NOR flash model:
// installed slots reach the rest of the firmware through Appearance (name,
// length, Settings order, effect sets), draw exactly the frames the engine
// draws from the file, survive a reboot, and once deleted resolve to their
// fallback; a slot that no longer loads is listed but never played; a play
// that keeps running over the guard is given up for the clean recap.
#include "../LaunchLabMini/loaded_effects.h"
#include "../LaunchLabMini/effect_set.h"
#include "../tools/fx/fx_compiler.h"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <vector>

#include "nor_flash.h"

namespace {
using Loaded=LoadedEffects<NorFlash>;
std::vector<uint8_t> compileText(const std::string &text) {
  const auto r=FxCompiler::compile(text);
  if(!r.ok)std::cerr<<r.line<<": "<<r.error<<"\\n";
  assert(r.ok);return r.bytes;
}
std::vector<uint8_t> comet(uint64_t uid,const char *name,const char *fallback,unsigned duration=3000) {
  char b[512];std::snprintf(b,sizeof(b),"effect \"%s\"\nuid 0x%llx\nduration %u\nfallback \"%s\"\n"
    "repeat 8 {\n  a = t*3 + i*.78\n  dot(acx + 40*cos(a), acy + 30 + 16*sin(a), accent, 1 - t/3)\n}\n",name,(unsigned long long)uid,duration,fallback);
  return compileText(b);
}
bool never(unsigned){return false;}
}

int main() {
  static BestEffectUI::Renderer renderer;
  std::vector<uint8_t> file(EffectStore<NorFlash>::MAX_PAYLOAD);alignas(4) static uint8_t hot[LoadableFx::HOT_BYTES];
  NorFlash flash;
  {
    Loaded loaded(renderer,file.data(),file.size(),hot);
    assert(loaded.begin(flash)==EffectStore<NorFlash>::Open::Unformatted && !Appearance::celebrates(96));
    bool failed=false;while(!loaded.store().prepareStep(failed))assert(!failed);
    const auto a=comet(0xA1,"Comet","Tidal"),b=comet(0xB2,"Glass","Sparkles",4500);
    unsigned slot=99;
    assert(loaded.install(a.data(),a.size(),nullptr,0,never,slot)==EffectStore<NorFlash>::Result::Ok && slot==0);
    assert(loaded.install(b.data(),b.size(),nullptr,0,never,slot)==EffectStore<NorFlash>::Result::Ok && slot==1);
    // Through Appearance, as the rest of the firmware sees them.
    assert(Appearance::celebrates(96) && Appearance::celebrates(97) && !Appearance::celebrates(98));
    assert(!std::strcmp(Appearance::label(Appearance::Effect(96)),"Comet") && Appearance::duration(Appearance::Effect(97))==4500);
    assert(Appearance::nextChoice(Appearance::Effect::Blink)==Appearance::Effect(96) && EffectSet::celebrating().has(97));
    // A frame drawn through the store equals the engine's frame from the file.
    for(uint32_t ms:{0u,700u,1900u}) {
      std::vector<uint16_t> x(135*240,0),y(135*240,0);
      assert(loaded.draw({x.data(),135,240},96,ms,0x07ff,BestEffectUI::Renderer::Anchor(),0));
      LoadableFx::Program p;assert(LoadableFx::load(a.data(),a.size(),p)==LoadableFx::Error::None);
      static BestEffectUI::Renderer other;LoadableFx::Engine e(other);e.draw({y.data(),135,240},p,ms,0x07ff);
      assert(x==y);
    }
    // Two slots alternating: each draws its own file.
    std::vector<uint16_t> s0(135*240,0),s1(135*240,0);
    assert(loaded.draw({s0.data(),135,240},96,500,0x07ff,BestEffectUI::Renderer::Anchor(),0) &&
           loaded.draw({s1.data(),135,240},97,500,0x07ff,BestEffectUI::Renderer::Anchor(),0) && s0==s1);  // the same motion: both are comets
    assert(!loaded.draw({s0.data(),135,240},98,500,0x07ff,BestEffectUI::Renderer::Anchor(),0));
    // Over the guard: each stopped frame keeps what it drew; the third in a
    // play gives the rest of it up for the clean recap, reported once; the
    // next play (time goes back) draws again.
    {
      LoadableFx::Guard late;late.expired=[](void *){return true;};
      const std::vector<uint16_t> clean(135*240,0);
      for(uint32_t ms:{100u,150u,200u}) {
        std::vector<uint16_t> f(135*240,0);
        assert(!loaded.overran() && loaded.draw({f.data(),135,240},96,ms,0x07ff,BestEffectUI::Renderer::Anchor(),0,late) && loaded.stats().stopped);
      }
      assert(loaded.overran() && !loaded.overran());
      for(uint32_t ms:{250u,700u}) {
        std::vector<uint16_t> f(135*240,0);
        assert(loaded.draw({f.data(),135,240},96,ms,0x07ff,BestEffectUI::Renderer::Anchor(),0) && f==clean && !loaded.overran());
      }
      std::vector<uint16_t> f(135*240,0),g(135*240,0);
      assert(loaded.draw({f.data(),135,240},96,700,0x07ff,BestEffectUI::Renderer::Anchor(),0) && f==clean);  // the same play: still given up
      assert(loaded.draw({f.data(),135,240},96,0,0x07ff,BestEffectUI::Renderer::Anchor(),0));                 // a new play
      assert(loaded.draw({f.data(),135,240},96,700,0x07ff,BestEffectUI::Renderer::Anchor(),0) && f!=clean);
      // Another effect is another play; two stopped frames are not enough.
      for(uint32_t ms:{100u,150u})assert(loaded.draw({g.data(),135,240},97,ms,0x07ff,BestEffectUI::Renderer::Anchor(),0,late));
      std::fill(g.begin(),g.end(),0);
      assert(loaded.draw({g.data(),135,240},97,700,0x07ff,BestEffectUI::Renderer::Anchor(),0) && g!=clean && !loaded.overran());
    }
  }
  // After a reboot: the same, read back from flash.
  {
    Loaded loaded(renderer,file.data(),file.size(),hot);
    assert(loaded.begin(flash)==EffectStore<NorFlash>::Open::Ready && loaded.info(0).installed && loaded.info(0).playable);
    assert(!std::strcmp(loaded.name(96),"Comet") && loaded.fallback(96)==unsigned(Appearance::Effect::Tidal) && loaded.info(1).duration==4500);
    // Deleted: no longer celebrates, and resolves to its fallback.
    assert(loaded.remove(0)==EffectStore<NorFlash>::Result::Ok && !Appearance::celebrates(96));
    assert(Appearance::resolve(Appearance::Effect(96))==Appearance::Effect::Tidal);  // its fallback, kept by the tombstone
    assert(Appearance::resolve(Appearance::Effect(97))==Appearance::Effect(97));
    // CELEBRATION names what plays: the fallback, not "Missing effect" (HIL loaded_store, 2026-10-23).
    assert(!std::strcmp(Appearance::playedLabel(Appearance::Effect(96)),"Tidal") && !std::strcmp(Appearance::label(Appearance::Effect(96)),"Missing effect"));
    // A slot whose file is damaged in flash is listed, not played, and stands in as its fallback.
    const auto &e=loaded.store().entry(1);
    flash.bytes[FlashLayout::EFFECTS_OFFSET+e.physical*EffectStore<NorFlash>::SLOT_BYTES+EffectStore<NorFlash>::HEADER+40]^=0x01;
    Loaded again(renderer,file.data(),file.size(),hot);
    assert(again.begin(flash)==EffectStore<NorFlash>::Open::Ready && again.info(1).installed && !again.info(1).playable && !Appearance::celebrates(97));
    assert(Appearance::resolve(Appearance::Effect(97))==Appearance::Effect::Sparkles && !std::strcmp(Appearance::playedLabel(Appearance::Effect(97)),"Sparkles"));
    // The tombstone survives a reboot.
    assert(Appearance::resolve(Appearance::Effect(96))==Appearance::Effect::Tidal && !again.info(0).installed);
  }
  // A loaded effect's own sound: rendered from its own copy of the file,
  // sample for sample what the engine renders from the file, identified by
  // the file's CRC so a replaced file renders again; an effect without a
  // sound, a deleted one, or a store without sound memory has none (each
  // then sounds as its stock fallback).
  {
    NorFlash fresh;
    std::vector<uint8_t> soundFile(EffectStore<NorFlash>::MAX_PAYLOAD);static Loaded::SoundSpace space;
    Loaded loaded(renderer,file.data(),file.size(),hot,soundFile.data(),&space);
    loaded.begin(fresh);bool failed=false;while(!loaded.store().prepareStep(failed))assert(!failed);
    auto chime=[&](uint64_t uid,float hz){char b[512];std::snprintf(b,sizeof(b),"effect \"Chime\"\nuid 0x%llx\nduration 2000\nsound 600\nframe { clip(H) }\n"
      "voice v = tone(decay: 6)\nnote(v, at: .05, level: .5, hz: %g)\n",(unsigned long long)uid,double(hz));return compileText(b);};
    const auto a=chime(0xC1,880),b=comet(0xC2,"Mute","Bloom"),c=chime(0xC1,660);
    unsigned slot=99;
    assert(loaded.install(a.data(),a.size(),nullptr,0,never,slot)==EffectStore<NorFlash>::Result::Ok && slot==0);
    assert(loaded.install(b.data(),b.size(),nullptr,0,never,slot)==EffectStore<NorFlash>::Result::Ok && slot==1);
    uint32_t key=0;size_t samples=0;
    assert(loaded.soundOf(96,key,samples) && key==LoadableFx::get32(a.data()+28) && samples==9600 && !loaded.soundOf(97,key,samples));
    std::vector<int16_t> got(9600),want(9600);
    assert(loaded.beginSound(96));for(size_t at=0;at<got.size();)at+=loaded.renderSound(got.data()+at,250);
    LoadableFx::Program p;assert(LoadableFx::load(a.data(),a.size(),p)==LoadableFx::Error::None);
    static LoadableFx::Sound direct;direct.begin(p);direct.run(want.data(),want.size());
    assert(got==want && loaded.renderSound(got.data(),1)==0);
    // Drawing between slices does not disturb the render (the drawn file is read into its own buffer).
    assert(loaded.beginSound(96) && loaded.renderSound(got.data(),300)==300);
    std::vector<uint16_t> frame(135*240,0);assert(loaded.draw({frame.data(),135,240},97,500,0x07ff,BestEffectUI::Renderer::Anchor(),0));
    for(size_t at=300;at<got.size();)at+=loaded.renderSound(got.data()+at,250);
    assert(got==want);
    // Replaced: the same slot, another CRC; a change to the store stops a render in progress.
    assert(loaded.beginSound(96) && loaded.renderSound(got.data(),10)==10);
    assert(loaded.install(c.data(),c.size(),nullptr,0,never,slot)==EffectStore<NorFlash>::Result::Ok && slot==0);
    assert(loaded.renderSound(got.data(),10)==0);
    uint32_t replaced=0;assert(loaded.soundOf(96,replaced,samples) && replaced!=key && replaced==LoadableFx::get32(c.data()+28));
    assert(loaded.remove(0)==EffectStore<NorFlash>::Result::Ok && !loaded.soundOf(96,key,samples) && !loaded.beginSound(96));
    // Without sound memory, no loaded effect has a sound of its own.
    Loaded mute(renderer,file.data(),file.size(),hot);mute.begin(fresh);
    assert(mute.install(a.data(),a.size(),nullptr,0,never,slot)==EffectStore<NorFlash>::Result::Ok && !mute.soundOf(96+slot,key,samples));
  }
  {
    // The device takes the internal hot block only when an added effect
    // first plays (0.19 took it at boot, leaving a HIL image under its 32 KB
    // DMA-capable floor with sound playing); until it gets one, effects draw
    // their fallbacks and it is asked again.
    static unsigned asked=0;static bool give=false;
    const Loaded::HotMemory memory=[]()->uint8_t *{++asked;return give?hot:nullptr;};
    NorFlash lazyFlash;Loaded lazy(renderer,file.data(),file.size(),memory);
    assert(lazy.begin(lazyFlash)==EffectStore<NorFlash>::Open::Unformatted);
    bool failed=false;while(!lazy.store().prepareStep(failed))assert(!failed);
    const auto a=comet(0xA1,"Comet","Tidal");unsigned slot=99;
    assert(lazy.install(a.data(),a.size(),nullptr,0,never,slot)==EffectStore<NorFlash>::Result::Ok && Appearance::celebrates(96));
    assert(asked==0 && !lazy.holdsHot());  // installed and listed, nothing taken
    std::vector<uint16_t> x(135*240,0);
    assert(!lazy.draw({x.data(),135,240},96,500,0x07ff,BestEffectUI::Renderer::Anchor(),0) && asked==1 && !lazy.holdsHot());
    give=true;
    assert(lazy.draw({x.data(),135,240},96,500,0x07ff,BestEffectUI::Renderer::Anchor(),0) && asked==2 && lazy.holdsHot());
    assert(lazy.draw({x.data(),135,240},96,700,0x07ff,BestEffectUI::Renderer::Anchor(),0) && asked==2);  // taken once
  }
  Appearance::loadedHooks()=Appearance::LoadedHooks();
  std::puts("test_loaded_effects: ok");
}
