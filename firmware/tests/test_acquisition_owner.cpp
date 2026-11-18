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
  {
    // ---- ADC timing (adc_clock.h). IDF times the ADC from the APB read at
    // configure, but it samples at the APB its power lock holds once started.
    // A recovery configured while DFS had the CPU at 40 MHz and started at 80
    // got interval 25 at a 5 MHz controller: 100 kS/s, read as 50. Every
    // start re-times it to the clock it runs at.
    auto timed=[](uint32_t divider){return APB_SARADC.apb_adc_clkm_conf.clkm_div_num==divider &&
      APB_SARADC.ctrl2.timer_target==50 && APB_SARADC.ctrl2.timer_en;};
    owner.closeAdc();fakeApbHz=40000000;lockedApbHz=80000000;
    assert(owner.initAdc() && fakeApbHz==80000000 && timed(15));
    owner.publish(true);assert(owner.snapshot().retimes==1);
    // The battery lab's 40 MHz CPU: the 5 MHz controller kept (IDF alone
    // would give 2.5 MHz and interval 25).
    owner.closeAdc();lockedApbHz=40000000;
    assert(owner.initAdc() && timed(7));
    // The CPU's top frequency changed under a running ADC: Retime re-times it
    // as a sample gap, once, and the detector stays Ready.
    optical(1200000);drain();
    const unsigned disables=timerDisables;fakeApbHz=80000000;
    command(Action::Retime);assert(timed(15) && timerDisables==disables+1);
    command(Action::Retime);assert(timed(15) && timerDisables==disables+1);
    fakeApbHz=40000000;command(Action::Retime);assert(timed(7) && timerDisables==disables+2);
    optical(1200000);assert(owner.snapshot().phase==D::Phase::Ready);
    // A resume after an idle pause re-times too.
    lockedApbHz=80000000;command(Action::IdlePause,owner.snapshot().launches);assert(!owner.ready_);
    command(Action::IdleResume);assert(owner.ready_ && timed(15));
    lockedApbHz=0;
  }
  std::puts("PASS: the ADC is re-timed to the APB it runs at after every start and on Retime");
  {
    // ---- Optical marks that never become a pull (OpticalSleepGuard). Strong
    // marks in Ready, or a Launch that never ends, refuse ShutdownBegin for as
    // long as they last: through 0.20.5 a steady signal kept the unit awake
    // until flat. ShutdownForce drops them as a sample gap and stops sampling.
    optical(1200000);drain();
    const uint32_t launches=owner.snapshot().launches;
    const float kept=owner.snapshot().resultRpm;assert(owner.snapshot().valid);
    turns(8000,2);assert(owner.detector_.phase==D::Phase::Ready && !owner.detector_.historyCheckpointIdle());
    command(Action::ShutdownBegin,launches);assert(owner.ready_ && !owner.suspended_);
    command(Action::ShutdownForce,launches+1);assert(owner.ready_ && !owner.suspended_);  // a pull it has not seen
    command(Action::ShutdownForce,launches);assert(!owner.ready_ && owner.suspended_ && owner.checkpoint_);
    Snapshot s=owner.snapshot();
    if(!s.valid)std::puts("a forced shutdown in Ready erased the completed result");
    assert(s.valid && s.resultRpm==kept && s.launches==launches);
    command(Action::CheckpointEnd);assert(owner.ready_);
    optical(1200000);assert(owner.snapshot().phase==D::Phase::Ready);
    // A Launch held by steady marks: refused, then forced; no pull is counted.
    turns(8000,40);assert(owner.detector_.phase==D::Phase::Launch);
    command(Action::ShutdownBegin,launches);assert(owner.ready_);
    command(Action::ShutdownForce,launches);assert(!owner.ready_ && owner.suspended_);
    assert(owner.detector_.phase!=D::Phase::Launch && owner.snapshot().launches==launches);
    command(Action::CheckpointEnd);assert(owner.ready_);
    optical(1200000);assert(owner.snapshot().phase==D::Phase::Ready);
    turns(6000,12);optical(1200000);s=owner.snapshot();
    assert(s.launches==launches+1 && std::fabs(s.resultRpm-10000)<3);
    drain();
  }
  std::puts("PASS: a forced shutdown stops through marks that never become a pull, and a pull after it is measured");
  {
    // ---- A result's burst times belong to its revision. A burst the
    // detector loses (an ADC stall or overflow mid-pull, USB R, a forced
    // stop) or a sensor fault publishes a new revision with no burst end:
    // through 0.20.10 the event still carried the previous pull's end, and
    // the Mic RPM page followed that old spin again (end before start).
    optical(1200000);drain();
    auto result=[](uint32_t after){Snapshot e,last{};while(owner.takeEvent(e))if(e.revision!=after)last=e;return last;};
    auto fresh=[&](const char *what){
      const uint32_t revision=owner.snapshot().revision;
      turns(8000,12);optical(1200000);const Snapshot e=result(revision);
      assert(e.revision!=revision && e.valid && e.burstStartHostUs && e.burstEndHostUs>e.burstStartHostUs);
      (void)what;return e;
    };
    auto lost=[&](const char *what,const Snapshot &pull,const Snapshot &e){
      if(e.burstEndHostUs)std::printf("%s left the end of the pull before it on its result: end %llu, that pull's end %llu\n",
        what,(unsigned long long)e.burstEndHostUs,(unsigned long long)pull.burstEndHostUs);
      assert(e.revision!=pull.revision && !e.valid && !e.burstEndHostUs);
    };
    // Lost mid-burst: an overflow's recovery (outside process()), USB R, a forced stop.
    Snapshot pull=fresh("overflow");turns(8000,6);assert(owner.detector_.phase==D::Phase::Launch);
    owner.overflows_++;owner.recover();owner.publish(true);lost("an overflow mid-pull",pull,result(pull.revision));
    optical(1200000);drain();
    pull=fresh("reset");turns(8000,6);assert(owner.detector_.phase==D::Phase::Launch);
    command(Action::Reset);lost("USB R mid-pull",pull,result(pull.revision));
    optical(1200000);drain();
    pull=fresh("force");turns(8000,40);assert(owner.detector_.phase==D::Phase::Launch);
    command(Action::ShutdownForce,owner.snapshot().launches);command(Action::CheckpointEnd);assert(owner.ready_);
    lost("a forced stop mid-pull",pull,result(pull.revision));
    optical(1200000);drain();
    // A sensor fault (a second on the low rail) in Ready after a pull.
    pull=fresh("fault");optical(1200000,0);assert(owner.detector_.signalFault);
    lost("a sensor fault",pull,result(pull.revision));
    optical(1200000);drain();
    // A checkpoint keeps the result and so its times; the next pull has its own.
    pull=fresh("checkpoint");command(Action::CheckpointBegin);command(Action::CheckpointEnd);optical(20000);
    assert(owner.snapshot().revision==pull.revision && owner.snapshot().burstEndHostUs==pull.burstEndHostUs);
    const Snapshot next=fresh("next");assert(next.burstStartHostUs>pull.burstEndHostUs);
  }
  std::puts("PASS: a lost burst or a sensor fault publishes no burst end; a completed pull and a checkpoint keep theirs");
  {
    // ---- Min RPM (0.21.0). The string fed back into the launcher spins the
    // gear like a pull (owner captures, 2026-10-27: 3,515-3,991 rpm), and one
    // optical channel cannot tell its direction. A result slower than the
    // floor is measured as ever but never published: no event, no launch
    // count, no curve, and the result before it stays on screen.
    optical(1200000);drain();
    owner.setLaunchFloor(4000);
    turns(8000,12);optical(1200000);
    const Snapshot shown=owner.snapshot();assert(shown.valid && std::fabs(shown.resultRpm-7500)<3 && shown.belowMin==0);
    AnalogTachometer::Curve curve;assert(owner.curve(shown.revision,curve) && curve.count);
    const uint16_t peak=curve.peak();
    drain();
    turns(20000,12);
    // While it is measured the snapshot is live, as for any burst.
    assert(owner.detector_.phase==D::Phase::Launch && !owner.snapshot().valid);
    optical(1200000);
    Snapshot s=owner.snapshot();
    assert(owner.detector_.launches==shown.launches+1);  // the detector measured it
    if(s.launches!=shown.launches)std::puts("a result below Min RPM was published");
    assert(s.launches==shown.launches && s.revision==shown.revision && s.phase==D::Phase::Ready);
    assert(s.valid && s.resultRpm==shown.resultRpm && s.ended==shown.ended && s.burstEndHostUs==shown.burstEndHostUs);
    assert(s.belowMin==1 && std::fabs(s.belowMinRpm-3000)<3 && s.minRpm==4000);
    assert(owner.curve(s.revision,curve) && curve.peak()==peak);
    {Snapshot e;while(owner.takeEvent(e))assert(e.revision==shown.revision && e.launches==shown.launches && e.belowMin<=1);}
    // The UI's requests carry the published count: a checkpoint still opens,
    // and its restore keeps the earlier result showing.
    command(Action::HistoryIdleCheckpointBegin,s.launches);assert(owner.suspended_ && owner.checkpoint_);
    command(Action::CheckpointEnd);assert(owner.ready_);
    s=owner.snapshot();assert(s.valid && s.resultRpm==shown.resultRpm && s.revision==shown.revision);
    command(Action::IdlePause,s.launches);assert(!owner.ready_ && owner.suspended_);
    command(Action::IdleResume);assert(owner.ready_);optical(1200000);
    command(Action::ShutdownBegin,shown.launches+1);assert(owner.ready_ && !owner.suspended_);  // the detector's own count: refused
    // A pull at the floor is published as the next launch, its own curve with it.
    drain();turns(15000,12);optical(1200000);s=owner.snapshot();
    assert(s.launches==shown.launches+1 && s.revision==shown.revision+1 && std::fabs(s.resultRpm-4000)<3 && s.belowMin==1);
    assert(owner.curve(s.revision,curve) && curve.peak()==4000);
    {Snapshot e,result{};while(owner.takeEvent(e))if(e.revision==s.revision)result=e;assert(result.valid && result.launches==s.launches);}
    // A result that is not a pull (lost samples mid-burst) is published as ever.
    turns(20000,6);assert(owner.detector_.phase==D::Phase::Launch);
    owner.discontinuity(true);optical(1200000);s=owner.snapshot();
    assert(!s.valid && s.revision==shown.revision+2 && s.launches==shown.launches+1);
    // Floor 0 (String wear check measures retractions): every result again.
    owner.setLaunchFloor(0);drain();turns(20000,12);optical(1200000);s=owner.snapshot();
    assert(s.launches==shown.launches+2 && std::fabs(s.resultRpm-3000)<3 && s.belowMin==1 && s.minRpm==0);
    // Hidden again, then the floor drops: the hidden result is not revived.
    owner.setLaunchFloor(4000);turns(20000,12);optical(1200000);
    owner.setLaunchFloor(1000);optical(100000);s=owner.snapshot();
    assert(s.launches==shown.launches+2 && s.belowMin==2 && std::fabs(s.resultRpm-3000)<3);
    owner.setLaunchFloor(0);drain();
  }
  std::puts("PASS: Min RPM keeps a slower result from being published, and the counts the UI sends match");
  return 0;
}
