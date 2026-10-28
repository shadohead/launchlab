// The recap's RPM curve (0.20.5): every strong turn of a launch, from the
// slow turns before it through its peak to the slow turns that ended it.
#include "../LaunchLabRpm/analog_tachometer.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using D=AnalogTachometer;
static void level(D &d,int value,uint32_t us){for(uint32_t i=0;i<us/20;i++)d.feed(uint16_t(value));}
static void turn(D &d,uint32_t period,int low=250,int high=1250){level(d,high,period/2);level(d,low,period-period/2);}
static D ready(){D d;level(d,250,1200000);assert(d.phase==D::Phase::Ready);return d;}
static void finish(D &d){level(d,250,1600000);}
static uint32_t periodOf(float rpm){return uint32_t(lroundf(60e6f/rpm/20))*20;}
static bool near(float a,float b,float tolerance){return std::fabs(a-b)<=tolerance;}
int main() {
  // A pull: the gear starting from rest, rising to 6,000 RPM, then slowing
  // until two slow turns end it. Turn periods are whole samples.
  {
    D d=ready();
    std::vector<float> rpms;
    for(float rpm:{400.f,700.f,900.f})rpms.push_back(rpm);
    for(float rpm=1200;rpm<6000;rpm*=1.3f)rpms.push_back(rpm);
    for(float rpm:{6000.f,6000.f,5600.f,5000.f,3900.f,3500.f})rpms.push_back(rpm);
    for(float rpm:rpms)turn(d,periodOf(rpm));
    // A turn completes at the next mark's rise: this one closes the last slow
    // turn, which ends the launch, and is itself after the end.
    turn(d,periodOf(3200));finish(d);
    assert(d.launches==1 && d.resultValid && d.ended==D::End::Slowdown);
    const auto &c=d.curve;
    // Three slow turns lead into the launch (the 1,000 RPM floor starts it),
    // and every turn is a point, through both slow turns that ended it.
    assert(c.stride==1);
    assert(near(c.rpm[0],400,5));
    assert(near(c.peak(),6000,30));
    for(unsigned i=1;i<c.count;++i)assert(c.ms[i]>c.ms[i-1]);
    assert(near(c.rpm[c.count-1],3500,20) && near(c.rpm[c.count-2],3900,20));
    // Each point is one turn, in order: the turns from the first point on.
    unsigned first=0;
    assert(c.count==rpms.size());
    for(unsigned i=0;i<c.count;++i)assert(near(c.rpm[i],rpms[first+i],rpms[first+i]*.01f));
    // The points end where the turns end, in ms from the first turn's start.
    float ms=0;for(unsigned i=0;i<c.count;++i){ms+=60000.f/rpms[first+i];assert(near(c.ms[i],ms,1.5f));}
  }
  // A long pull folds in pairs: the points stay within POINTS, the fastest
  // turn survives as the peak and the last point is the last turn.
  {
    D d=ready();
    for(unsigned i=0;i<250;++i)turn(d,periodOf(i==137?3300:3000));
    turn(d,periodOf(1800));turn(d,periodOf(1600));turn(d,periodOf(1500));finish(d);
    assert(d.launches==1 && d.ended==D::End::Slowdown);
    const auto &c=d.curve;
    assert(c.stride>1 && c.count<=D::Curve::POINTS && c.count>D::Curve::POINTS/2);
    assert(near(c.peak(),3300,15));
    for(unsigned i=1;i<c.count;++i)assert(c.ms[i]>c.ms[i-1]);
    const float total=250*20.f+60000.f/1800+60000.f/1600;
    assert(near(c.ms[c.count-1],total,3));
    // A folded point keeps its faster turn: the last one holds the slow
    // pair or a fast turn before it, never a speed no turn had.
    for(unsigned i=0;i<c.count;++i)assert(c.rpm[i]<=3315);
  }
  // A new launch starts its own curve; lost samples clear it.
  {
    D d=ready();
    for(float rpm:{3000.f,3500.f,4000.f,4000.f,4000.f,2500.f,2200.f,2000.f})turn(d,periodOf(rpm));finish(d);
    assert(d.launches==1 && near(d.curve.peak(),4000,20));
    for(float rpm:{1800.f,2000.f,2000.f,2000.f,1200.f,1100.f,1000.f})turn(d,periodOf(rpm));finish(d);
    assert(d.launches==2 && near(d.curve.peak(),2000,10) && near(d.curve.rpm[0],1800,10));
    d.dataLoss();assert(d.curve.count==0);
  }
  // Slow handling never forms a curve: no launch, nothing to draw.
  {
    D d=ready();for(unsigned i=0;i<12;++i)turn(d,periodOf(600));finish(d);
    assert(d.launches==0 && d.curve.count==0);
  }
  std::cout<<"rpm curve tests passed\n";
}
