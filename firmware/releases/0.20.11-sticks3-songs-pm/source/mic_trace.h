#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "mic_rpm.h"

// The spin heard since a launch, as the Mic RPM page plots it (mic_view_ui.h):
// slots 0.1 s apart from the end of the pull, a locked spin filling every
// second one. The apps start their time axis at the first tracked reading; this
// one starts at the launch, so the late start stays visible. Heard RPM is
// Hz x 60 / the cycles per turn the optical peak resolved, and never the
// optical RPM, which stays a separate marker at time zero.
namespace MicTrace {
constexpr unsigned MAX_READINGS=1200;  // 2 minutes of slots; the apps stop at 10
constexpr unsigned LOST_READINGS=80;   // 8 s of slots with no evidence ends a spin, as in the apps
constexpr float HOP_S=float(MicRpm::HOP)/float(MicRpm::TRACK_HZ);
constexpr float FIRST_S=float(MicRpm::WINDOW)/float(MicRpm::TRACK_HZ)/2;  // a reading is timed at its window's centre
constexpr float GAP_S=0.4f;            // a longer gap breaks the line, as in the apps
constexpr float MIN_SPAN_S=10,SPAN_STEP_S=10,MIN_TOP_RPM=9000,TOP_STEP_RPM=3000;

// The slot of a reading `seconds` after the release, at its window's centre.
inline unsigned slot(float seconds){return unsigned(std::lround(std::max(seconds-FIRST_S,0.f)/HOP_S));}
// One reading, from the analysis task to the loop.
struct Step {
  uint32_t launch=0;uint16_t index=0,rpm=0;
  MicRpm::Reason reason=MicRpm::Reason::Quiet;bool last=false;
};
// The apps' launch detector's evidence of a spin: trusted, or about to be.
inline bool evidence(const MicRpm::Reading &r) {
  return r.valid || (r.reason==MicRpm::Reason::Acquiring && r.support>=3 && r.score>=MicRpm::MIN_SCORE && r.margin>=MicRpm::MIN_MARGIN);
}
inline Step step(uint32_t launch,unsigned index,const MicRpm::Reading &r,bool last) {
  Step s;s.launch=launch;s.index=uint16_t(index);s.reason=r.reason;s.last=last;
  if(r.valid)s.rpm=uint16_t(std::lround(r.rpm));
  return s;
}
// Whether the reading at `index` is the spin's last: `quiet` readings have
// passed without evidence, or the trace is full.
inline bool finished(unsigned index,unsigned quiet){return quiet>=LOST_READINGS || index+1>=MAX_READINGS;}

// A spin's readings in order, on the analysis task: the Step each becomes, and
// when the spin is over.
class Follower {
public:
  Step next(uint32_t launch,const MicRpm::Reading &r,float seconds) {
    const unsigned at=MicTrace::slot(seconds);
    quiet_=evidence(r)?0:quiet_+(skipped_?1:at-slot_);slot_=at;skipped_=false;
    done_=finished(at,quiet_);
    return step(launch,at,r,done_);
  }
  // The reader skipped sound the ring had lost: the slots up to the next
  // reading were never heard, so they are not quiet ones.
  void skip(){skipped_=true;}
  bool done()const{return done_;}
  unsigned slot()const{return slot_;}
private:
  unsigned slot_=0,quiet_=0;bool done_=false,skipped_=false;
};

// Owned by the loop: the analysis task only sends Steps. Its slots live
// beside it in StoredTrace, so an empty one costs almost nothing.
class Trace {
public:
  constexpr explicit Trace(uint16_t *slots=nullptr):rpm_(slots){}
  void clear(){launch_=0;count_=heard_=first_=last_=0;peak_=optical_=0;started_=ended_=false;reason_=MicRpm::Reason::Quiet;}
  // `opticalRpm`: the launch's peak, 0 when the optical reading was invalid.
  void begin(uint32_t launch,float opticalRpm=0){clear();launch_=launch;optical_=uint16_t(std::lround(std::min(opticalRpm,65535.f)));started_=true;}
  // Steps of an older launch and repeats are ignored; a lost step reads as no reading.
  void add(const Step &s) {
    if(!started_ || s.launch!=launch_ || s.index<count_ || s.index>=MAX_READINGS)return;
    while(count_<s.index)rpm_[count_++]=0;
    rpm_[count_++]=s.rpm;reason_=s.reason;ended_=s.last;
    if(!s.rpm)return;
    if(!heard_)first_=s.index;
    last_=s.index;++heard_;peak_=std::max(peak_,s.rpm);
  }
  bool started()const{return started_;}
  bool ended()const{return ended_;}
  uint32_t launch()const{return launch_;}
  unsigned count()const{return count_;}
  unsigned heard()const{return heard_;}
  uint16_t rpm(unsigned i)const{return rpm_[i];}
  static float seconds(unsigned i){return FIRST_S+float(i)*HOP_S;}
  float elapsed()const{return count_?seconds(count_-1):0;}
  float firstSeconds()const{return seconds(first_);}
  float lastSeconds()const{return seconds(last_);}
  uint16_t firstRpm()const{return heard_?rpm_[first_]:0;}
  uint16_t latestRpm()const{return heard_?rpm_[last_]:0;}
  uint16_t peakRpm()const{return peak_;}
  uint16_t opticalRpm()const{return optical_;}
  MicRpm::Reason reason()const{return reason_;}
  // The change from the first reading to the latest, in percent.
  float changePercent()const{return heard_?100*(float(latestRpm())-float(firstRpm()))/float(firstRpm()):0;}
private:
  uint16_t *rpm_;  // 0: no trusted reading
  uint32_t launch_=0;unsigned count_=0,heard_=0,first_=0,last_=0;uint16_t peak_=0,optical_=0;
  bool started_=false,ended_=false;MicRpm::Reason reason_=MicRpm::Reason::Quiet;
};
class StoredTrace:public Trace {
public:
  StoredTrace():Trace(slots_){}
private:
  uint16_t slots_[MAX_READINGS]={};
};

// A made-up spin: `silent` readings before the first trusted one, then an
// exponential decline over `seconds`, which stays above the tracker's 1,800
// RPM floor for two minutes at 100. For the render check and USB `Y`.
inline void demo(Trace &t,unsigned readings,unsigned silent,float seconds=30) {
  t.begin(0,6900);
  for(unsigned i=0;i<readings;++i) {
    Step s;s.index=uint16_t(i);s.reason=i<silent?MicRpm::Reason::BroadbandNoise:MicRpm::Reason::Tracking;
    if(i>=silent)s.rpm=uint16_t(std::lround(6700*std::exp(-float(i-silent)*HOP_S/seconds)));
    t.add(s);
  }
}

// The axes: 0 RPM at the foot as in the apps, with whole thousands at each of
// their three divisions, and a whole number of 10 s spans so they stay put.
inline float topRpm(float peak){return std::ceil(std::max(MIN_TOP_RPM,peak*1.08f)/TOP_STEP_RPM)*TOP_STEP_RPM;}
inline float spanSeconds(float elapsed){return std::max(MIN_SPAN_S,std::ceil(elapsed/SPAN_STEP_S)*SPAN_STEP_S);}
// Calls emit(column,rpm,join) for each point of the line, columns counted
// from the launch in a plot `width` pixels across: the mean of the readings
// on that column, so a long spin costs at most one segment per column. `join`
// connects it to the point before; a gap of GAP_S or more does not.
template<class Emit> inline void columns(const Trace &t,int width,float span,Emit emit) {
  int column=-1;float sum=0,previous=0;unsigned n=0;bool join=false;
  for(unsigned i=0;i<t.count();++i) {
    if(!t.rpm(i))continue;
    const float time=Trace::seconds(i);
    const int x=int(std::lround(time/span*float(width)));
    if(n && x!=column){emit(column,sum/float(n),join);join=time-previous<GAP_S;sum=0;n=0;}
    column=x;sum+=float(t.rpm(i));++n;previous=time;
  }
  if(n)emit(column,sum/float(n),join);
}
}
