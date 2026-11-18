#!/usr/bin/env python3
"""Run the sketch's USB restore (finishRestore) on the real stores, then boot.

The restore glue is extracted from the sketch; the stores, the history log on
a NOR flash fake, the backup codec and the setting records are the real
headers. Writes can be refused by key (a full NVS) and power can be cut after
any NVS write or flash program. A "boot" reloads every record the way setup()
does, including the history log's newest-wins pick (history_log.h).

What the device shows after a restore, whatever its outcome, must be what it
loads at the next boot, and a restore must be all or nothing as far as the
device shows: a failure leaves the device's own records, a success the
backup's. A power cut at any write must leave records that load.
"""
import pathlib
import subprocess
import tempfile

from history_log_glue import function

TESTS = pathlib.Path(__file__).resolve().parent
SKETCH = TESTS.parent / 'LaunchLabMini'
SOURCE = (SKETCH / 'LaunchLabMini.ino').read_text()


def struct(source, name):
    start = source.index('struct ' + name + ' {')
    cursor = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[start:source.index(';', cursor) + 1]


def restore_glue(source):
    """finishRestore and the helpers it calls, whichever the sketch has."""
    parts = []
    if 'struct RestoreEffects {' in source:
        parts.append(struct(source, 'RestoreEffects'))
    if 'static bool restoreLoaded(' in source:
        parts.append(function(source, 'restoreLoaded'))
    parts.append(function(source, 'finishRestore'))
    return '\n'.join(parts)


PREFIX = r'''
#include "practice_store.h"
#include "appearance_store.h"
#include "motion_store.h"
#include "theme_store.h"
#include "milestone_store.h"
#include "effect_set.h"
#include "device_backup.h"
#include "history_log.h"
#include "rpm_estimator_setting.h"
#include "sensor_profile_setting.h"
#include "display_flip_setting.h"
#include "mount_orientation_setting.h"
#include "shake_wake_setting.h"
#include "battery_icon_setting.h"
#include "brightness.h"
#include "power_status.h"
#include "analog_tachometer.h"
#if __has_include("device_restore.h")
#include "device_restore.h"
#endif
#include "nor_flash.h"
#include "../../tools/fx/fx_compiler.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <new>
#include <string>
#include <vector>

// ---- power: a cut after `cutAt` more durable writes (NVS, flash, effects).
static long ops=0,cutAt=-1;static bool cut=false;
static bool durable(){++ops;if(cutAt>=0 && ops>cutAt)cut=true;if(cut)Preferences::failWrite=true;return !cut;}
struct CutFlash:NorFlash {
  bool write(uint32_t at,const void *data,size_t n){if(!durable())return false;return NorFlash::write(at,data,n);}
  bool erase(uint32_t at,size_t n){if(!durable())return false;return NorFlash::erase(at,n);}
};
using SpiffsFlash=CutFlash;
static SpiffsFlash spiffsFlash;static HistoryLog::Store<SpiffsFlash> historyLog;
static uint32_t loggedHistory=0,loggedBests=0;

// ---- the loaded-effects store (its own suites test it): slots in RAM.
template<class F> struct LoadedEffects {
  struct Store {
    enum class Result {Ok,Failed};
    bool open=true,formattable=true;
    bool isOpen()const{return open;}
    bool prepareStep(bool &failed){failed=!formattable;if(formattable)open=true;return true;}
  };
  Store store_;unsigned clears=0,writes=0;bool fail=false;std::map<unsigned,std::vector<uint8_t>> slots;
  Store &store(){return store_;}
  typename Store::Result clear(){++clears;if(!durable())return Store::Result::Failed;slots.clear();return Store::Result::Ok;}
  typename Store::Result restore(unsigned slot,const uint8_t *file,size_t bytes,const uint8_t *,size_t) {
    ++writes;if(fail || !durable())return Store::Result::Failed;slots[slot]={file,file+bytes};return Store::Result::Ok;
  }
};
static LoadedEffects<SpiffsFlash> effectStore;static LoadedEffects<SpiffsFlash> *loadedEffects=&effectStore;

// ---- the sketch's records and live values.
static Preferences prefs;
static PracticeHistory practice;static PracticeStore practiceStore;
static Appearance::Config appearance;static AppearanceStore<Appearance::Config> appearanceStore;
static Appearance::Bests personalBests;static AppearanceStore<Appearance::Bests> bestStore;
static LaunchMotion::Trace referenceMotion;static MotionStore motionStore;
static Themes::Library themeLibrary;static ThemeStore themeStore;
static Milestones::Config milestones;static MilestoneStore milestoneStore;
static EffectSet::Config effectSet;static AppearanceStore<EffectSet::Config> effectSetStore;
static InactivityTimer inactivity;static uint8_t brightnessPercent=Brightness::DEFAULT_PERCENT;
static RpmEstimator::Mode rpmEstimator=RpmEstimator::DEFAULT_MODE;
static StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
// Analysis: a running recording or an unsaved archive refuses a restore.
static struct {bool on=false;bool running()const{return on;}}analysis;
static bool calibrationPending=false;static bool calibrationNeedsSave(){return calibrationPending;}
static bool displayFlipped=false,shakeToWake=true,batteryIcon=true,motionPending=false,dirty=false;
static uint8_t minRpmSteps=MinRpm::DEFAULT_STEPS;
static MountOrientation::Mode mountMode=MountOrientation::Mode::Top;

// ---- the loop's surroundings.
struct Snapshot {AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;uint32_t launches=0;};
enum class Action {IdleCheckpointBegin,CheckpointEnd};
static struct {bool suspended=false;
  bool request(Action a,uint32_t=0){suspended=a==Action::IdleCheckpointBegin;return true;}} acquisition;
static std::vector<std::string> lines;
static struct {
  void println(const char *s){lines.push_back(s);}
  void printf(const char *f,...){char b[512];va_list a;va_start(a,f);std::vsnprintf(b,sizeof(b),f,a);va_end(a);
    std::string s(b);if(!s.empty() && s.back()=='\n')s.pop_back();lines.push_back(s);}
} Serial;
namespace UsbTx {inline bool drain(uint32_t){return true;}}
static void delay(unsigned){}
static void heapStatus(){}
static unsigned restarts=0;static struct {void restart(){++restarts;}} ESP;
static std::vector<uint8_t> sent;static char *restoreText=nullptr;
static struct {
  bool take(uint8_t *out,size_t capacity,size_t &n){if(sent.size()>capacity)return false;std::copy(sent.begin(),sent.end(),out);n=sent.size();return true;}
} restoreRx;
'''

