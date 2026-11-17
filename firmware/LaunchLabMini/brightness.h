#pragma once
#include <cstdint>
// Screen brightness, 10-100% in 10% steps. A unit with no saved value (new,
// never changed, or after Wipe all) uses the default; a saved value always
// wins. An invalid saved byte is left in place and the default is used.
// The default was 30% through 0.20.9; 20% saves an estimated 2 to 4 mA of
// about 70 for as long as the screen is on (docs/battery/README.md).
namespace Brightness {
constexpr const char *KEY="bright_pct";
constexpr uint8_t DEFAULT_PERCENT=20;
constexpr bool valid(uint8_t percent){return percent>=10 && percent<=100 && percent%10==0;}
// False only for an invalid saved value.
template<class Prefs> bool load(Prefs &prefs,uint8_t &percent) {
  const uint8_t saved=prefs.getUChar(KEY,DEFAULT_PERCENT);
  percent=valid(saved)?saved:DEFAULT_PERCENT;
  return percent==saved;
}
}
