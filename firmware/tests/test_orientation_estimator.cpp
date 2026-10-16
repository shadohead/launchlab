#include "../LaunchLabMini/orientation_estimator.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace LaunchMotion;
static Sample measurement(uint64_t us,Quaternion pose,const float *rate,const float *bias=nullptr,float acceleration=0) {
  Sample s;s.us=us;const float up[3]={acceleration,0,1};inverse(pose).rotate(up,s.a);
  for(unsigned j=0;j<3;++j)s.g[j]=rate[j]+(bias?bias[j]:0);return s;
}
int main() {
  LauncherOrientation filter;Sample last;auto ring=std::make_unique<Ring>();const float zero[3]={},bias[3]={.8f,-.6f,.4f};
  // Stationary calibration at a nonzero tilt; no per-launch stationary gate.
  Quaternion tilted;const float tiltRate[3]={0,30,0};tilted.integrate(tiltRate,1);
  for(unsigned i=0;i<=3000;++i)filter.feed(measurement(1000000+i*10000,tilted,zero,bias),[&](const Sample &s){last=s;ring->feed(s);});
  assert(last.fused && last.settled && filter.rest());
  assert(Quaternion::difference(last.q,tilted)<1);
  float learned[3];filter.biasDegrees(learned);
  for(unsigned j=0;j<3;++j)assert(std::fabs(learned[j]-bias[j])<.08f);
  const Quaternion onset=last.q;Quaternion truth=tilted;const float rate[3]={120,-80,160};
  const uint32_t jitter[4]={7000,13000,11000,9000};uint64_t now=last.us;
  // Combined-axis rotation with irregular polling intervals, driven by a
  // piecewise constant analytic rate. Fusion stays on measured elapsed time.
  float maximum=0;unsigned emitted=0;
  for(unsigned i=0;i<100;++i) {
    const auto dt=jitter[i%4];now+=dt;truth.integrate(rate,dt*1e-6f);
    filter.feed(measurement(now,truth,rate,bias),[&](const Sample &s){
      if(s.us>now-dt && s.us<=now){
        Quaternion at=tilted;at.integrate(rate,(s.us-31000000)*1e-6f);
        maximum=std::max(maximum,Quaternion::difference(relative(onset,s.q),relative(tilted,at)));++emitted;
      }last=s;ring->feed(s);
    });
  }
  assert(emitted==100 && maximum<1.5f);
  const auto captured=ring->build(31300000,31700000,6000,1);
  assert(captured.valid() && captured.fused && captured.startLevel.valid() && captured.endLevel.valid());
  assert(compare(captured,captured).valid && compare(captured,captured).turnDegrees<.05f);
  const auto before=filter.updates();filter.feed(measurement(now,truth,rate),[&](const Sample &){assert(false);});assert(filter.updates()==before);
  // Missing motion is never silently bridged. Recovery can calibrate without
  // changing the independent optical acceptance path.
  now+=60000;filter.feed(measurement(now,truth,zero,bias),[&](const Sample &s){last=s;});
  assert(filter.resets()==1 && !last.settled);
  Sample bad=measurement(now+10000,truth,zero);bad.a[0]=NAN;
  filter.feed(bad,[&](const Sample &s){assert(s.clipped && !s.fused);});assert(filter.resets()==2);
  // A finite full-scale reading keeps fusion continuous and only marks the
  // grid samples it touches as approximate; it never resets or warms up.
  LauncherOrientation steady;Quaternion level;uint64_t at=1000000;unsigned marked=0,after=0;bool settledThrough=true;
  for(unsigned i=0;i<400;++i,at+=10000) {
    Sample m=measurement(at,level,zero);m.saturated=i==300;
    steady.feed(m,[&](const Sample &s){
      assert(!s.clipped && s.fused);if(i>=200)settledThrough&=s.settled;
      marked+=s.saturated;if(i>301)after+=s.saturated;
    });
  }
  assert(steady.resets()==0 && settledThrough && marked==2 && after==0);
  // A brief translational acceleration pulse may perturb inclination, but
  // cannot create displayed travel because replay has no position state.
  LauncherOrientation pulse;Quaternion neutral;float pulseError=0;
  for(unsigned i=0;i<=4000;++i) {
    const float a=i>=3000 && i<3008?.8f:0;
    pulse.feed(measurement(1000000+i*10000,neutral,zero,nullptr,a),[&](const Sample &s){
      if(i>=3000)pulseError=std::max(pulseError,Quaternion::difference(s.q,neutral));
    });
  }
  assert(pulseError<2);
  std::cout<<"PASS: VQF stationary bias, tilted gravity, combined-axis analytic rotation, measured-time jitter, duplicate rejection, gap reset, invalid samples, and short acceleration pulse. Max turn error="<<maximum<<" deg; pulse tilt="<<pulseError<<" deg\n";
}
