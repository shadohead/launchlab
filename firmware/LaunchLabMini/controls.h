#pragma once
#include <cstdint>
#include "appearance.h"
#include "effect_set.h"
#include "wear_check.h"

// Navigation never starts/stops measurement. Effects are handled by the sketch
// after the visible state changes; setting writes only happen on Apply.
namespace Controls {
// Battery retains its persisted page ID; the menu and screen now call it Device.
enum class Page:uint8_t {Main,Menu,HistoryMenu,History,Battery,Settings,Edit,NewSession,Tournament,Wipe,WearCheck,MicRpm,About,Analysis};
enum class Effect:uint8_t {None,Recap,BeginEdit,Preview,Apply,CancelEdit,Older,NewSession,Tournament,BeginWipe,ConfirmWipe,CancelWipe,AnalysisStop,AnalysisReplay};
static constexpr unsigned MENU_COUNT=8;
static constexpr uint8_t MIC_ROW=6;  // before Back
// Values are stable (HIL `!set N`, saved wake navigation); the screen shows
// them in ORDER. Shake to wake (0.20.2) is listed right after Sleep after.
// About (0.20.8) opens a page of what the unit runs; it edits nothing.
// Battery icon (0.20.8) shows the charge in the top right corner of the
// Menu and Settings pages, On by default.
enum class Setting:uint8_t {Sleep,Rpm,Brightness,Flip,Sensor,Theme,BestEffect,Celebrate,Sound,Mount,ShakeWake,WipeAll,About,BatteryIcon};
static constexpr unsigned SETTING_COUNT=14;
static constexpr bool BATTERY_ICON_DEFAULT=true;
// The pages with the corner battery: the Menu and History lists, Settings, its
// editors and About. Not Main (owner, 2026-11-05), the history graphs, the
// Battery page or the pages that use the top band (Wear check, Mic RPM).
constexpr bool showsCornerBattery(Page page) {
  return page==Page::Menu || page==Page::HistoryMenu || page==Page::Settings || page==Page::Edit || page==Page::About;
}
// Every row but Wipe all and About has an editor and a value.
constexpr bool editable(unsigned setting) {
  return setting<SETTING_COUNT && setting!=unsigned(Setting::WipeAll) && setting!=unsigned(Setting::About);
}
// The Settings list, five rows a page: general, appearance, then Mounting,
// Battery icon, Wipe all, About and Back (SETTING_COUNT, after the last).
static constexpr Setting ORDER[SETTING_COUNT]={Setting::Sleep,Setting::ShakeWake,Setting::Rpm,Setting::Brightness,Setting::Sensor,
  Setting::Flip,Setting::Theme,Setting::BestEffect,Setting::Celebrate,Setting::Sound,Setting::Mount,Setting::BatteryIcon,
  Setting::WipeAll,Setting::About};
// A row's place in ORDER (Back, SETTING_COUNT, is last), and the row after it.
constexpr unsigned position(unsigned setting) {
  for(unsigned i=0;i<SETTING_COUNT;++i)if(unsigned(ORDER[i])==setting)return i;
  return SETTING_COUNT;
}
constexpr unsigned rowAt(unsigned position){return position<SETTING_COUNT?unsigned(ORDER[position]):SETTING_COUNT;}
constexpr unsigned nextRow(unsigned setting){return rowAt((position(setting)+1)%(SETTING_COUNT+1));}
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
  WearCheck::Session wear;
  bool autoRecap()const{return page==Page::Main;}
  Effect tapA() {
    switch(page) {
      case Page::Main:return Effect::Recap;
      case Page::Analysis:return Effect::AnalysisReplay;
      case Page::Menu:
        switch(menu) {
          case 0:page=Page::HistoryMenu;break;
          case 1:page=Page::NewSession;startSession=false;break;
          case 2:page=Page::Battery;break;
          case 3:page=Page::Settings;break;
          case 4:page=Page::Tournament;return Effect::Tournament;
          case 5:page=Page::WearCheck;wear.reset();break;
          case MIC_ROW:page=Page::MicRpm;break;
          default:page=Page::Main;break;
        }break;
      case Page::HistoryMenu:page=history<3?Page::History:Page::Menu;break;
      case Page::History:page=Page::HistoryMenu;break;
      case Page::Battery:page=Page::Menu;break;
      case Page::Settings:
        if(setting==unsigned(Setting::WipeAll)){page=Page::Wipe;return Effect::BeginWipe;}
        if(setting==unsigned(Setting::About)){page=Page::About;break;}
        if(editable(setting)){page=Page::Edit;return Effect::BeginEdit;}
        page=Page::Menu;break;
      case Page::Edit:return Effect::Apply;
      case Page::NewSession:page=Page::Menu;return startSession?Effect::NewSession:Effect::None;
      case Page::Tournament:break;
      case Page::Wipe:return Effect::ConfirmWipe;
      case Page::WearCheck:wear.reset();break;
      case Page::MicRpm:page=Page::Menu;break;
      case Page::About:page=Page::Settings;break;
    }
    return Effect::None;
  }
  Effect tapB() {
    switch(page) {
      case Page::Main:page=Page::Menu;menu=0;break;
      case Page::Analysis:return Effect::AnalysisStop;
      case Page::Menu:menu=(menu+1)%MENU_COUNT;break;
      case Page::HistoryMenu:history=(history+1)%4;break;
      case Page::History:return Effect::Older;
      case Page::Settings:setting=uint8_t(nextRow(setting));break;
      case Page::Edit:
        switch(Setting(setting)) {
          case Setting::Sleep:draft=draft>=10?1:draft+1;break;
          case Setting::Rpm:case Setting::Flip:case Setting::Sensor:case Setting::Mount:case Setting::ShakeWake:
          case Setting::BatteryIcon:draft=!draft;break;
          case Setting::Brightness:draft=draft>=100?10:draft+10;break;
          case Setting::Theme:
            do draft=(draft+1)%Appearance::ALL_THEMES;
            while(draft>=Appearance::THEMES && !((customThemes>>(draft-Appearance::THEMES))&1u));
            break;
          case Setting::BestEffect:draft=uint8_t(EffectSet::next(Appearance::Effect(draft),offeredEffects));break;
          case Setting::Celebrate:draft=(draft+1)%3;break;
          case Setting::Sound:draft=(draft+1)%3;break;
          case Setting::WipeAll:case Setting::About:return Effect::None;
        }return Effect::Preview;
      case Page::NewSession:startSession=!startSession;break;
      case Page::Tournament:return Effect::Tournament;
      case Page::Battery:break;
      case Page::Wipe:return Effect::CancelWipe;
      case Page::WearCheck:break;
      case Page::MicRpm:break;
      case Page::About:break;
    }
    return Effect::None;
  }
  Effect holdB(bool recap=false) {
    switch(page) {
      case Page::Edit:page=Page::Settings;return Effect::CancelEdit;
      case Page::About:page=Page::Settings;break;
      case Page::History:page=Page::HistoryMenu;break;
      case Page::HistoryMenu:case Page::Battery:case Page::Settings:case Page::NewSession:case Page::WearCheck:page=Page::Menu;break;
      case Page::MicRpm:page=Page::Menu;break;
      case Page::Menu:page=Page::Main;break;
      case Page::Main:page=Page::Analysis;break;
      case Page::Analysis:page=Page::Main;break;
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
// down one. 0.20.2 adds Shake to wake as value 10: Wipe all and Back move
// down one. 0.20.3 inserts Wear check at menu row 5: an older Back row (5)
// moves down one. Wear check itself wakes on its menu row, so no page value
// after Wipe is ever saved. 0.20.7 inserts Mic RPM at menu row 6: an older Back
// row moves down one again, and that page too wakes on its menu row. 0.20.8
// adds About (12) and Battery icon (13): the Settings Back row moves to 14,
// and About wakes on its Settings row.
static constexpr uint32_t LEGACY_WAKE_MAGIC=0x4e560000,WAKE7_MAGIC=0x4e570000,
  WAKE8_MAGIC=0x4e580000,WAKE9_MAGIC=0x4e590000,WAKE10_MAGIC=0x4e5a0000,WAKE11_MAGIC=0x4e5b0000,
  WAKE12_MAGIC=0x4e5c0000,WAKE13_MAGIC=0x4e5d0000,WAKE14_MAGIC=0x4e5e0000,WAKE_MAGIC=0x4e5f0000;
inline uint32_t encodeWake(State state,uint8_t tournamentView) {
  if(state.page==Page::Analysis)state.page=Page::Main;
  if(state.page==Page::Edit || state.page==Page::Wipe)state.page=Page::Settings;
  if(state.page==Page::WearCheck){state.page=Page::Menu;state.menu=5;}
  if(state.page==Page::MicRpm){state.page=Page::Menu;state.menu=MIC_ROW;}  // the mic never starts on wake
  if(state.page==Page::About){state.page=Page::Settings;state.setting=uint8_t(Setting::About);}
  return WAKE_MAGIC|uint32_t(state.page)|(uint32_t(state.menu)<<4)|
    (uint32_t(state.history)<<7)|(uint32_t(state.setting)<<9)|(uint32_t(tournamentView)<<13);
}
inline bool decodeWake(uint32_t value,State &out,uint8_t &tournamentView) {
  State state;state.page=Page(value&15);state.menu=(value>>4)&7;state.history=(value>>7)&3;
  // Each layout's Back row is the one after its last setting.
  unsigned setting=0,back=0;
  const auto wideMagic=value&0xffffc000;
  if(wideMagic==WAKE_MAGIC || wideMagic==WAKE14_MAGIC || wideMagic==WAKE13_MAGIC || wideMagic==WAKE12_MAGIC || wideMagic==WAKE11_MAGIC || wideMagic==WAKE10_MAGIC || wideMagic==WAKE9_MAGIC || wideMagic==WAKE8_MAGIC) {
    setting=(value>>9)&15;tournamentView=(value>>13)&1;
    back=wideMagic==WAKE8_MAGIC?8:wideMagic==WAKE9_MAGIC?9:wideMagic==WAKE10_MAGIC?10:wideMagic==WAKE11_MAGIC?11:
      wideMagic==WAKE_MAGIC?SETTING_COUNT:12;  // 12: Back in 0.20.2-0.20.7 (WAKE12-WAKE14)
  }
  else {
    const auto magic=value&0xffffe000;
    if(magic!=WAKE7_MAGIC && magic!=LEGACY_WAKE_MAGIC)return false;
    back=magic==LEGACY_WAKE_MAGIC?5:7;
    setting=(value>>9)&7;tournamentView=(value>>12)&1;
  }
  if(setting>back)return false;
  if(setting==back)setting=SETTING_COUNT;
  else if(wideMagic!=WAKE_MAGIC && wideMagic!=WAKE14_MAGIC && wideMagic!=WAKE13_MAGIC && wideMagic!=WAKE12_MAGIC) {
    // In the 0.15-0.20.1 numbering (WAKE11): Celebrate is 7, Wipe all 10.
    if(wideMagic!=WAKE11_MAGIC && setting>=7)++setting;
    if(setting==10)setting=unsigned(Setting::WipeAll);
  }
  state.setting=uint8_t(setting);
  const unsigned rows=wideMagic==WAKE_MAGIC || wideMagic==WAKE14_MAGIC?MENU_COUNT:wideMagic==WAKE13_MAGIC?7:6;
  if(uint8_t(state.page)>uint8_t(Page::Wipe) || state.page==Page::Edit || state.page==Page::Wipe ||
     state.menu>=rows || state.setting>SETTING_COUNT)return false;
  if(state.menu==rows-1)state.menu=MENU_COUNT-1;  // Back
  out=state;return true;
}
}