HARNESS = r'''
// ---- what the sketch's other paths do with these records.
// flushForShutdown's saves (power-off, sleep): the NVS mirror, and with the
// log open the log takes a copy NVS refuses.
static void powerOff() {
  const bool historyFailed=practiceStore.pending(practice) && (!practiceStore.ready() || !practiceStore.save(practice));
  const bool bestFailed=bestStore.pending(personalBests) && (!bestStore.ready() || !bestStore.save(personalBests));
  if((historyFailed || bestFailed) && historyLog.isOpen()){if(historyUnsaved())logHistory();if(bestsUnsaved())logBests();}
}
template<class T> static void fresh(T &t){t.~T();new(&t) T();}
static bool logOpen=true;
// setup()'s loads, in its order.
static void boot() {
  cut=false;cutAt=-1;Preferences::failWrite=false;Preferences::failWritePrefixes.clear();spiffsFlash.restore();effectStore.fail=false;
  restarts=0;lines.clear();
  fresh(historyLog);if(logOpen && historyLog.open(spiffsFlash)==HistoryLog::Store<SpiffsFlash>::Log::Open::Failed)std::abort();
  prefs.begin("launchlab-mini",false);
  uint8_t minutes=prefs.getUChar("sleep_min",InactivityTimer::DEFAULT_MINUTES);if(!inactivity.setMinutes(minutes))std::abort();
  Brightness::load(prefs,brightnessPercent);RpmEstimatorSetting::load(prefs,rpmEstimator);SensorProfileSetting::load(prefs,sensorProfile);
  DisplayFlipSetting::load(prefs,displayFlipped);MountOrientationSetting::load(prefs,mountMode);
  shakeToWake=true;ShakeWakeSetting::load(prefs,shakeToWake);batteryIcon=true;BatteryIconSetting::load(prefs,batteryIcon);MinRpmSetting::load(prefs,minRpmSteps);
  appearance={};fresh(appearanceStore);appearanceStore.begin(appearance,"ll-appearance","cfg0","cfg1");
  personalBests={};fresh(bestStore);
  const bool bestsInNvs=bestStore.begin(personalBests,"ll-bests","best0","best1") && bestStore.present();
  {Appearance::Bests logged;uint32_t mirrored=0;const bool inLog=historyLog.isOpen() && historyLog.loadBests(logged,mirrored);
   const auto nvs=bestsInNvs?HistoryLog::Nvs::Found:bestStore.ready() && !bestStore.present()?HistoryLog::Nvs::Absent:HistoryLog::Nvs::Unreadable;
   const auto from=HistoryLog::pick(inLog,mirrored,nvs,personalBests.generation());
   if(from==HistoryLog::Source::Log)personalBests=logged;
   loggedBests=from==HistoryLog::Source::Log?personalBests.generation():~personalBests.generation();}
  themeLibrary={};fresh(themeStore);themeStore.begin(themeLibrary);
  milestones={};fresh(milestoneStore);milestoneStore.begin(milestones);
  effectSet={};fresh(effectSetStore);effectSetStore.begin(effectSet,"ll-appearance","eset0","eset1",true);
  practice={};fresh(practiceStore);if(!practiceStore.begin(practice))std::abort();
  if(historyLog.isOpen()) {
    PracticeHistory logged;uint32_t mirrored=0;const bool inLog=historyLog.loadHistory(logged,mirrored);
    const auto nvs=practiceStore.found()?HistoryLog::Nvs::Found:practiceStore.ready()?HistoryLog::Nvs::Absent:HistoryLog::Nvs::Unreadable;
    const auto from=HistoryLog::pick(inLog,mirrored,nvs,practiceStore.savedGeneration());
    if(from==HistoryLog::Source::Log)practice=logged;
    loggedHistory=from==HistoryLog::Source::Log?practice.generation():~practice.generation();
  }
  referenceMotion={};fresh(motionStore);motionStore.begin(referenceMotion);
}

static LaunchMotion::Trace trace(uint32_t number,float rpm) {
  LaunchMotion::Trace t;t.fused=true;t.quality=LaunchMotion::Quality::Valid;t.count=48;t.number=number;t.rpm=rpm;t.durationMs=400;t.gravity[2]=1;
  for(unsigned i=0;i<t.count;++i)t.points[i].ms=-500+int(i*1150/47);
  t.startLevel={StickS3Level::State::Valid,6,.2f,0};t.endLevel={StickS3Level::State::Valid,15,0,-.5f};
  return t;
}
static std::vector<uint8_t> effectFile(unsigned uid) {
  char text[200];std::snprintf(text,sizeof(text),"effect \"E%u\"\nuid %u\nduration 2000\nfallback \"Tidal\"\nframe {\n  dot(acx, ay1 + 20, accent, 1 - t/2)\n}\n",uid,uid);
  const auto r=FxCompiler::compile(text);if(!r.ok)std::abort();return r.bytes;
}
static const std::vector<uint8_t> deviceEffect=effectFile(21),backupEffect=effectFile(22);
static void pull(PracticeHistory &h,Appearance::Bests &b,float rpm){h.accept(h.size()+1,true,rpm,1000*(h.size()+1));b.observe(0,0,true,rpm);}

// The device's own state: 23 pulls around 8000 (20 mirrored to NVS at an
// earlier sleep, 22 in the log, the last only in RAM), a look, settings, one
// installed effect, no tilt reference and no custom themes.
struct State {
  PracticeHistory practice;Appearance::Config look;Appearance::Bests bests;bool motion=false;
  uint8_t minutes=0,brightness=0;bool flipped=false;
  void take(){practice=::practice;look=appearance;bests=personalBests;motion=referenceMotion.valid();
    minutes=inactivity.minutes();brightness=brightnessPercent;flipped=displayFlipped;}
};
static State device;
static void makeDevice(bool withLog) {
  logOpen=withLog;Preferences::data.clear();fresh(spiffsFlash);ops=0;
  effectStore.slots={{0,deviceEffect}};effectStore.clears=effectStore.writes=0;effectStore.store_={};
  boot();
  appearance.select(Appearance::Theme::Mint,Appearance::Effect(2));if(!appearanceStore.saveViewed(appearance))std::abort();
  inactivity.setMinutes(3);brightnessPercent=50;displayFlipped=true;
  if(prefs.putUChar("sleep_min",3)!=1 || prefs.putUChar(Brightness::KEY,50)!=1 || !DisplayFlipSetting::save(prefs,true))std::abort();
  for(unsigned i=0;i<20;++i)pull(practice,personalBests,8000+10*i);
  if(!practiceStore.save(practice) || !bestStore.save(personalBests))std::abort();
  if(withLog && !(logHistory() && logBests()))std::abort();
  for(unsigned i=0;i<2;++i) {
    pull(practice,personalBests,8200+10*i);
    if(withLog?!(logHistory() && logBests()):!(practiceStore.save(practice) && bestStore.save(personalBests)))std::abort();
  }
  pull(practice,personalBests,8300);
  device.take();
}
static DeviceBackup::Contents backup(bool effects) {
  DeviceBackup::Contents b;
  for(unsigned i=0;i<5;++i)pull(b.practice,b.bests,12000+10*i);
  b.look.select(Appearance::Theme::Classic,Appearance::Effect(4));
  b.hasMotion=true;b.motion=trace(5,12040);
  b.settings.sleepMinutes=7;b.settings.brightness=80;b.settings.flipped=false;
  if(effects){b.loadedCount=1;b.loaded[0]={2,backupEffect.data(),nullptr,uint16_t(backupEffect.size()),0};}
  return b;
}
static std::string restore(const DeviceBackup::Contents &c) {
  sent.assign(DeviceBackup::encodedBound(c),0);sent.resize(DeviceBackup::encode(c,"0.20.10-test",sent.data(),sent.size()));
  if(sent.empty())std::abort();
  lines.clear();Snapshot s;finishRestore(s);
  for(const auto &l:lines)if(l.rfind("RESTORE_OK",0)==0 || l.rfind("RESTORE_ERROR",0)==0)return l;
  return "none";
}

static bool deviceHistory(const PracticeHistory &h){return h.size()>=22 && h.recent() && h.recent()->rpm>=8000 && h.recent()->rpm<9000;}
static bool backupHistory(const PracticeHistory &h){return h.size()==5 && h.recent() && h.recent()->rpm==12040;}
static bool sameHistory(const PracticeHistory &a,const PracticeHistory &b){return a.size()==b.size() && a.recent()->rpm==b.recent()->rpm && a.sessions()==b.sessions();}
static bool sameBests(const Appearance::Bests &a,const Appearance::Bests &b){return std::equal(a.rpm,a.rpm+4,b.rpm);}
static bool deviceLook(){return appearance.theme==Appearance::Theme::Mint && appearance.effect==Appearance::Effect(2);}
static bool backupLook(){return appearance.theme==Appearance::Theme::Classic && appearance.effect==Appearance::Effect(4);}
static bool deviceSettings(){return inactivity.minutes()==3 && brightnessPercent==50 && displayFlipped;}
static bool backupSettings(){return inactivity.minutes()==7 && brightnessPercent==80 && !displayFlipped;}
// Settings are separate keys: each one is the device's or the backup's.
static bool eachSetting(){return (inactivity.minutes()==3 || inactivity.minutes()==7) && (brightnessPercent==50 || brightnessPercent==80);}
// The device's own records, all of them, and they agree with each other.
static bool deviceShown(){return sameHistory(practice,device.practice) && sameBests(personalBests,device.bests) && deviceLook() && deviceSettings() && !referenceMotion.valid();}
static bool backupShown(){return backupHistory(practice) && personalBests.rpm[0]==12040 && backupLook() && backupSettings() && referenceMotion.valid() && referenceMotion.number==5;}

static unsigned failures=0;
static void check(const char *what,bool ok,const std::string &detail="") {
  std::cout<<(ok?"ok   ":"FAIL ")<<what<<(detail.empty()?"":"  ["+detail+"]")<<"\n";
  if(!ok)++failures;
}
static std::string history(){char b[96];std::snprintf(b,sizeof(b),"history records=%u best_recent=%.0f generation=%u",practice.size(),
  practice.recent()?practice.recent()->rpm:0.0,unsigned(practice.generation()));return b;}

int main() {
  static std::vector<char> text(DeviceBackup::LOADED_TEXT_BYTES);restoreText=text.data();
  Preferences::beforeWrite=[]{durable();};

  // 1. The reviewer's case, log closed (as before 0.18): bests are refused
  // after history was restored. The device keeps running on its own records;
  // at power-off and the next boot it must still have them.
  makeDevice(false);
  Preferences::failWritePrefixes={"ll-bestsbest"};
  std::string reply=restore(backup(false));
  check("log closed, bests refused: reported failed, nothing restored, no restart",reply.find("RESTORE_ERROR")==0 && reply.find("written=0")!=std::string::npos && !restarts,reply);
  check("log closed, bests refused: the device still shows its own records",deviceShown(),history());
  Preferences::failWritePrefixes.clear();powerOff();boot();
  check("log closed, bests refused: power-off then boot loads the device's own history, agreeing with its bests",deviceShown(),history());
  makeDevice(false);Preferences::failWritePrefixes={"ll-bestsbest"};restore(backup(false));boot();
  // Bests NVS refused stay pending, as any refused save does (the last pull's best is RAM's alone).
  check("log closed, bests refused: a power cut right after loads the device's own records",sameHistory(practice,device.practice) &&
        deviceLook() && deviceSettings() && !referenceMotion.valid() && personalBests.rpm[0]>=8200 && personalBests.rpm[0]<9000,history());

  // 1b. A running Analysis or its unsaved archive: refused before anything is written.
  for(int pending=0;pending<2;++pending) {
    makeDevice(true);analysis.on=!pending;calibrationPending=pending;
    reply=restore(backup(false));
    check(pending?"Analysis archive unsaved: restore refused, nothing written":"Analysis running: restore refused, nothing written",
          reply.find("RESTORE_ERROR reason=analysis_pending written=0")==0 && !restarts && deviceShown(),reply);
    analysis.on=false;calibrationPending=false;
  }

  // 2. Log open: the tilt reference is refused (a full NVS) after records
  // before it were written.
  makeDevice(true);
  Preferences::failWritePrefixes={"ll-motionref"};
  reply=restore(backup(false));
  check("motion refused: reported failed with nothing restored, no restart",reply.find("RESTORE_ERROR")==0 && reply.find("written=0")!=std::string::npos && !restarts,reply);
  Preferences::failWritePrefixes.clear();powerOff();boot();
  check("motion refused: power-off then boot loads every record of the device's own",deviceShown(),history());

  // 3. The variant: one pull after the failed restore, then a side-key
  // power-off before the log took it (log open: the restore failed before
  // history; log closed: after it).
  for(bool withLog:{true,false}) {
    makeDevice(withLog);
    Preferences::failWritePrefixes={withLog?"ll-motionref":"ll-bestsbest"};
    reply=restore(backup(false));Preferences::failWritePrefixes.clear();
    check(withLog?"one pull after a failed restore (log open): reported failed, no restart":"one pull after a failed restore (log closed): reported failed, no restart",
          reply.find("RESTORE_ERROR")==0 && !restarts,reply);
    pull(practice,personalBests,8400);device.take();powerOff();boot();
    check(withLog?"one pull after a failed restore (log open): power-off then boot keeps the pull and the device's history":
          "one pull after a failed restore (log closed): power-off then boot keeps the pull and the device's history",
          deviceShown() && practice.recent()->rpm==8400,history());
  }

  // 4. A full NVS with the log open: history and bests NVS mirrors are
  // refused, the log takes them; every other record fits. The restore succeeds.
  makeDevice(true);
  Preferences::failWritePrefixes={"ll-practicehistory","ll-bestsbest"};
  reply=restore(backup(false));
  check("full NVS, log open: RESTORE_OK and restart",reply.find("RESTORE_OK")==0 && restarts==1,reply);
  boot();
  check("full NVS, log open: the next boot loads the whole backup",backupShown(),history());
  check("full NVS, log open: NVS keeps the device's mirror, the log wins",practiceStore.savedGeneration()==20,std::to_string(practiceStore.savedGeneration()));

  // 5. A doomed restore does not rewrite the loaded effects; a retry that
  // succeeds replaces them once.
  makeDevice(true);
  Preferences::failWritePrefixes={"ll-motionref"};
  reply=restore(backup(true));
  check("refused restore: the loaded effects are untouched",effectStore.clears==0 && effectStore.writes==0 && effectStore.slots.count(0) && effectStore.slots.size()==1,
        "clears="+std::to_string(effectStore.clears)+" writes="+std::to_string(effectStore.writes));
  Preferences::failWritePrefixes.clear();
  reply=restore(backup(true));
  check("retry: RESTORE_OK, the effects replaced once",reply.find("RESTORE_OK")==0 && effectStore.clears==1 && effectStore.writes==1 &&
        effectStore.slots.size()==1 && effectStore.slots.count(2),reply);
  boot();check("retry: the next boot loads the backup",backupShown(),history());

  // 6. Effects that cannot be stored (the region holds a newer layout):
  // refused before anything is written.
  makeDevice(true);
  effectStore.store_.open=false;effectStore.store_.formattable=false;
  const auto before=Preferences::data;const auto flashBefore=spiffsFlash.bytes;
  reply=restore(backup(true));
  check("effects unavailable: refused, written=0, no restart",reply.find("RESTORE_ERROR")==0 && reply.find("written=0")!=std::string::npos && !restarts,reply);
  check("effects unavailable: NVS, the log and the effects unchanged",Preferences::data==before && spiffsFlash.bytes==flashBefore && effectStore.clears==0);
  effectStore.store_={};

  // 7. The loaded effects fail part way: what was replaced cannot come back,
  // so the device restarts and shows what storage holds; the records are its own.
  makeDevice(true);
  effectStore.fail=true;
  reply=restore(backup(true));
  check("effects fail: reported partial, restart",reply.find("RESTORE_ERROR")==0 && reply.find("written=partial")!=std::string::npos && restarts==1,reply);
  boot();
  check("effects fail: the next boot loads the device's own records",deviceShown(),history());

  // 8. A plain restore: the whole backup, and history agrees with bests.
  makeDevice(true);
  reply=restore(backup(false));
  check("plain restore: RESTORE_OK and restart",reply.find("RESTORE_OK")==0 && restarts==1,reply);
  boot();check("plain restore: the next boot loads the backup",backupShown(),history());

  // 9. A power cut after every durable write of a restore, of one that fails
  // and rolls back, and of one whose effects fail: each record loads, and is
  // either the device's or the backup's.
  for(int mode=0;mode<3;++mode) {
    unsigned cuts=0,bad=0;
    for(long n=0;;++n) {
      makeDevice(true);
      if(mode==1)Preferences::failWritePrefixes={"ll-bestsbest","ll-motionref"};
      if(mode==2)effectStore.fail=true;
      cutAt=ops+n;restore(backup(true));
      const bool reached=cut;
      boot();++cuts;
      const bool records=(deviceHistory(practice) || backupHistory(practice)) && (deviceLook() || backupLook()) &&
        eachSetting() && practiceStore.ready() && !appearanceStore.damaged() && !bestStore.damaged() && !effectSetStore.damaged();
      if(!records){++bad;std::cout<<"  cut after "<<n<<" writes: "<<history()<<"\n";}
      if(!reached)break;
    }
    check(mode==0?"power cut at every write of a restore: every record loads":mode==1?"power cut at every write of a rolled-back restore: every record loads":
          "power cut at every write of a partial restore: every record loads",!bad,std::to_string(cuts)+" cuts");
  }

  if(failures){std::cout<<"FAIL: "<<failures<<" restore checks\n";return 1;}
  std::cout<<"PASS: restore all or nothing as the device shows it; refusals before effects; full NVS restores through the log; power cuts load\n";
}
'''

HELPERS = ('historyUnsaved', 'bestsUnsaved', 'logHistory', 'logBests')

with tempfile.TemporaryDirectory() as tmp:
    cpp = pathlib.Path(tmp) / 'restore.cpp'
    cpp.write_text(PREFIX + '\n'.join(function(SOURCE, name) for name in HELPERS) + '\n' + restore_glue(SOURCE) + HARNESS)
    exe = pathlib.Path(tmp) / 'restore'
    subprocess.run(['c++', '-std=c++17', '-O1', '-g', '-DVQF_SINGLE_PRECISION', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(TESTS / 'fakes'), '-I' + str(SKETCH), '-I' + str(SKETCH.parent / 'LaunchLabRpm'), str(cpp),
                    str(SKETCH / 'src/vqf/vqf.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
