#pragma once
#include <Preferences.h>
#include <cstdint>
#include "power_status.h"

// Shake to wake (0.20.2): whether an inactivity sleep arms the IMU's motion
// wake. On (the default, as before 0.20.2) for a unit with no saved choice.
// Off sleeps as the side key's power-off does: the IMU is not armed and its
// supply not held, and the side key wakes the unit.
class ShakeWakeSetting {
public:
  static constexpr const char *KEY="shake_wake";
  static constexpr bool DEFAULT_ON=InactivityTimer::SHAKE_WAKE_DEFAULT;
  // False (and the default) for a stored value that is not 0 or 1, which is kept.
  static bool load(Preferences &prefs,bool &on) {
    on=DEFAULT_ON;
    if(!prefs.isKey(KEY))return true;
    const uint8_t value=prefs.getUChar(KEY,0xff);
    if(value>1)return false;
    on=value;return true;
  }
  static bool save(Preferences &prefs,bool on) {
    if(prefs.putUChar(KEY,uint8_t(on))!=1)return false;
    bool readback;
    return load(prefs,readback) && readback==on;
  }
};
