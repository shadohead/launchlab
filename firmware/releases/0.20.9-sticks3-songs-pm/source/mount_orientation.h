#pragma once
#include <cstdint>

// Normalize IMU samples to the original Top launcher frame before fusion.
// M5Unified native axes: +X screen-right, +Y screen-top, +Z screen-normal.
// The supported sideways Sidecar pose has B key / +X toward the user and
// Hat / +Y toward the ripcord. Its +X is Top +Z; its +Z is Top -X.
// This proper rotation applies equally to acceleration and angular rate.
// LCD Flip is a separate presentation choice and never changes this frame.
namespace MountOrientation {
enum class Mode:uint8_t { Top=0, Sidecar=1 };
inline bool valid(unsigned value){return value<=unsigned(Mode::Sidecar);}
inline const char *label(Mode mode){return mode==Mode::Sidecar?"Sidecar":"Top";}
inline void mapVector(Mode mode,const float *in,float *out) {
  // Cache every component so in-place mapping has the same result.
  const float x=in[0],y=in[1],z=in[2];
  if(mode==Mode::Sidecar){out[0]=-z;out[1]=y;out[2]=x;}
  else {out[0]=x;out[1]=y;out[2]=z;}
}
// The launcher-frame vector `in`, measured in Mounting `from`, as Mounting `to`
// would have read the same device sample.
inline void remapVector(Mode from,Mode to,const float *in,float *out) {
  const float x=in[0],y=in[1],z=in[2];
  const float device[3]={from==Mode::Sidecar?z:x,y,from==Mode::Sidecar?-x:z};
  mapVector(to,device,out);
}
}
