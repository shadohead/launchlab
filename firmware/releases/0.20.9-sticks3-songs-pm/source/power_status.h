#pragma once
#include <algorithm>
#include <cstdint>

// Every limit below that ends a keep-awake path goes through sleepLimit().
// In production it is the limit itself, a constant. A HIL build's
// !sleepscale <n> divides them all by n, so the rig's sleep_blockers waits
// seconds where a unit waits minutes; it is 1 from boot, and !reset and the
// end of every HIL scenario put it back. The host tests pin the real limits
// (test_motion_activity, check_keep_awake.py) and that production has no
// divisor (check_sleep_scale.py).
#if LAUNCHLAB_HIL
#include <atomic>
namespace Hil {inline std::atomic<uint16_t> sleepScale{1};}
inline uint32_t sleepLimit(uint32_t ms){return ms/Hil::sleepScale.load(std::memory_order_relaxed);}
#else
constexpr uint32_t sleepLimit(uint32_t ms){return ms;}
#endif

class InactivityTimer {
public:
  // A unit with no saved choice (1 minute from 0.20.2, 3 before; a saved
  // choice, the old default included, is kept).
  static constexpr uint8_t DEFAULT_MINUTES=1;
  // Shake to wake (shake_wake_setting.h) for a unit with no saved choice: On.
  static constexpr bool SHAKE_WAKE_DEFAULT=true;
  static constexpr uint32_t UNUSED_SHAKE_MS=30000;
  // Movement extends an unused shake wake, never past this since the wake.
  // Before 0.20.6 it had no limit: a unit shaken awake in a bag stayed awake,
  // screen lit, for as long as the bag kept moving.
  static constexpr uint32_t UNUSED_SHAKE_LIMIT_MS=120000;
  // Movement keeps a used unit awake, but never past this after its last
  // button, pull or command (or the selected timeout, if longer). Before
  // 0.20.6 it had no limit: a unit put in a pocket or bag after practice
  // stayed awake, screen lit, for as long as it kept moving.
  static constexpr uint32_t MOTION_LIMIT_MS=300000;
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
  uint32_t timeout()const{return sleepLimit(uint32_t(minutes_)*60*1000);}
  // A shake can be transport/handling rather than an intended practice session.
  // Buttons, an accepted pull or an interactive USB command establish use.
  void begin(uint32_t now,bool shakeWake=false){lastActivity_=lastUse_=wokeAt_=now;unusedShake_=shakeWake;}
  void touch(uint32_t now){lastActivity_=lastUse_=now;unusedShake_=false;}
  // Sustained movement keeps the current window open without promoting an
  // unused shake wake to the full selected practice-session timeout, and
  // only up to UNUSED_SHAKE_LIMIT_MS after that wake (MOTION_LIMIT_MS after
  // the last use otherwise).
  void motion(uint32_t now){lastActivity_=now;}
  bool unusedShake()const{return unusedShake_;}
  uint32_t activeTimeout()const{return unusedShake_?std::min(timeout(),sleepLimit(UNUSED_SHAKE_MS)):timeout();}
  uint32_t elapsed(uint32_t now)const{return now-lastActivity_;}
  // Since the last button, pull or command (touch), movement not counted.
  uint32_t sinceUse(uint32_t now)const{return now-lastUse_;}
  uint32_t remaining(uint32_t now)const {
    const uint32_t idle=elapsed(now),limit=activeTimeout();
    const uint32_t left=idle>=limit?0:limit-idle;
    const uint32_t since=unusedShake_?now-wokeAt_:sinceUse(now);
    const uint32_t cap=unusedShake_?sleepLimit(UNUSED_SHAKE_LIMIT_MS):std::max(timeout(),sleepLimit(MOTION_LIMIT_MS));
    return std::min(left,since>=cap?0:cap-since);
  }
  bool due(uint32_t now,bool measuring=false)const{return !measuring && !remaining(now);}
private:
  uint32_t lastActivity_=0,lastUse_=0,wokeAt_=0;
  uint8_t minutes_=DEFAULT_MINUTES;
  bool unusedShake_=false;
};

