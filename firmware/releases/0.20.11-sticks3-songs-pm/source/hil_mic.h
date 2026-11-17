#pragma once
// Hardware-in-the-loop sound for the mic. Test builds only (LAUNCHLAB_HIL=1):
// production never includes this header.
//
// The rig has no Bey, so `!mictone` replaces each captured chunk with a spin:
// launch noise from the pull's end, then a falling tone with its harmonics,
// then near silence. Capture, the ring, the tracker and the page all run as in
// production at the real rate. This is what proves a reading's cost and the
// page's timing on a device; it says nothing about what the mic really hears.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "esp_timer.h"

namespace Hil {
class MicTone {
public:
  void set(float hz,float fallHzPerS,float seconds,float noiseS) {
    enabled_=false;hz_=hz;fall_=fallHzPerS;seconds_=seconds;noise_=noiseS;started_=false;
    for(unsigned i=0;i<TABLE;++i)sine_[i]=int16_t(std::lround(32767*std::sin(2*M_PI*double(i)/TABLE)));
    enabled_=true;
  }
  void off(){enabled_=false;}
  void begin(){if(enabled_){index_=0;for(auto &p:phase_)p=0;started_=true;}}
  // In the capture task, chunk by chunk: a table walk, so the test adds little.
  void fill(int16_t *data,size_t count) {
    if(!enabled_)return;
    for(size_t i=0;i<count;++i)data[i]=next();
  }
private:
  static constexpr unsigned TABLE=1024,HARMONICS=6,RATE=16000;
  int16_t next() {
    state_=1664525u*state_+1013904223u;
    const int noise=int(state_>>20)-2048;  // +/-2048
    if(!started_)return int16_t(noise>>6);
    const float t=float(index_++)/RATE;
    if(t<noise_)return int16_t(noise*4);
    if(t>=noise_+seconds_)return int16_t(noise>>6);
    const float hz=hz_-fall_*(t-noise_);
    int sum=0;
    for(unsigned k=0;k<HARMONICS;++k) {
      phase_[k]+=uint32_t(hz*float(k+1)*(4294967296.f/RATE));
      sum+=sine_[phase_[k]>>22]/int(4*(k+1));
    }
    return int16_t(sum+(noise>>6));
  }
  std::atomic<bool> enabled_{false};bool started_=false;
  float hz_=0,fall_=0,seconds_=0,noise_=0;uint32_t index_=0,state_=12345,phase_[HARMONICS]={};
  int16_t sine_[TABLE]={};
};
inline MicTone micTone;
// !micstall 1: the mic task waits before it takes the mic down, as one stuck in
// a spin or in M5Unified's mic end() would, until !micstall 0.
inline std::atomic<bool> micStall{false};
}
