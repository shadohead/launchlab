#pragma once
#include <algorithm>
#include <cstdint>

class InactivityTimer {
public:
  // A unit with no saved choice (1 minute from 0.20.2, 3 before; a saved
  // choice, the old default included, is kept).
  static constexpr uint8_t DEFAULT_MINUTES=1;
  // Shake to wake (shake_wake_setting.h) for a unit with no saved choice: On.
  static constexpr bool SHAKE_WAKE_DEFAULT=true;
  static constexpr uint32_t UNUSED_SHAKE_MS=30000;
  static bool validMinutes(uint8_t minutes) {
    return minutes>=1 && minutes<=10;
  }
  static uint8_t nextMinutes(uint8_t minutes) {
    return validMinutes(minutes) && minutes<10 ? minutes+1 : 1;
  }
  bool setMinutes(uint8_t minutes) {
    if(!validMinutes(minutes))return false;
    minutes_=minutes;return true;
  }
  uint8_t minutes()const{return minutes_;}
  uint32_t timeout()const{return uint32_t(minutes_)*60*1000;}
  // A shake can be transport/handling rather than an intended practice session.
  // Buttons, an accepted pull or an interactive USB command establish use.
  void begin(uint32_t now,bool shakeWake=false){lastActivity_=now;unusedShake_=shakeWake;}
  void touch(uint32_t now){lastActivity_=now;unusedShake_=false;}
  // Sustained movement keeps the current window open without promoting an
  // unused shake wake to the full selected practice-session timeout.
  void motion(uint32_t now){lastActivity_=now;}
  bool unusedShake()const{return unusedShake_;}
  uint32_t activeTimeout()const{return unusedShake_?std::min(timeout(),UNUSED_SHAKE_MS):timeout();}
  uint32_t elapsed(uint32_t now)const{return now-lastActivity_;}
  uint32_t remaining(uint32_t now)const {
    const uint32_t idle=elapsed(now),limit=activeTimeout();return idle>=limit?0:limit-idle;
  }
  bool due(uint32_t now,bool measuring=false)const{return !measuring && elapsed(now)>=activeTimeout();}
private:
  uint32_t lastActivity_=0;
  uint8_t minutes_=DEFAULT_MINUTES;
  bool unusedShake_=false;
};

// How an inactivity sleep wakes. On: the IMU's motion interrupt is armed and
// its supply held (PM1 GPIO4 wake), and the side key wakes too. Off: the
// side key's power-off state, a deeper sleep with the IMU unpowered on
// battery; only the side key wakes (on USB power through its IRQ on G13).
struct SleepPlan {bool armImu;const char *wake,*mode;};
constexpr SleepPlan sleepPlan(bool shakeToWake) {
  return shakeToWake?SleepPlan{true,"imu_motion","imu_only"}:SleepPlan{false,"side_key","power_off"};
}

namespace IdlePowerPolicy {
static constexpr uint32_t DIM_MS=30000,LEVEL_POLL_MS=20,UI_POLL_MS=5;
static constexpr uint8_t DIM_PERCENT=10;
inline uint8_t brightness(uint8_t selected,uint32_t idle,bool host,bool feedback) {
  return !host && !feedback && idle>=DIM_MS?std::min(selected,DIM_PERCENT):selected;
}
}

struct StickS3Battery {
  static constexpr uint32_t STALE_MS=15000;
  uint16_t millivolts=0;
  int percent=-1;
  bool voltageOk=false,chargeKnown=false,charging=false;
  uint16_t estimateMillivolts=0;
  uint32_t sampledAt=0;
  void update(uint16_t mv,bool voltageRead,bool chargeRead,uint8_t gpioBits,uint32_t now) {
    const bool fresh=valid(now);
    const bool wasChargeKnown=chargeKnown,wasCharging=charging;
    sampledAt=now;millivolts=mv;
    voltageOk=voltageRead && mv>=2500 && mv<=4350;
    chargeKnown=chargeRead;charging=chargeRead && !(gpioBits&1); // PM1 G0 active low.
    if(!voltageOk){percent=-1;return;}
    // Reject a brief sag in the percentage, while always showing raw voltage.
    // Do not mix charging and battery-only samples or hide critically low voltage.
    // This remains an approximate voltage estimate, not a calibrated fuel gauge.
    if(!fresh || (chargeKnown && wasChargeKnown && charging!=wasCharging))count_=next_=0;
    samples_[next_]=mv;next_=(next_+1)%5;count_=std::min(5u,count_+1);
    uint16_t sorted[5];std::copy(samples_,samples_+count_,sorted);std::sort(sorted,sorted+count_);
    estimateMillivolts=mv<=3300?mv:sorted[count_/2];
    percent=std::max(0,std::min(100,(int(estimateMillivolts)-3300)*100/800));
  }
  bool valid(uint32_t now)const{return voltageOk && now-sampledAt<=STALE_MS;}
  const char *chargeName()const{return !chargeKnown?"unknown":charging?"charging":"not_charging";}
private:
  uint16_t samples_[5]={};unsigned count_=0,next_=0;
};
