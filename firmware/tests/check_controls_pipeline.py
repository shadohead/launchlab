#!/usr/bin/env python3
"""Exercise actual sketch navigation/edit handlers with host peripherals."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'LaunchLabMini/LaunchLabMini.ino').read_text()
def function(name):
    start=source.index('static void '+name+'(')
    # Some handlers have a forward declaration before their definition.
    while source.index(';',start)<source.index('{',start):
        start=source.index('static void '+name+'(',start+1)
    cursor=source.index('{',start)+1
    depth=1
    while depth:
        depth+=(source[cursor]=='{')-(source[cursor]=='}')
        cursor+=1
    return source[start:cursor]
prefix=r'''
#include "controls.h"
#include "appearance_store.h"
#include "milestone_store.h"
#include "theme_library.h"
#include "analog_tachometer.h"
#include "rpm_estimator_setting.h"
#include "brightness.h"
#include "device_wipe.h"
#include "display_flip_setting.h"
#include "shake_wake_setting.h"
#include "display_orientation.h"
#include "sensor_profile_setting.h"
#include "start_mark_reference.h"
#include "mount_orientation_setting.h"
#include "level_indicator.h"
#include "power_status.h"
#include "practice_ui.h"
#include "launch_feedback.h"
#include "wipe_confirmation.h"
#include "settings_save.h"
#include "milestones.h"
#include <cassert>
#include <iostream>
'''
# practice_ui.h needs graphics types only for its inline renderer. This fixture
# tests actions, so use the actual practice data and view types directly.
prefix=prefix.replace('#include "practice_ui.h"','#include "practice_history.h"\n#include "practice_views.h"')
practice_ui=(root/'LaunchLabMini/practice_ui.h').read_text()
prefix+=practice_ui[practice_ui.index('namespace PracticeUI {'):practice_ui.index('inline void draw(')]+'}\n'
prefix+=r'''
static Appearance::Config appearance;static Appearance::Bests personalBests;static Themes::Library themeLibrary;
static AppearanceStore<Appearance::Bests> bestStore;static AppearanceStore<Appearance::Config> appearanceStore;static Appearance::Celebration bestEffect;
static Milestones::Config milestones;static MilestoneStore milestoneStore;
static uint32_t clockMs=10000;static uint32_t millis(){return clockMs;}
static unsigned soundPreviews=0,effectPreviews=0;static void previewSound(){++soundPreviews;}static void previewEffect(){++effectPreviews;}
using Controls::Page;
static Controls::State controls;
static Controls::Gesture gestureA,gestureB;
static WipeConfirmation wipeConfirmation;
static bool wipeFailed=false,wipeBlocked=false;
static struct {void cancel(){}}restoreRx;
static void beginWipe();static void cancelWipe();
// The start-mark reference (its saves are checked by check_save_paths).
static StartMarkReference::Reference startMarks;static bool startMarksDirty=false;static void loadStartMarks(){}static void saveStartMarks(){}
static bool tournamentMode=false,historyPage=false,dirty=false,diagnostics=false,demo=false,motionDemo=false,motionPending=false,settingsSaveError=false,restorePreview=false,displayFlipped=false,shakeToWake=ShakeWakeSetting::DEFAULT_ON;
static uint8_t brightnessPercent=Brightness::DEFAULT_PERCENT;
static uint32_t historyOffset=0,newSessionAt=0,tournamentHintUntil=0;
enum class TournamentView:uint8_t{RecordingOnly,Rpm};
static TournamentView tournamentView=TournamentView::RecordingOnly;
static const char *tournamentViewName(){return tournamentView==TournamentView::Rpm?"rpm":"recording_only";}
static PracticeUI::View historyView=PracticeUI::View::Recent;
static PracticeHistory practice,demoPractice;
static struct {uint32_t number=1;} latestMotion;
// The recap's replay length does not matter to navigation.
namespace MotionReplay {template<typename T>static uint32_t playbackDurationMs(const T &){return 0;}}
static LaunchFeedback launchFeedback;
static InactivityTimer inactivity;
static RpmEstimator::Mode rpmEstimator=RpmEstimator::DEFAULT_MODE;
static StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
static MountOrientation::Mode mountMode=MountOrientation::Mode::Top,liveMount=MountOrientation::Mode::Top;
static SettingsSave settingsSave;static bool extPower=false,directWrites=false;
static StickS3Level level;static StickS3Level::Reading drawnLevel;static uint64_t lastLevelSampleUs=0;
struct MotionAcquisition {
  MountOrientation::Mode mode=MountOrientation::Mode::Top;unsigned changes=0;bool fail=false;
  bool setMount(MountOrientation::Mode next){if(fail)return false;mode=next;++changes;return true;}
}imuAcquisition;
struct Prefs:Preferences {
  unsigned writes=0;
  size_t putUChar(const char *key,uint8_t value){++writes;return putBytes(key,&value,1);}
  size_t putBool(const char *key,bool value){const uint8_t byte=value;return putUChar(key,byte);}
  size_t putBytes(const char *key,const void *data,size_t size){++writes;return Preferences::putBytes(key,data,size);}
}prefs;
static uint8_t backlightValue(){return (unsigned(brightnessPercent)*255+50)/100;}
// The default brightness, its next step and their backlight values.
static constexpr unsigned BRIGHT=Brightness::DEFAULT_PERCENT;
static constexpr unsigned light(unsigned percent){return (percent*255+50)/100;}
struct Snapshot{AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;uint32_t launches=0;
  bool ready=true,suspended=false,opticalIdle=true,signalFault=false;uint8_t pin=1;
  RpmEstimator::Mode estimator=RpmEstimator::DEFAULT_MODE;StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;};
enum class Action{IdleCheckpointBegin,CheckpointEnd,Estimator,SensorProfile};
// Refuses Begin exactly when the production owner would. refuseNext models a
// mark that reaches the owner after the UI read an idle snapshot.
struct Acquisition {
  Snapshot state;bool quietSignal=true,refuseNext=false,held=false;
  unsigned begins=0,ends=0,refusals=0,estimatorChanges=0,profileChanges=0;bool failEstimator=false,failProfile=false;
  Snapshot snapshot(){Snapshot s=state;s.suspended=held;
    s.opticalIdle=quietSignal && (s.phase==AnalogTachometer::Phase::Ready || s.phase==AnalogTachometer::Phase::Paused);return s;}
  bool request(Action action,uint32_t expected=0){
    if(action==Action::IdleCheckpointBegin) {
      if(held || refuseNext || state.phase==AnalogTachometer::Phase::Launch || expected!=state.launches ||
         (state.phase==AnalogTachometer::Phase::Ready && !quietSignal)){refuseNext=false;++refusals;return false;}
      ++begins;held=true;return true;
    }
    if(action==Action::CheckpointEnd){if(!held)return false;++ends;held=false;return true;}
    // The detector's metric and profile can change only inside a checkpoint.
    assert(held);
    if(action==Action::Estimator){++estimatorChanges;if(failEstimator)return false;state.estimator=RpmEstimator::Mode(expected);return true;}
    if(action==Action::SensorProfile){++profileChanges;if(failProfile)return false;state.sensorProfile=StickS3SensorProfile::Mode(expected);return true;}
    return true;
  }
}acquisition;
// Every NVS write happens inside a checkpoint, except the boot-load fixtures.
static void writeGuard(){assert(acquisition.held || directWrites);}
struct {struct {unsigned brightness=light(BRIGHT),rotation=0;void setBrightness(unsigned value){brightness=value;}void setRotation(unsigned value){rotation=value;}uint8_t getRotation()const{return rotation;}}Display;}M5;
struct {std::string last;template<class... T>void printf(const char *format,T...){last=format;}void println(const char *text){last=text;}}Serial;
'''
# Exercise the actual startup/wake setting load and LCD mapping, in addition
# to the full edit handlers. No copied physical-orientation implementation.
load_start=source.index('  if(!DisplayFlipSetting::load(')
load_statement=source[load_start:source.index(';',load_start)+1]
boot_rotation=re.findall(r'M5.Display.setRotation\([^;]+;',source)[-1]
prefix+='\nstatic void restoreDisplayAtBoot(){'+load_statement+boot_rotation+'}\n'
mount_load_start=source.index('  if(!MountOrientationSetting::load(')
mount_load=source[mount_load_start:source.index('liveMount=mountMode;',mount_load_start)+len('liveMount=mountMode;')]
prefix+='\nstatic void restoreMountAtBoot(){'+mount_load+'assert(liveMount==mountMode && imuAcquisition.setMount(mountMode));}\n'
# The sketch's own boot loads of the RPM method, sensor profile, sleep and
# brightness, with their notices: everything from the RPM load up to the
# display flip load, which restoreDisplayAtBoot covers.
settings_start=source.index('  if(!RpmEstimatorSetting::load(prefs,rpmEstimator))')
settings_load=source[settings_start:load_start]
assert 'Brightness::load(prefs,brightnessPercent)' in settings_load
prefix+='\nstatic void restoreSettingsAtBoot(){'+settings_load+'}\n'
# The sketch's own boot load of the milestone record (Celebrate mode and
# table), including the one-time import of 0.15's record.
milestone_start=source.index('  milestoneStore.begin(milestones);')
milestone_load=source[milestone_start:source.index('  if(!effectSetStore.begin(',milestone_start)]
prefix+='\nstatic void restoreMilestonesAtBoot(){'+milestone_load+'}\n'
visible_start=source.index('static bool visibleDisplayFlipped()')
prefix+='\n'+source[visible_start:source.index('}',visible_start)+1]+'\n'
tests=r'''
static void reset(){
  controls={};appearance={};appearanceStore={};bestEffect={};tournamentMode=historyPage=dirty=diagnostics=demo=motionDemo=motionPending=settingsSaveError=restorePreview=displayFlipped=false;shakeToWake=ShakeWakeSetting::DEFAULT_ON;
  gestureA={};gestureB={};wipeConfirmation={};wipeFailed=wipeBlocked=false;
  historyOffset=newSessionAt=tournamentHintUntil=0;historyView=PracticeUI::View::Recent;tournamentView=TournamentView::RecordingOnly;
  practice={};demoPractice={};latestMotion={};launchFeedback={};inactivity={};brightnessPercent=BRIGHT;rpmEstimator=RpmEstimator::DEFAULT_MODE;sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
  acquisition={};prefs={};M5.Display.brightness=light(BRIGHT);M5.Display.rotation=2;
  mountMode=liveMount=MountOrientation::Mode::Top;imuAcquisition={};level={};drawnLevel={};lastLevelSampleUs=0;
  clockMs=10000;settingsSave={};extPower=directWrites=false;
  Preferences::data.clear();Preferences::zeroLengthKeys.clear();Preferences::failWrite=Preferences::shortRead=false;Preferences::beforeWrite=writeGuard;
  assert(prefs.begin("controls-test",false));assert(appearanceStore.begin(appearance,"controls-appearance","cfg0","cfg1"));
  milestones={};milestoneStore={};milestoneStore.begin(milestones,"controls-appearance");assert(milestoneStore.ready());
}
static void retry(uint32_t ms){clockMs+=ms;saveSettings(millis(),acquisition.snapshot());}
static uint8_t stored(const char *key){uint8_t value=0;assert(prefs.getBytes(key,&value,1)==1);return value;}
// Boot on the store as it is: the RPM method and brightness the sketch runs
// with, and whether boot wrote anything (it never migrates a record).
static void boot(){Serial.last.clear();rpmEstimator=RpmEstimator::Mode(99);brightnessPercent=0;prefs.writes=0;restoreSettingsAtBoot();}
static void freshStore(){reset();assert(prefs.begin("launchlab-mini",false));directWrites=true;}
// A minimal DeviceWipe store over the fake NVS map, so the real wipe
// transaction decides which namespaces are erased.
struct WipeStore {
  bool clearFlash(){return true;}
  static std::string key(const char *space,const char *name){return std::string(space)+name;}
  DeviceWipe::State inspectIntent(){
    const auto i=Preferences::data.find(key(DeviceWipe::INTENT_SPACE,DeviceWipe::INTENT_KEY));
    if(i==Preferences::data.end())return DeviceWipe::State::Clean;
    return i->second==std::vector<unsigned char>(std::begin(DeviceWipe::INTENT_BYTES),std::end(DeviceWipe::INTENT_BYTES))?
      DeviceWipe::State::Pending:DeviceWipe::State::Invalid;
  }
  bool setIntent(){Preferences::data[key(DeviceWipe::INTENT_SPACE,DeviceWipe::INTENT_KEY)]={std::begin(DeviceWipe::INTENT_BYTES),std::end(DeviceWipe::INTENT_BYTES)};return true;}
  bool clearNamespace(const char *space){
    if(!DeviceWipe::ownedNamespace(space))return false;
    for(auto i=Preferences::data.begin();i!=Preferences::data.end();)i=i->first.rfind(space,0)==0?Preferences::data.erase(i):std::next(i);
    return true;
  }
  bool clearIntent(){Preferences::data.erase(key(DeviceWipe::INTENT_SPACE,DeviceWipe::INTENT_KEY));return true;}
};
static void bootDefaults() {
  // The owner's choice for a unit with no saved value (2026-10-21).
  static_assert(RpmEstimator::DEFAULT_MODE==RpmEstimator::Mode::SingleTurn && Brightness::DEFAULT_PERCENT==30);
  static_assert(Milestones::DEFAULT_MODE==Milestones::Mode::Both);
  static_assert(Brightness::valid(Brightness::DEFAULT_PERCENT));
  // New unit: the defaults, and nothing is written.
  freshStore();boot();
  assert(rpmEstimator==RpmEstimator::DEFAULT_MODE && brightnessPercent==Brightness::DEFAULT_PERCENT && !prefs.writes && Serial.last.empty());
  assert(Preferences::data.empty());
  // Updated from 0.15 or 0.17 having changed other settings but never these
  // two: there is no record of them, so the unit moves to the defaults.
  freshStore();assert(prefs.putUChar("sleep_min",5)==1 && DisplayFlipSetting::save(prefs,true) && SensorProfileSetting::save(prefs,StickS3SensorProfile::Mode::Tcrt));
  {const auto before=Preferences::data;boot();
   assert(rpmEstimator==RpmEstimator::DEFAULT_MODE && brightnessPercent==Brightness::DEFAULT_PERCENT && Preferences::data==before && !prefs.writes);}
  // Every saved choice wins over the defaults and is never rewritten,
  // including the old defaults (3-turn average, 40%) saved by earlier firmware.
  for(auto method:{RpmEstimator::Mode::ThreeTurn,RpmEstimator::Mode::SingleTurn})for(unsigned percent=10;percent<=100;percent+=10) {
    freshStore();assert(RpmEstimatorSetting::save(prefs,method) && prefs.putUChar(Brightness::KEY,uint8_t(percent))==1);
    const auto before=Preferences::data;boot();
    assert(rpmEstimator==method && brightnessPercent==percent && Preferences::data==before && !prefs.writes && Serial.last.empty());
  }
  // Sleep after (the owner, 2026-10-26): 1 minute for a unit that never
  // chose; a saved choice, the old 3-minute default included, is kept; an
  // invalid saved value falls back to 1 and is kept as it is.
  static_assert(InactivityTimer::DEFAULT_MINUTES==1);
  freshStore();inactivity.setMinutes(10);boot();
  assert(inactivity.minutes()==1 && inactivity.timeout()==60000 && Preferences::data.empty() && !prefs.writes && Serial.last.empty());
  for(uint8_t minutes:{2,3,5,10}) {
    freshStore();assert(prefs.putUChar("sleep_min",minutes)==1);const auto before=Preferences::data;
    inactivity.setMinutes(minutes==10?2:10);boot();
    assert(inactivity.minutes()==minutes && Preferences::data==before && !prefs.writes && Serial.last.empty());
  }
  for(uint8_t bad:{0,11,255}) {
    freshStore();assert(prefs.putUChar("sleep_min",bad)==1);const auto before=Preferences::data;
    inactivity.setMinutes(10);boot();
    assert(inactivity.minutes()==1 && Preferences::data==before && !prefs.writes && Serial.last=="SLEEP_SETTING_NOTICE invalid_saved_value_using_%u_min\n");
  }
  // Shake to wake (0.20.2): On with no saved choice; a saved Off is kept;
  // an invalid record reads as On, is kept, and says so.
  freshStore();shakeToWake=false;boot();assert(shakeToWake && Preferences::data.empty() && !prefs.writes);
  freshStore();assert(prefs.putUChar(ShakeWakeSetting::KEY,0)==1);{const auto before=Preferences::data;shakeToWake=true;boot();
   assert(!shakeToWake && Preferences::data==before && !prefs.writes && Serial.last.empty());}
  freshStore();assert(prefs.putUChar(ShakeWakeSetting::KEY,9)==1);{const auto before=Preferences::data;shakeToWake=false;boot();
   assert(shakeToWake && Preferences::data==before && !prefs.writes && Serial.last=="SHAKE_WAKE_SETTING_NOTICE invalid_saved_value_retained_using_on");}
  // Invalid records: the defaults are used, the bytes are kept and each
  // notice names the default it fell back to.
  for(uint8_t percent:{0,5,45,110,255}) {
    freshStore();assert(RpmEstimatorSetting::save(prefs,RpmEstimator::Mode::ThreeTurn) && prefs.putUChar(Brightness::KEY,percent)==1);
    Preferences::data["launchlab-minirpm_method"][2]=7;const auto before=Preferences::data;boot();
    assert(rpmEstimator==RpmEstimator::DEFAULT_MODE && brightnessPercent==Brightness::DEFAULT_PERCENT && Preferences::data==before && !prefs.writes);
    assert(Serial.last.rfind("BRIGHTNESS_SETTING_NOTICE invalid_saved_value_using_default_pct=",0)==0);
  }
  {freshStore();assert(RpmEstimatorSetting::save(prefs,RpmEstimator::Mode::ThreeTurn));Preferences::data["launchlab-minirpm_method"][2]=7;
   boot();assert(rpmEstimator==RpmEstimator::DEFAULT_MODE && brightnessPercent==Brightness::DEFAULT_PERCENT);
   assert(Serial.last=="RPM_SETTING_NOTICE invalid_record_retained_using_default=%s\n");}
  // Wipe all erases the saved choices with the rest, so the unit restarts on the defaults.
  freshStore();assert(RpmEstimatorSetting::save(prefs,RpmEstimator::Mode::ThreeTurn) && prefs.putUChar(Brightness::KEY,40)==1);
  {WipeStore store;assert(DeviceWipe::begin(store)==DeviceWipe::Result::Complete && Preferences::data.empty());}
  boot();assert(rpmEstimator==RpmEstimator::DEFAULT_MODE && brightnessPercent==Brightness::DEFAULT_PERCENT && !prefs.writes && Serial.last.empty());
  directWrites=false;
}
// Boot the milestone record as the sketch does: its RAM copy starts as the
// defaults (a static Config) and only a readable saved record replaces it.
static void bootMilestones(){Serial.last.clear();milestones=Milestones::Config{};milestoneStore={};restoreMilestonesAtBoot();}
static void saveMilestones(Milestones::Mode mode,bool v1=false) {
  Milestones::Config c;
  if(v1){Milestones::V1Record r;AppearanceStore<Milestones::V1Record> s;assert(s.begin(r,"ll-appearance","mile0","mile1",true));
    assert(r.table.select(mode));if(r.table.isDefault())++r.table.revision;assert(s.save(r));return;}
  MilestoneStore s;s.begin(c);assert(s.ready());
  assert(c.select(mode));if(c.isDefault())++c.revision;assert(s.save(c));
}
static void bootCelebrateDefault() {
  using Milestones::Mode;
  const auto fresh=[](){freshStore();directWrites=true;};
  // New unit: Both with the default table, nothing written.
  fresh();bootMilestones();
  assert(milestones.mode()==Milestones::DEFAULT_MODE && milestones.isDefault() && milestones.count()==Milestones::DEFAULT_COUNT);
  assert(Preferences::data.empty() && Serial.last.empty());
  // Updated from 0.15 or 0.17 having changed other settings but never Celebrate: no record, so the default.
  fresh();assert(prefs.putUChar("sleep_min",5)==1 && RpmEstimatorSetting::save(prefs,RpmEstimator::Mode::ThreeTurn));
  {const auto before=Preferences::data;bootMilestones();assert(milestones.isDefault() && Preferences::data==before);}
  // A saved mode wins, from this firmware's record or 0.15's, the old default (New best) included.
  for(bool v1:{false,true})for(auto mode:{Mode::Best,Mode::Milestones,Mode::Both}) {
    fresh();saveMilestones(mode,v1);assert(!Preferences::data.empty());const auto before=Preferences::data;bootMilestones();
    assert(milestones.mode()==mode && Preferences::data==before);
    assert(v1?Serial.last=="MILESTONE_NOTICE imported_v1_table":Serial.last.empty());
  }
  // A damaged record: the defaults, the bytes kept, the notice.
  for(bool v1:{false,true}) {
    fresh();saveMilestones(Mode::Best,v1);
    for(auto &item:Preferences::data)if(item.first.rfind("ll-appearance",0)==0)item.second[item.second.size()/2]^=0xff;
    const auto before=Preferences::data;bootMilestones();
    assert(milestones.isDefault() && Preferences::data==before && Serial.last=="MILESTONE_NOTICE invalid_record_using_defaults");
  }
  // Wipe all erases the record, so the unit restarts on the default.
  fresh();saveMilestones(Mode::Best);
  {WipeStore store;assert(DeviceWipe::begin(store)==DeviceWipe::Result::Complete && Preferences::data.empty());}
  bootMilestones();assert(milestones.isDefault() && milestones.mode()==Milestones::DEFAULT_MODE && Serial.last.empty());
  directWrites=false;milestones={};milestoneStore={};
}
static void edit(unsigned row){controls.page=Page::Settings;controls.setting=row;syncNavigation();buttonA();assert(controls.page==Page::Edit);}
int main(){
  bootDefaults();bootCelebrateDefault();
  reset();restoreMountAtBoot();assert(mountMode==MountOrientation::Mode::Top && !prefs.writes);
  for(auto selected:{MountOrientation::Mode::Sidecar,MountOrientation::Mode::Top}) {
    directWrites=true;assert(MountOrientationSetting::save(prefs,selected));directWrites=false;const auto stored=Preferences::data;
    mountMode=MountOrientation::Mode(99);restoreMountAtBoot();
    assert(mountMode==selected && imuAcquisition.mode==selected && Preferences::data==stored);
  }
  reset();restoreDisplayAtBoot();assert(!displayFlipped && M5.Display.rotation==2 && !prefs.writes);
  for(bool saved:{true,false}) {
    directWrites=true;assert(DisplayFlipSetting::save(prefs,saved));directWrites=false;const auto stored=Preferences::data;
    displayFlipped=!saved;M5.Display.rotation=99;restoreDisplayAtBoot();
    assert(displayFlipped==saved && M5.Display.rotation==(saved?0u:2u) && Preferences::data==stored);
  }
  reset();buttonA();holdB();assert(!launchFeedback.shown() && controls.page==Page::Main);
  reset();edit(2);acquisition.state.phase=AnalogTachometer::Phase::Launch;buttonB();
  assert(controls.draft==BRIGHT+10 && brightnessPercent==BRIGHT && M5.Display.brightness==light(BRIGHT) && restorePreview);
  acquisition.state.phase=AnalogTachometer::Phase::Ready;previewSetting();
  assert(M5.Display.brightness==light(BRIGHT+10) && !restorePreview);holdB();assert(M5.Display.brightness==light(BRIGHT) && !prefs.writes);
  reset();buttonA();assert(launchFeedback.shown());buttonB();assert(!launchFeedback.shown() && controls.page==Page::Menu);
  buttonA();assert(controls.page==Page::HistoryMenu);buttonA();assert(controls.page==Page::History);
  assert(practice.accept(1,true,6000,millis()));buttonB();holdB();assert(controls.page==Page::HistoryMenu);
  holdB();holdB();assert(controls.page==Page::Main && !historyPage);
  reset();edit(2);buttonB();assert(controls.draft==BRIGHT+10 && brightnessPercent==BRIGHT && M5.Display.brightness==light(BRIGHT+10) && !prefs.writes);
  holdB();assert(controls.page==Page::Settings && brightnessPercent==BRIGHT && M5.Display.brightness==light(BRIGHT) && !prefs.writes);
  edit(2);buttonB();buttonA();assert(controls.page==Page::Settings && brightnessPercent==BRIGHT+10 && M5.Display.brightness==light(BRIGHT+10) && prefs.writes && !settingsSaveError);
  assert(!settingsSave.pending() && stored(Brightness::KEY)==BRIGHT+10 && acquisition.begins==1 && acquisition.begins==acquisition.ends && !acquisition.held);
  // Applying the unchanged value closes the editor without a checkpoint.
  edit(2);buttonA();assert(controls.page==Page::Settings && acquisition.begins==1 && !settingsSave.pending());
  // The reported failure: pressing A jiggles the launcher, so the owner sees a
  // fresh strong mark at that instant. Apply still takes the value, and the
  // record follows once the signal is quiet, without another press.
  edit(2);buttonB();acquisition.quietSignal=false;buttonA();
  assert(controls.page==Page::Settings && !settingsSaveError && brightnessPercent==BRIGHT+20 && M5.Display.brightness==light(BRIGHT+20));
  assert(settingsSave.pending()==SettingsSave::Brightness && !acquisition.refusals && stored(Brightness::KEY)==BRIGHT+10);
  retry(20);assert(!acquisition.refusals && settingsSave.pending()); // No request while the owner would refuse.
  acquisition.quietSignal=true;retry(20);assert(!settingsSave.pending() && stored(Brightness::KEY)==BRIGHT+20);
  // A mark that reaches the owner after the idle snapshot: one refusal, then
  // a bounded retry (no IMU quiet wait on every UI tick).
  edit(2);buttonB();acquisition.refuseNext=true;buttonA();
  assert(controls.page==Page::Settings && brightnessPercent==BRIGHT+30 && acquisition.refusals==1 && settingsSave.refusals()==1 && settingsSave.pending());
  retry(SettingsSave::RETRY_MS-1);assert(settingsSave.pending() && acquisition.refusals==1);
  retry(1);assert(!settingsSave.pending() && stored(Brightness::KEY)==BRIGHT+30 && acquisition.begins==acquisition.ends);
  reset();edit(3);assert(!visibleDisplayFlipped());buttonB();assert(!displayFlipped && visibleDisplayFlipped() && M5.Display.rotation==0 && !prefs.writes);
  holdB();assert(!visibleDisplayFlipped() && M5.Display.rotation==2 && !prefs.writes);
  // A failed write keeps the applied choice and its record pending, retried
  // after the longer failure backoff and shown as Not saved meanwhile.
  edit(3);buttonB();Preferences::failWrite=true;buttonA();
  assert(controls.page==Page::Settings && !settingsSaveError && displayFlipped && visibleDisplayFlipped() && M5.Display.rotation==0);
  assert(settingsSave.failed() && settingsSave.pending()==SettingsSave::Flip && settingsSave.errors()==1 && acquisition.begins==acquisition.ends);
  Preferences::failWrite=false;retry(SettingsSave::RETRY_MS);assert(settingsSave.pending() && acquisition.begins==1);
  retry(SettingsSave::FAILED_RETRY_MS-SettingsSave::RETRY_MS);assert(!settingsSave.pending() && !settingsSave.failed());
  {bool flipped=false;assert(DisplayFlipSetting::load(prefs,flipped) && flipped);}
  const auto flipStored=Preferences::data;edit(3);buttonB();assert(!visibleDisplayFlipped() && displayFlipped && M5.Display.rotation==2);
  holdB();assert(visibleDisplayFlipped() && M5.Display.rotation==0 && Preferences::data==flipStored);
  // During a pull the display changes are deferred; cancel returns to the
  // saved orientation as soon as idle without persisting the draft.
  edit(3);acquisition.state.phase=AnalogTachometer::Phase::Launch;buttonB();
  assert(visibleDisplayFlipped() && M5.Display.rotation==0 && restorePreview);
  holdB();assert(visibleDisplayFlipped() && M5.Display.rotation==0 && restorePreview && Preferences::data==flipStored);
  acquisition.state.phase=AnalogTachometer::Phase::Ready;previewSetting();assert(!restorePreview && M5.Display.rotation==0);
  // Apply during a pull or capture: the choice is taken, its record waits.
  reset();edit(0);for(unsigned i=0;i<9;++i)buttonB();assert(controls.draft==10 && inactivity.minutes()==1 && !prefs.writes);
  acquisition.state.phase=AnalogTachometer::Phase::Launch;buttonA();
  assert(controls.page==Page::Settings && !settingsSaveError && inactivity.minutes()==10 && !prefs.writes && settingsSave.pending()==SettingsSave::Sleep);
  acquisition.state.phase=AnalogTachometer::Phase::Ready;motionPending=true;retry(20);assert(!prefs.writes && !acquisition.refusals);
  motionPending=false;retry(20);assert(!settingsSave.pending() && stored("sleep_min")==10 && !acquisition.refusals);
  reset();assert(practice.accept(1,true,6000,millis()));const auto session=practice.session()->number;
  const auto otherMethod=RpmEstimator::next(RpmEstimator::DEFAULT_MODE);
  edit(1);buttonB();assert(rpmEstimator==RpmEstimator::DEFAULT_MODE && practice.session()->number==session);
  holdB();assert(practice.accept(2,true,6100,millis()) && practice.session()->number==session);
  edit(1);buttonB();buttonA();assert(rpmEstimator==otherMethod && acquisition.estimatorChanges==1 && acquisition.state.estimator==rpmEstimator);
  assert(practice.accept(3,true,6200,millis()) && practice.session()->number==session+1 && practice.size()==3);
  // A metric switch the owner refuses keeps the old live metric, record and
  // session until it succeeds; the choice itself is kept and retried.
  reset();assert(practice.accept(1,true,6000,millis()));const auto metricSession=practice.session()->number;
  edit(1);buttonB();acquisition.failEstimator=true;buttonA();
  assert(controls.page==Page::Settings && rpmEstimator==otherMethod && acquisition.state.estimator==RpmEstimator::DEFAULT_MODE && settingsSave.failed());
  RpmEstimator::Mode saved;assert(RpmEstimatorSetting::load(prefs,saved) && saved==RpmEstimator::DEFAULT_MODE);
  assert(practice.accept(2,true,6100,millis()) && practice.session()->number==metricSession);
  acquisition.failEstimator=false;retry(SettingsSave::FAILED_RETRY_MS);
  assert(acquisition.state.estimator==rpmEstimator && !settingsSave.pending() && RpmEstimatorSetting::load(prefs,saved) && saved==rpmEstimator);
  assert(practice.accept(3,true,6200,millis()) && practice.session()->number==metricSession+1);
  reset();assert(practice.accept(1,true,6000,millis()));const auto profileSession=practice.session()->number;
  edit(4);buttonB();assert(controls.draft==1 && sensorProfile==StickS3SensorProfile::Mode::Standard && !prefs.writes);
  holdB();assert(sensorProfile==StickS3SensorProfile::Mode::Standard && !prefs.writes && !acquisition.profileChanges);
  edit(4);buttonB();acquisition.state.phase=AnalogTachometer::Phase::Launch;buttonA();
  assert(controls.page==Page::Settings && !settingsSaveError && sensorProfile==StickS3SensorProfile::Mode::Tcrt && !prefs.writes && !acquisition.profileChanges);
  assert(acquisition.state.sensorProfile==StickS3SensorProfile::Mode::Standard);
  acquisition.state.phase=AnalogTachometer::Phase::Ready;retry(20);
  assert(acquisition.profileChanges==1 && acquisition.state.sensorProfile==sensorProfile && !settingsSave.pending());
  StickS3SensorProfile::Mode profileSaved;
  assert(SensorProfileSetting::load(prefs,profileSaved) && profileSaved==sensorProfile);
  assert(practice.accept(2,true,6100,millis()) && practice.session()->number==profileSession+1);
  const auto writes=prefs.writes;edit(4);buttonA();assert(acquisition.profileChanges==1 && prefs.writes==writes);
  reset();edit(4);buttonB();acquisition.failProfile=true;buttonA();
  assert(controls.page==Page::Settings && sensorProfile==StickS3SensorProfile::Mode::Tcrt && acquisition.state.sensorProfile==StickS3SensorProfile::Mode::Standard && settingsSave.failed());
  assert(SensorProfileSetting::load(prefs,profileSaved) && profileSaved==StickS3SensorProfile::Mode::Standard);
  // The profile switch itself succeeds; only its record waits for the retry.
  reset();edit(4);buttonB();Preferences::failWrite=true;buttonA();
  assert(controls.page==Page::Settings && acquisition.profileChanges==1 && acquisition.state.sensorProfile==StickS3SensorProfile::Mode::Tcrt);
  assert(settingsSave.failed() && settingsSave.pending()==SettingsSave::Sensor);
  Preferences::failWrite=false;retry(SettingsSave::FAILED_RETRY_MS);
  assert(acquisition.profileChanges==1 && !settingsSave.pending() && SensorProfileSetting::load(prefs,profileSaved) && profileSaved==sensorProfile);
  assert(acquisition.begins==acquisition.ends);
  reset();const auto untouched=practice.generation();edit(5);buttonB();assert(controls.draft==1 && appearance.theme==Appearance::Theme::Classic);holdB();assert(appearance.theme==Appearance::Theme::Classic);
  edit(5);buttonB();buttonA();assert(appearance.theme==Appearance::Theme::Mint && practice.generation()==untouched && !acquisition.estimatorChanges && !acquisition.profileChanges && !settingsSave.pending());
  // Random is the default and comes first; B steps to Orbit and previews it.
  edit(unsigned(Controls::Setting::BestEffect));assert(controls.draft==uint8_t(Appearance::Effect::Random));const unsigned shown=effectPreviews;buttonB();assert(effectPreviews==shown+1);
  Preferences::failWrite=true;buttonA();assert(!settingsSaveError && appearance.effect==Appearance::Effect::Orbit && controls.page==Page::Settings && settingsSave.failed());
  Preferences::failWrite=false;retry(SettingsSave::FAILED_RETRY_MS);assert(!settingsSave.pending());
  Appearance::Config restored;AppearanceStore<Appearance::Config> reader;assert(reader.begin(restored,"controls-appearance","cfg0","cfg1") && restored.theme==appearance.theme && restored.effect==appearance.effect);
  // The effect set: B visits Random, the offered effects and Off only. A saved
  // effect outside the set opens the editor and Apply keeps it; B moves on
  // from its position to the next offered one.
  {
    using Appearance::Effect;using EffectSet::flag;
    controls.offeredEffects=flag(Effect::Slash)|flag(Effect::Crimson)|flag(Effect::Ravage);
    edit(unsigned(Controls::Setting::BestEffect));assert(controls.draft==uint8_t(Effect::Orbit));
    const Effect expected[]={Effect::Slash,Effect::Crimson,Effect::Ravage,Effect::Off,Effect::Random,Effect::Slash};
    for(Effect want:expected){buttonB();assert(controls.draft==uint8_t(want));}
    holdB();assert(appearance.effect==Effect::Orbit);
    edit(unsigned(Controls::Setting::BestEffect));buttonA();retry(SettingsSave::RETRY_MS);
    assert(appearance.effect==Effect::Orbit && controls.page==Page::Settings && !settingsSaveError);
    edit(unsigned(Controls::Setting::BestEffect));buttonB();buttonB();buttonA();retry(SettingsSave::RETRY_MS);
    assert(appearance.effect==Effect::Crimson && !settingsSave.pending());
    controls.offeredEffects=EffectSet::DEFAULT_MASK;
    edit(unsigned(Controls::Setting::BestEffect));buttonB();assert(controls.draft==uint8_t(Effect::Off));
    for(unsigned i=0;i<2;++i)buttonB();assert(controls.draft==uint8_t(Effect::Orbit));buttonA();retry(SettingsSave::RETRY_MS);
    assert(appearance.effect==Effect::Orbit && !settingsSave.pending());
  }
  // Sound: each B previews the draft level; only Apply saves it, with the theme and effect intact.
  edit(unsigned(Controls::Setting::Sound));assert(controls.draft==0);const unsigned previews=soundPreviews;buttonB();buttonB();
  assert(controls.draft==2 && soundPreviews==previews+2 && appearance.sound==Appearance::Sound::Off);
  holdB();assert(appearance.sound==Appearance::Sound::Off);
  edit(unsigned(Controls::Setting::Sound));buttonB();buttonA();assert(appearance.sound==Appearance::Sound::Low && controls.page==Page::Settings);
  {Appearance::Config again;AppearanceStore<Appearance::Config> reread;assert(reread.begin(again,"controls-appearance","cfg0","cfg1") &&
    again.sound==Appearance::Sound::Low && again.theme==Appearance::Theme::Mint && again.effect==Appearance::Effect::Orbit);}
  // An unreadable stored look cannot be replaced: Apply says so and keeps the
  // look shown, rather than one that would revert at the next boot.
  reset();Preferences::data["controls-appearancecfg0"]={1,2,3};appearanceStore={};
  assert(!appearanceStore.begin(appearance,"controls-appearance","cfg0","cfg1"));
  edit(5);buttonB();buttonA();assert(controls.page==Page::Edit && settingsSaveError && appearance.theme==Appearance::Theme::Classic && !settingsSave.pending());
  // Mount drafts never change live axes or flash. The new frame is switched to
  // inside the save checkpoint, never during a pull or its capture (switching
  // clears the IMU ring), and resets live level; scalar history/session and
  // Flip are preserved.
  reset();assert(practice.accept(1,true,6000,millis()));const auto mountSession=practice.session()->number;
  level.feed(.1f,0,1,millis());drawnLevel=level.reading(millis());lastLevelSampleUs=12345;
  edit(unsigned(Controls::Setting::Mount));assert(controls.draft==0);buttonB();
  assert(controls.draft==1 && mountMode==MountOrientation::Mode::Top && !prefs.writes && !imuAcquisition.changes);
  holdB();assert(mountMode==MountOrientation::Mode::Top && !prefs.writes && !imuAcquisition.changes);
  edit(unsigned(Controls::Setting::Mount));buttonB();acquisition.state.phase=AnalogTachometer::Phase::Launch;buttonA();
  assert(controls.page==Page::Settings && !settingsSaveError && mountMode==MountOrientation::Mode::Sidecar);
  assert(liveMount==MountOrientation::Mode::Top && !prefs.writes && !imuAcquisition.changes && lastLevelSampleUs==12345);
  acquisition.state.phase=AnalogTachometer::Phase::Ready;motionPending=true;retry(20);
  assert(liveMount==MountOrientation::Mode::Top && !prefs.writes && !imuAcquisition.changes && lastLevelSampleUs==12345);
  motionPending=false;retry(20);
  assert(liveMount==MountOrientation::Mode::Sidecar && imuAcquisition.mode==mountMode && imuAcquisition.changes==1 && !settingsSave.pending());
  assert(controls.page==Page::Settings && !settingsSaveError && !displayFlipped && M5.Display.rotation==2);
  assert(!level.reading(millis()).valid() && !drawnLevel.valid() && lastLevelSampleUs==0);
  assert(practice.accept(2,true,6100,millis()) && practice.session()->number==mountSession && practice.size()==2);
  MountOrientation::Mode mountSaved;assert(MountOrientationSetting::load(prefs,mountSaved) && mountSaved==mountMode);
  const auto mountBytes=Preferences::data;const auto mountChanges=imuAcquisition.changes;
  edit(unsigned(Controls::Setting::Mount));buttonA();assert(Preferences::data==mountBytes && imuAcquisition.changes==mountChanges);
  // A busy IMU owner keeps the old frame and record until it can switch.
  edit(unsigned(Controls::Setting::Mount));buttonB();imuAcquisition.fail=true;buttonA();
  assert(controls.page==Page::Settings && mountMode==MountOrientation::Mode::Top && liveMount==MountOrientation::Mode::Sidecar && settingsSave.failed());
  assert(MountOrientationSetting::load(prefs,mountSaved) && mountSaved==MountOrientation::Mode::Sidecar);
  imuAcquisition.fail=false;Preferences::failWrite=true;retry(SettingsSave::FAILED_RETRY_MS);
  assert(liveMount==MountOrientation::Mode::Top && imuAcquisition.changes==mountChanges+1 && settingsSave.failed());
  Preferences::failWrite=false;retry(SettingsSave::FAILED_RETRY_MS);
  assert(!settingsSave.pending() && MountOrientationSetting::load(prefs,mountSaved) && mountSaved==mountMode && imuAcquisition.changes==mountChanges+1);
  assert(acquisition.begins==acquisition.ends);
  // Celebrate: drafts change nothing; Apply takes the mode at once and saves
  // it with the milestone table, inside a checkpoint, leaving the table and
  // the look alone.
  {
    using Milestones::Mode;const auto row=unsigned(Controls::Setting::Celebrate);
    // B steps through the modes in order, from the default.
    const auto step=[](unsigned k){return Mode((unsigned(Milestones::DEFAULT_MODE)+k)%Milestones::MODES);};
    reset();const unsigned effects=effectPreviews,sounds=soundPreviews;edit(row);assert(controls.draft==uint8_t(step(0)));buttonB();
    assert(controls.draft==uint8_t(step(1)) && milestones.mode()==step(0) && !prefs.writes && effectPreviews==effects && soundPreviews==sounds);
    holdB();assert(milestones.mode()==step(0) && !prefs.writes);
    edit(row);buttonB();buttonB();assert(controls.draft==uint8_t(step(2)));buttonB();assert(controls.draft==uint8_t(step(0)));
    buttonB();buttonB();acquisition.state.phase=AnalogTachometer::Phase::Launch;buttonA();
    assert(controls.page==Page::Settings && milestones.mode()==step(2) && settingsSave.pending()==SettingsSave::Milestones && !prefs.writes);
    acquisition.state.phase=AnalogTachometer::Phase::Ready;retry(20);
    assert(!settingsSave.pending() && acquisition.begins==acquisition.ends);
    Milestones::Config reread;MilestoneStore reader;
    assert(reader.begin(reread,"controls-appearance") && reread.mode()==step(2) && reread.same(milestones));
    assert(appearance.theme==Appearance::Theme::Classic && appearance.effect==Appearance::Effect::Random);
    // A damaged record shows the defaults and the next Apply replaces it.
    reset();Preferences::data["controls-appearancempool0"]={1,2,3};Preferences::data["controls-appearancempool1"]={4,5,6};milestoneStore={};
    assert(!milestoneStore.begin(milestones,"controls-appearance") && milestoneStore.damaged() && milestoneStore.ready());
    assert(milestones.isDefault());
    edit(row);buttonB();buttonA();assert(controls.page==Page::Settings && !settingsSaveError && milestones.mode()==step(1) && !settingsSave.pending());
    Milestones::Config fixed;MilestoneStore again;
    assert(again.begin(fixed,"controls-appearance") && !again.damaged() && fixed.mode()==step(1));
  }
  reset();holdA();assert(tournamentMode && tournamentView==TournamentView::RecordingOnly);
  buttonA();holdB();assert(tournamentMode && !launchFeedback.shown());buttonB();assert(tournamentView==TournamentView::Rpm);
  holdA();assert(!tournamentMode && controls.page==Page::Main);
  reset();edit(3);buttonB();cancelSettingEdit();syncNavigation();assert(controls.page==Page::Settings && M5.Display.rotation==2 && !prefs.writes);
  reset();controls.page=Page::Settings;controls.setting=unsigned(Controls::Setting::WipeAll);buttonA();
  assert(controls.page==Page::Wipe && historyPage && wipeConfirmation.active() && !prefs.writes);
  for(unsigned i=0;i<50;++i){buttonA();holdA();}
  assert(wipeConfirmation.stage()==WipeConfirmation::Stage::Warning && !prefs.writes);
  holdB();assert(controls.page==Page::Settings && !wipeConfirmation.active() && !prefs.writes);
  buttonA();wipeConfirmation.tick(millis(),false,false);buttonA();
  assert(wipeConfirmation.stage()==WipeConfirmation::Stage::Confirm);
  buttonB();assert(controls.page==Page::Settings && !wipeConfirmation.active() && !prefs.writes);
  buttonA();wipeBlocked=true;holdB();assert(controls.page==Page::Wipe && !wipeConfirmation.active());
  std::cout<<"PASS: actual control handlers; menu/back, recap, preview rollback, Apply never refused by a busy signal, queued checkpoint-only writes with bounded retry, active-pull/capture guard, metric/session boundary at the switch, sound preview/apply and tournament lock\n";
}
'''
def definition(signature):
    start=source.index(signature)
    cursor=source.index('{',start)+1
    depth=1
    while depth:
        depth+=(source[cursor]=='{')-(source[cursor]=='}')
        cursor+=1
    return source[start:cursor]
prefix+=definition('static uint8_t settingValue(unsigned row)')+'\n'
names=['syncNavigation','toggleFeedback','previewSetting','beginWipe','cancelWipe','cancelSettingEdit','writeSettings','saveSettings','commitSetting','newSession','browseOlder','navigate','buttonA','buttonB','holdA','holdB']
with tempfile.TemporaryDirectory(prefix='launchlab-controls-') as directory:
    directory=Path(directory);cpp=directory/'test.cpp';exe=directory/'test'
    cpp.write_text(prefix+'\n'.join(function(name) for name in names)+tests)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                    '-I'+str(root/'tests/fakes'),'-I'+str(root/'LaunchLabMini'),'-I'+str(root/'LaunchLabRpm'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
