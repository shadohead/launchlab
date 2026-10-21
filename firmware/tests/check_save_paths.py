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
#include "theme_library.h"
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
#include "theme_store.h"
#include "device_backup.h"
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
static Appearance::Config appearance;static Appearance::Bests personalBests;static Themes::Library themeLibrary;
static AppearanceStore<Appearance::Config> appearanceStore;static AppearanceStore<Appearance::Bests> bestStore;
static RpmEstimator::Mode rpmEstimator=RpmEstimator::DEFAULT_MODE;
static StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
static MountOrientation::Mode mountMode=MountOrientation::Mode::Top,liveMount=MountOrientation::Mode::Top;
static StickS3Level level;static StickS3Level::Reading drawnLevel;static uint64_t lastLevelSampleUs=0;
static uint8_t backlightValue(){return (unsigned(brightnessPercent)*255+50)/100;}
static void syncNavigation(){dirty=true;}
// Custom theme requests (USB T): the real receiver and store; free NVS entries are set by the test.
static ThemeStore themeStore;static DeviceBackup::LineReceiver<(Themes::Request::BYTES+2)/3*4> themeRx;
// Milestone requests (USB U): the real receiver, record and store.
static Milestones::Config milestones;static AppearanceStore<Milestones::Config> milestoneStore;
static DeviceBackup::LineReceiver<(Milestones::Request::BYTES+2)/3*4> milestoneRx;
// Effect set requests (USB U op 3): the same receiver, the real record and store.
static EffectSet::Config effectSet;static AppearanceStore<EffectSet::Config> effectSetStore;
static Appearance::Effect upcomingEffect=Appearance::Effect::Off;
static unsigned freeEntries=158;static bool nvsAvailable(unsigned &available){available=freeEntries;return true;}
static void cancelSettingEdit(){if(controls.page==Controls::Page::Edit)controls.page=Controls::Page::Settings;}
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
  boot();imuAcquisition.veto=true;apply(2,70);apply(unsigned(Controls::Setting::Mount),uint8_t(MountOrientation::Mode::Sidecar));
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

