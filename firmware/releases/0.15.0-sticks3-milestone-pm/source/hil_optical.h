#pragma once
// Hardware-in-the-loop optical source. Test builds only (LAUNCHLAB_HIL=1):
// production never includes this header.
//
// The real ADC keeps running and paces the detector at its true 50 kS/s DMA
// cadence; only the sample values in each consumed DMA frame are replaced.
// Stop/start, overflow and recovery therefore stay the production paths.
// Playback time follows the host clock: samples that fall in an ADC stop
// (flash checkpoint, export) are lost exactly as a real pull's would be.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"

namespace Hil {
// Shared start of a scripted pull (host clock, us). 0 = no pull scheduled.
inline std::atomic<int64_t> pullStartUs{0};
inline std::atomic<uint32_t> pullCount{0};

// Small, fast generator for noise: xorshift32, triangular distribution.
struct Noise {
  uint32_t state=0x9e3779b9u;
  uint32_t next(){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
  // Approximately unit standard deviation (sum of two uniforms, scaled).
  float unit(){return (float(next()&0xffff)+float(next()&0xffff))*(2.449f/65536.0f)-2.449f;}
  // Per-sample path: one draw from a precomputed table of the same distribution.
  float fast() {
    static float table[256];static bool ready=false;
    if(!ready){Noise seed;for(auto &v:table)v=seed.unit();ready=true;}
    return table[next()>>24];
  }
};

class Optical {
public:
  enum class Mode:uint8_t {Pass,Idle,Buffer,Generator};
  static constexpr uint32_t SAMPLE_HZ=50000;
  static constexpr uint32_t BUFFER_CAPACITY=524288; // 1 MiB PSRAM, 10.5 s.
  static constexpr unsigned KEYS=64;
  struct Key {int32_t ms=0;float rpm=0,depth=0,noise=0;};
  static const char *name(Mode m) {
    switch(m){case Mode::Idle:return "idle";case Mode::Buffer:return "buffer";case Mode::Generator:return "generator";default:return "pass";}
  }

