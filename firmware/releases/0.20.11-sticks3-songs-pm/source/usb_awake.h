#pragma once
#include <cstdint>

// USB SOF identifies a computer without requiring an open CDC reader. Retain
// that observed host across bus suspend only while checked PM1 VIN is present.
// A charger at boot has no SOF and cannot establish remembered host identity.
class UsbHostAwake {
public:
  static constexpr uint32_t HOST_POLL_MS=250,POWER_POLL_MS=1000;
  static constexpr uint32_t POWER_STALE_MS=2000,UNKNOWN_GRACE_MS=2000;
  static constexpr uint8_t VIN_MASK=1; // PM1 PWR_SRC 0x04 bit0, not charging.
  bool hostPollDue(uint32_t now)const{return !hostPolled_ || now-lastHostPoll_>=HOST_POLL_MS;}
  bool powerPollDue(uint32_t now)const {
    return (sof_ || remembered_) && (!powerPolled_ || now-lastPowerPoll_>=POWER_POLL_MS);
  }
  void sampleHost(uint32_t now,bool sof) {
    hostPolled_=true;lastHostPoll_=now;sof_=sof;
    if(sof) {
      lastSof_=now;
      // IDF initializes its connection monitor optimistically. Two positive
      // observations separated by 250ms reject that few-tick boot transient.
      if(!confirming_){confirming_=true;confirmAt_=now;}
      else if(now-confirmAt_>=HOST_POLL_MS)remembered_=true;
    } else confirming_=false;
  }
  // An attempt, including a mutex miss, consumes the one-second poll budget.
  // A read failure never masquerades as VIN absent. A remembered host survives
  // unknown status so a later successful read can recover after USB suspend.
  void samplePower(uint32_t now,bool ok,uint8_t bits) {
    powerPolled_=true;lastPowerPoll_=now;lastPowerReadOk_=ok;
    if(ok){powerKnown_=true;powerBits_=bits;lastGoodPower_=now;
      if(!(bits&VIN_MASK) && !sof_)remembered_=false;
    }
  }
  bool inhibited(uint32_t now)const {
    if(sof_)return true;
    if(!remembered_)return false;
    if(powerKnown_ && now-lastGoodPower_<=POWER_STALE_MS)return powerBits_&VIN_MASK;
    // Bound uncertain keep-awake on battery after a power-bus failure.
    return now-lastSof_<UNKNOWN_GRACE_MS;
  }
  bool sof()const{return sof_;}
  bool remembered()const{return remembered_;}
  bool powerKnown(uint32_t now)const{return powerKnown_ && now-lastGoodPower_<=POWER_STALE_MS;}
  bool lastPowerReadOk()const{return lastPowerReadOk_;}
  uint8_t powerBits()const{return powerBits_;}
  const char *reason(uint32_t now)const {
    if(sof_)return "usb_host";
    if(inhibited(now))return powerKnown(now)?"host_seen_vin_present":"host_power_unknown_grace";
    return remembered_?"host_power_unknown_timeout":"battery_or_charger";
  }
private:
  bool hostPolled_=false,powerPolled_=false,sof_=false,remembered_=false;
  bool confirming_=false,powerKnown_=false,lastPowerReadOk_=false;
  uint8_t powerBits_=0;
  uint32_t lastHostPoll_=0,lastPowerPoll_=0,lastSof_=0,confirmAt_=0,lastGoodPower_=0;
};
