#pragma once
#include <cstdint>

// A chip reset (browser updater, esptool, watchdog) during an I2C read leaves
// the slave that was sending (the BMI270 during a long FIFO burst) holding SDA
// low mid-byte. A CPU reset does not power-cycle it, so PM1 and the IMU stay
// unreachable until the battery is disconnected; reproduced on device by
// resetting during a FIFO catch-up read. Before the bus is first used, clock
// SCL until the slave releases SDA (at most nine pulses finish any byte and
// its acknowledge slot), then issue a STOP (UM10204 section 3.1.16).
namespace I2cBusRecovery {
static constexpr unsigned MAX_PULSES=9;
enum class Result:uint8_t {Idle,Recovered,Stuck};
inline const char *name(Result r){return r==Result::Idle?"idle":r==Result::Recovered?"recovered":"stuck";}
// Bus provides sdaHigh(), sclHigh(bool), sdaLow(bool) (true drives SDA low,
// false releases it) and halfPeriod(). Lines are open-drain with pull-ups.
template<class Bus> Result recover(Bus &bus,unsigned &pulses) {
  pulses=0;bus.sdaLow(false);bus.sclHigh(true);bus.halfPeriod();
  if(bus.sdaHigh())return Result::Idle;
  while(pulses<MAX_PULSES && !bus.sdaHigh()) {
    bus.sclHigh(false);bus.halfPeriod();bus.sclHigh(true);bus.halfPeriod();++pulses;
  }
  if(!bus.sdaHigh())return Result::Stuck;
  // STOP: SDA rises while SCL is high, ending the interrupted transaction.
  bus.sclHigh(false);bus.halfPeriod();bus.sdaLow(true);bus.halfPeriod();
  bus.sclHigh(true);bus.halfPeriod();bus.sdaLow(false);bus.halfPeriod();
  return bus.sdaHigh()?Result::Recovered:Result::Stuck;
}
}

#ifdef ARDUINO
#include <Arduino.h>
#include "driver/gpio.h"
namespace I2cBusRecovery {
// StickS3 internal bus (PM1, BMI270): SCL GPIO48, SDA GPIO47. Run before
// M5.begin(); both pins are returned to plain inputs for its I2C driver.
// sdaWas/sclWas are the raw levels before any reconfiguration (diagnostics:
// a held SCL cannot be cleared by clocking and needs a power cycle).
inline Result recoverStickS3Internal(unsigned &pulses,bool &sdaWas,bool &sclWas) {
  static constexpr gpio_num_t SCL=GPIO_NUM_48,SDA=GPIO_NUM_47;
  struct Bus {
    bool sdaHigh(){return gpio_get_level(SDA)!=0;}
    void sclHigh(bool high){gpio_set_level(SCL,high?1:0);}
    void sdaLow(bool low){gpio_set_level(SDA,low?0:1);}
    void halfPeriod(){delayMicroseconds(5);}
  } bus;
  for(const auto pin:{SCL,SDA}){gpio_set_direction(pin,GPIO_MODE_INPUT);gpio_set_pull_mode(pin,GPIO_PULLUP_ONLY);}
  delayMicroseconds(10);sdaWas=gpio_get_level(SDA)!=0;sclWas=gpio_get_level(SCL)!=0;
  for(const auto pin:{SCL,SDA}) {
    gpio_reset_pin(pin);gpio_set_level(pin,1);
    gpio_set_direction(pin,GPIO_MODE_INPUT_OUTPUT_OD);gpio_set_pull_mode(pin,GPIO_PULLUP_ONLY);
  }
  const Result result=recover(bus,pulses);
  for(const auto pin:{SCL,SDA}){gpio_set_direction(pin,GPIO_MODE_INPUT);}
  return result;
}
}
#endif
