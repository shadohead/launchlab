#include "i2c_bus_recovery.h"
#include <cassert>
#include <iostream>
using namespace I2cBusRecovery;

// Open-drain bus with one slave that releases SDA after `hold` more SCL rising
// edges (it is mid-byte), or never when hold<0. Records STOP conditions.
struct SimBus {
  int hold;bool scl=true,masterSdaLow=false,slaveSdaLow;unsigned rises=0,stops=0,clocksWhileIdle=0;
  explicit SimBus(int h):hold(h),slaveSdaLow(h!=0){}
  bool sdaHigh()const{return !masterSdaLow && !slaveSdaLow;}
  void sclHigh(bool high){
    if(high && !scl){++rises;if(!slaveSdaLow)++clocksWhileIdle;
      if(slaveSdaLow && hold>0 && --hold==0)slaveSdaLow=false;}
    scl=high;
  }
  void sdaLow(bool low){const bool was=sdaHigh();masterSdaLow=low;if(scl && !was && sdaHigh())++stops;}
  void halfPeriod(){}
};
int main(){
  unsigned pulses=99;
  SimBus idle(0);assert(recover(idle,pulses)==Result::Idle && pulses==0 && idle.rises==0 && idle.stops==0);
  std::cout<<"idle bus: no clocks, no STOP\n";
  for(int hold=1;hold<=9;++hold){
    SimBus bus(hold);assert(recover(bus,pulses)==Result::Recovered);
    assert(pulses==unsigned(hold) && bus.stops==1 && bus.sdaHigh() && bus.scl);
    assert(bus.clocksWhileIdle==1); // only the STOP's own SCL rise after release
  }
  std::cout<<"slave mid-byte: released after exactly the needed pulses (1-9), then one STOP\n";
  SimBus stuck(-1);assert(recover(stuck,pulses)==Result::Stuck && pulses==MAX_PULSES && stuck.stops==0);
  SimBus late(10);assert(recover(late,pulses)==Result::Stuck && pulses==MAX_PULSES);
  std::cout<<"PASS: I2C bus recovery: idle untouched, 1-9 pulse release plus STOP, permanent hold reported stuck after 9\n";
}
