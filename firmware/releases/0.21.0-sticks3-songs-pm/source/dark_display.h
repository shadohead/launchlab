#pragma once
#include <cstdint>

// Op 15: the phone reads the numbers, so the stick's screen can stay dark. Only
// the backlight and the drawing stop; the measurement runs as ever. A button
// lights it for a while, and it ends with the link, so it never looks dead.
struct DarkDisplay {
  static constexpr uint32_t WAKE_MS=10000;
  bool requested=false,woken=false;
  uint32_t wokenAt=0;
  void set(bool dark){requested=dark;woken=false;}
  // True when this press only woke the screen, and must do nothing else.
  bool press(uint32_t now) {
    if(!requested)return false;
    const bool wasDark=dark(now);
    woken=true;wokenAt=now;
    return wasDark;
  }
  bool dark(uint32_t now)const{return requested && (!woken || now-wokenAt>=WAKE_MS);}
  // The window ends once, so the 32-bit clock cannot wrap into it again.
  void tick(uint32_t now){if(woken && now-wokenAt>=WAKE_MS)woken=false;}
};
