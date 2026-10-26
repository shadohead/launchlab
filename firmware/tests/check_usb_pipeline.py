#!/usr/bin/env python3
"""Execute exact production USB polling/standby functions under bounded fakes."""
import argparse
import pathlib
import subprocess
import tempfile

parser=argparse.ArgumentParser()
location=pathlib.Path(__file__).resolve()
default_firmware=location.parents[1] if (location.parents[1]/'LaunchLabMini/usb_awake.h').exists() else location.parents[2]/'qa-integration/firmware'
parser.add_argument('firmware',nargs='?',type=pathlib.Path,
                    default=default_firmware)
parser.add_argument('--late-save-probe',action='store_true',help='compatibility option; late-save attachment is always tested')
args=parser.parse_args()
source=(args.firmware/'LaunchLabMini/LaunchLabMini.ino').read_text()
def function(name):
    start=source.index('static void '+name+'(')
    cursor=source.index('{',start)+1
    depth=1
    while depth:
        depth+=(source[cursor]=='{')-(source[cursor]=='}')
        cursor+=1
    return source[start:cursor]

def shutdown_flush():
    # Shared save-before-power-down helper with its result type.
    start = source.index("static ShutdownFlush flushForShutdown(")
    cursor = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    enum = source.index("enum class ShutdownFlush")
    return source[enum:source.index("};", enum) + 2] + "\n" + source[start:cursor]

