// The production optical owner (acquisition.h) on the synchronous host
// runtime: sample loss, checkpoint pauses and result-event delivery.
#include "fake_runtime.h"
#include "sensor_profile.h"
#include "rpm_estimator.h"
#include "sample_ring.h"
#include "history_checkpoint.h"
// Expose the owner's internals only to drive its command loop and samples.
#define private public
#include "acquisition.h"
#undef private
#include <cassert>
#include <cstdio>
#include <vector>
using Action=StickS3Acquisition::Action;using Snapshot=StickS3Acquisition::Snapshot;using D=AnalogTachometer;
static StickS3Acquisition owner;static std::array<uint16_t,4096> storage{};
static void boot() {
  owner.storage_=storage.data();owner.ring_=SampleRing(storage.data(),storage.size());
  owner.commands_=xQueueCreate(4,sizeof(StickS3Acquisition::Command));
  owner.replies_=xQueueCreate(4,sizeof(StickS3Acquisition::Reply));
  owner.events_=xQueueCreate(16,sizeof(Snapshot));owner.task_=reinterpret_cast<void*>(1);
  StickS3SensorProfile::configure(owner.detector_,StickS3SensorProfile::Mode::Standard);
  assert(owner.initAdc());owner.publish(true);
}
static void command(Action action,uint32_t value=0){owner.command({action,value,++owner.requestId_});StickS3Acquisition::Reply r;xQueueReceive(owner.replies_,&r,0);}
// Optical samples through the owner's real process(); marks are AO 1100 on
// a 1000 baseline. The clock runs at the sample rate, and on its own while
// the ADC is stopped.
static void optical(unsigned us,int value=1000) {
  if(!owner.ready_){clockUs+=us;owner.publish();return;}
  std::array<adc_digi_output_data_t,250> batch{};
  for(auto &sample:batch)sample.type2={uint16_t(value),owner.channel_,0};
  for(unsigned remaining=us/20;remaining;) {
    const auto n=std::min(remaining,unsigned(batch.size()));clockUs+=n*20;owner.lastRead_=clockUs;
    owner.process(reinterpret_cast<const uint8_t*>(batch.data()),n*SOC_ADC_DIGI_RESULT_BYTES);remaining-=n;
    owner.publish();
  }
}
static void turns(unsigned period,unsigned count){for(unsigned i=0;i<count;++i){optical(period/2,1100);optical(period-period/2);}}
static unsigned drain(Snapshot *last=nullptr){unsigned n=0;Snapshot e;while(owner.takeEvent(e)){++n;if(last)*last=e;}return n;}
int main() {
  boot();optical(1200000);drain();
  assert(owner.snapshot().phase==D::Phase::Ready);
  // ---- An overflow after a completed pull keeps the result on screen and
  // Ready, so a pull in the next second is measured.
  turns(8000,12);optical(1200000);
  Snapshot s=owner.snapshot();assert(s.launches==1 && s.valid && std::fabs(s.resultRpm-7500)<3);
  owner.overflows_++;owner.recover();owner.publish(true);
  s=owner.snapshot();
  if(!s.valid)std::puts("overflow after a pull erased the result");
  assert(s.valid && std::fabs(s.resultRpm-7500)<3 && s.phase==D::Phase::Ready);
  turns(6000,12);optical(1200000);s=owner.snapshot();
  assert(s.launches==2 && std::fabs(s.resultRpm-10000)<3);
  // A malformed frame in the middle of a burst still discards that burst.
  turns(8000,6);assert(owner.detector_.phase==D::Phase::Launch);
  {adc_digi_output_data_t bad{};bad.type2={1000,uint8_t(owner.channel_+1),0};owner.process(reinterpret_cast<const uint8_t*>(&bad),sizeof(bad));}
  assert(owner.detector_.phase==D::Phase::Settling && owner.snapshot().launches==2);
  optical(1200000);drain();
  std::puts("PASS: lost samples keep a completed result and Ready; a burst across them is discarded");

  // ---- A flash checkpoint in REARM: its pause counts as quiet, so the next
  // pull a second after the previous one is measured as without it.
  turns(10000,12);optical(300000);assert(owner.detector_.phase==D::Phase::Hold);
  command(Action::CheckpointBegin);assert(owner.suspended_ && owner.checkpoint_);
  optical(400000);command(Action::CheckpointEnd);assert(owner.ready_);
  assert(owner.detector_.phase==D::Phase::Hold && owner.detector_.rearmMs()<=320);
  optical(400000);assert(owner.detector_.phase==D::Phase::Ready);
  turns(6000,12);optical(1200000);s=owner.snapshot();assert(s.launches==4 && std::fabs(s.resultRpm-10000)<3);
  drain();
  std::puts("PASS: a REARM checkpoint's pause counts toward REARM");

  // ---- A result event dropped by a full queue is delivered once the UI
  // drains it; the launch is never only in the counter.
  const uint32_t launches=owner.snapshot().launches;
  for(unsigned i=0;i<16;++i){Snapshot filler{};xQueueSend(owner.events_,&filler,0);}
  turns(8000,12);optical(1200000);assert(owner.snapshot().launches==launches+1);
  drain();optical(20000);
  Snapshot last{};unsigned delivered=0;Snapshot e;
  while(owner.takeEvent(e)){++delivered;if(e.launches==launches+1 && e.valid)last=e;}
  if(last.launches!=launches+1)std::puts("a dropped result event was never offered again");
  assert(delivered && last.launches==launches+1 && std::fabs(last.resultRpm-7500)<3);
  std::puts("PASS: a dropped result event is offered again");

  // ---- The queue stays full from launch N's result until burst N+1 has
  // started: the current snapshot then no longer holds N's result, so N's
  // own event must be kept, or its RPM never reaches history.
  {
    const uint32_t before=owner.snapshot().launches;
    for(unsigned i=0;i<16;++i){Snapshot filler{};xQueueSend(owner.events_,&filler,0);}
    turns(8000,12);optical(1200000);assert(owner.snapshot().launches==before+1);
    turns(6000,6);assert(owner.detector_.phase==D::Phase::Launch && !owner.snapshot().valid);
    drain();optical(20000);turns(6000,8);optical(1200000);
    std::vector<Snapshot> events;Snapshot e;
    for(unsigned guard=0;guard<64;++guard){while(owner.takeEvent(e))events.push_back(e);optical(5000);}
    bool first=false,second=false;uint32_t lastLaunches=0;
    for(const auto &ev:events) {
      assert(ev.launches>=lastLaunches);lastLaunches=ev.launches;
      if(ev.launches==before+1 && ev.valid && std::fabs(ev.resultRpm-7500)<3)first=true;
      if(ev.launches==before+2 && ev.valid && std::fabs(ev.resultRpm-10000)<3)second=true;
    }
    if(!first)std::puts("launch N's result was lost behind a full queue");
    assert(first && second);
  }
  // Two complete pulls while the UI takes nothing: both results arrive.
  {
    const uint32_t before=owner.snapshot().launches;
    for(unsigned i=0;i<16;++i){Snapshot filler{};xQueueSend(owner.events_,&filler,0);}
    turns(8000,12);optical(1200000);turns(6000,12);optical(1200000);turns(10000,12);optical(1200000);
    assert(owner.snapshot().launches==before+3);
    drain();std::vector<Snapshot> events;Snapshot e;
    for(unsigned guard=0;guard<64;++guard){while(owner.takeEvent(e))events.push_back(e);optical(5000);}
    unsigned results=0;
    for(const auto &ev:events)if(ev.valid && ((ev.launches==before+1 && std::fabs(ev.resultRpm-7500)<3) ||
      (ev.launches==before+2 && std::fabs(ev.resultRpm-10000)<3) || (ev.launches==before+3 && std::fabs(ev.resultRpm-6000)<3))){
      results|=1u<<(ev.launches-before-1);
    }
    assert(results==7);
  }
  std::puts("PASS: a completed result is kept until the queue takes it, in order");
  return 0;
}
