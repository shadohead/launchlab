#!/usr/bin/env python3
"""Fault-inject the actual sketch startup/retry and owner begin functions.

The IMU transport, clock and RTOS allocators are replaced. This verifies cold
detection/data/allocation failures without claiming physical BMI270 validation.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'LaunchLabMini/LaunchLabMini.ino').read_text()
owner = (root / 'LaunchLabMini/motion_acquisition.h').read_text()

def extract(text, signature):
    start = text.index(signature)
    cursor = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[cursor] == '{') - (text[cursor] == '}')
        cursor += 1
    return text[start:cursor]

prefix = r'''
#include "analog_tachometer.h"
#include "mount_orientation.h"
#include <cmath>
#include <cassert>
#include <cstdint>
#include <iostream>
static uint32_t clockMs=100;
static uint32_t millis(){return clockMs;}
static void delay(uint32_t ms){clockMs+=ms;}
namespace m5 {
struct IMU_Class {static constexpr unsigned sensor_mask_accel=1,sensor_mask_gyro=2;};
enum class board_t {board_M5StickS3};
}
struct Vector {float x=.2f,y=-.3f,z=.9f;};
struct Data {Vector accel;Vector gyro{120,-200,60};};
struct FakeImu {
  bool enabled=false,stale=false,nonfinite=false,onlyAccel=false,deadAfterBegin=false;
  unsigned failures=0,begins=0,reads=0,calibrations=0;
  uint32_t readyAt=0,dataDelay=30;
  bool isEnabled(){return enabled;}
  int getType(){return enabled?6:0;}
  bool begin(void*,m5::board_t){
    ++begins;
    if(failures){--failures;enabled=false;return false;}
    enabled=true;stale=deadAfterBegin;nonfinite=false;onlyAccel=deadAfterBegin;
    readyAt=clockMs+dataDelay;return true;
  }
  unsigned update(){++reads;return enabled && !stale && clockMs>=readyAt?(onlyAccel?1:3):0;}
  Data getImuData(){Data d;if(nonfinite)d.gyro.x=NAN;return d;}
  void setCalibration(int,int,int){++calibrations;}
};
struct FakeM5 {FakeImu Imu;int In_I2C=0;} M5;
static unsigned rangeConfigs=0,rangeFailures=0;
static bool configureMotionSensor(){++rangeConfigs;if(rangeFailures){--rangeFailures;return false;}return M5.Imu.enabled;}
struct FakeSerial {template<class... T>void printf(const char*,T...){}}Serial;
using TaskHandle_t=void*;
using SemaphoreHandle_t=void*;
static constexpr int pdPASS=1;
static unsigned mutexCalls=0,taskCalls=0,mutexFailures=0,taskFailures=0;
static SemaphoreHandle_t xSemaphoreCreateMutex(){
  ++mutexCalls;if(mutexFailures){--mutexFailures;return nullptr;}
  return reinterpret_cast<void*>(uintptr_t(mutexCalls+100));
}
static int xTaskCreatePinnedToCore(void(*)(void*),const char*,int,void*,int,TaskHandle_t* out,int){
  ++taskCalls;if(taskFailures){--taskFailures;return 0;}
  *out=reinterpret_cast<void*>(uintptr_t(taskCalls+1000));return pdPASS;
}
class FakeAcquisition {
  SemaphoreHandle_t gate_=nullptr,ringGate_=nullptr;TaskHandle_t task_=nullptr;
  static void entry(void*){}
public:
  bool running(){return task_!=nullptr;}
  bool setMount(MountOrientation::Mode){return true;}
'''
globals_ = r'''
};
static FakeAcquisition imuAcquisition;
static constexpr uint32_t MOTION_INIT_RETRY_MS=5000,MOTION_DATA_TIMEOUT_MS=250;
static uint32_t motionInitAttempts=0,lastMotionInit=0;
static bool motionInitPaired=false,motionInitSuccess=false,motionPending=false,dirty=false;
static MountOrientation::Mode mountMode=MountOrientation::Mode::Top;
struct Snapshot {AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;};
'''
tests = r'''
static void reset(){
  clockMs=100;M5=FakeM5{};imuAcquisition=FakeAcquisition{};rangeConfigs=rangeFailures=0;
  mutexCalls=taskCalls=mutexFailures=taskFailures=0;
  motionInitAttempts=lastMotionInit=0;
  motionInitPaired=motionInitSuccess=motionPending=dirty=false;
}
static void boot(){for(unsigned attempt=0;attempt<3 && !startMotion();++attempt)if(attempt<2)delay(100);}
int main(){
  reset();assert(!imuAcquisition.begin());assert(mutexCalls==0 && taskCalls==0);
  M5.Imu.enabled=true;assert(imuAcquisition.begin());
  assert(imuAcquisition.begin() && mutexCalls==2 && taskCalls==1);
  reset();M5.Imu.enabled=true;mutexFailures=1;
  assert(!imuAcquisition.begin() && mutexCalls==2 && taskCalls==0);
  assert(imuAcquisition.begin() && mutexCalls==3 && taskCalls==1);
  reset();M5.Imu.enabled=true;taskFailures=1;
  assert(!imuAcquisition.begin() && mutexCalls==2 && !imuAcquisition.running());
  assert(imuAcquisition.begin() && mutexCalls==2 && taskCalls==2);
  std::cout<<"owner: disabled/allocation failures retry without leaked gates or duplicate tasks\n";

  reset();M5.Imu.failures=2;boot();
  assert(motionInitAttempts==3 && imuAcquisition.running() && motionInitPaired);
  assert(M5.Imu.begins==3 && mutexCalls==2 && taskCalls==1);
  unsigned reads=M5.Imu.reads;assert(startMotion());
  assert(motionInitAttempts==3 && M5.Imu.reads==reads);
  assert(rangeConfigs==1);
  std::cout<<"cold boot: two failed detections recover on third attempt; real paired moving data required\n";

  // M5 begin resets the BMI270 to 8 g. Fusion never starts on an unverified
  // range: an unreadable range forces re-detection, then a later retry.
  reset();M5.Imu.enabled=true;assert(startMotion() && rangeConfigs==1 && M5.Imu.begins==0);
  reset();M5.Imu.enabled=true;rangeFailures=1;
  assert(startMotion() && rangeConfigs==2 && M5.Imu.begins==1 && imuAcquisition.running());
  reset();M5.Imu.enabled=true;rangeFailures=2;
  assert(!startMotion() && rangeConfigs==2 && M5.Imu.begins==1 && !imuAcquisition.running() && M5.Imu.reads==0);
  std::cout<<"range: configured after every detection; unverified range blocks fusion start\n";

  reset();M5.Imu.enabled=true;M5.Imu.stale=true;
  assert(startMotion() && M5.Imu.begins==1 && clockMs>=380);
  reset();M5.Imu.enabled=true;M5.Imu.nonfinite=true;
  assert(startMotion() && M5.Imu.begins==1 && motionInitPaired);
  reset();M5.Imu.enabled=true;M5.Imu.onlyAccel=true;M5.Imu.deadAfterBegin=true;
  assert(!startMotion() && !motionInitPaired && !imuAcquisition.running());
  assert(clockMs==600 && mutexCalls==0 && taskCalls==0);
  std::cout<<"probe: cached, nonfinite and accel-only reads cannot falsely start fusion; 250 ms timeout\n";

  reset();M5.Imu.failures=3;boot();
  assert(motionInitAttempts==3 && !imuAcquisition.running());
  Snapshot state;clockMs=lastMotionInit+4999;retryMotion(clockMs,state);
  assert(motionInitAttempts==3);
  clockMs=lastMotionInit+5000;
  for(auto phase:{AnalogTachometer::Phase::Launch,AnalogTachometer::Phase::Hold,AnalogTachometer::Phase::Settling}){
    state.phase=phase;retryMotion(clockMs,state);assert(motionInitAttempts==3);
  }
  state.phase=AnalogTachometer::Phase::Ready;motionPending=true;retryMotion(clockMs,state);
  assert(motionInitAttempts==3);motionPending=false;retryMotion(clockMs,state);
  assert(motionInitAttempts==4 && imuAcquisition.running() && motionInitSuccess && dirty);
  retryMotion(clockMs+10000,state);assert(motionInitAttempts==4 && taskCalls==1);
  reset();lastMotionInit=UINT32_MAX-1000;clockMs=3998;retryMotion(clockMs,state);
  assert(motionInitAttempts==0);clockMs=3999;retryMotion(clockMs,state);
  assert(motionInitAttempts==1 && imuAcquisition.running());
  std::cout<<"idle recovery: bounded retries, phase/capture guards, timer wrap and one persistent owner\n";
  std::cout<<"PASS: exact production IMU startup/probe/retry and owner allocation functions\n";
}
'''
cpp = prefix + extract(owner, 'bool begin()') + globals_
for name in ('motionSensorPaired', 'startMotion', 'retryMotion'):
    kind = 'void' if name == 'retryMotion' else 'bool'
    cpp += '\n' + extract(source, 'static ' + kind + ' ' + name + '(')
cpp += tests
with tempfile.TemporaryDirectory(prefix='launchlab-imu-startup-') as temporary:
    temporary = Path(temporary)
    (temporary / 'test.cpp').write_text(cpp)
    subprocess.run(['c++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I' + str(root / 'LaunchLabRpm'),
                    '-I' + str(root / 'LaunchLabMini'),
                    str(temporary / 'test.cpp'), '-o', str(temporary / 'test')], check=True)
    subprocess.run([str(temporary / 'test')], check=True)
