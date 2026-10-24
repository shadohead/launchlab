// Edge timing between the detector's 100 us evaluation grid (0.20.1).
// The rolling mean is evaluated every 5th 20 us sample; an edge used to be
// stamped at the grid point after its threshold crossing, so every period
// was a whole number of 100 us steps: one step is 2.6% of a turn at 15,500
// rpm and 4.8% at 29,000, and the 0.20 gate read a 29,000 rpm pull 6% low
// (a 2,200 us reading of a 2,069 us turn). Each crossing is now placed
// between the two grid values that straddle its threshold.
//
// Pulls like the HIL generator's (hil_optical.h: a 30% mark with 4-sample
// transitions, 1,000-count swing, Gaussian noise of 4 counts; standard_pull's
// ramp, hold and decay) at 8,000-30,000 rpm, each at eight grid phases (the
// first mark's offset within a turn and the sample within the 5-sample
// group), read by both estimators: within 1% of the peak.
#include "../LaunchLabRpm/analog_tachometer.h"
#include "../LaunchLabMini/sensor_profile.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
struct Generator {
  double phase=0;uint32_t rng=12345;
  double gauss() {
    double s=0;
    for(int i=0;i<12;++i){rng=rng*1664525u+1013904223u;s+=double(rng>>8)/16777216.0;}
    return s-6;
  }
  // One 20 us sample at `rpm` (0: no marks), as hil_optical.h draws it.
  uint16_t sample(double rpm,double depth,double noise=4,double baseline=1800) {
    const double step=rpm/60.0*20e-6,edge=4*step,duty=.3;
    phase+=step;phase-=std::floor(phase);
    double level=phase<duty?1:0;
    if(rpm>0 && phase<edge)level=phase/edge;
    else if(rpm>0 && phase>=duty && phase-duty<edge)level=1-(phase-duty)/edge;
    if(rpm<=0)level=0;
    return uint16_t(std::max(0.0,std::min(4095.0,baseline-depth*level+noise*gauss())));
  }
};

// standard_pull(peak): from max(1500, peak/4) to the peak over 400 ms, held
// 120 ms, down to 55% over 500 ms, then the marks stop.
double pullRpm(double peak,double ms) {
  const double start=std::max(1500.0,peak*.25);
  if(ms<400)return start+(peak-start)*ms/400;
  if(ms<520)return peak;
  if(ms<1020)return peak+(peak*.55-peak)*(ms-520)/500;
  return 0;
}

float measure(double peak,unsigned grid,bool singleTurn,uint32_t seed) {
  AnalogTachometer d;StickS3SensorProfile::configure(d);d.singleTurnPeak=singleTurn;
  Generator g;g.rng=seed;g.phase=double(grid)/8;
  for(unsigned i=0;i<60000+grid;++i)d.feed(g.sample(0,0));  // 1.2 s idle, then `grid` samples into the 5-sample group
  assert(d.phase==AnalogTachometer::Phase::Ready);
  for(double us=0;us<1020000;us+=20)d.feed(g.sample(pullRpm(peak,us/1000),1000));
  for(unsigned i=0;i<100000 && d.phase==AnalogTachometer::Phase::Launch;++i)d.feed(g.sample(0,0));
  return d.resultValid?d.resultRpm:0;
}
}

int main() {
  const double rpms[]={8000,12000,15500,22000,25000,27000,29000,30000};
  double worst=0;bool ok=true;
  for(const bool single:{true,false}) {
    std::printf("%s:\n",single?"single-turn peak":"three-turn sustained");
    for(const double rpm:rpms) {
      double lo=1e9,hi=-1e9;
      for(unsigned grid=0;grid<8;++grid) {
        const float r=measure(rpm,grid,single,1000+grid*7+unsigned(rpm));
        const double e=r>0?(r-rpm)/rpm:-1;
        lo=std::min(lo,e);hi=std::max(hi,e);worst=std::max(worst,std::fabs(e));
        if(!(std::fabs(e)<.01)){ok=false;std::printf("  %5.0f rpm, grid phase %u: read %.0f (%+.2f%%)\n",rpm,grid,r,100*e);}
      }
      std::printf("  %5.0f rpm: %+.2f%% .. %+.2f%% over 8 grid phases\n",rpm,100*lo,100*hi);
    }
  }
  assert(ok);
  std::printf("PASS: both estimators within 1%% at 8,000-30,000 rpm over 8 grid phases (worst %.2f%%)\n",100*worst);
}
