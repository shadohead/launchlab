#!/usr/bin/env python3
"""Execute the exact production side-key click and power-off functions under bounded fakes."""
import pathlib
import os
import re
import subprocess
import tempfile
from history_log_glue import closed

firmware=pathlib.Path(__file__).resolve().parents[1]
source=(firmware/'LaunchLabMini/LaunchLabMini.ino').read_text()
def body(start):
    cursor=source.index('{',start)+1
    depth=1
    while depth:
        depth+=(source[cursor]=='{')-(source[cursor]=='}')
        cursor+=1
    return source[start:cursor].replace('LOOP_HELPER ','')
def function(name,result='void'):
    return body(source.index('static '+result+' '+name+'('))
def shutdown_flush():
    enum=source.index('enum class ShutdownFlush')
    return source[enum:source.index('};',source.index('enum class ShutdownWhy'))+2]+'\n'+body(source.index('static ShutdownFlush flushForShutdown('))
wait=re.search(r'static constexpr uint32_t POWER_KEY_WAIT_MS=\d+;',source).group()

prefix=r'''
#include "history_checkpoint.h"
#include "power_status.h"
#include "power_savers.h"
#include "frame_rows.h"
static FrameRows frameRows;
#include "practice_store.h"
#include "appearance_store.h"
#include "power_diagnostics.h"
#include "controls.h"
#include "level_indicator.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <iostream>
#include <string>
static Appearance::Bests personalBests;static AppearanceStore<Appearance::Bests> bestStore;
static Controls::State controls;static bool wipeBlocked=false;
// The mic is off unless a test says otherwise: leaving its page stops it.
#include "mic_session.h"
static struct {bool running=false,stuck=false;uint32_t stopAt=0;bool off()const{return !running;}
  bool holdsSleep(uint32_t now)const{return MicSession::holdsSleep(!running?MicSession::State::Off:stopAt?MicSession::State::Stopping:MicSession::State::On,now-stopAt);}
  void dump(){}void askBench(){}void armTape(){}void sendTape(){}unsigned heard=0;void launch(uint32_t,bool,float,uint64_t,uint64_t){++heard;}}micListener;
template<class E> static void followAnalysisLaunch(const E &e){micListener.launch(e.launches,e.valid,e.resultRpm,e.burstStartHostUs,e.burstEndHostUs);}  // Analysis closed
static uint32_t millis();
static unsigned micPageLeaves=0;static void leaveMicPage(){++micPageLeaves;if(!micListener.stuck)micListener.running=false;else if(!micListener.stopAt)micListener.stopAt=millis();}
static uint64_t clockUs=1000000;
static uint32_t millis(){return uint32_t(clockUs/1000);}
static int64_t wallUs(){return clockUs;}
struct SerialFake {std::string last,lines;void println(const char *s){last=s;lines+=s;lines+='\n';}void flush(){}
  template<class... A>void printf(const char *f,A... a){char line[320];snprintf(line,sizeof(line),f,a...);last=line;lines+=line;}
  bool has(const char *s)const{return lines.find(s)!=std::string::npos;}} Serial;
// USB output is drained (never Serial.flush(), which discards it): usb_tx.h.
namespace UsbTx {inline unsigned drains=0;inline bool drain(uint32_t){++drains;return true;}}

static InactivityTimer inactivity;static OpticalSleepGuard opticalGuard;static ShutdownSaveGuard shutdownSaves;static PracticeHistory practice;static PracticeStore practiceStore;
static bool motionPending=false,saveError=false,dirty=false,mutexAvailable=true,irqLine=true;
static uint32_t lastSaveTry=0,saveErrors=0,historyPendingSince=0;
static unsigned locks=0,unlocks=0,keyReads=0,wipeCancels=0,editCancels=0,powerSaves=0;
static PowerDiagnostics::Image powerLog;static bool powerLogDirty=false;
static bool powerKeyRequested=false;
static uint32_t powerKeyRequestedAt=0,lastPowerKeyRead=0;
'''+wait+r'''
static constexpr uint8_t POWER_KEY_IRQ_PIN=13;
static int digitalRead(uint8_t pin){assert(pin==13);return irqLine;}
struct Key {bool pending=false;bool takeClick(){assert(locks>unlocks);++keyReads;const bool c=pending;pending=false;irqLine=true;return c;}};
static Key key;static Key &powerKey(){return key;}
enum class Action {ShutdownBegin,ShutdownForce,CheckpointEnd,Pin};
struct Snapshot {AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;uint32_t launches=0;bool valid=true;float resultRpm=6000;uint8_t pin=1;
  uint32_t revision=1,revolutions=40;bool signalFault=false;float amplitude=900,startNoise=8,contrast=1000,noise=8;
  uint64_t detectorUs=5000000,burstStartHostUs=4000000,burstEndHostUs=4800000;
  float minRpm=4000,belowMinRpm=0;uint32_t belowMin=0;
  const char *endName()const{return "slowdown";}const char *stateName()const{return "REARM";}};
// The loop's record of the last event: an event is a new result once.
static uint32_t lastRevision=0,lastBelowMin=0;static AnalogTachometer::Phase lastPhase=AnalogTachometer::Phase::Ready;static bool lastFault=false;
static StickS3Level level;
struct Acquisition {
  Snapshot state;bool suspended=false,refuse=false;unsigned begins=0,resumes=0,forced=0;std::deque<Snapshot> events;
  Snapshot snapshot(){return state;}
  bool takeEvent(Snapshot &e){if(events.empty())return false;e=events.front();events.pop_front();return true;}
  bool request(Action action,uint32_t expected=0){
    if(action==Action::CheckpointEnd || action==Action::Pin){suspended=false;++resumes;return true;}
    // ShutdownForce drops marks that never became a pull (acquisition.h).
    if(refuse || (action!=Action::ShutdownForce && state.phase==AnalogTachometer::Phase::Launch) || expected!=state.launches)return false;
    if(action==Action::ShutdownForce)++forced;
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
// Silenced before the IMU gate is taken: the speaker's own power-down takes it.
static struct {unsigned shutdowns=0;void shutdown(){assert(locks==unlocks);++shutdowns;}} soundPlayer;
static UsbStandby usbStandby{};static uint64_t standbyRecheckUs(){return 900000000;}
struct PoweredOff {};
// Set by a test that expects a power-off with a refused save left in RAM.
static bool unsavedOff=false;
struct ShakeWake {unsigned arms=0,sleeps=0;bool off=true;
  bool arm(){++arms;return true;}
  bool sleep(uint64_t recheckUs){
    // Everything a player would miss is durable and the screen is dark first.
    assert(recheckUs==900000000);
    assert(locks>unlocks && (unsavedOff || (!practiceStore.pending(practice) && !bestStore.pending(personalBests))));
    assert(!M5.Display.brightness && M5.Display.asleep && acquisition.suspended && powerLog.pendingSleep && sleepReceipt.magic);
    // A USB-retained standby knows it began at the side key.
    assert(soundPlayer.shutdowns && usbStandby.magic==UsbStandby::MAGIC && usbStandby.keyOnly);
    ++sleeps;if(off)throw PoweredOff{};return false;
  }
} shakeWake;
static unsigned restoreFailureNotes=0;static void noteMotionRestoreFailure(){++restoreFailureNotes;}
// What an accepted practice pull does (prepareMotion: motion capture, best,
// celebration; logLaunchMetrics; the start-mark reference), counted.
static unsigned motionPreps=0,celebrations=0,metricLogs=0,startMarkNotes=0;
static void prepareMotion(const Snapshot&,bool celebrate=true){++motionPreps;celebrations+=celebrate;motionPending=true;}
static void logLaunchMetrics(const Snapshot&){++metricLogs;}static void noteStartMark(const Snapshot&){++startMarkNotes;}static void saveStartMarks(){}
// Settings persistence has its own production harness (check_save_paths.py).
static void writeSettings(){}
static bool calibrationNeedsSave(){return false;}
static void cancelSettingEdit(){++editCancels;}
static void cancelWipe(){++wipeCancels;controls.page=Controls::Page::Settings;}
'''
tests=r'''
static void at(uint32_t now){clockUs=uint64_t(now)*1000;}
static void reset(){
  controls={};wipeBlocked=false;clockUs=1000000;shutdownSaves={};
  inactivity=InactivityTimer{};inactivity.setMinutes(3);inactivity.touch(millis());
  practice=PracticeHistory{};practiceStore=PracticeStore{};bestStore=AppearanceStore<Appearance::Bests>{};personalBests={};
  motionPending=saveError=dirty=false;mutexAvailable=irqLine=true;lastSaveTry=saveErrors=historyPendingSince=0;
  locks=unlocks=keyReads=wipeCancels=editCancels=powerSaves=0;
  PowerDiagnostics::fresh(powerLog);powerLogDirty=false;sleepReceipt={};
  powerKeyRequested=false;powerKeyRequestedAt=lastPowerKeyRead=0;key=Key{};
  acquisition=Acquisition{};M5=M5Fake{};shakeWake=ShakeWake{};Serial.last.clear();Serial.lines.clear();
  lastRevision=lastBelowMin=0;lastPhase=AnalogTachometer::Phase::Ready;lastFault=false;level={};micListener.heard=0;
  motionPreps=celebrations=metricLogs=startMarkNotes=0;
  Preferences::data.clear();Preferences::zeroLengthKeys.clear();Preferences::failWrite=Preferences::shortRead=false;Preferences::beforeWrite=nullptr;
  assert(practiceStore.begin(practice) && bestStore.begin(personalBests,"ll-bests","best0","best1"));
}
static void click(){key.pending=true;irqLine=false;}
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

  // Marks that never end are not a pull: after the wait the off stops
  // sampling anyway (through 0.20.5 it gave up and the unit stayed on).
  reset();click();acquisition.state.phase=AnalogTachometer::Phase::Launch;
  for(uint32_t t=1000;t<1000+POWER_KEY_WAIT_MS;t+=5){at(t);assert(!step());}
  assert(powerKeyRequested && !acquisition.begins && !shakeWake.sleeps);
  at(1000+POWER_KEY_WAIT_MS);
  if(!step())std::cout<<"steady marks kept the side key from powering off\n";
  assert(shakeWake.sleeps==1 && acquisition.forced==1);
  std::cout<<"PASS: marks that never end: after the wait the side key stops sampling and powers off\n";
  // An ADC another owner holds refuses even the forced stop: the request is dropped.
  reset();click();acquisition.state.phase=AnalogTachometer::Phase::Launch;acquisition.refuse=true;
  for(uint32_t t=1000;t<=1000+POWER_KEY_WAIT_MS;t+=5){at(t);assert(!step());}
  assert(!powerKeyRequested && !shakeWake.sleeps && !acquisition.suspended);
  // A motion capture that never finishes drops it too, without stopping sampling.
  reset();click();motionPending=true;
  for(uint32_t t=1000;t<=1000+POWER_KEY_WAIT_MS;t+=5){at(t);assert(!step());}
  assert(!powerKeyRequested && !shakeWake.sleeps && !acquisition.begins);
  std::cout<<"PASS: an acquisition that refuses even a forced stop drops the request instead of powering off later\n";

  reset();unsavedPull();click();Preferences::failWrite=true;
  assert(!step() && !powerKeyRequested && saveError && !acquisition.suspended && !shakeWake.sleeps);
  assert(practiceStore.pending(practice) && M5.Display.brightness==100 && !M5.Display.asleep && !powerLog.sleeps);
  std::cout<<"PASS: a failed save keeps the unit on with sampling resumed and the pull still pending\n";

  // The history log closed (a newer layout, a flash failure): NVS is the only
  // store. One that refuses every write (full) kept every click from powering
  // off through 0.20.10, until flat. The third refusal in a row
  // (ShutdownSaveGuard::LIMIT) powers off anyway, the pull lost as on a flat battery.
  reset();unsavedPull();Preferences::failWrite=true;
  static_assert(ShutdownSaveGuard::LIMIT==3,"the limit the docs give");
  for(uint32_t n=1;n<ShutdownSaveGuard::LIMIT;++n) {
    at(1000+n*1000);click();
    assert(!step() && !powerKeyRequested && saveError && !acquisition.suspended && !shakeWake.sleeps && !powerLog.sleeps);
  }
  at(1000+ShutdownSaveGuard::LIMIT*1000);click();unsavedOff=true;
  if(!step())std::cout<<"a refused save kept the side key from ever powering off\n";
  assert(shakeWake.sleeps==1 && practiceStore.pending(practice) && saveError);
  assert(Serial.lines.find("POWER_OFF_NOTICE history_unsaved")!=std::string::npos);
  unsavedOff=false;Preferences::failWrite=false;
  std::cout<<"PASS: with NVS the only store and refusing, the third click in a row powers off anyway, unsaved\n";

  reset();shakeWake.off=false;click();
  assert(!step() && shakeWake.sleeps==1 && !acquisition.suspended && M5.Display.brightness==100 && !M5.Display.asleep);
  assert(!powerLog.pendingSleep && !sleepReceipt.magic && dirty && locks==unlocks);
  std::cout<<"PASS: PM1 refusing power-off restores display and sampling\n";

  reset();wipeBlocked=true;click();assert(!step() && !powerKeyRequested && !acquisition.begins);
  reset();controls.page=Controls::Page::Wipe;click();assert(step() && wipeCancels==1);
  std::cout<<"PASS: clicks are ignored during an erase; an unconfirmed wipe prompt is cancelled before power-off\n";

  reset();micListener.running=true;click();assert(!step() && micPageLeaves==1 && powerKeyRequested && !acquisition.begins && !shakeWake.sleeps);
  assert(step() && !micListener.running);
  std::cout<<"PASS: a click on the Mic RPM page leaves it and powers off only once the mic has stopped\n";

  reset();micListener=decltype(micListener){};micListener.running=micListener.stuck=true;micPageLeaves=0;click();
  assert(!step() && micPageLeaves==1 && micListener.stopAt==1000 && powerKeyRequested && !acquisition.begins);
  at(1000+MicSession::STOP_LIMIT_MS-1);assert(!step() && powerKeyRequested && !acquisition.begins && !shakeWake.sleeps);
  at(1000+MicSession::STOP_LIMIT_MS);assert(step() && Serial.lines.find("POWER_OFF_NOTICE mic_stop_timeout\n")!=std::string::npos);
  micListener=decltype(micListener){};
  std::cout<<"PASS: a mic whose stop never ends holds the power key's off MicSession::STOP_LIMIT_MS at most\n";

  reset();mutexAvailable=false;click();assert(!step() && !irqLine && !keyReads && !powerKeyRequested);
  mutexAvailable=true;at(1005);assert(step());
  std::cout<<"PASS: a busy I2C gate leaves the latched line for the next pass\n";

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
  // A pull whose result the shutdown flush drains (it finished between the
  // loop's drain and the flush) is handled as the loop handles it.
  // On the Wear check page a retraction belongs to Wear check: never to
  // practice history, bests, motion capture or the start-mark reference
  // (through 0.20.10 the flush saved it as a practice pull, without USB lines).
  reset();controls.page=Controls::Page::WearCheck;controls.wear.reset();
  for(uint32_t t=0;t<200;t+=20){at(1000+t);level.feed(0,0,1,millis());}
  controls.wear.observeOptics(1000000,0,true);
  assert(controls.wear.observeBurst(1,true,4800,level.reading(millis()))==WearCheck::Outcome::Extension);
  controls.wear.observeOptics(1600000,0,true);assert(controls.wear.step()==WearCheck::Step::LetGo);
  {
    const auto bests=personalBests.generation();
    acquisition.state.launches=2;Snapshot retraction;retraction.launches=2;retraction.revision=7;retraction.resultRpm=3100;
    acquisition.events.push_back(retraction);click();
    // A Wear check outcome is a pull: the off waits a pass, as for a practice pull.
    assert(!step() && powerKeyRequested && !acquisition.suspended);
    if(practice.size())std::cout<<"a Wear check retraction drained at shutdown was saved as a practice pull\n";
    assert(!practice.size() && !practiceStore.pending(practice) && personalBests.generation()==bests);
    assert(!motionPreps && !motionPending && !metricLogs && !startMarkNotes && !Serial.has("HISTORY_ADDED"));
    assert(controls.wear.stageCount(0)==1 && controls.wear.pullRpm(0,0)==3100.0f);
    assert(Serial.has("LAUNCH_RESULT valid=1 peak_rpm=3100.0") && Serial.has("WEAR_CHECK_PULL stage=level pull=1/3 outcome=accepted rpm=3100.0"));
    assert(lastRevision==7 && micListener.heard==1);
    at(1100);assert(step() && !practice.size());
    PracticeHistory reboot;PracticeStore reader;assert(reader.begin(reboot) && !reboot.size());
  }
  std::cout<<"PASS: a Wear check retraction drained by the shutdown flush goes to Wear check (USB lines, no practice pull, best, capture or start mark)\n";

  // On Main the drained pull is saved, with the loop's USB lines and the mic
  // told, but starts no celebration: the unit is about to power off.
  reset();acquisition.events.push_back(Snapshot{AnalogTachometer::Phase::Ready,1,true,7000,1});click();
  assert(!step() && practice.size()==1 && motionPreps==1 && !celebrations && metricLogs==1 && startMarkNotes==1);
  if(!Serial.has("HISTORY_ADDED"))std::cout<<"a pull drained at shutdown printed no USB result lines\n";
  assert(Serial.has("LAUNCH_RESULT valid=1 peak_rpm=7000.0") && Serial.has("HISTORY_ADDED number=1 session=1 rpm=7000.0"));
  assert(lastRevision==1 && micListener.heard==1);
  // The same result offered again (the loop already took it) is not a new pull.
  motionPending=false;acquisition.state.launches=1;acquisition.events.push_back(Snapshot{AnalogTachometer::Phase::Ready,1,true,7000,1});
  at(1500);assert(step() && practice.size()==1 && motionPreps==1);
  std::cout<<"PASS: a practice pull drained at shutdown is saved with LAUNCH_RESULT and HISTORY_ADDED and no celebration; a repeated result is taken once\n";
  // The Bluetooth restart goes dark next too: its flush takes the pull without
  // a celebration. (The inactivity sleep's, which the pull abandons, may
  // celebrate: check_usb_pipeline.)
  reset();acquisition.events.push_back(Snapshot{AnalogTachometer::Phase::Ready,1,true,7000,1});
  assert(flushForShutdown(millis(),acquisition.snapshot(),ShutdownWhy::Restart)==ShutdownFlush::NewPull);
  assert(practice.size()==1 && motionPreps==1 && !celebrations && Serial.has("HISTORY_ADDED number=1"));
  std::cout<<"PASS: a pull drained by the Bluetooth restart's flush is saved without a celebration\n";

  std::cout<<"PASS: exact production pollPowerKey, flushForShutdown and powerKeyOff\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp=pathlib.Path(tmp)/'power_key_pipeline.cpp'
    # The per-event handler the loop and the flush share, when the sketch has one.
    route=function('routeEvent','bool')+'\n' if 'static bool routeEvent(' in source else ''
    cpp.write_text(prefix+closed(source)+function('logWearPull')+'\n'+route+function('pollPowerKey')+'\n'+shutdown_flush()+'\n'+function('powerKeyOff')+tests)
    exe=pathlib.Path(tmp)/'power_key_pipeline'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+str(firmware/'tests/fakes'),'-I'+str(firmware/'LaunchLabMini'),'-I'+str(firmware/'LaunchLabRpm'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

# One function owns each launch event: the loop and the shutdown flush both
# call it, so the two cannot drift apart again (through 0.20.10 the flush had
# its own copy, which skipped Wear check and the USB result lines).
route=body(source.index('static bool routeEvent('))
loop=body(source.index('void loop() {'))
# No GPIO interrupt in the sketch: ESP-IDF installs the GPIO ISR service on
# the IPC task (1 KB stack in the PM SDK), and an interrupt arriving inside that
# allocation overflowed it at boot ("Stack canary watchpoint triggered (ipc1)",
# 0.21.0 gate). The power key's PM1 line is latched, so it is polled instead.
import re as _re
for _path in sorted((firmware/'LaunchLabMini').rglob('*')):
    if _path.suffix in ('.ino', '.h', '.cpp', '.c'):
        _found = _re.findall(r'\b(attachInterrupt|gpio_install_isr_service|gpio_isr_register|gpio_isr_handler_add)\s*\(', _path.read_text(errors='replace'))
        assert not _found, f'{_path.name} installs a GPIO interrupt ({_found[0]}): poll the line instead'
print('PASS: the sketch installs no GPIO interrupt (the power key is polled)')
assert source.count('practice.accept(')==1 and 'practice.accept(' in route, 'practice history is offered a pull outside routeEvent'
assert source.count('controls.wear.observeBurst(')==1 and 'controls.wear.observeBurst(' in route, 'Wear check is offered a burst outside routeEvent'
assert source.count('"LAUNCH_RESULT ')==1 and source.count('"HISTORY_ADDED ')==1, 'a launch USB line is printed outside routeEvent'
assert source.count('micListener.launch(')==1 and source.count('followAnalysisLaunch(event);')==1 and 'followAnalysisLaunch(event);' in route, \
    'the mic and Analysis are told of a launch outside routeEvent'
assert 'while(acquisition.takeEvent(event))routeEvent(event,millis(),true);' in loop
assert 'while(acquisition.takeEvent(event))if(routeEvent(event,now,why==ShutdownWhy::Sleep))newPull=true;' in shutdown_flush()
# Each caller says why it flushes: the save guard and the celebration follow from that.
assert 'flushForShutdown(now,s,ShutdownWhy::Sleep,force)' in function('autoPowerOff')
assert 'flushForShutdown(now,s,ShutdownWhy::SideKey,late)' in function('powerKeyOff')
assert 'flushForShutdown(now,s,ShutdownWhy::Restart)' in function('restartForBluetooth')
assert source.count('flushForShutdown(now,s')==3
print('PASS: the loop and the shutdown flush hand every launch event to routeEvent, its only handler')
