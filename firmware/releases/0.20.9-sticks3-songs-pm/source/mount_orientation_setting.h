#pragma once
#include <Preferences.h>
#include "mount_orientation.h"

// A separate key leaves existing display, sensor and history records intact.
class MountOrientationSetting {
public:
  static constexpr const char *KEY="mount_mode";
  static bool load(Preferences &prefs,MountOrientation::Mode &mode) {
    mode=MountOrientation::Mode::Top;
    const size_t length=prefs.getBytesLength(KEY);
    if(!length)return !prefs.isKey(KEY);
    uint8_t bytes[4];
    if(length!=4 || prefs.getBytes(KEY,bytes,4)!=4 || bytes[0]!=0x4d || bytes[1]!=1 ||
       !MountOrientation::valid(bytes[2]) || bytes[3]!=(0x4d^1^bytes[2]))return false;
    mode=MountOrientation::Mode(bytes[2]);return true;
  }
  static bool save(Preferences &prefs,MountOrientation::Mode mode) {
    const uint8_t value=uint8_t(mode);
    if(!MountOrientation::valid(value))return false;
    const uint8_t bytes[]={0x4d,1,value,uint8_t(0x4d^1^value)};
    if(prefs.putBytes(KEY,bytes,sizeof(bytes))!=sizeof(bytes))return false;
    MountOrientation::Mode readback;
    return load(prefs,readback) && readback==mode;
  }
};
