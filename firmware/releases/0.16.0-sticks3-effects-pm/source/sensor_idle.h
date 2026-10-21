#pragma once
#include "motion_activity.h"

// Experimental sampling-only policy. The shared 3V3_L2 rail stays powered.
// Entry uses confirmed inactivity; exit deliberately uses the lower motion
// threshold without waiting for confirmation, to shorten the blind interval.
class SensorIdle {
public:
  static constexpr uint32_t IDLE_MS=30000,RETRY_MS=1000;
  enum class Action {None,Pause,Resume};
  Action decide(uint32_t now,uint64_t clockUs,const MotionActivity::State &activity,
                uint32_t idleMs,bool resting,bool opticalIdle,bool blocked)const {
    const bool responsive=activity.moving || activity.gyroDps>=MotionActivity::GYRO_OFF ||
      activity.accelChangeG>=MotionActivity::ACCEL_OFF;
    const bool awake=blocked || !activity.fresh(clockUs) || responsive || idleMs<IDLE_MS;
    if(resting)return awake && (!resumeFailed_ || uint32_t(now-lastTry_)>=RETRY_MS)?Action::Resume:Action::None;
    if(awake || !opticalIdle || (attempted_ && uint32_t(now-lastTry_)<RETRY_MS) ||
       (armed_ && uint32_t(now-armedAt_)<IDLE_MS))return Action::None;
    return Action::Pause;
  }
  void attempted(uint32_t now,Action action,bool ok) {
    attempted_=true;lastTry_=now;
    resumeFailed_=action==Action::Resume && !ok;
    if(action==Action::Resume && ok){armed_=true;armedAt_=now;}
    if(action==Action::Pause && ok)++pauses_;
    if(action==Action::Resume && ok)++resumes_;
  }
  uint32_t pauses()const{return pauses_;}
  uint32_t resumes()const{return resumes_;}
private:
  uint32_t lastTry_=0,armedAt_=0,pauses_=0,resumes_=0;
  bool attempted_=false,armed_=false,resumeFailed_=false;
};
