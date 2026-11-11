#include "controls.h"
#include <cassert>
#include <iostream>
using namespace Controls;
int main() {
  State s;assert(s.autoRecap() && s.tapA()==Effect::Recap);
  assert(s.holdB(true)==Effect::Recap && s.holdB()==Effect::None);
  s.tapB();assert(s.page==Page::Menu && s.menu==0 && !s.autoRecap());
  s.tapA();assert(s.page==Page::HistoryMenu);
  for(unsigned view=0;view<3;++view) {
    s.history=view;s.tapA();assert(s.page==Page::History && s.tapB()==Effect::Older);
    s.holdB();assert(s.page==Page::HistoryMenu && s.history==view);
  }
  s.history=3;s.tapA();assert(s.page==Page::Menu);
  s.menu=NEW_SESSION_ROW;s.tapA();assert(s.page==Page::NewSession && !s.startSession);
  assert(s.tapA()==Effect::None && s.page==Page::Menu);
  s.tapA();s.tapB();assert(s.tapA()==Effect::NewSession && s.page==Page::Menu);
  // The Menu's order (0.20.9): a session's rows, the tools, the device, Back,
  // and B walks them top to bottom.
  static_assert(HISTORY_ROW==0 && NEW_SESSION_ROW==1 && TOURNAMENT_ROW==2 && WEAR_ROW==3 && MIC_ROW==4 && BATTERY_ROW==5 &&
                BLUETOOTH_ROW==6 && SETTINGS_ROW==7 && BACK_ROW==8 && MENU_COUNT==9);
  static_assert(MENU_GROUPS==((1u<<WEAR_ROW)|(1u<<BATTERY_ROW)|(1u<<BACK_ROW)));
  {State m;m.page=Page::Menu;for(unsigned row=0;row<MENU_COUNT;++row){assert(m.menu==row);m.tapB();}assert(m.menu==0);}
  s.menu=BATTERY_ROW;s.tapA();assert(s.page==Page::Battery);s.tapA();assert(s.page==Page::Menu && s.menu==BATTERY_ROW);
  s.menu=SETTINGS_ROW;s.tapA();assert(s.page==Page::Settings);
  // Every row but Wipe all and About has an editor, Battery icon (0.20.8) too.
  assert(!editable(unsigned(Setting::WipeAll)) && !editable(unsigned(Setting::About)) && editable(unsigned(Setting::BatteryIcon)));
  assert(!editable(SETTING_COUNT));
  // The corner battery shows on the Menu and Settings pages only, not Main (owner, 2026-11-05).
  for(unsigned page=0;page<=unsigned(Page::EnableBluetooth);++page) {
    const Page p=Page(page);
    const bool menus=p==Page::Menu || p==Page::HistoryMenu || p==Page::Settings || p==Page::Edit || p==Page::About;
    assert(showsCornerBattery(p)==menus);
  }
  assert(!showsCornerBattery(Page::Main) && !showsCornerBattery(Page::History) && !showsCornerBattery(Page::Battery));
  // Main shows it while the unit charges, and only Main adds that (owner, 2026-11-11).
  for(unsigned page=0;page<=unsigned(Page::EnableBluetooth);++page)assert(showsChargingBattery(Page(page))==(Page(page)==Page::Main));
  for(unsigned row=0;row<SETTING_COUNT;++row) {
    if(!editable(row))continue;
    s.setting=row;assert(s.tapA()==Effect::BeginEdit && s.page==Page::Edit);
    assert(s.tapA()==Effect::Apply && s.page==Page::Edit); // Backend must confirm success.
    assert(s.holdA()==Effect::None);assert(s.holdB()==Effect::CancelEdit && s.page==Page::Settings);
  }
  s.page=Page::Settings;s.setting=unsigned(Setting::WipeAll);
  assert(s.tapA()==Effect::BeginWipe && s.page==Page::Wipe);
  assert(s.tapA()==Effect::ConfirmWipe && s.holdA()==Effect::None);
  assert(s.tapB()==Effect::CancelWipe && s.holdB()==Effect::CancelWipe);
  s.page=Page::Settings; // The sketch consumes CancelWipe before navigating.
  // Battery icon (0.20.8) follows Mounting. About (0.20.8): after Wipe all and
  // before Back, it opens a page that edits nothing; A or hold B returns to its
  // row, B does nothing there.
  assert(nextRow(unsigned(Setting::Mount))==unsigned(Setting::BatteryIcon) && nextRow(unsigned(Setting::BatteryIcon))==unsigned(Setting::WipeAll));
  assert(nextRow(unsigned(Setting::WipeAll))==unsigned(Setting::About) && nextRow(unsigned(Setting::About))==SETTING_COUNT);
  assert(nextRow(SETTING_COUNT)==unsigned(Setting::Sleep) && position(unsigned(Setting::BatteryIcon))==11);
  assert(position(unsigned(Setting::WipeAll))==12 && position(unsigned(Setting::About))==13);
  s.setting=unsigned(Setting::About);
  assert(s.tapA()==Effect::None && s.page==Page::About && !s.autoRecap());
  assert(s.tapB()==Effect::None && s.holdA()==Effect::None && s.page==Page::About);
  assert(s.tapA()==Effect::None && s.page==Page::Settings && s.setting==unsigned(Setting::About));
  s.tapA();assert(s.holdB()==Effect::None && s.page==Page::Settings && s.setting==unsigned(Setting::About));
  s.setting=SETTING_COUNT;s.tapA();assert(s.page==Page::Menu);
  s.menu=WEAR_ROW;s.tapA();assert(s.page==Page::WearCheck && !s.autoRecap() && s.wear.totalAccepted()==0);
  assert(s.wear.observePull(1,true,3250,WearCheck::Tilt(true,1.0f,0.0f,0.0f))==WearCheck::Outcome::Accepted && s.wear.totalAccepted()==1);
  assert(s.tapB()==Effect::None && s.page==Page::WearCheck && s.wear.totalAccepted()==1);
  assert(s.tapA()==Effect::None && s.page==Page::WearCheck && s.wear.totalAccepted()==0);
  assert(s.holdB()==Effect::None && s.page==Page::Menu && s.menu==WEAR_ROW);
  s.menu=BACK_ROW;s.tapA();assert(s.page==Page::Main);s.page=Page::Menu;s.menu=WEAR_ROW;
  s.holdB();assert(s.page==Page::Main);
  assert(s.holdA(true)==Effect::None && s.page==Page::Main);
  assert(s.holdA()==Effect::Tournament && s.page==Page::Tournament && !s.autoRecap());
  assert(s.tapA()==Effect::None && s.holdB()==Effect::None && s.page==Page::Tournament);
  assert(s.tapB()==Effect::Tournament && s.page==Page::Tournament);
  assert(s.holdA()==Effect::Tournament && s.page==Page::Main);
  // Best effect cycles Random, the offered effects (the default set: the 16
  // of 0.16) and Off.
  for(unsigned row=0;row<SETTING_COUNT;++row) {
    if(!editable(row))continue;
    s.page=Page::Edit;s.setting=row;s.draft=row==0?3:row==2?40:0;
    const unsigned original=s.draft,cycle=row==0 || row==2?10:row==5?Appearance::THEMES:row==6?EffectSet::count(s.offeredEffects)+2:row==7 || row==8?3:2;
    for(unsigned i=0;i<cycle;++i)assert(s.tapB()==Effect::Preview);
    assert(s.draft==original);
  }
  // Every theme is reachable exactly once from every saved starting choice.
  for(unsigned start=0;start<Appearance::THEMES;++start) {
    s.page=Page::Edit;s.setting=unsigned(Setting::Theme);s.draft=start;
    unsigned visited=0;
    for(unsigned i=0;i<Appearance::THEMES;++i){visited|=1u<<s.draft;assert(s.tapB()==Effect::Preview);}
    assert(s.draft==start && visited==(1u<<Appearance::THEMES)-1);
  }
  // Hold/release must never execute both actions, including a synthetic
  // library release click and a new short press immediately afterwards.
  Gesture gesture;assert(!gesture.click(true,false,false));
  assert(gesture.click(false,false,true));
  assert(!gesture.click(true,false,false));assert(!gesture.click(false,true,false));
  assert(!gesture.click(false,false,true));assert(!gesture.click(true,false,false));
  assert(gesture.click(false,false,true));
  for(unsigned page=0;page<=unsigned(Page::EnableBluetooth);++page)
    for(unsigned menu=0;menu<MENU_COUNT;++menu)for(unsigned setting=0;setting<=SETTING_COUNT;++setting)for(uint8_t view=0;view<2;++view) {
      State before;before.page=Page(page);before.menu=menu;before.setting=setting;before.history=2;
      State after;uint8_t saved=99;
      assert(decodeWake(encodeWake(before,view),after,saved));
      // Wear check, Mic RPM and the Bluetooth pages wake on their
      // Menu rows (a fresh check, the mic off) and About on its Settings row:
      // no page value beyond Wipe is ever saved. Bluetooth's editor has no
      // Settings row, so it wakes on Back.
      const bool wear=before.page==Page::WearCheck,mic=before.page==Page::MicRpm,about=before.page==Page::About;
      const bool bluetooth=before.page==Page::Bluetooth || before.page==Page::ForgetPhones || before.page==Page::EnableBluetooth;
      assert(after.page==((before.page==Page::Edit || before.page==Page::Wipe || about)?Page::Settings:wear || mic || bluetooth?Page::Menu:before.page));
      assert(after.menu==(wear?WEAR_ROW:mic?MIC_ROW:bluetooth?BLUETOOTH_ROW:menu) && after.history==2 && saved==view);
      assert(after.setting==(about?unsigned(Setting::About):setting==unsigned(Setting::Bluetooth)?SETTING_COUNT:setting));
    }
  State out;uint8_t view=0;
  // Older menus were in the order their rows were added: History, New session,
  // Battery, Settings, Tournament, then String wear check (0.20.3) and Mic RPM
  // (0.20.7), with Back last. A saved row wakes on the same thing in 0.20.9's order.
  const uint8_t was[]={HISTORY_ROW,NEW_SESSION_ROW,BATTERY_ROW,SETTINGS_ROW,TOURNAMENT_ROW,WEAR_ROW,MIC_ROW};
  // 0.20.2 and earlier: six rows, Back at 5.
  for(uint32_t magic:{WAKE12_MAGIC,WAKE11_MAGIC,WAKE10_MAGIC,WAKE9_MAGIC,WAKE8_MAGIC,WAKE7_MAGIC,LEGACY_WAKE_MAGIC}) {
    for(unsigned row=0;row<5;++row)assert(decodeWake(magic|uint32_t(Page::Menu)|(row<<4),out,view) && out.page==Page::Menu && out.menu==was[row]);
    assert(decodeWake(magic|uint32_t(Page::Menu)|(5u<<4),out,view) && out.menu==BACK_ROW);
    assert(!decodeWake(magic|uint32_t(Page::Menu)|(6u<<4),out,view));
  }
  // 0.20.3-0.20.5 (WAKE13): seven rows, Back at 6.
  for(unsigned row=0;row<6;++row)assert(decodeWake(WAKE13_MAGIC|uint32_t(Page::Menu)|(row<<4),out,view) && out.menu==was[row]);
  assert(decodeWake(WAKE13_MAGIC|uint32_t(Page::Menu)|(6u<<4),out,view) && out.menu==BACK_ROW);
  assert(!decodeWake(WAKE13_MAGIC|uint32_t(Page::Menu)|(7u<<4),out,view));
  assert(decodeWake(WAKE13_MAGIC|uint32_t(Page::Settings)|(10u<<9),out,view) && out.setting==10);
  // 0.20.7 (WAKE14) and 0.20.8 (WAKE15): eight rows, Back at 7.
  for(uint32_t magic:{WAKE14_MAGIC,WAKE15_MAGIC}) {
    for(unsigned row=0;row<7;++row)assert(decodeWake(magic|uint32_t(Page::Menu)|(row<<4),out,view) && out.menu==was[row]);
    assert(decodeWake(magic|uint32_t(Page::Menu)|(7u<<4),out,view) && out.menu==BACK_ROW);
  }
  // 0.20.9's own rows are saved as they are.
  for(unsigned row=0;row<8;++row)assert(decodeWake(WAKE_MAGIC|uint32_t(Page::Menu)|(row<<4),out,view) && out.menu==row);
  // 0.20.7 (WAKE14): Settings Back at 12, where About is now: it still opens Back.
  assert(decodeWake(WAKE14_MAGIC|uint32_t(Page::Settings)|(12u<<9)|(1u<<13),out,view) && out.setting==SETTING_COUNT && view==1);
  assert(decodeWake(WAKE14_MAGIC|uint32_t(Page::Settings)|(11u<<9),out,view) && out.setting==unsigned(Setting::WipeAll));
  // 0.20.8 (WAKE15): About and Battery icon, Back at 14.
  assert(decodeWake(WAKE15_MAGIC|uint32_t(Page::Settings)|(12u<<9),out,view) && out.setting==unsigned(Setting::About));
  assert(decodeWake(WAKE15_MAGIC|uint32_t(Page::Settings)|(13u<<9),out,view) && out.setting==unsigned(Setting::BatteryIcon));
  assert(decodeWake(WAKE15_MAGIC|uint32_t(Page::Settings)|(14u<<9)|(1u<<13),out,view) && out.setting==SETTING_COUNT && view==1);
  assert(!decodeWake(WAKE15_MAGIC|uint32_t(Page::Settings)|(15u<<9),out,view));
  assert(!decodeWake(WAKE15_MAGIC|0x4000u|uint32_t(Page::Menu),out,view));
  // 0.20.9: the same Settings values, Back at 15; Bluetooth (14) has no row and opens Back.
  assert(decodeWake(WAKE_MAGIC|uint32_t(Page::Settings)|(12u<<9),out,view) && out.setting==unsigned(Setting::About));
  assert(decodeWake(WAKE_MAGIC|uint32_t(Page::Settings)|(13u<<9),out,view) && out.setting==unsigned(Setting::BatteryIcon));
  assert(decodeWake(WAKE_MAGIC|uint32_t(Page::Settings)|(14u<<9),out,view) && out.setting==SETTING_COUNT);
  assert(decodeWake(WAKE_MAGIC|uint32_t(Page::Settings)|(15u<<9),out,view) && out.setting==SETTING_COUNT);
  // Nine menu rows: Back, row 8, takes bit 14, this layout's alone.
  {
    State menu;menu.page=Page::Menu;uint8_t saved=0;
    menu.menu=BACK_ROW;{const uint32_t code=encodeWake(menu,0);assert(code&0x4000u);
      assert(decodeWake(code,out,saved) && out.page==Page::Menu && out.menu==BACK_ROW);}
    menu.menu=7;assert(!(encodeWake(menu,0)&0x4000u) && decodeWake(encodeWake(menu,0),out,saved) && out.menu==7);
    assert(!decodeWake(WAKE_MAGIC|0x4000u|(1u<<4),out,saved)); // Row 9 does not exist.
    assert(!decodeWake(WAKE_MAGIC|0x8000u,out,saved));
    // The Bluetooth prototype's own layouts (0.20.5-sticks3-ble) are not read.
    assert(!decodeWake(0x4e600000u|uint32_t(Page::Menu),out,saved) && !decodeWake(0x4e610000u|uint32_t(Page::Menu),out,saved));
  }
  // The Bluetooth page: from its menu row; B walks On/Off, Forget phones, Back.
  // Off, A on its row asks first: it is experimental, Cancel is chosen, and only
  // Turn on with A turns it on. On, A turns it off at one press. Hold B returns;
  // Forget takes a choice, and comes back here.
  {
    State p;p.page=Page::Menu;p.menu=BLUETOOTH_ROW;uint8_t saved=0;
    assert(p.tapA()==Effect::None && p.page==Page::Bluetooth && p.bluetoothRow==0 && !p.autoRecap());
    assert(p.tapA()==Effect::None && p.page==Page::EnableBluetooth && !p.enableChosen);
    assert(decodeWake(encodeWake(p,0),out,saved) && out.page==Page::Menu && out.menu==BLUETOOTH_ROW);
    assert(p.tapA()==Effect::None && p.page==Page::Bluetooth); // Cancel by default.
    p.tapA();assert(p.tapB()==Effect::None && p.enableChosen);
    assert(p.holdB()==Effect::None && p.page==Page::Bluetooth); // Hold B never turns it on.
    p.tapA();assert(!p.enableChosen);p.tapB();p.tapB();assert(!p.enableChosen);p.tapB();
    assert(p.tapA()==Effect::ToggleBluetooth && p.page==Page::Bluetooth);
    p.bluetoothOn=true; // The sketch keeps it current.
    assert(p.tapA()==Effect::ToggleBluetooth && p.page==Page::Bluetooth); // Off needs no notice.
    // Already on (the phone cannot do this, but a stale page could): no second toggle.
    p.page=Page::EnableBluetooth;p.enableChosen=true;assert(p.tapA()==Effect::None && p.page==Page::Bluetooth);
    assert(!decodeWake(WAKE_MAGIC|uint32_t(Page::Bluetooth),out,saved) && !decodeWake(WAKE_MAGIC|uint32_t(Page::ForgetPhones),out,saved));
    assert(decodeWake(encodeWake(p,0),out,saved) && out.page==Page::Menu && out.menu==BLUETOOTH_ROW);
    assert(p.tapB()==Effect::None && p.bluetoothRow==1);
    assert(p.tapA()==Effect::None && p.page==Page::ForgetPhones && !p.forgetChosen);
    assert(decodeWake(encodeWake(p,0),out,saved) && out.page==Page::Menu && out.menu==BLUETOOTH_ROW);
    assert(p.tapA()==Effect::None && p.page==Page::Bluetooth); // Cancel by default.
    p.tapA();assert(p.tapB()==Effect::None && p.forgetChosen);
    assert(p.holdB()==Effect::None && p.page==Page::Bluetooth); // Hold B never forgets.
    p.tapA();assert(!p.forgetChosen);p.tapB();
    assert(p.tapA()==Effect::ForgetPhones && p.page==Page::Bluetooth);
    assert(p.tapB()==Effect::None && p.bluetoothRow==2);
    assert(p.tapA()==Effect::None && p.page==Page::Menu);
    p.tapA();assert(p.holdB()==Effect::None && p.page==Page::Menu);
    // Not a Settings row; its value is the protocol's id and stays.
    static_assert(position(unsigned(Setting::Bluetooth))==ROWS && unsigned(Setting::Bluetooth)==14);
    // Every row is reached in ORDER, once.
    unsigned row=0,seen=0;do{row=nextRow(row);++seen;}while(row!=0 && seen<40);assert(seen==ROWS+1);
  }
  // 0.20.2 shares this settings numbering.
  assert(decodeWake(WAKE12_MAGIC|uint32_t(Page::Settings)|(10u<<9),out,view) && out.setting==unsigned(Setting::ShakeWake));
  assert(decodeWake(WAKE12_MAGIC|uint32_t(Page::Settings)|(12u<<9)|(1u<<13),out,view) && out.setting==SETTING_COUNT && view==1);
  assert(decodeWake(LEGACY_WAKE_MAGIC|uint32_t(Page::Settings)|(5u<<9),out,view) && out.setting==SETTING_COUNT);
  // 0.11 layout: seven settings, Back at row 7, view at bit 12.
  assert(decodeWake(WAKE7_MAGIC|uint32_t(Page::Settings)|(7u<<9)|(1u<<12),out,view) && out.setting==SETTING_COUNT && view==1);
  assert(decodeWake(WAKE7_MAGIC|uint32_t(Page::Settings)|(6u<<9),out,view) && out.setting==6 && view==0);
  // 0.12 Back must stay Back after inserting Mount at row 8.
  assert(decodeWake(WAKE8_MAGIC|uint32_t(Page::Settings)|(8u<<9)|(1u<<13),out,view) && out.setting==SETTING_COUNT && view==1);
  // Rows from Sound on moved down one when Celebrate was inserted after Best effect.
  assert(decodeWake(WAKE8_MAGIC|uint32_t(Page::Settings)|(7u<<9),out,view) && out.setting==unsigned(Setting::Sound) && view==0);
  assert(decodeWake(WAKE8_MAGIC|uint32_t(Page::Settings)|(6u<<9),out,view) && out.setting==unsigned(Setting::BestEffect));
  assert(decodeWake(WAKE_MAGIC|uint32_t(Page::Settings)|(9u<<9),out,view) && out.setting==unsigned(Setting::Mount));
  assert(decodeWake(WAKE_MAGIC|uint32_t(Page::Settings)|(7u<<9),out,view) && out.setting==unsigned(Setting::Celebrate));
  assert(decodeWake(WAKE9_MAGIC|uint32_t(Page::Settings)|(9u<<9)|(1u<<13),out,view) && out.setting==SETTING_COUNT && view==1);
  assert(decodeWake(WAKE9_MAGIC|uint32_t(Page::Settings)|(8u<<9),out,view) && out.setting==unsigned(Setting::Mount));
  // Before Bluetooth: Wipe all at row 11 and Back at row 12; rows before them keep their numbers.
  assert(decodeWake(WAKE13_MAGIC|uint32_t(Page::Settings)|(10u<<9),out,view) && out.setting==unsigned(Setting::ShakeWake));
  assert(decodeWake(WAKE13_MAGIC|uint32_t(Page::Settings)|(11u<<9),out,view) && out.setting==unsigned(Setting::WipeAll));
  assert(decodeWake(WAKE13_MAGIC|uint32_t(Page::Settings)|(12u<<9),out,view) && out.setting==SETTING_COUNT);
  assert(decodeWake(WAKE12_MAGIC|uint32_t(Page::Settings)|(11u<<9),out,view) && out.setting==unsigned(Setting::WipeAll));
  // 0.13.9-0.14: Wipe all at row 9 and Back at row 10.
  assert(decodeWake(WAKE10_MAGIC|uint32_t(Page::Settings)|(7u<<9),out,view) && out.setting==unsigned(Setting::Sound));
  assert(decodeWake(WAKE10_MAGIC|uint32_t(Page::Settings)|(8u<<9),out,view) && out.setting==unsigned(Setting::Mount));
  assert(decodeWake(WAKE10_MAGIC|uint32_t(Page::Settings)|(9u<<9),out,view) && out.setting==unsigned(Setting::WipeAll));
  assert(decodeWake(WAKE10_MAGIC|uint32_t(Page::Settings)|(10u<<9)|(1u<<13),out,view) && out.setting==SETTING_COUNT && view==1);
  assert(decodeWake(WAKE10_MAGIC|uint32_t(Page::Settings)|(5u<<9),out,view) && out.setting==unsigned(Setting::Theme));
  for(uint32_t corrupt:{0u,WAKE_MAGIC|15u,WAKE_MAGIC|uint32_t(Page::Edit),WAKE13_MAGIC|(7u<<4),LEGACY_WAKE_MAGIC|(7u<<9),
                        WAKE14_MAGIC|0x4000u,WAKE14_MAGIC|(13u<<9),WAKE11_MAGIC|(12u<<9),WAKE10_MAGIC|(11u<<9),WAKE8_MAGIC|(9u<<9),WAKE9_MAGIC|(10u<<9),
                        WAKE_MAGIC|uint32_t(Page::Wipe),WAKE9_MAGIC|uint32_t(Page::Wipe),WAKE7_MAGIC|0x2000u,
                        WAKE_MAGIC|uint32_t(Page::WearCheck),WAKE12_MAGIC|uint32_t(Page::WearCheck),WAKE12_MAGIC|(7u<<4),
                        WAKE_MAGIC|uint32_t(Page::About),WAKE14_MAGIC|uint32_t(Page::MicRpm)})
    assert(!decodeWake(corrupt,out,view));
  std::cout<<"PASS: consistent actions, visible menu/back, deliberate sessions, staged settings, tournament lock, hold release, wake round trips and corruption\n";
}