// Optical activity that is not a pull must not keep the unit awake. A pull's
// Launch phase lasts seconds: past MEASURE_LIMIT_MS it no longer counts as
// use or holds off the sleep. A sleep the detector refuses (strong marks in
// Ready, or such a Launch) retries a minute later, and after FORCE_AFTER
// refusals in a row stops sampling anyway, discarding the marks. Through
// 0.20.5 a steady signal, such as a light flickering into the sensor or a
// fan, kept the unit awake until flat.
class OpticalSleepGuard {
public:
  static constexpr uint32_t MEASURE_LIMIT_MS=30000;
  static constexpr uint8_t FORCE_AFTER=2;
  // Whether the detector's current phase is a pull still being measured.
  bool measuring(uint32_t now,bool launch) {
    if(!launch){inLaunch_=false;return false;}
    if(!inLaunch_){inLaunch_=true;launchSince_=now;}
    return now-launchSince_<sleepLimit(MEASURE_LIMIT_MS);
  }
  void refused(){if(refusals_<FORCE_AFTER)++refusals_;}
  // A sleep that went ahead, or use since: the count starts again.
  void clear(){refusals_=0;}
  bool force()const{return refusals_>=FORCE_AFTER;}
  uint8_t refusals()const{return refusals_;}
private:
  uint32_t launchSince_=0;bool inLaunch_=false;uint8_t refusals_=0;
};

// A button held down is use for LIMIT_MS at most (the longest control is a
// 3-second hold); its release is use again. Through 0.20.5 a button pressed
// in a pocket or bag kept the unit awake for as long as it stayed pressed.
class HeldButtonUse {
public:
  static constexpr uint32_t LIMIT_MS=30000;
  bool use(uint32_t now,bool held,bool released) {
    if(!held){holding_=false;return released;}
    if(!holding_){holding_=true;since_=now;}
    return released || now-since_<sleepLimit(LIMIT_MS);
  }
private:
  uint32_t since_=0;bool holding_=false;
};

// A connected phone (Bluetooth, 0.20.9) holds the sleep off, but only for
// LIMIT_MS after the last button or accepted pull on the stick. What the phone
// sends is not use: an app left open, or a phone left in range, would repeat
// it until the battery is flat. The prototype stayed awake for as long as a
// phone stayed connected. The sleep then ends the link.
struct PhoneLinkAwake {
  static constexpr uint32_t LIMIT_MS=600000;
  static bool holds(bool connected,uint32_t sinceUse){return connected && sinceUse<sleepLimit(LIMIT_MS);}
  // Until the sleep, given what the timer itself has left.
  static uint32_t remaining(bool connected,uint32_t sinceUse,uint32_t timerLeft) {
    return holds(connected,sinceUse)?std::max(timerLeft,sleepLimit(LIMIT_MS)-sinceUse):timerLeft;
  }
};

// How an inactivity sleep wakes. On: the IMU's motion interrupt is armed and
// its supply held (PM1 GPIO4 wake), and the side key wakes too. Off: the
// side key's power-off state, a deeper sleep with the IMU unpowered on
// battery; only the side key wakes (on USB power through its IRQ on G13).
struct SleepPlan {bool armImu;const char *wake,*mode;};
constexpr SleepPlan sleepPlan(bool shakeToWake) {
  return shakeToWake?SleepPlan{true,"imu_motion","imu_only"}:SleepPlan{false,"side_key","power_off"};
}
// A standby the ESP32 survived because USB kept its rail up (shake_wake.h),
// kept in RTC memory: whether only the side key wakes it (the power key's
// off, Shake to wake Off, an IMU that could not be armed), so the unplug that
// wakes it can finish that same power-off on battery. Lost, as it should be,
// when PM1 really powers off.
struct UsbStandby {static constexpr uint32_t MAGIC=0x55534231;uint32_t magic;bool keyOnly;};
// How a boot out of a USB-retained standby goes on. finish: unplugged (PM1's
// 5VIN-removed IRQ woke it, or the timer re-check found no 5 V) and 5 V
// checked absent now, so power off again by the same path, on battery; a
// latched removal with 5 V back (unplugged and replugged) must not re-enter
// the standby. keyWake: the side key ended a key-only standby, a full session
// rather than an unused shake wake.
struct UsbStandbyWake {bool finish=false,keyWake=false;};
constexpr uint8_t PM1_VIN_REMOVED_IRQ=0x02;  // PM1 IRQ_STATUS2 (0x41) bit 1
inline UsbStandbyWake usbStandbyWake(const UsbStandby &receipt,bool ext0,bool timerFinish,bool irqRead,uint8_t systemIrq,bool vinAbsent) {
  UsbStandbyWake w;
  if(receipt.magic!=UsbStandby::MAGIC)return w;
  w.finish=vinAbsent && (timerFinish || (ext0 && irqRead && (systemIrq&PM1_VIN_REMOVED_IRQ)));
  w.keyWake=receipt.keyOnly && !w.finish && ext0;
  return w;
}

namespace IdlePowerPolicy {
static constexpr uint32_t DIM_MS=30000,LEVEL_POLL_MS=20,UI_POLL_MS=5;
static constexpr uint8_t DIM_PERCENT=10;
inline uint8_t brightness(uint8_t selected,uint32_t idle,bool host,bool feedback,uint32_t dimMs=DIM_MS) {
  return !host && !feedback && idle>=dimMs?std::min(selected,DIM_PERCENT):selected;
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
