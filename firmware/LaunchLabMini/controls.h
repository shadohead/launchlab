#pragma once
#include <cstdint>
#include "appearance.h"
#include "effect_set.h"
#include "min_rpm.h"
#include "mount_orientation.h"
#include "wear_check.h"

// Navigation never starts/stops measurement. Effects are handled by the sketch
// after the visible state changes; setting writes only happen on Apply.
namespace Controls {
// Battery retains its persisted page ID; the menu and screen call it Device.
// Analysis (0.21.0, experimental) took the value of the Mic RPM page it
// replaced (0.20.7-0.20.11): neither is ever saved for wake.
enum class Page:uint8_t {Main,Menu,HistoryMenu,History,Battery,Settings,Edit,NewSession,Tournament,Wipe,WearCheck,Analysis,About,
  Bluetooth,ForgetPhones,EnableBluetooth,BeybladeX,Battles,Rival};
enum class Effect:uint8_t {None,Recap,BeginEdit,Preview,Apply,CancelEdit,Older,NewSession,Tournament,BeginWipe,ConfirmWipe,CancelWipe,ForgetPhones,ToggleBluetooth,
  ToggleBattles,ToggleMatchLength,AnalysisStop,AnalysisReplay};
// The Menu, top to bottom, in three groups (0.20.9): what a session uses
// (History, New session, Tournament and now Battles), the tools (String
// wear check, Analysis), then the device (Battery, Bluetooth, Settings), and
// Back. Through 0.20.8 the rows were in the order they were added
// (MENU_ROWS_0208); 0.20.9 to 0.20.11 and the Min RPM builds had these
// without Battles (MENU_ROWS_0209). Analysis (0.21.0) has Mic RPM's row: it
// listens with the same mic and shows the same live RPM, and more.
enum MenuRow:uint8_t {HISTORY_ROW,NEW_SESSION_ROW,TOURNAMENT_ROW,BATTLES_ROW,WEAR_ROW,ANALYSIS_ROW,BATTERY_ROW,BLUETOOTH_ROW,SETTINGS_ROW,BACK_ROW};
static constexpr unsigned MENU_COUNT=10;
// Bit i: row i starts a group, drawn with a little space above it.
static constexpr unsigned MENU_GROUPS=(1u<<WEAR_ROW)|(1u<<BATTERY_ROW)|(1u<<BACK_ROW);
// 0.20.7-0.20.8's rows before their Back (0.20.3 had the first six, earlier the first five).
static constexpr MenuRow MENU_ROWS_0208[]={HISTORY_ROW,NEW_SESSION_ROW,BATTERY_ROW,SETTINGS_ROW,TOURNAMENT_ROW,WEAR_ROW,ANALYSIS_ROW};
// The nine rows of 0.20.9-0.20.11 and the Min RPM builds, Back included.
static constexpr MenuRow MENU_ROWS_0209[]={HISTORY_ROW,NEW_SESSION_ROW,TOURNAMENT_ROW,WEAR_ROW,ANALYSIS_ROW,BATTERY_ROW,BLUETOOTH_ROW,SETTINGS_ROW,BACK_ROW};
// Values are stable (HIL `!set N`, saved wake navigation, the Bluetooth
// protocol's setting ids); the screen shows them in ORDER. Shake to wake
// (0.20.2) is listed right after Sleep after.
// About (0.20.8) opens a page of what the unit runs; it edits nothing.
// Battery icon (0.20.8) shows the charge in the top right corner of the
// Menu and Settings pages, On by default.
// Bluetooth (0.20.9, experimental) keeps a value and an editor (the protocol's
// id 14, HIL) but has no row here: it has its own Menu page, which asks before
// turning it on. Min RPM (0.21.0, value 15) is the slowest launch recorded
// (min_rpm.h), listed after RPM method. Language (value 16) is the
// screen's language (ui_text.h), first on the unit's page. ROWS is the list's length;
// SETTING_COUNT stands for Back.
enum class Setting:uint8_t {Sleep,Rpm,Brightness,Flip,Sensor,Theme,BestEffect,Celebrate,Sound,Mount,ShakeWake,WipeAll,About,BatteryIcon,Bluetooth,MinRpm,Language};
static constexpr unsigned SETTING_COUNT=17,ROWS=16;
// The languages Language cycles through (UiText::Lang).
static constexpr unsigned LANGUAGES=3;
static constexpr bool BATTERY_ICON_DEFAULT=true;
// The pages with the corner battery: the Menu, History and Battles lists,
// Settings, its editors and About. Not Main (owner, 2026-11-05), the history graphs, the
// Battery page or the pages that use the top band (Wear check, Mic RPM).
constexpr bool showsCornerBattery(Page page) {
  return page==Page::Menu || page==Page::HistoryMenu || page==Page::Settings || page==Page::Edit || page==Page::About || page==Page::Battles;
}
// Main shows it only while the unit charges (0.20.9), so a plugged-in unit says so.
constexpr bool showsChargingBattery(Page page){return page==Page::Main;}
// Every row but Wipe all and About has an editor and a value.
constexpr bool editable(unsigned setting) {
  return setting<SETTING_COUNT && setting!=unsigned(Setting::WipeAll) && setting!=unsigned(Setting::About);
}
// The Settings list in pages: general and measuring, appearance (five rows
// each), then the unit's own page of seven: Language, Mounting, Flip
// display, Battery icon, Wipe all, About and Back (SETTING_COUNT, after the
// last). Through
// 0.20.11 Brightness was on the first page and Flip display on the second;
// Min RPM (0.21.0) took Brightness's place beside RPM method.
static constexpr unsigned PAGE_ROWS=5,LAST_PAGE=2*PAGE_ROWS;
static constexpr Setting ORDER[ROWS]={Setting::Sleep,Setting::ShakeWake,Setting::Rpm,Setting::MinRpm,Setting::Sensor,
  Setting::Brightness,Setting::Theme,Setting::BestEffect,Setting::Celebrate,Setting::Sound,
  Setting::Language,Setting::Mount,Setting::Flip,Setting::BatteryIcon,Setting::WipeAll,Setting::About};
// A row's place in ORDER (Back, SETTING_COUNT, is last), and the row after it.
constexpr unsigned position(unsigned setting) {
  for(unsigned i=0;i<ROWS;++i)if(unsigned(ORDER[i])==setting)return i;
  return ROWS;
}
constexpr unsigned rowAt(unsigned position){return position<ROWS?unsigned(ORDER[position]):SETTING_COUNT;}
constexpr unsigned nextRow(unsigned setting){return rowAt((position(setting)+1)%(ROWS+1));}
struct State {
  Page page=Page::Main;
  uint8_t menu=0,history=0,setting=0,draft=0;
  bool startSession=false,forgetChosen=false,enableChosen=false;
  // Whether Bluetooth is on, kept current by the sketch: turning it on asks first.
  bool bluetoothOn=false;
  // The Bluetooth page's row: 0 On/Off, 1 Beyblade X, 2 Forget phones, 3 Back.
  // Beyblade X opens a page for the Battle Pass profile, where A held is the
  // pass's button (apps want it pressed to register a pass) and B goes back.
  uint8_t bluetoothRow=0;
  // The Battles page: row 0 turns Auto battles on or off, row 1 switches
  // the match length (first to 4 or 7), then one row for each rival, most
  // recent first, then Back. The sketch keeps the choices and the number of
  // rivals current; Rival shows the one at `rival`.
  static constexpr uint8_t BATTLE_AUTO_ROW=0,BATTLE_MATCH_ROW=1,BATTLE_RIVAL_ROW=2;
  bool battlesOn=true;
  uint8_t battleRow=0,rivals=0,rival=0;
  unsigned battleRows()const{return unsigned(rivals)+BATTLE_RIVAL_ROW+1;}
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
          case HISTORY_ROW:page=Page::HistoryMenu;break;
          case NEW_SESSION_ROW:page=Page::NewSession;startSession=false;break;
          case TOURNAMENT_ROW:page=Page::Tournament;return Effect::Tournament;
          case BATTLES_ROW:page=Page::Battles;battleRow=0;break;
          case WEAR_ROW:page=Page::WearCheck;wear.reset();break;
          case ANALYSIS_ROW:page=Page::Analysis;break;
          case BATTERY_ROW:page=Page::Battery;break;
          case BLUETOOTH_ROW:page=Page::Bluetooth;bluetoothRow=0;break;
          case SETTINGS_ROW:page=Page::Settings;break;
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
      case Page::About:page=Page::Settings;break;
      case Page::Bluetooth:
        // Turning it off is immediate; turning it on first says it is experimental.
        if(bluetoothRow==0){if(bluetoothOn)return Effect::ToggleBluetooth;page=Page::EnableBluetooth;enableChosen=false;}
        else if(bluetoothRow==1)page=Page::BeybladeX;
        else if(bluetoothRow==2){page=Page::ForgetPhones;forgetChosen=false;}
        else page=Page::Menu;
        break;
      case Page::BeybladeX:break;  // A is the pass's button here: held, not tapped
      case Page::ForgetPhones:page=Page::Bluetooth;return forgetChosen?Effect::ForgetPhones:Effect::None;
      case Page::EnableBluetooth:page=Page::Bluetooth;return enableChosen && !bluetoothOn?Effect::ToggleBluetooth:Effect::None;
      case Page::Battles:
        if(battleRow>=battleRows())battleRow=0;
        if(battleRow==BATTLE_AUTO_ROW) {
          // Battles are found over Bluetooth: with it off, this row leads to turning it on.
          if(!bluetoothOn){page=Page::EnableBluetooth;enableChosen=false;break;}
          return Effect::ToggleBattles;
        }
        if(battleRow==BATTLE_MATCH_ROW)return Effect::ToggleMatchLength;
        if(battleRow<BATTLE_RIVAL_ROW+rivals){rival=uint8_t(battleRow-BATTLE_RIVAL_ROW);page=Page::Rival;}
        else page=Page::Menu;
        break;
      case Page::Rival:page=Page::Battles;break;
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
          case Setting::BatteryIcon:case Setting::Bluetooth:draft=!draft;break;
          case Setting::Brightness:draft=draft>=100?10:draft+10;break;
          case Setting::MinRpm:draft=MinRpm::next(draft);break;
          case Setting::Theme:
            do draft=(draft+1)%Appearance::ALL_THEMES;
            while(draft>=Appearance::THEMES && !((customThemes>>(draft-Appearance::THEMES))&1u));
            break;
          case Setting::BestEffect:draft=uint8_t(EffectSet::next(Appearance::Effect(draft),offeredEffects));break;
          case Setting::Celebrate:draft=(draft+1)%3;break;
          case Setting::Language:draft=uint8_t((draft+1)%LANGUAGES);break;
          case Setting::Sound:draft=(draft+1)%3;break;
          case Setting::WipeAll:case Setting::About:return Effect::None;
        }return Effect::Preview;
      case Page::NewSession:startSession=!startSession;break;
      case Page::Tournament:return Effect::Tournament;
      case Page::Battery:break;
      case Page::Wipe:return Effect::CancelWipe;
      case Page::WearCheck:break;
      case Page::About:break;
      case Page::Bluetooth:bluetoothRow=(bluetoothRow+1)%4;break;
      case Page::BeybladeX:page=Page::Bluetooth;break;
      case Page::ForgetPhones:forgetChosen=!forgetChosen;break;
      case Page::EnableBluetooth:enableChosen=!enableChosen;break;
      case Page::Battles:battleRow=uint8_t((battleRow+1)%battleRows());break;
      case Page::Rival:break;
    }
    return Effect::None;
  }
  Effect holdB(bool recap=false) {
    switch(page) {
      case Page::Edit:page=Page::Settings;return Effect::CancelEdit;
      case Page::About:page=Page::Settings;break;
      case Page::History:page=Page::HistoryMenu;break;
      case Page::HistoryMenu:case Page::Battery:case Page::Settings:case Page::NewSession:case Page::WearCheck:case Page::Bluetooth:
      case Page::Battles:page=Page::Menu;break;
      case Page::Rival:page=Page::Battles;break;
      case Page::ForgetPhones:case Page::EnableBluetooth:case Page::BeybladeX:page=Page::Bluetooth;break;
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
// and About wakes on its Settings row. 0.20.9 adds Bluetooth to the Menu,
// which wakes on its row, and puts the Menu in a new order: an older state's
// row is found by what it was (MENU_ROWS_0208). Bluetooth is value 14 without
// a Settings row: Back moves to 15. Nine menu rows need a fourth bit, bit 14
// of that layout's magic (0x4e62, WAKE18: the Bluetooth prototype's builds
// used 0x4e60 and 0x4e61 for other layouts, which are not read). 0.21.0 adds
// Min RPM (15): Back moves to 16, which needs a fifth setting bit, bit 15
// (0x4e63, WAKE19, in the Min RPM builds before Battles). Battles is then
// inserted at menu row 3 under a new magic (0x4e64): a WAKE18 or WAKE19
// state's row is found by what it was (MENU_ROWS_0209). The Battles pages
// wake on their menu row. Settings rows are found by their value, so the
// 0.21.0 page order needs nothing else; older firmware rejects 0x4e64.
// Language (16) moves Back to 17 under 0x4e65; a 0x4e64 (WAKE20) state's
// Back is 16.
static constexpr uint32_t LEGACY_WAKE_MAGIC=0x4e560000,WAKE7_MAGIC=0x4e570000,
  WAKE8_MAGIC=0x4e580000,WAKE9_MAGIC=0x4e590000,WAKE10_MAGIC=0x4e5a0000,WAKE11_MAGIC=0x4e5b0000,
  WAKE12_MAGIC=0x4e5c0000,WAKE13_MAGIC=0x4e5d0000,WAKE14_MAGIC=0x4e5e0000,WAKE15_MAGIC=0x4e5f0000,WAKE18_MAGIC=0x4e620000,
  WAKE19_MAGIC=0x4e630000,WAKE20_MAGIC=0x4e640000,WAKE_MAGIC=0x4e650000;
inline uint32_t encodeWake(State state,uint8_t tournamentView) {
  if(state.page==Page::Analysis)state.page=Page::Main;
  if(state.page==Page::Edit || state.page==Page::Wipe)state.page=Page::Settings;
  if(state.page==Page::WearCheck){state.page=Page::Menu;state.menu=WEAR_ROW;}
  if(state.page==Page::About){state.page=Page::Settings;state.setting=uint8_t(Setting::About);}
  if(state.page==Page::Bluetooth || state.page==Page::ForgetPhones || state.page==Page::EnableBluetooth || state.page==Page::BeybladeX){state.page=Page::Menu;state.menu=BLUETOOTH_ROW;}
  if(state.page==Page::Battles || state.page==Page::Rival){state.page=Page::Menu;state.menu=BATTLES_ROW;}
  return WAKE_MAGIC|uint32_t(state.page)|(uint32_t(state.menu&7)<<4)|(uint32_t(state.menu>>3)<<14)|
    (uint32_t(state.history)<<7)|(uint32_t(state.setting&15)<<9)|(uint32_t(state.setting>>4)<<15)|(uint32_t(tournamentView)<<13);
}
inline bool decodeWake(uint32_t value,State &out,uint8_t &tournamentView) {
  State state;state.page=Page(value&15);state.history=(value>>7)&3;
  // Three layouts have a fourth menu bit: 0.20.9's (WAKE18) and the Min RPM
  // builds' (WAKE19) with nine rows, and this one with ten. The last two
  // also have a fifth setting bit.
  const uint32_t magic16=value&0xffff0000;
  const bool battles=magic16==WAKE20_MAGIC,current=magic16==WAKE_MAGIC || battles,minRpmRows=magic16==WAKE19_MAGIC,
    nineRows=minRpmRows || (value&0xffff8000)==WAKE18_MAGIC,fiveSettingBits=current || minRpmRows;
  state.menu=((value>>4)&7)|(current || nineRows?((value>>14)&1)<<3:0);
  // Each layout's Back row is the one after its last setting.
  unsigned setting=0,back=0;
  const auto wideMagic=current?WAKE_MAGIC:minRpmRows?WAKE19_MAGIC:nineRows?WAKE18_MAGIC:value&0xffffc000;
  if(wideMagic==WAKE_MAGIC || wideMagic==WAKE19_MAGIC || wideMagic==WAKE18_MAGIC || wideMagic==WAKE15_MAGIC || wideMagic==WAKE14_MAGIC || wideMagic==WAKE13_MAGIC || wideMagic==WAKE12_MAGIC || wideMagic==WAKE11_MAGIC || wideMagic==WAKE10_MAGIC || wideMagic==WAKE9_MAGIC || wideMagic==WAKE8_MAGIC) {
    setting=((value>>9)&15)|(fiveSettingBits?((value>>15)&1)<<4:0);tournamentView=(value>>13)&1;
    back=wideMagic==WAKE8_MAGIC?8:wideMagic==WAKE9_MAGIC?9:wideMagic==WAKE10_MAGIC?10:wideMagic==WAKE11_MAGIC?11:
      battles || minRpmRows?16:fiveSettingBits?SETTING_COUNT:wideMagic==WAKE18_MAGIC?15:wideMagic==WAKE15_MAGIC?14:12;  // 12: Back in 0.20.2-0.20.7 (WAKE12-WAKE14)
  }
  else {
    const auto magic=value&0xffffe000;
    if(magic!=WAKE7_MAGIC && magic!=LEGACY_WAKE_MAGIC)return false;
    back=magic==LEGACY_WAKE_MAGIC?5:7;
    setting=(value>>9)&7;tournamentView=(value>>12)&1;
  }
  if(setting>back)return false;
  if(setting==back)setting=SETTING_COUNT;
  else if(wideMagic!=WAKE_MAGIC && wideMagic!=WAKE19_MAGIC && wideMagic!=WAKE18_MAGIC && wideMagic!=WAKE15_MAGIC && wideMagic!=WAKE14_MAGIC && wideMagic!=WAKE13_MAGIC && wideMagic!=WAKE12_MAGIC) {
    // In the 0.15-0.20.1 numbering (WAKE11): Celebrate is 7, Wipe all 10.
    if(wideMagic!=WAKE11_MAGIC && setting>=7)++setting;
    if(setting==10)setting=unsigned(Setting::WipeAll);
  }
  // Bluetooth has no Settings row (its editor is reached by HIL alone): Back.
  if(setting==unsigned(Setting::Bluetooth))setting=SETTING_COUNT;
  state.setting=uint8_t(setting);
  const unsigned rows=current?MENU_COUNT:nineRows?9:wideMagic==WAKE15_MAGIC || wideMagic==WAKE14_MAGIC?8:wideMagic==WAKE13_MAGIC?7:6;
  if(uint8_t(state.page)>uint8_t(Page::Wipe) || state.page==Page::Edit || state.page==Page::Wipe ||
     state.menu>=rows || state.setting>SETTING_COUNT)return false;
  // Each older layout's last row was Back; its other rows are where they are now.
  if(nineRows)state.menu=uint8_t(MENU_ROWS_0209[state.menu]);
  else if(!current)state.menu=state.menu==rows-1?uint8_t(BACK_ROW):uint8_t(MENU_ROWS_0208[state.menu]);
  out=state;return true;
}
}