  // ---- Host-side configuration (UI task). Values are copied per batch.
  void setMode(Mode m){portENTER_CRITICAL(&mux_);mode_=m;resync_=true;portEXIT_CRITICAL(&mux_);}
  Mode mode()const{return mode_;}
  void setIdle(float baseline,float noise){portENTER_CRITICAL(&mux_);baseline_=baseline;idleNoise_=noise;portEXIT_CRITICAL(&mux_);}
  void setDuty(float duty){portENTER_CRITICAL(&mux_);duty_=std::max(.02f,std::min(.9f,duty));portEXIT_CRITICAL(&mux_);}
  void clearKeys(){portENTER_CRITICAL(&mux_);keyCount_=0;portEXIT_CRITICAL(&mux_);}
  bool addKey(const Key &k) {
    bool ok=false;portENTER_CRITICAL(&mux_);
    if(keyCount_<KEYS && (!keyCount_ || k.ms>=keys_[keyCount_-1].ms)){keys_[keyCount_++]=k;ok=true;}
    portEXIT_CRITICAL(&mux_);return ok;
  }
  // Buffer upload: the caller first leaves Buffer mode and waits for any
  // in-flight batch (< 2 ms) before writing; the buffer is never freed.
  uint16_t *buffer() {
    if(!buffer_)buffer_=static_cast<uint16_t *>(heap_caps_malloc(BUFFER_CAPACITY*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    return buffer_;
  }
  void setBufferLength(uint32_t n){portENTER_CRITICAL(&mux_);bufferLength_=std::min(n,BUFFER_CAPACITY);portEXIT_CRITICAL(&mux_);}
  uint32_t bufferLength()const{return bufferLength_;}
  unsigned keyCount()const{return keyCount_;}
  // Acquisition-side discontinuity (ADC stop/restart, recovery): the next
  // batch re-derives its playback position from the host clock.
  void resync(){resync_=true;}
  uint64_t replaced()const{return replaced_;}
  int64_t cursor()const{return cursor_;}
  uint32_t resyncs()const{return resyncs_;}
  float baseline()const{return baseline_;}
  float idleNoise()const{return idleNoise_;}

  // ---- Acquisition task: fill n sample values for the batch that ended at
  // readUs. Returns false in Pass mode (keep the real ADC values). Runs on the
  // optical owner at 50 kS/s: single-precision and integer math only (the
  // S3 emulates doubles), generator parameters evaluated once per 5 ms batch.
  bool fill(uint16_t *out,unsigned n,int64_t readUs) {
    portENTER_CRITICAL(&mux_);
    const Mode mode=mode_;const float baseline=baseline_,idleNoise=idleNoise_,duty=duty_;
    const uint32_t length=bufferLength_;const unsigned keys=keyCount_;
    const bool resync=resync_;resync_=false;
    portEXIT_CRITICAL(&mux_);
    if(mode==Mode::Pass)return false;
    const int64_t start=pullStartUs.load();
    if(start!=activeStart_){activeStart_=start;phase_=0;cursorValid_=false;}
    if(resync){cursorValid_=false;++resyncs_;}
    if(!cursorValid_ && start) {
      // The batch's last sample was converted about when it was read.
      cursor_=(readUs-start)*int64_t(SAMPLE_HZ)/1000000-int64_t(n);cursorValid_=true;
    }
    const int64_t first=start && cursorValid_?cursor_:-1;
    Key k;bool marks=false;
    if(mode==Mode::Generator && keys && first+int64_t(n)>0)marks=keyAt(int32_t((first+n/2)/(SAMPLE_HZ/1000)),keys,k);
    // One mark per revolution; more reflected IR pulls AO down. A linear edge
    // of about 4 samples keeps transitions finite like a real mark.
    const uint32_t step=marks?uint32_t(k.rpm*(4294967296.0f/60.0f/SAMPLE_HZ)):0;
    const uint32_t markEnd=uint32_t(duty*4294967296.0f),edge=std::max<uint32_t>(1,step*4);
    for(unsigned i=0;i<n;++i) {
      const int64_t index=first>=0?first+int64_t(i):-1;
      float value;
      if(mode==Mode::Buffer && buffer_ && index>=0 && index<int64_t(length))value=float(buffer_[index]&0x0fff);
      else if(marks && index>=0) {
        phase_+=step;
        float level=phase_<markEnd?1.0f:0.0f;
        if(phase_<edge)level=float(phase_)/float(edge);
        else if(phase_>=markEnd && phase_-markEnd<edge)level=1.0f-float(phase_-markEnd)/float(edge);
        value=baseline-k.depth*level+k.noise*noise_.fast();
      } else value=baseline+idleNoise*noise_.fast();
      out[i]=uint16_t(int32_t(std::max(0.0f,std::min(4095.0f,value))+.5f));
    }
    if(start && cursorValid_)cursor_+=n;
    replaced_+=n;return true;
  }
private:
  // Linear interpolation of the keyframes; false outside them (idle).
  bool keyAt(int32_t ms,unsigned keys,Key &out)const {
    if(ms<keys_[0].ms || ms>keys_[keys-1].ms)return false;
    unsigned hi=1;while(hi<keys && keys_[hi].ms<ms)++hi;
    if(hi>=keys){out=keys_[keys-1];return true;}
    const Key &a=keys_[hi-1],&b=keys_[hi];
    const float f=b.ms>a.ms?float(ms-a.ms)/float(b.ms-a.ms):1.0f;
    out.ms=ms;out.rpm=a.rpm+(b.rpm-a.rpm)*f;out.depth=a.depth+(b.depth-a.depth)*f;out.noise=a.noise+(b.noise-a.noise)*f;
    return true;
  }
  portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;
  Mode mode_=Mode::Pass;
  float baseline_=2000,idleNoise_=2,duty_=.3f;
  Key keys_[KEYS];unsigned keyCount_=0;
  uint16_t *buffer_=nullptr;uint32_t bufferLength_=0;
  bool resync_=true,cursorValid_=false;
  int64_t activeStart_=0,cursor_=0;uint32_t phase_=0;
  uint64_t replaced_=0;uint32_t resyncs_=0;
  Noise noise_;
};
inline Optical &optical(){static Optical instance;return instance;}
}
