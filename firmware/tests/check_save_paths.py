#!/usr/bin/env python3
"""Run the exact sketch save paths against the complete production ADC owner.

The acquisition owner (acquisition.h), its request() hook and reply-timeout
path, the detector and the stores are real. A host pump runs queued owner
commands as the core-0 task would; IMU quiet, NVS and the clock are fakes. The
fake Preferences fails the run on any write outside a granted checkpoint with
the IMU quiet window held. No device, NVS partition or user data is accessed.
"""
import argparse
import os
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('firmware', nargs='?', type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parents[1])
args = parser.parse_args()
source = (args.firmware / 'LaunchLabMini/LaunchLabMini.ino').read_text()


def body(start):
    cursor = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[start:cursor]


def function(name, result='void'):
    signature = 'static ' + result + ' ' + name + '('
    start = source.index(signature)
    while source.index(';', start) < source.index('{', start):
        start = source.index(signature, start + 1)
    return body(start)


def statement(prefix):
    start = source.index(prefix)
    return source[start:source.index(';', start) + 1]


def block(prefix):
    start = source.index(prefix)
    return source[start:source.index('}', start) + 1]


def shutdown_flush():
    enum = source.index('enum class ShutdownFlush')
    return source[enum:source.index('};', enum) + 2] + '\n' + body(source.index('static ShutdownFlush flushForShutdown('))


prefix = r'''
#include "fake_runtime.h"
#include "sensor_profile.h"
#include "rpm_estimator.h"
#include "sample_ring.h"
#include "history_checkpoint.h"
// Expose the owner's internals only to drive its command loop and samples.
#define private public
#include "acquisition.h"
#undef private
#include "controls.h"
#include "appearance_store.h"
#include "practice_store.h"
#include "rpm_estimator_setting.h"
#include "display_flip_setting.h"
#include "display_orientation.h"
#include "sensor_profile_setting.h"
#include "mount_orientation_setting.h"
#include "level_indicator.h"
#include "power_status.h"
#include "launch_feedback.h"
#include "settings_save.h"
#include <cassert>
#include <iostream>
#define LOOP_HELPER
using Action=StickS3Acquisition::Action;using Snapshot=StickS3Acquisition::Snapshot;using D=AnalogTachometer;
static StickS3Acquisition acquisition;static std::array<uint16_t,4096> storage{};
// Fault injection for the owner's reply path: a reply that never reaches the
// UI (its request times out after 2 s) or a command that is never executed.
static bool dropReplies=false,dropCommands=false;
static void pump() {
  StickS3Acquisition::Command c;
  while(xQueueReceive(acquisition.commands_,&c,0)==pdTRUE) {
    if(dropCommands)continue;
    acquisition.command(c);
    if(dropReplies){StickS3Acquisition::Reply r;xQueueReceive(acquisition.replies_,&r,0);}
  }
}
struct Imu {
  bool veto=false,active=false,failMount=false;unsigned windows=0,vetoes=0,mounts=0;
  MountOrientation::Mode mode=MountOrientation::Mode::Top;
  bool quiet(){if(veto){++vetoes;return false;}++windows;active=true;return true;}
  void endQuiet(){active=false;}
  bool setMount(MountOrientation::Mode m){if(failMount)return false;mode=m;++mounts;return true;}
}imuAcquisition;
static unsigned guardedWrites=0;static bool directWrites=false;
static void writeGuard() {
  assert(directWrites || (acquisition.checkpoint_ && acquisition.suspended_ && !acquisition.ready_ && imuAcquisition.active));
  ++guardedWrites;
}
static uint32_t millis(){return uint32_t(clockUs/1000);}
struct SerialFake {template<class... T>void printf(const char*,T...){}void println(const char*){}}Serial;
// USB output is drained (never Serial.flush(), which discards it): usb_tx.h.
namespace UsbTx {inline unsigned drains=0;inline bool drain(uint32_t){++drains;return true;}}

struct Prefs:Preferences {
  size_t putUChar(const char *key,uint8_t value){return putBytes(key,&value,1);}
  size_t putBool(const char *key,bool value){const uint8_t byte=value;return putBytes(key,&byte,1);}
}prefs;
static uint8_t stored(const char *key){uint8_t value=0;assert(prefs.getBytes(key,&value,1)==1);return value;}
struct {struct {unsigned brightness=0,rotation=0;void setBrightness(unsigned v){brightness=v;}void setRotation(unsigned v){rotation=v;}}Display;}M5;
static Controls::State controls;static SettingsSave settingsSave;
static bool motionPending=false,settingsSaveError=false,restorePreview=false,displayFlipped=false,dirty=false,extPower=false;
static bool saveError=false,wipeCheckpointHeld=false,orphanSeen=false;
static uint8_t brightnessPercent=40;static uint32_t orphanSince=0;
static constexpr uint32_t ORPHAN_CHECKPOINT_MS=1000,HISTORY_COALESCE_MS=10000,HISTORY_SAVE_RETRY_MS=5000;
static uint32_t lastSaveTry=0,saveCount=0,saveErrors=0,maxSaveUs=0,lastAcceptedAt=0,historyPendingSince=0;
static HistoryCheckpoint::Stillness imuStill{true,0}; // launcher set down
static InactivityTimer inactivity;static LaunchFeedback launchFeedback;
static PracticeHistory practice;static PracticeStore practiceStore;
static Appearance::Config appearance;static Appearance::Bests personalBests;
static AppearanceStore<Appearance::Config> appearanceStore;static AppearanceStore<Appearance::Bests> bestStore;
static RpmEstimator::Mode rpmEstimator=RpmEstimator::DEFAULT_MODE;
static StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
static MountOrientation::Mode mountMode=MountOrientation::Mode::Top,liveMount=MountOrientation::Mode::Top;
static StickS3Level level;static StickS3Level::Reading drawnLevel;static uint64_t lastLevelSampleUs=0;
static uint8_t backlightValue(){return (unsigned(brightnessPercent)*255+50)/100;}
static void syncNavigation(){dirty=true;}
static void prepareMotion(const Snapshot&){lastAcceptedAt=millis();}static void logLaunchMetrics(const Snapshot&){}
'''