// One framed request through the receiver, as the USB loop feeds it.
static void themeRequest(Themes::Request::Op op,uint8_t argument,const Palette::Custom *theme=nullptr) {
  Themes::Request q;q.op=op;q.argument=argument;if(theme)q.theme=*theme;
  uint8_t bytes[Themes::Request::BYTES];q.encode(bytes);std::string line;
  DeviceBackup::base64Lines(bytes,sizeof(bytes),96,[&](const char *text){line+=text;});
  themeRx.start(millis());auto event=DeviceBackup::TransferEvent::None;
  for(char c:line+"\n.\n")event=themeRx.feed(c,millis());
  assert(event==DeviceBackup::TransferEvent::Complete);
  finishThemeRequest(snap());
}
// A copy of `theme` renamed; the name stays NUL padded, as a valid slot requires.
static Palette::Custom renamed(Palette::Custom theme,const char *name) {
  std::memset(theme.name,0,sizeof(theme.name));std::strcpy(theme.name,name);return theme;
}
static void themeRequests() {
  using Op=Themes::Request::Op;
  boot();themeStore=ThemeStore{};themeLibrary={};freeEntries=158;assert(themeStore.begin(themeLibrary)==ThemeStore::Status::Empty);
  Palette::Custom navy;std::strcpy(navy.name,"HIL Navy");
  const uint16_t roles[]={0x0006,0xffff,0x5aeb,0x2d7f,0xfea0,0xfda0,0x07e0,0xf800};std::memcpy(navy.role,roles,sizeof(roles));
  // An upload is written in its own checkpoint with the IMU quiet, then the ADC runs again.
  themeRequest(Op::Put,1,&navy);
  assert(themeLibrary.state(1)==Themes::SlotState::Valid && guardedWrites==1 && running() && controls.customThemes==0b10);
  {Themes::Library stored;ThemeStore reader;assert(reader.begin(stored)==ThemeStore::Status::Loaded && stored==themeLibrary);}
  // Busy (a pull, a vetoed IMU barrier) or short of NVS: refused, nothing written.
  // "Other" is a valid theme, so each refusal below is for its stated reason.
  const auto other=renamed(navy,"Other");assert(other.valid());
  optical(5000,1100);optical(5000);optical(5000,1100);optical(5000);
  for(unsigned turn=0;turn<4;++turn){optical(5000,1100);optical(5000);}
  assert(snap().phase==D::Phase::Launch);themeRequest(Op::Put,2,&other);
  assert(themeLibrary.state(2)==Themes::SlotState::Empty && guardedWrites==1);
  passes(1700);assert(practice.size()==1 && running());
  imuAcquisition.veto=true;themeRequest(Op::Put,2,&other);imuAcquisition.veto=false;
  assert(themeLibrary.state(2)==Themes::SlotState::Empty && guardedWrites==1 && running());
  freeEntries=ThemeStore::NEEDED_ENTRIES-1;themeRequest(Op::Put,2,&other);freeEntries=158;
  assert(themeLibrary.state(2)==Themes::SlotState::Empty && guardedWrites==1);
  // A malformed name is refused before any checkpoint.
  const auto unnamed=renamed(navy," Lead");const unsigned windows=imuAcquisition.windows;
  themeRequest(Op::Put,2,&unnamed);assert(themeLibrary.state(2)==Themes::SlotState::Empty && imuAcquisition.windows==windows && guardedWrites==1);
  // Colors are not: nearly invisible text on a white background is written like any theme.
  auto light=renamed(navy,"Light");light.role[Palette::Background]=0xffff;light.role[Palette::Text]=0xef7d;
  themeRequest(Op::Put,2,&light);assert(themeLibrary.state(2)==Themes::SlotState::Valid && guardedWrites>1 && !directWrites && running());
  {Themes::Library stored;ThemeStore reader;assert(reader.begin(stored)==ThemeStore::Status::Loaded && stored.state(2)==Themes::SlotState::Valid);}
  themeRequest(Op::Delete,2);assert(themeLibrary.state(2)==Themes::SlotState::Empty && running());
  // Select saves the appearance record like Apply; Settings Apply refuses a missing theme.
  themeRequest(Op::Select,Appearance::THEMES+1);passes(100);
  assert(appearance.theme==Themes::slotTheme(1) && !settingsSave.pending() && running());
  {Appearance::Config saved;AppearanceStore<Appearance::Config> reader;assert(reader.begin(saved,"ll-appearance","cfg0","cfg1") && saved.theme==Themes::slotTheme(1));}
  apply(5,uint8_t(Appearance::THEMES+3));assert(settingsSaveError && appearance.theme==Themes::slotTheme(1));
  controls.page=Controls::Page::Main;
  // Deleting the selected theme returns to the default, saved too.
  themeRequest(Op::Delete,1);passes(100);
  assert(themeLibrary.empty() && appearance.theme==Themes::FALLBACK && !settingsSave.pending() && running());
  {Appearance::Config saved;AppearanceStore<Appearance::Config> reader;assert(reader.begin(saved,"ll-appearance","cfg0","cfg1") && saved.theme==Themes::FALLBACK);}
  // An unreadable record is kept until Clear, which may replace it.
  themeRequest(Op::Put,0,&navy);Preferences::data["ll-appearancethemes"][9]^=1;
  themeStore=ThemeStore{};assert(themeStore.begin(themeLibrary)==ThemeStore::Status::Unreadable && themeLibrary.empty());
  const auto retained=Preferences::data["ll-appearancethemes"];
  themeRequest(Op::Put,0,&navy);assert(themeLibrary.empty() && Preferences::data["ll-appearancethemes"]==retained);
  themeRequest(Op::Clear,0);assert(!Preferences::data.count("ll-appearancethemes") && themeStore.status()==ThemeStore::Status::Empty);
  themeRequest(Op::Put,0,&navy);assert(themeLibrary.state(0)==Themes::SlotState::Valid && running());
  std::cout<<"PASS: theme uploads, deletes and clears write inside a checkpoint only; busy, short of NVS, misnamed or unreadable requests write nothing, any colors are written; select and fallback save like Apply\n";
}

