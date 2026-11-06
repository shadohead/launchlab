#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Bias-corrected gyro (degrees/s) and acceleration (g), in one mount frame.
// Presence is a heuristic: transport above these thresholds can also qualify.
// Filter signed vectors before their norms so alternating vibration cancels.
class MotionActivity {
public:
  static constexpr uint64_t MAX_GAP_US=100000,FRESH_US=250000;
  static constexpr uint64_t WARMUP_US=250000,CONFIRM_US=150000,RELEASE_US=300000;
  static constexpr float GYRO_ON=2.0f,GYRO_OFF=0.8f,ACCEL_ON=.04f,ACCEL_OFF=.015f;
  struct State {
    uint64_t sampleUs=0,lastMotionUs=0;
    float gyroDps=0,accelChangeG=0;
    bool valid=false,moving=false;
    bool fresh(uint64_t now)const{return valid && now>=sampleUs && now-sampleUs<=FRESH_US;}
  };
  void reset(){*this=MotionActivity{};}
  State state()const{return state_;}
  void feed(uint64_t us,const float *a,const float *g,bool usable=true) {
    for(unsigned j=0;j<3;++j)usable&=std::isfinite(a[j]) && std::isfinite(g[j]);
    if(!usable){reset();return;}
    if(state_.sampleUs && us<=state_.sampleUs)return;
    if(!state_.sampleUs || us-state_.sampleUs>MAX_GAP_US) {
      reset();started_=state_.sampleUs=us;
      for(unsigned j=0;j<3;++j)accel_[j]=baseline_[j]=a[j];
      return;
    }
    const float dt=float(us-state_.sampleUs)/1000000.0f;
    const float ag=dt/(.06f+dt),aa=dt/(.08f+dt),ab=dt/(1.0f+dt);
    float gyro2=0,accel2=0;
    for(unsigned j=0;j<3;++j) {
      gyro_[j]+=ag*(g[j]-gyro_[j]);accel_[j]+=aa*(a[j]-accel_[j]);
      baseline_[j]+=ab*(accel_[j]-baseline_[j]);
      gyro2+=gyro_[j]*gyro_[j];const float change=accel_[j]-baseline_[j];accel2+=change*change;
    }
    state_.sampleUs=us;state_.gyroDps=std::sqrt(gyro2);state_.accelChangeG=std::sqrt(accel2);
    state_.valid=us-started_>=WARMUP_US;
    if(!state_.valid)return;
    const bool above=state_.gyroDps>=GYRO_ON || state_.accelChangeG>=ACCEL_ON;
    const bool below=state_.gyroDps<GYRO_OFF && state_.accelChangeG<ACCEL_OFF;
    if(!state_.moving) {
      if(!above)candidate_=0;
      else if(!candidate_)candidate_=us;
      else if(us-candidate_>=CONFIRM_US){state_.moving=true;released_=0;}
    } else {
      if(!below)released_=0;
      else if(!released_)released_=us;
      else if(us-released_>=RELEASE_US){state_.moving=false;candidate_=released_=0;}
    }
    if(state_.moving)state_.lastMotionUs=us;
  }
private:
  State state_;
  uint64_t started_=0,candidate_=0,released_=0;
  float gyro_[3]={},accel_[3]={},baseline_[3]={};
};
