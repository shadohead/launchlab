#!/usr/bin/env python3
"""Run the actual wipe UI/loop/USB flow with physical-button and storage fakes.

The wipe transaction and pinned M5 button implementation are real. Storage,
ADC acknowledgement, reboot and ordinary writers are observable host fakes.
No device, user history, NVS partition or release artifact is accessed.
"""
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "LaunchLabMini/LaunchLabMini.ino"
BUTTON_COMMIT = "8b63555e7bd9ac71978c55ad868fd1d1d3b2ef3e"
BUTTON_FILES = {
    "Button_Class.hpp": "9bb5b2b35c74510cbbadd943b337deb814b81f894cfb3cc039bdf5ef14493a2d",
    "Button_Class.inl": "013f532e72067dc9350785c59110bb4f9cf635363434d58c71d075406d72b335",
    "LICENSE": "44611d917bece311f5c8eeffce83d8bf9e9ca3f815d262f694ff4a7996af50ea",
}


def function(source, name, result="void", static=True):
    signature = ("static " if static else "") + result + " " + name + "("
    start = source.index(signature)
    while source.index(";", start) < source.index("{", start):
        start = source.index(signature, start + 1)
    opening = source.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREFIX = r'''
#include "controls.h"
#include "usb_command_guard.h"
#include "accel_range.h"
#include "wipe_confirmation.h"
#include "device_wipe.h"
#include "analog_tachometer.h"
#include "display_orientation.h"
#include "level_indicator.h"
#include "power_status.h"
#include "cpu_power.h"
#include "usb_awake.h"
#include "rpm_estimator.h"
#include "brightness.h"
#include "sensor_profile_setting.h"
#include "mount_orientation.h"
#include "device_backup.h"
#include "theme_library.h"
#include "launch_motion.h"
#include "Button_Class.hpp"
#define M5UNIFIED_IMPLEMENTATION
#include "Button_Class.inl"
#include <cassert>
#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
using Controls::Page;
static uint32_t clockMs=1000;
static uint32_t millis(){return clockMs;}
static unsigned normalWrites=0,normalLoads=0,statusReads=0,draws=0,levelPolls=0,restarts=0;
static void delay(unsigned ms){clockMs+=ms;}
static void normal(){++normalWrites;}
static UsbCommandGuard usbCommands;
struct SerialFake {
  std::deque<char> input;std::vector<std::string> output;
  unsigned available()const{return input.size();}
  char read(){const auto c=input.front();input.pop_front();return c;}
  void println(const char *s){output.emplace_back(s);}void flush(){}
  template<class... T>void printf(const char *,T...){}
  explicit operator bool()const{return true;}
  void feed(const std::string &s){for(char c:s)input.push_back(c);}
}Serial;
// USB output is drained (never Serial.flush(), which discards it): usb_tx.h.
namespace UsbTx {inline unsigned drains=0;inline bool drain(uint32_t){++drains;return true;}}
struct {void restart(){++restarts;}}ESP;
struct M5Fake {
  m5::Button_Class BtnA,BtnB;bool rawA=false,rawB=false;unsigned updates=0;
  void update(){++updates;BtnA.setRawState(millis(),rawA);BtnB.setRawState(millis(),rawB);}
  struct {unsigned rotation=0,brightness=0;void setRotation(unsigned n){rotation=n;}void setBrightness(unsigned n){brightness=n;}
    unsigned getBrightness()const{return brightness;}void setTextSize(unsigned){} }Display;
  struct {void setExtOutput(bool){normal();}}Power;
}M5;
struct IdleFake {template<class F>bool run(F fn){return fn();}};
static IdleFake &pm1Idle(){static IdleFake idle;return idle;}
struct {bool available=true;bool lock(){return available;}void unlock(){}bool quiet(uint32_t){return true;}}imuAcquisition;
static Controls::State controls;static Controls::Gesture gestureA,gestureB;
static WipeConfirmation wipeConfirmation;
static bool wipeFailed=false,wipeBlocked=false,wipeComplete=false;
static bool wipeCheckpointHeld=false,wipeBootRecovery=false,wipeRestartRequested=false;
static bool motionPending=false,dirty=false,diagnostics=false,historyPage=false,demo=false,motionDemo=false,tournamentMode=false;
static bool extPower=false,stress=false,restorePreview=false,settingsSaveError=false,displayFlipped=false;
static bool celebrationSound=false,soundPreview=false,lastFault=false,effectWasMoving=false;
static uint8_t brightnessPercent=Brightness::DEFAULT_PERCENT;
static uint32_t lastDraw=0,lastStatus=0,lastRevision=0,newSessionAt=0,tournamentHintUntil=0,uiStalls=0,historyOffset=0;
static AnalogTachometer::Phase lastPhase=AnalogTachometer::Phase::Ready;
namespace PracticeUI {enum class View:uint8_t{Recent,Best,Trend,Battery,Settings};}
static PracticeUI::View historyView=PracticeUI::View::Recent;
enum class TournamentView:uint8_t{RecordingOnly,Rpm};
static TournamentView tournamentView=TournamentView::RecordingOnly;
static const char *tournamentViewName(){return "recording_only";}
static RpmEstimator::Mode rpmEstimator=RpmEstimator::DEFAULT_MODE;
static StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
static MountOrientation::Mode mountMode=MountOrientation::Mode::Top;
static Appearance::Config appearance;static Appearance::Celebration bestEffect,editorEffect;static Themes::Library themeLibrary;
static InactivityTimer inactivity;static StickS3Level level;static StickS3Level::Reading drawnLevel;
static UsbHostAwake usbAwake;
struct {uint32_t magic=123;}sleepReceipt;
struct Sound {unsigned stops=0;void stop(){++stops;}}soundPlayer;
static struct {bool active=false;}soundRequest;
@CELEBRATION_HOLD@
struct Feedback {
  bool visible=true;void clear(){visible=false;}bool shown()const{return visible;}
  void show(uint32_t){visible=true;}bool tick(uint32_t){return false;}
  unsigned replayElapsed(uint32_t)const{return 0;}unsigned lastReplayElapsed()const{return 0;}
}launchFeedback;
struct Receiver {
  bool receiving=false;unsigned cancellations=0;
  bool active()const{return receiving;}void cancel(){receiving=false;++cancellations;}
  void start(uint32_t){receiving=true;normal();}
}restoreRx,themeRx,milestoneRx;
enum class Action{IdleCheckpointBegin,CheckpointEnd,Pin,Reset};
struct Snapshot {
  AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;
  bool opticalIdle=true;
  uint32_t launches=7,revision=1,revolutions=3;bool valid=true,signalFault=false;
  float resultRpm=6000,amplitude=1000,startNoise=3,contrast=1000,noise=3;uint64_t detectorUs=1000000;
  uint8_t pin=1;const char *endName()const{return "gap";}const char *stateName()const{return "Ready";}
};
struct Acquisition {
  Snapshot state;bool failBegin=false,advanceBeforeAck=false,launchBeforeAck=false,suspended=false;
  unsigned begins=0,resumes=0,eventDrains=0;std::deque<Snapshot> events;
  Snapshot snapshot()const{return state;}
  void displayBoundary(bool){}
  bool request(Action action,uint32_t expected=0) {
    if(action==Action::IdleCheckpointBegin) {
      ++begins;if(advanceBeforeAck)++state.launches;
      if(launchBeforeAck)state.phase=AnalogTachometer::Phase::Launch;
      if(failBegin || state.phase==AnalogTachometer::Phase::Launch || expected!=state.launches)return false;
      suspended=true;return true;
    }
    if(action==Action::CheckpointEnd){++resumes;suspended=false;return true;}
    normal();return true;
  }
  bool takeEvent(Snapshot &out) {
    ++eventDrains;if(events.empty())return false;out=events.front();events.pop_front();return true;
  }
}acquisition;
struct Practice {
  struct Record{uint32_t number=1,session=1;float rpm=6000;}record;
  bool accept(uint32_t,bool,float,uint32_t){normal();return true;}
  const Record *recent()const{return &record;}
}practice;
static LaunchMotion::Trace latestMotion,demoMotion;
namespace MotionUI {static void makeDemo(LaunchMotion::Trace &,bool){normal();}}
namespace MotionReplay {static bool playbackNeedsFrame(const LaunchMotion::Trace &,unsigned,unsigned){return false;}}
static bool visibleDisplayFlipped(){return false;}
struct Prefs {unsigned writes=0;size_t putUChar(const char *,uint8_t){++writes;normal();return 1;}
  bool putBool(const char *,bool){++writes;normal();return true;}}prefs;
struct {bool arm(){normal();return true;}bool restore(){normal();return true;}}shakeWake;
static unsigned restoreFailureNotes=0;static void noteMotionRestoreFailure(){++restoreFailureNotes;}
static void status(){++statusReads;}
static void draw(const Snapshot &){++draws;}
static void pollLevel(uint32_t){++levelPolls;}
static void previewSetting(){normal();}static void previewSound(){normal();}static void previewEffect(){normal();}
static void commitSetting(){normal();}static void cancelSettingEdit(){normal();}
static void toggleFeedback(){normal();}static void browseOlder(){normal();}static void newSession(){normal();}
static void pollRestore(const Snapshot &){normal();restoreRx.receiving=false;}
static void pollThemeRequest(const Snapshot &){normal();themeRx.receiving=false;}
static void listThemes(){normal();}static void themeReply(){}
static void pollMilestoneRequest(const Snapshot &){normal();milestoneRx.receiving=false;}
static void listMilestones(){normal();}static void milestoneReply(){}
static uint8_t settingValue(unsigned){return 0;}
static void exportPowerLog(){normal();}static void toggleHistory(){normal();}static void nextView(){normal();}
static void exportMotion(){normal();}static void exportHistory(){normal();}static void exportBackup(){normal();}
static void previewHistory(){normal();}static void checkStorage(){normal();}static void checkMotionStorage(){normal();}
static void exportScreen(){normal();}static void exportSignal(){normal();}
static Appearance::Effect celebrationEffect(){return Appearance::Effect::Flames;}
static void previewBestEffect(Appearance::Effect){normal();}
static void prepareMotion(const Snapshot &){normal();}static void logLaunchMetrics(const Snapshot &){normal();}
static void finishMotion(){normal();}static void retryMotion(uint32_t,const Snapshot &){normal();}
static void saveHistory(uint32_t,const Snapshot &){normal();}
// Settings saves and the orphaned-checkpoint release must never run after an
// erase attempt: one would write old RAM, the other resume the held checkpoint.
static void saveSettings(uint32_t,const Snapshot &){normal();}static void releaseOrphanCheckpoint(uint32_t,const Snapshot &){normal();}
struct SettingsSave {enum Item:uint16_t {Pin=128,ExtPower=256};void mark(uint16_t){normal();}bool pending()const{return false;}}settingsSave;
static void pollBattery(uint32_t,const Snapshot &){normal();}static void tickPowerLog(uint32_t,const Snapshot &){normal();}static void tickHistoryLog(uint32_t,const Snapshot &){normal();}
static void pollUsbAwake(uint32_t,const Snapshot &){normal();}static void tickSound(const Snapshot &){normal();}
// Side-key power-off has its own production harness; its wipe guard is tested there.
static bool powerKeyRequested=false;static uint32_t powerKeyRequestedAt=0;
static void pollPowerKey(uint32_t){}static void powerKeyOff(uint32_t,const Snapshot &){}
static void beginWipe();static void cancelWipe();
namespace DeviceWipe {
// Host replacement of the platform adapter only. The transaction core above
// still executes real inspect/begin/resume and its exact namespace order.
struct NvsStore {
  inline static State marker=State::Clean;
  inline static std::map<std::string,std::string> records;
  inline static unsigned inspections=0,intents=0,clears=0,removals=0;
  inline static unsigned failClear=0;inline static bool failIntent=false,failRemove=false,boot=false;
  static void writerGuard(){assert(wipeBlocked || boot);assert(boot || acquisition.suspended || wipeBootRecovery);}
  State inspectIntent(){++inspections;return marker;}
  bool setIntent(){writerGuard();++intents;if(failIntent)return false;marker=State::Pending;return true;}
  bool clearNamespace(const char *name) {
    writerGuard();assert(marker==State::Pending && ownedNamespace(name));++clears;
    records.erase(name);return clears!=failClear; // Failure may follow partial mutation.
  }
  inline static unsigned flashClears=0;
  bool clearFlash(){writerGuard();assert(marker==State::Pending && clears>=NAMESPACE_COUNT);++flashClears;return true;}
  bool clearIntent(){writerGuard();++removals;if(failRemove)return false;marker=State::Clean;return true;}
  static void seed(State state=State::Clean) {
    marker=state;records.clear();for(const auto *name:NAMESPACES)records[name]="old synthetic RAM image";
    records["unrelated"]="must survive";inspections=intents=clears=removals=failClear=flashClears=0;
    failIntent=failRemove=boot=false;
  }
};
}
'''


