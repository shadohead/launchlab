#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "level_indicator.h"

namespace LaunchMotion {
static constexpr float DEG=57.295779513f;
struct Quaternion {
  float w=1,x=0,y=0,z=0;
  void integrate(const float *rate,float dt) {
    const float speed=std::sqrt(rate[0]*rate[0]+rate[1]*rate[1]+rate[2]*rate[2])/DEG;
    const float angle=speed*dt/2,k=speed>1e-8f?std::sin(angle)/speed/DEG:dt/2/DEG;
    const float a=std::cos(angle),b=rate[0]*k,c=rate[1]*k,d=rate[2]*k;
    const Quaternion q{w*a-x*b-y*c-z*d,w*b+x*a+y*d-z*c,w*c-x*d+y*a+z*b,w*d+x*c-y*b+z*a};
    const float n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
    w=q.w/n;x=q.x/n;y=q.y/n;z=q.z/n;
  }
  void rotate(const float *v,float *out)const {
    const float t[3]={2*(y*v[2]-z*v[1]),2*(z*v[0]-x*v[2]),2*(x*v[1]-y*v[0])};
    out[0]=v[0]+w*t[0]+y*t[2]-z*t[1];
    out[1]=v[1]+w*t[1]+z*t[0]-x*t[2];
    out[2]=v[2]+w*t[2]+x*t[1]-y*t[0];
  }
  static Quaternion blend(Quaternion a,Quaternion b,float f) {
    if(a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z<0){b.w=-b.w;b.x=-b.x;b.y=-b.y;b.z=-b.z;}
    Quaternion q{a.w+(b.w-a.w)*f,a.x+(b.x-a.x)*f,a.y+(b.y-a.y)*f,a.z+(b.z-a.z)*f};
    const float n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
    q.w/=n;q.x/=n;q.y/=n;q.z/=n;return q;
  }
  static float difference(const Quaternion &a,const Quaternion &b) {
    // Normalize the dot product in double precision. Packed/float quaternions
    // can otherwise report nonzero error even when comparing an identical pose.
    const double aa=double(a.w)*a.w+double(a.x)*a.x+double(a.y)*a.y+double(a.z)*a.z;
    const double bb=double(b.w)*b.w+double(b.x)*b.x+double(b.y)*b.y+double(b.z)*b.z;
    const double dot=double(a.w)*b.w+double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;
    return aa>0 && bb>0?float(2*std::acos(std::min(1.0,std::fabs(dot)/std::sqrt(aa*bb)))*DEG):180;
  }
  void angles(float &roll,float &pitch,float &yaw)const {
    roll=std::atan2(2*(w*x+y*z),1-2*(x*x+y*y))*DEG;
    pitch=std::asin(std::max(-1.0f,std::min(1.0f,2*(w*y-z*x))))*DEG;
    yaw=std::atan2(2*(w*z+x*y),1-2*(y*y+z*z))*DEG;
  }
};
enum class Quality:uint8_t { Missing, Valid, Gap, Clipped, TooLong, Warming };
inline const char *name(Quality q) {
  switch(q){case Quality::Valid:return "valid";case Quality::Gap:return "Motion incomplete";
    case Quality::Clipped:return "Motion out of range";
    case Quality::TooLong:return "Pull too long";case Quality::Warming:return "Motion warming up";default:return "Motion unavailable";}
}
struct Point {
  int16_t ms=0,a[3]={},g[3]={},q[4]={16384,0,0,0};
  Quaternion rotation()const {
    Quaternion r{q[0]/16384.0f,q[1]/16384.0f,q[2]/16384.0f,q[3]/16384.0f};
    const float n=std::sqrt(r.w*r.w+r.x*r.x+r.y*r.y+r.z*r.z);
    r.w/=n;r.x/=n;r.y/=n;r.z/=n;return r;
  }
  float magnitude(bool gyro)const {
    const int16_t *v=gyro?g:a;const float scale=gyro?.1f:.001f;
    return std::sqrt(float(v[0])*v[0]+float(v[1])*v[1]+float(v[2])*v[2])*scale;
  }
  void turn(float &right,float &down)const {
    const float normal[3]={0,0,1};float v[3];rotation().rotate(normal,v);
    right=std::atan2(v[0],v[2])*DEG;down=-std::atan2(v[1],v[2])*DEG;
  }
};
struct Trace {
  static constexpr unsigned POINTS=48;
  Quality quality=Quality::Missing;
  uint32_t number=0,durationMs=0;
  float rpm=0;
  unsigned count=0;
  bool stationaryBias=false,fused=false,saturated=false,bridged=false;
  bool approximate()const{return saturated || bridged;}
  // Largest single-axis reading from optical onset through post-roll, in the
  // launcher frame. Diagnostics only; never stored or compared.
  float peakAccelG=0,peakGyroDps=0;
  // Time of the first rejected sample relative to optical onset, if any.
  static constexpr int32_t NO_ISSUE=INT32_MIN;int32_t issueMs=NO_ISSUE;
  float gravity[3]={};
  StickS3Level::Reading startLevel,endLevel;
  Point points[POINTS];
  bool valid()const{return quality==Quality::Valid && count>=12 && count<=POINTS &&
    std::isfinite(rpm) && rpm>=1000 && durationMs>0 && durationMs<=2500;}
};
// q maps the sensor axes to a gravity-aligned frame; heading is arbitrary.
// Fusion runs continuously before trace reduction. g is bias-corrected deg/s.
// clipped: unusable (non-finite) data. saturated: a finite reading at sensor
// full scale. Fusion continues through saturation, which under-reads the true
// rate, so a trace containing it is kept but marked approximate.
struct Sample {uint64_t us=0;float a[3]={},g[3]={};Quaternion q;
  // bridged: interpolated across a short data gap; kept but approximate.
  bool clipped=false,saturated=false,bridged=false,fused=false,settled=false,stationary=false;};
static constexpr uint64_t PRE_ROLL_US=500000,POST_ROLL_US=150000;
inline bool captureReady(uint64_t now,uint64_t end,uint64_t latestFused) {
  // Resampling can lag the newest raw poll by one grid interval. Only fused
  // coverage establishes post-roll availability; a raw timestamp cannot.
  return now>=end+POST_ROLL_US && (latestFused>=end+POST_ROLL_US || now>=end+500000);
}
inline Quaternion inverse(const Quaternion &q){return {q.w,-q.x,-q.y,-q.z};}
inline Quaternion multiply(const Quaternion &a,const Quaternion &b) {
  return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
    a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
inline Quaternion relative(const Quaternion &onset,const Quaternion &pose) {
  // Express turn in the launch's initial sensor axes. This cancels arbitrary
  // global heading and never treats constant heading offsets as launch errors.
  return multiply(inverse(onset),pose);
}
inline StickS3Level::Reading levelAt(const Quaternion &pose,const float *baseline) {
  const Quaternion inverse{pose.w,-pose.x,-pose.y,-pose.z};float gravity[3];inverse.rotate(baseline,gravity);
  StickS3Level level;level.feed(gravity[0],gravity[1],gravity[2],1);
  const auto captured=level.reading(1);return captured.valid()?captured:StickS3Level::Reading{};
}
inline void recordLevels(Trace &t);
class Ring {
public:
  static constexpr unsigned CAPACITY=1024;
  // Discard pre-roll across a mounting change without a large stack temporary.
  void clear(){next_=count_=0;}
  void feed(const Sample &s) {
    if(count_ && s.us<=at(count_-1).us)return;
    samples_[next_]=s;next_=(next_+1)%CAPACITY;if(count_<CAPACITY)++count_;
  }
  unsigned size()const{return count_;}
  const Sample &at(unsigned i)const{return samples_[(next_+CAPACITY-count_+i)%CAPACITY];}
  uint64_t latest()const{return count_?at(count_-1).us:0;}
  Trace build(uint64_t start,uint64_t end,float rpm,uint32_t number)const {
    Trace out;out.rpm=rpm;out.number=number;
    if(end<=start || !count_)return out;
    if(end-start>2500000){out.quality=Quality::TooLong;return out;}
    const uint64_t desiredFirst=start>PRE_ROLL_US?start-PRE_ROLL_US:0,last=end+POST_ROLL_US;
    if(at(0).us>start || latest()<last)return out;
    unsigned onset=0,stop=0;
    while(onset+1<count_ && at(onset+1).us<=start)++onset;
    stop=onset;while(stop+1<count_ && at(stop).us<last)++stop;
    auto quality=[](const Sample &s) {
      if(s.clipped)return Quality::Clipped;
      if(!s.fused || !s.settled)return Quality::Warming;
      const float norm=s.q.w*s.q.w+s.q.x*s.q.x+s.q.y*s.q.y+s.q.z*s.q.z;
      if(!std::isfinite(norm) || norm<.98f || norm>1.02f)return Quality::Clipped;
      for(unsigned j=0;j<3;++j)if(!std::isfinite(s.a[j]) || !std::isfinite(s.g[j]))return Quality::Clipped;
      return Quality::Valid;
    };
    for(unsigned i=onset;i<=stop;++i)for(unsigned j=0;j<3;++j) {
      const auto &s=at(i);
      if(std::isfinite(s.a[j]))out.peakAccelG=std::max(out.peakAccelG,std::fabs(s.a[j]));
      if(std::isfinite(s.g[j]))out.peakGyroDps=std::max(out.peakGyroDps,std::fabs(s.g[j]));
    }
    // Optical onset through the measured post-roll is mandatory. A gap or
    // unsettled pose in the actual pull is never trimmed or interpolated over.
    for(unsigned i=onset;i<=stop;++i) {
      const auto &s=at(i);
      const int32_t ms=int32_t((int64_t(s.us)-int64_t(start))/1000);
      if(i>onset && s.us-at(i-1).us>35000){out.quality=Quality::Gap;out.issueMs=ms;return out;}
      const auto q=quality(s);if(q!=Quality::Valid){out.quality=q;out.issueMs=ms;return out;}
    }
    // Pre-launch context is optional. Keep only its newest continuous, settled
    // segment instead of rejecting a healthy pull for older startup/gap data.
    unsigned begin=onset;
    while(begin && at(begin).us>desiredFirst && at(begin).us-at(begin-1).us<=35000 &&
          quality(at(begin-1))==Quality::Valid)--begin;
    const uint64_t first=std::max(desiredFirst,at(begin).us);
    for(unsigned i=begin;i<=stop;++i)out.stationaryBias|=at(i).stationary;
    for(unsigned i=onset;i<=stop;++i){out.saturated|=at(i).saturated;out.bridged|=at(i).bridged;}
    // Gravity comes from continuous orientation fusion, never from a moving
    // acceleration average. Acceleration is retained for diagnostics only.
    out.gravity[2]=1;out.fused=true;
    out.durationMs=uint32_t((end-start)/1000);
    if(!out.durationMs)return out;
    const unsigned burst=std::min<unsigned>(24,out.durationMs+1);
    const unsigned pre=std::min<unsigned>((Trace::POINTS-burst)/2,unsigned((start-first)/1000)),post=Trace::POINTS-burst-pre;
    auto pack=[](float value,float scale){return int16_t(std::lround(std::max(-32767.0f,std::min(32767.0f,value*scale))));};
    unsigned hi=begin+1;
    for(unsigned pointIndex=0;pointIndex<Trace::POINTS;++pointIndex) {
      // Preserve optical onset and end exactly and devote half the display
      // points to the pull. Pre/post motion is retained without integrating
      // reduced chart points or synthesizing travel.
      uint64_t target;
      if(pointIndex<pre)target=first+(start-first)*pointIndex/pre;
      else if(pointIndex<pre+burst)target=start+(end-start)*(pointIndex-pre)/(burst-1);
      else target=end+POST_ROLL_US*(pointIndex-pre-burst+1)/post;
      while(hi<stop && at(hi).us<target)++hi;
      const auto &a=at(hi-1),&b=at(hi);
      const float f=float(target-a.us)/float(b.us-a.us);
      const auto pose=Quaternion::blend(a.q,b.q,f);
      auto &point=out.points[pointIndex];point.ms=int16_t((int64_t(target)-int64_t(start))/1000);
      float raw[3],world[3];
      for(unsigned j=0;j<3;++j){raw[j]=a.a[j]+(b.a[j]-a.a[j])*f;point.g[j]=pack(a.g[j]+(b.g[j]-a.g[j])*f,10);}
      pose.rotate(raw,world);
      for(unsigned j=0;j<3;++j)point.a[j]=pack(world[j]-out.gravity[j],1000);
      point.q[0]=pack(pose.w,16384);point.q[1]=pack(pose.x,16384);
      point.q[2]=pack(pose.y,16384);point.q[3]=pack(pose.z,16384);
      if(pointIndex==pre)out.startLevel=levelAt(pose,out.gravity);
      if(pointIndex==pre+burst-1)out.endLevel=levelAt(pose,out.gravity);
    }
    out.count=Trace::POINTS;
    out.quality=Quality::Valid;return out;
  }
private:
  Sample samples_[CAPACITY];unsigned count_=0,next_=0;
};
struct Interpolated {float a[3]={},g[3]={};Quaternion q;};
inline bool interpolate(const Trace &t,int ms,Interpolated &out) {
  if(!t.valid() || ms<t.points[0].ms || ms>t.points[t.count-1].ms)return false;
  unsigned hi=1;while(hi+1<t.count && t.points[hi].ms<ms)++hi;
  const Point &a=t.points[hi-1],&b=t.points[hi];
  if(b.ms<=a.ms)return false;
  const float f=float(ms-a.ms)/(b.ms-a.ms);
  for(unsigned j=0;j<3;++j){out.a[j]=(a.a[j]+(b.a[j]-a.a[j])*f)*.001f;out.g[j]=(a.g[j]+(b.g[j]-a.g[j])*f)*.1f;}
  out.q=Quaternion::blend(a.rotation(),b.rotation(),f);return true;
}
// Start/end tilt comes from recorded fused orientation at optical onset/end.
// Recap never reads the current sensor and never estimates travel distance.
inline void recordLevels(Trace &t) {
  t.startLevel={};t.endLevel={};
  if(!t.valid())return;
  for(unsigned i=0;i<2;++i) {
    Interpolated pose;if(!interpolate(t,i?int(t.durationMs):0,pose))continue;
    auto &reading=i?t.endLevel:t.startLevel;reading=levelAt(pose.q,t.gravity);
  }
}
struct Difference {bool valid=false;float turnDegrees=0,accelG=0,gyroDps=0,rpm=0,rpmPercent=0;int durationMs=0;unsigned overlap=0;};
inline Difference compare(const Trace &reference,const Trace &trial) {
  Difference d;if(!reference.valid() || !trial.valid() || !reference.fused || !trial.fused)return d;
  Interpolated refOnset,trialOnset;
  if(!interpolate(reference,0,refOnset) || !interpolate(trial,0,trialOnset))return d;
  double angle2=0,a2=0,g2=0;
  for(unsigned i=0;i<trial.count;++i) {
    Interpolated ref;if(!interpolate(reference,trial.points[i].ms,ref))continue;
    const auto &p=trial.points[i];const float angle=Quaternion::difference(relative(refOnset.q,ref.q),relative(trialOnset.q,p.rotation()));angle2+=angle*angle;
    for(unsigned j=0;j<3;++j){const float a=p.a[j]*.001f-ref.a[j],g=p.g[j]*.1f-ref.g[j];a2+=a*a;g2+=g*g;}
    ++d.overlap;
  }
  if(d.overlap<12)return d;
  d.valid=true;d.turnDegrees=std::sqrt(angle2/d.overlap);d.accelG=std::sqrt(a2/d.overlap);d.gyroDps=std::sqrt(g2/d.overlap);
  d.rpm=trial.rpm-reference.rpm;d.rpmPercent=100*d.rpm/reference.rpm;d.durationMs=int(trial.durationMs)-int(reference.durationMs);return d;
}
// Compact, canonical reference image: 1.1 KiB per alternating NVS slot.
static constexpr unsigned IMAGE_BYTES=64+Trace::POINTS*22+4;
using Image=std::array<uint8_t,IMAGE_BYTES>;
inline uint32_t checksum(const uint8_t *p,size_t n){uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(i*8));}
inline uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(p[i])<<(i*8);return v;}
inline void put16(uint8_t *p,int16_t v){p[0]=uint8_t(v);p[1]=uint8_t(uint16_t(v)>>8);}
inline int16_t get16(const uint8_t *p){return int16_t(uint16_t(p[0])|(uint16_t(p[1])<<8));}
inline void encode(const Trace &t,uint32_t generation,Image &image) {
  image.fill(0);put32(image.data(),0x544f4d4c);put32(image.data()+4,4);put32(image.data()+8,t.count);
  put32(image.data()+12,t.number);uint32_t rpm;std::memcpy(&rpm,&t.rpm,4);put32(image.data()+16,rpm);
  put32(image.data()+20,t.durationMs);put32(image.data()+24,generation);
  for(unsigned j=0;j<3;++j)put16(image.data()+28+j*2,int16_t(std::lround(t.gravity[j]*1000)));
  image[34]=t.stationaryBias;image[35]=t.fused;image[52]=uint8_t(t.saturated)|uint8_t(t.bridged<<1);
  for(unsigned i=0;i<2;++i) {
    const auto &level=i?t.endLevel:t.startLevel;auto *p=image.data()+36+i*8;
    if(level.valid()) {
      p[0]=uint8_t(level.state);put16(p+2,int16_t(std::lround(level.degrees*100)));
      put16(p+4,int16_t(std::lround(level.right*16384)));put16(p+6,int16_t(std::lround(level.down*16384)));
    }
  }
  for(unsigned i=0;i<t.count && i<Trace::POINTS;++i) {
    auto *p=image.data()+64+i*22;put16(p,t.points[i].ms);
    for(unsigned j=0;j<3;++j){put16(p+2+j*2,t.points[i].a[j]);put16(p+8+j*2,t.points[i].g[j]);}
    for(unsigned j=0;j<4;++j)put16(p+14+j*2,t.points[i].q[j]);
  }
  put32(image.data()+IMAGE_BYTES-4,checksum(image.data(),IMAGE_BYTES-4));
}
inline bool decode(const Image &image,Trace &out,uint32_t &generation) {
  const auto *p=image.data();
  const uint32_t schema=get32(p+4);
  if(get32(p)!=0x544f4d4c || (schema!=1 && schema!=2 && schema!=3 && schema!=4) || get32(p+IMAGE_BYTES-4)!=checksum(p,IMAGE_BYTES-4))return false;
  const unsigned count=get32(p+8);if(count<12 || count>Trace::POINTS)return false;
  Trace t;t.fused=schema>=4 && p[35]!=0;t.saturated=schema>=4 && (p[52]&1);t.bridged=schema>=4 && (p[52]&2);t.stationaryBias=schema==1 || p[34]!=0;t.count=count;t.number=get32(p+12);const uint32_t rpm=get32(p+16);std::memcpy(&t.rpm,&rpm,4);
  t.durationMs=get32(p+20);if(!std::isfinite(t.rpm) || t.rpm<1000 || t.durationMs>2500)return false;
  for(unsigned j=0;j<3;++j)t.gravity[j]=get16(p+28+j*2)*.001f;
  for(unsigned i=0;i<count;++i) {
    const auto *b=p+64+i*22;auto &point=t.points[i];point.ms=get16(b);
    if(point.ms< -520 || point.ms>2750 || (i && point.ms<=t.points[i-1].ms))return false;
    for(unsigned j=0;j<3;++j){point.a[j]=get16(b+2+j*2);point.g[j]=get16(b+8+j*2);}
    float n=0;for(unsigned j=0;j<4;++j){point.q[j]=get16(b+14+j*2);n+=point.q[j]/16384.0f*point.q[j]/16384.0f;}
    if(n<.98f || n>1.02f)return false;
  }
  t.quality=Quality::Valid;
  if(schema<3)recordLevels(t);
  else for(unsigned i=0;i<2;++i) {
    const auto *b=p+36+i*8;auto &level=i?t.endLevel:t.startLevel;
    if(b[0]!=uint8_t(StickS3Level::State::Unavailable) && b[0]!=uint8_t(StickS3Level::State::Valid))return false;
    if(b[0]==uint8_t(StickS3Level::State::Valid)) {
      level={StickS3Level::State::Valid,get16(b+2)*.01f,get16(b+4)/16384.0f,get16(b+6)/16384.0f};
      if(level.degrees<0 || level.degrees>90 || std::hypot(level.right,level.down)>1.001f)return false;
    }
  }
  out=t;generation=get32(p+24);return true;
}
}
