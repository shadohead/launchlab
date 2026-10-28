#pragma once
#include <cstdint>

// StickS3 side power key. By default M5PM1 resets the ESP32 on a single
// click (BTN_CFG_1 0x49 bit0 SINGLE_RST_DIS=0): the screen went dark and the
// app restarted about a second later. The firmware takes the click instead,
// saves and powers off. PM1 GPIO1 is its IRQ output, wired to ESP32 G13
// (StickS3 V0.6 schematic PYG1_IRQ, U20 pin 18) and driven low on an unmasked
// event. Double-click off and long-press download mode remain PM1 hardware
// functions, so an unresponsive app can still be switched off and on.
// Callers on device must own the shared IMU/I2C gate (or run before its task).
template<class Registers> class PowerKey {
public:
  static constexpr uint8_t GPIO_MODE=0x10,GPIO_DRIVE=0x13,GPIO_FUNC0=0x16,BUTTON_IRQ=0x42,
    GPIO_IRQ_MASK=0x43,SYSTEM_IRQ_MASK=0x44,BUTTON_IRQ_MASK=0x45,BUTTON_CONFIG=0x49;
  static constexpr uint8_t CLICK=0x01,BUTTON_EVENTS=0x07,SINGLE_RESET_DISABLE=0x01;
  explicit PowerKey(Registers &registers):registers_(registers){}
  bool configured()const{return configured_;}
  // Route only the click to the IRQ line, then disable the hardware reset
  // last: any failure leaves (or restores) PM1's own single-click reset.
  bool configure() {
    configured_=registers_.masked(GPIO_MODE,0x02,0x02)     // GPIO1 output,
      && registers_.masked(GPIO_DRIVE,0x02,0)               // push-pull,
      && registers_.masked(GPIO_FUNC0,0x0C,0x04)            // IRQ function.
      && registers_.masked(GPIO_IRQ_MASK,0x1F,0x1F)         // Bit set masks.
      && registers_.masked(SYSTEM_IRQ_MASK,0x3F,0x3F)
      && registers_.masked(BUTTON_IRQ_MASK,BUTTON_EVENTS,BUTTON_EVENTS&~CLICK)
      && clear()
      && registers_.masked(BUTTON_CONFIG,SINGLE_RESET_DISABLE,SINGLE_RESET_DISABLE);
    if(!configured_)registers_.masked(BUTTON_CONFIG,SINGLE_RESET_DISABLE,0);
    return configured_;
  }
  // Reads and clears pending button events; true when a single click was
  // pending. Only the observed bits are cleared (write zero to clear), so an
  // event arriving between read and write keeps the IRQ line asserted.
  bool takeClick() {
    uint8_t pending=0;
    if(!registers_.read(BUTTON_IRQ,&pending,1))return false;
    pending&=BUTTON_EVENTS;
    if(pending && !registers_.write(BUTTON_IRQ,uint8_t(~pending&BUTTON_EVENTS)))++clearFailures_;
    return pending&CLICK;
  }
  uint32_t clearFailures()const{return clearFailures_;}
private:
  Registers &registers_;
  bool configured_=false;
  uint32_t clearFailures_=0;
  bool clear() {
    uint8_t pending=0;
    return registers_.read(BUTTON_IRQ,&pending,1) &&
      (!(pending&BUTTON_EVENTS) || registers_.write(BUTTON_IRQ,uint8_t(~pending&BUTTON_EVENTS)));
  }
};

#ifndef POWER_KEY_NATIVE
#include <Arduino.h>
#include "pm1_idle.h"
static constexpr uint8_t POWER_KEY_IRQ_PIN=13;
using StickS3PowerKey=PowerKey<PM1Idle<StickS3PM1Transport>>;
inline StickS3PowerKey &powerKey() {
  static StickS3PowerKey key(pm1Idle());
  return key;
}
#endif