// One framed milestone request through the receiver, as the USB loop feeds it.
static void milestoneBytes(const uint8_t *bytes) {
  std::string line;DeviceBackup::base64Lines(bytes,Milestones::Request::BYTES,96,[&](const char *text){line+=text;});
  milestoneRx.start(millis());auto event=DeviceBackup::TransferEvent::None;
  for(char c:line+"\n.\n")event=milestoneRx.feed(c,millis());
  assert(event==DeviceBackup::TransferEvent::Complete);
  finishMilestoneRequest(snap());
}
static void milestoneRequest(Milestones::Request::Op op,const Milestones::Config *table=nullptr) {
  uint8_t bytes[Milestones::Request::BYTES];Milestones::Request::encode(op,table,bytes);milestoneBytes(bytes);
}
static bool storedTable(Milestones::Config &out){AppearanceStore<Milestones::Config> reader;return reader.begin(out,"ll-appearance","mpool0","mpool1",true);}
static void milestoneRequests() {
  using Op=Milestones::Request::Op;using Milestones::Row;using Appearance::Effect;
  boot();milestones={};milestoneStore={};freeEntries=158;assert(milestoneStore.begin(milestones,"ll-appearance","mpool0","mpool1",true));
  Milestones::Config table;const Row rows[]={{8000,Effect::Orbit},{10500,Effect::Lightning},{13000,Effect::Slash}};
  assert(table.set(Milestones::Mode::Milestones,rows,3)==Milestones::Problem::None);
  // A table is written in its own checkpoint with the IMU quiet, then the ADC runs again.
  milestoneRequest(Op::Set,&table);
  assert(milestones.same(table) && guardedWrites==1 && running());
  {Milestones::Config stored;assert(storedTable(stored) && stored.same(table) && stored.revision==milestones.revision);}
  // The same table again writes nothing.
  milestoneRequest(Op::Set,&table);assert(guardedWrites==1);
  // Busy (a pull, a vetoed IMU barrier) or short of NVS: refused, nothing written.
  Milestones::Config other;assert(other.select(Milestones::Mode::Both));
  for(unsigned turn=0;turn<6;++turn){optical(5000,1100);optical(5000);}
  assert(snap().phase==D::Phase::Launch);milestoneRequest(Op::Set,&other);
  assert(milestones.same(table) && guardedWrites==1);
  passes(1700);assert(running());
  imuAcquisition.veto=true;milestoneRequest(Op::Set,&other);imuAcquisition.veto=false;
  assert(milestones.same(table) && guardedWrites==1 && running());
  freeEntries=MILESTONE_NEEDED_ENTRIES-1;milestoneRequest(Op::Set,&other);freeEntries=158;
  assert(milestones.same(table) && guardedWrites==1);
  // Invalid tables are refused before any checkpoint, whatever the checksum.
  const unsigned windows=imuAcquisition.windows;
  auto invalid=[&](void (*damage)(uint8_t *)) {
    uint8_t bytes[Milestones::Request::BYTES];Milestones::Request::encode(Op::Set,&other,bytes);damage(bytes);
    Appearance::put32(bytes+Milestones::Request::BYTES-4,Appearance::hash(bytes,Milestones::Request::BYTES-4));milestoneBytes(bytes);
    assert(milestones.same(table) && guardedWrites==1 && imuAcquisition.windows==windows);
  };
  invalid([](uint8_t *b){b[8+4+10]=b[8+4];b[8+4+11]=b[8+4+1];});    // two rows at the same RPM
  invalid([](uint8_t *b){b[8+4]=0xe7;b[8+5]=0x03;});                 // 999 RPM
  invalid([](uint8_t *b){b[8+4+2]|=1u<<unsigned(Effect::Random);});  // not a drawn effect in the pool
  invalid([](uint8_t *b){std::memset(b+8+4+2,0,8);});                // an empty pool
  invalid([](uint8_t *b){b[8]=3;});                                  // no such mode
  invalid([](uint8_t *b){b[8+1]=0;});                                // no rows
  {uint8_t bytes[Milestones::Request::BYTES];Milestones::Request::encode(Op::Set,&other,bytes);bytes[20]^=1;milestoneBytes(bytes);
   assert(milestones.same(table) && guardedWrites==1 && imuAcquisition.windows==windows);}
  // Reset returns to the defaults, saved.
  milestoneRequest(Op::Reset);passes(100);
  assert(milestones.isDefault() && guardedWrites==2 && running());
  {Milestones::Config stored;assert(storedTable(stored) && stored.isDefault());}
  // A damaged record shows the defaults; a request replaces it, even with the defaults.
  Preferences::data["ll-appearancempool0"][9]^=1;Preferences::data["ll-appearancempool1"][9]^=1;
  milestones={};milestoneStore={};assert(!milestoneStore.begin(milestones,"ll-appearance","mpool0","mpool1",true) && milestoneStore.damaged());
  assert(milestones.isDefault() && !std::strcmp(milestoneStoreName(),"damaged"));
  milestoneRequest(Op::Reset);
  assert(!milestoneStore.damaged() && guardedWrites==3 && !std::strcmp(milestoneStoreName(),"ok"));
  {Milestones::Config stored;AppearanceStore<Milestones::Config> reader;assert(reader.begin(stored,"ll-appearance","mpool0","mpool1",true) && !reader.damaged() && stored.isDefault());}
  std::cout<<"PASS: milestone tables and resets write inside a checkpoint only; busy, short of NVS, corrupt or invalid requests write nothing; a damaged record is replaced\n";
}

