// Shake to wake (0.20.2, shake_wake_setting.h): the setting's record, the
// sleep's wake source by it, and its place in the USB backup, including what
// the frozen 0.20.1 source does with a backup that has it Off.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#if defined(__GNUC__) && !defined(__clang__)
#error "test_shake_wake needs clang (GCC merges byte-identical #pragma once headers)"
#endif
#include "../LaunchLabMini/shake_wake_setting.h"
#include "../LaunchLabMini/device_backup.h"
#include "../LaunchLabMini/controls.h"
namespace Released0201 {
#include "../releases/0.20.1-sticks3-songs-pm/source/device_backup.h"
}
#include <cassert>
#include <iostream>
#include <string>

static DeviceBackup::Contents sample(bool shake) {
  DeviceBackup::Contents c;
  for(uint32_t i=1;i<=12;++i)c.practice.accept(i,true,5000+i*37,i*20000);
  c.look.select(Appearance::Theme::Amber,Appearance::Effect::Lightning);
  c.bests.observe(0,0,true,8800);
  c.settings={7,60,RpmEstimator::Mode::SingleTurn,StickS3SensorProfile::Mode::Tcrt,true,MountOrientation::Mode::Top,shake};
  return c;
}

int main() {
  // The record: no key is On (as before 0.20.2); 0 and 1 load; anything else
  // is refused, reads as On and is left as it is.
  {
    Preferences prefs;Preferences::data.clear();assert(prefs.begin("launchlab-mini",false));
    bool on=false;assert(ShakeWakeSetting::load(prefs,on) && on && Preferences::data.empty());
    static_assert(ShakeWakeSetting::DEFAULT_ON && InactivityTimer::SHAKE_WAKE_DEFAULT);
    assert(ShakeWakeSetting::save(prefs,false) && Preferences::data["launchlab-minishake_wake"]==std::vector<unsigned char>{0});
    on=true;assert(ShakeWakeSetting::load(prefs,on) && !on);
    assert(ShakeWakeSetting::save(prefs,true));on=false;assert(ShakeWakeSetting::load(prefs,on) && on);
    for(uint8_t bad:{2,7,255}) {
      Preferences::data["launchlab-minishake_wake"]={bad};on=false;
      assert(!ShakeWakeSetting::load(prefs,on) && on && Preferences::data["launchlab-minishake_wake"]==std::vector<unsigned char>{bad});
    }
    // A failed write is reported, not trusted.
    Preferences::failWrite=true;assert(!ShakeWakeSetting::save(prefs,false));Preferences::failWrite=false;
  }
  // The wake source: On arms the IMU's motion wake; Off sleeps as the side
  // key's power-off does and only the side key wakes.
  {
    constexpr auto on=sleepPlan(true),off=sleepPlan(false);
    static_assert(on.armImu && !off.armImu);
    assert(std::string(on.wake)=="imu_motion" && std::string(on.mode)=="imu_only");
    assert(std::string(off.wake)=="side_key" && std::string(off.mode)=="power_off");
  }
  // Settings: Shake to wake is listed right after Sleep after, toggles like
  // Flip display, and every other row keeps its value (HIL `!set N`).
  {
    using Controls::Setting;
    static_assert(unsigned(Setting::Sleep)==0 && unsigned(Setting::Mount)==9 && unsigned(Setting::ShakeWake)==10 && unsigned(Setting::WipeAll)==11);
    static_assert(Controls::ORDER[0]==Setting::Sleep && Controls::ORDER[1]==Setting::ShakeWake);
    Controls::State s;s.page=Controls::Page::Settings;s.setting=0;
    unsigned seen=0;
    for(unsigned i=0;i<=Controls::SETTING_COUNT;++i){seen|=1u<<s.setting;s.tapB();}
    assert(s.setting==0 && seen==(1u<<(Controls::SETTING_COUNT+1))-1);  // every row and Back, once around
    s.setting=0;s.tapB();assert(s.setting==unsigned(Setting::ShakeWake));
    assert(s.tapA()==Controls::Effect::BeginEdit && s.page==Controls::Page::Edit);
    s.draft=1;assert(s.tapB()==Controls::Effect::Preview && s.draft==0);s.tapB();assert(s.draft==1);
    // A wake from a 0.20.1 settings list (WAKE11): Wipe all (10) and Back (11) keep their rows.
    Controls::State out;uint8_t view=0;
    auto old=[&](unsigned setting){return Controls::WAKE11_MAGIC|uint32_t(Controls::Page::Settings)|(3u<<4)|(setting<<9);};
    assert(Controls::decodeWake(old(10),out,view) && out.setting==unsigned(Setting::WipeAll));
    assert(Controls::decodeWake(old(11),out,view) && out.setting==Controls::SETTING_COUNT);
    assert(Controls::decodeWake(old(4),out,view) && out.setting==unsigned(Setting::Sensor));
    s.page=Controls::Page::Settings;s.setting=uint8_t(Setting::ShakeWake);
    assert(Controls::decodeWake(Controls::encodeWake(s,0),out,view) && out.setting==unsigned(Setting::ShakeWake));
  }
  // The backup: byte 6 of the settings section is 1 for Off. On writes the
  // same bytes as before 0.20.2, so older backups restore as On and the frozen
  // 0.20.1 source restores an On backup; it refuses an Off one before writing
  // anything, and nothing but 0 and 1 is accepted there.
  {
    using namespace DeviceBackup;
    for(bool shake:{true,false}) {
      auto c=std::make_unique<Contents>(sample(shake));auto buffer=std::make_unique<Buffer>();
      const size_t n=encode(*c,"0.20.2-sticks3-test",*buffer);
      auto back=std::make_unique<Contents>();assert(decode(buffer->data(),n,*back) && back->settings.shakeWake==shake);
      auto old=std::make_unique<Released0201::DeviceBackup::Contents>();
      assert(Released0201::DeviceBackup::decode(buffer->data(),n,*old)==shake);
      if(shake) {
        // Byte for byte what 0.20.1 writes for the same contents.
        auto mine=std::make_unique<Released0201::DeviceBackup::Contents>();
        for(uint32_t i=1;i<=12;++i)mine->practice.accept(i,true,5000+i*37,i*20000);
        mine->look.select(Released0201::Appearance::Theme::Amber,Released0201::Appearance::Effect::Lightning);
        mine->bests.observe(0,0,true,8800);
        mine->settings={7,60,Released0201::RpmEstimator::Mode::SingleTurn,Released0201::StickS3SensorProfile::Mode::Tcrt,true};
        auto theirs=std::make_unique<Released0201::DeviceBackup::Buffer>();
        const size_t m=Released0201::DeviceBackup::encode(*mine,"0.20.2-sticks3-test",*theirs);
        assert(m==n && std::memcmp(theirs->data(),buffer->data(),n)==0);
      }
    }
    // A value other than 0 or 1 in byte 6 is refused whole.
    auto c=std::make_unique<Contents>(sample(false));auto buffer=std::make_unique<Buffer>();
    const size_t n=encode(*c,"0.20.2-sticks3-test",*buffer);
    size_t at=HEADER_BYTES;while(at<n && buffer->data()[at]!=Settings)at+=8+get32(buffer->data()+at+4);
    assert(at<n && buffer->data()[at+8+6]==1);
    buffer->data()[at+8+6]=2;put32(buffer->data()+n-4,checksum(buffer->data(),n-4));
    auto refused=std::make_unique<Contents>();assert(!decode(buffer->data(),n,*refused));
  }
  std::cout<<"PASS: Shake to wake: On without a saved choice, 0/1 records, others refused and kept; On arms the IMU wake, Off sleeps "
             "as the side key's power-off; listed after Sleep after with stable row values and 0.20.1 wake rows; backup byte 6 "
             "round-trips, On backups are 0.20.1's bytes and 0.20.1 refuses an Off backup\n";
}
