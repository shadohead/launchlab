#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "appearance.h"
#include "effect_store.h"
#include "fx_effect.h"

// The firmware's loaded effects (docs/FIRMWARE_LOADABLE_EFFECTS.md): the
// store in the spiffs partition, what each installed slot is (its name,
// length and fallback, read once at boot and after each change), the
// Appearance hooks that let effect sets, pools and Settings see them, and
// the engine that draws one. Stored IDs 96-127 are slots 0-31.
//
// Memory: the playing file is read into `file` (PSRAM on the device, up to
// a slot's payload) and only its hot parts are copied into `hot` (4 KB of
// internal RAM): LoadableFx::place. The device keeps this object in PSRAM
// and takes `hot` from the internal heap only when an added effect first
// plays (HotMemory): internal RAM is the DMA-capable memory the speaker and
// the ADC need, and a unit without added effects should not give up any.
// A sound is rendered from its own copy of its file (`soundFile`, PSRAM)
// with its own program and renderer (SoundSpace, PSRAM), so preparing one
// never disturbs the effect drawing.
template<class Flash> class LoadedEffects {
public:
  using Store=EffectStore<Flash>;
  static constexpr unsigned SLOTS=Store::LOGICAL;
  static_assert(SLOTS==Appearance::LOADED,"one store slot per loaded effect ID");
  struct Info {
    bool installed=false,playable=false;
    char name[LoadableFx::MAX_NAME+1]={};
    uint32_t duration=0;uint8_t fallback=LoadableFx::Program::NO_FALLBACK;
    uint64_t uid=0;uint16_t revision=0;uint8_t engine=0;
    // Its own sound (a SOUND section): the file's CRC identifies it.
    bool sound=false;uint32_t crc=0,soundSamples=0;
  };
  // What renders a loaded effect's sound: its program and the renderer.
  struct SoundSpace {LoadableFx::Program program;LoadableFx::Sound sound;};

  // `soundFile` (fileCapacity bytes, 4-byte aligned) and `sound` may be
  // null: loaded effects then sound as their stock fallbacks.
  LoadedEffects(BestEffectUI::Renderer &renderer,uint8_t *file,size_t fileCapacity,uint8_t *hot,uint8_t *soundFile=nullptr,SoundSpace *sound=nullptr)
    :engine_(renderer),file_(file),fileCapacity_(fileCapacity),hot_(hot),soundFile_(soundFile),sound_(sound){}
  // The hot block (HOT_BYTES, 4-byte aligned) on first play instead; while
  // it returns null, effects draw their fallbacks and it is asked again.
  using HotMemory=uint8_t *(*)();
  LoadedEffects(BestEffectUI::Renderer &renderer,uint8_t *file,size_t fileCapacity,HotMemory hot,uint8_t *soundFile=nullptr,SoundSpace *sound=nullptr)
    :LoadedEffects(renderer,file,fileCapacity,static_cast<uint8_t *>(nullptr),soundFile,sound){hotMemory_=hot;}
  // Whether the hot block is held (taken on first play).
  bool holdsHot()const{return hot_!=nullptr;}

  // Opens the store and reads every slot; installs the Appearance hooks.
  typename Store::Open begin(Flash &flash) {
    instance()=this;
    auto &h=Appearance::loadedHooks();
    h.playable=[](unsigned e){return instance() && instance()->playable(e);};
    h.name=[](unsigned e){return instance()?instance()->name(e):nullptr;};
    h.duration=[](unsigned e){return instance()?instance()->duration(e):uint32_t(0);};
    h.fallback=[](unsigned e){return instance()?instance()->fallback(e):~0u;};
    const auto r=store_.open(flash);
    refresh();
    return r;
  }
  Store &store(){return store_;}
  const Store &store()const{return store_;}
  const Info &info(unsigned slot)const{static const Info none;return slot<SLOTS?info_[slot]:none;}
  static unsigned slotOf(unsigned effect){return effect-Appearance::FIRST_LOADED;}
  bool playable(unsigned effect)const{return Appearance::loaded(effect) && info_[slotOf(effect)].playable;}
  const char *name(unsigned effect)const{return playable(effect)?info_[slotOf(effect)].name:nullptr;}
  uint32_t duration(unsigned effect)const{return playable(effect)?info_[slotOf(effect)].duration:0;}
  // The slot's fallback: from its header, so a deleted or unreadable effect keeps its stand-in.
  unsigned fallback(unsigned effect)const {
    if(!Appearance::loaded(effect))return ~0u;
    const uint8_t f=store_.fallback(slotOf(effect));
    return f!=LoadableFx::Program::NO_FALLBACK?f:~0u;
  }

  // Re-reads every slot's file: after begin(), an install or a delete.
  void refresh(){for(unsigned s=0;s<SLOTS;++s)read(s);current_=-1;soundSlot_=-1;}

  // Installs a file and its packed source (EffectStore::put), then reads the slot.
  template<class Referenced> typename Store::Result install(const uint8_t *file,size_t bytes,const uint8_t *source,size_t sourceBytes,Referenced referenced,unsigned &slot) {
    current_=-1;soundSlot_=-1;
    const auto r=store_.put(file,bytes,source,sourceBytes,referenced,slot);
    if(r==Store::Result::Ok)read(slot);
    return r;
  }
  typename Store::Result remove(unsigned slot){current_=-1;soundSlot_=-1;const auto r=store_.remove(slot);read(slot);return r;}
  // A restore's effect into its own slot (EffectStore::putAt).
  typename Store::Result restore(unsigned slot,const uint8_t *file,size_t bytes,const uint8_t *source,size_t sourceBytes) {
    current_=-1;const auto r=store_.putAt(slot,file,bytes,source,sourceBytes);refresh();return r;
  }
  typename Store::Result clear(){current_=-1;const auto r=store_.clear();refresh();return r;}

  // Draws a loaded effect's frame (its file read and placed on first use);
  // false when the effect cannot play here (the caller then draws its fallback).
  // A frame the guard stops keeps what it drew; after OVERRUNS stopped frames
  // in one play (a play ends when the effect changes or time goes back) the
  // rest of that play is the clean recap, and overran() says so once.
  static constexpr unsigned OVERRUNS=3;
  bool draw(const BestEffectUI::Surface &surface,unsigned effect,uint32_t elapsed,uint16_t accent,
            const BestEffectUI::Renderer::Anchor &anchor,uint16_t background,const LoadableFx::Guard &guard=LoadableFx::Guard()) {
    if(!prepare(effect))return false;
    if(play_.effect!=int(effect) || elapsed<play_.last)play_=Play{int(effect)};
    play_.last=elapsed;
    if(play_.stopped>=OVERRUNS)return true;
    engine_.draw(surface,program_,elapsed,accent,anchor,background,guard);
    if(engine_.stats().stopped && ++play_.stopped==OVERRUNS)overran_=true;
    return true;
  }
  // A playable effect's own sound: its identity (the file's CRC) and length
  // in samples; false when it has none (it sounds as its stock fallback).
  bool soundOf(unsigned effect,uint32_t &key,size_t &samples)const {
    if(!playable(effect) || !soundFile_ || !sound_)return false;
    const Info &i=info_[slotOf(effect)];
    if(!i.sound)return false;
    key=i.crc;samples=i.soundSamples;return true;
  }
  // Starts rendering an effect's own sound from its own copy of the file;
  // renderSound() continues it, a slice at a time, the same for any slicing.
  bool beginSound(unsigned effect) {
    soundSlot_=-1;
    uint32_t key;size_t samples;
    if(!soundOf(effect,key,samples))return false;
    size_t n=0;LoadableFx::Program &p=sound_->program;
    if(!store_.readFile(slotOf(effect),soundFile_,fileCapacity_,n) || LoadableFx::load(soundFile_,n,p)!=LoadableFx::Error::None ||
       !p.hasSound || LoadableFx::get32(soundFile_+28)!=key || p.soundSamples()!=samples)return false;
    sound_->sound.begin(p);soundSlot_=int(slotOf(effect));return true;
  }
  size_t renderSound(int16_t *out,size_t count){return soundSlot_>=0?sound_->sound.run(out,count):0;}
  // True once after a play is abandoned for running over the guard.
  bool overran(){const bool o=overran_;overran_=false;return o;}
  const LoadableFx::Engine::Stats &stats()const{return engine_.stats();}

private:
  static LoadedEffects *&instance(){static LoadedEffects *p=nullptr;return p;}
  // Reads slot s's file to learn what it is; a slot whose file no longer
  // loads (damaged, or for a newer engine) is installed but not playable.
  void read(unsigned s) {
    Info &i=info_[s];i=Info();
    const auto &e=store_.entry(s);
    if(!e.used)return;
    i.installed=true;i.uid=e.uid;
    size_t n=0;
    if(!file_ || !store_.readFile(s,file_,fileCapacity_,n))return;
    i.engine=n>5?file_[5]:0;
    LoadableFx::Program p;
    if(LoadableFx::load(file_,n,p)!=LoadableFx::Error::None)return;
    std::memcpy(i.name,p.name,sizeof(i.name));i.duration=p.duration;i.fallback=p.fallback;i.revision=p.revision;
    i.sound=p.hasSound;i.soundSamples=p.soundSamples();i.crc=LoadableFx::get32(file_+28);
    i.playable=LoadableFx::hotBytes(p)<=LoadableFx::HOT_BYTES;
  }
  // The effect's program, read and placed unless it is the one already placed.
  bool prepare(unsigned effect) {
    if(!playable(effect))return false;
    if(!hot_ && hotMemory_)hot_=hotMemory_();
    if(!hot_)return false;
    const int s=int(slotOf(effect));
    if(current_==s)return true;
    current_=-1;size_t n=0;
    if(!store_.readFile(unsigned(s),file_,fileCapacity_,n) || LoadableFx::load(file_,n,program_)!=LoadableFx::Error::None ||
       !LoadableFx::place(program_,hot_,LoadableFx::HOT_BYTES))return false;
    current_=s;return true;
  }
  Store store_;
  LoadableFx::Engine engine_;LoadableFx::Program program_;
  uint8_t *file_;size_t fileCapacity_;uint8_t *hot_;HotMemory hotMemory_=nullptr;
  uint8_t *soundFile_;SoundSpace *sound_;int soundSlot_=-1;
  int current_=-1;
  struct Play {int effect=-1;uint32_t last=0;unsigned stopped=0;};
  Play play_;bool overran_=false;
  Info info_[SLOTS];
};
