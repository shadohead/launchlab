#pragma once
#include <Preferences.h>
#include "rpm_estimator.h"
// A new versioned key only. Missing means the restored three-turn default;
// invalid bytes are retained. Existing sleep/GPIO/history/reference keys stay intact.
class RpmEstimatorSetting {
public:
  static constexpr const char *KEY="rpm_method";
  static bool load(Preferences &prefs,RpmEstimator::Mode &mode) {
    mode=RpmEstimator::DEFAULT_MODE;
    const size_t length=prefs.getBytesLength(KEY);
    if(!length)return !prefs.isKey(KEY);
    uint8_t bytes[4];
    if(length!=4 || prefs.getBytes(KEY,bytes,4)!=4 || bytes[0]!=0x52 || bytes[1]!=1 ||
      !RpmEstimator::valid(bytes[2]) || bytes[3]!=(0x52^1^bytes[2]))return false;
    mode=RpmEstimator::Mode(bytes[2]);return true;
  }
  static bool save(Preferences &prefs,RpmEstimator::Mode mode) {
    const uint8_t value=uint8_t(mode);
    if(!RpmEstimator::valid(value))return false;
    const uint8_t bytes[]={0x52,1,value,uint8_t(0x52^1^value)};
    if(prefs.putBytes(KEY,bytes,sizeof(bytes))!=sizeof(bytes))return false;
    RpmEstimator::Mode readback;
    return load(prefs,readback) && readback==mode;
  }
};
