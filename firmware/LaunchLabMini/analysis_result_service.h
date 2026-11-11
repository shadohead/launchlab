#pragma once
#include "analysis_mode.h"
#include "analysis_details.h"
#include "mic_trace.h"
#include "decay_record.h"
#include <atomic>
#include <new>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace AnalysisResult {
// The loop snapshots immutable readings. Only the worker touches unfinished fits.
// A release/acquire completion flag transfers ownership back to the loop.
class Service {
  struct Job {
    Decay::Session input;
    Decay::Workspace *workspace=nullptr;
    Decay::Results results;
    Decay::Curve winner;AnalysisDetails::Summary details;
    std::atomic<bool> cancel{false},done{false};int64_t lastYield=0;
  };
  static constexpr unsigned STACK_BYTES=8192;
  Job *job_=nullptr;void *stack_=nullptr;StaticTask_t *block_=nullptr;TaskHandle_t task_=nullptr;
  uint32_t launch_=0,attempted_=0;bool ready_=false,failed_=false,recordPending_=false;
  bool needsRecord_=false;
  Decay::Record::Image record_{};
  static bool proceed(void *context){
    auto &j=*static_cast<Job *>(context);if(j.cancel.load(std::memory_order_relaxed))return false;
    const auto now=esp_timer_get_time();if(now-j.lastYield>=4000){vTaskDelay(1);j.lastYield=esp_timer_get_time();}
    return !j.cancel.load(std::memory_order_relaxed);
  }
  static void entry(void *context){
    auto &j=*static_cast<Job *>(context);
    auto &workspace=*j.workspace;
    j.results=Decay::fitAllModels(j.input,workspace,{},proceed,&j);
    // The microphone snapshot is chronological. Match the retained times back
    // to its raw observations so the results graph can show the fit exclusions.
    unsigned retained=0;
    for(unsigned i=0;i<j.input.count;++i){
      auto &sample=j.input.samples[i];
      sample.excludedFromFit=true;
      if(retained<workspace.count){
        const float time=workspace.points[retained].t+j.results.acousticStart;
        if(sample.ms==uint32_t(std::lround(time*1000))){
          sample.excludedFromFit=false;
          ++retained;
        }
      }
    }
    if(j.results.best>=0 && proceed(&j)){
      Decay::makeCurve(j.results,unsigned(j.results.best),j.winner,{},proceed,&j);
      if(proceed(&j))j.details=AnalysisDetails::summarize(j.winner,j.input.launcherRpm,j.input.stopMs/1000.f);
    }
    j.done.store(true,std::memory_order_release);
    // Loop deletes the suspended worker before reclaiming its stack/TCB.
    for(;;)ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
  }
  void reap(){
    if(task_){
      vTaskDelete(task_);
      task_=nullptr;
    }
    heap_caps_free(stack_);
    stack_=nullptr;
    heap_caps_free(block_);
    block_=nullptr;
    // The graph owns its raw points and curve. Only the worker needs this buffer.
    if(job_ && job_->workspace){
      job_->workspace->~Workspace();
      heap_caps_free(job_->workspace);
      job_->workspace=nullptr;
    }
  }
  void release(){
    reap();
    if(job_){
      job_->~Job();
      heap_caps_free(job_);
      job_=nullptr;
    }
    ready_=false;
  }
public:
  bool busy()const{return task_!=nullptr;}
  bool ready()const{return ready_;}
  bool failed()const{return failed_;}
  const Decay::Results *results()const{return ready_?&job_->results:nullptr;}
  const Decay::Session *input()const{return ready_?&job_->input:nullptr;}
  const Decay::Curve *winner()const{return ready_ && job_->results.best>=0?&job_->winner:nullptr;}
  const AnalysisDetails::Summary *details()const{return winner()?&job_->details:nullptr;}
  bool recordPending()const{return recordPending_;}
  const Decay::Record::Image &record()const{return record_;}
  void recordHandled(){recordPending_=false;}
  // Called on every loop, including outside Analysis, so cancelled workers are reaped.
  bool tick(bool open,const AnalysisMode::Session &s,const MicTrace::Trace *trace){
    bool changed=false;
    if(job_ && (!open || s.number()!=launch_ || s.running())){
      job_->cancel.store(true,std::memory_order_relaxed);
      if(!task_ || job_->done.load(std::memory_order_acquire)){release();changed=true;}
    }
    if(task_ && job_->done.load(std::memory_order_acquire)){
      reap();
      if(job_->cancel.load(std::memory_order_relaxed) || job_->results.cancelled){release();changed=true;}
      else {ready_=true;needsRecord_=true;changed=true;}
    }
    if(!open || !s.number() || s.running()){
      if(!job_){attempted_=0;failed_=false;}return changed;
    }
    if(ready_ && s.results() && needsRecord_){
      const unsigned model=unsigned(std::max(0,job_->results.best));
      Decay::Record::encode(job_->results,model,record_);
      needsRecord_=false;
      recordPending_=true;
      changed=true;
    }
    if(job_ || attempted_==s.number())return changed;
    attempted_=launch_=s.number();failed_=false;
    auto *memory=heap_caps_malloc(sizeof(Job),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(memory){
      job_=new(memory)Job;
      auto *scratch=heap_caps_malloc(sizeof(Decay::Workspace),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
      if(scratch)job_->workspace=new(scratch)Decay::Workspace;
    }
    stack_=heap_caps_malloc(STACK_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    block_=static_cast<StaticTask_t *>(heap_caps_malloc(sizeof(StaticTask_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(!job_ || !job_->workspace || !stack_ || !block_){release();failed_=true;return true;}
    auto &in=job_->input;in.launcherRpm=s.rpm();in.stopMs=uint32_t(s.elapsedUs(0)/1000);in.hasStop=s.hasManualStop();
    if(trace && trace->launch()==s.number())for(unsigned i=0;i<trace->count() && in.count<Decay::MAX_SAMPLES;++i){
      const auto rpm=trace->rawRpm(i);if(!rpm)continue;
      in.samples[in.count++]={uint32_t(std::lround(MicTrace::Trace::seconds(i)*1000)),float(rpm),trace->rejectedRpm(i)!=0};
    }
    task_=xTaskCreateStaticPinnedToCore(entry,"analysis_fit",STACK_BYTES,job_,0,static_cast<StackType_t *>(stack_),block_,1);
    if(!task_){release();failed_=true;}
    return true;
  }
};
}
