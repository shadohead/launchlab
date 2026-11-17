#!/usr/bin/env python3
"""Host-execute Analysis interruption/capture helpers without touching a device.

The session, trace, raw packet writer and sketch helpers are production code.
The microphone fake deliberately overwrites both source buffers on each launch
so the preservation-before-reuse contract is checked independently of scheduling.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "LaunchLabMini/LaunchLabMini.ino").read_text()


def function(name):
    match = re.search(r"(?:LOOP_HELPER )?static [^\n{;]+\b" + name + r"\([^;]*?\)\s*\{", source)
    assert match, f"Production function missing: {name}"
    opening = source.index("{", match.start())
    depth, cursor = 1, opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[match.start():cursor]


state = source[source.index("static constexpr const char *CAL_KEY"):
               source.index("static void observeForecastCheckpoint(")]

# Execute the actual Analysis-specific prefix; the rest of navigate concerns
# settings/history and has separate production-helper harnesses.
navigation = function("navigate")
navigation = navigation[:navigation.index("  if(previous==Controls::Page::Wipe")] + "}\n"

# Execute every production condition before the optical sleep guard. Reaching
# the sentinel means Analysis no longer inhibits normal inactivity processing.
auto_power = function("autoPowerOff")
auto_power = auto_power[:auto_power.index("  const bool measuring=")] + "  ++sleepChecks;\n}\n"

# Side-key cleanup must run even when the microphone is already unavailable.
# Its hardware shutdown tail is exercised by check_power_key_pipeline.py.
power_key = function("powerKeyOff")
power_key = power_key[:power_key.index("  if(micListener.holdsSleep")] + "  ++sleepChecks;\n}\n"
restart = function("restartForBluetooth")
restart = restart[:restart.index("  if(flushForShutdown")] + "  ++restartChecks;\n}\n"
pairing = function("answerPairing")

# The restore guard must run before allocating, decoding, checkpointing, or
# writing the replacement backup. The existing restore harness owns that tail.
finish_restore = function("finishRestore")
finish_restore = finish_restore[:finish_restore.index("  auto contents=")] + "  ++restoreFinishes;\n}\n"
navigation_state = function("syncNavigation").replace("syncNavigation(", "syncNavigationState(", 1)
navigation_state += "\nstatic void syncNavigation(){++navigationSyncs;syncNavigationState();}\n"

prefix = r'''
#define LOOP_HELPER
#include "analysis_mode.h"
#include "audio_metadata_store.h"
#include "mic_trace.h"
#include "settings_save.h"
#include "controls.h"
#include "power_status.h"
#include "bluetooth_setting.h"
#include "analog_tachometer.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

constexpr unsigned MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
static unsigned allocations=0,failAllocation=0;
static void *heap_caps_malloc(size_t n,unsigned){
  if(failAllocation && !--failAllocation)return nullptr;
  void *p=std::malloc(n);if(p)++allocations;return p;
}
static void heap_caps_free(void *p){if(p){assert(allocations);--allocations;std::free(p);}}
static uint64_t clockUs=1000000;
static unsigned millis(){return unsigned(clockUs/1000);}
static uint64_t esp_timer_get_time(){return clockUs;}
static struct {uint8_t app_elf_sha256[32]={0x12,0x34};} app;
static auto esp_app_get_description(){return &app;}
static struct {std::string last;void println(const char *s){last=s;}template<class...T>void printf(const char*,T...){}
  size_t write(const uint8_t*,size_t n){return n;}} Serial;
static AnalysisMode::Session analysis;
static SettingsSave settingsSave;
static Controls::State controls;
static InactivityTimer inactivity;
static bool dirty=false,saveError=false;
static bool powerKeyRequested=true,wipeBlocked=false,motionPending=false;
static uint32_t powerKeyRequestedAt=1000;
static constexpr uint32_t POWER_KEY_WAIT_MS=10000;
static unsigned navigationSyncs=0,sleepChecks=0;
static unsigned restartChecks=0;
static bool bluetoothOn=true,bluetoothAtBoot=false;
namespace BleLink {
  static bool pending=false;
  static unsigned rejected=0;
  static bool connected(){return false;}
  static bool replyFlushed(uint32_t){return true;}
  static bool comparison(uint32_t&,bool&){return pending;}
  static void answerComparison(bool accepted){assert(!accepted);pending=false;++rejected;}
}
struct Button {bool wasClicked()const{return false;}};
static struct {Button BtnA,BtnB;} M5;
static struct Gesture {bool consumed=false;} gestureA,gestureB;
static void syncNavigation();
static bool demo=false,diagnostics=false,historyPage=true,tournamentMode=false;
static uint32_t newSessionAt=0,historyOffset=0;
namespace PracticeUI {enum class View {Recent,Battery,Settings};}
static PracticeUI::View historyView=PracticeUI::View::Recent;
static struct Practice {
  unsigned sessions=0,records=0;
  void newSession(){++sessions;}
  unsigned size()const{return records;}
  bool accept(uint32_t,bool,float,uint32_t){++records;return true;}
} practice,demoPractice;
static struct {void clear(){}} bestEffect;
static struct {void clear(){}} launchFeedback;
static struct {bool blocked=false;bool inhibited(uint32_t){return blocked;}} usbAwake;
namespace DeviceBackup {constexpr unsigned LOADED_TEXT_BYTES=64;}
static char *restoreText=nullptr;
static unsigned restoreFinishes=0;
static struct {unsigned starts=0;void start(char*,unsigned,unsigned){++starts;}} restoreRx;
struct Snapshot {
  uint32_t launches=0;bool valid=true;float resultRpm=6000;
  uint64_t burstStartHostUs=0,burstEndHostUs=0;
  AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;
};
struct Microphone {
  uint16_t slots[3000]{},raw[3000]{};
  uint8_t recovery[375]{},confidence[3000]{};
  MicTrace::Trace readings{slots,3000,raw,recovery,confidence};
  AudioMetadata::Recorder audio;
  unsigned launches=0,drains=0,freezes=0;
  uint32_t audioLaunch=0;
  bool available=true,hasTrace=true,queued=false;
  MicTrace::Step pending;
  const MicTrace::Trace *trace()const{return hasTrace?&readings:nullptr;}
  bool on()const{return available;}
  bool warm(unsigned)const{return available;}
  bool holdsSleep(unsigned)const{return false;}
  void drainTrace(){++drains;if(queued){readings.add(pending);queued=false;}}
  void freezeTrace(unsigned elapsed){++freezes;assert(drains);readings.freeze(elapsed);}
  const AudioMetadata::Recorder *audioMetadata(uint32_t launch)const{return launch==audio.launch()?&audio:nullptr;}
  void launch(uint32_t launch,bool valid,float rpm,uint64_t,uint64_t){
    ++launches;
    // The real task can reuse these before the caller next runs.
    audio.reset(launch);
    audioLaunch=launch;
    if(valid && available && hasTrace)readings.begin(launch,rpm);
  }
  void reset(){
    readings.clear();launches=drains=freezes=audioLaunch=0;
    available=hasTrace=true;queued=false;pending={};
    audio.reset(0);
  }
} micListener;
static struct {
  EarlySpin::Forecast forecast(uint32_t){return {};}
} analysisResults;
'''

tests = r'''
static unsigned packets(){unsigned n=0;for(auto *p=firstCalibration;p;p=p->next)++n;return n;}
static std::vector<uint8_t> bytes(const CalibrationPacket *p){
  const auto *data=reinterpret_cast<const uint8_t*>(p+1);
  return {data,data+p->raw};
}
static void reset(){
  while(firstCalibration){auto *p=firstCalibration;firstCalibration=p->next;p->~CalibrationPacket();heap_caps_free(p);}
  heap_caps_free(restoreText);restoreText=nullptr;restoreRx.starts=restoreFinishes=0;
  latestCalibration=nullptr;assert(!allocations);failAllocation=0;
  analysis.clear();controls={};controls.page=Controls::Page::Analysis;
  settingsSave={};inactivity={};inactivity.begin(1000);
  capturedAnalysisLaunch=lastCaptureTry=0;calibrationCapturePending=false;
  shadowLog.reset();micListener.reset();
  clockUs=1000000;dirty=saveError=false;navigationSyncs=sleepChecks=0;usbAwake.blocked=false;
  powerKeyRequested=true;wipeBlocked=motionPending=false;powerKeyRequestedAt=1000;
  demo=diagnostics=tournamentMode=false;historyPage=true;historyView=PracticeUI::View::Recent;
  restartChecks=0;BleLink::pending=false;BleLink::rejected=0;gestureA={};gestureB={};
  practice={};demoPractice={};newSessionAt=historyOffset=0;Serial.last.clear();
}
static Snapshot event(uint32_t number,bool valid=true,uint64_t release=1000000){
  return {number,valid,6000,release>=100000?release-100000:0,release};
}
static void seed(uint32_t number=1){
  followAnalysisLaunch(event(number));assert(analysis.running() && analysis.number()==number);
  for(unsigned i=0;i<3;++i){
    MicTrace::Step s;s.launch=number;s.index=uint16_t(2*i);s.rpm=s.rawRpm=uint16_t(5900-100*i);
    s.frame=true;s.reason=MicRpm::Reason::Tracking;micListener.readings.add(s);
  }
  AudioMetadata::Measurement m{};m[2]=90;m[3]=100;m[8]=77;micListener.audio.add(400,500,m);
  clockUs=21000000;
}
static void check(const CalibrationPacket *packet,uint32_t launch,uint32_t ms,unsigned reason,unsigned count=3){
  assert(packet);const auto raw=bytes(packet);
  assert(!memcmp(raw.data(),"LLC",3));
  assert(FlashLayout::get32(raw.data()+4)==launch);
  assert(FlashLayout::get32(raw.data()+12)==ms);
  assert(FlashLayout::get32(raw.data()+16)==reason);
  assert(FlashLayout::get32(raw.data()+20)==count);
  assert(!memcmp(raw.data()+64,app.app_elf_sha256,32));
  assert(FlashLayout::get32(raw.data()+raw.size()-4)==Decay::Record::checksum(raw.data(),raw.size()-4));
  if(count){
    // Decode the first raw point independently of packRaw: centre of first
    // acoustic window is 200 ms, never the wall clock or launch count.
    const uint8_t *p=raw.data()+96;uint32_t delta=0;unsigned shift=0;
    uint8_t b;do{b=*p++;delta|=uint32_t(b&127)<<shift;shift+=7;}while(b&128);
    assert((delta>>4)==200 && unsigned(p[0]|unsigned(p[1])<<8)==5900);
  }
}
int main(){
  // A remote Bluetooth restart must capture the raw trace first. A failed
  // allocation keeps the session intact until the restart retries.
  reset();seed();failAllocation=1;restartForBluetooth(millis(),event(1));
  assert(!restartChecks && calibrationCapturePending && controls.page==Controls::Page::Analysis);
  clockUs+=1000000;restartForBluetooth(millis(),event(1));
  check(latestCalibration,1,20000,2);
  assert(restartChecks==1 && controls.page==Controls::Page::Main);

  // Dismiss a pairing already on screen without consuming either button;
  // the same loop pass can still handle the user's manual stop.
  reset();seed();BleLink::pending=true;
  assert(!answerPairing(true,event(1)) && BleLink::rejected==1);
  assert(!gestureA.consumed && !gestureB.consumed && analysis.running());
  // A physical stop is captured once, including a queued reading, before
  // another launch can overwrite the raw trace or the acoustic metadata.
  reset();seed();
  micListener.pending.launch=1;micListener.pending.index=6;
  micListener.pending.rpm=micListener.pending.rawRpm=5500;micListener.queued=true;
  assert(analysis.stop(clockUs));assert(captureStoppedCalibration());
  check(latestCalibration,1,20000,1,4);assert(packets()==1 && !calibrationCapturePending);
  const auto saved=bytes(latestCalibration);auto *first=latestCalibration;
  assert(first->audio && first->audioData()[AudioMetadata::HEADER+AudioMetadata::RECORD_BYTES+8]==77);
  assert(captureStoppedCalibration() && packets()==1);
  followAnalysisLaunch(event(2,true,clockUs));
  assert(analysis.running() && analysis.number()==2 && packets()==1);
  assert(bytes(first)==saved && first->audioData()[AudioMetadata::HEADER+AudioMetadata::RECORD_BYTES+8]==77);

  // A second valid launch archives an unknown physical endpoint. Invalid or
  // repeated launch notifications leave the active recording untouched.
  reset();seed();auto snapshot=micListener.readings.rawCount();
  followAnalysisLaunch(event(2,false,clockUs));followAnalysisLaunch(event(1,true,clockUs));
  followAnalysisLaunch(event(0,true,clockUs));followAnalysisLaunch(event(2,true,0));
  assert(analysis.running() && analysis.number()==1 && !packets());
  assert(micListener.launches==1 && micListener.readings.rawCount()==snapshot);
  followAnalysisLaunch(event(2,true,clockUs));
  check(firstCalibration,1,20000,2);
  assert(analysis.running() && analysis.number()==2 && packets()==1);
  assert(firstCalibration->audioData()[AudioMetadata::HEADER+AudioMetadata::RECORD_BYTES+8]==77);

  // B hold takes the real navigation prefix, after controls changes page.
  reset();seed();const auto previous=controls.page;auto effect=controls.holdB();
  navigate(effect,previous);check(latestCalibration,1,20000,2);
  assert(controls.page==Controls::Page::Main && !analysis.number());
  navigate(Controls::Effect::None,controls.page);assert(packets()==1);

  // Side-key cleanup captures independently of mic availability.
  for(bool available:{false,true}){
    reset();seed();micListener.available=available;powerKeyOff(millis(),event(1));
    check(latestCalibration,1,20000,2);
    assert(controls.page==Controls::Page::Main && !analysis.number() && navigationSyncs==1 && sleepChecks==1);
    leaveMicPage();assert(packets()==1);
  }
  reset();seed();micListener.available=micListener.hasTrace=false;leaveMicPage();
  check(latestCalibration,1,20000,2,0);assert(latestCalibration->audio && AudioMetadata::valid(latestCalibration->audioData(),latestCalibration->audio));

  // Allocation failure must preserve the stopped session and its sources so
  // retry succeeds; never reset/reuse them merely to start the next launch.
  reset();seed();failAllocation=1;followAnalysisLaunch(event(2,true,clockUs));
  assert(!packets() && calibrationCapturePending && analysis.number()==1 && !analysis.running());
  assert(calibrationNeedsSave() && settingsSave.pending() && saveError && lastCaptureTry==millis());
  assert(micListener.launches==1 && micListener.readings.launch()==1);
  assert(captureStoppedCalibration());check(latestCalibration,1,20000,2);
  assert(!calibrationCapturePending);
  followAnalysisLaunch(event(2,true,clockUs));assert(analysis.number()==2 && analysis.running() && packets()==1);
  reset();seed();failAllocation=1;leaveMicPage();
  assert(controls.page==Controls::Page::Analysis && analysis.number()==1 && calibrationCapturePending);
  leaveMicPage();check(latestCalibration,1,20000,2);
  assert(controls.page==Controls::Page::Main && !analysis.number() && packets()==1);
  reset();seed();failAllocation=1;powerKeyOff(millis(),event(1));
  assert(!sleepChecks && calibrationCapturePending && controls.page==Controls::Page::Analysis);
  powerKeyOff(millis(),event(1));check(latestCalibration,1,20000,2);assert(sleepChecks==1);
  reset();seed();failAllocation=1;auto previousPage=controls.page;
  navigate(controls.holdB(),previousPage);
  assert(controls.page==Controls::Page::Analysis && calibrationCapturePending && !packets());
  previousPage=controls.page;navigate(controls.holdB(),previousPage);
  check(latestCalibration,1,20000,2);assert(controls.page==Controls::Page::Main);

  // Delayed queue delivery cannot put readings at/after the endpoint into
  // the raw packet or the fit input it later supplies.
  reset();seed();clockUs=1400000;
  assert(analysis.stop(clockUs) && captureStoppedCalibration());
  check(latestCalibration,1,400,1,1);

  // One-minute inactivity remains due while recording, but the real power
  // prefix returns until the five-minute recording limit ends the session.
  reset();seed();assert(inactivity.minutes()==1 && !inactivity.setMinutes(0));
  assert(inactivity.due(61000));
  for(uint32_t ms:{61000u,299000u}){clockUs=uint64_t(ms)*1000;autoPowerOff(ms,event(1));assert(!sleepChecks && analysis.running());}
  clockUs=1000000+AnalysisMode::Session::LIMIT_US+123456;
  assert(analysis.tick(clockUs) && captureStoppedCalibration());
  check(latestCalibration,1,300000,0);
  assert(!analysis.hasManualStop() && analysis.nextResult() && captureStoppedCalibration() && packets()==1);
  autoPowerOff(millis(),event(1));assert(sleepChecks==1);
  leaveMicPage();assert(packets()==1);

  // USB n starts a practice session without hiding or stopping Analysis.
  reset();seed();newSession();
  assert(practice.sessions==1 && historyPage && controls.page==Controls::Page::Analysis);
  assert(analysis.running() && analysis.number()==1 && !packets() && navigationSyncs==1);
  assert(micListener.readings.rawCount()==3 && !demo && !diagnostics);

  // USB t archives the active recording before showing synthetic history.
  // Failed capture must leave Analysis visible and must not create demo data.
  reset();seed();previewHistory();check(latestCalibration,1,20000,2);
  assert(!analysis.number() && controls.page==Controls::Page::Main && historyPage && demo);
  assert(demoPractice.size()==18*12 && packets()==1);
  reset();seed();failAllocation=1;previewHistory();
  assert(calibrationCapturePending && controls.page==Controls::Page::Analysis && historyPage);
  assert(!demo && !demoPractice.size() && !packets());
  previewHistory();check(latestCalibration,1,20000,2);
  assert(controls.page==Controls::Page::Main && demo && demoPractice.size()==18*12);

  // Restore is refused both at receipt and immediately before application:
  // active recording, failed snapshot allocation, raw-only RAM packets, and
  // optional attachments awaiting persistence must all survive the request.
  auto refusedRestore=[](){
    const unsigned before=allocations,starts=restoreRx.starts,finishes=restoreFinishes;
    startRestore();assert(restoreRx.starts==starts && allocations==before);
    assert(Serial.last=="RESTORE_ERROR reason=analysis_pending written=0");
    finishRestore(event(1));assert(restoreFinishes==finishes && allocations==before);
    assert(Serial.last=="RESTORE_ERROR reason=analysis_pending written=0");
  };
  reset();seed();refusedRestore();assert(analysis.running() && !packets());
  reset();seed();assert(analysis.stop(clockUs));failAllocation=1;assert(!captureStoppedCalibration());
  refusedRestore();assert(calibrationCapturePending && !packets());
  assert(captureStoppedCalibration());refusedRestore();check(latestCalibration,1,20000,1);
  latestCalibration->stored={1,unsigned(latestCalibration->raw),Calibration::Archive::BASE};
  refusedRestore(); // Audio metadata is still awaiting persistence.
  latestCalibration->audioSaved=true;latestCalibration->fit=Calibration::fitBytes(latestCalibration->count);
  refusedRestore(); // Raw durable, fit attachment still pending.
  latestCalibration->fitSaved=true;assert(!calibrationNeedsSave());
  startRestore();assert(restoreRx.starts==1);finishRestore(event(1));assert(restoreFinishes==1);

  // A recording that begins after receipt must be caught by finishRestore.
  reset();startRestore();assert(restoreRx.starts==1);
  seed();const auto before=allocations;finishRestore(event(1));
  assert(!restoreFinishes && allocations==before && analysis.running());

  // Wake never resumes an unfinished stopwatch or automatically opens mic.
  Controls::State wake;uint8_t view=0;controls.page=Controls::Page::Analysis;
  assert(Controls::decodeWake(Controls::encodeWake(controls,0),wake,view));
  assert(wake.page==Controls::Page::Main);
  reset();
  std::cout<<"PASS: production Analysis capture, replacement ordering, exit, timeout, retries, restore guards, USB navigation and wake reset\n";
}
'''

helpers = "\n".join(function(name) for name in (
    "observeForecastCheckpoint", "calibrationNeedsSave", "releaseSavedCalibrations", "captureStoppedCalibration",
    "interruptAnalysis", "followAnalysisLaunch", "leaveMicPage", "newSession", "previewHistory", "startRestore"))

with tempfile.TemporaryDirectory(prefix="analysis-lifecycle-") as directory:
    cpp = Path(directory) / "analysis_lifecycle.cpp"
    exe = Path(directory) / "analysis_lifecycle"
    cpp.write_text(prefix + navigation_state + state + helpers + navigation + auto_power + power_key + restart + pairing + finish_restore + tests)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I" + str(root / "tests/fakes"),
                    "-I" + str(root / "LaunchLabMini"), "-I" + str(root / "LaunchLabRpm"),
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
