#include "analysis_result_service.h"
#include <cassert>
#include <iostream>

static void testEarlyReference() {
  StaminaFinish::EarlyReference ref;
  assert(ref.adjust(9999, 150, true) == 150);
  assert(ref.adjust(10000, 100, true) == 100);
  ref.adjust(10999, 170, true);  // A second redraw cannot vote twice.
  ref.adjust(11000, 140, false);
  ref.adjust(12000, 110, true);
  auto sparse = ref;
  assert(sparse.adjust(16000, 80, true) == 80);
  assert(ref.adjust(15999, 106, true) == 106);
  assert(ref.adjust(16000, 96, false) == 101);  // Median 106, spread exactly ten.
  assert(ref.adjust(17000, 108, true) == 108);
  assert(ref.adjust(18000, 60, false) == 72);

  ref = {};
  assert(ref.adjust(16000, 80, true) == 80);
  ref = {};
  for (unsigned i = 0; i < 6; ++i)
    assert(ref.adjust(10000 + i * 1000, 105 - i, true) == 105 - i);
  assert(ref.adjust(16000, 100, true) == 101.25);  // Even median is 102.5.

  ref = {};
  for (unsigned i = 0; i < 5; ++i) ref.adjust(10000 + i * 1000, 99, true);
  ref.adjust(15999, 110, true);
  assert(ref.adjust(16000, 80, true) == 80);  // The last early estimate breaks agreement.
}

static void testPaceWindows() {
  using namespace StaminaFinish;
  Scratch scratch{};
  auto sampleWindow = [&](bool falling) -> Paces {
    PaceSampler sampler(3);
    for (unsigned i = 0; i < 3; ++i) {
      const float rpm = falling ? 5000 - 1000 * i : 3000 + 1000 * i;
      sampler.add(scratch.live, i, 2000 + 3000 * i, rpm);
    }
    return scratch.live.measurements(sampler.size(), scratch.clock);
  };
  const auto falling = sampleWindow(true);
  for (const auto &p : falling) {
    assert(p.secondsPerUnit > 0);
    assert(std::abs(p.timeAt3k - 8.f) < .001f);
  }
  // Revising a trace in place can remove or restore a measurable decline.
  for (const auto &p : sampleWindow(false)) assert(!std::isfinite(p.secondsPerUnit));
  assert(sampleWindow(true)[0].secondsPerUnit == falling[0].secondsPerUnit);
  for (const auto &p : scratch.live.measurements(0, scratch.clock))
    assert(!std::isfinite(p.secondsPerUnit));

  auto model = WEIGHTS;
  for (auto &r : model.pace) {
    r.beta = {};
    r.beta[0] = 140.;
  }
  auto finish = [&](unsigned ms, const Paces &live, const Paces &opening) {
    return blendFinish({ms, 7000, 0, 0}, 100., live, opening, model);
  };
  assert(finish(10999, falling, falling) == 100.);
  assert(std::abs(finish(11000, falling, falling) - 104.) < .001);
  assert(std::abs(finish(14999, falling, falling) - 116.) < .001);
  assert(std::abs(finish(15000, falling, falling) - 120.) < .001);
  assert(std::abs(finish(20000, Paces{}, falling) - 120.) < .001);
  assert(finish(20000, Paces{}, Paces{}) == 100.);
}

static void testLiveAudioForecast() {
  AnalysisResult::Service service;
  AnalysisMode::Session spin;
  AudioMetadata::Recorder audio;
  audio.reset(17);
  AudioMetadata::Measurement frame{180, 0, 98, 127, 20, 132, 64, 3,
                                   73, 80, 77, 76, 72, 62, 61, 60};
  for (unsigned ms : {1000, 2000, 3000, 4000}) audio.add(ms, ms, frame);
  audio.add(9800, 10020, frame);
  spin.begin(17, true, 4300, 1000000);
  service.tick(true, spin, nullptr, 11000000, &audio);
  assert(std::abs(service.forecast(17).seconds - 129.80848f) < .001f); // Fifth bin still in flight.
  service.tick(true, spin, nullptr, 12000000, &audio);
  assert(std::abs(service.forecast(17).seconds - 41.78203f) < .001f); // Frozen model reference.
  service.tick(true, spin, nullptr, 31000000, &audio);
  assert(std::abs(service.forecast(17).seconds - 43.61843f) < .001f);
  spin.begin(18, true, 4300, 32000000);
  service.tick(true, spin, nullptr, 42000000, &audio);
  assert(std::abs(service.forecast(18).seconds - 129.80848f) < .001f); // Previous launch's recorder.
}

