// UsbTx::drain() against a model of the core's TX ring: output is pushed out
// or left queued, never discarded (HWCDC::flush() discarded the whole ring
// after a 10 ms host stall and lost a HIL reply).
#include "../LaunchLabMini/usb_tx.h"
#include <cassert>
#include <iostream>
struct Ring {
  int capacity=8192,queued=0;uint32_t ms=0;
  // The host reads `rate` bytes per ms once `stallUntil` has passed. While the
  // core believes the host is gone (`gone`), or a short reply is `stuck` in
  // the ring with the refill interrupt idle, nothing moves until restart().
  int rate=64;uint32_t stallUntil=0;bool gone=false,stuck=false;unsigned restarts=0;
  int availableForWrite(){return capacity-queued;}
  void restart(){++restarts;gone=false;stuck=false;}
  uint32_t now(){return ms;}
  void wait(){++ms;if(!gone && !stuck && ms>=stallUntil)queued=std::max(0,queued-rate);}
};
// The core's HWCDC::flush(): waits while the host drains; after `timeout` ms
// without progress it marks the host gone and discards the whole ring, and
// with the host already marked gone it discards at once.
static void coreFlush(Ring &r,uint32_t timeout) {
  if(r.gone){r.queued=0;return;}
  uint32_t tries=timeout;
  while(r.queued && tries){const int before=r.queued;r.wait();if(r.queued==before)--tries;}
  if(!tries){r.gone=true;r.queued=0;}
}
int main() {
  {
    // Root cause of the lost HIL `!reset` reply: a status block plus the reply
    // queued, a host stall past 10 ms, and flush() deletes all of it.
    Ring r;r.queued=4100;r.stallUntil=40;coreFlush(r,10);assert(r.queued==0 && r.gone && r.ms<40);
    Ring after;after.queued=14;after.gone=true;coreFlush(after,10);assert(after.queued==0);
  }
  {
    Ring r;r.queued=4000;assert(UsbTx::drain(r,r.capacity,100) && r.queued==0 && r.ms<=63);
  }
  {
    // A 40 ms host stall (beyond the 10 ms timeout that made flush() discard
    // everything): the reply waits and goes out.
    Ring r;r.queued=4100;r.stallUntil=40;assert(UsbTx::drain(r,r.capacity,200) && r.queued==0 && r.ms>=40);
  }
  {
    // The core had marked the host gone after an earlier timeout: drain
    // restarts the transmit instead of dropping the queue.
    Ring r;r.queued=14;r.gone=true;assert(UsbTx::drain(r,r.capacity,100) && r.queued==0 && r.restarts>=1);
  }
  {
    // A short reply stuck in the ring while the core believes the host is
    // connected (HIL_UP_READY held 3 s): every wait restarts the transmit.
    Ring r;r.queued=30;r.stuck=true;assert(UsbTx::drain(r,r.capacity,100) && r.queued==0 && r.ms<=2);
  }
  {
    // No host reading at all: bounded wait, nothing discarded.
    Ring r;r.queued=500;r.stallUntil=100000;
    assert(!UsbTx::drain(r,r.capacity,50) && r.queued==500 && r.ms==50);
  }
  {
    // Nothing queued: returns at once.
    Ring r;assert(UsbTx::drain(r,r.capacity,50) && r.ms==0);
  }
  std::cout<<"PASS: USB TX drain pushes queued output out through host stalls and a core marked disconnected, bounded, never discarding\n";
}
