#pragma once
#include <Preferences.h>
#include <cstdint>
#include "controls.h"

// Battery icon (0.20.8): whether the screens show the charge in their top
// right corner (ControlsUI::cornerBattery). On for a unit with no saved choice.
class BatteryIconSetting {
public:
  static constexpr const char *KEY="battery_icon";
  static constexpr bool DEFAULT_ON=Controls::BATTERY_ICON_DEFAULT;
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
