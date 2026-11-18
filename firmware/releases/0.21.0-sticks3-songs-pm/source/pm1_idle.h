#pragma once
#include <cstddef>
#include <cstdint>

// M5PM1 datasheet, I2C_CFG (0x09), sections 6/7: idle sleep keeps the
// existing power rails; its first I2C transaction only wakes the controller
// and fails. A matching second address must arrive within 300 ms. Retry
// reads after a 1 ms yield, never retry shutdown or other writes. All callers
// on device must already own the shared IMU/I2C gate (or run before its task).
template<class Transport> class PM1Idle {
public:
  static constexpr uint8_t CONFIG=0x09,DEFAULT_SECONDS=2;
  struct Stats {
    uint32_t readFailures=0,writeFailures=0,wakeRetries=0,recoveredReads=0;
    uint32_t operationFailures=0,configFailures=0,fallbacks=0;
    uint8_t seconds=0;
    bool configured=false,fallbackVerified=false;
  };
  explicit PM1Idle(Transport &transport):transport_(transport){}
  const Stats &stats()const{return stats_;}
  bool read(uint8_t reg,uint8_t *data,size_t length) {
    if(transport_.read(reg,data,length))return true;
    ++stats_.readFailures;++stats_.wakeRetries;
    transport_.pause();
    if(transport_.read(reg,data,length)){++stats_.recoveredReads;return true;}
    ++stats_.readFailures;return false;
  }
  bool ensureAwake(){uint8_t config=0;return read(CONFIG,&config,1);}
  // Guard high-level library calls whose internal register accesses do not
  // handle idle wake. The callback runs once only, and never after wake fails.
  template<class Operation> bool run(Operation operation) {
    if(!ensureAwake()){++stats_.operationFailures;return false;}
    if(operation())return true;
    ++stats_.operationFailures;return false;
  }
  bool write(uint8_t reg,uint8_t value) {
    return run([&](){return writeOnce(reg,value);});
  }
  bool masked(uint8_t reg,uint8_t mask,uint8_t value) {
    uint8_t before=0,after=0;
    if(!read(reg,&before,1))return false;
    const uint8_t wanted=uint8_t((before&~mask)|(value&mask));
    if(!writeOnce(reg,wanted))return false;
    return read(reg,&after,1) && after==wanted;
  }
  // A failure leaves this feature disabled if communication permits. Never
  // invent SPD/reserved bits after a failed read; retain the last observed
  // upper nibble only for a fallback following an acknowledged enable write.
  bool configure(uint8_t seconds=DEFAULT_SECONDS) {
    stats_.configured=false;stats_.fallbackVerified=false;
    uint8_t before=0,after=0;
    const bool known=read(CONFIG,&before,1);
    if(known && seconds<=15) {
      const uint8_t wanted=uint8_t((before&0xF0)|seconds);
      if(writeOnce(CONFIG,wanted) && read(CONFIG,&after,1) && after==wanted) {
        stats_.seconds=seconds;stats_.configured=true;return true;
      }
    }
    ++stats_.configFailures;++stats_.fallbacks;
    // Re-read if possible: SPD may have changed independently. A first wake
    // failed read is bounded; fallback never becomes a repeated retry loop.
    uint8_t current=0;
    const bool fresh=read(CONFIG,&current,1);
    if(fresh || known) {
      const uint8_t disabled=uint8_t((fresh?current:before)&0xF0);
      if(writeOnce(CONFIG,disabled) && read(CONFIG,&after,1) && after==disabled) {
        stats_.seconds=0;stats_.fallbackVerified=true;
      }
    }
    return false;
  }
private:
  Transport &transport_;Stats stats_;
  bool writeOnce(uint8_t reg,uint8_t value) {
    if(transport_.write(reg,value))return true;
    ++stats_.writeFailures;return false;
  }
};

#ifndef PM1_IDLE_NATIVE
#include <M5Unified.h>
struct StickS3PM1Transport {
  bool read(uint8_t reg,uint8_t *data,size_t length){return M5.Power.M5pm1.readRegister(reg,data,length);}
  bool write(uint8_t reg,uint8_t value){return M5.Power.M5pm1.writeRegister8(reg,value);}
  void pause(){delay(1);}
};
inline PM1Idle<StickS3PM1Transport> &pm1Idle() {
  static StickS3PM1Transport transport;
  static PM1Idle<StickS3PM1Transport> idle(transport);
  return idle;
}
#endif
