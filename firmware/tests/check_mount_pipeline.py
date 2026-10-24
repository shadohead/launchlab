#!/usr/bin/env python3
"""Exercise the real motion owner and level consumer across mount boundaries.

Only class access is changed in a temporary header. Production setMount/run,
fusion, ring and pollLevel bodies execute unchanged with a fake IMU, clock and
real host timed mutexes. This proves software ownership, not physical mounting.
"""
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
HEADERS = ROOT / "LaunchLabMini"


def function(source, name):
    start = source.index("static void " + name + "(")
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


RUNTIME = r'''
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
using TickType_t = uint32_t;
using TaskHandle_t = void *;
using BaseType_t = int;
struct FakeMutex {
  std::timed_mutex mutex;
  std::atomic<unsigned> attempts{0}, releases{0};
  // Another task holds the gate for this long, in virtual time, so a wait
  // budget is tested without depending on how threads get scheduled.
  uint32_t heldByOtherMs=0;
};
using SemaphoreHandle_t = FakeMutex *;
static constexpr int pdTRUE=1,pdFALSE=0,pdPASS=1;
#define pdMS_TO_TICKS(ms) (ms)
namespace Fake {
inline uint64_t now=1000000;
inline unsigned completed=0,limit=0;
inline std::vector<std::unique_ptr<FakeMutex>> gates;
inline std::function<void(FakeMutex *)> locked,released;
inline std::function<void(unsigned)> delayed;
inline bool inHook=false;
// The scheduler model. By default every period wait blocks a whole period,
// as on an idle core. A realistic run follows FreeRTOS instead: a
// delay-until whose wake time has already passed returns at once without
// blocking, and each I2C transfer costs busy CPU time (the bus is polled).
inline bool realistic=false;
inline uint64_t transferUs=0;
// Longest stretch of task CPU time with no block: on device IDLE0 cannot run
// in it, and the task watchdog resets the chip after 5 s.
inline uint64_t busySince=0,longestBusyUs=0;
inline void blocked(uint64_t until) {
  longestBusyUs=std::max(longestBusyUs,now-busySince);now=std::max(now,until);busySince=now;
}
struct Stop {};
inline void hook(const std::function<void(FakeMutex *)> &callback,FakeMutex *gate) {
  if(callback && !inHook) {inHook=true;callback(gate);inHook=false;}
}
}
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  Fake::gates.emplace_back(new FakeMutex);return Fake::gates.back().get();
}
inline int xSemaphoreTake(SemaphoreHandle_t gate,TickType_t wait) {
  if(!gate)return pdFALSE;
  ++gate->attempts;
  if(const uint32_t held=gate->heldByOtherMs) {
    if(wait<held){Fake::blocked(Fake::now+uint64_t(wait)*1000);return pdFALSE;}
    Fake::blocked(Fake::now+uint64_t(held)*1000);gate->heldByOtherMs=0;
  }
  if(!gate->mutex.try_lock_for(std::chrono::milliseconds(wait)))return pdFALSE;
  Fake::hook(Fake::locked,gate);return pdTRUE;
}
inline void xSemaphoreGive(SemaphoreHandle_t gate) {
  ++gate->releases;gate->mutex.unlock();Fake::hook(Fake::released,gate);
}
inline TickType_t xTaskGetTickCount(){return TickType_t(Fake::now/1000);}
inline int xTaskCreatePinnedToCore(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *out,int) {
  *out=reinterpret_cast<void *>(uintptr_t(1));return pdPASS;
}
namespace Fake {inline unsigned stackScans=0;}
inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t){++Fake::stackScans;return 4096;}
inline BaseType_t xTaskDelayUntil(TickType_t *last,TickType_t period) {
  ++Fake::completed;
  if(Fake::delayed)Fake::delayed(Fake::completed);
  BaseType_t delayed=pdTRUE;
  if(!Fake::realistic){*last+=period;Fake::now+=uint64_t(period)*1000;Fake::busySince=Fake::now;}
  else {
    const TickType_t wake=*last+period;*last=wake;
    delayed=int32_t(wake-xTaskGetTickCount())>0;
    if(delayed)Fake::blocked(uint64_t(wake)*1000);
  }
  if(Fake::completed==Fake::limit)throw Fake::Stop{};
  return delayed;
}
inline void vTaskDelayUntil(TickType_t *last,TickType_t period){xTaskDelayUntil(last,period);}
inline void vTaskDelay(TickType_t ticks){Fake::blocked((Fake::now/1000+ticks)*1000);}
inline int64_t esp_timer_get_time(){return int64_t(Fake::now);}
namespace m5 {
struct IMU_Class {static constexpr unsigned sensor_mask_accel=1,sensor_mask_gyro=2;};
}
struct FakeVector {float x=0,y=0,z=0;};
struct FakeData {FakeVector accel{0,0,1},gyro;};
struct FakeImu {
  bool enabled=true;unsigned fresh=3,updates=0,calibrations=0;FakeData data;
  bool isEnabled()const{return enabled;}
  void setCalibration(int,int,int){++calibrations;}
  unsigned update(){++updates;return fresh;}
  FakeData getImuData()const{return data;}
};
inline struct {FakeImu Imu;} M5;
// The owner reads the BMI270 FIFO; the fake builds one header-mode frame per
// read from the fake IMU (gyro+accel, gyro-only, accel-only or nothing).
#include <cstddef>
namespace Bmi270Fifo {bool readRegisters(uint8_t reg,uint8_t *data,size_t n);bool writeRegister(uint8_t reg,uint8_t value);}
'''


