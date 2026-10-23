#pragma once
#include <Preferences.h>
#include <cstdint>

class DisplayFlipSetting {
public:
  static constexpr const char *KEY="disp_flip";
  static bool load(Preferences &prefs,bool &flipped) {
    flipped=false;
    const size_t length=prefs.getBytesLength(KEY);
    if(!length)return !prefs.isKey(KEY);
    uint8_t bytes[4];
    if(length!=4 || prefs.getBytes(KEY,bytes,4)!=4 || bytes[0]!=0x44 || bytes[1]!=1 ||
       bytes[2]>1 || bytes[3]!=(0x44^1^bytes[2]))return false;
    flipped=bytes[2];return true;
  }
  static bool save(Preferences &prefs,bool flipped) {
    const uint8_t bytes[]={0x44,1,uint8_t(flipped),uint8_t(0x44^1^uint8_t(flipped))};
    if(prefs.putBytes(KEY,bytes,sizeof(bytes))!=sizeof(bytes))return false;
    bool readback;
    return load(prefs,readback) && readback==flipped;
  }
};
