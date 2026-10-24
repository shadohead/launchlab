#pragma once
#include "launch_motion.h"
#include "src/vqf/vqf.hpp"

// Continuous six-axis orientation, with timestamp-based 100 Hz resampling.
// Only orientation and bias-corrected rates are emitted. A single IMU cannot
// observe translation or absolute yaw. No velocity/position state is invented.
class LauncherOrientation {
public:
  // A gap up to BRIDGE_US (one stalled or discarded FIFO read, a short task
  // stall) is integrated across with linearly interpolated rates, keeping the
  // pose and warm-up, and its samples are marked bridged (approximate). Longer
  // gaps cannot be bridged honestly and restart inclination as before.
  static constexpr uint64_t STEP_US=10000,MAX_GAP_US=35000,BRIDGE_US=100000;
  LauncherOrientation():filter_(.01){}
  uint32_t updates()const{return updates_;}
  uint32_t resets()const{return resets_;}
  bool rest()const{return filter_.getRestDetected();}
  // A mounting change changes the coordinate frame of both sensors. Previous
  // gyro bias, interpolation and pose cannot cross that boundary. Unlike a
  // timestamp gap, relearn bias in the new frame and require fresh warmup.
  void resetFrame() {
    filter_.resetState();previous_={};next_=0;
    for(auto &v:integral_)v=0;
    segmentUpdates_=0;++resets_;
  }
  float biasDegrees(float *out)const {
    vqf_real_t bias[3];const float sigma=filter_.getBiasEstimate(bias)*LaunchMotion::DEG;
    for(unsigned j=0;j<3;++j)out[j]=bias[j]*LaunchMotion::DEG;
    return sigma;
  }
  template<class Emit> void feed(const LaunchMotion::Sample &raw,Emit emit) {
    if(previous_.us && raw.us<=previous_.us)return;
    bool usable=!raw.clipped;
    for(unsigned j=0;j<3;++j)usable&=std::isfinite(raw.a[j]) && std::isfinite(raw.g[j]);
    if(!usable) {
      auto bad=raw;bad.clipped=true;emit(bad);restart();previous_={};return;
    }
    if(previous_.us && raw.us-previous_.us>BRIDGE_US){restart();previous_={};}
    if(!previous_.us) {
      previous_=raw;next_=raw.us+STEP_US;
      vqf_real_t acceleration[3];for(unsigned j=0;j<3;++j)acceleration[j]=raw.a[j]*9.80665;
      filter_.updateAcc(acceleration);
      emit(result(raw));return;
    }
    // A grid sample touching a full-scale reading on either side is approximate.
    const bool saturated=previous_.saturated || raw.saturated;
    const bool bridged=raw.us-previous_.us>MAX_GAP_US;
    // Integrate each piece of the measured gyro interval onto the fixed grid.
    // This preserves actual elapsed time even when USB/I2C polling jitters.
    uint64_t cursor=previous_.us;
    while(cursor<raw.us) {
      const uint64_t end=std::min(raw.us,next_);
      const float f0=float(cursor-previous_.us)/float(raw.us-previous_.us);
      const float f1=float(end-previous_.us)/float(raw.us-previous_.us);
      for(unsigned j=0;j<3;++j)integral_[j]+=(previous_.g[j]+(raw.g[j]-previous_.g[j])*(f0+f1)*.5f)*(end-cursor);
      if(end==next_) {
        LaunchMotion::Sample sample;sample.us=next_;sample.saturated=saturated;sample.bridged=bridged;vqf_real_t rate[3],acceleration[3];
        for(unsigned j=0;j<3;++j) {
          sample.a[j]=previous_.a[j]+(raw.a[j]-previous_.a[j])*f1;
          sample.g[j]=integral_[j]/STEP_US;integral_[j]=0;
          rate[j]=sample.g[j]/LaunchMotion::DEG;acceleration[j]=sample.a[j]*9.80665;
        }
        filter_.update(rate,acceleration);++updates_;++segmentUpdates_;
        emit(result(sample));next_+=STEP_US;
      }
      cursor=end;
    }
    previous_=raw;
  }
private:
  VQF filter_;
  LaunchMotion::Sample previous_;
  uint64_t next_=0;double integral_[3]={};
  uint32_t updates_=0,segmentUpdates_=0,resets_=0;
  void restart() {
    // Retain learned bias through a data gap, but never bridge unobserved
    // rotation or draw a capture crossing that gap. Inclination reinitializes.
    vqf_real_t bias[3];const auto sigma=filter_.getBiasEstimate(bias);
    filter_.resetState();filter_.setBiasEstimate(bias,sigma);
    for(auto &v:integral_)v=0;
    segmentUpdates_=0;++resets_;
  }
  LaunchMotion::Sample result(LaunchMotion::Sample sample)const {
    vqf_real_t q[4],bias[3];filter_.getQuat6D(q);filter_.getBiasEstimate(bias);
    sample.q={float(q[0]),float(q[1]),float(q[2]),float(q[3])};
    for(unsigned j=0;j<3;++j)sample.g[j]-=bias[j]*LaunchMotion::DEG;
    sample.fused=true;sample.settled=segmentUpdates_>=150;
    sample.stationary=filter_.getRestDetected();return sample;
  }
};
