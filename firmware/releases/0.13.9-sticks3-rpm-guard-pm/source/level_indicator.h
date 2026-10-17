#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "display_orientation.h"

// Gravity-based screen-plane level, fed only by fresh accelerometer samples.
// This is an aiming aid while still, not a launch-angle measurement.
class StickS3Level {
public:
  static constexpr uint32_t STALE_MS=250;
  static constexpr float RIM_DEGREES=30.0f;
  enum class State:uint8_t { Unavailable, Moving, Valid };
  struct Reading {
    State state=State::Unavailable;
    float degrees=0,right=0,down=0;
    bool valid() const{return state==State::Valid;}
    const char *name() const {
      switch(state){case State::Valid:return "valid";
        case State::Moving:return "moving";default:return "unavailable";}
    }
  };
  static Reading forDisplay(Reading reading,bool flipEnabled) {
    const auto sign=DisplayOrientation::axisSign(flipEnabled);
    reading.right*=sign;reading.down*=sign;
    return reading;
  }
  void feed(float x,float y,float z,uint32_t now) {
    ++samples_;last_[0]=x;last_[1]=y;last_[2]=z;
    const uint32_t elapsed=now-lastSample_;
    lastSample_=now;seen_=true;
    const float length=std::sqrt(x*x+y*y+z*z);
    // Freefall, large acceleration and non-finite data cannot establish level.
    if(!std::isfinite(length) || length<0.75f || length>1.25f) {
      ++rejected_;usable_=initialized_=false;return;
    }
    const float alpha=(!initialized_ || elapsed>STALE_MS)?1.0f:
      float(elapsed)/(200.0f+elapsed);
    const float sample[3]={x,y,z};
    for(unsigned i=0;i<3;++i)gravity_[i]+=alpha*(sample[i]-gravity_[i]);
    initialized_=usable_=true;
  }
  Reading reading(uint32_t now) const {
    if(!seen_ || now-lastSample_>STALE_MS)return {};
    if(!usable_)return {State::Moving};
    const float horizontal=std::hypot(gravity_[0],gravity_[1]);
    if(std::hypot(horizontal,gravity_[2])<0.5f)return {State::Moving};
    const float angle=std::atan2(horizontal,std::fabs(gravity_[2]))*57.2957795f;
    const float radius=std::min(angle/RIM_DEGREES,1.0f);
    const float scale=horizontal>0.0001f?radius/horizontal:0;
    // Native portrait (rotation 0), USB-C down. The BMI270 diagram has raw
    // +X toward USB and +Y toward screen-right. M5Unified maps these to
    // board +X toward screen-right and +Y toward screen-top (X=rawY,Y=-rawX).
    // Reverse both screen directions per the user's mounted-device check.
    // The angle and rim scale stay unchanged.
    return {State::Valid,angle,gravity_[0]*scale,-gravity_[1]*scale};
  }
  uint32_t samples() const{return samples_;}
  uint32_t rejected() const{return rejected_;}
  const float *lastAccel() const{return last_;}
private:
  float gravity_[3]={},last_[3]={};
  uint32_t lastSample_=0,samples_=0,rejected_=0;
  bool seen_=false,usable_=false,initialized_=false;
};
