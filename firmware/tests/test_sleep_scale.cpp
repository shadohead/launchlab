// The HIL sleep scale (power_status.h, sleepLimit()), built twice by
// check_sleep_scale.py: as production, where every limit is the constant and
// no divisor exists, and with LAUNCHLAB_HIL=1, where !sleepscale divides every
// limit and 1 gives the real ones.
#include "power_status.h"
#include <cassert>
#include <cstdint>
#include <iostream>

#if !LAUNCHLAB_HIL
// Production: sleepLimit() is the limit itself, at compile time.
static_assert(sleepLimit(InactivityTimer::MOTION_LIMIT_MS)==300000,"production movement limit");
static_assert(sleepLimit(InactivityTimer::UNUSED_SHAKE_LIMIT_MS)==120000,"production shake limit");
static_assert(sleepLimit(OpticalSleepGuard::MEASURE_LIMIT_MS)==30000,"production measured-pull limit");
static_assert(sleepLimit(HeldButtonUse::LIMIT_MS)==30000,"production held-button limit");
static_assert(sleepLimit(60000)==60000,"production retry after a deferred sleep");
// No divisor in production: a Hil::sleepScale compiled into it collides with this one.
namespace Hil {constexpr int sleepScale=0;}
#endif

// Every limit through the classes' own paths, divided by n.
static void limits(uint32_t n) {
  const uint32_t t0=1000000;
  InactivityTimer timer;  // Sleep after's 1-minute default
  timer.begin(t0);
  assert(timer.timeout()==60000/n && !timer.due(t0+60000/n-1) && timer.due(t0+60000/n));
  // Movement after use: awake up to the movement limit after the last use, never past it.
  timer.touch(t0);
  for(uint32_t t=t0;t<t0+300000/n;t+=100/n?100/n:1) {
    timer.motion(t);assert(!timer.due(t));
  }
  timer.motion(t0+300000/n);assert(timer.due(t0+300000/n));
  // A shake wake: 30 s, movement extends it to 2 minutes after the wake at most.
  timer.begin(t0,true);
  assert(timer.activeTimeout()==30000/n && !timer.due(t0+30000/n-1) && timer.due(t0+30000/n));
  timer.begin(t0,true);
  for(uint32_t t=t0;t<t0+120000/n;t+=100/n?100/n:1) {
    timer.motion(t);assert(!timer.due(t));
  }
  timer.motion(t0+120000/n);assert(timer.due(t0+120000/n));
  // A pull still being measured, then steady marks that are not one.
  OpticalSleepGuard guard;
  assert(guard.measuring(t0,true) && guard.measuring(t0+30000/n-1,true) && !guard.measuring(t0+30000/n,true));
  // A held button counts as use for 30 s at most.
  HeldButtonUse held;
  assert(held.use(t0,true,false) && held.use(t0+30000/n-1,true,false) && !held.use(t0+30000/n,true,false));
  assert(sleepLimit(60000)==60000/n);
}

int main() {
#if LAUNCHLAB_HIL
  assert(Hil::sleepScale.load()==1);  // from boot: the real limits
  limits(1);
  Hil::sleepScale=10;limits(10);
  Hil::sleepScale=60;limits(60);
  Hil::sleepScale=1;limits(1);
  std::cout<<"PASS: HIL build: the sleep limits are the real ones at scale 1 (from boot) and each divided by 10 and 60\n";
#else
  limits(1);
  std::cout<<"PASS: production: every sleep limit is the constant, no divisor exists\n";
#endif
  return 0;
}