TESTS = r'''
#include <cassert>
#include <cmath>
#include <iostream>
#include <thread>
#include "motion_owner_test.h"
using Mode=MountOrientation::Mode;
// Like the chip, the fake FIFO holds one gyro frame per 5 ms of sensor time
// since the previous read (at least one per read), the accelerometer in every
// second one, all with the fake IMU's values, and saturates at its capacity.
namespace FakeFifo {inline std::vector<uint8_t> pending;inline unsigned flushes=0,forcedLength=0,lengthReads=0,sensorFrames=0;inline uint64_t stallUs=0,lastUs=0;}
bool Bmi270Fifo::readRegisters(uint8_t reg,uint8_t *data,size_t n) {
  Fake::now+=Fake::transferUs;
  if(reg==FIFO_LENGTH_0 && n==2) {
    ++FakeFifo::lengthReads;auto &p=FakeFifo::pending;p.clear();
    if(FakeFifo::forcedLength){p.assign(FakeFifo::forcedLength,0x8C);data[0]=uint8_t(p.size());data[1]=uint8_t(p.size()>>8);return true;}
    auto &last=FakeFifo::lastUs;
    const int64_t since=last?int64_t(Fake::now)-int64_t(last):GYRO_PERIOD_US;
    const unsigned frames=unsigned(std::max<int64_t>(1,since/GYRO_PERIOD_US));
    last=(last?last:Fake::now-GYRO_PERIOD_US)+uint64_t(frames)*GYRO_PERIOD_US;
    const unsigned fresh=M5.Imu.update();const auto d=M5.Imu.getImuData();
    // Inverse of boardAxes: raw X=-board Y, raw Y=board X, raw Z=board Z.
    auto put=[&](const FakeVector &v,float scale) {
      const float raw[3]={-v.y/scale,v.x/scale,v.z/scale};
      for(float r:raw){const auto x=int16_t(std::lround(std::max(-32768.0f,std::min(32767.0f,r))));p.push_back(uint8_t(x));p.push_back(uint8_t(uint16_t(x)>>8));}
    };
    for(unsigned i=0;i<frames;++i) {
      const bool gyro=fresh&2,accel=(fresh&1) && FakeFifo::sensorFrames++%2==0;
      if(!gyro && !accel)continue;
      p.push_back(gyro && accel?ACC_GYR:gyro?GYR:ACC);
      if(gyro)put(d.gyro,StickS3AccelRange::GYRO_FULL_SCALE_DPS/32768);
      if(accel)put(d.accel,accelRange().fullScaleG()/32768);
    }
    if(p.size()>CAPACITY)p.resize(CAPACITY);
    data[0]=uint8_t(p.size());data[1]=uint8_t(p.size()>>8);return true;
  }
  if(reg==FIFO_DATA && n<=FakeFifo::pending.size()) {
    Fake::now+=FakeFifo::stallUs;
    std::copy(FakeFifo::pending.begin(),FakeFifo::pending.begin()+n,data);
    FakeFifo::pending.erase(FakeFifo::pending.begin(),FakeFifo::pending.begin()+n);return true;
  }
  return false;
}
bool Bmi270Fifo::writeRegister(uint8_t reg,uint8_t value) {
  if(reg==CMD && value==FLUSH){++FakeFifo::flushes;FakeFifo::pending.clear();FakeFifo::lastUs=Fake::now;}
  return true;
}
static bool near(float a,float b,float tolerance=1e-5f){return std::fabs(a-b)<=tolerance;}
static void vectorIs(const float *v,float x,float y,float z) {
  if(!near(v[0],x) || !near(v[1],y) || !near(v[2],z))
    std::cerr<<"vector at "<<Fake::now<<": "<<v[0]<<","<<v[1]<<","<<v[2]
             <<" expected "<<x<<","<<y<<","<<z<<"\n";
  assert(near(v[0],x) && near(v[1],y) && near(v[2],z));
}
static void cleanRuntime() {
  Fake::now=1000000;Fake::completed=0;Fake::locked={};Fake::released={};Fake::delayed={};
  Fake::inHook=false;M5.Imu={};
  Fake::realistic=false;Fake::transferUs=0;Fake::busySince=Fake::now;Fake::longestBusyUs=0;FakeFifo::lastUs=0;FakeFifo::sensorFrames=0;
}
static void runFor(StickS3MotionAcquisition &owner,unsigned count,
                   std::function<void(unsigned)> delayed={}) {
  Fake::completed=0;Fake::limit=count;Fake::delayed=delayed;
  try{owner.run();assert(false);}catch(const Fake::Stop &){}
  assert(Fake::completed==count);
  Fake::locked={};Fake::released={};Fake::delayed={};
}
static void sameState(const StickS3MotionAcquisition &owner,
                      const StickS3MotionAcquisition::Snapshot &saved,
                      unsigned ringSize,uint64_t latest,uint32_t frame,uint32_t resets) {
  assert(owner.frame_==frame && owner.mode_==saved.mount);
  assert(owner.ring_.size()==ringSize && owner.ring_.latest()==latest);
  assert(owner.orientation_.resets()==resets);
  const auto &s=owner.snapshot_;
  assert(s.mount==saved.mount && s.fusedUs==saved.fusedUs && s.pairedUs==saved.pairedUs);
  assert(s.orientationReady==saved.orientationReady && s.fusionSamples==saved.fusionSamples);
  assert(s.fusionResets==saved.fusionResets && near(s.biasSigma,saved.biasSigma));
  for(unsigned j=0;j<3;++j) {
    assert(near(s.a[j],saved.a[j]) && near(s.g[j],saved.g[j]));
    assert(near(s.gravity[j],saved.gravity[j]) && near(s.bias[j],saved.bias[j]));
  }
}
static void validationAndSerialization() {
  cleanRuntime();auto owner=std::make_unique<StickS3MotionAcquisition>();
  assert(owner->mode_==Mode::Top && owner->frame_==0);
  assert(!owner->setMount(Mode(2)) && !owner->setMount(Mode(255)));
  assert(owner->frame_==0 && owner->orientation_.resets()==0);
  // A saved Sidecar setting is applied before the task/mutex exists at boot.
  assert(owner->setMount(Mode::Sidecar));assert(owner->snapshot_.mount==Mode::Sidecar);
  assert(owner->frame_==1 && owner->orientation_.resets()==1);
  assert(owner->setMount(Mode::Sidecar) && owner->frame_==1);
  assert(owner->begin() && owner->running() && M5.Imu.calibrations==1);
  M5.Imu.data.accel={1,0,0};runFor(*owner,400);
  assert(owner->snapshot_.orientationReady && owner->ring_.size()>150);
  const auto saved=owner->snapshot_;const auto frame=owner->frame_,resets=owner->orientation_.resets();
  const unsigned size=owner->ring_.size();const uint64_t latest=owner->ring_.latest();
  assert(owner->setMount(Mode::Sidecar));sameState(*owner,saved,size,latest,frame,resets);
  const unsigned attempts=owner->ringGate_->attempts;
  assert(!owner->setMount(Mode(2)) && owner->ringGate_->attempts==attempts);
  sameState(*owner,saved,size,latest,frame,resets);
  // The owner gate excludes a genuine concurrent mode change. No optimistic
  // mutation is permitted when the 20 ms lock attempt fails.
  assert(xSemaphoreTake(owner->ringGate_,20)==pdTRUE);
  bool changed=true;std::thread contender([&]{changed=owner->setMount(Mode::Top);});contender.join();
  assert(!changed);sameState(*owner,saved,size,latest,frame,resets);
  xSemaphoreGive(owner->ringGate_);
  assert(owner->setMount(Mode::Top) && owner->frame_==frame+1);
  assert(owner->ring_.size()==0 && owner->ring_.latest()==0);
  const auto cleared=owner->snapshot();
  assert(cleared.mount==Mode::Top && !cleared.fusedUs && !cleared.pairedUs && !cleared.orientationReady);
  LaunchMotion::Trace trace;assert(owner->capture(latest-100000,latest-50000,6000,1,trace));
  assert(trace.quality==LaunchMotion::Quality::Missing);
}
static void continuousOwnerWarmup() {
  cleanRuntime();auto owner=std::make_unique<StickS3MotionAcquisition>();assert(owner->begin());
  M5.Imu.data.accel={.25f,.1f,.963068f};M5.Imu.data.gyro={.2f,-.1f,.15f};
  uint64_t firstNew=0;uint32_t oldResets=0;bool sawWarming=false,sawReady=false,sawWarmCapture=false;
  // Polls in time: switch at 4 s, then run 2.1 s, past the 1.5 s warmup.
  constexpr unsigned POLL=StickS3MotionAcquisition::POLL_MS,SWITCH=4000/POLL,FRAMES=POLL/5;
  runFor(*owner,SWITCH+2100/POLL+1,[&](unsigned iteration) {
    if(iteration==SWITCH) {
      assert(owner->snapshot_.orientationReady && std::fabs(owner->snapshot_.gravity[0])>.1f);
      oldResets=owner->orientation_.resets();assert(owner->setMount(Mode::Sidecar));
      assert(!owner->snapshot().fusedUs && !owner->ring_.size());
      // The new values begin with an accelerometer frame (it comes every second gyro frame).
      M5.Imu.data.accel={1,0,0};M5.Imu.data.gyro={0,0,0};FakeFifo::sensorFrames=0;
    }
    if(iteration<=SWITCH)return;
    const auto s=owner->snapshot();assert(s.mount==Mode::Sidecar);
    // Diagnostics remain native while stored samples/fusion use Top axes.
    vectorIs(s.a,1,0,0);vectorIs(s.g,0,0,0);
    if(iteration==SWITCH+1) {
      // The first read after the switch holds one poll's frames; the oldest starts the warmup.
      // Fusion emits the first frame, then one sample per 10 ms grid step.
      assert(owner->ring_.size()==1+(FRAMES-1)*5000/LauncherOrientation::STEP_US);firstNew=owner->ring_.at(0).us;
      assert(Fake::now-firstNew==(FRAMES-1)*5000);
      const auto &sample=owner->ring_.at(0);vectorIs(sample.a,0,0,1);vectorIs(sample.g,0,0,0);
      assert(!sample.settled && !s.orientationReady && s.fusionResets==oldResets+1);
      LauncherOrientation fresh;LaunchMotion::Sample raw;raw.us=firstNew;raw.a[2]=1;
      fresh.feed(raw,[&](const LaunchMotion::Sample &expected) {
        assert(LaunchMotion::Quaternion::difference(sample.q,expected.q)<.001f);
        for(unsigned j=0;j<3;++j)assert(near(sample.g[j],expected.g[j]));
      });
      float freshBias[3];const float sigma=fresh.biasDegrees(freshBias);
      assert(near(s.biasSigma,sigma));for(unsigned j=0;j<3;++j)assert(near(s.bias[j],freshBias[j]));
    }
    assert(owner->ring_.at(0).us==firstNew);
    const uint64_t age=Fake::now-firstNew;
    assert(s.orientationReady==(age>=1500000));
    if(!s.orientationReady)sawWarming=true;else sawReady=true;
    if(age>=400000 && !sawWarmCapture) {
      sawWarmCapture=true;LaunchMotion::Trace trace;assert(owner->capture(firstNew+100000,firstNew+200000,6000,2,trace));
      assert(trace.quality==LaunchMotion::Quality::Warming);
    }
  });
  assert(sawWarming && sawReady && sawWarmCapture);
  const auto s=owner->snapshot();vectorIs(s.gravity,0,0,1);
  LaunchMotion::Trace trace;assert(owner->capture(firstNew+1700000,firstNew+1850000,6000,3,trace));
  assert(trace.valid() && trace.number==3 && trace.fused);
  assert(trace.startLevel.valid() && trace.endLevel.valid() && trace.startLevel.degrees<.001f);
}
static void currentModeAtFusion() {
  cleanRuntime();auto owner=std::make_unique<StickS3MotionAcquisition>();assert(owner->begin());
  M5.Imu.data.accel={1,2,3};M5.Imu.data.gyro={4.8828125f,6.103515625f,7.32421875f};bool changed=false;
  // IMU acquisition has finished, but fusion has not yet taken the frame gate.
  Fake::released=[&](FakeMutex *gate) {
    if(gate==owner->gate_ && !changed){changed=true;assert(owner->setMount(Mode::Sidecar));}
  };
  runFor(*owner,1);assert(changed && owner->ring_.size()==1);
  vectorIs(owner->ring_.at(0).a,-3,2,1);
  const auto s=owner->snapshot();assert(s.mount==Mode::Sidecar && s.fusedUs==1000000);
  // Ring rates are canonical AND bias-corrected by the actual VQF owner.
  vectorIs(owner->ring_.at(0).g,-7.32421875f-s.bias[0],6.103515625f-s.bias[1],4.8828125f-s.bias[2]);
  vectorIs(s.a,1,2,3);vectorIs(s.g,4.8828125f,6.103515625f,7.32421875f);
}
static void stalePublication(bool returnToTop) {
  cleanRuntime();auto owner=std::make_unique<StickS3MotionAcquisition>();assert(owner->begin());
  M5.Imu.data.accel={1,0,0};bool changed=false;const Mode target=returnToTop?Mode::Top:Mode::Sidecar;
  Fake::released=[&](FakeMutex *gate) {
    if(gate==owner->ringGate_ && !changed) {
      changed=true;assert(owner->setMount(Mode::Sidecar));
      if(returnToTop)assert(owner->setMount(Mode::Top));
    }
  };
  runFor(*owner,4,[&](unsigned iteration) {
    if(iteration<=3) {
      const auto s=owner->snapshot();assert(s.mount==target && !s.fusedUs && !s.pairedUs);
      assert(!s.orientationReady && !owner->ring_.size());
      // An empty read, then an accel-only frame, cannot republish old pose.
      M5.Imu.fresh=iteration==1?0:iteration==2?1:3;
      if(returnToTop)M5.Imu.data.accel={0,0,1};
    }
  });
  assert(changed && owner->frame_==(returnToTop?2u:1u));
  // Frames are stamped on the sensor timeline, which follows read time gradually.
  // The fourth poll brings one poll's gyro frames: the first fuses at once, then one per grid step.
  constexpr uint64_t POLL_US=StickS3MotionAcquisition::POLL_MS*1000;constexpr unsigned FRAMES=POLL_US/5000;
  const auto s=owner->snapshot();assert(s.mount==target && s.fusedUs>1000000 && s.fusedUs<=1000000+3*POLL_US && !s.orientationReady);
  assert(owner->ring_.size()==1+(FRAMES-1)*5000/LauncherOrientation::STEP_US && owner->ring_.latest()==s.fusedUs);
  for(unsigned i=0;i<owner->ring_.size();++i)vectorIs(owner->ring_.at(i).a,0,0,1);
}
static void flashQuiet() {
  // No FIFO transfer starts inside a flash-write window; reads resume at its
  // end or, if no end arrives, after the bounded maximum.
  cleanRuntime();auto owner=std::make_unique<StickS3MotionAcquisition>();assert(owner->begin());
  runFor(*owner,20);const unsigned before=M5.Imu.updates,samples=owner->ring_.size();
  // Each runFor re-enters run(), whose diagnostics counters are per run.
  owner->quiet();runFor(*owner,10);
  assert(M5.Imu.updates==before && owner->snapshot_.quietPolls==10 && owner->ring_.size()==samples);
  owner->endQuiet();runFor(*owner,3);assert(M5.Imu.updates==before+3 && owner->ring_.size()>samples);
  // Window lengths in polls: 40 ms, and the 10 s cap on a window that never ends.
  constexpr unsigned POLL=StickS3MotionAcquisition::POLL_MS,SHORT=40/POLL,CAP=StickS3MotionAcquisition::QUIET_MAX_MS/POLL;
  owner->quiet(40);runFor(*owner,20);
  assert(owner->snapshot_.quietPolls==SHORT && M5.Imu.updates==before+3+(20-SHORT));
  owner->quiet(60000);runFor(*owner,CAP+20);
  assert(owner->snapshot_.quietPolls==CAP && M5.Imu.updates==before+3+(20-SHORT)+20);
  // A (nearly) full FIFO may begin inside a payload: flushed, never parsed.
  const unsigned flushes=FakeFifo::flushes,size=owner->ring_.size();
  FakeFifo::forcedLength=Bmi270Fifo::CAPACITY-8;runFor(*owner,1);FakeFifo::forcedLength=0;
  assert(FakeFifo::flushes==flushes+1 && owner->snapshot_.fifoOverflows==1 && owner->snapshot_.fifoErrors==0);
  assert(owner->ring_.size()==size);
  // A transfer stalled 8 ms past its bus time is suspect even though it
  // reported success. The read after the flush holds one poll's frames.
  const unsigned bytes=Bmi270Fifo::fillBytes(POLL);
  FakeFifo::stallUs=Bmi270Fifo::transferBudgetUs(bytes)+1;runFor(*owner,1);FakeFifo::stallUs=0;
  assert(FakeFifo::flushes==flushes+2 && owner->snapshot_.fifoStalls==1 && owner->snapshot_.lastStallUs==Bmi270Fifo::transferBudgetUs(bytes)+1);
  assert(owner->ring_.size()==size && owner->snapshot_.fifoErrors==0);
  FakeFifo::stallUs=Bmi270Fifo::transferBudgetUs(bytes)-1;runFor(*owner,1);FakeFifo::stallUs=0;
  assert(owner->snapshot_.fifoStalls==0 && owner->ring_.size()>size);
}
static void quietBarrierAndAnchoring() {
  // A read in flight longer than lock()'s 20 ms is waited out, not raced.
  cleanRuntime();auto owner=std::make_unique<StickS3MotionAcquisition>();assert(owner->begin());
  owner->gate_->heldByOtherMs=90;
  const uint64_t waitStart=Fake::now;
  assert(owner->quiet(300));
  assert(Fake::now-waitStart==90000 && owner->gate_->heldByOtherMs==0);
  // The window starts after the wait, so a short one cannot expire during it.
  assert(owner->quietUntil_.load()==int64_t(Fake::now)+300000 && owner->quietBeganMs()==uint32_t(Fake::now/1000));
  owner->endQuiet();
  // A gate held past the 250 ms budget vetoes: false, and no window or marker changes.
  const uint32_t began=owner->quietBeganMs();Fake::now+=5000000;
  owner->gate_->heldByOtherMs=400;
  assert(!owner->quiet());assert(owner->quietUntil_.load()==0 && owner->quietBeganMs()==began);
  owner->gate_->heldByOtherMs=0;
  // The budget itself: a hold of exactly 250 ms is waited out, 251 ms vetoes.
  owner->gate_->heldByOtherMs=251;assert(!owner->quiet());owner->gate_->heldByOtherMs=0;
  owner->gate_->heldByOtherMs=250;assert(owner->quiet());owner->endQuiet();
  // Frames are stamped when the FIFO length is read, not after a long transfer.
  cleanRuntime();owner=std::make_unique<StickS3MotionAcquisition>();assert(owner->begin());
  FakeFifo::stallUs=9000;runFor(*owner,1);FakeFifo::stallUs=0;
  assert(owner->snapshot_.fifoStalls==0 && owner->ring_.size()==1 && owner->ring_.at(0).us==1000000);
}
// Runs the owner in the realistic scheduler until the virtual clock reaches
// end. step sees every loop iteration as it reaches its period wait.
static void runUntil(StickS3MotionAcquisition &owner,uint64_t end,std::function<void()> step) {
  Fake::completed=0;Fake::limit=0;
  Fake::delayed=[&](unsigned){step();if(Fake::now>=end)throw Fake::Stop{};};
  try{owner.run();assert(false);}catch(const Fake::Stop &){}
  Fake::delayed={};
}
static void lateLoopYields() {
  // Held up for a minute (here: another task holds both IMU gates, so every
  // poll waits 20 ms for each and the schedule falls far behind), the loop
  // must not then run back-to-back through every missed period. The FIFO
  // already holds what those polls would have read, and on device that spin
  // kept IDLE0 off core 0 until the task watchdog reset the chip.
  constexpr uint64_t POLL_US=StickS3MotionAcquisition::POLL_MS*1000;
  cleanRuntime();Fake::realistic=true;Fake::transferUs=400;
  auto owner=std::make_unique<StickS3MotionAcquisition>();assert(owner->begin());
  M5.Imu.data.accel={0,0,1};
  const uint64_t hold=Fake::now+2000000,release=hold+60000000;
  unsigned before=0,polls=0,steady=0;uint64_t previous=0;
  runUntil(*owner,release+30000000,[&] {
    if(Fake::now<hold)return;
    if(!before) {
      // Settled and on schedule before the hold.
      assert(owner->snapshot_.orientationReady && Fake::longestBusyUs<POLL_US);
      assert(owner->snapshot_.latePolls==0 && owner->snapshot_.missedPolls==0);
      before=owner->ring_.size();owner->gate_->heldByOtherMs=owner->ringGate_->heldByOtherMs=60000;return;
    }
    if(owner->gate_->heldByOtherMs && Fake::now>=release)owner->gate_->heldByOtherMs=owner->ringGate_->heldByOtherMs=0;
    if(Fake::now<release)return;
    // Back on schedule at once after the release.
    if(previous && Fake::now-previous==POLL_US)++steady;
    previous=Fake::now;++polls;
  });
  // Every poll blocked: no busy stretch longer than one poll's own transfers.
  assert(Fake::longestBusyUs<=2*Fake::transferUs);
  const auto &s=owner->snapshot_;
  assert(s.gateMisses>1000 && s.latePolls>=s.gateMisses && s.missedPolls>=s.gateMisses);
  const unsigned expected=30000000/POLL_US;
  assert(polls+5>=expected && polls<=expected+2 && steady+3>=polls);
  // Fusion and the ring carried on from the FIFO; the hold is one honest gap.
  assert(before && s.gaps==1 && s.maxGapUs>60000000 && s.orientationReady);
  assert(owner->ring_.latest()+POLL_US>=Fake::now);
  // A loop whose every poll costs more than its period (core 0 overloaded)
  // still blocks once per poll.
  cleanRuntime();Fake::realistic=true;Fake::transferUs=POLL_US*3/4;
  owner=std::make_unique<StickS3MotionAcquisition>();assert(owner->begin());
  runUntil(*owner,Fake::now+10000000,[]{});
  assert(Fake::longestBusyUs<=2*Fake::transferUs && owner->snapshot_.latePolls>200);
}
static void pollCost() {
  // Each FIFO transaction busy-waits about 0.4 ms on the M5 I2C driver
  // whatever its length, and the stack high-water mark walks the whole free
  // stack (about 1 ms). At most 50 polls a second (two transactions each)
  // and one stack walk a second, while every 200 Hz frame still arrives.
  cleanRuntime();auto owner=std::make_unique<StickS3MotionAcquisition>();assert(owner->begin());
  M5.Imu.data.accel={0,0,1};
  Fake::stackScans=0;runFor(*owner,1);assert(Fake::stackScans==1 && owner->snapshot_.stackFree==4096);
  Fake::stackScans=0;FakeFifo::lengthReads=0;
  const uint64_t start=Fake::now;
  unsigned polls=0;
  Fake::completed=0;Fake::limit=0;
  Fake::delayed=[&](unsigned){++polls;if(Fake::now-start>=10000000)throw Fake::Stop{};};
  try{owner->run();assert(false);}catch(const Fake::Stop &){}
  Fake::delayed={};
  const uint64_t seconds=(Fake::now-start)/1000000;
  assert(seconds>=10 && FakeFifo::lengthReads<=50*seconds+1 && polls<=50*seconds+1);
  // run() was re-entered: one walk on its first poll, then one a second.
  assert(Fake::stackScans>=seconds && Fake::stackScans<=seconds+2);
  // Every frame arrived (run() counts from its own start): 200 Hz gyro, each
  // paired with an accelerometer reading.
  const unsigned frames=owner->snapshot_.pairedSamples;
  assert(frames+4>=200*seconds && frames<=200*seconds+4 && owner->snapshot_.gaps==0);
}
static void fifoHeadroom() {
  // A poll right after the longest normal stop of reads (a flash-write quiet
  // window of STALL_BUDGET_MS) finds the FIFO short of overflow: nothing is
  // lost and fusion sees no gap. A stop past the FIFO's second overflows it,
  // which is reported as one honest gap.
  using Owner=StickS3MotionAcquisition;
  constexpr uint64_t POLL_US=Owner::POLL_MS*1000;
  cleanRuntime();auto owner=std::make_unique<Owner>();assert(owner->begin());
  M5.Imu.data.accel={0,0,1};
  const uint64_t start=Fake::now,quietAt=start+1000000,longAt=quietAt+3000000,end=longAt+3000000;
  unsigned quiets=0;size_t largest=0;
  Fake::completed=0;Fake::limit=0;
  Fake::delayed=[&](unsigned) {
    largest=std::max(largest,size_t(owner->snapshot_.fifoMaxBytes));
    if(!quiets && Fake::now>=quietAt){assert(owner->quiet(Owner::STALL_BUDGET_MS));++quiets;}
    else if(quiets==1 && Fake::now>=longAt) {
      // Everything so far arrived: every 200 Hz gyro frame paired, no gap.
      const auto &s=owner->snapshot_;
      assert(s.fifoOverflows==0 && s.gaps==0 && s.maxGapUs<=2*Bmi270Fifo::GYRO_PERIOD_US);
      assert(s.pairedSamples+4>=(Fake::now-start)/Bmi270Fifo::GYRO_PERIOD_US);
      assert(largest>=Bmi270Fifo::fillBytes(Owner::STALL_BUDGET_MS) && largest<Bmi270Fifo::CAPACITY-Bmi270Fifo::FULL_MARGIN);
      assert(owner->quiet(Owner::QUIET_MAX_MS));++quiets;
    } else if(quiets==2 && Fake::now>=longAt+1100000){owner->endQuiet();++quiets;}
    if(Fake::now>=end)throw Fake::Stop{};
  };
  try{owner->run();assert(false);}catch(const Fake::Stop &){}
  Fake::delayed={};
  const auto &s=owner->snapshot_;
  assert(quiets==3 && s.fifoOverflows==1 && s.gaps==1 && s.orientationReady);
  assert(owner->ring_.latest()+POLL_US>=Fake::now);
}
static StickS3MotionAcquisition imuAcquisition;
static StickS3Level level;
static Mode liveMount=Mode::Top;
static uint64_t lastLevelSampleUs=0;
static uint64_t lastActivityMotionUs=0;
static HistoryCheckpoint::Stillness imuStill;
#define STILL_QUIET(now) HistoryCheckpoint::automaticQuietMs(now,imuStill,0,10000)
static InactivityTimer inactivity;
static uint32_t lastImuPoll=0,maxImuPollUs=0;
static uint32_t millis(){return uint32_t(Fake::now/1000);}
'''

