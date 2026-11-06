#pragma once
#include <stdint.h>

// Push queued USB serial output out without losing it.
//
// The core's HWCDC::flush() discards its whole TX ring when the host has not
// read for the TX timeout (10 ms here), or at once when the core has already
// marked the host gone. Flushing after a reply therefore deleted that reply
// and everything queued before it (a HIL `!reset` reply and a status block,
// 2026-10-17), with no short write to show for it. drain() waits for the ring
// to empty instead and gives up after `ms` without discarding anything.
// While waiting it must restart the transmit exactly as flush() does, every
// time: a short reply can otherwise sit in the ring until more output
// follows (a HIL_UP_READY held 3 s, 2026-10-17, when only a disconnected
// core was restarted).
namespace UsbTx {
// Port: int availableForWrite(), void restart(), uint32_t now(), void wait().
template<class Port> bool drain(Port &port,int emptyFree,uint32_t ms) {
  const uint32_t start=port.now();
  while(port.availableForWrite()<emptyFree) {
    if(uint32_t(port.now()-start)>=ms)return false;
    port.restart();port.wait();
  }
  return true;
}
}

#if defined(ARDUINO)
#include <Arduino.h>
#include <HWCDC.h>
#include <algorithm>
#include "hal/usb_serial_jtag_ll.h"
namespace UsbTx {
// Free space of the empty TX ring, measured right after Serial.begin().
inline int emptyFree=0;
inline uint32_t drainTimeouts=0;
inline void calibrate(){emptyFree=std::max(emptyFree,Serial.availableForWrite());}
struct SerialPort {
  int availableForWrite(){return Serial.availableForWrite();}
  // What HWCDC::flush() does while it waits, minus the discard: commit the
  // TX FIFO and re-arm the interrupt that refills it from the ring. A core
  // that believes the host is gone is re-armed by its own isConnected().
  void restart() {
    if(!HWCDC::isConnected())return;
    usb_serial_jtag_ll_txfifo_flush();usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
  }
  uint32_t now(){return millis();}
  void wait(){delay(1);}
};
inline bool drain(uint32_t ms) {
  SerialPort port;const bool ok=drain(port,emptyFree,ms);
  if(!ok)++drainTimeouts;
  return ok;
}
}
#endif
