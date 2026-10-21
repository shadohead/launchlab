#pragma once
#include <cstdint>
#include "appearance.h"
#include "effect_set.h"

// Navigation never starts/stops measurement. Effects are handled by the sketch
// after the visible state changes; setting writes only happen on Apply.
namespace Controls {
enum class Page:uint8_t {Main,Menu,HistoryMenu,History,Battery,Settings,Edit,NewSession,Tournament,Wipe};
enum class Effect:uint8_t {None,Recap,BeginEdit,Preview,Apply,CancelEdit,Older,NewSession,Tournament,BeginWipe,ConfirmWipe,CancelWipe};
enum class Setting:uint8_t {Sleep,Rpm,Brightness,Flip,Sensor,Theme,BestEffect,Celebrate,Sound,Mount,WipeAll};
static constexpr unsigned EDITABLE_SETTING_COUNT=10,SETTING_COUNT=11;
struct State {
  Page page=Page::Main;
  uint8_t menu=0,history=0,setting=0,draft=0;
  bool startSession=false;
  // Valid custom theme slots (bit i: theme THEMES+i), kept current by the
  // sketch; Theme cycles through the built-ins and then these.
  uint8_t customThemes=0;
  // The effect set (effect_set.h), kept current by the sketch; Best effect
  // cycles through Random, these and Off.
  EffectSet::Mask offeredEffects=EffectSet::DEFAULT_MASK;
  bool autoRecap()const{return page==Page::Main;}
  Effect tapA() {
    switch(page) {
      case Page::Main:return Effect::Recap;
      case Page::Menu:
        switch(menu) {
          case 0:page=Page::HistoryMenu;break;
          case 1:page=Page::NewSession;startSession=false;break;
          case 2:page=Page::Battery;break;
          case 3:page=Page::Settings;break;
          case 4:page=Page::Tournament;return Effect::Tournament;
          default:page=Page::Main;break;
        }break;
      case Page::HistoryMenu:page=history<3?Page::History:Page::Menu;break;
      case Page::History:page=Page::HistoryMenu;break;
      case Page::Battery:page=Page::Menu;break;
      case Page::Settings:
        if(setting==unsigned(Setting::WipeAll)){page=Page::Wipe;return Effect::BeginWipe;}
        if(setting<EDITABLE_SETTING_COUNT){page=Page::Edit;return Effect::BeginEdit;}
        page=Page::Menu;break;
      case Page::Edit:return Effect::Apply;
      case Page::NewSession:page=Page::Menu;return startSession?Effect::NewSession:Effect::None;
      case Page::Tournament:break;
      case Page::Wipe:return Effect::ConfirmWipe;
    }
    return Effect::None;
  }
  Effect tapB() {
    switch(page) {
      case Page::Main:page=Page::Menu;menu=0;break;
      case Page::Menu:menu=(menu+1)%6;break;
      case Page::HistoryMenu:history=(history+1)%4;break;
      case Page::History:return Effect::Older;
      case Page::Settings:setting=(setting+1)%(SETTING_COUNT+1);break;
      case Page::Edit:
        switch(Setting(setting)) {
          case Setting::Sleep:draft=draft>=10?1:draft+1;break;
          case Setting::Rpm:case Setting::Flip:case Setting::Sensor:case Setting::Mount:draft=!draft;break;
          case Setting::Brightness:draft=draft>=100?10:draft+10;break;
          case Setting::Theme:
            do draft=(draft+1)%Appearance::ALL_THEMES;
            while(draft>=Appearance::THEMES && !((customThemes>>(draft-Appearance::THEMES))&1u));
            break;
          case Setting::BestEffect:draft=uint8_t(EffectSet::next(Appearance::Effect(draft),offeredEffects));break;
          case Setting::Celebrate:draft=(draft+1)%3;break;
          case Setting::Sound:draft=(draft+1)%3;break;
          case Setting::WipeAll:return Effect::None;
        }return Effect::Preview;
      case Page::NewSession:startSession=!startSession;break;
      case Page::Tournament:return Effect::Tournament;
      case Page::Battery:break;
      case Page::Wipe:return Effect::CancelWipe;
    }
    return Effect::None;
  }
  Effect holdB(bool recap=false) {
    switch(page) {
      case Page::Edit:page=Page::Settings;return Effect::CancelEdit;
      case Page::History:page=Page::HistoryMenu;break;
      case Page::HistoryMenu:case Page::Battery:case Page::Settings:case Page::NewSession:page=Page::Menu;break;
      case Page::Menu:page=Page::Main;break;
      case Page::Main:return recap?Effect::Recap:Effect::None;
      case Page::Tournament:break;
      case Page::Wipe:return Effect::CancelWipe;
    }
    return Effect::None;
  }
  Effect holdA(bool recap=false) {
    if(page==Page::Tournament){page=Page::Main;return Effect::Tournament;}
    if(page==Page::Main && !recap){page=Page::Tournament;return Effect::Tournament;}
    return Effect::None;
  }
};
// Gate physical clicks separately from library event details. A consumed hold
// cannot trigger an unrelated click on release; a new press clears the latch.
struct Gesture {
  bool consumed=false;
  bool click(bool pressed,bool holding,bool clicked) {
    if(pressed)consumed=false;
    if(holding)consumed=true;
    return clicked && !consumed;
  }
};
// Saved navigation layouts. 0.10: five settings, setting in 3 bits.
// 0.11: seven settings, setting in 3 bits, view at bit 12. 0.12: eight
// settings in 4 bits (9-12), view at bit 13. 0.13 adds Mount; distinguish
// its schema so the old Back row still opens Back instead of the new editor.
// Adding Wipe uses another schema and never resumes a destructive dialog.
// 0.15 inserts Celebrate after Best effect: older rows from Sound on move
// down one.
static constexpr uint32_t LEGACY_WAKE_MAGIC=0x4e560000,WAKE7_MAGIC=0x4e570000,
  WAKE8_MAGIC=0x4e580000,WAKE9_MAGIC=0x4e590000,WAKE10_MAGIC=0x4e5a0000,WAKE_MAGIC=0x4e5b0000;
inline uint32_t encodeWake(State state,uint8_t tournamentView) {
  if(state.page==Page::Edit || state.page==Page::Wipe)state.page=Page::Settings;
  return WAKE_MAGIC|uint32_t(state.page)|(uint32_t(state.menu)<<4)|
    (uint32_t(state.history)<<7)|(uint32_t(state.setting)<<9)|(uint32_t(tournamentView)<<13);
}
inline bool decodeWake(uint32_t value,State &out,uint8_t &tournamentView) {
  State state;state.page=Page(value&15);state.menu=(value>>4)&7;state.history=(value>>7)&3;
  // Each layout's Back row is the one after its last setting.
  unsigned setting=0,back=0;
  const auto wideMagic=value&0xffffc000;
  if(wideMagic==WAKE_MAGIC || wideMagic==WAKE10_MAGIC || wideMagic==WAKE9_MAGIC || wideMagic==WAKE8_MAGIC) {
    setting=(value>>9)&15;tournamentView=(value>>13)&1;
    back=wideMagic==WAKE8_MAGIC?8:wideMagic==WAKE9_MAGIC?9:wideMagic==WAKE10_MAGIC?10:SETTING_COUNT;
  }
  else {
    const auto magic=value&0xffffe000;
    if(magic!=WAKE7_MAGIC && magic!=LEGACY_WAKE_MAGIC)return false;
    back=magic==LEGACY_WAKE_MAGIC?5:7;
    setting=(value>>9)&7;tournamentView=(value>>12)&1;
  }
  if(setting>back)return false;
  if(setting==back)setting=SETTING_COUNT;
  else if(wideMagic!=WAKE_MAGIC && setting>=unsigned(Setting::Celebrate))++setting;
  state.setting=uint8_t(setting);
  if(uint8_t(state.page)>uint8_t(Page::Wipe) || state.page==Page::Edit || state.page==Page::Wipe || state.menu>=6 || state.setting>SETTING_COUNT)return false;
  out=state;return true;
}
}
