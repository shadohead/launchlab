#pragma once
#include <Preferences.h>
#include "sensor_profile.h"

class SensorProfileSetting {
public:
  static constexpr const char *KEY="sensor_profile";
  static bool load(Preferences &prefs,StickS3SensorProfile::Mode &mode) {
    mode=StickS3SensorProfile::DEFAULT_MODE;
    const size_t length=prefs.getBytesLength(KEY);
    if(!length)return !prefs.isKey(KEY);
    uint8_t bytes[4];
    if(length!=4 || prefs.getBytes(KEY,bytes,4)!=4 || bytes[0]!=0x53 || bytes[1]!=1 ||
       !StickS3SensorProfile::valid(bytes[2]) || bytes[3]!=(0x53^1^bytes[2]))return false;
    mode=StickS3SensorProfile::Mode(bytes[2]);return true;
  }
  static bool save(Preferences &prefs,StickS3SensorProfile::Mode mode) {
    const uint8_t value=uint8_t(mode);
    if(!StickS3SensorProfile::valid(value))return false;
    const uint8_t bytes[]={0x53,1,value,uint8_t(0x53^1^value)};
    if(prefs.putBytes(KEY,bytes,sizeof(bytes))!=sizeof(bytes))return false;
    StickS3SensorProfile::Mode readback;
    return load(prefs,readback) && readback==mode;
  }
};
