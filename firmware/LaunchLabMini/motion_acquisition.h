#pragma once
#include <M5Unified.h>
#include <atomic>
#include <memory>
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "accel_range.h"
#include "bmi270_fifo.h"
#include "launch_motion.h"
#include "orientation_estimator.h"
#include "mount_orientation.h"
#include "motion_activity.h"
#include "cpu_power.h"

// One IMU owner on core 0, below the optical ADC owner's priority. UI calls
// involving M5's shared I2C use the same gate. Samples come from the BMI270
// FIFO, so USB/UI, I2C and flash/cache stalls shorter than the FIFO's ~1 s
// only delay data. Longer stalls overflow it and still count as gaps.
class StickS3MotionAcquisition {
public:
  struct Snapshot {
    float a[3]={},g[3]={},gravity[3]={},bias[3]={},biasSigma=0;uint64_t accelUs=0,pairedUs=0,fusedUs=0;
    uint32_t accelSamples=0,pairedSamples=0,maxGapUs=0,maxPollUs=0,stackFree=0,gateMisses=0,gaps=0;
    // Polls that began a period or more late, and the whole periods skipped.
    uint32_t latePolls=0,missedPolls=0;
    uint32_t fusionSamples=0,fusionResets=0,maxFusionUs=0;
    uint32_t fifoReads=0,fifoErrors=0,fifoSkipped=0,fifoAnchors=0,fifoMaxBytes=0,quietPolls=0;float fifoPeriodUs=0;
    // Error causes: length read, data read (chunk offset), parse, oversize length.
    uint32_t fifoLengthErrors=0,fifoDataErrors=0,fifoParseErrors=0,fifoOversize=0,lastErrorBytes=0,lastErrorOffset=0,lastErrorMs=0;
    uint32_t fifoOverflows=0; // Full FIFO discarded: data already lost, never parsed.
    // A transfer far slower than its bus time was stalled mid-transfer
    // (preemption or flash). Its bytes are suspect even when the read succeeds.
    uint32_t fifoStalls=0,lastStallUs=0,lastStallMs=0;
    // Where the time went between the two samples around each >35 ms gap:
    // loop period, IMU gate wait, FIFO read, ring gate/fusion, poll counts.
    struct Gap {uint32_t atMs=0,gapUs=0,polls=0,emptyPolls=0,periodUs=0,lockUs=0,pollUs=0,ringUs=0;};
    static constexpr unsigned GAPS=6;Gap gapLog[GAPS];uint32_t gapNext=0;
    // Raw samples at or beyond full scale; axis bits 0-2 accel x/y/z, 3-5 gyro x/y/z.
    uint32_t clipSamples=0,clipRun=0,maxClipRun=0,lastClipRun=0,lastClipMs=0,lastClipAxes=0;
    bool orientationReady=false,rest=false;
    MountOrientation::Mode mount=MountOrientation::Mode::Top;
    MotionActivity::State activity;
  };
  bool begin() {
    if(task_)return true;
    if(!M5.Imu.isEnabled())return false;
    // A failed probe or task allocation can be retried without leaking gates
    // or creating a second owner of the same IMU and fusion state.
    if(!gate_)gate_=xSemaphoreCreateMutex();
    if(!ringGate_)ringGate_=xSemaphoreCreateMutex();
    if(!gate_ || !ringGate_)return false;
    M5.Imu.setCalibration(0,0,0);
    TaskHandle_t created=nullptr;
    if(xTaskCreatePinnedToCore(entry,"launch_imu",8192,this,2,&created,0)!=pdPASS)return false;
    task_=created;return true;
  }
  // Flash erase/write stalls both cores for tens of ms. An I2C transfer caught
  // in that stall returns corrupted FIFO bytes (seen on device as parse errors
  // on reads that reported success), forcing a flush and a real gap. For the
  // whole flash-write checkpoint, start no FIFO reads, after waiting out any
  // read in flight. The FIFO buffers about 1 s; a longer write overflows it,
  // which is reported and becomes an honest gap. The cap only guards against
  // a missing checkpoint end, which would also leave the optical ADC stopped.
  static constexpr uint32_t QUIET_MAX_MS=10000;
  // A catch-up FIFO read can hold the gate far longer than lock()'s 20 ms
  // (about 90 ms near capacity). Wait well beyond that, but if quiescence
  // cannot be established return false so the flash checkpoint is vetoed and
  // deferred; never write flash with a transfer possibly in flight.
  static constexpr uint32_t QUIET_WAIT_MS=250;
  // One poll per 20 ms reads the FIFO's four or so frames in two I2C
  // transactions. Each transaction costs about 0.4 ms of busy-waiting
  // whatever its length, so polling every 5 ms spent most of the IMU task's
  // core-0 time on bus overhead. Frame times come from the sensor clock
  // (Timeline), and a capture waits for fused samples past its end, so a
  // longer poll delays data by at most one period and loses none.
  static constexpr uint32_t POLL_MS=20,STACK_POLLS=1000/POLL_MS;
  // The longest normal stop of FIFO reads is a flash-write checkpoint's quiet
  // window. On device (0.18) a history log append takes about 16 ms and an
  // NVS write 110-160 ms; this budget leaves room over the slowest. A poll
  // right after it reads POLL_MS+STALL_BUDGET_MS of frames, about half the
  // FIFO. A stop over about a second overflows it and is reported as a gap.
  static constexpr uint32_t STALL_BUDGET_MS=500;
  static_assert(Bmi270Fifo::fillBytes(POLL_MS+STALL_BUDGET_MS)<Bmi270Fifo::CAPACITY-Bmi270Fifo::FULL_MARGIN,
    "A poll after the longest normal stall must find the FIFO short of overflow");
  bool quiet(uint32_t ms=QUIET_MAX_MS) {
    if(gate_ && xSemaphoreTake(gate_,pdMS_TO_TICKS(QUIET_WAIT_MS))!=pdTRUE)return false;
    // Publish the window while holding the gate: the IMU task checks it under
    // the same gate, and a short window cannot expire during the wait.
    const int64_t now=esp_timer_get_time();
    quietBeganMs_=uint32_t(now/1000);
    quietUntil_=now+int64_t(std::min(ms,QUIET_MAX_MS))*1000;
    unlock();
    return true;
  }
  void endQuiet(){quietUntil_=0;quietEndedMs_=uint32_t(esp_timer_get_time()/1000);}
  uint32_t quietBeganMs()const{return quietBeganMs_;}
  uint32_t quietEndedMs()const{return quietEndedMs_;}
  bool lock(){return !gate_ || xSemaphoreTake(gate_,pdMS_TO_TICKS(20))==pdTRUE;}
  void unlock(){if(gate_)xSemaphoreGive(gate_);}
  bool running()const{return task_!=nullptr;}
  bool setMount(MountOrientation::Mode mode) {
    if(!MountOrientation::valid(unsigned(mode)))return false;
    if(ringGate_ && xSemaphoreTake(ringGate_,pdMS_TO_TICKS(20))!=pdTRUE)return false;
    if(mode_!=mode) {
      mode_=mode;++frame_;
      orientation_.resetFrame();activity_.reset();ring_.clear();
      snapshot_={};snapshot_.mount=mode;
    }
    if(ringGate_)xSemaphoreGive(ringGate_);
    return true;
  }
  Snapshot snapshot() {
    if(!ringGate_ || xSemaphoreTake(ringGate_,pdMS_TO_TICKS(20))!=pdTRUE)return {};
    const Snapshot s=snapshot_;xSemaphoreGive(ringGate_);return s;
  }
  bool capture(uint64_t start,uint64_t end,float rpm,uint32_t number,LaunchMotion::Trace &out) {
    auto copy=std::unique_ptr<LaunchMotion::Ring>(new(std::nothrow) LaunchMotion::Ring);
    if(!copy || !ringGate_ || xSemaphoreTake(ringGate_,pdMS_TO_TICKS(20))!=pdTRUE)return false;
    *copy=ring_;xSemaphoreGive(ringGate_);
    out=copy->build(start,end,rpm,number);return true;
  }
private:
  LaunchMotion::Ring ring_;LauncherOrientation orientation_;MotionActivity activity_;Snapshot snapshot_;
  MountOrientation::Mode mode_=MountOrientation::Mode::Top;
  uint32_t frame_=0;
  SemaphoreHandle_t gate_=nullptr,ringGate_=nullptr;TaskHandle_t task_=nullptr;
  static void entry(void *self){static_cast<StickS3MotionAcquisition *>(self)->run();}
  void run() {
    TickType_t last=xTaskGetTickCount();Snapshot s;uint32_t activeFrame=0;
    Snapshot::Gap span;uint64_t previousWake=0;bool haveAccel=false;unsigned stackPolls=0;
    for(;;) {
      const uint64_t wake=esp_timer_get_time();
      if(previousWake)span.periodUs=std::max(span.periodUs,uint32_t(wake-previousWake));
      previousWake=wake;++span.polls;
      // Checked while holding the gate; quiet() publishes the window under it.
      const bool locked=lock();
      if(locked && esp_timer_get_time()<quietUntil_.load()){unlock();++s.quietPolls;}
      else if(locked) {
        CpuPower::Guard clock(CpuPower::Imu);
        const int64_t started=esp_timer_get_time();span.lockUs=std::max(span.lockUs,uint32_t(started-wake));
        uint64_t sampledUs=0;
        const unsigned bytes=readFifo(s,sampledUs);
        // Validate, and flush on failure, before releasing the gate: a flush is
        // an I2C write and must never run inside a flash-quiet window.
        unsigned gyroFrames=0,skipped=0;
        const bool parsed=bytes && Bmi270Fifo::parse(fifo_,bytes,[&](const Bmi270Fifo::Frame &f){gyroFrames+=f.gyro;},
          [&](unsigned n){skipped+=n;});
        if(bytes && !parsed){++s.fifoParseErrors;noteError(s,bytes,0);discardFifo(s);}
        unlock();
        const uint64_t now=esp_timer_get_time();s.maxPollUs=std::max(s.maxPollUs,uint32_t(now-started));
        span.pollUs=std::max(span.pollUs,uint32_t(now-started));
        if(!gyroFrames)++span.emptyPolls;
        const float accelScale=accelRange().fullScaleG()/32768,gyroScale=StickS3AccelRange::GYRO_FULL_SCALE_DPS/32768;
        if(parsed) {timeline_.skip(skipped);s.fifoSkipped+=skipped;}
        if(parsed && !gyroFrames) {
          // Accelerometer-only frames update the held value; nothing is fused or published.
          Bmi270Fifo::parse(fifo_,bytes,[&](const Bmi270Fifo::Frame &f) {
            if(f.accel){Bmi270Fifo::boardAxes(f.a,accelScale,s.a);s.accelUs=sampledUs;++s.accelSamples;haveAccel=true;}
          },[](unsigned){});
        }
        if(parsed && gyroFrames) {
          timeline_.begin(sampledUs,gyroFrames);
          const uint64_t fusionStart=esp_timer_get_time();
          if(xSemaphoreTake(ringGate_,pdMS_TO_TICKS(20))==pdTRUE) {
            if(activeFrame!=frame_) {
              activeFrame=frame_;s.fusedUs=0;s.orientationReady=s.rest=false;
              for(unsigned j=0;j<3;++j){s.gravity[j]=s.bias[j]=0;}
              s.biasSigma=0;
            }
            s.mount=mode_;
            Bmi270Fifo::parse(fifo_,bytes,[&](const Bmi270Fifo::Frame &f) {
              if(f.accel){Bmi270Fifo::boardAxes(f.a,accelScale,s.a);s.accelUs=sampledUs;++s.accelSamples;haveAccel=true;}
              if(!f.gyro)return;
              const uint64_t at=timeline_.next();
              if(!haveAccel)return;
              Bmi270Fifo::boardAxes(f.g,gyroScale,s.g);feed(s,span,at);
            },[](unsigned){});
            s.fusionSamples=orientation_.updates();s.fusionResets=orientation_.resets();s.rest=orientation_.rest();
            s.biasSigma=orientation_.biasDegrees(s.bias);
            s.activity=activity_.state();
            xSemaphoreGive(ringGate_);
          } else for(unsigned i=0;i<gyroFrames;++i)timeline_.next();
          const uint32_t fusionUs=uint32_t(esp_timer_get_time()-fusionStart);
          s.maxFusionUs=std::max(s.maxFusionUs,fusionUs);span.ringUs=std::max(span.ringUs,fusionUs);
          s.fifoAnchors=timeline_.anchors();s.fifoPeriodUs=float(timeline_.periodUs());
        }
      } else ++s.gateMisses;
      // The high-water mark walks the whole free stack a byte at a time
      // (about 1 ms on device, as long as the rest of a poll): once a second.
      if(!s.stackFree || ++stackPolls>=STACK_POLLS){stackPolls=0;s.stackFree=uxTaskGetStackHighWaterMark(nullptr);}
      const uint64_t publishStart=esp_timer_get_time();
      if(xSemaphoreTake(ringGate_,pdMS_TO_TICKS(20))==pdTRUE) {
        // setMount can run between fusion and publication. Never restore an
        // old-frame level after its snapshot has been invalidated.
        if(activeFrame==frame_)snapshot_=s;
        xSemaphoreGive(ringGate_);
      }
      span.ringUs=std::max(span.ringUs,uint32_t(esp_timer_get_time()-publishStart));
      pace(last,s);
    }
  }
  // Once a period or more behind, FreeRTOS's delay-until returns without
  // blocking, so a loop held up for long (gate waits, an overloaded core 0)
  // would run back-to-back through every missed period and keep IDLE0 off
  // core 0 until the task watchdog resets the chip. The FIFO already holds
  // the samples those polls would have read, so a late poll skips them: the
  // period restarts now, after a one-tick block.
  static constexpr TickType_t PERIOD=pdMS_TO_TICKS(POLL_MS);
  static void pace(TickType_t &last,Snapshot &s) {
    if(xTaskDelayUntil(&last,PERIOD)==pdTRUE)return;
    const TickType_t now=xTaskGetTickCount();
    ++s.latePolls;s.missedPolls+=(now-last)/PERIOD;
    last=now;vTaskDelay(1);
  }
  // Reads every complete FIFO byte. Returns 0 when empty or on a failed
  // length read, which consumes nothing and is retried on the next poll.
  // sampledUs is when the FIFO length was read: every returned frame was
  // already buffered then. A long transfer must not shift sample times later.
  unsigned readFifo(Snapshot &s,uint64_t &sampledUs) {
    sampledUs=esp_timer_get_time();
    uint8_t length[2];
    if(!Bmi270Fifo::readRegisters(Bmi270Fifo::FIFO_LENGTH_0,length,2)){++s.fifoErrors;++s.fifoLengthErrors;return 0;}
    const unsigned bytes=unsigned(length[0])|(unsigned(length[1]&0x3F)<<8);
    if(!bytes)return 0;
    if(bytes>Bmi270Fifo::CAPACITY){++s.fifoOversize;noteError(s,bytes,0);discardFifo(s);return 0;}
    // A full FIFO has overwritten its oldest frame, possibly mid-frame. Never
    // parse a buffer that may start inside a payload; take the real gap.
    if(bytes>=Bmi270Fifo::CAPACITY-Bmi270Fifo::FULL_MARGIN){++s.fifoOverflows;discardFifo(s,false);return 0;}
    // One transaction, as Bosch's driver does: a burst split mid-frame was
    // seen on device to return misaligned (unparseable) catch-up data.
    const int64_t readStart=esp_timer_get_time();
    if(!Bmi270Fifo::readRegisters(Bmi270Fifo::FIFO_DATA,fifo_,bytes)) {
      ++s.fifoDataErrors;noteError(s,bytes,0);discardFifo(s);return 0;
    }
    const uint32_t readUs=uint32_t(esp_timer_get_time()-readStart);
    if(readUs>Bmi270Fifo::transferBudgetUs(bytes)) {
      ++s.fifoStalls;s.lastStallUs=readUs;s.lastStallMs=uint32_t(readStart/1000);discardFifo(s,false);return 0;
    }
    ++s.fifoReads;s.fifoMaxBytes=std::max(s.fifoMaxBytes,uint32_t(bytes));return bytes;
  }
  static void noteError(Snapshot &s,unsigned bytes,unsigned offset) {
    s.lastErrorBytes=bytes;s.lastErrorOffset=offset;s.lastErrorMs=uint32_t(esp_timer_get_time()/1000);
  }
  void discardFifo(Snapshot &s,bool error=true){if(error)++s.fifoErrors;Bmi270Fifo::writeRegister(Bmi270Fifo::CMD,Bmi270Fifo::FLUSH);timeline_.reset();}
  // One gyro frame with the latest accelerometer frame. Caller holds ringGate_.
  void feed(Snapshot &s,Snapshot::Gap &span,uint64_t now) {
    if(s.pairedUs && now>s.pairedUs){
      const uint32_t gap=uint32_t(now-s.pairedUs);s.maxGapUs=std::max(s.maxGapUs,gap);
      if(gap>35000) {
        ++s.gaps;span.atMs=uint32_t(now/1000);span.gapUs=gap;
        s.gapLog[s.gapNext%Snapshot::GAPS]=span;++s.gapNext;
      }
    }
    s.pairedUs=now;++s.pairedSamples;span={};
    LaunchMotion::Sample sample;sample.us=now;uint32_t axes=0;
    for(unsigned j=0;j<3;++j) {
      if(accelRange().accelClipped(s.a[j]))axes|=1u<<j;
      if(StickS3AccelRange::gyroClipped(s.g[j]))axes|=8u<<j;
    }
    // Saturated readings remain finite and keep fusion continuous.
    sample.saturated=axes!=0;
    for(unsigned j=0;j<3;++j)sample.clipped|=!std::isfinite(s.a[j]) || !std::isfinite(s.g[j]);
    if(axes) {
      ++s.clipSamples;++s.clipRun;s.maxClipRun=std::max(s.maxClipRun,s.clipRun);
      s.lastClipRun=s.clipRun;s.lastClipMs=uint32_t(now/1000);
      s.lastClipAxes=s.clipRun==1?axes:s.lastClipAxes|axes;
    } else s.clipRun=0;
    // Keep diagnostics' a/g native; fusion, level and stored traces
    // all use one canonical launcher frame, independent of LCD flip.
    MountOrientation::mapVector(mode_,s.a,sample.a);
    MountOrientation::mapVector(mode_,s.g,sample.g);
    orientation_.feed(sample,[&](const LaunchMotion::Sample &fused) {
      activity_.feed(fused.us,fused.a,fused.g,!fused.clipped);
      ring_.feed(fused);s.fusedUs=fused.us;s.orientationReady=fused.fused && fused.settled && !fused.clipped;
      const float up[3]={0,0,1};LaunchMotion::inverse(fused.q).rotate(up,s.gravity);
    });
  }
  Bmi270Fifo::Timeline timeline_;uint8_t fifo_[Bmi270Fifo::CAPACITY];std::atomic<int64_t> quietUntil_{0};std::atomic<uint32_t> quietBeganMs_{0},quietEndedMs_{0};
};
