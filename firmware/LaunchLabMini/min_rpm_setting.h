#pragma once
#include <Preferences.h>
#include <cstdint>
#include "min_rpm.h"

// Min RPM's record (min_rpm.h): one byte, its step count. Missing means the
// 4,000 rpm default; an out-of-range byte is kept and the default used.
class MinRpmSetting {
public:
  static constexpr const char *KEY="min_rpm";
  static bool load(Preferences &prefs,uint8_t &steps) {
    steps=MinRpm::DEFAULT_STEPS;
    if(!prefs.isKey(KEY))return true;
    const uint8_t value=prefs.getUChar(KEY,0);
    if(!MinRpm::valid(value))return false;
    steps=value;return true;
  }
  static bool save(Preferences &prefs,uint8_t steps) {
    if(!MinRpm::valid(steps) || prefs.putUChar(KEY,steps)!=1)return false;
    uint8_t readback;
    return load(prefs,readback) && readback==steps;
  }
};
