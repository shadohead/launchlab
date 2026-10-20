#pragma once
#include "launch_motion.h"
#include "display_orientation.h"

// Fixed-center orientation replay. Coordinates below are display units only.
// Translation, height change, distance, velocity and compass heading are absent.
namespace MotionReplay {
// Replay the full stored context at 1x, including real pre/post optical motion.
// Short optical intervals alone can be briefer than one or two display frames.
inline uint32_t playbackDurationMs(const LaunchMotion::Trace &t) {
  if(!t.valid() || !t.fused)return 0;
  const int span=int(t.points[t.count-1].ms)-int(t.points[0].ms);
  return span>0?uint32_t(span):0;
}
inline float playbackProgress(const LaunchMotion::Trace &t,uint32_t elapsedMs) {
  const auto duration=playbackDurationMs(t);
  return duration?std::min(elapsedMs,duration)/float(duration):1.0f;
}
inline bool playbackNeedsFrame(const LaunchMotion::Trace &t,uint32_t elapsedMs,uint32_t lastElapsedMs) {
  // Draw once after crossing the end, then hold that endpoint. Comparing the
  // previous draw also catches a stalled UI that skips over the last sample.
  const auto duration=playbackDurationMs(t);
  return duration && (elapsedMs<duration || lastElapsedMs<duration);
}
struct Vec {
  float x=0,y=0,z=0;
  Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};}
  Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};}
  Vec operator*(float f)const{return {x*f,y*f,z*f};}
  float length()const{return std::sqrt(x*x+y*y+z*z);}
};
inline Vec rotate(const LaunchMotion::Quaternion &q,Vec v) {
  const float in[3]={v.x,v.y,v.z};float out[3];q.rotate(in,out);return {out[0],out[1],out[2]};
}
inline LaunchMotion::Quaternion forDisplay(LaunchMotion::Quaternion pose,bool flipEnabled) {
  // Change both board and horizontal world axes by 180 degrees about +Z:
  // Rz(pi) * pose * inverse(Rz(pi)). This reverses screen-right/top tilt like
  // the level dots, preserves gravity-up and yaw, and keeps neutral heading.
  // Apply after cache lookup so a display preview also works on saved traces.
  const auto sign=DisplayOrientation::axisSign(flipEnabled);
  pose.x*=sign;pose.y*=sign;
  return pose;
}
struct Replay {
  static constexpr unsigned COUNT=48;
  bool valid=false;
  LaunchMotion::Quaternion pose[COUNT];int ms[COUNT]={};
  LaunchMotion::Quaternion at(int timeMs)const {
    if(timeMs<=ms[0])return pose[0];
    for(unsigned i=1;i<COUNT;++i)if(timeMs<=ms[i])
      return LaunchMotion::Quaternion::blend(pose[i-1],pose[i],float(timeMs-ms[i-1])/std::max(1,ms[i]-ms[i-1]));
    return pose[COUNT-1];
  }
  bool build(const LaunchMotion::Trace &t) {
    valid=false;if(!t.valid() || !t.fused)return false;
    LaunchMotion::Interpolated onset;if(!LaunchMotion::interpolate(t,0,onset))return false;
    // Reset only compass heading. Preserve real gravity-relative roll/pitch:
    // inverse(onset)*pose would erase initial tilt and show a fictitious flat
    // start. Align the horizontal board width with the camera's horizontal
    // axis (+X,+Y), midway between the two front cube walls. An upright neutral
    // board then faces the nearest vertical edge instead of either wall.
    Vec width=rotate(onset.q,{1,0,0});float yaw;
    if(std::hypot(width.x,width.y)>.2f)yaw=-std::atan2(width.y,width.x);
    else {const Vec top=rotate(onset.q,{0,1,0});yaw=std::atan2(top.x,top.y);}
    yaw+=.785398163f;
    const LaunchMotion::Quaternion heading{std::cos(yaw*.5f),0,0,std::sin(yaw*.5f)};
    // Include all available stored pre/post context (new captures retain up to
    // 500 ms before and 150 ms after; older stored traces retain their span).
    // Optical onset fixes the relative orientation, not a guessed velocity.
    for(unsigned i=0;i<COUNT;++i) {
      ms[i]=t.points[0].ms+(t.points[t.count-1].ms-t.points[0].ms)*int(i)/int(COUNT-1);
      LaunchMotion::Interpolated sample;if(!LaunchMotion::interpolate(t,ms[i],sample))return false;
      pose[i]=LaunchMotion::multiply(heading,sample.q);
    }
    valid=true;return true;
  }
};
struct Cube {
  // Fixed scale and centered body for every capture. These are not meters.
  Vec low{-.84f,-.84f,-.84f},high{.84f,.84f,.84f};
  Vec corner(unsigned i)const {
    static constexpr unsigned bits[8]={0,1,3,2,4,5,7,6};const unsigned b=bits[i];
    return {b&1?high.x:low.x,b&2?high.y:low.y,b&4?high.z:low.z};
  }
  Vec body(const Replay &r,unsigned i,unsigned vertex)const {
    static constexpr unsigned bits[8]={0,1,3,2,4,5,7,6};const unsigned b=bits[vertex];
    // Board +X is screen-right, +Y is screen-top, +Z faces out of the screen.
    // Portrait proportions and a thin body follow those axes.
    return rotate(r.pose[i],{(b&1?1:-1)*.4f,(b&2?1:-1)*.65f,(b&4?1:-1)*.12f});
  }
};
// Board top / forward (+Y) recedes toward the upper right. USB (-Y) is nearer
// the bottom; gravity-up (+Z) is screen-up. Viewer is on (+X,-Y,+Z).
inline Vec camera(Vec v){return {(v.x+v.y)*.707106781f,(v.x-v.y-2*v.z)*.40824829f,0};}
}