TESTS = r'''
using Store=DeviceWipe::NvsStore;
using Stage=WipeConfirmation::Stage;
static void reset() {
  clockMs=1000;normalWrites=normalLoads=statusReads=draws=levelPolls=restarts=0;
  controls={};gestureA={};gestureB={};wipeConfirmation={};
  wipeFailed=wipeBlocked=wipeComplete=wipeCheckpointHeld=wipeBootRecovery=wipeRestartRequested=false;
  motionPending=dirty=diagnostics=historyPage=demo=motionDemo=tournamentMode=false;
  extPower=stress=restorePreview=settingsSaveError=displayFlipped=celebrationSound=soundPreview=lastFault=effectWasMoving=false;
  lastDraw=lastStatus=lastRevision=newSessionAt=tournamentHintUntil=uiStalls=historyOffset=0;
  acquisition={};M5={};imuAcquisition.available=true;Serial={};restoreRx={};soundPlayer={};soundRequest={};
  M5.BtnA.setHoldThresh(1000);M5.BtnB.setHoldThresh(1000);
  launchFeedback={};inactivity={};usbAwake={};appearance={};bestEffect={};editorEffect={};sleepReceipt.magic=123;
  prefs={};Store::seed();
}
static void stepAt(uint32_t now,bool a=false,bool b=false,bool fresh=true) {
  clockMs=now;M5.rawA=a;M5.rawB=b;imuAcquisition.available=fresh;loop();
}
static void step(bool a=false,bool b=false,bool fresh=true){stepAt(clockMs+20,a,b,fresh);}
static void tapA(){step(true);step(false);step(false);}
static void open() {
  controls.page=Page::Settings;controls.setting=unsigned(Controls::Setting::WipeAll);
  tapA();assert(controls.page==Page::Wipe && wipeConfirmation.stage()==Stage::Warning);
  assert(wipeConfirmation.active() && !wipeConfirmation.awaitingRelease() && !Store::intents);
}
static void arm() {
  tapA();assert(wipeConfirmation.stage()==Stage::Confirm);
  tapA();assert(wipeConfirmation.stage()==Stage::Armed && !wipeConfirmation.awaitingRelease());
}
static uint32_t startHold(){const auto start=clockMs+20;stepAt(start,true);return start;}
static void holdUntil(uint32_t start,uint32_t duration) {
  for(uint32_t elapsed=100;elapsed<duration;elapsed+=100)stepAt(start+elapsed,true);
  stepAt(start+duration,true);
}
static void noErase(){assert(!Store::intents && !Store::clears && !Store::removals && !wipeBlocked && !restarts);}
static void usbBlocked() {
  const auto stage=wipeConfirmation.stage();const auto writes=normalWrites;
  // A transfer can also become active externally after the dialog opens.
  // The entry guard must cancel it before pollRestore, not just ignore R.
  restoreRx.receiving=true;themeRx.receiving=true;milestoneRx.receiving=true;
  Serial.feed("aaoooRgxE0SyzwntqcdhPuKeiMU\npp");
  while(Serial.available())pollCommands(acquisition.snapshot());
  assert(!restoreRx.active() && !themeRx.active() && !milestoneRx.active() && wipeConfirmation.stage()==stage && normalWrites==writes && statusReads>=2);
  noErase();
}
static void pendingUsbPrefix() {
  for(const char *prefix:{"g","E"}) {
    reset();Serial.feed(prefix);pollCommands(acquisition.snapshot());open();
    const auto writes=normalWrites;Serial.feed("b\n");pollCommands(acquisition.snapshot());
    assert(controls.page==Page::Settings && !wipeConfirmation.active());
    // If the old prefix survived cancellation, this digit/newline would
    // change GPIO or play an effect on the normal settings screen.
    Serial.feed("1\n");pollCommands(acquisition.snapshot());
    assert(normalWrites==writes && !acquisition.begins);noErase();
  }
}
static void physicalAndUsb() {
  reset();restoreRx.receiving=true;open();assert(!restoreRx.active());
  usbBlocked();tapA();assert(wipeConfirmation.stage()==Stage::Confirm);usbBlocked();
  tapA();assert(wipeConfirmation.stage()==Stage::Armed);usbBlocked();
  // Direct compatibility callbacks cannot create the final physical event.
  for(unsigned i=0;i<8;++i){holdA();buttonA();pollWipe(clockMs,true,acquisition.snapshot());}noErase();
  const auto start=startHold();holdUntil(start,2999);noErase();
  const auto writes=normalWrites,drains=acquisition.eventDrains;
  acquisition.events.push_back(acquisition.snapshot());Serial.feed("Rtxgn");
  stepAt(start+3000,true);
  assert(wipeComplete && wipeBlocked && wipeCheckpointHeld && acquisition.suspended);
  assert(Store::clears==DeviceWipe::NAMESPACE_COUNT && Store::flashClears==1 && Store::removals==1 && Store::marker==DeviceWipe::State::Clean);
  assert(Store::records.size()==1 && Store::records.at("unrelated")=="must survive");
  assert(normalWrites==writes && acquisition.eventDrains==drains && !restarts && !acquisition.resumes);
  step(true);assert(!restarts && normalWrites==writes);
  // A cached physical release cannot authorize reboot either.
  M5.rawA=false;M5.update();step(false,false,false);assert(!restarts);
  step();assert(restarts==1 && wipeRestartRequested && !sleepReceipt.magic);
  step();assert(restarts==1 && normalWrites==writes && !acquisition.resumes);
}
static void interruptedHold() {
  for(unsigned kind=0;kind<4;++kind) {
    reset();open();arm();const auto start=startHold();holdUntil(start,2800);
    if(kind==0)stepAt(start+3000,true,true); // B wins at the deadline.
    if(kind==1)stepAt(start+3000,true,false,false); // Cached down never completes.
    if(kind==2)stepAt(start+3501,true); // Blocking operation is not observed holding.
    if(kind==3)stepAt(start+2820,false); // A debounced release resets progress.
    noErase();
    if(kind==0)assert(controls.page==Page::Settings && !wipeConfirmation.active());
    else {
      assert(wipeConfirmation.elapsed(clockMs)==0);
      step(false);assert(!wipeConfirmation.awaitingRelease());
      const auto next=startHold();holdUntil(next,3000);assert(wipeComplete);
    }
  }
  reset();open();arm();const auto opened=clockMs;
  stepAt(opened+30000);assert(controls.page==Page::Settings && !wipeConfirmation.active());noErase();
  reset();open();Serial.feed("b");pollCommands(acquisition.snapshot());
  assert(controls.page==Page::Settings && !wipeConfirmation.active());noErase();
}
static void busyDoesNotWrite() {
  for(unsigned kind=0;kind<4;++kind) {
    reset();open();arm();
    if(kind==0)motionPending=true;
    if(kind==1)acquisition.launchBeforeAck=true;
    if(kind==2)acquisition.failBegin=true;
    if(kind==3)acquisition.advanceBeforeAck=true;
    const auto start=startHold();holdUntil(start,3000);noErase();
    assert(wipeFailed && wipeConfirmation.active() && wipeConfirmation.stage()==Stage::Warning);
    assert(!wipeCheckpointHeld && !acquisition.suspended && !acquisition.resumes);
    assert(acquisition.begins==(kind==0?0u:1u));
  }
}
static void unseenWarningsDuringLaunch() {
  for(unsigned advances=0;advances<3;++advances) {
    reset();open();for(unsigned i=0;i<advances;++i)tapA();
    const auto stage=wipeConfirmation.stage();const auto drawCount=draws;
    acquisition.state.phase=AnalogTachometer::Phase::Launch;
    tapA();tapA();assert(wipeConfirmation.stage()==stage && draws==drawCount);noErase();
    // A press begun while the screen is frozen, then released on the first
    // Ready frame, must not acknowledge a warning the owner has not seen.
    step(true);acquisition.state.phase=AnalogTachometer::Phase::Ready;step(false);step(false);
    assert(wipeConfirmation.stage()==stage);noErase();
    for(unsigned i=advances;i<2;++i)tapA();
    assert(wipeConfirmation.stage()==Stage::Armed);
    const auto start=startHold();holdUntil(start,3000);assert(wipeComplete);
  }
  // Even entry into Wipe during a burst must wait for a displayed warning.
  reset();controls.page=Page::Settings;controls.setting=unsigned(Controls::Setting::WipeAll);
  acquisition.state.phase=AnalogTachometer::Phase::Launch;tapA();
  assert(controls.page==Page::Wipe && wipeConfirmation.stage()==Stage::Warning && !draws);
  tapA();assert(wipeConfirmation.stage()==Stage::Warning);noErase();
  acquisition.state.phase=AnalogTachometer::Phase::Ready;step();assert(draws);
  arm();const auto entryHold=startHold();holdUntil(entryHold,3000);assert(wipeComplete);

  reset();open();arm();const auto start=startHold();holdUntil(start,2800);
  acquisition.state.phase=AnalogTachometer::Phase::Launch;stepAt(start+2900,true);
  assert(wipeConfirmation.elapsed(clockMs)==0 && wipeConfirmation.awaitingRelease());
  acquisition.state.phase=AnalogTachometer::Phase::Ready;
  for(unsigned elapsed=3000;elapsed<=6000;elapsed+=100)stepAt(start+elapsed,true);
  noErase();step();assert(!wipeConfirmation.awaitingRelease());
  const auto retry=startHold();holdUntil(retry,3000);assert(wipeComplete);

  reset();open();arm();acquisition.state.phase=AnalogTachometer::Phase::Launch;
  step(false,true);assert(controls.page==Page::Settings && !wipeConfirmation.active());noErase();
  reset();open();acquisition.state.phase=AnalogTachometer::Phase::Launch;
  stepAt(clockMs+30000);assert(controls.page==Page::Settings && !wipeConfirmation.active());noErase();
}
static void partialFailureBlocksOldRam() {
  for(unsigned kind=0;kind<3;++kind) {
    reset();open();arm();Store::failIntent=kind==0;Store::failClear=kind==1?3:0;Store::failRemove=kind==2;
    const auto start=startHold();holdUntil(start,3000);
    assert(wipeBlocked && wipeFailed && !wipeComplete && wipeCheckpointHeld && acquisition.suspended);
    const auto writes=normalWrites,drains=acquisition.eventDrains,clears=Store::clears;
    acquisition.events.push_back(acquisition.snapshot());Serial.feed("Rtxg1\nbnaooS");restoreRx.receiving=true;
    for(unsigned i=0;i<10;++i)step();
    assert(normalWrites==writes && acquisition.eventDrains==drains && Store::clears==clears);
    assert(!restarts && !acquisition.resumes && Serial.available());
    // The exact blocked-loop/display-power path dims without retrying writes,
    // draining queued results or abandoning the pending wipe transaction.
    const auto stillWrites=normalWrites,stillDraws=draws;
    stepAt(clockMs+30000);
    assert(M5.Display.brightness==26 && normalWrites==stillWrites && draws==stillDraws);
    cancelWipe();assert(controls.page==Page::Wipe && wipeBlocked);
    tapA();assert(wipeConfirmation.active());
    step(false,true);assert(!wipeConfirmation.active() && controls.page==Page::Wipe);
    step();assert(!restarts); // Cancel's B release cannot become error-page reboot.
    Store::failIntent=Store::failRemove=false;Store::failClear=0;
    restoreRx.receiving=false;Serial.input.clear();tapA();assert(wipeConfirmation.active());
    arm();const auto retry=startHold();holdUntil(retry,3000);
    assert(wipeComplete && acquisition.begins==1 && !acquisition.resumes);
    assert(Store::records.size()==1 && !restarts);step();assert(restarts==1);
  }
}
static void bootRecovery() {
  for(auto state:{DeviceWipe::State::Clean,DeviceWipe::State::Pending,DeviceWipe::State::Invalid,DeviceWipe::State::Error}) {
    reset();Store::marker=state;Store::boot=true;actualBootBoundary();Store::boot=false;
    if(state==DeviceWipe::State::Clean || state==DeviceWipe::State::Pending) {
      assert(normalLoads==1 && !wipeBlocked && !wipeBootRecovery);
      assert(Store::clears==(state==DeviceWipe::State::Pending?DeviceWipe::NAMESPACE_COUNT:0));
      if(state==DeviceWipe::State::Pending)assert(!sleepReceipt.magic && Store::records.size()==1);
    } else {
      assert(!normalLoads && wipeBlocked && wipeBootRecovery && wipeFailed && !Store::clears);
      assert(controls.page==Page::Wipe && !acquisition.begins);
      const auto writes=normalWrites;Serial.feed("RtxbnaooS");step();assert(normalWrites==writes);
    }
  }
  reset();Store::marker=DeviceWipe::State::Pending;Store::failClear=2;Store::boot=true;
  actualBootBoundary();Store::boot=false;
  assert(!normalLoads && wipeBlocked && wipeBootRecovery && Store::marker==DeviceWipe::State::Pending);
  Store::failClear=0;tapA();arm();const auto retry=startHold();holdUntil(retry,3000);
  assert(wipeComplete && !acquisition.begins && !acquisition.resumes);step();assert(restarts==1);
}
static void standbyGuard() {
  reset();open();const auto writes=normalWrites;
  inactivity.touch(millis()-inactivity.timeout());autoPowerOff(millis(),acquisition.snapshot());
  assert(normalWrites==writes && !acquisition.begins);
  controls.page=Page::Main;wipeBlocked=true;autoPowerOff(millis(),acquisition.snapshot());
  assert(normalWrites==writes && !acquisition.begins);
}
int main() {
  physicalAndUsb();pendingUsbPrefix();interruptedHold();busyDoesNotWrite();unseenWarningsDuringLaunch();
  partialFailureBlocksOldRam();bootRecovery();standbyGuard();
  std::cout<<"PASS: actual wipe sketch/loop/USB flow; physical3s, fresh observations, release restart, "
              "busy/late-owner gates, partial-failure old-RAM write exclusion, boot recovery and standby guards\n";
}
'''


