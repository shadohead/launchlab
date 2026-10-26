#include "../LaunchLabMini/power_status.h"
#include <cassert>
#include <iostream>

int main() {
  InactivityTimer timer;timer.touch(1000);
  assert(timer.minutes()==1 && timer.timeout()==60000);
  assert(timer.remaining(1000)==60000 && !timer.due(60999));
  assert(timer.due(61000) && timer.remaining(61000)==0);
  assert(!timer.due(61000,true)); // active measured burst is protected
  for(uint8_t minutes=1;minutes<=10;++minutes) {
    assert(timer.setMinutes(minutes));timer.touch(1000);
    assert(!timer.due(1000+timer.timeout()-1));assert(timer.due(1000+timer.timeout()));
    timer.touch(1000+timer.timeout());assert(timer.remaining(1000+timer.timeout())==timer.timeout());
    timer.touch(UINT32_MAX-50000);
    assert(!timer.due(uint32_t(UINT32_MAX-50000+timer.timeout()-1)));
    assert(timer.due(uint32_t(UINT32_MAX-50000+timer.timeout())));
  }
  for(uint8_t bad:{0,11,15,30,255})assert(!timer.setMinutes(bad) && timer.minutes()==10);
  uint8_t minutes=1;for(unsigned i=0;i<10;++i)minutes=InactivityTimer::nextMinutes(minutes);
  assert(minutes==1);

  timer.setMinutes(3);timer.begin(UINT32_MAX-10000,true);
  assert(timer.unusedShake() && timer.activeTimeout()==30000 && timer.timeout()==180000);
  assert(!timer.due(19998) && timer.due(19999) && !timer.due(19999,true));
  timer.touch(19999);assert(!timer.unusedShake() && timer.remaining(19999)==180000);
  timer.begin(100,true);timer.setMinutes(10);assert(timer.activeTimeout()==30000);
  timer.begin(100,false);assert(timer.activeTimeout()==600000);
  assert(IdlePowerPolicy::brightness(40,29999,false,false)==40);
  assert(IdlePowerPolicy::brightness(40,30000,false,false)==10);
  assert(IdlePowerPolicy::brightness(5,30000,false,false)==5);
  assert(IdlePowerPolicy::brightness(40,30000,true,false)==40);
  assert(IdlePowerPolicy::brightness(40,30000,false,true)==40);

  StickS3Battery b;
  assert(!b.valid(0) && b.percent==-1);
  b.update(3700,true,true,0,100);assert(b.valid(100) && b.percent==50 && b.charging);
  assert(b.valid(15100) && !b.valid(15101));
  b.update(4200,true,true,1,200);assert(b.percent==100 && !b.charging);
  b.update(3000,true,true,1,200);assert(b.percent==0);
  for(unsigned mv:{0u,2400u,4400u,65535u}) {
    b.update(mv,true,true,0,300);assert(!b.valid(300) && b.percent==-1);
  }
  b.update(3700,false,false,0,400);assert(!b.valid(400) && !b.chargeKnown && !b.charging);
  b.update(3900,true,false,0,500);assert(b.valid(500) && b.percent==75 && !b.chargeKnown);
  b.update(3900,true,true,1,UINT32_MAX-1000);assert(b.valid(1000));
  StickS3Battery smooth;
  for(unsigned i=0;i<5;++i)smooth.update(3900,true,true,1,i*5000);
  smooth.update(3700,true,true,1,25000);
  assert(smooth.millivolts==3700 && smooth.estimateMillivolts==3900 && smooth.percent==75);
  smooth.update(3700,true,true,1,30000);assert(smooth.percent==75);
  smooth.update(3700,true,true,1,35000);assert(smooth.percent==50);
  smooth.update(3250,true,true,1,40000);assert(smooth.percent==0 && smooth.estimateMillivolts==3250);
  smooth.update(4100,true,true,0,45000);assert(smooth.percent==100); // charger change resets history
  smooth.update(3900,true,true,1,50000);assert(smooth.percent==75);
  smooth.update(3500,true,true,1,66000);assert(smooth.percent==25); // stale samples reset
  smooth.update(0,false,false,0,67000);assert(smooth.percent==-1);
  smooth.update(3700,true,true,1,68000);assert(smooth.percent==50); // error recovery resets
  std::cout<<"PASS: 1-minute default, all 1–10 minute timeouts, invalid-value rejection, activity reset, active-burst guard, millis wrap, checked battery voltage, estimate limits, stale/error handling and charging GPIO\n";
}
