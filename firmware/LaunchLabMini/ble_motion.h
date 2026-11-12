#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "motion_replay.h"

// The stick's 3D orientation for the LaunchLab profile (docs/BLE_PROTOCOL.md,
// Live orientation and Tilt replay): the fusion's latest pose as a typed
// event, and the latest pull's tilt replay, the poses the recap draws. Host
// testable: bytes only. The poses are the stick's own, in its sensor axes as
// Mounting maps them; nothing here changes what is measured or drawn.
namespace BleMotion {
// Event type 2: `type u8 = 2, w i16, x i16, y i16, z i16`, the quaternion scaled by 16384.
static constexpr uint8_t POSE_EVENT=2;
static constexpr size_t POSE_BYTES=9;
static constexpr uint32_t PERIOD_MS=100;
inline uint16_t unit(float v){return uint16_t(int16_t(std::lround(std::max(-1.0f,std::min(1.0f,v))*16384)));}
// About ten a second, never in a pull, and only while the fusion is ready. A
// refused tick does not use up the period.
struct Gate {
  uint32_t last=0;
  bool due(uint32_t now,bool launch,bool ready) {
    if(launch || !ready || now-last<PERIOD_MS)return false;
    last=now;return true;
  }
};
inline void encodePose(const LaunchMotion::Quaternion &q,uint8_t *out) {
  const float parts[4]={q.w,q.x,q.y,q.z};
  out[0]=POSE_EVENT;
  for(unsigned j=0;j<4;++j){const uint16_t v=unit(parts[j]);out[1+2*j]=uint8_t(v);out[2+2*j]=uint8_t(v>>8);}
}
// Get replay's payload: `number u32, durationMs u32, count u16, flags u8`, then
// `count` points of `ms i32, w i16, x i16, y i16, z i16`. The count is the
// recap's 48, or 0 when the recap could not draw this pull's motion. Flag bit 0
// is the recap's "Tilt approx." mark. `scratch` is the caller's: a Replay is
// too large for the loop's stack.
static constexpr size_t REPLAY_HEADER=11,REPLAY_POINT=12,REPLAY_BYTES=REPLAY_HEADER+MotionReplay::Replay::COUNT*REPLAY_POINT;
inline size_t encodeReplay(const LaunchMotion::Trace &t,MotionReplay::Replay &scratch,uint8_t *out,size_t capacity) {
  if(capacity<REPLAY_BYTES)return 0;
  auto put=[&out](uint32_t v,unsigned bytes){for(unsigned i=0;i<bytes;++i)*out++=uint8_t(v>>(8*i));};
  const bool usable=scratch.build(t);
  put(t.number,4);put(t.durationMs,4);put(usable?MotionReplay::Replay::COUNT:0,2);put(usable && t.approximate()?1:0,1);
  if(!usable)return REPLAY_HEADER;
  for(unsigned i=0;i<MotionReplay::Replay::COUNT;++i) {
    const auto &q=scratch.pose[i];put(uint32_t(scratch.ms[i]),4);
    put(unit(q.w),2);put(unit(q.x),2);put(unit(q.y),2);put(unit(q.z),2);
  }
  return REPLAY_BYTES;
}
}
