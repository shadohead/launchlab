#pragma once
#include <Preferences.h>
#include <cstdint>

// Bluetooth link to the phone app: off for a unit with no saved choice, since
// the radio costs battery and the stack takes internal RAM. Off at boot keeps
// stock's memory layout and starts nothing of BLE, so the choice applies at a
// restart: on at boot, the canvas goes to PSRAM to leave the stack internal RAM.
class BluetoothSetting {
public:
  static constexpr bool restartNeeded(bool on,bool onAtBoot){return on!=onAtBoot;}
  static constexpr bool canvasInPsram(bool onAtBoot){return onAtBoot;}
  static constexpr const char *KEY="bluetooth";
  static constexpr bool DEFAULT_ON=false;
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