// A completed U transfer as pollMilestoneRequest dispatches it.
static void uTransfer(const uint8_t *bytes,size_t n=EffectSet::Request::BYTES) {
  std::string line;DeviceBackup::base64Lines(bytes,n,96,[&](const char *text){line+=text;});
  milestoneRx.start(millis());auto event=DeviceBackup::TransferEvent::None;
  for(char c:line+"\n.\n")event=milestoneRx.feed(c,millis());
  assert(event==DeviceBackup::TransferEvent::Complete);
  const auto s=snap();if(!finishEffectSetRequest(s))finishMilestoneRequest(s);
}
static void effectSetRequest(EffectSet::Mask mask){uint8_t bytes[EffectSet::Request::BYTES];EffectSet::Request::encode(mask,bytes);uTransfer(bytes);}
static bool storedSet(EffectSet::Config &out){AppearanceStore<EffectSet::Config> reader;return reader.begin(out,"ll-appearance","eset0","eset1",true) && !reader.damaged();}
static void effectSetRequests() {
  using Appearance::Effect;using EffectSet::flag;
  boot();effectSet={};effectSetStore={};milestones={};milestoneStore={};freeEntries=158;upcomingEffect=Effect::Orbit;
  assert(effectSetStore.begin(effectSet,"ll-appearance","eset0","eset1",true) && milestoneStore.begin(milestones,"ll-appearance","mpool0","mpool1",true));
  assert(!std::strcmp(effectSetFields()," effect_set=0000000000000000000000000003ff9f effect_set_store=ok effect_set_default=1"));
  const EffectSet::Mask mine=flag(Effect::Slash)|flag(Effect::Crimson)|flag(Effect::Ravage);
  // A Best effect editor open on the device closes: it cycles the old set.
  controls.page=Controls::Page::Edit;controls.setting=unsigned(Controls::Setting::BestEffect);
  // A set is written in its own checkpoint with the IMU quiet, then the ADC runs again.
  effectSetRequest(mine);
  assert(effectSet.mask()==mine && guardedWrites==1 && running() && controls.offeredEffects==mine && controls.page==Controls::Page::Settings);
  // Random's next pick, Orbit, is not offered any more: the next one is drawn from the set.
  assert(upcomingEffect==Effect::Off);
  {EffectSet::Config stored;assert(storedSet(stored) && stored.mask()==mine && stored.revision==effectSet.revision);}
  assert(!std::strcmp(effectSetFields()," effect_set=00000000000000000000040000040080 effect_set_store=ok effect_set_default=0"));
  // The same set again writes nothing.
  effectSetRequest(mine);assert(guardedWrites==1);
  // Busy (a pull, a vetoed IMU barrier) or short of NVS: refused, nothing written.
  for(unsigned turn=0;turn<6;++turn){optical(5000,1100);optical(5000);}
  assert(snap().phase==D::Phase::Launch);effectSetRequest(EffectSet::ALL);
  assert(effectSet.mask()==mine && guardedWrites==1);
  passes(1700);assert(running());
  imuAcquisition.veto=true;effectSetRequest(EffectSet::ALL);imuAcquisition.veto=false;
  assert(effectSet.mask()==mine && guardedWrites==1 && running());
  freeEntries=EFFECT_SET_NEEDED_ENTRIES-1;effectSetRequest(EffectSet::ALL);freeEntries=158;
  assert(effectSet.mask()==mine && guardedWrites==1);
  // Invalid sets and damaged requests are refused before any checkpoint.
  const unsigned windows=imuAcquisition.windows;
  for(EffectSet::Mask bad:{EffectSet::Mask(),EffectSet::DEFAULT_MASK|flag(Effect::Off),flag(Effect::Random),flag(43),flag(63),flag(127)}) {
    effectSetRequest(bad);assert(effectSet.mask()==mine && guardedWrites==1 && imuAcquisition.windows==windows);
  }
  {uint8_t bytes[EffectSet::Request::BYTES];EffectSet::Request::encode(EffectSet::ALL,bytes);bytes[9]^=1;uTransfer(bytes);
   EffectSet::Request::encode(EffectSet::ALL,bytes);bytes[30]=1;Appearance::put32(bytes+36,Appearance::hash(bytes,36));uTransfer(bytes);
   assert(effectSet.mask()==mine && guardedWrites==1 && imuAcquisition.windows==windows);}
  // Milestone requests on the same transfer still reach the milestone code;
  // neither record touches the other.
  // Both milestone frames: 0.15's v1 and the pooled v2.
  {Milestones::Config table;assert(table.select(Milestones::Mode::Both));uint8_t bytes[Milestones::Request::BYTES];
   Milestones::Request::encode(Milestones::Request::Op::Set,&table,bytes,true);uTransfer(bytes,Milestones::Request::V1_BYTES);
   assert(milestones.mode()==Milestones::Mode::Both && effectSet.mask()==mine && guardedWrites==2);
   const Milestones::Row rows[]={{9000,(flag(Effect::Crimson)|flag(Effect::Tidal)).lo}};
   assert(table.set(Milestones::Mode::Both,rows,1)==Milestones::Problem::None);
   Milestones::Request::encode(Milestones::Request::Op::Set,&table,bytes);uTransfer(bytes,Milestones::Request::BYTES);
   assert(milestones.same(table) && effectSet.mask()==mine && guardedWrites==3);}
  // The defaults back, saved.
  effectSetRequest(EffectSet::DEFAULT_MASK);assert(effectSet.isDefault() && guardedWrites==4 && controls.offeredEffects==EffectSet::DEFAULT_MASK);
  {EffectSet::Config stored;assert(storedSet(stored) && stored.isDefault());}
  // A damaged record offers the default set; a request replaces it, even with the default.
  Preferences::data["ll-appearanceeset0"][9]^=1;Preferences::data["ll-appearanceeset1"][9]^=1;
  effectSet={};effectSetStore={};assert(!effectSetStore.begin(effectSet,"ll-appearance","eset0","eset1",true) && effectSetStore.damaged());
  assert(effectSet.isDefault() && !std::strcmp(effectSetFields()," effect_set=0000000000000000000000000003ff9f effect_set_store=damaged effect_set_default=1"));
  effectSetRequest(EffectSet::DEFAULT_MASK);
  assert(!effectSetStore.damaged() && guardedWrites==5 && !std::strcmp(effectSetStoreName(),"ok"));
  {EffectSet::Config stored;assert(storedSet(stored) && stored.isDefault());}
  {Milestones::Config kept;assert(storedTable(kept) && kept.mode()==Milestones::Mode::Both);}
  std::cout<<"PASS: effect set requests write inside a checkpoint only; busy, short of NVS, corrupt or invalid requests write nothing; milestone requests unaffected; a damaged record is replaced\n";
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
  freshEdgeAtApply();imuVeto();applyDuringPull();lostReplies();usbDeviceSettings();shutdownFlush();themeRequests();milestoneRequests();effectSetRequests();sensorFaultHistory();
}
'''

functions = '\n'.join([
    function('quietImuForFlash', 'bool'), function('previewSetting'), function('writeSettings'),
    function('saveSettings'), function('commitSetting'), function('saveHistory'),
    function('releaseOrphanCheckpoint'), shutdown_flush(),
    function('themeReply'), function('themesChanged'), function('selectTheme'), function('finishThemeRequest'),
    function('milestoneReply'), function('*milestoneStoreName', 'const char'), statement('static constexpr unsigned MILESTONE_NEEDED_ENTRIES'),
    function('finishMilestoneRequest'),
    function('*effectSetStoreName', 'const char'), statement('static constexpr unsigned EFFECT_SET_NEEDED_ENTRIES'),
    function('*effectSetFields', 'const char'), function('effectSetChanged'), function('finishEffectSetRequest', 'bool'),
])
# pollMilestoneRequest hands a completed transfer to the effect set first, as uTransfer() does.
assert 'case DeviceBackup::TransferEvent::Complete:if(!finishEffectSetRequest(s))finishMilestoneRequest(s);break;' in source
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