tests = r'''
static Snapshot snap(){acquisition.publish(true);return acquisition.snapshot();}
// Optical samples through the owner's real process(); the clock only runs
// while the ADC is stopped.
static void optical(unsigned us,int value=1000) {
  if(!acquisition.ready_){clockUs+=us;acquisition.publish(true);return;}
  std::array<adc_digi_output_data_t,256> batch{};
  for(auto &sample:batch)sample.type2={uint16_t(value),acquisition.channel_,0};
  unsigned remaining=us/20;
  while(remaining) {
    const auto n=std::min(remaining,unsigned(batch.size()));clockUs+=n*20;acquisition.lastRead_=clockUs;
    acquisition.process(reinterpret_cast<const uint8_t*>(batch.data()),n*SOC_ADC_DIGI_RESULT_BYTES);remaining-=n;
  }
  acquisition.publish(true);
}
// One UI loop pass in loop()'s order for the save paths, then 5 ms of signal.
static void pass(int value=1000) {
  Snapshot event;
  while(acquisition.takeEvent(event))
    if(practice.accept(event.launches,event.valid,event.resultRpm,millis())){lastAcceptedAt=millis();if(!historyPendingSince)historyPendingSince=lastAcceptedAt;}
  const uint32_t now=millis();Snapshot s=acquisition.snapshot();
  saveHistory(now,s);s=acquisition.snapshot();
  saveSettings(now,s);s=acquisition.snapshot();
  releaseOrphanCheckpoint(now,s);
  optical(5000,value);
}
static void passes(unsigned ms,int value=1000){for(unsigned t=0;t<ms;t+=5)pass(value);}
static void pull(){for(unsigned turn=0;turn<12;++turn){optical(5000,1100);optical(5000);}}
static void boot() {
  if(acquisition.adc_)acquisition.closeAdc();
  if(acquisition.commands_){vQueueDelete(acquisition.commands_);vQueueDelete(acquisition.replies_);vQueueDelete(acquisition.events_);}
  acquisition.~StickS3Acquisition();new(&acquisition) StickS3Acquisition();
  acquisition.storage_=storage.data();acquisition.ring_=SampleRing(storage.data(),storage.size());
  acquisition.commands_=xQueueCreate(4,sizeof(StickS3Acquisition::Command));
  acquisition.replies_=xQueueCreate(4,sizeof(StickS3Acquisition::Reply));
  acquisition.events_=xQueueCreate(16,sizeof(Snapshot));acquisition.task_=reinterpret_cast<void*>(1);
  StickS3SensorProfile::configure(acquisition.detector_,StickS3SensorProfile::Mode::Standard);
  assert(acquisition.initAdc());ownerPump=pump;acquisition.setRequestHook(quietImuForFlash);
  dropReplies=dropCommands=false;imuAcquisition={};controls={};settingsSave={};
  motionPending=settingsSaveError=restorePreview=displayFlipped=dirty=extPower=saveError=wipeCheckpointHeld=orphanSeen=false;
  brightnessPercent=40;orphanSince=lastSaveTry=saveCount=saveErrors=maxSaveUs=lastAcceptedAt=historyPendingSince=0;
  inactivity={};launchFeedback={};practice={};appearance={};personalBests={};
  rpmEstimator=RpmEstimator::DEFAULT_MODE;sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
  mountMode=liveMount=MountOrientation::Mode::Top;level={};drawnLevel={};lastLevelSampleUs=0;
  Preferences::data.clear();Preferences::zeroLengthKeys.clear();Preferences::failWrite=Preferences::shortRead=false;
  Preferences::beforeWrite=writeGuard;guardedWrites=0;directWrites=false;
  practiceStore=PracticeStore{};appearanceStore={};bestStore={};
  assert(prefs.begin("launchlab-mini",false) && practiceStore.begin(practice));
  assert(appearanceStore.begin(appearance,"ll-appearance","cfg0","cfg1") && bestStore.begin(personalBests,"ll-bests","best0","best1"));
  optical(1200000);assert(snap().phase==D::Phase::Ready && snap().opticalIdle);
}
static void apply(unsigned row,uint8_t value){controls.page=Controls::Page::Edit;controls.setting=row;controls.draft=value;commitSetting();}
static bool running(){const auto s=snap();return s.ready && !s.suspended && !acquisition.checkpoint_ && acquisition.adc_->active;}

static void freshEdgeAtApply() {
  boot();
  // Five presses, each jiggling the launcher across a mark just before Apply.
  for(uint8_t i=0;i<5;++i) {
    optical(2000,1100);assert(!snap().opticalIdle);
    apply(2,uint8_t(50+10*i));
    assert(controls.page==Controls::Page::Settings && !settingsSaveError && brightnessPercent==50+10*i);
    assert(settingsSave.pending()==SettingsSave::Brightness && imuAcquisition.windows==i && running());
    // The record follows within a second of ordinary loop passes; no second press.
    passes(1000);assert(!settingsSave.pending() && stored("bright_pct")==50+10*i && imuAcquisition.windows==i+1u && running());
  }
  std::cout<<"PASS: Apply right after a fresh optical edge (the press jiggling the launcher) is taken at once and saved on the next quiet pass\n";
}

static void imuVeto() {
  boot();imuAcquisition.veto=true;apply(0,7);
  assert(controls.page==Controls::Page::Settings && inactivity.minutes()==7 && settingsSave.pending()==SettingsSave::Sleep && running());
  // Apply's own attempt, then one per second over 600 UI passes, not one per pass.
  passes(3000);assert(imuAcquisition.vetoes==3 && settingsSave.refusals()==3 && settingsSave.pending() && !guardedWrites);
  imuAcquisition.veto=false;passes(1000);assert(!settingsSave.pending() && stored("sleep_min")==7 && running());
  std::cout<<"PASS: a vetoed IMU barrier leaves the setting pending with one retry per second, never a write without the quiet window\n";
}

static void applyDuringPull() {
  boot();optical(5000,1100);optical(5000);optical(5000,1100);optical(5000);
  for(unsigned turn=0;turn<4;++turn){optical(5000,1100);optical(5000);}
  assert(snap().phase==D::Phase::Launch);
  apply(1,uint8_t(RpmEstimator::Mode::SingleTurn));
  assert(rpmEstimator==RpmEstimator::Mode::SingleTurn && snap().estimator==RpmEstimator::Mode::ThreeTurn && !imuAcquisition.windows);
  for(unsigned turn=0;turn<6;++turn){pass(1100);pass(1000);}
  assert(snap().phase==D::Phase::Launch && !guardedWrites && !imuAcquisition.windows);
  passes(1700);
  // The pull is measured intact with the metric it started under; the
  // switch and its new session follow in the first quiet checkpoint.
  assert(practice.size()==1 && std::fabs(practice.recent()->rpm-6000)<3 && !settingsSave.pending());
  assert(snap().estimator==RpmEstimator::Mode::SingleTurn && running());
  RpmEstimator::Mode saved;assert(RpmEstimatorSetting::load(prefs,saved) && saved==RpmEstimator::Mode::SingleTurn);
  std::cout<<"PASS: Apply during a pull never interrupts it; the metric switches and saves in the first checkpoint after\n";
}

static void lostReplies() {
  // A Begin the owner executed but whose reply was lost: the UI saw a refusal
  // and the ADC stayed stopped. It is released within a second and the next
  // pull is measured.
  boot();dropReplies=true;apply(3,1);dropReplies=false;
  // The immediate attempt timed out after 2 s; the owner holds the checkpoint.
  assert(acquisition.checkpoint_ && snap().suspended && !snap().resting && !imuAcquisition.active && settingsSave.pending());
  passes(1100);assert(running() && !acquisition.checkpoint_);
  passes(1100);bool flipped=false;assert(!settingsSave.pending() && DisplayFlipSetting::load(prefs,flipped) && flipped);
  pull();passes(1700);assert(practice.size()==1 && std::fabs(practice.recent()->rpm-6000)<3);
  // An End that never reached the owner leaves it stopped the same way.
  boot();const auto s=snap();assert(acquisition.request(Action::IdleCheckpointBegin,s.launches));
  dropCommands=true;assert(!acquisition.request(Action::CheckpointEnd));dropCommands=false;
  assert(snap().suspended && !imuAcquisition.active);
  passes(1100);assert(running());pull();passes(1700);assert(practice.size()==1);
  // A resting owner is not an orphan and stays paused.
  boot();assert(acquisition.request(Action::IdlePause,0) && snap().resting);passes(3000);assert(snap().resting);
  std::cout<<"PASS: a checkpoint left held by a lost Begin reply or a lost End is released within a second and the next pull is measured; resting is untouched\n";
}

static void usbDeviceSettings() {
  boot();const long pin=1;bool changed=true;bool nextExtPower=true;
  directWrites=false;
  '''+statement('if(acquisition.request(Action::Pin,uint8_t(pin)))')+r'''
  '''+block('if(changed){extPower=nextExtPower;')+r'''
  assert(!guardedWrites && settingsSave.pending()==(SettingsSave::Pin|SettingsSave::ExtPower));
  optical(1200000);pass();
  assert(!settingsSave.pending() && stored("ao_pin")==1 && stored("ext5v")==1 && guardedWrites==2 && running());
  std::cout<<"PASS: USB GPIO and 5 V choices are written inside a checkpoint, never beside a running ADC\n";
}

static void shutdownFlush() {
  // Pending settings are written in the shutdown checkpoint, and the live
  // metric and IMU frame switch there too: a power-off cancelled afterwards
  // (USB attached, sleep refused) resumes with live state matching the records.
  boot();imuAcquisition.veto=true;apply(2,70);apply(8,uint8_t(MountOrientation::Mode::Sidecar));
  apply(1,uint8_t(RpmEstimator::Mode::SingleTurn));
  assert(settingsSave.pending()==(SettingsSave::Brightness|SettingsSave::Mount|SettingsSave::Rpm));imuAcquisition.veto=false;
  assert(flushForShutdown(millis(),snap())==ShutdownFlush::Ready);
  assert(!settingsSave.pending() && stored("bright_pct")==70 && liveMount==MountOrientation::Mode::Sidecar && imuAcquisition.mode==liveMount);
  MountOrientation::Mode mount;assert(MountOrientationSetting::load(prefs,mount) && mount==MountOrientation::Mode::Sidecar);
  RpmEstimator::Mode metric;assert(RpmEstimatorSetting::load(prefs,metric) && metric==RpmEstimator::Mode::SingleTurn);
  assert(acquisition.request(Action::CheckpointEnd) && running() && snap().estimator==RpmEstimator::Mode::SingleTurn);
  boot();imuAcquisition.veto=true;apply(2,70);imuAcquisition.veto=false;Preferences::failWrite=true;
  assert(flushForShutdown(millis(),snap())==ShutdownFlush::Ready && settingsSave.failed());
  Preferences::failWrite=false;assert(acquisition.request(Action::CheckpointEnd));
  passes(SettingsSave::FAILED_RETRY_MS+10);assert(!settingsSave.pending() && stored("bright_pct")==70);
  std::cout<<"PASS: shutdown writes pending settings in its own checkpoint; a settings write failure never blocks power-off\n";
}

static void sensorFaultHistory() {
  // After a pull, the sensor signal drops to the low rail (CHECK SENSOR). The
  // detector holds in Hold indefinitely; history still flushes once quiet.
  boot();pull();passes(1700);assert(practice.size()==1 && practiceStore.pending(practice));
  passes(2000,0);assert(snap().signalFault && snap().phase==D::Phase::Hold);
  passes(HISTORY_COALESCE_MS+1000,0);
  assert(!practiceStore.pending(practice) && saveCount==1 && snap().signalFault);
  // Restored signal resumes measurement.
  passes(1200);pull();passes(1700);assert(practice.size()==2);
  std::cout<<"PASS: history pending during a sensor fault is flushed through an idle checkpoint\n";
}
'''

main = r'''
int main() {
  freshEdgeAtApply();imuVeto();applyDuringPull();lostReplies();usbDeviceSettings();shutdownFlush();sensorFaultHistory();
}
'''

functions = '\n'.join([
    function('quietImuForFlash', 'bool'), function('previewSetting'), function('writeSettings'),
    function('saveSettings'), function('commitSetting'), function('saveHistory'),
    function('releaseOrphanCheckpoint'), shutdown_flush(),
])
with tempfile.TemporaryDirectory(prefix='launchlab-save-paths-') as temp:
    build = pathlib.Path(temp)
    cpp = build / 'save_paths.cpp'
    exe = build / 'save_paths'
    cpp.write_text(prefix + functions + tests + main)
    here = pathlib.Path(__file__).resolve().parent
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I' + str(here / 'fakes/acquisition_host'), '-I' + str(here / 'fakes'),
                    '-I' + str(args.firmware / 'LaunchLabMini'), '-I' + str(args.firmware / 'LaunchLabRpm'),
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=120)
