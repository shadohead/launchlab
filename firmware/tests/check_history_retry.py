#!/usr/bin/env python3
"""Run exact production capture/save/sleep functions under a host scheduler."""
import pathlib
import subprocess
import tempfile
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument("sketch", nargs="?", type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parent.parent / "LaunchLabMini/LaunchLabMini.ino")
args = parser.parse_args()
source = args.sketch.read_text()
def function(name):
    start = source.index("static void " + name + "(")
    cursor = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
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
prefix = r'''
#include "history_checkpoint.h"
#include "controls.h"
#include "accel_range.h"
#include "sensor_profile.h"
#include "practice_store.h"
#include "appearance_store.h"
#include "rpm_estimator.h"
#include "orientation_estimator.h"
#include "power_status.h"
struct IdleFake {template<class F>bool run(F fn){return fn();}};
static IdleFake &pm1Idle(){static IdleFake idle;return idle;}
#include "usb_awake.h"
#include "power_diagnostics.h"
#include "wake_ui_state.h"
#include "motion_replay.h"
#include <cassert>
#include <iostream>
#include <vector>
static Controls::State controls;
static bool wipeBlocked=false;
static Appearance::Config appearance;static Appearance::Bests personalBests;
static AppearanceStore<Appearance::Bests> bestStore;static AppearanceStore<Appearance::Config> appearanceStore;static Appearance::Celebration bestEffect;
static Appearance::Effect celebrationEffect(){return appearance.effect==Appearance::Effect::Random?Appearance::Effect::Orbit:appearance.effect;}
static uint64_t clockUs=1000000;
static uint32_t millis(){return uint32_t(clockUs/1000);}
static int64_t esp_timer_get_time(){return clockUs;}
static int64_t wallUs(){return clockUs;}
static constexpr uint32_t HISTORY_COALESCE_MS=10000,HISTORY_SAVE_RETRY_MS=5000;
static PracticeHistory practice;static PracticeStore practiceStore;
static LaunchMotion::Trace latestMotion,demoMotion;
static bool motionPending=false,saveError=false,dirty=false,tournamentMode=true,historyPage=false,diagnostics=false,motionDemo=false,demo=false;
static constexpr uint64_t MOTION_CAPTURE_RETRY_US=3000000;static uint32_t motionCaptureRetries=0;
static uint32_t lastSaveTry=0,maxSaveUs=0,saveCount=0,saveErrors=0,lastAcceptedAt=0,historyPendingSince=0,motionDrops=0,lastDraw=0,lastShutdownTry=0;
static HistoryCheckpoint::Stillness imuStill{true,0}; // launcher set down unless a case says otherwise
static uint64_t motionStart=0,motionEnd=0;
static void cancelSettingEdit(){}
static bool saveWakeNavigation(){return true;}
struct Feedback {void show(uint32_t){}bool shown(){return false;}bool replayStarted(){return false;}uint32_t replayElapsed(uint32_t){return 0;}uint32_t lastReplayElapsed(){return 0;}} launchFeedback;
struct HWCDC {static bool isPlugged(){return false;}};
struct SerialFake {void println(const char*){};void flush(){};template<class... Args>void printf(const char*,Args...){}} Serial;
// USB output is drained (never Serial.flush(), which discards it): usb_tx.h.
namespace UsbTx {inline unsigned drains=0;inline bool drain(uint32_t){++drains;return true;}}

enum class Action {IdleCheckpointBegin,CheckpointBegin,CheckpointEnd,HistoryIdleCheckpointBegin,ShutdownBegin,Pin};
struct Snapshot {AnalogTachometer::Phase phase;uint32_t launches;bool valid=true,signalFault=false;float resultRpm=6000;uint64_t burstStartHostUs=0,burstEndHostUs=0;uint8_t pin=1;StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;RpmEstimator::Mode estimator=RpmEstimator::DEFAULT_MODE;};
struct Acquisition {
  AnalogTachometer detector;HistoryCheckpoint saved;unsigned begins=0,lagUs=0,deliveryLagUs=0;bool suspended=false;
  Snapshot snapshot(){Snapshot s{detector.phase,detector.launches};s.resultRpm=detector.resultRpm;
    s.burstStartHostUs=clockUs-(detector.nowUs-detector.burstStartUs);
    s.burstEndHostUs=clockUs-(detector.nowUs-detector.burstEndUs);return s;}
  bool takeEvent(Snapshot&){return false;}
  bool request(Action action,uint32_t expected=0){
    if(action==Action::CheckpointEnd){saved.restore(detector);suspended=false;lagUs=deliveryLagUs;return true;}
    if(action==Action::HistoryIdleCheckpointBegin && !HistoryCheckpoint::allowedIdle(detector,expected))return false;
    if(action==Action::ShutdownBegin && (detector.phase==AnalogTachometer::Phase::Launch || detector.launches!=expected))return false;
    if(action==Action::CheckpointBegin && !HistoryCheckpoint::allowed(detector))return false;
    ++begins;saved=HistoryCheckpoint(detector);suspended=true;return true;
  }
} acquisition;
struct ImuAcquisition {
  LauncherOrientation orientation;std::unique_ptr<LaunchMotion::Ring> ring=std::make_unique<LaunchMotion::Ring>();uint64_t fused=0;bool clipped=false;
  struct Snapshot {uint64_t fusedUs;};Snapshot snapshot(){return {fused};}
  bool lock(){return true;}void unlock(){}
  bool capture(uint64_t start,uint64_t end,float rpm,uint32_t number,LaunchMotion::Trace &out){out=ring->build(start,end,rpm,number);return true;}
  void feed(){LaunchMotion::Sample raw;raw.us=clockUs;raw.a[2]=1;raw.clipped=clipped;
    orientation.feed(raw,[&](const LaunchMotion::Sample &s){ring->feed(s);fused=s.us;});}
} imuAcquisition;
static InactivityTimer inactivity;static UsbHostAwake usbAwake;static bool usbAutoOffInhibited=false;
static void pollUsbAwake(uint32_t,const Snapshot&,bool=false){}static PowerDiagnostics::Image powerLog;static bool powerLogDirty=false;
static bool extPower=false,reachedSleep=false;
enum class TournamentView:uint8_t {RecordingOnly,Rpm};static TournamentView tournamentView=TournamentView::RecordingOnly;
namespace PracticeUI {enum class View:uint8_t {Recent};}static PracticeUI::View historyView=PracticeUI::View::Recent;
struct {struct {void setBrightness(unsigned){}void sleep(){}void wakeup(){}}Display;
  struct {void setExtOutput(bool){}}Power;} M5;
struct {size_t putUInt(const char*,uint32_t){return 4;}bool remove(const char*){return true;}} prefs;
static uint8_t backlightValue(){return 100;}
static void powerRecord(uint8_t){}static bool savePowerLog(){return true;}
struct SleepReceipt{uint32_t magic;int64_t atUs;};static SleepReceipt sleepReceipt;
struct {bool arm(){return true;}bool restore(){return true;}
  bool sleep(){assert(!practiceStore.pending(practice));reachedSleep=true;return false;}} shakeWake;
static unsigned restoreFailureNotes=0;static void noteMotionRestoreFailure(){++restoreFailureNotes;}
static void logLaunchMetrics(const Snapshot&){}
// Settings persistence has its own production harness (check_save_paths.py).
static void writeSettings(){}
static unsigned writeDurationMs=178;static std::vector<uint32_t> writeAt;
'''
tests = r'''
static void level(unsigned value,uint64_t us){
  const uint64_t end=clockUs+us;
  while(clockUs<end){clockUs+=20;
    if(acquisition.lagUs)acquisition.lagUs=acquisition.lagUs>20?acquisition.lagUs-20:0;
    else if(!acquisition.suspended)acquisition.detector.feed(value);
    if(clockUs%10000==0)imuAcquisition.feed();
  }
}
static void reset(unsigned lag=0,uint64_t warmUs=2200000){
  controls={};wipeBlocked=false;
  Preferences::data.clear();Preferences::zeroLengthKeys.clear();Preferences::failWrite=false;Preferences::shortRead=false;
  clockUs=1000000;practice=PracticeHistory{};practiceStore=PracticeStore{};
  latestMotion={};acquisition=Acquisition{};imuAcquisition=ImuAcquisition{};
  lastSaveTry=maxSaveUs=saveCount=saveErrors=lastAcceptedAt=historyPendingSince=motionDrops=lastDraw=lastShutdownTry=0;
  motionPending=saveError=dirty=reachedSleep=false;writeAt.clear();writeDurationMs=178;
  inactivity=InactivityTimer{};PowerDiagnostics::fresh(powerLog);
  StickS3SensorProfile::configure(acquisition.detector);acquisition.detector.singleTurnPeak=true;
  acquisition.deliveryLagUs=lag;assert(practiceStore.begin(practice));
  Preferences::beforeWrite=[](){writeAt.push_back(millis());clockUs+=writeDurationMs*1000;};
  level(140,warmUs);assert(acquisition.detector.phase==AnalogTachometer::Phase::Ready);
}
static void tick(){finishMotion();saveHistory(millis(),acquisition.snapshot());}
static void spin(unsigned ms){for(unsigned i=0;i<ms/10;++i){level(140,10000);tick();}}
static void waitReady(){unsigned waited=0;while(acquisition.detector.phase!=AnalogTachometer::Phase::Ready){spin(10);assert(++waited<=150);}}
static void pull(){
  assert(acquisition.detector.phase==AnalogTachometer::Phase::Ready);
  for(unsigned i=0;i<12;++i){level(240,5000);level(140,5000);}
  // Marks stopping near the peak end the burst 50 ms later (no marks resume).
  level(240,2000);level(140,2000);level(240,60000);
  const auto event=acquisition.snapshot();assert(event.phase==AnalogTachometer::Phase::Hold);
  assert(practice.accept(event.launches,true,event.resultRpm,millis()));prepareMotion(event);
  while(motionPending){level(140,10000);tick();}
}
int main(){
  reset(0,1600000);pull();assert(latestMotion.valid() && latestMotion.points[0].ms>-200);
  reset();clockUs+=150000;spin(1600);pull();assert(latestMotion.valid() && latestMotion.points[0].ms>-200);
  std::cout<<"early recovery: settled pull at 1.6s after startup or a real gap uses shorter measured pre-roll\n";
  // A short gap (<=100 ms, one stalled FIFO read) is bridged: no reset, full
  // pre-roll, and the gap lies outside the pull so the replay stays exact.
  reset();clockUs+=65000;spin(1200);pull();
  assert(latestMotion.valid() && !latestMotion.bridged && latestMotion.points[0].ms<-400 && imuAcquisition.orientation.resets()==0);
  std::cout<<"short 65ms gap bridged: no warm-up, full pre-roll, exact replay\n";
  reset();pull();assert(latestMotion.valid());
  const uint32_t began=historyPendingSince;
  for(unsigned n=2;n<=4;++n){spin(1050);pull();assert(latestMotion.valid() && writeAt.empty());}
  assert(practiceStore.pendingCount(practice)==4);
  spin(HISTORY_COALESCE_MS);assert(writeAt.size()==1 && !practiceStore.pending(practice) && historyPendingSince==0);
  assert(writeAt[0]-lastAcceptedAt>=HISTORY_COALESCE_MS && writeAt[0]-began>HISTORY_COALESCE_MS);
  PracticeHistory reboot;PracticeStore reader;assert(reader.begin(reboot) && reboot.size()==4);
  std::cout<<"rapid bout: 4 valid captures, 1 coalesced Ready checkpoint, 4 records durable\n";
  waitReady();pull();assert(latestMotion.quality==LaunchMotion::Quality::Warming);
  spin(2300);assert(imuAcquisition.orientation.resets()>0);
  // Handheld (IMU never still): the automatic write waits for a 60 s pause
  // instead of landing on the usual 10-20 s re-pull time.
  reset();imuStill={};pull();spin(HISTORY_COALESCE_MS+5000);
  assert(writeAt.empty() && practiceStore.pending(practice));
  spin(HistoryCheckpoint::HANDHELD_QUIET_MS-HISTORY_COALESCE_MS);
  assert(writeAt.size()==1 && !practiceStore.pending(practice) && writeAt[0]-lastAcceptedAt>=HistoryCheckpoint::HANDHELD_QUIET_MS);
  // A handled bout with a pull every 25 s never gets a 60 s pause; once its
  // oldest unsaved pull is three minutes old it is written after the next one.
  reset();imuStill={};pull();const uint32_t bout=historyPendingSince;
  while(millis()-bout+25000<HistoryCheckpoint::MAX_PENDING_MS){spin(25000);assert(writeAt.empty());pull();}
  spin(25000);
  assert(writeAt.size()==1 && !practiceStore.pending(practice));
  assert(writeAt[0]-bout>=HistoryCheckpoint::MAX_PENDING_MS && writeAt[0]-bout<HistoryCheckpoint::MAX_PENDING_MS+10000);
  imuStill={true,0};
  std::cout<<"handheld: coalesced write waits 60 s since the last pull; a 25 s bout is saved within 3 min\n";
  reset();pull();const uint32_t oldest=historyPendingSince;
  for(unsigned n=0;n<20;++n){spin(1050);pull();assert(latestMotion.valid() && writeAt.empty());}
  assert(millis()-oldest>HISTORY_COALESCE_MS && practiceStore.pendingCount(practice)==21);
  spin(HISTORY_COALESCE_MS);assert(writeAt.size()==1 && !practiceStore.pending(practice));
  std::cout<<"long bout: no forced max-age write, 21 valid captures, one quiet checkpoint\n";
  for(unsigned lag:{0u,5120u,10000u}){
    reset(lag);pull();Preferences::failWrite=true;spin(HISTORY_COALESCE_MS);
    assert(writeAt.size()==1 && saveError && practiceStore.pending(practice));
    const uint32_t firstTry=writeAt[0];spin(2500);pull();assert(latestMotion.valid());
    assert(writeAt.size()==1);spin(HISTORY_COALESCE_MS);
    assert(writeAt.size()==2 && writeAt[1]-firstTry>=5000+writeDurationMs);
    assert(acquisition.detector.sustainedPeakRpm==6000 && acquisition.detector.singlePeakRpm==15000);
    Preferences::failWrite=false;spin(5000);
    assert(!practiceStore.pending(practice) && !saveError);
    std::cout<<"failed-write recovery: lag_us="<<lag<<" retry_ms="<<writeAt[1]-firstTry
             <<" valid_capture_before_retry=1 writes="<<writeAt.size()<<"\n";
  }
  reset();pull();assert(writeAt.empty() && practiceStore.pending(practice));
  inactivity.touch(millis()-inactivity.timeout());autoPowerOff(millis(),acquisition.snapshot());
  assert(reachedSleep && !practiceStore.pending(practice) && historyPendingSince==0 && writeAt.size()==1);
  PracticeHistory slept;PracticeStore afterSleep;assert(afterSleep.begin(slept) && slept.size()==1);
  std::cout<<"forced sleep: pending record durable before shakeWake.sleep call\n";
  reset();pull();Preferences::failWrite=true;
  inactivity.touch(millis()-inactivity.timeout());autoPowerOff(millis(),acquisition.snapshot());
  assert(!reachedSleep && practiceStore.pending(practice) && saveError && writeAt.size()==1);
  const uint32_t sleepTry=writeAt[0];spin(2500);pull();assert(latestMotion.valid() && writeAt.size()==1);
  unsigned waited=0;while(writeAt.size()==1){spin(10);assert(++waited<1500);}
  assert(writeAt.size()==2 && writeAt[1]-sleepTry>=5000+writeDurationMs);
  std::cout<<"failed sleep: no standby; 5s automatic retry backoff; valid capture before retry\n";
  reset();clockUs+=150000;spin(1200);pull();
  assert(latestMotion.quality==LaunchMotion::Quality::Warming && imuAcquisition.orientation.resets()>0);
  reset();imuAcquisition.clipped=true;pull();assert(latestMotion.quality==LaunchMotion::Quality::Clipped);
  std::cout<<"real 150ms gap and clipped paired samples still reject tilt capture\n";
  for(bool single:{false,true}){
    reset();acquisition.detector.singleTurnPeak=single;pull();const float expected=single?15000:6000;
    spin(HISTORY_COALESCE_MS);PracticeHistory saved;PracticeStore reader;assert(reader.begin(saved));
    assert(saved.size()==1 && saved.recent()->rpm==expected && acquisition.detector.resultRpm==expected);
    assert(acquisition.detector.sustainedPeakRpm==6000 && acquisition.detector.singlePeakRpm==15000);
  }
  std::cout<<"both selected RPM modes: correct history value after Ready checkpoint and reboot\n";
  // Enable the real PB store only for these scenarios; previous cases
  // deliberately isolate history write timing. Production shutdown must flush
  // both stores and defer standby if the second store fails.
  reset();personalBests={};bestStore={};assert(bestStore.begin(personalBests,"sleep-best","best0","best1"));pull();
  inactivity.touch(millis()-inactivity.timeout());autoPowerOff(millis(),acquisition.snapshot());
  assert(reachedSleep && !bestStore.pending(personalBests));
  Appearance::Bests durable;AppearanceStore<Appearance::Bests> bestReader;assert(bestReader.begin(durable,"sleep-best","best0","best1") && durable.rpm[0]>0);
  reset();personalBests={};bestStore={};assert(bestStore.begin(personalBests,"sleep-best-fail","best0","best1"));pull();
  assert(acquisition.request(Action::ShutdownBegin,acquisition.detector.launches));assert(practiceStore.save(practice));assert(acquisition.request(Action::CheckpointEnd));
  Preferences::failWrite=true;inactivity.touch(millis()-inactivity.timeout());autoPowerOff(millis(),acquisition.snapshot());
  assert(!reachedSleep && bestStore.pending(personalBests) && saveError && !acquisition.suspended);
  Preferences::failWrite=false;spin(HISTORY_COALESCE_MS+HISTORY_SAVE_RETRY_MS);assert(!bestStore.pending(personalBests));
  std::cout<<"PB shutdown: verified forced flush survives reboot; failed second-store write prevents standby and resumes ADC\n";
  std::cout<<"PASS: exact production prepare/capture/save/sleep; real VQF/ring/stores/detector; quiet coalescing, no forced mid-bout writes, longer fault backoff, output retention and forced flush\n";
}
'''
constants='\n'.join(re.search(r'static constexpr uint32_t '+name+r'=\d+;',source).group()
                    for name in ['HISTORY_COALESCE_MS','HISTORY_SAVE_RETRY_MS'])
prefix=prefix.replace('static constexpr uint32_t HISTORY_COALESCE_MS=10000,HISTORY_SAVE_RETRY_MS=5000;',constants)
with tempfile.TemporaryDirectory() as tmp:
    cpp=pathlib.Path(tmp)/"test.cpp"
    cpp.write_text(prefix+'\n'.join(function(name) for name in ['prepareMotion','finishMotion','saveHistory'])+'\n'+shutdown_flush()+'\n'+function('autoPowerOff')+tests)
    exe=pathlib.Path(tmp)/"test"
    subprocess.run(['c++','-std=c++17','-O1','-g','-DVQF_SINGLE_PRECISION','-fsanitize=address,undefined','-fno-omit-frame-pointer',
      '-I'+str(pathlib.Path(__file__).resolve().parent/'fakes'),'-I'+str(args.sketch.parent),'-I'+str(args.sketch.parent.parent/'LaunchLabRpm'),str(cpp),
      str(args.sketch.parent/'src/vqf/vqf.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