prefix=r'''
#include "usb_awake.h"
#include "power_status.h"
#include "history_checkpoint.h"
#include "practice_store.h"
#include "appearance_store.h"
#include "power_diagnostics.h"
#include "wake_ui_state.h"
#include "controls.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
static Appearance::Config appearance;static Appearance::Bests personalBests;
static AppearanceStore<Appearance::Bests> bestStore;static AppearanceStore<Appearance::Config> appearanceStore;static Appearance::Celebration bestEffect;
static Controls::State controls;static bool wipeBlocked=false;
static uint64_t clockUs=1000000;
static uint32_t millis(){return uint32_t(clockUs/1000);}
static int64_t wallUs(){return clockUs;}
static bool hardwareSof=false,powerReadOk=true,mutexAvailable=true,attachOnPowerSave=false;
static uint8_t powerSource=4;
static unsigned hostReads=0,powerReads=0,locks=0,unlocks=0,flashWrites=0,powerSaves=0;
struct HWCDC {static bool isPlugged(){++hostReads;return hardwareSof;}};
struct SerialFake {void println(const char*){}void flush(){}template<class... A>void printf(const char*,A...){}} Serial;
// USB output is drained (never Serial.flush(), which discards it): usb_tx.h.
namespace UsbTx {inline unsigned drains=0;inline bool drain(uint32_t){++drains;return true;}}

static UsbHostAwake usbAwake;static bool usbAutoOffInhibited=false;
static InactivityTimer inactivity;static PracticeHistory practice;static PracticeStore practiceStore;
static bool motionPending=false,saveError=false,dirty=false,extPower=false,tournamentMode=false,historyPage=false,demo=false,diagnostics=false;
static uint32_t lastShutdownTry=0,lastSaveTry=0,saveErrors=0,historyPendingSince=0;
static PowerDiagnostics::Image powerLog;static bool powerLogDirty=false;
enum class TournamentView:uint8_t {RecordingOnly,Rpm};static TournamentView tournamentView=TournamentView::RecordingOnly;
namespace PracticeUI {enum class View:uint8_t {Recent};}static PracticeUI::View historyView=PracticeUI::View::Recent;
enum class Action {ShutdownBegin,CheckpointEnd,Pin};
struct Snapshot {AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;uint32_t launches=0;bool valid=true;float resultRpm=6000;uint8_t pin=1;};
struct Acquisition {
  Snapshot state;bool suspended=false;unsigned begins=0,resumes=0;
  Snapshot snapshot(){return state;}bool takeEvent(Snapshot&){return false;}
  bool request(Action action,uint32_t expected=0){
    if(action==Action::CheckpointEnd || action==Action::Pin){suspended=false;++resumes;return true;}
    if(state.phase==AnalogTachometer::Phase::Launch || expected!=state.launches)return false;
    suspended=true;++begins;return true;
  }
} acquisition;
struct Imu {bool lock(){++locks;return mutexAvailable;}void unlock(){++unlocks;}} imuAcquisition;
struct PM1 {bool readRegister(uint8_t reg,uint8_t *out,size_t count){
  assert(reg==0x04 && count==1);++powerReads;if(powerReadOk)*out=powerSource;return powerReadOk;
}};
struct M5Fake {
  struct DisplayFake {unsigned writes=0;void setBrightness(unsigned){++writes;}void sleep(){++writes;}void wakeup(){++writes;}} Display;
  struct PowerFake {PM1 M5pm1;unsigned writes=0;void setExtOutput(bool){++writes;}} Power;
} M5;
// Wake retry itself is fault-tested in test_pm1_idle/check_pm1_idle. Keep this
// harness focused on host policy and exact standby ordering.
struct IdleFake {
  bool read(uint8_t reg,uint8_t *data,size_t size){return M5.Power.M5pm1.readRegister(reg,data,size);}
  template<class F>bool run(F fn){return fn();}
};
static IdleFake &pm1Idle(){static IdleFake idle;return idle;}
struct Settings {std::map<std::string,uint32_t> values;unsigned writes=0;
  size_t putUInt(const char *key,uint32_t value){++writes;values[key]=value;return 4;}
  bool remove(const char *key){return values.erase(key)>0;}
} prefs;
static uint8_t backlightValue(){return 100;}
static void powerRecord(uint8_t event){PowerDiagnostics::append(powerLog,event,3900,PowerDiagnostics::ChargeKnown,40,240);powerLogDirty=true;}
static bool savePowerLog(){++powerSaves;powerLog.checksum=PowerDiagnostics::hash(powerLog);powerLogDirty=false;
  if(attachOnPowerSave){hardwareSof=true;clockUs+=100000;}return true;}
struct Receipt {uint32_t magic=0;int64_t atUs=0;} sleepReceipt;
struct ShakeWake {unsigned arms=0,sleeps=0,restores=0;bool arm(){++arms;return true;}bool restore(){++restores;return true;}
  bool sleep(){assert(!practiceStore.pending(practice));++sleeps;return false;}
} shakeWake;
static bool shakeToWake=true;
static unsigned restoreFailureNotes=0;static void noteMotionRestoreFailure(){++restoreFailureNotes;}
static void prepareMotion(const Snapshot&){motionPending=true;}
static void logLaunchMetrics(const Snapshot&){}static void noteStartMark(const Snapshot&){}static void saveStartMarks(){}
// Settings persistence has its own production harness (check_save_paths.py).
static void writeSettings(){}
static void cancelSettingEdit(){}
static bool saveWakeNavigation(){return true;}
'''
tests=r'''
static void at(uint32_t now){clockUs=uint64_t(now)*1000;}
static void reset(){
  controls={};wipeBlocked=false;
  clockUs=1000000;hardwareSof=false;powerReadOk=mutexAvailable=true;powerSource=4;attachOnPowerSave=false;
  hostReads=powerReads=locks=unlocks=flashWrites=powerSaves=0;
  usbAwake=UsbHostAwake{};usbAutoOffInhibited=false;inactivity=InactivityTimer{};inactivity.setMinutes(1);inactivity.touch(millis());
  practice=PracticeHistory{};practiceStore=PracticeStore{};acquisition=Acquisition{};M5=M5Fake{};prefs=Settings{};shakeWake=ShakeWake{};
  motionPending=saveError=dirty=extPower=tournamentMode=historyPage=demo=diagnostics=false;
  lastShutdownTry=lastSaveTry=saveErrors=historyPendingSince=0;PowerDiagnostics::fresh(powerLog);powerLogDirty=false;sleepReceipt={};
  prefs.values["sleep_min"]=1;prefs.values["ext5v"]=0;prefs.values["bright_pct"]=40;
  Preferences::data.clear();Preferences::zeroLengthKeys.clear();Preferences::failWrite=Preferences::shortRead=false;Preferences::beforeWrite=nullptr;
  assert(practiceStore.begin(practice));
}
static void poll(uint32_t now){at(now);pollUsbAwake(now,acquisition.snapshot());}
static void off(){autoPowerOff(millis(),acquisition.snapshot());}
static void confirmedHost(){hardwareSof=true;powerSource=5;poll(1000);poll(1250);assert(usbAwake.remembered());}
static void assertNoStandby(){assert(!acquisition.begins && !shakeWake.arms && !shakeWake.sleeps && !prefs.writes && !M5.Display.writes && !M5.Power.writes && !powerSaves);}
int main(){
  // The connection monitor can remain optimistically positive for multiple
  // polls. SOF alone must not promote a shake wake when checked VIN is absent.
  reset();inactivity.begin(1000,true);hardwareSof=true;powerSource=4;
  poll(1000);poll(1250);assert(usbAwake.remembered() && inactivity.unusedShake());
  hardwareSof=false;poll(1500);
  assert(inactivity.unusedShake() && inactivity.remaining(1500)==29500);
  at(31000);off();assert(shakeWake.sleeps==1);
  // A real host remains inhibited while a failed VIN read is uncertain, but
  // promotion waits for verified input power and then restores normal timeout.
  reset();inactivity.begin(1000,true);hardwareSof=true;powerReadOk=false;
  poll(1000);poll(1250);assert(usbAwake.inhibited(1250) && inactivity.unusedShake());
  powerReadOk=true;powerSource=5;poll(2000);assert(!inactivity.unusedShake());
  hardwareSof=false;powerSource=4;poll(3000);
  assert(!usbAwake.inhibited(3000) && inactivity.remaining(3000)==60000);
  std::cout<<"PASS: multi-poll optimistic SOF with battery-only power preserves shake grace; verified VIN recovery promotes real USB use\n";
  reset();inactivity.begin(1000,true);hardwareSof=true;powerSource=1;poll(1000);
  assert(inactivity.unusedShake());hardwareSof=false;poll(1250);
  assert(inactivity.unusedShake() && inactivity.remaining(1250)==29750);
  at(31000);off();assert(shakeWake.sleeps==1);
  reset();inactivity.begin(1000,true);confirmedHost();assert(!inactivity.unusedShake());
  hardwareSof=false;powerSource=4;poll(2000);
  assert(!usbAwake.inhibited(2000) && inactivity.remaining(2000)==60000);
  std::cout<<"PASS: unconfirmed boot SOF preserves short shake grace; confirmed USB use and detach restore full selected timeout\n";
  reset();confirmedHost();const auto settings=prefs.values;
  for(uint32_t now=1500;now<=181000;now+=250){poll(now);off();}
  assertNoStandby();assert(prefs.values==settings && inactivity.minutes()==1);
  assert(hostReads<=722 && powerReads<=181 && locks==unlocks);
  std::cout<<"PASS: idle computer with no Serial reader stays awake beyond three timeouts; host/power polls bounded; settings/rails/charging path untouched\n";

  reset();powerSource=1;hardwareSof=true;poll(1000);assert(!usbAwake.remembered());hardwareSof=false;poll(1250);
  assert(!usbAwake.remembered() && !usbAwake.inhibited(1250));
  for(uint32_t now=1500;now<61000;now+=250)poll(now);
  assert(powerReads==1); // Only the optimistic first SOF caused a checked read.
  off();assert(!shakeWake.sleeps);at(61250);off();assert(shakeWake.sleeps==1);
  reset();powerSource=1;
  for(uint32_t now=1000;now<=61000;now+=250)poll(now);
  assert(!powerReads && !usbAwake.remembered());off();assert(shakeWake.sleeps==1);
  std::cout<<"PASS: optimistic boot pulse releases; charger and full-battery VIN do not establish host; ordinary battery/charger timer still sleeps\n";

  reset();confirmedHost();hardwareSof=false;
  for(uint32_t now=1500;now<=181000;now+=250){poll(now);off();assert(usbAwake.inhibited(now));}
  assertNoStandby();powerSource=4;poll(181250);poll(182000);
  assert(!usbAwake.remembered() && !usbAwake.inhibited(182000) && inactivity.elapsed(182000)==0);
  poll(182500);assert(inactivity.elapsed(182500)==500);
  at(241999);off();assert(!shakeWake.sleeps);at(242000);off();assert(shakeWake.sleeps==1);
  std::cout<<"PASS: powered host suspend persists; checked detach starts exactly one complete timeout instead of immediate or repeatedly postponed standby\n";

  reset();confirmedHost();hardwareSof=false;powerSource=4;poll(2000);
  hardwareSof=true;powerSource=1;poll(2250);assert(usbAwake.inhibited(2250) && !usbAwake.remembered());
  poll(2500);assert(usbAwake.remembered());at(600000);pollUsbAwake(millis(),acquisition.snapshot());off();assertNoStandby();
  std::cout<<"PASS: reconnect inhibits immediately, then confirms remembered host; old battery deadline cannot power off\n";

  for(bool busFailure:{false,true}){
    reset();confirmedHost();hardwareSof=false;powerReadOk=!busFailure;mutexAvailable=busFailure;
    if(!busFailure)mutexAvailable=false; // Separate checked-read error and mutex miss.
    for(uint32_t now=1500;now<=3500;now+=250)poll(now);
    assert(!usbAwake.inhibited(3500) && usbAwake.remembered() && inactivity.elapsed(3500)==250);
    assert(locks<=3 && powerReads<=3);
    powerReadOk=mutexAvailable=true;powerSource=1;poll(4000);
    assert(usbAwake.inhibited(4000) && usbAwake.remembered() && inactivity.elapsed(4000)==0);assertNoStandby();
  }
  reset();confirmedHost();hardwareSof=false;acquisition.state.phase=AnalogTachometer::Phase::Launch;
  for(uint32_t now=1500;now<=70000;now+=250){poll(now);off();}
  assert(powerReads==1 && usbAwake.remembered() && !usbAwake.inhibited(70000));assertNoStandby();
  acquisition.state.phase=AnalogTachometer::Phase::Ready;poll(70250);
  assert(usbAwake.inhibited(70250) && powerReads==2);
  std::cout<<"PASS: power-read failures, mutex misses and long Launch deferral are bounded, retain host identity, and recover without new SOF\n";

  reset();const uint32_t base=0xffffff00u;at(base);inactivity.touch(base);hardwareSof=true;powerSource=1;
  pollUsbAwake(base,acquisition.snapshot());clockUs+=250000;pollUsbAwake(millis(),acquisition.snapshot());assert(usbAwake.remembered());
  hardwareSof=false;powerSource=4;clockUs+=750000;pollUsbAwake(millis(),acquisition.snapshot());
  const uint32_t detached=millis();assert(!usbAwake.inhibited(detached));clockUs+=59999000;off();assert(!shakeWake.sleeps);
  clockUs+=1000;off();assert(shakeWake.sleeps==1 && inactivity.minutes()==1);
  std::cout<<"PASS: actual polling/standby countdown survives millis wrap\n";

  reset();poll(1000);at(61000);hardwareSof=true;powerSource=1;
  assert(!usbAwake.inhibited(millis()));off();assert(usbAwake.inhibited(millis()));assertNoStandby();
  std::cout<<"PASS: fresh timer-boundary SOF recheck prevents stale-false shutdown before owner checkpoint\n";

  reset();assert(practice.accept(1,true,6000,millis()));acquisition.state.launches=1;
  at(61000);Preferences::beforeWrite=[](){++flashWrites;clockUs+=178000;hardwareSof=true;powerSource=1;};
  off();assert(flashWrites==1 && !practiceStore.pending(practice));
  assert(usbAwake.inhibited(millis()) && acquisition.begins==1 && acquisition.resumes==1 && !acquisition.suspended);
  assert(!shakeWake.arms && !shakeWake.sleeps && !prefs.writes && !M5.Display.writes && !M5.Power.writes && !powerSaves);
  PracticeHistory reboot;PracticeStore reader;assert(reader.begin(reboot) && reboot.size()==1 && reboot.recent()->rpm==6000);
  std::cout<<"PASS: computer attached during178ms history flush prevents sleep, resumes owner and retains durable record without wake markers/rail writes\n";
  for(unsigned entries:{0u,PowerDiagnostics::CAPACITY+3}){
    reset();for(unsigned i=0;i<entries;++i)powerRecord(PowerDiagnostics::Sample);
    powerLog.sleeps=7;powerLog.checksum=PowerDiagnostics::hash(powerLog);
    const auto previous=powerLog;const auto settings=prefs.values;
    at(61000);attachOnPowerSave=true;off();
    assert(!shakeWake.sleeps && !acquisition.suspended && acquisition.resumes==1 && shakeWake.restores==1);
    assert(locks==1 && unlocks==1); // Last recheck does not acquire the already-held mutex.
    assert(!powerLog.pendingSleep && !sleepReceipt.magic && !prefs.values.count("wake_ui") && usbAwake.inhibited(millis()));
    assert(powerLog.sleeps==previous.sleeps && powerLog.count==previous.count && powerLog.next==previous.next);
    assert(!std::memcmp(powerLog.records,previous.records,sizeof(powerLog.records)) && PowerDiagnostics::valid(powerLog));
    assert(prefs.values==settings && inactivity.minutes()==1);
    std::cout<<"PASS: late power-log attachment cancels sleep/restores display+IMU+ADC, clears markers and rolls back diagnostics; prior_entries="<<entries<<"\n";
  }
  std::cout<<"PASS: exact production pollUsbAwake and autoPowerOff; real USB policy, inactivity timer and alternating history store\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=pathlib.Path(tmp)/'usb_pipeline.cpp'
    cpp.write_text(prefix+function('pollUsbAwake')+'\n'+shutdown_flush()+'\n'+function('autoPowerOff')+tests)
    exe=pathlib.Path(tmp)/'usb_pipeline'
    cmd=['c++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
         '-I'+str(args.firmware/'tests/fakes'),'-I'+str(args.firmware/'LaunchLabMini'),'-I'+str(args.firmware/'LaunchLabRpm'),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True)
    subprocess.run([str(exe)],check=True)