int main(){
  testLiveAudioForecast();
  testEarlyReference();
  testPaceWindows();
  // The frozen stamina fallback starts at ten seconds with no mic. It keeps
  // ticking through silence and the last live snapshot survives manual stop.
  {
    AnalysisResult::Service service;
    AnalysisMode::Session spin;
    spin.begin(41,true,4300,1000000);
    service.tick(true,spin,nullptr,10999000);
    assert(!std::isfinite(service.forecast(41).seconds));
    service.tick(true,spin,nullptr,11000000);
    auto first=service.forecast(41);
    assert(first.atMs==10000 && first.rawCount==0 && std::abs(first.seconds-129.80848f)<.001f);
    service.tick(true,spin,nullptr,11999000);
    assert(service.forecast(41).atMs==10000);
    service.tick(true,spin,nullptr,31000000);
    const auto last=service.forecast(41);
    assert(last.atMs==30000 && last.seconds!=first.seconds);
    spin.stop(31500000);
    assert(service.forecast(41).seconds==last.seconds);
    spin.begin(42,true,4300,32000000);
    service.tick(true,spin,nullptr,32000000);
    assert(!std::isfinite(service.forecast(42).seconds) && !std::isfinite(service.forecast(41).seconds));
  }
  // Generate a spin on the shared drag shape, starting below the IR launch RPM.
  // A harmonic spike is withheld; its raw value is still in the snapshot.
  {
    static Decay::Session spin;
    static Decay::Workspace scratch;
    Decay::Curve curve;
    spin.launcherRpm=8000;
    using namespace Decay::FixedRiccati;
    const double q=std::sqrt(4*A*C-B*B);
    const double intercept=std::atan((2*C*6+B)/q), scale=1.15;
    for(unsigned i=0;i<60;++i){
      const double t=1+i*.25;
      const double x=(q*std::tan(intercept-scale*q*t/2)-B)/(2*C);
      spin.samples[spin.count++]={uint32_t(t*1000),float(std::round(1000*x))};
    }
    spin.samples[22].rpm+=1700;
    spin.samples[1].rejected=true;
    spin.samples[2].recovered=true;
    spin.samples[3].rejected=true;spin.samples[3].recovered=true;
    spin.stopMs=22000;
    auto fits=Decay::fitLiveModels(spin,scratch,curve);
    const double expected=(intercept-std::atan((2*C*TOPPLE+B)/q))*2/(scale*q);
    assert(fits.fixedRiccati && fits.sampleCount==59 && fits.sourceCount==60);
    assert(fits.support.original==57 && fits.support.accepted==56);
    assert(std::fabs(curve.end-expected)<1);
    assert(std::fabs(curve.rpm[0]-6000)<50 && std::fabs(curve.rpm[Decay::CURVE_POINTS-1]-578)<1);
    spin.hasStop=true;spin.stopMs=80000;
    const auto stopped=Decay::fitStoppedModels(spin,scratch,curve);
    assert(stopped.fixedRiccati && stopped.manualAnchor && stopped.opticalAnchor);
    assert(stopped.sampleCount==59 && curve.rpm[0]==spin.launcherRpm && curve.end==80);
    assert(curve.joinTime==stopped.lastTime && curve.rpm[curve.joinIndex]==stopped.tailRpm);
    assert(curve.rpm[Decay::CURVE_POINTS-1]==0 && stopped.models[stopped.best].stopSeconds==80);
    Decay::Record::Image packed;
    Decay::Record::encode(stopped,stopped.best,packed);
    Decay::Results unpacked;unsigned slot;
    assert(Decay::Record::decode(packed,unpacked,slot) && unpacked.fixedRiccati);
    Decay::Curve rebuilt;Decay::makeRiccatiCurve(unpacked,rebuilt);
    for(unsigned i=0;i<Decay::CURVE_POINTS;++i)assert(rebuilt.rpm[i]==curve.rpm[i]);
    spin.hasStop=false;
    for(unsigned i=0;i<spin.count;++i)spin.samples[i].rpm=4000;
    auto flat=Decay::fitLiveModels(spin,scratch,curve);
    assert(!flat.fixedRiccati && !flat.models[unsigned(Decay::Model::Riccati)].valid);
  }
  // A sparse, discontinuous Wizard Rod LF trace declines the fixed shape
  // but still produces an endpoint-anchored, validated Free Riccati curve.
  {
    static Decay::Session spin;
    static Decay::Workspace work;
    Decay::Curve curve;
    const Decay::Sample trace[]={{3900,4980},{7500,2970},{7700,2910},{18000,4680},
      {18500,4560},{18700,4530},{20000,4260},{20500,4140},{20700,4140},{21300,2640},
      {21900,2580},{22100,2610},{22600,2490},{22800,2520},{23300,2400},{24000,2310}};
    spin.hasStop=true;spin.stopMs=43074;spin.launcherRpm=7765.985f;
    for(const auto &p:trace)spin.samples[spin.count++]=p;
    const auto fits=Decay::fitStoppedModels(spin,work,curve);
    assert(!fits.fixedRiccati && fits.best==int(Decay::Model::Riccati));
    assert(fits.models[fits.best].validation.valid && curve.rpm[Decay::CURVE_POINTS-1]==0);
    for(unsigned i=0;i<Decay::MODEL_COUNT;++i)
      if(i!=unsigned(fits.best))assert(!fits.models[i].valid);
    spin.count=2;
    const auto sparse=Decay::fitStoppedModels(spin,work,curve);
    assert(sparse.endpointEstimate && sparse.fixedRiccati && !sparse.validationPossible);
    assert(sparse.sampleCount==0 && work.count==0 && curve.rpm[0]==spin.launcherRpm && curve.end==43.074f);
  }
  AnalysisResult::Service service;AnalysisMode::Session session;
  uint16_t slots[3000]={},raw[3000]={};uint8_t recovery[(3000+7)/8]={},confidence[3000]={};
  MicTrace::Trace trace(slots,3000,raw,recovery,confidence);
  session.begin(1,true,10000,1000000);trace.begin(1,10000);
  for(unsigned i=0;i<150;++i){MicTrace::Step s;s.launch=1;s.index=i;s.rpm=9000-i*20;s.rawRpm=s.rpm+10;
    if(i==10){s.recovered=true;s.pitchConfidence=192;}
    if(i==11)s.recovered=true; // Confirmed acquisition recovery keeps confidence zero.
    trace.add(s);}
  assert(trace.rawRpm(10)==8810 && trace.rejectedRpm(10)==0);
  uint64_t now=17000000; // 16 seconds elapsed; the trace contains a fresh reading.
  assert(service.tick(true,session,&trace,now) && service.busy());
  MicTrace::Step returned;returned.launch=1;returned.index=152;returned.rpm=5960;returned.rawRpm=5960;
  trace.add(returned);
  for(unsigned wait=0;service.busy() && wait<10000;++wait){service.tick(true,session,&trace,now);vTaskDelay(1);}
  assert(service.matches(session) && service.winner() && service.input()->count==150);
  assert(service.input()->samples[10].recovered && service.input()->samples[10].pitchConfidence==192);
  assert(service.input()->samples[11].recovered && !service.input()->samples[11].pitchConfidence);
  assert(!service.input()->samples[12].recovered && !service.input()->samples[12].pitchConfidence);
  auto published=service.publishedFinish(1);
  const auto shown=service.plot(session);
  assert(shown.fit==service.results() && shown.input==service.input() && shown.curve==service.winner());
  const auto oldCurve=*shown.curve;
  const auto support=service.publishedSupport(1);
  assert(support.original && support.accepted==support.original && !service.publishedSupport(2).original);
  assert(std::isfinite(published) && !std::isfinite(service.publishedFinish(2)));
  assert(!service.tick(true,session,&trace,now+1999999));
  assert(service.tick(true,session,&trace,now+2000000) && service.busy());
  const auto refitting=service.plot(session);
  assert(refitting.fit==shown.fit && refitting.input==shown.input && refitting.curve==shown.curve);
  assert(refitting.input->count==150 && refitting.curve->end==oldCurve.end);
  for(unsigned i=0;i<Decay::CURVE_POINTS;++i)assert(refitting.curve->rpm[i]==oldCurve.rpm[i]);
  assert(!service.matches(session) && !service.results() && !service.input()); // No extra exporter publication.
  assert(service.publishedFinish(1)==published);
  assert(service.publishedSupport(1).original==support.original);
  for(unsigned wait=0;service.busy() && wait<10000;++wait){service.tick(true,session,&trace,now+2000000);vTaskDelay(1);}
  assert(service.input()->count==151 && !service.tick(true,session,&trace,now+7000000));
  assert(service.plot(session).input==service.input() && service.plot(session).input->count==151 && allocations==1);
  // Allocation/task failures during a refit preserve the displayed snapshot.
  for(unsigned failure=1;failure<=5;++failure){
    auto prior=service.plot(session);
    returned.index=uint16_t(152+failure);trace.add(returned);
    failOnAllocation=failure<5?failure:0;failTask=failure==5;
    assert(service.tick(true,session,&trace,now+uint64_t(2+2*failure)*1000000) && !service.busy());
    assert(service.plot(session).input==prior.input && service.plot(session).curve==prior.curve && allocations==1);
    assert(service.publishedFinish(1)==prior.curve->end);
    failOnAllocation=0;failTask=false;
  }
  // Keep the rest of the fixture's raw trace unchanged.
  trace.freeze(15500);
  session.stop(101000000);service.tick(true,session,&trace);assert(service.busy());
  assert(!service.plot(session).curve); // A live forecast is not a completed post-stop curve.
  unsigned tries=0;while(service.busy() && ++tries<10000){service.tick(true,session,&trace);vTaskDelay(1);}
  assert(service.ready() && !service.busy() && !service.recordPending());
  assert(allocations==1); // Only the displayable result remains after fitting.
  session.nextResult();service.tick(true,session,&trace);assert(service.recordPending());
  assert(service.input()->count==151 && service.input()->samples[10].rpm==8810);
  Decay::Results saved;unsigned selected=9;assert(Decay::Record::decode(service.record(),saved,selected) && selected==unsigned(std::max(0,saved.best)));
  assert(saved.opticalAnchor && saved.manualAnchor && !saved.models[0].stopErrorValid);
  service.recordHandled();session.nextResult();service.tick(true,session,&trace);assert(!service.recordPending());
  assert(service.winner() && service.details()->complete);
  service.tick(false,session,&trace);assert(!service.ready() && allocations==0);
  // Exit cancels a running calculation and safely reclaims the job, stack and TCB.
  service.tick(true,session,&trace);service.tick(false,session,&trace);
  tries=0;while(service.busy() && ++tries<10000){service.tick(false,session,&trace);vTaskDelay(1);}assert(allocations==0);
  failAllocation=true;service.tick(true,session,&trace);assert(service.failed() && !service.busy() && allocations==0);
  assert(service.ready() && service.matches(session) && service.winner() && service.details()->complete);
  assert(service.results()->endpointEstimate && !service.input() && service.plot(session).curve==service.winner());
  failAllocation=false;
  for(unsigned failure=1;failure<=4;++failure){
    service.tick(false,session,&trace);
    failOnAllocation=failure;
    service.tick(true,session,&trace);
    assert(service.failed() && !service.busy() && allocations==0);
    assert(service.ready() && service.results()->endpointEstimate && service.winner()->end==100);
    assert(service.winner()->rpm[0]==session.rpm() && service.winner()->rpm[Decay::CURVE_POINTS-1]==0);
    session.nextResult();service.tick(true,session,&trace);
    assert(service.recordPending() && Decay::Record::decode(service.record(),saved,selected) && saved.endpointEstimate);
    service.recordHandled();
    failOnAllocation=0;
  }
  service.tick(false,session,&trace);failTask=true;service.tick(true,session,&trace);assert(service.failed() && allocations==0);failTask=false;
  assert(service.ready() && service.results()->endpointEstimate && service.details()->complete);
  service.tick(false,session,&trace);assert(!service.ready() && !service.plot(session).curve);
  service.tick(false,session,&trace);session.begin(2,true,9000,2000000);session.tick(302000000);session.nextResult();trace.begin(2,9000);
  service.tick(true,session,&trace);tries=0;while(service.busy() && ++tries<10000){service.tick(true,session,&trace);vTaskDelay(1);}
  assert(service.ready() && !service.input()->hasStop && !service.results()->actualStopValid);
  assert(!service.results()->endpointEstimate && !service.winner());
  service.tick(false,session,&trace);assert(allocations==0);
  // The completed worker keeps raw observations, but a shape-only estimate
  // must not describe any of them as fitted/validated mic data.
  session.begin(3,true,7300,1000000);trace.begin(3,7300);
  for(unsigned i=0;i<2;++i){MicTrace::Step s;s.launch=3;s.index=i+10;s.rawRpm=s.rpm=4000;trace.add(s);}
  session.stop(83000000);service.tick(true,session,&trace);
  for(unsigned wait=0;service.busy() && wait<10000;++wait){service.tick(true,session,&trace);vTaskDelay(1);}
  assert(service.ready() && service.results()->endpointEstimate && service.details()->endpointEstimate);
  assert(service.input()->count==2 && service.results()->sampleCount==0 && service.winner()->end==82);
  for(unsigned i=0;i<service.input()->count;++i)assert(service.input()->samples[i].excludedFromFit);
  service.tick(false,session,&trace);assert(allocations==0);
  // Exiting or launching again while a replacement runs discards the retained
  // plot immediately, then reclaims the cancelled worker's separate snapshot.
  for(bool newLaunch:{false,true}){
    AnalysisResult::Service changing;AnalysisMode::Session current;
    current.begin(20,true,10000,1000000);trace.begin(20,10000);
    for(unsigned i=0;i<150;++i){MicTrace::Step s;s.launch=20;s.index=uint16_t(i);s.rawRpm=s.rpm=9000-i*20;trace.add(s);}
    changing.tick(true,current,&trace,17000000);
    for(unsigned wait=0;changing.busy() && wait<10000;++wait){changing.tick(true,current,&trace,17000000);vTaskDelay(1);}
    assert(changing.plot(current).curve && allocations==1);
    MicTrace::Step more;more.launch=20;more.index=152;more.rawRpm=more.rpm=5960;trace.add(more);
    assert(changing.tick(true,current,&trace,19000000) && changing.busy() && changing.plot(current).curve);
    if(newLaunch){current.begin(21,true,9000,19000000);trace.begin(21,9000);assert(!changing.plot(current).curve);}
    changing.tick(newLaunch,current,&trace,19000000);
    assert(!changing.plot(current).curve);
    for(unsigned wait=0;changing.busy() && wait<10000;++wait){changing.tick(newLaunch,current,&trace,19000000);vTaskDelay(1);}
    assert(!changing.plot(current).curve && allocations==0);
  }
  std::cout<<"PASS: asynchronous result ownership, raw evidence, selection persistence, cancellation, timeout and allocation failures\n";
}
