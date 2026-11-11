// The Bluetooth setting's record (bluetooth_setting.h), its row and its save
// item. Its place in the USB backup is covered by test_device_backup.
#include "bluetooth_setting.h"
#include "controls.h"
#include "settings_save.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
  // No key is Off, and a load writes nothing; 0 and 1 load; anything else is
  // refused, reads as Off and is left as it is.
  {
    Preferences prefs;Preferences::data.clear();assert(prefs.begin("launchlab-mini",false));
    static_assert(!BluetoothSetting::DEFAULT_ON);
    bool on=true;assert(BluetoothSetting::load(prefs,on) && !on && Preferences::data.empty());
    assert(BluetoothSetting::save(prefs,true) && Preferences::data["launchlab-minibluetooth"]==std::vector<unsigned char>{1});
    on=false;assert(BluetoothSetting::load(prefs,on) && on);
    assert(BluetoothSetting::save(prefs,false) && Preferences::data["launchlab-minibluetooth"]==std::vector<unsigned char>{0});
    on=true;assert(BluetoothSetting::load(prefs,on) && !on);
    for(uint8_t bad:{2,7,255}) {
      Preferences::data["launchlab-minibluetooth"]={bad};on=true;
      assert(!BluetoothSetting::load(prefs,on) && !on && Preferences::data["launchlab-minibluetooth"]==std::vector<unsigned char>{bad});
    }
    // A failed write is reported, not trusted.
    Preferences::failWrite=true;assert(!BluetoothSetting::save(prefs,true));Preferences::failWrite=false;
  }
  // The row toggles like Flip display and saves its own item. Older rows keep
  // their values (HIL `!set N`, saved wake navigation).
  {
    using Controls::Setting;
    // A change applies at a restart; off at boot keeps stock's internal canvas.
    static_assert(BluetoothSetting::restartNeeded(true,false) && BluetoothSetting::restartNeeded(false,true));
    static_assert(!BluetoothSetting::restartNeeded(true,true) && !BluetoothSetting::restartNeeded(false,false));
    static_assert(BluetoothSetting::canvasInPsram(true) && !BluetoothSetting::canvasInPsram(false));
    static_assert(unsigned(Setting::ShakeWake)==10 && unsigned(Setting::WipeAll)==11 && unsigned(Setting::Bluetooth)==14);
    assert(SettingsSave::item(unsigned(Setting::Bluetooth))==SettingsSave::Bluetooth);
    assert(SettingsSave::item(unsigned(Setting::WipeAll))==0);
    // Its editor stays for HIL's `!set`; the screen toggles it on its own page.
    Controls::State s;s.page=Controls::Page::Edit;s.setting=uint8_t(Setting::Bluetooth);
    s.draft=0;assert(s.tapB()==Controls::Effect::Preview && s.draft==1);s.tapB();assert(s.draft==0);
  }
  std::cout<<"PASS: Bluetooth: Off without a saved choice, 0/1 records, others refused and kept, failed writes reported, own row and save item\n";
}
