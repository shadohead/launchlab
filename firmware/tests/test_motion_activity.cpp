#include "motion_activity.h"
#include "power_status.h"
#include <cassert>
#include <iostream>
#include <limits>

int main() {
  MotionActivity filter;uint64_t us=1000000;
  float a[3]={0,0,1},g[3]={};
  auto feed=[&](unsigned n){for(unsigned i=0;i<n;++i){us+=10000;filter.feed(us,a,g);}};
  feed(100);assert(filter.state().valid && !filter.state().moving && !filter.state().lastMotionUs);
  // Bias has already been removed by production fusion. Small residual jitter
  // and alternating table/transport vibration cannot qualify as sustained use.
  for(unsigned i=0;i<4000;++i) {
    a[0]=(i&1)?.1f:-.1f;g[0]=(i&1)?6.f:-6.f;
    feed(1);assert(!filter.state().moving);
  }
  a[0]=g[0]=0;feed(100);
  g[0]=10;feed(2);g[0]=0;feed(100);assert(!filter.state().lastMotionUs);
  // A deliberate slow angle adjustment qualifies before a third of a second.
  g[1]=3;feed(35);assert(filter.state().moving && filter.state().lastMotionUs==us);
  const auto active=filter.state();feed(100);assert(filter.state().lastMotionUs>active.lastMotionUs);
  g[1]=0;feed(300);assert(!filter.state().moving);
  assert(filter.state().fresh(us) && !filter.state().fresh(us+MotionActivity::FRESH_US+1));
  assert(!filter.state().fresh(us-1));
  // Translation qualifies through acceleration even without body rotation.
  filter.reset();feed(100);a[0]=.15f;feed(40);assert(filter.state().moving);
  a[0]=0;feed(500);assert(!filter.state().moving);
  const auto before=filter.state();filter.feed(us,a,g);filter.feed(us-1,a,g);
  assert(filter.state().sampleUs==before.sampleUs && filter.state().lastMotionUs==before.lastMotionUs);
  us+=MotionActivity::MAX_GAP_US+1;filter.feed(us,a,g);
  assert(!filter.state().valid && !filter.state().moving && !filter.state().lastMotionUs);
  feed(100);a[0]=std::numeric_limits<float>::quiet_NaN();feed(1);
  assert(!filter.state().valid && !filter.state().sampleUs);
  a[0]=0;feed(100);filter.feed(us+10000,a,g,false);assert(!filter.state().valid);

  // A 3-minute session (the default is 1 minute from 0.20.2).
  InactivityTimer timer;assert(timer.setMinutes(3));timer.begin(1000,true);
  for(uint32_t t=1000;t<91000;t+=100)timer.motion(t);
  assert(timer.unusedShake() && timer.activeTimeout()==30000 && !timer.due(91000));
  assert(!timer.due(120899) && timer.due(120900));
  timer.touch(121000);assert(!timer.unusedShake() && timer.activeTimeout()==180000);
  timer.motion(140000);assert(timer.remaining(140000)==180000);
  timer.begin(UINT32_MAX-15000,true);timer.motion(UINT32_MAX-10000);
  assert(!timer.due(19998) && timer.due(19999));
  std::cout<<"Motion activity: vibration, adjustment, translation, stale/invalid data and rolling sleep windows passed\n";
}
