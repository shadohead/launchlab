#pragma once
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include "best_effect_sound.h"
#include "motion_acquisition.h"
#include "pm1_idle.h"

// Plays personal-best sounds on the StickS3 speaker (ES8311 codec and amp).
// They are powered only while a sound plays: M5.Speaker.begin() before and
// end() once it is silent. Both switch power over the internal I2C bus the IMU
// owner polls, so they run under its gate, and end() never runs during a
// measured pull. Playback is the speaker's DMA task, pinned to the UI core
// and fed from a PSRAM buffer rendered once per effect, so UI frame timing
// cannot disturb it and acquisition on core 0 is untouched. Each effect has
// its own buffer, kept once rendered: a milestone's effect is only known
// when its pull ends, so every effect it may play is rendered ahead.
class BestEffectSpeaker {
public:
  explicit BestEffectSpeaker(StickS3MotionAcquisition &imu):imu_(imu){}
  // Call once after M5.begin() with the internal speaker configured.
  void configure() {
    auto c=M5.Speaker.config();c.task_pinned_core=1;M5.Speaker.config(c);
    configured_=M5.Speaker.isEnabled();
  }
  // Renders up to `slice` more samples of the effect's sound into its own
  // buffer, restarting if another effect was in progress. A whole sound
  // takes far longer than a UI frame at 80 MHz, so the sketch renders a
  // slice per idle loop pass. Another effect's buffer may be playing.
  bool prepare(Appearance::Effect e,size_t slice=SIZE_MAX) {
    if(!configured_ || !Appearance::drawn(unsigned(e)))return false;
    if(prepared(e))return true;
    const unsigned i=Appearance::drawnIndex(e);
    if(!buffers_[i]) {
      buffers_[i]=static_cast<int16_t *>(heap_caps_malloc(BestEffectSound::samples(e)*sizeof(int16_t),MALLOC_CAP_SPIRAM));
      if(!buffers_[i])return false;
    }
    if(target_!=e){target_=e;synth_=BestEffectSound::Synth(e,BestEffectSound::samples(e));renderUs_=0;}
    const int64_t started=esp_timer_get_time();
    synth_.run(buffers_[i]+synth_.index(),std::min(slice,BestEffectSound::samples(e)-synth_.index()));
    const uint32_t spent=uint32_t(esp_timer_get_time()-started);renderUs_+=spent;maxSliceUs_=std::max(maxSliceUs_,spent);
    if(synth_.done()){rendered_|=1u<<i;target_=Appearance::Effect::Off;}
    return prepared(e);
  }
  bool prepared(Appearance::Effect e)const{return Appearance::drawn(unsigned(e)) && ((rendered_>>Appearance::drawnIndex(e))&1u);}
  bool play(Appearance::Effect e,Appearance::Sound level) {
    // Rendering is the sketch's job, a slice per idle pass; play only what is ready.
    if(level==Appearance::Sound::Off || !prepared(e))return false;
    if(!begun_) {
      if(!imu_.lock()){++lockMisses_;return false;}
      const int64_t started=esp_timer_get_time();
      if(!pm1Idle().ensureAwake()){imu_.unlock();return false;}
      const bool ok=M5.Speaker.begin();
      beginUs_=std::max(beginUs_,uint32_t(esp_timer_get_time()-started));imu_.unlock();
      if(!ok)return false;
      begun_=true;
    }
    M5.Speaker.setVolume(level==Appearance::Sound::High?HIGH_VOLUME:LOW_VOLUME);
    if(!M5.Speaker.playRaw(buffers_[Appearance::drawnIndex(e)],BestEffectSound::samples(e),BestEffectSound::RATE,false,1,0,true))return false;
    ++plays_;return true;
  }
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
    char rendered[64]="none";size_t used=0;
    for(unsigned i=0;i<Appearance::DRAWN;++i)if((rendered_>>i)&1u)
      used+=snprintf(rendered+used,sizeof(rendered)-used,"%s%s",used?",":"",Appearance::label(Appearance::drawnEffect(i)));
    Serial.printf("SOUND_STATUS level=%s configured=%d buffers=%u rendered=%s powered=%d playing=%d plays=%lu stops=%lu lock_misses=%lu render_us=%lu max_slice_us=%lu max_begin_us=%lu max_end_us=%lu\n",
      Appearance::label(level),configured_,buffers,rendered,begun_,playing(),
      (unsigned long)plays_,(unsigned long)stops_,(unsigned long)lockMisses_,(unsigned long)renderUs_,(unsigned long)maxSliceUs_,(unsigned long)beginUs_,(unsigned long)endUs_);
  }
private:
  static constexpr uint8_t LOW_VOLUME=72,HIGH_VOLUME=160;
  StickS3MotionAcquisition &imu_;
  int16_t *buffers_[Appearance::DRAWN]={};uint32_t rendered_=0;
  BestEffectSound::Synth synth_{Appearance::Effect::Off,0};
  Appearance::Effect target_=Appearance::Effect::Off;
  bool configured_=false,begun_=false;
  uint32_t plays_=0,stops_=0,lockMisses_=0,renderUs_=0,maxSliceUs_=0,beginUs_=0,endUs_=0;
};