LEVEL_TESTS = r'''
static void ownerActivity() {
  cleanRuntime();auto owner=std::make_unique<StickS3MotionAcquisition>();assert(owner->begin());
  M5.Imu.data.accel={0,0,1};M5.Imu.data.gyro={0,0,0};runFor(*owner,300);
  assert(owner->snapshot().activity.valid && !owner->snapshot().activity.moving);
  M5.Imu.data.gyro={0,0,12};runFor(*owner,200);
  assert(owner->snapshot().activity.moving && owner->snapshot().activity.lastMotionUs);
  M5.Imu.data.gyro={0,0,0};runFor(*owner,600);
  assert(!owner->snapshot().activity.moving);
  owner->setMount(Mode::Sidecar);assert(!owner->snapshot().activity.valid);
}
static void fallbackLevel() {
  cleanRuntime();M5.Imu.data.accel={.98f,.1f,-.2f};level={};lastImuPoll=0;
  liveMount=Mode::Top;pollLevel(millis());vectorIs(level.lastAccel(),.98f,.1f,-.2f);
  level={};liveMount=Mode::Sidecar;Fake::now+=20000;pollLevel(millis());
  vectorIs(level.lastAccel(),.2f,.1f,.98f);assert(level.reading(millis()).valid());
  unsigned count=level.samples(),updates=M5.Imu.updates;
  pollLevel(millis());assert(level.samples()==count && M5.Imu.updates==updates);
  M5.Imu.fresh=0;Fake::now+=20000;pollLevel(millis());assert(level.samples()==count);
  M5.Imu.enabled=false;M5.Imu.fresh=3;Fake::now+=20000;pollLevel(millis());assert(level.samples()==count);
  M5.Imu.enabled=true;assert(imuAcquisition.begin());level={};lastLevelSampleUs=0;Fake::now+=20000;
  auto &s=imuAcquisition.snapshot_;s.fusedUs=Fake::now;s.gravity[0]=.2f;s.gravity[2]=.98f;
  s.orientationReady=false;pollLevel(millis());assert(level.samples()==1 && !level.reading(millis()).valid());
  pollLevel(millis());assert(level.samples()==1);
  Fake::now+=10000;s.fusedUs=Fake::now;s.orientationReady=true;pollLevel(millis());
  assert(level.samples()==1); // UI snapshot copies are capped at 50 Hz.
  Fake::now+=10000;s.fusedUs=Fake::now;pollLevel(millis());
  vectorIs(level.lastAccel(),.2f,0,.98f);assert(level.samples()==2 && level.reading(millis()).valid());
  // Owner gravity is already canonical. It must not be Sidecar-mapped twice.
  assert(level.reading(millis()).right>0 && level.reading(millis()).degrees<15);
  // Execute the actual UI consumer. Motion extends probation without changing
  // its 30-second limit; cached/stale snapshots cannot perpetually keep awake.
  inactivity.begin(millis(),true);lastActivityMotionUs=0;
  for(unsigned i=0;i<1800;++i) {
    Fake::now+=20000;s.activity.sampleUs=s.activity.lastMotionUs=Fake::now;
    s.activity.valid=s.activity.moving=true;pollLevel(millis());
    assert(inactivity.unusedShake() && inactivity.remaining(millis())==30000);
  }
  const auto lastUsed=lastActivityMotionUs;Fake::now+=1000000;pollLevel(millis());
  assert(lastActivityMotionUs==lastUsed && inactivity.remaining(millis())==29000);
  // A current snapshot with an old movement marker cannot renew the timer.
  Fake::now+=1000000;s.activity.sampleUs=Fake::now;s.activity.lastMotionUs=lastUsed+1;
  pollLevel(millis());assert(lastActivityMotionUs==lastUsed && inactivity.remaining(millis())==28000);
  Fake::now+=28000000;pollLevel(millis());assert(inactivity.due(millis()));
  // Repeated fusion warmup hides the dot but is not activity. Execute the
  // production consumer for an unused shake wake, with fresh quiet samples
  // and periodic 1.5-second orientation resets, through its sleep deadline.
  inactivity.begin(millis(),true);s.activity={};s.activity.valid=true;
  const auto quietStart=Fake::now;
  for(unsigned i=1;i<=1600;++i) {
    Fake::now+=20000;s.fusedUs=s.activity.sampleUs=Fake::now;
    s.orientationReady=(i%500)>=75;
    pollLevel(millis());
    assert(!s.activity.moving && inactivity.elapsed(millis())==i*20);
    assert(level.reading(millis()).valid()==s.orientationReady);
  }
  assert(Fake::now-quietStart==32000000 && inactivity.unusedShake() && inactivity.due(millis()));
  // Automatic checkpoints keep the short quiet only after 2 s of IMU
  // stillness, measured from the poll that first saw it (never ahead of now).
  s.activity={};s.activity.valid=true;s.activity.moving=true;
  Fake::now=(Fake::now/2000+1)*2000+20000;s.activity.sampleUs=Fake::now;pollLevel(millis());
  for(unsigned i=0;i<3;++i) {
    Fake::now+=20000+i*1000;s.activity.sampleUs=Fake::now;s.activity.moving=false;pollLevel(millis());
    assert(STILL_QUIET(millis())==60000);s.activity.moving=true;Fake::now+=20000;s.activity.sampleUs=Fake::now;pollLevel(millis());
  }
  Fake::now+=20000;s.activity.sampleUs=Fake::now;s.activity.moving=false;pollLevel(millis());
  assert(STILL_QUIET(millis())==60000);
  for(unsigned i=0;i<99;++i){Fake::now+=20000;s.activity.sampleUs=Fake::now;pollLevel(millis());}
  assert(STILL_QUIET(millis())==60000);
  Fake::now+=20000;s.activity.sampleUs=Fake::now;pollLevel(millis());assert(STILL_QUIET(millis())==10000);
  // A stale snapshot is not stillness.
  Fake::now+=MotionActivity::FRESH_US+20000;pollLevel(millis());assert(STILL_QUIET(millis())==60000);
}
static void activityAndStillnessTiming() {
  // Wake activity and the 2 s stillness gate come from MotionActivity, fed
  // every fused sample on sensor time. A reference fed the same 5 ms frames
  // directly gives the transitions; through the real owner (one read per
  // POLL_MS) and the real UI poll each must show no earlier and at most one
  // IMU poll, one fusion step and one UI poll later.
  using Owner=StickS3MotionAcquisition;
  constexpr uint64_t LATE_US=Owner::POLL_MS*1000+LauncherOrientation::STEP_US+IdlePowerPolicy::LEVEL_POLL_MS*1000;
  constexpr uint64_t FRAME_US=Bmi270Fifo::GYRO_PERIOD_US;
  cleanRuntime();level={};lastImuPoll=0;lastLevelSampleUs=0;lastActivityMotionUs=0;imuStill={};
  const float rest[3]={0,0,0},turning[3]={0,0,12},gravity[3]={0,0,1};
  M5.Imu.data.accel={0,0,1};M5.Imu.data.gyro={0,0,0};
  // The sketch's own owner: pollLevel reads it.
  Owner &owner=imuAcquisition;assert(owner.begin());
  const uint64_t start=Fake::now,moveAt=start+3000000,stopAt=moveAt+1000000,end=stopAt+4000000;
  // Reference: the owner's fusion and activity path on ideal sensor time,
  // with the FIFO's quantised values.
  LauncherOrientation refFusion;MotionActivity refActivity;
  uint64_t refMoving=0,refReleased=0;
  auto quantised=[](float v,float scale){return float(std::lround(v/scale))*scale;};
  // The owner's first read holds one frame, stamped at its read time: the grid starts there.
  for(uint64_t us=start;us<=end;us+=FRAME_US) {
    const float *g=us>moveAt && us<=stopAt?turning:rest;
    LaunchMotion::Sample raw;raw.us=us;
    for(unsigned j=0;j<3;++j){raw.a[j]=quantised(gravity[j],accelRange().fullScaleG()/32768);raw.g[j]=quantised(g[j],StickS3AccelRange::GYRO_FULL_SCALE_DPS/32768);}
    LaunchMotion::Sample mapped=raw;MountOrientation::mapVector(Mode::Top,raw.a,mapped.a);MountOrientation::mapVector(Mode::Top,raw.g,mapped.g);
    refFusion.feed(mapped,[&](const LaunchMotion::Sample &f) {
      const bool was=refActivity.state().moving;refActivity.feed(f.us,f.a,f.g,!f.clipped);
      if(!was && refActivity.state().moving && !refMoving)refMoving=f.us;
      if(was && !refActivity.state().moving && refMoving && !refReleased)refReleased=f.us;
    });
  }
  assert(refMoving>moveAt+MotionActivity::CONFIRM_US && refMoving<moveAt+MotionActivity::CONFIRM_US+100000);
  assert(refReleased>stopAt+MotionActivity::RELEASE_US && refReleased<stopAt+MotionActivity::RELEASE_US+500000);
  uint64_t seenMoving=0,seenReleased=0,gateClosed=0,gateOpened=0;
  Fake::completed=0;Fake::limit=0;
  Fake::delayed=[&](unsigned) {
    // Frames read at the next poll were produced from now on.
    if(Fake::now>=moveAt && Fake::now<stopAt)M5.Imu.data.gyro={0,0,12};else M5.Imu.data.gyro={0,0,0};
    pollLevel(millis());
    const auto a=owner.snapshot().activity;
    if(a.moving && !seenMoving)seenMoving=Fake::now;
    if(seenMoving && !a.moving && !seenReleased)seenReleased=Fake::now;
    const bool open=STILL_QUIET(millis())==10000;
    if(seenMoving && !open && !gateClosed)gateClosed=Fake::now;
    if(seenReleased && open && !gateOpened)gateOpened=Fake::now;
    if(Fake::now>=end)throw Fake::Stop{};
  };
  try{owner.run();assert(false);}catch(const Fake::Stop &){}
  Fake::delayed={};
  assert(seenMoving>=refMoving && seenMoving<=refMoving+LATE_US);
  assert(seenReleased>=refReleased && seenReleased<=refReleased+LATE_US);
  // The gate closes with the motion, and opens STILL_MS after the poll that first saw stillness.
  assert(gateClosed && gateClosed<=seenMoving+IdlePowerPolicy::LEVEL_POLL_MS*1000);
  const uint64_t gateDue=refReleased+HistoryCheckpoint::STILL_MS*1000ull;
  assert(gateOpened>=gateDue && gateOpened<=gateDue+LATE_US);
}
int main() {
  ownerActivity();
  validationAndSerialization();continuousOwnerWarmup();currentModeAtFusion();
  stalePublication(false);stalePublication(true);flashQuiet();quietBarrierAndAnchoring();lateLoopYields();pollCost();fifoHeadroom();fallbackLevel();activityAndStillnessTiming();
  std::cout<<"PASS: actual mount owner; mutex exclusion, invalid/failure/noop preservation, "
              "frame invalidation, fresh fusion warmup, canonical capture, late/current mapping, "
              "stale/ABA publication guards, bounded flash-write quiet window, 250 ms quiet barrier/veto, length-time FIFO anchoring, late polls yield instead of starving IDLE0, at most 50 polls and one stack walk a second with every frame delivered, no FIFO overflow at a poll after the longest normal stall, activity and the 2 s stillness gate on sensor time at most one poll late, actual level fallback and dot dropouts without timer renewal\n";
}
'''


