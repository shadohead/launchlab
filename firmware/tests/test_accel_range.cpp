#include "accel_range.h"
#include <cassert>
#include <cmath>
#include <iostream>

struct FakeBus {
  uint8_t reg=0x02|0x80; // Reset default 8 g plus reserved upper bits.
  bool readFails=false,writeFails=false,readbackFails=false,ignoresWrite=false;
  unsigned reads=0,writes=0;
  bool read(uint8_t address,uint8_t &value){
    assert(address==StickS3AccelRange::ACC_RANGE);++reads;
    if(readFails || (readbackFails && writes))return false;
    value=reg;return true;
  }
  bool write(uint8_t address,uint8_t value){
    assert(address==StickS3AccelRange::ACC_RANGE);++writes;
    if(writeFails)return false;
    if(!ignoresWrite)reg=value;return true;
  }
};
int main(){
  StickS3AccelRange range;assert(range.fullScaleG()==8);
  FakeBus bus;assert(range.configure(bus) && bus.writes==1 && bus.reads==2);
  assert(bus.reg==(0x80|0x03) && range.fullScaleG()==16);
  // M5Unified converts with 8 g/32768; a raw 12 g reads as 6 from M5.
  assert(range.toG(6)==12 && range.toG(-.5f)==-1);
  assert(!range.accelClipped(12) && !range.accelClipped(-15.5f));
  assert(range.accelClipped(15.6f) && range.accelClipped(-16) && range.accelClipped(NAN) && range.accelClipped(INFINITY));
  // Already 16 g: verified without another write.
  FakeBus ready;ready.reg=0x03;assert(range.configure(ready) && ready.writes==0 && range.fullScaleG()==16);
  std::cout<<"16 g programmed after reset, upper bits preserved, M5 values rescaled, clip at 97.5% of range\n";

  // A rejected write keeps an honest 8 g scale and threshold; never assumes 16 g.
  FakeBus rejected;rejected.writeFails=true;assert(range.configure(rejected) && range.fullScaleG()==8);
  assert(range.toG(6)==6 && !range.accelClipped(7.7f) && range.accelClipped(7.8f));
  FakeBus ignored;ignored.ignoresWrite=true;assert(range.configure(ignored) && range.fullScaleG()==8);
  FakeBus small;small.reg=0x01;small.ignoresWrite=true;assert(range.configure(small) && range.fullScaleG()==4 && range.toG(2)==1);
  // Unreadable range: report failure so startup retries; scale falls back to the reset default.
  range.configure(bus);assert(range.fullScaleG()==16);
  FakeBus unreadable;unreadable.readFails=true;assert(!range.configure(unreadable) && range.fullScaleG()==8);
  range.configure(bus);
  FakeBus noReadback;noReadback.readbackFails=true;assert(!range.configure(noReadback) && range.fullScaleG()==8);
  std::cout<<"failed write/readback never claims 16 g; unreadable range fails configuration\n";

  assert(!StickS3AccelRange::gyroClipped(1949) && !StickS3AccelRange::gyroClipped(-1949));
  assert(StickS3AccelRange::gyroClipped(1950) && StickS3AccelRange::gyroClipped(-2000) && StickS3AccelRange::gyroClipped(NAN));
  std::cout<<"PASS: BMI270 accel range programming, verification, scaling and clip limits\n";
}
