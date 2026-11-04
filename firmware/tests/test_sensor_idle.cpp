#include "sensor_idle.h"
#include <cassert>
#include <iostream>
int main() {
  using A=SensorIdle::Action;
  SensorIdle policy;MotionActivity::State activity;
  uint32_t now=30000;uint64_t us=uint64_t(now)*1000;
  activity.valid=true;activity.sampleUs=us;
  auto decide=[&](uint32_t idle,bool resting=false,bool optical=true,bool blocked=false) {
    return policy.decide(now,us,activity,idle,resting,optical,blocked);
  };
  assert(decide(29999)==A::None && decide(30000)==A::Pause);
  // A shorter rest delay (the battery savers' restMs) pauses sooner.
  assert(policy.decide(now,us,activity,9999,false,true,false,10000)==A::None);
  assert(policy.decide(now,us,activity,10000,false,true,false,10000)==A::Pause);
  assert(decide(30000,false,false)==A::None && decide(30000,false,true,true)==A::None);
  activity.valid=false;assert(decide(30000)==A::None && decide(30000,true)==A::Resume);
  activity.valid=true;activity.sampleUs=us+1;assert(decide(30000)==A::None);
  activity.sampleUs=us-MotionActivity::FRESH_US-1;assert(decide(30000)==A::None);
  assert(decide(30000,true)==A::Resume);activity.sampleUs=us;
  policy.attempted(now,A::Pause,false);assert(decide(30000)==A::None);
  now+=1000;us+=1000000;activity.sampleUs=us;assert(decide(30000)==A::Pause);
  policy.attempted(now,A::Pause,true);assert(policy.pauses()==1);
  assert(decide(30000,true)==A::None);
  // Waking is intentionally faster/more sensitive than confirmed use. A tiny
  // qualifying rate can resume ADC without promoting the short sleep window.
  activity.gyroDps=MotionActivity::GYRO_OFF;assert(!activity.moving && decide(30000,true)==A::Resume);
  policy.attempted(now,A::Resume,false);assert(decide(30000,true)==A::None);
  now+=999;us+=999000;activity.sampleUs=us;assert(decide(30000,true)==A::None);
  ++now;us+=1000;activity.sampleUs=us;assert(decide(30000,true)==A::Resume);
  policy.attempted(now,A::Resume,true);assert(policy.resumes()==1);
  activity.gyroDps=0;assert(decide(30000)==A::None); // Minimum armed interval prevents chatter.
  now+=30000;us+=30000000;activity.sampleUs=us;assert(decide(30000)==A::Pause);
  activity.accelChangeG=MotionActivity::ACCEL_OFF;assert(decide(30000,true)==A::Resume);
  activity.accelChangeG=0;assert(decide(0,true)==A::Resume && decide(30000,true,true,true)==A::Resume);
  policy=SensorIdle{};now=UINT32_MAX-10000;us=uint64_t(now)*1000;activity.sampleUs=us;
  policy.attempted(now,A::Resume,true);now+=29999;us+=29999000;activity.sampleUs=us;
  assert(decide(30000)==A::None);++now;us+=1000;activity.sampleUs=us;assert(decide(30000)==A::Pause);
  std::cout<<"Sensor idle: freshness, responsive wake, optical/busy guards, retries, no chatter and wrap passed\n";
}
