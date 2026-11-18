#pragma once
#include <Preferences.h>
#include <cstdint>
#include "battle_beacon.h"

// Battles (docs/FIRMWARE_BATTLES.md), one byte: whether a launch looks for
// another LaunchLab launched with it (bit 0), and the match length this player
// chooses (bit 1: first to 7, otherwise first to 4).
// Auto battles is on for a unit with no saved choice: battles ride on
// Bluetooth, which is itself off until chosen, and they use the radio only
// around a launch. Off, a launch tells and hears nothing.
class BattlesSetting {
public:
  static constexpr const char *KEY="battles";
  static constexpr bool DEFAULT_ON=true;
  static constexpr uint8_t ON=1,LONG_MATCH=2;
  static constexpr uint8_t value(bool on,uint8_t matchTo){return uint8_t((on?ON:0)|(matchTo==Battle::MATCH_LONG?LONG_MATCH:0));}
  // False (and the defaults) for a stored value this firmware did not write, which is kept.
  static bool load(Preferences &prefs,bool &on,uint8_t &matchTo) {
    on=DEFAULT_ON;matchTo=Battle::MATCH_SHORT;
    if(!prefs.isKey(KEY))return true;
    const uint8_t stored=prefs.getUChar(KEY,0xff);
    if(stored>(ON|LONG_MATCH))return false;
    on=stored&ON;matchTo=stored&LONG_MATCH?Battle::MATCH_LONG:Battle::MATCH_SHORT;return true;
  }
  static bool save(Preferences &prefs,bool on,uint8_t matchTo) {
    if(prefs.putUChar(KEY,value(on,matchTo))!=1)return false;
    bool readOn;uint8_t readTo;
    return load(prefs,readOn,readTo) && readOn==on && readTo==(matchTo==Battle::MATCH_LONG?Battle::MATCH_LONG:Battle::MATCH_SHORT);
  }
};
