#pragma once
#include <Preferences.h>
#include <cstdint>
#include <cstring>
#include "ble_security.h"

// The stick's Bluetooth name (docs/BLE_PROTOCOL.md, Name). Not in backups: it
// names this stick, not its data. An empty name is the default from the address.
class NameSetting {
public:
  static constexpr const char *KEY="ble_name";
  // False (and the default) for a stored name that is not valid, which is kept.
  static bool load(Preferences &prefs,char *out) {
    out[0]=0;
    if(!prefs.isKey(KEY))return true;
    char bytes[BleName::MAX];
    const size_t n=prefs.getBytesLength(KEY);
    if(n<1 || n>BleName::MAX || prefs.getBytes(KEY,bytes,n)!=n || !BleName::valid(bytes,n))return false;
    std::memcpy(out,bytes,n);out[n]=0;return true;
  }
  static bool save(Preferences &prefs,const char *name) {
    const size_t n=strlen(name);
    if(!BleName::valid(name,n) || prefs.putBytes(KEY,name,n)!=n)return false;
    char readback[BleName::MAX+1];
    return load(prefs,readback) && std::strcmp(readback,name)==0;
  }
};
