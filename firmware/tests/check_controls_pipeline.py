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
#include "theme_library.h"
#include "analog_tachometer.h"
#include "rpm_estimator_setting.h"
#include "display_flip_setting.h"
#include "display_orientation.h"
#include "sensor_profile_setting.h"
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
static Milestones::Config milestones;static AppearanceStore<Milestones::Config> milestoneStore;
static uint32_t clockMs=10000;static uint32_t millis(){return clockMs;}
static unsigned soundPreviews=0,effectPreviews=0;static void previewSound(){++soundPreviews;}static void previewEffect(){++effectPreviews;}
using Controls::Page;
static Controls::State controls;
static Controls::Gesture gestureA,gestureB;
static WipeConfirmation wipeConfirmation;
static bool wipeFailed=false,wipeBlocked=false;
static struct {void cancel(){}}restoreRx;
static void beginWipe();static void cancelWipe();
static bool tournamentMode=false,historyPage=false,dirty=false,diagnostics=false,demo=false,motionDemo=false,motionPending=false,settingsSaveError=false,restorePreview=false,displayFlipped=false;
static uint8_t brightnessPercent=40;
static uint32_t historyOffset=0,newSessionAt=0,tournamentHintUntil=0;
enum class TournamentView:uint8_t{RecordingOnly,Rpm};
static TournamentView tournamentView=TournamentView::RecordingOnly;
static const char *tournamentViewName(){return tournamentView==TournamentView::Rpm?"rpm":"recording_only";}
static PracticeUI::View historyView=PracticeUI::View::Recent;
static PracticeHistory practice,demoPractice;
static struct {uint32_t number=1;} latestMotion;
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
struct {struct {unsigned brightness=102,rotation=0;void setBrightness(unsigned value){brightness=value;}void setRotation(unsigned value){rotation=value;}uint8_t getRotation()const{return rotation;}}Display;}M5;
struct {template<class... T>void printf(const char*,T...){}void println(const char*){}}Serial;
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
visible_start=source.index('static bool visibleDisplayFlipped()')
prefix+='\n'+source[visible_start:source.index('}',visible_start)+1]+'\n'
tests=r'''
static void reset(){
  controls={};appearance={};appearanceStore={};bestEffect={};tournamentMode=historyPage=dirty=diagnostics=demo=motionDemo=motionPending=settingsSaveError=restorePreview=displayFlipped=false;
  gestureA={};gestureB={};wipeConfirmation={};wipeFailed=wipeBlocked=false;
  historyOffset=newSessionAt=tournamentHintUntil=0;historyView=PracticeUI::View::Recent;tournamentView=TournamentView::RecordingOnly;
  practice={};demoPractice={};latestMotion={};launchFeedback={};inactivity={};brightnessPercent=40;rpmEstimator=RpmEstimator::DEFAULT_MODE;sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
  acquisition={};prefs={};M5.Display.brightness=102;M5.Display.rotation=2;
  mountMode=liveMount=MountOrientation::Mode::Top;imuAcquisition={};level={};drawnLevel={};lastLevelSampleUs=0;
  clockMs=10000;settingsSave={};extPower=directWrites=false;
  Preferences::data.clear();Preferences::zeroLengthKeys.clear();Preferences::failWrite=Preferences::shortRead=false;Preferences::beforeWrite=writeGuard;
  assert(prefs.begin("controls-test",false));assert(appearanceStore.begin(appearance,"controls-appearance","cfg0","cfg1"));
  milestones={};milestoneStore={};assert(milestoneStore.begin(milestones,"controls-appearance","mile0","mile1",true));
}
static void retry(uint32_t ms){clockMs+=ms;saveSettings(millis(),acquisition.snapshot());}
static uint8_t stored(const char *key){uint8_t value=0;assert(prefs.getBytes(key,&value,1)==1);return value;}
static void edit(unsigned row){controls.page=Page::Settings;controls.setting=row;syncNavigation();buttonA();assert(controls.page==Page::Edit);}
int main(){
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
  assert(controls.draft==50 && brightnessPercent==40 && M5.Display.brightness==102 && restorePreview);
  acquisition.state.phase=AnalogTachometer::Phase::Ready;previewSetting();
  assert(M5.Display.brightness==128 && !restorePreview);holdB();assert(M5.Display.brightness==102 && !prefs.writes);
  reset();buttonA();assert(launchFeedback.shown());buttonB();assert(!launchFeedback.shown() && controls.page==Page::Menu);
  buttonA();assert(controls.page==Page::HistoryMenu);buttonA();assert(controls.page==Page::History);
  assert(practice.accept(1,true,6000,millis()));buttonB();holdB();assert(controls.page==Page::HistoryMenu);
  holdB();holdB();assert(controls.page==Page::Main && !historyPage);
  reset();edit(2);buttonB();assert(controls.draft==50 && brightnessPercent==40 && M5.Display.brightness==128 && !prefs.writes);
  holdB();assert(controls.page==Page::Settings && brightnessPercent==40 && M5.Display.brightness==102 && !prefs.writes);
  edit(2);buttonB();buttonA();assert(controls.page==Page::Settings && brightnessPercent==50 && M5.Display.brightness==128 && prefs.writes && !settingsSaveError);
  assert(!settingsSave.pending() && stored("bright_pct")==50 && acquisition.begins==1 && acquisition.begins==acquisition.ends && !acquisition.held);
  // Applying the unchanged value closes the editor without a checkpoint.
  edit(2);buttonA();assert(controls.page==Page::Settings && acquisition.begins==1 && !settingsSave.pending());
  // The reported failure: pressing A jiggles the launcher, so the owner sees a
  // fresh strong mark at that instant. Apply still takes the value, and the
  // record follows once the signal is quiet, without another press.
  edit(2);buttonB();acquisition.quietSignal=false;buttonA();
  assert(controls.page==Page::Settings && !settingsSaveError && brightnessPercent==60 && M5.Display.brightness==153);
  assert(settingsSave.pending()==SettingsSave::Brightness && !acquisition.refusals && stored("bright_pct")==50);
  retry(20);assert(!acquisition.refusals && settingsSave.pending()); // No request while the owner would refuse.
  acquisition.quietSignal=true;retry(20);assert(!settingsSave.pending() && stored("bright_pct")==60);
  // A mark that reaches the owner after the idle snapshot: one refusal, then
  // a bounded retry (no IMU quiet wait on every UI tick).
  edit(2);buttonB();acquisition.refuseNext=true;buttonA();
  assert(controls.page==Page::Settings && brightnessPercent==70 && acquisition.refusals==1 && settingsSave.refusals()==1 && settingsSave.pending());
  retry(SettingsSave::RETRY_MS-1);assert(settingsSave.pending() && acquisition.refusals==1);
  retry(1);assert(!settingsSave.pending() && stored("bright_pct")==70 && acquisition.begins==acquisition.ends);
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
  reset();edit(0);for(unsigned i=0;i<7;++i)buttonB();assert(controls.draft==10 && inactivity.minutes()==3 && !prefs.writes);
  acquisition.state.phase=AnalogTachometer::Phase::Launch;buttonA();
  assert(controls.page==Page::Settings && !settingsSaveError && inactivity.minutes()==10 && !prefs.writes && settingsSave.pending()==SettingsSave::Sleep);
  acquisition.state.phase=AnalogTachometer::Phase::Ready;motionPending=true;retry(20);assert(!prefs.writes && !acquisition.refusals);
  motionPending=false;retry(20);assert(!settingsSave.pending() && stored("sleep_min")==10 && !acquisition.refusals);
  reset();assert(practice.accept(1,true,6000,millis()));const auto session=practice.session()->number;
  edit(1);buttonB();assert(rpmEstimator==RpmEstimator::Mode::ThreeTurn && practice.session()->number==session);
  holdB();assert(practice.accept(2,true,6100,millis()) && practice.session()->number==session);
  edit(1);buttonB();buttonA();assert(rpmEstimator==RpmEstimator::Mode::SingleTurn && acquisition.estimatorChanges==1 && acquisition.state.estimator==rpmEstimator);
  assert(practice.accept(3,true,6200,millis()) && practice.session()->number==session+1 && practice.size()==3);
  // A metric switch the owner refuses keeps the old live metric, record and
  // session until it succeeds; the choice itself is kept and retried.
  reset();assert(practice.accept(1,true,6000,millis()));const auto metricSession=practice.session()->number;
  edit(1);buttonB();acquisition.failEstimator=true;buttonA();
  assert(controls.page==Page::Settings && rpmEstimator==RpmEstimator::Mode::SingleTurn && acquisition.state.estimator==RpmEstimator::Mode::ThreeTurn && settingsSave.failed());
  RpmEstimator::Mode saved;assert(RpmEstimatorSetting::load(prefs,saved) && saved==RpmEstimator::Mode::ThreeTurn);
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
    reset();const unsigned effects=effectPreviews,sounds=soundPreviews;edit(row);assert(controls.draft==uint8_t(Mode::Best));buttonB();
    assert(controls.draft==uint8_t(Mode::Milestones) && milestones.mode()==Mode::Best && !prefs.writes && effectPreviews==effects && soundPreviews==sounds);
    holdB();assert(milestones.mode()==Mode::Best && !prefs.writes);
    edit(row);buttonB();buttonB();assert(controls.draft==uint8_t(Mode::Both));buttonB();assert(controls.draft==uint8_t(Mode::Best));
    buttonB();buttonB();acquisition.state.phase=AnalogTachometer::Phase::Launch;buttonA();
    assert(controls.page==Page::Settings && milestones.mode()==Mode::Both && settingsSave.pending()==SettingsSave::Milestones && !prefs.writes);
    acquisition.state.phase=AnalogTachometer::Phase::Ready;retry(20);
    assert(!settingsSave.pending() && acquisition.begins==acquisition.ends);
    Milestones::Config reread;AppearanceStore<Milestones::Config> reader;
    assert(reader.begin(reread,"controls-appearance","mile0","mile1",true) && reread.mode()==Mode::Both && reread.same(milestones));
    assert(appearance.theme==Appearance::Theme::Classic && appearance.effect==Appearance::Effect::Random);
    // A damaged record shows the defaults and the next Apply replaces it.
    reset();Preferences::data["controls-appearancemile0"]={1,2,3};Preferences::data["controls-appearancemile1"]={4,5,6};milestoneStore={};
    assert(!milestoneStore.begin(milestones,"controls-appearance","mile0","mile1",true) && milestoneStore.damaged() && milestoneStore.ready());
    assert(milestones.isDefault());
    edit(row);buttonB();buttonA();assert(controls.page==Page::Settings && !settingsSaveError && milestones.mode()==Mode::Milestones && !settingsSave.pending());
    Milestones::Config fixed;AppearanceStore<Milestones::Config> again;
    assert(again.begin(fixed,"controls-appearance","mile0","mile1",true) && !again.damaged() && fixed.mode()==Mode::Milestones);
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
