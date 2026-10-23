#pragma once
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include "best_effect_sound.h"
#include "motion_acquisition.h"
#include "pm1_idle.h"
#include "sound_sync.h"

// Plays personal-best sounds on the StickS3 speaker (ES8311 codec and amp).
// They are powered only while a sound plays: M5.Speaker.begin() before and
// end() once it is silent. Both switch power over the internal I2C bus the IMU
// owner polls, so they run under its gate, and end() never runs during a
// measured pull. Playback is the speaker's DMA task, pinned to the UI core
// and fed from a PSRAM buffer rendered once per effect, so UI frame timing
// cannot disturb it and acquisition on core 0 is untouched. Each sound has
// its own buffer, kept once rendered: a milestone's effect is only known
// when its pull ends, so every effect it may play is rendered ahead. At most
// MAX_BUFFERS are kept (about 90 KB each for a 2.8 s sound): preparing
// another frees the least recently used one, never the one playing. A
// loaded effect with a sound of its own (LoadableFx::Sound) has its slot's
// buffer, kept while its file stays the same; any other plays its stock
// fallback's.
class BestEffectSpeaker {
public:
  // A loaded effect's own sound (loaded_effects.h, through the sketch):
  // whether the effect has one, its identity (its file's CRC, so a replaced
  // file renders again) and length, and a resumable render that gives the
  // same samples for any slicing, as BestEffectSound::Synth does.
  struct LoadedSounds {
    virtual bool sound(unsigned effect,uint32_t &key,size_t &samples)=0;  // false: it sounds as its stock fallback
    virtual bool begin(unsigned effect)=0;
    virtual size_t run(int16_t *out,size_t count)=0;
  };
  explicit BestEffectSpeaker(StickS3MotionAcquisition &imu):imu_(imu){}
  // Call once after M5.begin() with the internal speaker configured.
  // Mono output: M5Unified sets the StickS3 up as stereo, so its task mixed
  // two identical channels from our mono sounds, refilling the DMA on the UI
  // core during effect frames (0.6-1 ms of a worst frame with sound on). Mono
  // halves that mixing at the same loudness (mono folds left and right
  // without the stereo gain shift) and the same I2S frames (the S3's mono
  // slot mode sends each sample on both slots, so the ES8311 sees no change).
  void configure() {
    auto c=M5.Speaker.config();c.task_pinned_core=1;c.stereo=false;M5.Speaker.config(c);
    configured_=M5.Speaker.isEnabled();
  }
  // Loaded effects' own sounds, once the store is open; without it every
  // loaded effect sounds as its stock fallback.
  void attach(LoadedSounds *loaded){loaded_=loaded;}
  // Renders up to `slice` more samples of the effect's sound into its own
  // buffer, restarting if another sound was in progress. A whole sound
  // takes far longer than a UI frame at 80 MHz, so the sketch renders a
  // slice per idle loop pass. Another sound's buffer may be playing.
  // A loaded effect plays its own sound when its file has one, else its
  // stock fallback's.
  bool prepare(Appearance::Effect e,size_t slice=SIZE_MAX) {
    const Source s=source(e);
    if(!configured_ || s.index==SOUNDS)return false;
    if(ready(s))return true;
    const unsigned i=s.index;
    // A loaded effect replaced while its old sound plays renders once that ends.
    if(i==playingIndex_ && begun_ && M5.Speaker.isPlaying())return false;
    used_[i]=++clock_;
    if(buffers_[i] && size(i)!=s.samples){heap_caps_free(buffers_[i]);buffers_[i]=nullptr;}
    if(!buffers_[i]) {
      makeRoom(i);
      buffers_[i]=static_cast<int16_t *>(heap_caps_malloc(s.samples*sizeof(int16_t),MALLOC_CAP_SPIRAM));
      if(!buffers_[i])return false;
      if(i>=Appearance::DRAWN)loadedSize_[i-Appearance::DRAWN]=s.samples;
    }
    rendered_[i]=false;
    if(target_!=i || targetKey_!=s.key) {
      target_=i;targetKey_=s.key;progress_=0;renderUs_=0;
      if(s.loaded){if(!loaded_->begin(unsigned(e))){target_=SOUNDS;return false;}}
      else synth_.restart(s.effect,s.samples);
    }
    const int64_t started=esp_timer_get_time();
    const size_t want=std::min(slice,s.samples-progress_);
    const size_t got=s.loaded?loaded_->run(buffers_[i]+progress_,want):synth_.run(buffers_[i]+progress_,want);
    progress_+=got;
    const uint32_t spent=uint32_t(esp_timer_get_time()-started);renderUs_+=spent;maxSliceUs_=std::max(maxSliceUs_,spent);
    if(progress_>=s.samples){rendered_[i]=true;if(i>=Appearance::DRAWN)loadedKey_[i-Appearance::DRAWN]=s.key;target_=SOUNDS;}
    else if(got<want)target_=SOUNDS;  // the render stopped short: start over next time
    return ready(s);
  }
  bool prepared(Appearance::Effect e)const{return ready(source(e));}
  // Powers the codec if it is off (under the IMU owner's gate).
  bool power() {
    if(begun_)return true;
    if(!imu_.lock()){++lockMisses_;return false;}
    const int64_t started=esp_timer_get_time();
    if(!pm1Idle().ensureAwake()){imu_.unlock();return false;}
    const bool ok=M5.Speaker.begin();
    beginUs_=std::max(beginUs_,uint32_t(esp_timer_get_time()-started));imu_.unlock();
    begun_=ok;return ok;
  }
  // Starts the effect's sound `skip` samples in; false if it is not ready,
  // the codec cannot start or nothing is left.
  bool play(Appearance::Effect e,Appearance::Sound level,size_t skip=0) {
    const Source s=source(e);
    // Rendering is the sketch's job, a slice per idle pass; play only what is ready.
    if(level==Appearance::Sound::Off || !ready(s) || skip>=s.samples || !power())return false;
    M5.Speaker.setVolume(level==Appearance::Sound::High?HIGH_VOLUME:LOW_VOLUME);
    const unsigned i=s.index;
    outputUs_=SoundSync::outputUs(M5.Speaker.isPlaying());
    if(!M5.Speaker.playRaw(buffers_[i]+skip,s.samples-skip,BestEffectSound::RATE,false,1,0,true))return false;
    used_[i]=++clock_;playingIndex_=i;++plays_;return true;
  }
  // The latency of the last play's first sample (SoundSync::outputUs), and
  // what the next would have now.
  uint32_t outputUs()const{return outputUs_;}
  uint32_t nextOutputUs(){return SoundSync::outputUs(begun_ && M5.Speaker.isPlaying());}
  // Silence now: no bus traffic, safe during a measured pull.
  void stop(){if(begun_ && M5.Speaker.isPlaying()){M5.Speaker.stop();++stops_;}}
  // Powers the codec down once silent, but never while a pull is measured.
  void tick(bool measuring) {
    if(!begun_ || measuring || M5.Speaker.isPlaying() || !imu_.lock())return;
    const int64_t started=esp_timer_get_time();
    if(!pm1Idle().ensureAwake()){imu_.unlock();return;}
    M5.Speaker.end();endUs_=std::max(endUs_,uint32_t(esp_timer_get_time()-started));
    imu_.unlock();begun_=false;
  }
  bool powered()const{return begun_;}
  bool playing()const{return begun_ && M5.Speaker.isPlaying();}
  void status(Appearance::Sound level) {
    unsigned buffers=0;for(const auto *b:buffers_)buffers+=b!=nullptr;
    static char rendered[512],loaded[160];std::strcpy(rendered,"none");std::strcpy(loaded,"none");size_t used=0,usedLoaded=0;
    for(unsigned i=0;i<Appearance::DRAWN;++i)if(rendered_[i])
      used+=snprintf(rendered+used,sizeof(rendered)-used,"%s%s",used?",":"",Appearance::label(Appearance::drawnEffect(i)));
    for(unsigned i=Appearance::DRAWN;i<SOUNDS;++i)if(rendered_[i])
      usedLoaded+=snprintf(loaded+usedLoaded,sizeof(loaded)-usedLoaded,"%s%u",usedLoaded?",":"",Appearance::FIRST_LOADED+i-Appearance::DRAWN);
    Serial.printf("SOUND_STATUS level=%s configured=%d buffers=%u evictions=%lu rendered=%s powered=%d playing=%d plays=%lu stops=%lu lock_misses=%lu render_us=%lu max_slice_us=%lu max_begin_us=%lu max_end_us=%lu loaded_rendered=%s\n",
      Appearance::label(level),configured_,buffers,(unsigned long)evictions_,rendered,begun_,playing(),
      (unsigned long)plays_,(unsigned long)stops_,(unsigned long)lockMisses_,(unsigned long)renderUs_,(unsigned long)maxSliceUs_,(unsigned long)beginUs_,(unsigned long)endUs_,loaded);
  }
  static constexpr unsigned MAX_BUFFERS=12;
  // A buffer per built-in effect, then one per loaded slot.
  static constexpr unsigned SOUNDS=Appearance::DRAWN+Appearance::LOADED;
private:
  static constexpr uint8_t LOW_VOLUME=72,HIGH_VOLUME=160;
  // What an effect sounds like: a loaded effect's own sound (its slot's
  // buffer, keyed by its file), else the built-in it stands for.
  struct Source {unsigned index=SOUNDS;Appearance::Effect effect=Appearance::Effect::Off;bool loaded=false;uint32_t key=0;size_t samples=0;};
  Source source(Appearance::Effect e)const {
    Source s;
    if(Appearance::loaded(unsigned(e)) && loaded_ && loaded_->sound(unsigned(e),s.key,s.samples) && s.samples) {
      s.index=Appearance::DRAWN+(unsigned(e)-Appearance::FIRST_LOADED);s.effect=e;s.loaded=true;return s;
    }
    e=Appearance::stockFallback(e);
    if(Appearance::drawn(unsigned(e))){s.index=Appearance::drawnIndex(e);s.effect=e;s.samples=BestEffectSound::samples(e);}
    return s;
  }
  bool ready(const Source &s)const{return s.index<SOUNDS && rendered_[s.index] && size(s.index)==s.samples && (!s.loaded || loadedKey_[s.index-Appearance::DRAWN]==s.key);}
  // A buffer's length: a built-in's is fixed, a loaded slot's is its file's.
  size_t size(unsigned i)const{return i<Appearance::DRAWN?BestEffectSound::samples(Appearance::drawnEffect(i)):loadedSize_[i-Appearance::DRAWN];}
  // Frees least recently used buffers until one more fits, keeping `keep`
  // and the buffer the speaker is playing.
  void makeRoom(unsigned keep) {
    for(;;) {
      unsigned count=0,oldest=SOUNDS;
      for(unsigned j=0;j<SOUNDS;++j) {
        if(!buffers_[j])continue;
        ++count;
        if(j==keep || (j==playingIndex_ && M5.Speaker.isPlaying()))continue;
        if(oldest==SOUNDS || used_[j]<used_[oldest])oldest=j;
      }
      if(count<MAX_BUFFERS || oldest==SOUNDS)return;
      heap_caps_free(buffers_[oldest]);buffers_[oldest]=nullptr;rendered_[oldest]=false;++evictions_;
      if(target_==oldest)target_=SOUNDS;
    }
  }
  StickS3MotionAcquisition &imu_;
  LoadedSounds *loaded_=nullptr;
  // More sounds than a 64-bit mask holds since 0.18: one flag each.
  int16_t *buffers_[SOUNDS]={};bool rendered_[SOUNDS]={};uint32_t used_[SOUNDS]={},clock_=0,evictions_=0;unsigned playingIndex_=SOUNDS;
  // A loaded slot's rendered sound: its file's key and its length.
  uint32_t loadedKey_[Appearance::LOADED]={};size_t loadedSize_[Appearance::LOADED]={};
  BestEffectSound::Synth synth_{Appearance::Effect::Off,0};
  unsigned target_=SOUNDS;uint32_t targetKey_=0;size_t progress_=0;  // the sound being rendered and how far
  bool configured_=false,begun_=false;
  uint32_t plays_=0,stops_=0,lockMisses_=0,renderUs_=0,maxSliceUs_=0,beginUs_=0,endUs_=0,outputUs_=0;
};

