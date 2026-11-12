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
  // A unit shaken awake in a moving bag: movement extends the 30 s window,
  // but it sleeps two minutes after the wake however long the movement lasts.
  timer.begin(500000,true);
  for(uint32_t t=500000;t<500000+InactivityTimer::UNUSED_SHAKE_LIMIT_MS;t+=100) {
    timer.motion(t);assert(!timer.due(t) && timer.remaining(t)<=InactivityTimer::UNUSED_SHAKE_LIMIT_MS-(t-500000));
  }
  timer.motion(620000);assert(timer.due(620000) && !timer.remaining(620000) && timer.unusedShake());
  assert(!timer.due(620000,true));  // never during a measured pull
  // A button, pull or USB command before the limit makes it a session: the
  // shake limit is gone, and movement keeps it awake up to MOTION_LIMIT_MS
  // after that use, never longer (through 0.20.5 a used unit carried in a
  // pocket or bag stayed awake for as long as it moved).
  timer.begin(1000000,true);timer.motion(1100000);timer.touch(1110000);
  for(uint32_t t=1110000;t<1260000;t+=100)timer.motion(t);
  assert(!timer.unusedShake() && !timer.due(1260000) && timer.remaining(1260000)==InactivityTimer::MOTION_LIMIT_MS-150000);
  for(uint32_t t=1260000;t<1110000+InactivityTimer::MOTION_LIMIT_MS;t+=100) {
    timer.motion(t);assert(!timer.due(t) && timer.remaining(t)<=InactivityTimer::MOTION_LIMIT_MS-(t-1110000));
  }
  const uint32_t limit=1110000+InactivityTimer::MOTION_LIMIT_MS;
  timer.motion(limit);if(!timer.due(limit))std::cout<<"movement kept a used unit awake past the limit\n";
  assert(timer.due(limit) && !timer.due(limit,true));
  timer.touch(limit+100);assert(!timer.due(limit+100) && timer.remaining(limit+100)==180000);
  // Still, the selected timeout is all there is: no movement, no change.
  timer.begin(3000000);assert(!timer.due(3179999) && timer.due(3180000));
  // A selected timeout longer than the limit: movement never ends it sooner.
  assert(timer.setMinutes(10));timer.begin(4000000);
  for(uint32_t t=4000000;t<4000000+590000;t+=100)timer.motion(t);
  assert(!timer.due(4590000) && !timer.due(4599999) && timer.due(4600000));
  assert(timer.setMinutes(3));
  // Finishing a USB standby after an unplug: due at once, and handling the
  // unit cannot keep it awake; a button still makes it a session.
  timer.begin(2000000-InactivityTimer::UNUSED_SHAKE_LIMIT_MS,true);
  assert(timer.due(2000000));timer.motion(2000100);assert(timer.due(2000100) && timer.unusedShake());
  timer.touch(2000200);assert(!timer.due(2000200) && !timer.unusedShake());
  // The limit holds across the millisecond counter's wrap.
  timer.begin(UINT32_MAX-60000,true);
  for(uint32_t t=UINT32_MAX-60000,n=0;n<1200;t+=100,++n)timer.motion(t);
  assert(timer.due(uint32_t(UINT32_MAX-60000+120000)) && !timer.due(uint32_t(UINT32_MAX-60000+119900)));
  // Optical activity that is not a pull (power_status.h OpticalSleepGuard).
  {
    OpticalSleepGuard guard;
    assert(!guard.measuring(1000,false) && guard.measuring(2000,true));
    assert(guard.measuring(2000+OpticalSleepGuard::MEASURE_LIMIT_MS-1,true));
    if(guard.measuring(2000+OpticalSleepGuard::MEASURE_LIMIT_MS,true))std::cout<<"steady marks counted as a pull forever\n";
    assert(!guard.measuring(2000+OpticalSleepGuard::MEASURE_LIMIT_MS,true));
    // A new Launch after any other phase is a pull again.
    assert(!guard.measuring(40000,false) && guard.measuring(40001,true));
    // Two refusals in a row, then the next sleep is forced; use starts over.
    assert(!guard.force());guard.refused();assert(!guard.force());guard.refused();
    assert(guard.force() && guard.refusals()==OpticalSleepGuard::FORCE_AFTER);
    guard.refused();assert(guard.refusals()==OpticalSleepGuard::FORCE_AFTER);
    guard.clear();assert(!guard.force() && !guard.refusals());
    // Across the millisecond counter's wrap.
    assert(!guard.measuring(0,false) && guard.measuring(UINT32_MAX-1000,true) && guard.measuring(1000,true) &&
           !guard.measuring(OpticalSleepGuard::MEASURE_LIMIT_MS,true));
  }
  // A button held down (power_status.h HeldButtonUse): use for 30 s, then
  // not until it is released.
  {
    HeldButtonUse held;
    assert(!held.use(1000,false,false) && held.use(1000,false,true));
    assert(held.use(2000,true,false) && held.use(2000+HeldButtonUse::LIMIT_MS-1,true,false));
    if(held.use(2000+HeldButtonUse::LIMIT_MS,true,false))std::cout<<"a button pressed in a pocket kept the unit awake\n";
    assert(!held.use(2000+HeldButtonUse::LIMIT_MS,true,false) && !held.use(600000,true,false));
    assert(held.use(600010,false,true) && !held.use(600020,false,false));
    assert(held.use(700000,true,false));  // a new press is use again
    // Across the millisecond counter's wrap.
    assert(!held.use(0,false,false) && held.use(UINT32_MAX-1000,true,false) && held.use(1000,true,false) &&
           !held.use(HeldButtonUse::LIMIT_MS,true,false));
  }
  // A connected phone (power_status.h PhoneLinkAwake, 0.20.9): it holds the
  // sleep off for 10 minutes after the last button or pull on the stick, then
  // no longer, however long it stays connected. The prototype had no limit.
  {
    using P=PhoneLinkAwake;
    assert(P::LIMIT_MS==600000);
    assert(!P::holds(false,0) && !P::holds(false,P::LIMIT_MS));  // no phone: the timer alone
    assert(P::holds(true,0) && P::holds(true,P::LIMIT_MS-1));
    if(P::holds(true,P::LIMIT_MS))std::cout<<"a connected phone kept the unit awake\n";
    assert(!P::holds(true,P::LIMIT_MS) && !P::holds(true,UINT32_MAX));
    // What the phone is told: the longer of the timer and the hold, then the timer.
    assert(P::remaining(false,1000,59000)==59000 && P::remaining(true,1000,59000)==P::LIMIT_MS-1000);
    assert(P::remaining(true,P::LIMIT_MS-1,0)==1 && P::remaining(true,P::LIMIT_MS,0)==0 && P::remaining(true,P::LIMIT_MS,5000)==5000);
    // With the sleep itself: a one-minute timer and a phone that never leaves.
    InactivityTimer timer;timer.begin(0);timer.touch(0);
    uint32_t slept=0;
    for(uint32_t now=0;now<=3600000 && !slept;now+=1000)
      if(timer.due(now) && !P::holds(true,timer.sinceUse(now)))slept=now;
    assert(slept==P::LIMIT_MS);
  }
  std::cout<<"Motion activity: vibration, adjustment, translation, stale/invalid data rolling sleep windows, the movement limit, the optical sleep guard, held buttons and a connected phone passed\n";
}
