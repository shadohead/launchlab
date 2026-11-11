#pragma once
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <atomic>
#include <new>
#include "mic_rpm.h"
#include "mic_session.h"
#include "mic_trace.h"
#include "analysis_rpm_filter.h"
#include "motion_acquisition.h"
#include "pm1_idle.h"
#include "raw_transfer.h"
#if LAUNCHLAB_HIL
#include "hil_mic.h"
#endif

// The built-in mic behind the Mic RPM page, off until the page opens. See
// docs/FIRMWARE_MIC_RPM.md. Nothing in the optical path reads it.
//
// The mic and speaker share the ES8311 and its I2S clock pins (G15, G17, G18)
// and M5Unified runs them from separate ports with separate codec setups, so
// they cannot run together: the sketch keeps the speaker muted while it runs.
// setup() leaves M5Unified's mic unconfigured; this sets it up on first use.
//
// One task on the UI core owns all of it and never touches core 0: it brings the
// mic up, reads the ring M5.Mic's task fills, and takes the mic down and frees
// everything on its way out, so the loop never waits on a mic start or stop.
// It runs at the loop's priority, waiting on a notification when it has nothing
// to do, and follows each spin from the end of its pull, behind it by a
// reading: the tracker needs 0.4 s windows, and the ring gives it 8 s to fall
// back on. Only its bench runs at the idle priority. Each codec register is one
// gate hold, at a raised priority, so the IMU owner waits a register write,
// not a mic start.
// M5Unified saves the enable callback for itself. setup() never gives it one here.
struct MicAccess:m5::Mic_Class {
  static auto enableCallback(){return &MicAccess::setCallback;}
};
class MicListener {
public:
  static constexpr uint32_t RATE=MicRpm::CAPTURE_HZ,RING_SAMPLES=RATE*8,CHUNK=512;  // 8 s in 32 ms chunks
  static constexpr uint32_t BEFORE_MS=500,DUMP_MS=2500;
  static constexpr uint32_t WARMUP_MS=1000;  // the ES8311 gives zeros until then
  // ES8311 ADC: analog PGA +21 dB and ADC volume +6 dB (M5Unified's Stopwatch
  // setup, where its SNR plateaus). Its digital high-pass stays off: the
  // Stopwatch's cuts below 200 Hz, where the spin tone is.
  static constexpr uint8_t CODEC=0x18,PGA=0x17,VOLUME=0xCB;
  explicit MicListener(StickS3MotionAcquisition &imu):imu_(imu){}
  MicSession::State state()const{return session_.state();}
  MicSession::Why why()const{return session_.why();}
  bool muted()const{return session_.muted();}
  bool off()const{return session_.off();}
  // Sleep and power-off wait while this holds: a stop STOP_LIMIT_MS at most.
  bool holdsSleep(uint32_t now)const{return MicSession::holdsSleep(state(),now-stopAskedMs_);}
  bool on()const{return state()==MicSession::State::On;}
  bool warm(uint32_t now)const{return on() && now-onMs_>=WARMUP_MS;}
  const MicTrace::Trace *trace()const{return trace_;}
  void freezeTrace(){if(trace_){trace_->freeze();stoppedLaunch_=trace_->launch();if(analysisFollowing_ && task_)xTaskNotifyGive(task_);}}
  void setAnalysisMode(bool enabled){analysisMode_=enabled;}
  // A reading arrived since the last call.
  bool fresh(){const bool was=fresh_;fresh_=false;return was;}
  // Each loop pass while the page is open or the mic has not yet stopped.
  void tick(bool wanted,bool speakerPowered,uint32_t now) {
    const int32_t room=int32_t(heap_caps_get_free_size(DMA_HEAP))-floor_;
    if(off())wantedMs_=now;
    const auto before=state();
    switch(session_.step(wanted,speakerPowered,room,worker())) {
      case MicSession::Action::Spawn:if(!spawn(now))session_.spawnFailed();break;
      case MicSession::Action::Quit:
        quit_=true;quitUs_=esp_timer_get_time();vTaskPrioritySet(task_,PRIORITY);xTaskNotifyGive(task_);stopAskedMs_=now;break;
      case MicSession::Action::Reap:reap();break;
      case MicSession::Action::None:break;
    }
    if(before!=state()) {
      if(state()==MicSession::State::On){onMs_=now;startMs_=now-wantedMs_;}
      if(before==MicSession::State::Stopping){stopMs_=now-stopAskedMs_;stopMaxMs_=std::max(stopMaxMs_,stopMs_);}
      Serial.printf("MIC_STATE state=%s why=%s\n",name(state()),name(why()));
    }
    if(on()) {
#if LAUNCHLAB_HIL
      if(delayed_.due && esp_timer_get_time()>=delayed_.atUs){delayed_.due=false;take(delayed_.number,delayed_.valid,delayed_.rpm,delayed_.startUs,delayed_.endUs);}
#endif
      heapLow_=std::min(heapLow_,room+floor_);
      MicTrace::Step step;
      while(xQueueReceive(steps_,&step,0)==pdTRUE)add(step);
    }
    report();
  }
  // A pull's result, from the loop. `rpm` is its peak when `valid`. Without
  // one the search has no prior, and it cannot end a spin being heard: the
  // page keeps what it shows until this one is heard.
  void launch(uint32_t number,bool valid,float rpm,uint64_t startUs,uint64_t endUs) {
    if(!on() || !endUs)return;
    if(!valid && job_.load()==Job::Following)return;
#if LAUNCHLAB_HIL
    Hil::micTone.begin();
    if(lagUs_){delayed_={true,number,valid,rpm,startUs,endUs,esp_timer_get_time()+int64_t(lagUs_)};return;}
#endif
    take(number,valid,rpm,startUs,endUs);
  }
  // USB `Y`: the bench again, as soon as the task is idle.
  void askBench(){portENTER_CRITICAL(&clockMux_);benchAsked_=on();portEXIT_CRITICAL(&clockMux_);if(task_ && on())xTaskNotifyGive(task_);else Serial.println("MIC_BENCH none");}
  // USB `A`: the last pull's sound (0.5 s before it to 2.5 s after it ended)
  // through the raw transfer, then capture resumes. The ring holds only 8 s, so
  // this ends the spin being followed.
  void dump() {
    if(on()) {
      dumping_=true;xTaskNotifyGive(task_);
      for(unsigned i=0;i<50 && job_.load()!=Job::Idle;++i)delay(10);
      dumping_=false;
    }
    if(!on() || !have_ || waiting_ || job_.load()!=Job::Idle){Serial.println("MIC_DUMP none");return;}
    hold_=true;delay(80);  // the chunks in flight land
    const uint64_t first=pull_.pullSample>BEFORE_MS*RATE/1000?pull_.pullSample-BEFORE_MS*RATE/1000:0;
    const uint64_t have=written(),end=std::min<uint64_t>(pull_.releaseSample+DUMP_MS*RATE/1000,have);
    if(end<=first || have-first>RING_SAMPLES-CHUNK*2) {
      Serial.println("MIC_DUMP stale");
    } else {
      Serial.printf("MIC_DUMP {\"launch\":%lu,\"optical_rpm\":%.1f,\"rate\":%lu,\"pull_sample\":%llu,\"release_sample\":%llu,\"pga\":%u,\"volume\":%u,\"late_chunks\":%lu}\n",
        (unsigned long)pull_.number,pull_.rpm,(unsigned long)RATE,(unsigned long long)(pull_.pullSample-first),(unsigned long long)(pull_.releaseSample-first),PGA,VOLUME,(unsigned long)late_);
      opticalTransferRaw(reinterpret_cast<const uint16_t *>(ring_),RING_SAMPLES,first,uint32_t(end-first),0,late_,misses_,true,ESP_OK,false);
    }
    hold_=false;start();
  }
  // USB `W`: from now until the next boot, each spin's 4 kHz tracker line is
  // kept too, the last TAPE_S seconds of them, for USB `V` after a replug: the
  // owner pulls off USB, and `A` keeps only 3 s. Off unless asked: 960 KB of PSRAM.
  void armTape() {
    if(!tapeArmed_.load() && !tape_.samples) {
      tape_.capacity=TAPE_SAMPLES;
      tape_.samples=static_cast<int16_t *>(heap_caps_calloc(TAPE_SAMPLES,sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
      tapeArmed_=tape_.samples!=nullptr;
    }
    Serial.printf("MIC_TAPE armed=%d seconds=%lu spin_max_s=%lu\n",int(tapeArmed_.load()),(unsigned long)TAPE_S,(unsigned long)TAPE_SPIN_S);
  }
  // USB `V`: every kept spin still whole on the tape, oldest first, each a
  // MIC_TAPE header and a raw transfer of int16 at 4 kHz from its release.
  void sendTape() {
    if(!tapeArmed_.load() || !spins_){Serial.println("MIC_TAPE none");return;}
    if(job_.load()!=Job::Idle){Serial.println("MIC_TAPE busy");return;}
    unsigned sent=0;
    for(uint32_t i=logged_.load()>SPINS?logged_.load()-SPINS:0;i<logged_.load();++i) {
      const Spin &s=spins_[i%SPINS];
      if(!s.tapeCount || tape_.written-s.tapeStart>tape_.capacity)continue;
      Serial.printf("MIC_TAPE {\"launch\":%lu,\"optical_rpm\":%.1f,\"rate\":%lu,\"samples\":%lu,\"skipped_ms\":%lu,\"first_ms\":%lu,\"frames\":%u,\"valid\":%u,\"pga\":%u,\"volume\":%u}\n",
        (unsigned long)s.launch,s.opticalRpm,(unsigned long)MicRpm::TRACK_HZ,(unsigned long)s.tapeCount,(unsigned long)s.skippedMs,(unsigned long)s.firstMs,unsigned(s.frames),unsigned(s.valid),PGA,VOLUME);
      opticalTransferRaw(reinterpret_cast<const uint16_t *>(tape_.samples),tape_.capacity,s.tapeStart,s.tapeCount,0,0,0,true,ESP_OK,false);
      ++sent;
    }
    Serial.printf("MIC_TAPE end spins=%u\n",sent);
  }
  // Every counter stays until the next boot, and the last spins with it: a
  // unit unplugged for its pulls still reports them on the next `p`.
  void status() {
    Serial.printf("MIC_STATUS state=%s why=%s samples=%llu late_chunks=%lu record_misses=%lu restarts=%lu starts=%lu start_ms=%lu stop_ms=%lu stop_max_ms=%lu stop_task_ms=%lu stop_end_ms=%lu gate_max_us=%lu heap_low=%ld floor=%ld peak=%d min_interval_us=%lu max_interval_us=%lu task_stack_free=%lu behind_max_ms=%lu overruns=%lu late_starts=%lu lost_steps=%lu reading_us_max=%lu\n",
      name(state()),name(why()),(unsigned long long)written(),(unsigned long)late_,(unsigned long)misses_,(unsigned long)restarts_,(unsigned long)starts_,
      (unsigned long)startMs_,(unsigned long)stopMs_,(unsigned long)stopMaxMs_,(unsigned long)stopTaskMs_,(unsigned long)stopEndMs_,(unsigned long)gateMaxUs_,(long)heapLow_,(long)floor_,takePeak(),
      (unsigned long)intervalMin_,(unsigned long)intervalMax_,(unsigned long)stackFree_,
      (unsigned long)(behindMax_*1000/RATE),(unsigned long)overruns_,(unsigned long)lateStarts_,(unsigned long)lostSteps_,(unsigned long)readingMax_);
    intervalMin_=UINT32_MAX;intervalMax_=0;
    for(unsigned i=0;i<SEARCHES;++i)if(cost_[i].n)
      Serial.printf("MIC_READING search=%s n=%lu mean_us=%lu max_us=%lu\n",searchName(i),(unsigned long)cost_[i].n,(unsigned long)(cost_[i].sum/cost_[i].n),(unsigned long)cost_[i].max);
    Serial.printf("MIC_BENCH runs=%lu prior_us=%lu near_us=%lu full_us=%lu quiet_us=%lu fft_us=%lu plain_fft_us=%lu fft_error=%.6f decimate_us_per_s=%lu\n",
      (unsigned long)bench_.runs,(unsigned long)bench_.wall[1],(unsigned long)bench_.wall[2],(unsigned long)bench_.wall[3],(unsigned long)bench_.wall[0],
      (unsigned long)bench_.fft,(unsigned long)bench_.plainFft,bench_.fftError,(unsigned long)bench_.decimate);
    if(spins_)for(uint32_t i=logged_.load()>SPINS?logged_.load()-SPINS:0;i<logged_.load();++i)print("MIC_SPIN_LOG",spins_[i%SPINS]);
  }
#if LAUNCHLAB_HIL
  // !micfloor: production's floor less the HIL image's own internal RAM.
  void setFloor(int32_t bytes){floor_=bytes;}
  // !miclag: each result reaches the task this much later, as a slow result or
  // a busy task would; past the ring's 8 s the spin is heard from its oldest sample.
  void setLag(uint32_t ms){lagUs_=uint64_t(ms)*1000;delayed_.due=false;}
  // !micframe: the page's slowest frame.
  void worstFrame(){if(trace_){MicTrace::demo(*trace_,MicTrace::MAX_READINGS,8,100);fresh_=true;}}
  bool hasTrace()const{return trace_;}
#endif
private:
  static constexpr uint32_t DMA_HEAP=MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA;
  static constexpr unsigned SEARCHES=4,SPINS=8,STEPS=16;  // 3.2 s of readings: far over any loop stall
  // The ring a reader may use, the newest two chunks being in flight, and the
  // room it keeps once it has had to skip ahead: a second of catching up.
  static constexpr uint32_t USABLE=RING_SAMPLES-CHUNK*2,MARGIN=RATE;
  // The loop's: it shares the core with the UI tick by tick, and a reading is
  // about 10% of it, so a frame or checkpoint waits at most that much. At the
  // idle priority it also shared with the idle task (a reading's wall time was
  // 55 ms for 16-21 of CPU), and a pull or a stop waited for whatever the loop
  // left: the owner's a8ee2234 pulls lost four spins whole and a stop took 13 s.
  // The bench alone stays there, and a pull or a stop lifts it out at once.
  static constexpr UBaseType_t PRIORITY=1,BENCH_PRIORITY=0;
  static constexpr uint32_t TAPE_S=120,TAPE_SPIN_S=90,TAPE_SAMPLES=TAPE_S*MicRpm::TRACK_HZ;
  // Over the loop and the capture task: preempted here it would hold the IMU
  // gate for a whole frame.
  static constexpr UBaseType_t GATE_PRIORITY=3;
  // In PSRAM: the DMA-capable heap has little to spare, and this task touches
  // no flash, so it never runs with the cache off. mic_rpm.h's deepest frame is
  // under 700 bytes, and the mic's start and stop run on this stack too.
  static constexpr uint32_t TASK_STACK=4096;
  enum class Job:uint8_t{Idle,Following};
  // `lagMs`: from the release to the result reaching launch(), at `notedUs`.
  struct Pull {uint32_t number;float rpm;uint64_t pullSample,releaseSample;uint32_t pullMs,lagMs;int64_t notedUs;bool analysis=false;};
  struct Cost {uint32_t n=0,max=0;uint64_t sum=0;void add(uint32_t us){++n;sum+=us;max=std::max(max,us);}};
  // One spin, as MIC_SPIN reports it. `wakeMs`: from launch() to the task
  // taking the pull; `skippedMs`: the sound the ring had already lost.
  struct Spin {
    uint32_t launch,pullMs,firstMs,wallMs,lagMs,wakeMs,skippedMs,tapeCount;uint64_t tapeStart;float opticalRpm,firstHz,settledHz,lastHz,levelMax;
    uint16_t frames,valid,cycles,withheld[MicRpm::REASONS];
  };
  struct Bench {uint32_t runs=0,wall[SEARCHES]={},fft=0,plainFft=0,decimate=0;float fftError=0;};
  static const char *name(MicSession::State s) {
    using S=MicSession::State;
    switch(s){case S::Off:return "off";case S::Clearing:return "clearing";case S::Starting:return "starting";case S::On:return "on";case S::Stopping:return "stopping";default:return "refused";}
  }
  static const char *name(MicSession::Why w) {
    using W=MicSession::Why;
    return w==W::Memory?"memory":w==W::Failed?"failed":"none";
  }
  static const char *searchName(unsigned i){static constexpr const char *NAMES[SEARCHES]={"none","prior","near","full"};return NAMES[i];}
  // Loop side -----------------------------------------------------------------
  bool ensureTrace() {
    const unsigned capacity=analysisMode_?MicTrace::ANALYSIS_READINGS:MicTrace::MAX_READINGS;
    if(trace_ && trace_->capacity()>=capacity)return true;
    void *memory=heap_caps_calloc(1,sizeof(MicTrace::Trace)+capacity*sizeof(uint16_t)*(analysisMode_?2:1),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!memory)return false;
    auto *slots=reinterpret_cast<uint16_t *>(static_cast<uint8_t *>(memory)+sizeof(MicTrace::Trace));
    auto *replacement=new(memory) MicTrace::Trace(slots,capacity,analysisMode_?slots+capacity:nullptr);
    heap_caps_free(trace_);trace_=replacement;return true;
  }
  void take(uint32_t number,bool valid,float rpm,uint64_t startUs,uint64_t endUs) {
    if(!ensureTrace())return;
    stoppedLaunch_=0;
    int64_t lag=0;const uint64_t release=sampleAt(endUs,&lag);
    const Pull pull{number,valid?rpm:0,sampleAt(startUs),release,uint32_t((endUs-startUs)/1000),uint32_t(std::max<int64_t>(lag,0)/1000),esp_timer_get_time(),analysisMode_};
    if(valid)trace_->begin(number,rpm);else unheard_=number;
    portENTER_CRITICAL(&clockMux_);next_=pull;waiting_=true;portEXIT_CRITICAL(&clockMux_);
    vTaskPrioritySet(task_,PRIORITY);  // out of its bench, if it is in it
    xTaskNotifyGive(task_);
  }
  bool spawn(uint32_t now) {
    (void)now;
    if(!spins_)spins_=static_cast<Spin *>(heap_caps_calloc(SPINS,sizeof(Spin),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    ensureTrace();
    steps_=xQueueCreate(STEPS,sizeof(MicTrace::Step));
    stack_=heap_caps_malloc(TASK_STACK,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    block_=static_cast<StaticTask_t *>(heap_caps_malloc(sizeof(StaticTask_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    quit_=false;quitUs_=0;hold_=true;waiting_=false;have_=false;job_=Job::Idle;fresh_=false;unheard_=0;chain_.begin();
    worker_=MicSession::Worker::Starting;
    if(spins_ && trace_ && steps_ && stack_ && block_)
      task_=xTaskCreateStaticPinnedToCore(entry,"mic_rpm",TASK_STACK,this,PRIORITY,static_cast<StackType_t *>(stack_),block_,1);
    if(task_)return true;
    release();worker_=MicSession::Worker::None;return false;
  }
  // What the task says. Stopped or Failed is its last word before it waits
  // forever: it shares the loop's core, so reap() never deletes it mid-step.
  MicSession::Worker worker()const{return worker_.load();}
  void reap(){vTaskDelete(task_);task_=nullptr;release();worker_=MicSession::Worker::None;}
  void release() {
    if(steps_){vQueueDelete(steps_);steps_=nullptr;}
    heap_caps_free(trace_);heap_caps_free(stack_);heap_caps_free(block_);
    trace_=nullptr;stack_=nullptr;block_=nullptr;
  }
  void add(const MicTrace::Step &step) {
    if(step.launch!=trace_->launch() && step.launch==unheard_ && step.rpm)trace_->begin(step.launch,0);
    trace_->add(step);fresh_=true;
  }
  void report() {
    const uint32_t logged=logged_.load();
    if(reported_==logged)return;
    if(logged-reported_>SPINS)reported_=logged-SPINS;
    print("MIC_SPIN",spins_[reported_++%SPINS]);
  }
  static void print(const char *tag,const Spin &s) {
    using R=MicRpm::Reason;
    auto n=[&](R r){return unsigned(s.withheld[unsigned(r)]);};
    Serial.printf("%s launch=%lu optical_rpm=%.1f pull_ms=%lu frames=%u valid=%u first_ms=%lu first_hz=%.1f settled_hz=%.1f last_hz=%.1f cycles=%u level_max_db=%.1f quiet=%u clipped=%u broadband=%u weak=%u ambiguous=%u harmonic=%u acquiring=%u faster=%u wall_ms=%lu lag_ms=%lu wake_ms=%lu skipped_ms=%lu\n",
      tag,(unsigned long)s.launch,s.opticalRpm,(unsigned long)s.pullMs,unsigned(s.frames),unsigned(s.valid),(unsigned long)s.firstMs,s.firstHz,s.settledHz,s.lastHz,unsigned(s.cycles),s.levelMax,
      n(R::Quiet),n(R::Clipping),n(R::BroadbandNoise),n(R::WeakHarmonics),n(R::Ambiguous),n(R::HarmonicAmbiguity),n(R::Acquiring),n(R::FasterThanOptical),
      (unsigned long)s.wallMs,(unsigned long)s.lagMs,(unsigned long)s.wakeMs,(unsigned long)s.skippedMs);
  }
  uint64_t written() {
    portENTER_CRITICAL(&clockMux_);const uint64_t n=written_;portEXIT_CRITICAL(&clockMux_);
    return n;
  }
  // The loudest sample since the last report.
  int takePeak() {
    portENTER_CRITICAL(&clockMux_);const int peak=peak_;peak_=0;portEXIT_CRITICAL(&clockMux_);
    return peak;
  }
  bool takeBenchAsk() {
    portENTER_CRITICAL(&clockMux_);const bool asked=benchAsked_;benchAsked_=false;portEXIT_CRITICAL(&clockMux_);
    return asked;
  }
  // The stream position of a time: the newest chunk ended at lastUs_, which
  // `age` gets the time before (0 before the first chunk).
  uint64_t sampleAt(uint64_t us,int64_t *age=nullptr) {
    portENTER_CRITICAL(&clockMux_);const uint64_t written=written_;const int64_t last=lastUs_;portEXIT_CRITICAL(&clockMux_);
    if(age)*age=last?last-int64_t(us):0;
    const int64_t back=(last-int64_t(us))*RATE/1000000;
    return back>=int64_t(written)?0:back<=0?written:written-uint64_t(back);
  }
  // Capture task --------------------------------------------------------------
  static void released(void *self,void *data,size_t length){static_cast<MicListener *>(self)->chunk(static_cast<int16_t *>(data),length);}
  // Once per chunk: keep it short and never block.
  void chunk(int16_t *data,size_t length) {
#if LAUNCHLAB_HIL
    Hil::micTone.fill(data,length);
#endif
    const int64_t now=esp_timer_get_time();
    int peak=0;for(size_t i=0;i<length;i+=4)peak=std::max(peak,std::abs(int(data[i])));
    if(lastUs_) {
      const uint32_t gap=uint32_t(now-lastUs_);intervalMin_=std::min(intervalMin_,gap);intervalMax_=std::max(intervalMax_,gap);
      if(gap>CHUNK*1000000/RATE*2)++late_;
    }
    portENTER_CRITICAL(&clockMux_);written_+=length;lastUs_=now;peak_=std::max(peak_,peak);portEXIT_CRITICAL(&clockMux_);
    // Callback runs in the capture task, not an ISR. Only Analysis followers subscribe.
    // Publish samples before notifying; counting notifications close the check/wait race.
    if(analysisFollowing_.load() && task_)xTaskNotifyGive(task_);
    if(hold_)return;
    // The two queued requests stay continuous. Only this task refills them, once
    // start() has lit the chain: a second writer can outrun it and both then see
    // a full queue, which held the task here for 10 s behind a stop.
    if(queue())queue();
    // hold_ goes up before the mic's end(), which refuses every request.
    else if(chain_.refilled(false,hold_,M5.Mic.isRecording()))++misses_;
  }
  // The next chunk of the ring, if M5.Mic has a free request.
  bool queue() {
    if(!M5.Mic.record(ring_+queued_,CHUNK))return false;
    queued_=(queued_+CHUNK)%RING_SAMPLES;
    return true;
  }
  void start() {
    queued_=uint32_t(written()%RING_SAMPLES);queued_-=queued_%CHUNK;
    lastUs_=0;
    if(!queue())chain_.refilled(false,false,false);
  }
  // Task ----------------------------------------------------------------------
  // This task's CPU time so far, which only moves when it is switched out.
  static void entry(void *self){static_cast<MicListener *>(self)->run();}
  void run() {
    const bool up=allocate() && startMic();
    if(up) {
      hold_=false;start();worker_=MicSession::Worker::Running;++starts_;
      serve();
    }
#if LAUNCHLAB_HIL
    while(Hil::micStall.load())vTaskDelay(pdMS_TO_TICKS(10));  // !micstall: a stop that does not end
#endif
    const int64_t left=esp_timer_get_time();
    if(quitUs_)stopTaskMs_=uint32_t((left-quitUs_)/1000);
    hold_=true;
    stopMic();
    stopEndMs_=uint32_t((esp_timer_get_time()-left)/1000);
    stackFree_=uxTaskGetStackHighWaterMark(nullptr);
    discard();
    worker_=up?MicSession::Worker::Stopped:MicSession::Worker::Failed;
    for(;;)ulTaskNotifyTake(pdTRUE,portMAX_DELAY);  // the loop deletes it, then frees its stack
  }
  void serve() {
    while(!quit_) {
      if(chain_.relight(M5.Mic.isRecording())){++restarts_;start();}
      if(takePull()){follow();continue;}
      if((takeBenchAsk() || !bench_.runs) && !waiting_)benchmark();
      ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(100));
    }
  }
  bool allocate() {
    ring_=static_cast<int16_t *>(heap_caps_malloc(RING_SAMPLES*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    space_=static_cast<MicRpm::Space *>(heap_caps_aligned_alloc(alignof(MicRpm::Space),sizeof(MicRpm::Space),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    void *memory=heap_caps_malloc(sizeof(MicRpm::Stream),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(space_ && memory)stream_=new(memory) MicRpm::Stream(space_);
    else heap_caps_free(memory);
    return ring_ && stream_;
  }
  void discard() {
    heap_caps_free(ring_);heap_caps_free(space_);heap_caps_free(stream_);
    ring_=nullptr;space_=nullptr;stream_=nullptr;
  }
  bool startMic() {
    portENTER_CRITICAL(&clockMux_);written_=0;lastUs_=0;portEXIT_CRITICAL(&clockMux_);
    auto c=M5.Mic.config();
    c.pin_mck=GPIO_NUM_18;c.pin_bck=GPIO_NUM_17;c.pin_ws=GPIO_NUM_15;c.pin_data_in=GPIO_NUM_16;c.i2s_port=I2S_NUM_1;
    // 128 frames of 16 kHz mono x 8: 64 ms of DMA in 2 KB, over the 30 ms a
    // flash write holds the UI core. The length also sets M5.Mic's task stack.
    c.sample_rate=RATE;c.over_sampling=1;c.magnification=2;c.noise_filter_level=0;c.stereo=false;
    c.dma_buf_len=128;c.dma_buf_count=8;c.task_priority=2;c.task_pinned_core=1;
    M5.Mic.config(c);
    (M5.Mic.*MicAccess::enableCallback())(this,enabled);
    M5.Mic.setBufferReleaseCallback(this,released);
    if(!M5.Mic.begin())return false;
    // M5Unified's setup writes SYSTEM 0x0D before the clock runs, which its own
    // Stopwatch notes say the ES8311 ignores: arm it now.
    bool ok=true;
    for(unsigned i=0;i<3;++i){vTaskDelay(pdMS_TO_TICKS(30));ok&=write(0x0D,0x01);}
    ok=write(0x14,PGA) && write(0x17,VOLUME) && ok;
    return ok;
  }
  // Joins M5.Mic's task, up to its 100 ms read, off the gate.
  void stopMic(){M5.Mic.end();lastUs_=0;}
  // M5Unified's own StickS3 sequences, a register at a time: its callback holds
  // the bus for the whole list.
  static bool enabled(void *self,bool on) {
    static constexpr uint8_t ON[][2]={{0x00,0x80},{0x01,0xBA},{0x02,0x18},{0x0D,0x01},{0x0E,0x02},{0x14,0x10},{0x17,0xFF},{0x1C,0x6A}};
    static constexpr uint8_t OFF[][2]={{0x0D,0xFC},{0x0E,0x6A},{0x00,0x00}};
    auto *mic=static_cast<MicListener *>(self);bool ok=true;
    if(on)for(const auto &w:ON)ok&=mic->write(w[0],w[1]);
    else for(const auto &w:OFF)ok&=mic->write(w[0],w[1]);
    return ok;
  }
  bool write(uint8_t reg,uint8_t value) {
    const UBaseType_t priority=uxTaskPriorityGet(nullptr);
    vTaskPrioritySet(nullptr,GATE_PRIORITY);
    bool ok=false;
    for(unsigned attempt=0;attempt<3 && !ok;++attempt) {
      if(!imu_.lock())continue;
      const int64_t held=esp_timer_get_time();
      ok=pm1Idle().ensureAwake();
      if(ok) {
        m5gfx::i2c::i2c_temporary_switcher_t pins(1,GPIO_NUM_47,GPIO_NUM_48);
        ok=M5.In_I2C.writeRegister8(CODEC,reg,value,100000);
        pins.restore();
      }
      gateMaxUs_=std::max(gateMaxUs_,uint32_t(esp_timer_get_time()-held));
      imu_.unlock();
    }
    vTaskPrioritySet(nullptr,priority);
    return ok;
  }
  bool takePull() {
    portENTER_CRITICAL(&clockMux_);
    const bool waiting=waiting_;waiting_=false;const Pull pull=next_;
    portEXIT_CRITICAL(&clockMux_);
    if(!waiting)return false;
    pull_=pull;job_=Job::Following;have_=true;
    return true;
  }
  void send(const MicTrace::Step &step){if(xQueueSend(steps_,&step,pdMS_TO_TICKS(100))!=pdTRUE)++lostSteps_;}
  void finish(const MicRpm::Summary &summary,float firstSeconds,float lastHz,float levelMax,int64_t started,uint64_t skipped,uint64_t taped) {
    Spin &s=spins_[logged_.load()%SPINS];
    s.launch=pull_.number;s.opticalRpm=pull_.rpm;s.pullMs=pull_.pullMs;s.frames=uint16_t(summary.frames);s.valid=uint16_t(summary.valid);
    s.tapeStart=taped;s.tapeCount=uint32_t(tape_.written-taped);
    s.lagMs=pull_.lagMs;s.wakeMs=uint32_t(std::max<int64_t>(started-pull_.notedUs,0)/1000);s.skippedMs=uint32_t(skipped*1000/RATE);
    s.firstMs=uint32_t(firstSeconds*1000);s.firstHz=summary.firstHz();s.settledHz=summary.settledHz();s.lastHz=lastHz;s.cycles=uint16_t(summary.cycles);s.levelMax=levelMax;
    for(unsigned i=0;i<MicRpm::REASONS;++i)s.withheld[i]=uint16_t(summary.withheld[i]);
    s.wallMs=uint32_t((esp_timer_get_time()-started)/1000);
    logged_.fetch_add(1);
  }
  // The spin as heard from the end of the pull, reading by reading as its
  // samples reach the ring, until it is lost or a newer pull arrives. Sound the
  // ring has already lost is skipped, on the spin's clock, never the spin. The
  // optical path never waits for this.
  void follow() {
    const int64_t started=esp_timer_get_time();
    MicRpm::Summary summary;float levelMax=-200,firstSeconds=0,lastHz=0;
    analysisFollowing_=pull_.analysis;
    stream_->reset(pull_.rpm);
    const uint64_t taped=tape_.written;
    stream_->record(tapeArmed_.load()?&tape_:nullptr);
    uint64_t at=pull_.releaseSample,skipped=0;MicTrace::Follower follower(pull_.analysis);
    AnalysisMode::RpmFilter analysisFilter;
    uint32_t busy=0;
    while(!follower.done() && !quit_ && !waiting_ && !dumping_ && !(pull_.analysis && stoppedLaunch_.load()==pull_.number)) {
      const uint64_t have=written();
      if(at>=have){
        if(pull_.analysis)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(100));
        else vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      const uint64_t from=MicSession::resume(at,have,USABLE,MARGIN);
      if(from!=at) {
        if(summary.frames)++overruns_;else ++lateStarts_;
        stream_->skip(from-at);follower.skip();skipped+=from-at;at=from;
      }
      behindMax_=std::max(behindMax_,uint32_t(have-at));
      const uint32_t offset=uint32_t(at%RING_SAMPLES);
      const size_t count=size_t(std::min<uint64_t>(have-at,RING_SAMPLES-offset));
      MicRpm::Reading r;bool ready=false;
      const int64_t t=esp_timer_get_time();
      const size_t taken=stream_->feed(ring_+offset,count,r,ready);
      busy+=uint32_t(esp_timer_get_time()-t);
      at+=taken;
      if(!ready)continue;
      cost_[unsigned(r.search)].add(busy);readingMax_=std::max(readingMax_,busy);busy=0;
      if(r.valid && !summary.valid)firstSeconds=stream_->seconds();
      if(r.valid)lastHz=r.hz;
      summary.add(r);levelMax=std::max(levelMax,r.level);
      auto step=follower.next(pull_.number,r,stream_->seconds());
      if(pull_.analysis) {
        step.rawRpm=r.valid?uint16_t(std::lround(r.rpm)):0;
        step.rpm=uint16_t(std::lround(analysisFilter.update(r.valid?r.rpm:0,stream_->seconds())));
        if(r.valid && !step.rpm){step.rejectedRpm=uint16_t(std::lround(r.rpm));step.reason=MicRpm::Reason::Acquiring;}
      }
      send(step);
      if(tape_.written-taped>=uint64_t(TAPE_SPIN_S)*MicRpm::TRACK_HZ)stream_->record(nullptr);
    }
    stream_->record(nullptr);
    // A spin that stopped short still ends on the page; a newer pull replaces it.
    if(!follower.done() && !waiting_ && !quit_){MicRpm::Reading none;send(MicTrace::step(pull_.number,follower.slot()+1,none,true));}
    analysisFollowing_=false;
    finish(summary,firstSeconds,lastHz,levelMax,started,skipped,taped);
    job_=Job::Idle;
  }
  // A synthetic spin through the real code at the real clock, once on the first
  // open and on USB `Y`, so the cost is known however the unit was used. It runs
  // at the idle priority, the worst a reading ever met, gives way to a pull or a
  // stop within a reading, and goes again when the task is next idle.
  void benchmark() {
    vTaskPrioritySet(nullptr,BENCH_PRIORITY);
    measure();
    vTaskPrioritySet(nullptr,PRIORITY);
  }
  void measure() {
    static constexpr unsigned SECONDS=3;
    auto *pcm=static_cast<int16_t *>(heap_caps_malloc(RATE*SECONDS*sizeof(int16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!pcm)return;
    for(uint32_t i=0;i<RATE*SECONDS;++i) {
      if(!(i%CHUNK) && (quit_ || waiting_)){heap_caps_free(pcm);return;}
      const float phase=2*float(M_PI)*110.f*float(i)/RATE;
      pcm[i]=int16_t(32768*(0.1f*std::sin(phase)+0.05f*std::sin(2*phase)+0.03f*std::sin(3*phase)+0.02f*std::sin(4*phase)));
    }
    Bench bench;Cost wall[SEARCHES];bool stopped=false;
    auto pass=[&](float opticalRpm,const int16_t *samples,size_t count) {
      stream_->reset(opticalRpm);
      while(count && !(stopped=quit_ || waiting_)) {
        MicRpm::Reading r;bool ready=false;
        const int64_t t=esp_timer_get_time();
        const size_t taken=stream_->feed(samples,count,r,ready);
        samples+=taken;count-=taken;
        if(!ready)continue;
        wall[unsigned(r.search)].add(uint32_t(esp_timer_get_time()-t));
      }
    };
    // Near and prior with a prior, full without one, and a window with nothing in it.
    pass(6900,pcm,RATE*SECONDS);
    pass(0,pcm,RATE);
    std::fill(pcm,pcm+RATE,int16_t(0));
    pass(0,pcm,RATE);
    if(!stopped) {
      for(unsigned i=0;i<SEARCHES;++i)if(wall[i].n){bench.wall[i]=uint32_t(wall[i].sum/wall[i].n);}
      float low,total=0;const int64_t t=esp_timer_get_time();
      auto *decimator=new(pcm) MicRpm::Decimator;
      for(uint32_t i=0;i<RATE;++i)if(decimator->feed(float(i&255)/256.f,low))total+=low;
      bench.decimate=uint32_t(esp_timer_get_time()-t);
      if(total<0)bench.decimate=0;  // keeps the loop
      // The tracker's transform against the portable one, on the same window.
      auto &tracker=stream_->tracker();
      auto *power=reinterpret_cast<float *>(pcm);  // what is left of the input buffer
      auto input=[&]{for(unsigned i=0;i<MicRpm::MAX_FFT;++i)space_->work[i]=std::sin(0.05f*float(i))+0.3f*std::sin(0.31f*float(i));};
      input();
      const int64_t plain=esp_timer_get_time();
      tracker.benchTransform(true);
      bench.plainFft=uint32_t(esp_timer_get_time()-plain);
      std::copy(space_->power,space_->power+MicRpm::MAX_FFT/2+1,power);
      input();
      const int64_t fast=esp_timer_get_time();
      tracker.benchTransform(false);
      bench.fft=uint32_t(esp_timer_get_time()-fast);
      float biggest=0,worst=0;
      for(unsigned i=0;i<=MicRpm::MAX_FFT/2;++i){biggest=std::max(biggest,power[i]);worst=std::max(worst,std::fabs(power[i]-space_->power[i]));}
      bench.fftError=biggest>0?worst/biggest:1;
      bench.runs=bench_.runs+1;bench_=bench;
    }
    heap_caps_free(pcm);
    stream_->reset();
  }
  StickS3MotionAcquisition &imu_;
  MicSession::Machine session_;std::atomic<MicSession::Worker> worker_{MicSession::Worker::None};
  int16_t *ring_=nullptr;MicRpm::Space *space_=nullptr;MicRpm::Stream *stream_=nullptr;TaskHandle_t task_=nullptr;QueueHandle_t steps_=nullptr;
  void *stack_=nullptr;StaticTask_t *block_=nullptr;
  bool analysisMode_=false;std::atomic<bool> analysisFollowing_{false};std::atomic<uint32_t> stoppedLaunch_{0};
  MicTrace::Trace *trace_=nullptr;Spin *spins_=nullptr;  // PSRAM, on first use; the spin log stays for the next `p`
  MicRpm::Tape tape_;std::atomic<bool> tapeArmed_{false};  // the task writes it only while following
  volatile bool hold_=true,waiting_=false,quit_=false,dumping_=false;bool have_=false,fresh_=false;
  std::atomic<Job> job_{Job::Idle};MicSession::Chain chain_;std::atomic<uint32_t> logged_{0};
  // Under clockMux_: a 64-bit atomic or an exchange is a libatomic call the IDF
  // places in IRAM, and every IRAM byte comes out of the internal heap.
  uint64_t written_=0;int peak_=0;bool benchAsked_=false;
  portMUX_TYPE clockMux_=portMUX_INITIALIZER_UNLOCKED;
  int64_t lastUs_=0,quitUs_=0;uint32_t queued_=0;
#if LAUNCHLAB_HIL
  struct Late {bool due=false;uint32_t number=0;bool valid=false;float rpm=0;uint64_t startUs=0,endUs=0;int64_t atUs=0;};
  uint64_t lagUs_=0;Late delayed_;
#endif
  Pull next_{},pull_{};  // next_ under clockMux_; pull_ is the task's until the job is Idle
  int32_t floor_=MicSession::FLOOR_BYTES,heapLow_=INT32_MAX;
  uint32_t onMs_=0,wantedMs_=0,stopAskedMs_=0,startMs_=0,stopMs_=0,stopMaxMs_=0,stopTaskMs_=0,stopEndMs_=0,unheard_=0,reported_=0;
  uint32_t late_=0,misses_=0,restarts_=0,starts_=0,intervalMin_=UINT32_MAX,intervalMax_=0,behindMax_=0,overruns_=0,lateStarts_=0,lostSteps_=0,stackFree_=0,gateMaxUs_=0,readingMax_=0;
  Cost cost_[SEARCHES];Bench bench_;
};