def main():
    owner = (HEADERS / "motion_acquisition.h").read_text()
    if owner.count("\nprivate:") != 1:
        raise RuntimeError("Motion owner access boundary changed; review fixture extraction")
    owner = owner.replace("\nprivate:", "\npublic:", 1)
    poll = function((HEADERS / "LaunchLabMini.ino").read_text(), "pollLevel")
    with tempfile.TemporaryDirectory(prefix="launchlab-mount-owner-") as temp:
        build = Path(temp)
        (build / "freertos").mkdir()
        (build / "fixture_runtime.h").write_text(RUNTIME)
        for name in ("M5Unified.h", "freertos/semphr.h", "esp_timer.h"):
            (build / name).write_text('#pragma once\n#include "fixture_runtime.h"\n')
        (build / "motion_owner_test.h").write_text(owner)
        cpp = build / "test.cpp"
        cpp.write_text('#include "power_status.h"\n#include "history_checkpoint.h"\n' + TESTS + "\n" + poll + "\n" + LEVEL_TESTS)
        executable = build / "test"
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g", "-pthread",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        "-I" + str(build), "-I" + str(HEADERS), "-I" + str(HEADERS.parent / "LaunchLabRpm"), str(cpp),
                        str(HEADERS / "src/vqf/vqf.cpp"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
