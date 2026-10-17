#!/usr/bin/env python3
"""Execute exact .ino power checkpoint functions with a real detector.

Only M5/Preferences/FreeRTOS plumbing is replaced. This is software fault
injection, not physical NVS timing or wake validation.
"""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("sketch", nargs="?", type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parent.parent / "LaunchLabMini/LaunchLabMini.ino")
parser.add_argument("--expect-baseline", action="store_true")
args = parser.parse_args()
args.expect_fixed = not args.expect_baseline
source = args.sketch.read_text()

def function(name):
    start = source.index("static ", source.index(name) - 25)
    brace = source.index("{", start)
    depth = 1
    cursor = brace + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor]

prefix = r'''
#include "history_checkpoint.h"
#include "power_diagnostics.h"
#include <cassert>
#include <iostream>
static uint32_t clockMs=0;
static uint32_t millis(){return clockMs;}
static bool powerLogReady=true,powerLogDirty=true,motionPending=false;
static constexpr uint32_t HISTORY_COALESCE_MS=10000;
static uint32_t lastAcceptedAt=0;static HistoryCheckpoint::Stillness imuStill{true,0}; // launcher set down
struct {bool active=false;bool shown(){return active;}}launchFeedback;
static PowerDiagnostics::Image powerLog;
static uint32_t powerLogTick=0,lastPowerSample=0,lastPowerSave=0,lastPowerSaveTry=0;
static uint8_t powerFlags(){return 0;}
static void powerRecord(uint8_t event){PowerDiagnostics::append(powerLog,event,3700,0,100,240);powerLogDirty=true;}
struct FakePrefs {
  unsigned writes=0;
  bool fail=false;
  size_t putBytes(const char*,const void*,size_t n){++writes;return fail?0:n;}
} powerPrefs;
struct {struct {int getBrightness(){return 100;}} Display;} M5;
struct {void println(const char*){}} Serial;
enum class Action {IdleCheckpointBegin,CheckpointEnd};
struct Snapshot {AnalogTachometer::Phase phase;uint32_t launches;bool resting=false;};
struct FakeAcquisition {
  AnalogTachometer detector;
  HistoryCheckpoint saved;
  unsigned begins=0,ends=0,vetoes=0;bool veto=false;
  bool request(Action action,uint32_t=0){
    // A vetoed begin (IMU quiet barrier unavailable) enqueues nothing.
    if(action==Action::IdleCheckpointBegin && veto){++vetoes;return false;}
    if(action==Action::IdleCheckpointBegin){++begins;saved=HistoryCheckpoint(detector);}
    else {++ends;saved.restore(detector);}
    return true;
  }
} acquisition;
'''
test = r'''
static void reset(bool ready,bool fail){
  powerLogReady=ready;powerLogDirty=true;motionPending=false;lastAcceptedAt=0;launchFeedback.active=false;
  PowerDiagnostics::fresh(powerLog);powerLogTick=lastPowerSample=lastPowerSave=lastPowerSaveTry=0;
  powerPrefs=FakePrefs{};powerPrefs.fail=fail;acquisition=FakeAcquisition{};
  for(unsigned i=0;i<60000;++i)acquisition.detector.feed(2000);
  assert(acquisition.detector.phase==AnalogTachometer::Phase::Ready);
}
static void run(const char* name,bool ready,bool fail){
  reset(ready,fail);
  unsigned readyTicks=0;
  for(unsigned i=0;i<250;++i){
    clockMs=300000+i*10;
    for(unsigned j=0;j<500;++j)acquisition.detector.feed(2000);
    Snapshot s{acquisition.detector.phase,acquisition.detector.launches};
    tickPowerLog(clockMs,s);
    if(acquisition.detector.phase==AnalogTachometer::Phase::Ready)++readyTicks;
  }
  std::cout<<name<<": writes="<<powerPrefs.writes<<" checkpoints="<<acquisition.begins
           <<" ready_ticks="<<readyTicks<<"/250 phase="<<int(acquisition.detector.phase)<<"\n";
#ifdef EXPECT_FIXED
  assert(acquisition.begins<2 && readyTicks>100);
  if(!ready)assert(acquisition.begins==0 && powerPrefs.writes==0);
  if(fail){
    clockMs=600000;
    Snapshot s{acquisition.detector.phase,acquisition.detector.launches};
    tickPowerLog(clockMs,s);
    assert(acquisition.begins==2 && powerPrefs.writes==2);
    powerPrefs.fail=false;clockMs=900000;tickPowerLog(clockMs,s);
    assert(!powerLogDirty && powerPrefs.writes==3);
  }
#else
  assert(acquisition.begins==250 && readyTicks==0);
#endif
}
int main(){
  // A vetoed checkpoint is tried once per 5-minute window, not every UI tick,
  // and the dirty log stays pending for the next window.
  reset(true,false);acquisition.veto=true;
  for(unsigned i=0;i<250;++i){clockMs=300000+i*10;tickPowerLog(clockMs,{AnalogTachometer::Phase::Ready,0});}
  assert(acquisition.vetoes==1 && acquisition.begins==0 && acquisition.ends==0 && powerPrefs.writes==0 && powerLogDirty);
  acquisition.veto=false;clockMs=600000;tickPowerLog(clockMs,{AnalogTachometer::Phase::Ready,0});
  assert(acquisition.begins==1 && acquisition.ends==1 && powerPrefs.writes==1 && !powerLogDirty);
  std::cout<<"vetoed IMU barrier: one attempt per window, log kept pending, next window saves\n";
  reset(true,false);
  for(unsigned i=0;i<250;++i){clockMs=300000+i*10;tickPowerLog(clockMs,{AnalogTachometer::Phase::Ready,0,true});}
  assert(acquisition.begins==0 && powerPrefs.writes==0 && powerLogDirty);
  clockMs=600000;tickPowerLog(clockMs,{AnalogTachometer::Phase::Ready,0,false});
  assert(acquisition.begins==1 && powerPrefs.writes==1 && !powerLogDirty);
  run("failed NVS write",true,true);
  run("invalid existing optional power log",false,false);
  reset(true,false);clockMs=300000;
  tickPowerLog(clockMs,{acquisition.detector.phase,0});
  assert(powerPrefs.writes==1 && !powerLogDirty);
  clockMs=300010;tickPowerLog(clockMs,{acquisition.detector.phase,0});
  assert(powerPrefs.writes==1);
  reset(true,true);clockMs=300000;
  tickPowerLog(clockMs,{AnalogTachometer::Phase::Launch,0});assert(acquisition.begins==0);
  reset(true,false);clockMs=300000;lastAcceptedAt=clockMs-9000;
  tickPowerLog(clockMs,{AnalogTachometer::Phase::Ready,0});assert(powerPrefs.writes==0);
  clockMs+=1000;launchFeedback.active=true;tickPowerLog(clockMs,{AnalogTachometer::Phase::Ready,0});assert(powerPrefs.writes==0);
  launchFeedback.active=false;tickPowerLog(clockMs,{AnalogTachometer::Phase::Ready,0});assert(powerPrefs.writes==1);
  // Handheld, the power log waits for a 60 s pause after a pull, as history
  // does; three minutes overdue, it is written soon after a pull.
  reset(true,false);imuStill={};clockMs=300000;lastAcceptedAt=clockMs-20000;
  tickPowerLog(clockMs,{AnalogTachometer::Phase::Ready,0});assert(acquisition.begins==0 && powerLogDirty);
  clockMs=300000+HistoryCheckpoint::MAX_PENDING_MS-1;lastAcceptedAt=clockMs-4000;
  tickPowerLog(clockMs,{AnalogTachometer::Phase::Ready,0});assert(acquisition.begins==0 && powerLogDirty);
  clockMs+=1;lastAcceptedAt=clockMs-4000;
  tickPowerLog(clockMs,{AnalogTachometer::Phase::Ready,0});assert(powerPrefs.writes==1 && !powerLogDirty);
  imuStill={true,0};
  std::cout<<"handheld: power log waits 60 s after a pull, aged log written 3 s after one\n";
  std::cout<<"PASS: exact production checkpoint functions; failed/corrupt log, normal save, active burst, practice quiet and visible recap\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = pathlib.Path(tmp) / "test.cpp"
    cpp.write_text(prefix + function("savePowerLog()") + "\n" + function("tickPowerLog(") + test)
    executable = pathlib.Path(tmp) / "test"
    command = ["c++", "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
               "-fno-omit-frame-pointer", "-I" + str(args.sketch.parent), "-I" + str(args.sketch.parent.parent / "LaunchLabRpm"), str(cpp), "-o", str(executable)]
    if args.expect_fixed:
        command.insert(1, "-DEXPECT_FIXED")
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
