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
  assert(timer.unusedShake() && timer.activeTimeout()==15000 && timer.timeout()==180000);
  assert(!timer.due(4998) && timer.due(4999) && !timer.due(4999,true));
  timer.touch(4999);assert(!timer.unusedShake() && timer.remaining(4999)==180000);
  timer.begin(100,true);timer.setMinutes(10);assert(timer.activeTimeout()==15000);
  timer.begin(100,false);assert(timer.activeTimeout()==600000);
  assert(IdlePowerPolicy::brightness(40,29999,false,false)==40);
  assert(IdlePowerPolicy::brightness(40,30000,false,false)==10);
  assert(IdlePowerPolicy::brightness(5,30000,false,false)==5);
  assert(IdlePowerPolicy::brightness(40,30000,true,false)==40);
  assert(IdlePowerPolicy::brightness(40,30000,false,true)==40);
  // The battery savers' dim delay (power_savers.h).
  assert(IdlePowerPolicy::brightness(40,9999,false,false,10000)==40 && IdlePowerPolicy::brightness(40,10000,false,false,10000)==10);
  // Movement keeps the inactivity window open but is not use: sinceUse()
  // counts only begin() and touch(), for a dim that ignores motion.
  timer.begin(1000);timer.motion(20000);
  assert(timer.elapsed(25000)==5000 && timer.sinceUse(25000)==24000);
  timer.touch(30000);timer.motion(40000);assert(timer.sinceUse(45000)==15000 && timer.elapsed(45000)==5000);
  timer.begin(UINT32_MAX-1000);assert(timer.sinceUse(1000)==2001);

  StickS3Battery b;
  assert(!b.valid(0) && b.percent==-1);
  // On the charger the reading is 150 mV above the cell: 3,700 mV is 31%, not 50%.
  b.update(3700,true,true,0,100);assert(b.valid(100) && b.percent==31 && b.charging);
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
  smooth.update(4100,true,true,0,45000);assert(smooth.percent==81); // charger change resets history
  smooth.update(3900,true,true,1,50000);assert(smooth.percent==75);
  smooth.update(3500,true,true,1,66000);assert(smooth.percent==25); // stale samples reset
  smooth.update(0,false,false,0,67000);assert(smooth.percent==-1);
  smooth.update(3700,true,true,1,68000);assert(smooth.percent==50); // error recovery resets
  {  // The percentage while charging follows the charge put in, not the charger's voltage.
    // A unit charged from flat on 2026-11-12: the reading and
    // the share of the charge delivered by then (constant current for 130
    // minutes, then the taper).
    static constexpr struct {uint16_t mv;int charged;} measured[]={
      {3630,15},{3684,22},{3702,26},{3748,33},{3812,40},{3894,48},{3946,55},
      {4008,65},{4060,73},{4092,76},{4127,84},{4174,91}};
    for(const auto &m:measured) {
      const int shown=StickS3Battery::percentFor(m.mv,true);
      assert(shown-m.charged<=8 && m.charged-shown<=8);
    }
    // The reading as it was (through 0.20.9) showed half at a quarter and full at 84%.
    assert(StickS3Battery::percentFor(3702,false)-26>=20 && StickS3Battery::percentFor(4127,false)==100);
    // Full only once the charger has finished: the charger holds 4,200 mV for the taper.
    assert(StickS3Battery::percentFor(4200,true)<100 && StickS3Battery::percentFor(4190,false)==100);
    // On battery nothing changes, and a flat cell on the charger reads 0.
    assert(StickS3Battery::percentFor(3700,false)==50 && StickS3Battery::percentFor(3338,true)==0);
    StickS3Battery unplug;
    for(unsigned i=0;i<5;++i)unplug.update(3900,true,true,0,i*5000);
    assert(unplug.percent==56);
    unplug.update(3760,true,true,1,25000);assert(unplug.percent==57 && !unplug.charging); // off the charger: no jump
  }
  {  // A boot out of a USB-retained standby.
    const UsbStandby none{},shake{UsbStandby::MAGIC,false},key{UsbStandby::MAGIC,true};
    const uint8_t removed=PM1_VIN_REMOVED_IRQ;
    auto w=usbStandbyWake(none,true,false,true,removed,true);assert(!w.finish && !w.keyWake);  // no receipt: an ordinary wake
    w=usbStandbyWake(shake,true,false,true,removed,true);assert(w.finish && !w.keyWake);       // unplugged
    w=usbStandbyWake(key,true,false,true,removed|0x01,true);assert(w.finish && !w.keyWake);
    w=usbStandbyWake(shake,true,false,true,removed,false);assert(!w.finish && !w.keyWake);     // replugged, or PM1's own event: 5 V is there
    w=usbStandbyWake(key,true,false,true,removed,false);assert(!w.finish && w.keyWake);
    w=usbStandbyWake(shake,true,false,true,0,true);assert(!w.finish && !w.keyWake);            // a shake
    w=usbStandbyWake(key,true,false,true,0,false);assert(!w.finish && w.keyWake);              // the side key on USB
    w=usbStandbyWake(key,true,false,false,removed,true);assert(!w.finish && w.keyWake);        // IRQ unreadable: not proof of unplug
    w=usbStandbyWake(shake,false,true,false,0,true);assert(w.finish && !w.keyWake);            // the timer found no 5 V
    w=usbStandbyWake(shake,false,true,false,0,false);assert(!w.finish && !w.keyWake);          // ... but 5 V is there now
    w=usbStandbyWake(key,false,false,true,removed,true);assert(!w.finish && !w.keyWake);       // not an ext0 wake
  }
  std::cout<<"PASS: USB standby wake decisions, 1-minute default, all 1–10 minute timeouts, invalid-value rejection, activity reset, active-burst guard, millis wrap, checked battery voltage, estimate limits, stale/error handling and charging GPIO\n";
}
