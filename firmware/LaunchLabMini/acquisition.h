#pragma once
#include "esp_adc/adc_continuous.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sensor_profile.h"
#include "rpm_estimator.h"
#include "sample_ring.h"
#include "history_checkpoint.h"

// All ADC API calls, detector state and recorder writes have ONE owner on core
// 0. M5Unified, drawing, USB and Preferences stay on the Arduino/UI core. No
// sample-by-sample logging, cross-core detector reads, or ADC calls from UI.
class StickS3Acquisition {
public:
  static constexpr uint32_t SAMPLE_HZ=AnalogTachometer::SAMPLE_HZ;
  static constexpr uint32_t RING_SAMPLES=SAMPLE_HZ*60;
  enum class Action:uint8_t { Pin, Suspend, Resume, Reset, CheckpointBegin, CheckpointEnd, ShutdownBegin, IdleCheckpointBegin, Estimator, HistoryIdleCheckpointBegin, SensorProfile, IdlePause, IdleResume };
  struct Snapshot {
    AnalogTachometer::Phase phase=AnalogTachometer::Phase::Settling;
    AnalogTachometer::End ended=AnalogTachometer::End::None;
    bool ready=false,suspended=false,enabled=true,valid=false,signalFault=false;
    bool opticalIdle=false;
    bool resting=false;
    uint8_t pin=1;
    esp_err_t error=ESP_OK;
    uint64_t samples=0,detectorUs=0,ringFirst=0;
    uint64_t burstStartHostUs=0,burstEndHostUs=0;
    uint32_t ringCount=0,revision=0,launches=0,revolutions=0,edges=0;
    uint32_t weak=0,slow=0,shape=0,inconsistent=0,rearmExtensions=0,rearmMs=0;
    uint32_t singlePeakUpdates=0,singlePeakRejected=0;
    uint32_t last=0,minimum=0,maximum=0,overflows=0,readErrors=0,invalidFrames=0;
    uint32_t lostEvents=0,maxBatchUs=0,maxReadGapUs=0,stackFree=0;
    float mean=0,rate=0,processingPercent=0,resultRpm=0,peakRpm=0;
    float sustainedPeakRpm=0,singlePeakRpm=0;
    RpmEstimator::Mode estimator=RpmEstimator::DEFAULT_MODE;
    StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
    float candidateRpm=0,noise=0,amplitude=0,startNoise=0,contrast=0,hysteresis=0;
    const char *stateName() const {
      if(suspended)return resting?"RESTING":"EXPORT";
      if(!ready)return "ADC ERROR";
      if(signalFault)return "CHECK SENSOR";
      switch(phase){case AnalogTachometer::Phase::Paused:return "PAUSED";
        case AnalogTachometer::Phase::Settling:return "STARTING";
        case AnalogTachometer::Phase::Ready:return "READY";
        case AnalogTachometer::Phase::Launch:return "MEASURING";default:return "REARM";}
    }
    const char *endName() const {
      switch(ended){case AnalogTachometer::End::Slowdown:return "slowdown";
        case AnalogTachometer::End::Gap:return "gap";
        case AnalogTachometer::End::Signal:return "signal_fault";
        case AnalogTachometer::End::DataLoss:return "data_loss";
        case AnalogTachometer::End::Cancel:return "cancelled";default:return "none";}
    }
  };
  bool begin(uint8_t pin,RpmEstimator::Mode estimator=RpmEstimator::DEFAULT_MODE,
             StickS3SensorProfile::Mode profile=StickS3SensorProfile::DEFAULT_MODE) {
    if(!StickS3SensorProfile::valid(uint8_t(profile)))return false;
    pin_=pin;estimator_=estimator;sensorProfile_=profile;
    storage_=static_cast<uint16_t *>(heap_caps_malloc(RING_SAMPLES*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    ring_=SampleRing(storage_,RING_SAMPLES);
    commands_=xQueueCreate(4,sizeof(Command));
    replies_=xQueueCreate(4,sizeof(Reply));
    events_=xQueueCreate(16,sizeof(Snapshot));
    if(!commands_ || !replies_ || !events_)return false;
    return xTaskCreatePinnedToCore(taskEntry,"optical",8192,this,3,&task_,0)==pdPASS;
  }
  // Preflight may veto before enqueueing or stopping the ADC. A veto changes
  // no ownership and has no completion callback. An accepted request reports
  // its reply/failure so the hook can release any established quiet window.
  using RequestHook=bool(*)(Action action,bool done,bool ok);
  void setRequestHook(RequestHook hook){hook_=hook;}
  bool request(Action action,uint32_t value=0) {
    if(hook_ && !hook_(action,false,false))return false;
    const bool ok=send(action,value);
    if(hook_)hook_(action,true,ok);
    return ok;
  }
  Snapshot snapshot() {
    portENTER_CRITICAL(&snapshotMux_);Snapshot copy=snapshot_;portEXIT_CRITICAL(&snapshotMux_);return copy;
  }
  bool takeEvent(Snapshot &event){return events_ && xQueueReceive(events_,&event,0)==pdTRUE;}
  const uint16_t *storage() const{return storage_;}
  // Recorder markers describe the first acquired batch consumed after each UI
  // boundary (about 5 ms resolution). They do not invoke the Waveshare's
  // empirically measured 2.5-14 ms electrical blanking on a different board.
  void displayBoundary(bool start) {
    portENTER_CRITICAL(&marksMux_);pendingMarks_|=start?0x8000:0x4000;portEXIT_CRITICAL(&marksMux_);
  }
private:
  RequestHook hook_=nullptr;
  bool send(Action action,uint32_t value) {
    if(!task_)return false;
    const Command command{action,value,++requestId_};
    if(xQueueSend(commands_,&command,pdMS_TO_TICKS(100))!=pdTRUE)return false;
    const TickType_t started=xTaskGetTickCount(),timeout=pdMS_TO_TICKS(2000);
    Reply reply{};
    while(xTaskGetTickCount()-started<timeout) {
      if(xQueueReceive(replies_,&reply,pdMS_TO_TICKS(20))==pdTRUE && reply.id==command.id)return reply.ok;
    }
    return false;
  }
  struct Command {Action action;uint32_t value;uint32_t id;};
  struct Reply {uint32_t id;bool ok;};
  uint16_t *storage_=nullptr;
  SampleRing ring_{nullptr,0};
  AnalogTachometer detector_;
  RpmEstimator::Mode estimator_=RpmEstimator::DEFAULT_MODE;
  StickS3SensorProfile::Mode sensorProfile_=StickS3SensorProfile::DEFAULT_MODE;
  adc_continuous_handle_t adc_=nullptr;
  QueueHandle_t commands_=nullptr,replies_=nullptr,events_=nullptr;
  TaskHandle_t task_=nullptr;
  portMUX_TYPE snapshotMux_=portMUX_INITIALIZER_UNLOCKED;
  portMUX_TYPE overflowMux_=portMUX_INITIALIZER_UNLOCKED;
  portMUX_TYPE marksMux_=portMUX_INITIALIZER_UNLOCKED;
  Snapshot snapshot_;
  uint32_t requestId_=0,overflows_=0,seenOverflows_=0,readErrors_=0,invalidFrames_=0,lostEvents_=0;
  uint16_t pendingMarks_=0;
  uint8_t pin_=1,channel_=0;
  bool ready_=false,suspended_=false,idlePaused_=false;
  bool checkpoint_=false;
  HistoryCheckpoint checkpointResult_;
  esp_err_t error_=ESP_OK;
  uint64_t samples_=0,sum_=0;
  uint64_t burstStartHostUs_=0,burstEndHostUs_=0;
  uint32_t count_=0,minimum_=4095,maximum_=0,last_=0,maxBatchUs_=0,maxReadGapUs_=0;
  int64_t windowStart_=0,lastRead_=0,lastPublish_=0,retryAt_=0;
  uint64_t processingUs_=0;
  float mean_=0,rate_=0,processingPercent_=0;
  uint32_t displayMin_=0,displayMax_=0,previousRevision_=0;
  AnalogTachometer::Phase previousPhase_=AnalogTachometer::Phase::Paused;
  bool previousFault_=false;
  static void taskEntry(void *self){static_cast<StickS3Acquisition *>(self)->run();}
  static bool IRAM_ATTR overflow(adc_continuous_handle_t,const adc_continuous_evt_data_t *,void *self) {
    auto &owner=*static_cast<StickS3Acquisition *>(self);
    portENTER_CRITICAL_ISR(&owner.overflowMux_);++owner.overflows_;portEXIT_CRITICAL_ISR(&owner.overflowMux_);
    return false;
  }
  uint32_t overflowCount() {
    portENTER_CRITICAL(&overflowMux_);const uint32_t copy=overflows_;portEXIT_CRITICAL(&overflowMux_);return copy;
  }
  uint16_t takeMarks() {
    portENTER_CRITICAL(&marksMux_);const uint16_t copy=pendingMarks_;pendingMarks_=0;portEXIT_CRITICAL(&marksMux_);return copy;
  }
  void resetWindow() {
    count_=0;sum_=0;minimum_=4095;maximum_=0;processingUs_=0;
    windowStart_=esp_timer_get_time();lastRead_=0;
  }
  void discontinuity() {ring_.discontinuity();detector_.dataLoss();takeMarks();resetWindow();}
  void closeAdc() {
    if(adc_) {
      if(ready_)adc_continuous_stop(adc_);
      adc_continuous_deinit(adc_);adc_=nullptr;
    }
    ready_=false;
  }
  bool initAdc() {
    adc_unit_t unit;adc_channel_t channel;
    error_=adc_continuous_io_to_channel(pin_,&unit,&channel);
    if(error_!=ESP_OK || unit!=ADC_UNIT_1 || pin_<1 || pin_>10) {
      error_=ESP_ERR_INVALID_ARG;return false;
    }
    channel_=uint8_t(channel);
    gpio_set_direction(static_cast<gpio_num_t>(pin_),GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(pin_),GPIO_FLOATING);
    adc_continuous_handle_cfg_t handle={};
    handle.max_store_buf_size=32768;handle.conv_frame_size=1024;
    error_=adc_continuous_new_handle(&handle,&adc_);
    adc_digi_pattern_config_t pattern={};
    pattern.atten=ADC_ATTEN_DB_12;pattern.channel=channel_;
    pattern.unit=ADC_UNIT_1;pattern.bit_width=12;
    adc_continuous_config_t config={};
    config.sample_freq_hz=SAMPLE_HZ;config.conv_mode=ADC_CONV_SINGLE_UNIT_1;
    config.format=ADC_DIGI_OUTPUT_FORMAT_TYPE2;config.pattern_num=1;config.adc_pattern=&pattern;
    if(error_==ESP_OK)error_=adc_continuous_config(adc_,&config);
    adc_continuous_evt_cbs_t callbacks={};callbacks.on_pool_ovf=overflow;
    if(error_==ESP_OK)error_=adc_continuous_register_event_callbacks(adc_,&callbacks,this);
    if(error_==ESP_OK)error_=adc_continuous_start(adc_);
    ready_=error_==ESP_OK;
    if(!ready_)closeAdc();
    seenOverflows_=overflowCount();resetWindow();return ready_;
  }
  bool recover() {
    discontinuity();closeAdc();
    const bool ok=initAdc();retryAt_=esp_timer_get_time()+1000000;return ok;
  }
  void process(const uint8_t *buffer,uint32_t bytes) {
    // Reject malformed/foreign frames as sample loss, rather than silently
    // compressing timestamps by skipping samples and inventing faster RPM.
    if(!bytes || bytes%SOC_ADC_DIGI_RESULT_BYTES){++invalidFrames_;discontinuity();return;}
    for(uint32_t i=0;i<bytes;i+=SOC_ADC_DIGI_RESULT_BYTES) {
      const auto *sample=reinterpret_cast<const adc_digi_output_data_t *>(buffer+i);
      if(sample->type2.unit!=0 || sample->type2.channel!=channel_) {
        ++invalidFrames_;discontinuity();return;
      }
    }
    const int64_t started=esp_timer_get_time();
    const uint32_t attempts=detector_.attempts,revision=detector_.resultRevision;
    uint16_t marks=takeMarks();
    for(uint32_t i=0;i<bytes;i+=SOC_ADC_DIGI_RESULT_BYTES) {
      const uint16_t value=reinterpret_cast<const adc_digi_output_data_t *>(buffer+i)->type2.data;
      ring_.record(value|marks);marks=0;
      detector_.feed(value);
      last_=value;sum_+=value;++count_;++samples_;
      minimum_=std::min(minimum_,uint32_t(value));maximum_=std::max(maximum_,uint32_t(value));
    }
    // Map optical sample time to the consumed DMA frame's host clock once,
    // preserving event times across later flash/export gaps. Alignment is
    // approximate at DMA-frame/IMU-poll resolution, not a hardware sync pulse.
    if(detector_.attempts!=attempts && detector_.nowUs>=detector_.burstStartUs)
      burstStartHostUs_=uint64_t(lastRead_)-(detector_.nowUs-detector_.burstStartUs);
    if(detector_.resultRevision!=revision && detector_.nowUs>=detector_.burstEndUs)
      burstEndHostUs_=uint64_t(lastRead_)-(detector_.nowUs-detector_.burstEndUs);
    const uint32_t duration=uint32_t(esp_timer_get_time()-started);
    processingUs_+=duration;maxBatchUs_=std::max(maxBatchUs_,duration);
  }
  void command(const Command &c) {
    bool ok=true;
    switch(c.action) {
      case Action::Estimator:
      case Action::SensorProfile:
        if(!checkpoint_ || !suspended_ || !(c.action==Action::Estimator?
          RpmEstimator::valid(c.value):StickS3SensorProfile::valid(c.value))){ok=false;break;}
        if(c.action==Action::Estimator)estimator_=RpmEstimator::Mode(c.value);
        else {sensorProfile_=StickS3SensorProfile::Mode(c.value);StickS3SensorProfile::configure(detector_,sensorProfile_);}
        // Clear the live number until the next pull; do not reinterpret history.
        checkpointResult_.valid=false;checkpointResult_.rpm=checkpointResult_.peak=0;
        checkpointResult_.three=checkpointResult_.single=0;
        checkpointResult_.threeAmplitude=checkpointResult_.singleAmplitude=0;
        ++checkpointResult_.revision;
        break;
      case Action::Reset:discontinuity();break;
      case Action::Pin:
        if(c.value<1 || c.value>10){ok=false;break;}
        closeAdc();pin_=c.value;suspended_=idlePaused_=checkpoint_=false;discontinuity();ok=initAdc();break;
      case Action::Suspend:
        if(!ready_ || suspended_){ok=false;break;}
        error_=adc_continuous_stop(adc_);ok=error_==ESP_OK;
        if(ok){ready_=false;suspended_=true;}
        break;
      case Action::IdleCheckpointBegin:
        if(!ready_){ok=false;break;}
        [[fallthrough]];
      case Action::ShutdownBegin:
        // A verified Ready idle pause already owns a stopped ADC and checkpoint.
        // Convert it into ordinary shutdown ownership without restarting ADC.
        if(c.action==Action::ShutdownBegin && idlePaused_ && checkpoint_ && adc_ && detector_.launches==c.value) {
          idlePaused_=false;break;
        }
        // Recheck on the detector's owner: a burst can start after the UI
        // reads its snapshot. Never let an idle shutdown interrupt that burst.
        // Ready can already hold the first strong marks of an unconfirmed pull.
        if(suspended_ || detector_.phase==AnalogTachometer::Phase::Launch || detector_.launches!=c.value ||
           (detector_.phase==AnalogTachometer::Phase::Ready && !detector_.historyCheckpointIdle())){ok=false;break;}
        if(ready_){error_=adc_continuous_stop(adc_);ok=error_==ESP_OK;}
        if(ok){checkpointResult_=HistoryCheckpoint(detector_);ready_=false;suspended_=true;checkpoint_=adc_!=nullptr;}
        break;
      case Action::IdlePause:
      case Action::HistoryIdleCheckpointBegin:
        // Automatic coalesced history saves may flush after Ready. Recheck
        // candidates and the accepted counter on the ADC owner before stopping.
        if(!ready_ || suspended_ || !HistoryCheckpoint::allowedIdle(detector_,c.value) ||
           (c.action==Action::IdlePause && detector_.phase!=AnalogTachometer::Phase::Ready)){ok=false;break;}
        error_=adc_continuous_stop(adc_);ok=error_==ESP_OK;
        if(ok){checkpointResult_=HistoryCheckpoint(detector_);ready_=false;suspended_=checkpoint_=true;
          idlePaused_=c.action==Action::IdlePause;}
        break;
      case Action::Resume:
        if(!suspended_ || !adc_){ok=false;break;}
        suspended_=idlePaused_=checkpoint_=false;discontinuity();
        error_=adc_continuous_flush_pool(adc_);
        if(error_==ESP_OK)error_=adc_continuous_start(adc_);
        ready_=error_==ESP_OK;ok=ready_;seenOverflows_=overflowCount();
        break;
      case Action::CheckpointBegin:
        if(!ready_ || suspended_ || !HistoryCheckpoint::allowed(detector_)){ok=false;break;}
        error_=adc_continuous_stop(adc_);ok=error_==ESP_OK;
        if(ok){checkpointResult_=HistoryCheckpoint(detector_);ready_=false;suspended_=checkpoint_=true;}
        break;
      case Action::IdleResume:
        if(!idlePaused_){ok=false;break;}
        [[fallthrough]];
      case Action::CheckpointEnd:
        if(!checkpoint_ || !suspended_ || !adc_){ok=false;break;}
        checkpoint_=suspended_=idlePaused_=false;
        ring_.discontinuity();checkpointResult_.restore(detector_);takeMarks();resetWindow();
        error_=adc_continuous_flush_pool(adc_);
        if(error_==ESP_OK)error_=adc_continuous_start(adc_);
        ready_=error_==ESP_OK;ok=ready_;seenOverflows_=overflowCount();
        break;
    }
    publish(true);
    const Reply reply{c.id,ok};
    if(xQueueSend(replies_,&reply,0)!=pdTRUE)++lostEvents_;
  }
  void publish(bool force=false) {
    const int64_t now=esp_timer_get_time();
    const bool changed=detector_.resultRevision!=previousRevision_ || detector_.phase!=previousPhase_ || detector_.signalFault!=previousFault_;
    if(!force && !changed && now-lastPublish_<10000)return;
    Snapshot s;
    s.phase=detector_.phase;s.ended=detector_.ended;s.ready=ready_;s.suspended=suspended_;s.resting=idlePaused_;
    s.enabled=detector_.enabled;s.valid=detector_.resultValid;s.signalFault=detector_.signalFault;
    s.opticalIdle=detector_.historyCheckpointIdle();
    s.pin=pin_;s.error=error_;s.samples=samples_;s.detectorUs=detector_.nowUs;
    s.ringCount=ring_.count();s.ringFirst=ring_.first();
    s.burstStartHostUs=burstStartHostUs_;s.burstEndHostUs=burstEndHostUs_;
    s.revision=detector_.resultRevision;s.launches=detector_.launches;s.revolutions=detector_.revolutions;
    s.edges=detector_.opticalEdges;s.weak=detector_.weakWindows;s.slow=detector_.slowWindows;
    s.shape=detector_.rejectedShape;s.inconsistent=detector_.inconsistentWindows;
    s.rearmExtensions=detector_.rearmExtensions;s.rearmMs=detector_.rearmMs();
    s.last=last_;s.mean=mean_;s.rate=idlePaused_?0:rate_;s.minimum=displayMin_;s.maximum=displayMax_;
    s.processingPercent=idlePaused_?0:processingPercent_;s.overflows=overflowCount();s.readErrors=readErrors_;
    s.invalidFrames=invalidFrames_;s.lostEvents=lostEvents_;s.maxBatchUs=maxBatchUs_;
    s.maxReadGapUs=maxReadGapUs_;s.stackFree=uxTaskGetStackHighWaterMark(nullptr);
    s.estimator=estimator_;s.sensorProfile=sensorProfile_;
    s.resultRpm=detector_.resultValid?RpmEstimator::select(estimator_,detector_.sustainedPeakRpm,detector_.singlePeakRpm):0;
    s.valid=detector_.resultValid && s.resultRpm>0;
    s.peakRpm=RpmEstimator::select(estimator_,detector_.sustainedPeakRpm,detector_.singlePeakRpm);
    s.candidateRpm=detector_.candidateRpm;
    s.sustainedPeakRpm=detector_.sustainedPeakRpm;s.singlePeakRpm=detector_.singlePeakRpm;s.singlePeakUpdates=detector_.singlePeakUpdates;s.singlePeakRejected=detector_.singlePeakRejected;
    s.noise=detector_.noiseBand;s.amplitude=RpmEstimator::select(estimator_,detector_.sustainedPeakAmplitude,detector_.singlePeakAmplitude);s.startNoise=detector_.startNoiseBand;
    s.contrast=detector_.signalContrast;s.hysteresis=detector_.hysteresis;
    portENTER_CRITICAL(&snapshotMux_);snapshot_=s;portEXIT_CRITICAL(&snapshotMux_);
    if(changed && xQueueSend(events_,&s,0)!=pdTRUE)++lostEvents_;
    previousRevision_=s.revision;previousPhase_=s.phase;previousFault_=s.signalFault;lastPublish_=now;
  }
  void run() {
    StickS3SensorProfile::configure(detector_,sensorProfile_);initAdc();publish(true);
    alignas(4) uint8_t buffer[1024];
    for(;;) {
      Command c;
      while(xQueueReceive(commands_,&c,0)==pdTRUE)command(c);
      if(!ready_) {
        if(!suspended_ && esp_timer_get_time()>=retryAt_)recover();
        publish();vTaskDelay(pdMS_TO_TICKS(10));continue;
      }
      if(overflowCount()!=seenOverflows_){recover();publish(true);continue;}
      uint32_t bytes=0;
      error_=adc_continuous_read(adc_,buffer,sizeof(buffer),&bytes,10);
      if(error_==ESP_ERR_TIMEOUT){error_=ESP_OK;publish();continue;}
      if(error_!=ESP_OK){++readErrors_;recover();publish(true);continue;}
      if(overflowCount()!=seenOverflows_){recover();publish(true);continue;}
      const int64_t now=esp_timer_get_time();
      if(lastRead_)maxReadGapUs_=std::max(maxReadGapUs_,uint32_t(now-lastRead_));
      lastRead_=now;process(buffer,bytes);
      if(overflowCount()!=seenOverflows_){recover();publish(true);continue;}
      const int64_t elapsed=esp_timer_get_time()-windowStart_;
      if(elapsed>=500000) {
        mean_=count_?float(sum_)/count_:0;rate_=count_*1000000.0f/elapsed;
        processingPercent_=float(processingUs_)*100/elapsed;
        displayMin_=minimum_;displayMax_=maximum_;resetWindow();
      }
      publish();
    }
  }
};
