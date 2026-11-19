#pragma once
#include <Preferences.h>
#include <cstdint>
#include "ui_text.h"

// Language's record (Controls::Setting::Language): one byte, the screen's
// language (UiText::Lang). Missing means English; an unknown byte (a
// language a newer build had) is kept and English used.
class LanguageSetting {
public:
  static constexpr const char *KEY="ui_lang";
  static bool load(Preferences &prefs,uint8_t &language) {
    language=0;
    if(!prefs.isKey(KEY))return true;
    const uint8_t value=prefs.getUChar(KEY,0);
    if(value>=UiText::LANGS)return false;
    language=value;return true;
  }
  static bool save(Preferences &prefs,uint8_t language) {
    if(language>=UiText::LANGS || prefs.putUChar(KEY,language)!=1)return false;
    uint8_t readback;
    return load(prefs,readback) && readback==language;
  }
};
