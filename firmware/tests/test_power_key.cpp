#define PM1_IDLE_NATIVE
#define POWER_KEY_NATIVE
#include "pm1_idle.h"
#include "power_key.h"
#include <array>
#include <cassert>
#include <iostream>
#include <vector>

// Register-level fake of M5PM1: button IRQ status is write-zero-to-clear.
struct PM1Bus {
  std::array<uint8_t,256> regs{};
  bool asleep=false;int failWriteTo=-1,failReadOf=-1;
  std::vector<uint8_t> writes;
  bool read(uint8_t reg,uint8_t *data,size_t size) {
    if(asleep){asleep=false;return false;}
    if(reg==failReadOf)return false;
    for(size_t i=0;i<size;++i)data[i]=regs[reg+i];
    return true;
  }
  bool write(uint8_t reg,uint8_t value) {
    writes.push_back(reg);
    if(reg==failWriteTo)return false;
    if(reg==0x42)regs[reg]&=value;else regs[reg]=value;
    return true;
  }
  void pause(){}
};
using Key=PowerKey<PM1Idle<PM1Bus>>;

int main() {
  {
    // Board defaults plus unrelated bits that must survive: GPIO3 speaker
    // output, GPIO4 IMU input, DL_LOCK/delays in BTN_CFG_1, a stale double click.
    PM1Bus bus;bus.regs[0x10]=0x08;bus.regs[0x13]=0x02;bus.regs[0x16]=0x40;
    bus.regs[0x45]=0x00;bus.regs[0x49]=0xA6;bus.regs[0x42]=0x04;
    PM1Idle<PM1Bus> idle(bus);Key key(idle);
    assert(key.configure() && key.configured());
    assert(bus.regs[0x10]==0x0A);           // GPIO1 output, GPIO3 kept.
    assert(bus.regs[0x13]==0x00);           // GPIO1 push-pull.
    assert(bus.regs[0x16]==0x44);           // GPIO1 IRQ, GPIO3 bits kept.
    assert(bus.regs[0x43]==0x1F && bus.regs[0x44]==0x3F);
    assert(bus.regs[0x45]==0x06);           // Only the single click reaches G13.
    assert(bus.regs[0x42]==0x00);           // Stale events cannot hold the line.
    assert(bus.regs[0x49]==0xA7);           // Reset disabled; other fields kept.
    assert(bus.writes.back()==0x49);        // Disabled only after routing works.

    bus.regs[0x42]=0x01;assert(key.takeClick() && bus.regs[0x42]==0);
    bus.regs[0x42]=0x04;assert(!key.takeClick() && bus.regs[0x42]==0); // Double click is PM1's.
    bus.regs[0x42]=0x02;assert(!key.takeClick() && bus.regs[0x42]==0);
    bus.regs[0x42]=0x00;bus.writes.clear();assert(!key.takeClick() && bus.writes.empty());
    // PM1 idle sleep: the first transaction only wakes it.
    bus.regs[0x42]=0x01;bus.asleep=true;assert(key.takeClick() && bus.regs[0x42]==0);
    // A failed clear still reports the click; the asserted line retries it.
    bus.regs[0x42]=0x01;bus.failWriteTo=0x42;assert(key.takeClick() && key.clearFailures()==1);
    bus.failWriteTo=-1;
    bus.failReadOf=0x42;bus.regs[0x42]=0x01;assert(!key.takeClick() && bus.regs[0x42]==0x01);
  }
  {
    // Routing failure keeps PM1's own single-click reset, even when an
    // earlier boot left it disabled.
    PM1Bus bus;bus.regs[0x49]=0x01;bus.failWriteTo=0x45;
    PM1Idle<PM1Bus> idle(bus);Key key(idle);
    assert(!key.configure() && !key.configured());
    assert(bus.regs[0x49]==0x00);
  }
  {
    // An unreadable routing register leaves the hardware reset enabled.
    PM1Bus bus;bus.failReadOf=0x10;PM1Idle<PM1Bus> idle(bus);Key key(idle);
    assert(!key.configure() && !key.configured() && bus.regs[0x49]==0x00);
  }
  std::cout<<"PASS: PM1 click routed to G13 before single-click reset is disabled; failures keep hardware reset; preserved neighbouring bits; selective IRQ clear\n";
}
