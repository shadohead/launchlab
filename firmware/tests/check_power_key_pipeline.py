#!/usr/bin/env python3
"""Execute the exact production side-key click and power-off functions under bounded fakes."""
import pathlib
import re
import subprocess
import tempfile

firmware=pathlib.Path(__file__).resolve().parents[1]
source=(firmware/'LaunchLabMini/LaunchLabMini.ino').read_text()
def body(start):
    cursor=source.index('{',start)+1
    depth=1
    while depth:
        depth+=(source[cursor]=='{')-(source[cursor]=='}')
        cursor+=1
    return source[start:cursor].replace('LOOP_HELPER ','')
def function(name):
    return body(source.index('static void '+name+'('))
def shutdown_flush():
    enum=source.index('enum class ShutdownFlush')
    return source[enum:source.index('};',enum)+2]+'\n'+body(source.index('static ShutdownFlush flushForShutdown('))
wait=re.search(r'static constexpr uint32_t POWER_KEY_WAIT_MS=\d+;',source).group()

prefix=r'''
#include "history_checkpoint.h"
#include "power_status.h"
#include "practice_store.h"
#include "appearance_store.h"
#include "power_diagnostics.h"
#include "controls.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <string>
static Appearance::Bests personalBests;static AppearanceStore<Appearance::Bests> bestStore;
static Controls::State controls;static bool wipeBlocked=false;
static uint64_t clockUs=1000000;
static uint32_t millis(){return uint32_t(clockUs/1000);}
static int64_t wallUs(){return clockUs;}
struct SerialFake {std::string last;void println(const char *s){last=s;}void flush(){}
  template<class... A>void printf(const char *f,A...){last=f;}} Serial;
// USB output is drained (never Serial.flush(), which discards it): usb_tx.h.
namespace UsbTx {inline unsigned drains=0;inline bool drain(uint32_t){++drains;return true;}}

static InactivityTimer inactivity;static PracticeHistory practice;static PracticeStore practiceStore;
static bool motionPending=false,saveError=false,dirty=false,mutexAvailable=true,irqLine=true;
static uint32_t lastSaveTry=0,saveErrors=0,historyPendingSince=0;
static unsigned locks=0,unlocks=0,keyReads=0,wipeCancels=0,editCancels=0,powerSaves=0;
static PowerDiagnostics::Image powerLog;static bool powerLogDirty=false;
static volatile bool powerKeyIrq=false;static bool powerKeyRequested=false;
static uint32_t powerKeyRequestedAt=0,lastPowerKeyRead=0;
'''+wait+r'''
static constexpr uint8_t POWER_KEY_IRQ_PIN=13;
static int digitalRead(uint8_t pin){assert(pin==13);return irqLine;}
struct Key {bool pending=false;bool takeClick(){assert(locks>unlocks);++keyReads;const bool c=pending;pending=false;irqLine=true;return c;}};
static Key key;static Key &powerKey(){return key;}
enum class Action {ShutdownBegin,CheckpointEnd,Pin};
struct Snapshot {AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;uint32_t launches=0;bool valid=true;float resultRpm=6000;uint8_t pin=1;};
struct Acquisition {
  Snapshot state;bool suspended=false;unsigned begins=0,resumes=0;std::deque<Snapshot> events;
  Snapshot snapshot(){return state;}
  bool takeEvent(Snapshot &e){if(events.empty())return false;e=events.front();events.pop_front();return true;}
  bool request(Action action,uint32_t expected=0){
    if(action==Action::CheckpointEnd || action==Action::Pin){suspended=false;++resumes;return true;}
    if(state.phase==AnalogTachometer::Phase::Launch || expected!=state.launches)return false;
    suspended=true;++begins;return true;
  }
} acquisition;
struct Imu {bool lock(){++locks;if(!mutexAvailable){++unlocks;return false;}return true;}void unlock(){++unlocks;}} imuAcquisition;
struct M5Fake {struct DisplayFake {unsigned brightness=100;bool asleep=false;
  void setBrightness(unsigned b){brightness=b;}void sleep(){asleep=true;}void wakeup(){asleep=false;}} Display;} M5;
static uint8_t backlightValue(){return 100;}
static void powerRecord(uint8_t event){PowerDiagnostics::append(powerLog,event,3900,PowerDiagnostics::ChargeKnown,40,80);powerLogDirty=true;}
static bool savePowerLog(){++powerSaves;powerLog.checksum=PowerDiagnostics::hash(powerLog);powerLogDirty=false;return true;}
struct Receipt {uint32_t magic=0;int64_t atUs=0;} sleepReceipt;
struct PoweredOff {};
struct ShakeWake {unsigned arms=0,sleeps=0;bool off=true;
  bool arm(){++arms;return true;}
  bool sleep(){
    // Everything a player would miss is durable and the screen is dark first.
    assert(locks>unlocks && !practiceStore.pending(practice) && !bestStore.pending(personalBests));
    assert(!M5.Display.brightness && M5.Display.asleep && acquisition.suspended && powerLog.pendingSleep && sleepReceipt.magic);
    ++sleeps;if(off)throw PoweredOff{};return false;
  }
} shakeWake;
static unsigned restoreFailureNotes=0;static void noteMotionRestoreFailure(){++restoreFailureNotes;}
static void prepareMotion(const Snapshot&){motionPending=true;}
static void logLaunchMetrics(const Snapshot&){}static void noteStartMark(const Snapshot&){}static void saveStartMarks(){}
// Settings persistence has its own production harness (check_save_paths.py).
static void writeSettings(){}
static void cancelSettingEdit(){++editCancels;}
static void cancelWipe(){++wipeCancels;controls.page=Controls::Page::Settings;}
'''
tests=r'''
static void at(uint32_t now){clockUs=uint64_t(now)*1000;}
static void reset(){
  controls={};wipeBlocked=false;clockUs=1000000;
  inactivity=InactivityTimer{};inactivity.setMinutes(3);inactivity.touch(millis());
  practice=PracticeHistory{};practiceStore=PracticeStore{};bestStore=AppearanceStore<Appearance::Bests>{};personalBests={};
  motionPending=saveError=dirty=false;mutexAvailable=irqLine=true;lastSaveTry=saveErrors=historyPendingSince=0;
  locks=unlocks=keyReads=wipeCancels=editCancels=powerSaves=0;
  PowerDiagnostics::fresh(powerLog);powerLogDirty=false;sleepReceipt={};
  powerKeyIrq=powerKeyRequested=false;powerKeyRequestedAt=lastPowerKeyRead=0;key=Key{};
  acquisition=Acquisition{};M5=M5Fake{};shakeWake=ShakeWake{};Serial.last.clear();
  Preferences::data.clear();Preferences::zeroLengthKeys.clear();Preferences::failWrite=Preferences::shortRead=false;Preferences::beforeWrite=nullptr;
  assert(practiceStore.begin(practice) && bestStore.begin(personalBests,"ll-bests","best0","best1"));
}
static void click(){key.pending=true;irqLine=false;powerKeyIrq=true;}
// One loop pass: poll the key, then act, exactly as loop() orders them.
static bool step(){
  pollPowerKey(millis());
  try{powerKeyOff(millis(),acquisition.snapshot());}catch(PoweredOff&){return true;}
  return false;
}
static void unsavedPull(){assert(practice.accept(1,true,6000,millis()));acquisition.state.launches=1;assert(practiceStore.pending(practice));}
int main(){
  reset();unsavedPull();click();
  assert(step() && shakeWake.sleeps==1 && !shakeWake.arms && editCancels==1);
  assert(powerLog.sleeps==1 && powerLog.count==1 && powerLog.records[0].event==PowerDiagnostics::Sleep);
  PracticeHistory reboot;PracticeStore reader;assert(reader.begin(reboot) && reboot.size()==1 && reboot.recent()->rpm==6000);
  assert(locks==unlocks+1); // The power-off call holds the bus until the unit is dark.
  std::cout<<"PASS: single click saves the unsaved pull, darkens the screen and powers off without arming shake wake\n";

  reset();click();acquisition.state.phase=AnalogTachometer::Phase::Launch;
  for(uint32_t t=1000;t<4000;t+=5){at(t);assert(!step());}
  assert(powerKeyRequested && !acquisition.begins && keyReads==1);
  acquisition.state.phase=AnalogTachometer::Phase::Ready;at(4000);assert(step());
  std::cout<<"PASS: a click during a measured burst waits for the burst, then powers off\n";

  reset();click();motionPending=true;
  for(uint32_t t=1000;t<3000;t+=5){at(t);assert(!step());}
  motionPending=false;at(3000);assert(step());
  reset();click();acquisition.events.push_back(Snapshot{AnalogTachometer::Phase::Ready,1,true,7000,1});
  assert(!step() && motionPending && powerKeyRequested && acquisition.resumes==1 && !acquisition.suspended);
  motionPending=false;acquisition.state.launches=1;at(1500);assert(step());
  assert(reader.begin(reboot) && reboot.size()==1 && reboot.recent()->rpm==7000);
  std::cout<<"PASS: capture in progress or a pull queued at the click is finished and saved before power-off\n";

  reset();click();acquisition.state.phase=AnalogTachometer::Phase::Launch;
  for(uint32_t t=1000;t<=1000+POWER_KEY_WAIT_MS;t+=5){at(t);assert(!step());}
  assert(!powerKeyRequested && !shakeWake.sleeps && !acquisition.suspended);
  std::cout<<"PASS: an acquisition that never becomes idle drops the request instead of powering off later\n";

  reset();unsavedPull();click();Preferences::failWrite=true;
  assert(!step() && !powerKeyRequested && saveError && !acquisition.suspended && !shakeWake.sleeps);
  assert(practiceStore.pending(practice) && M5.Display.brightness==100 && !M5.Display.asleep && !powerLog.sleeps);
  std::cout<<"PASS: a failed save keeps the unit on with sampling resumed and the pull still pending\n";

  reset();shakeWake.off=false;click();
  assert(!step() && shakeWake.sleeps==1 && !acquisition.suspended && M5.Display.brightness==100 && !M5.Display.asleep);
  assert(!powerLog.pendingSleep && !sleepReceipt.magic && dirty && locks==unlocks);
  std::cout<<"PASS: PM1 refusing power-off restores display and sampling\n";

  reset();wipeBlocked=true;click();assert(!step() && !powerKeyRequested && !acquisition.begins);
  reset();controls.page=Controls::Page::Wipe;click();assert(step() && wipeCancels==1);
  std::cout<<"PASS: clicks are ignored during an erase; an unconfirmed wipe prompt is cancelled before power-off\n";

  reset();mutexAvailable=false;click();assert(!step() && powerKeyIrq && !keyReads && !powerKeyRequested);
  mutexAvailable=true;at(1005);assert(step());
  std::cout<<"PASS: a busy I2C gate keeps the latched edge for the next pass\n";

  // No click: the idle line costs no I2C. A missed edge with the line held
  // low is read at most once a second, so PM1 idle sleep is preserved.
  reset();for(uint32_t t=1000;t<60000;t+=5){at(t);assert(!step());}
  assert(!keyReads && !locks);
  reset();irqLine=false;for(uint32_t t=1000;t<11000;t+=5){at(t);irqLine=false;assert(!step());}
  assert(keyReads==10 && !powerKeyRequested);
  reset();irqLine=false;key.pending=true;at(5000);assert(step());
  std::cout<<"PASS: idle line causes no PM1 traffic; a held line is rechecked once per second and still catches a missed edge\n";

  reset();click();pollPowerKey(millis());assert(powerKeyRequested && inactivity.elapsed(millis())==0);
  const uint32_t first=powerKeyRequestedAt;at(1500);click();pollPowerKey(millis());assert(powerKeyRequestedAt==first);
  std::cout<<"PASS: repeated clicks keep the first request deadline\n";
  std::cout<<"PASS: exact production pollPowerKey, flushForShutdown and powerKeyOff\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=pathlib.Path(tmp)/'power_key_pipeline.cpp'
    cpp.write_text(prefix+function('pollPowerKey')+'\n'+shutdown_flush()+'\n'+function('powerKeyOff')+tests)
    exe=pathlib.Path(tmp)/'power_key_pipeline'
    subprocess.run(['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+str(firmware/'tests/fakes'),'-I'+str(firmware/'LaunchLabMini'),'-I'+str(firmware/'LaunchLabRpm'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