def main():
    source = SOURCE.read_text()
    setup = function(source, "setup", static=False)
    boundary = "if(!recoverWipe())return;"
    recovery = setup.index(boundary)
    for load in ('prefs.begin(', 'powerPrefs.begin(', 'practiceStore.begin(',
                 'appearanceStore.begin(', 'bestStore.begin(', 'motionStore.begin(',
                 'startMotion()', 'acquisition.begin('):
        if setup.index(load) < recovery:
            raise RuntimeError("Pending wipe recovery must precede normal load/task: " + load)
    names = ["exportCpuPower", "syncNavigation", "navigate", "buttonA", "buttonB", "holdA", "holdB",
             "beginWipe", "cancelWipe", "restartAfterWipe", "commitWipe", "pollWipe", "tickDisplayPower"]
    bodies = "\n".join(function(source, name) for name in names)
    hold = re.search(r"static constexpr uint32_t CELEBRATION_HOLD_MS=\d+;", source)
    bodies += "\n" + function(source, "recoverWipe", result="bool")
    bodies += "\n" + function(source, "runUsbCommand") + "\n" + function(source, "pollCommands")
    # Execute the exact earliest standby exclusion; the remaining existing
    # sleep/save behavior has its own full USB/standby production harness.
    off = function(source, "autoPowerOff")
    bodies += off[:off.index("  if(usbAwake.inhibited(now))")] + "normal();}\n"
    bodies += function(source, "loop", static=False)
    bodies += "\nstatic void actualBootBoundary(){" + boundary + "++normalLoads;}\n"
    buttons = ROOT / "tests/fakes/m5_button"
    provenance = json.loads((buttons / "PROVENANCE.json").read_text())
    if provenance["source_commit"] != BUTTON_COMMIT:
        raise RuntimeError("Review the vendored M5Unified button revision before changing its pin")
    for name, expected in BUTTON_FILES.items():
        if hashlib.sha256((buttons / name).read_bytes()).hexdigest() != expected:
            raise RuntimeError("Vendored M5Unified button/license hash mismatch: " + name)
    with tempfile.TemporaryDirectory(prefix="launchlab-wipe-pipeline-") as temp:
        build = Path(temp)
        cpp = build / "test.cpp"
        cpp.write_text(PREFIX.replace('@CELEBRATION_HOLD@', hold[0] if hold else '') + bodies + TESTS)
        executable = build / "test"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(ROOT / "tests/fakes"), "-I" + str(ROOT / "LaunchLabMini"),
                        "-I" + str(ROOT / "LaunchLabRpm"), "-I" + str(buttons),
                        str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
