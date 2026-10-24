#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <cassert>
#include <iostream>
#include "../LaunchLabMini/sensor_profile.h"
#include "../LaunchLabMini/history_checkpoint.h"
#include "../LaunchLabMini/rpm_estimator.h"
namespace Original {
#include "../releases/0.6.2-sticks3-qre1113-rotation/source/analog_tachometer.h"
}
namespace BeforeGuard {
#include "../releases/0.11.2-sticks3-qre1113-scorepreview/source/analog_tachometer.h"
}
// 0.20.1 places each edge between the 100 us evaluation grid points, where
// the frozen detectors stamp the grid point after it: a stamp moves by less
// than one grid step, and a reading by less than one step of its period.
// Launches, validity, phases, revisions and rearm stay exactly equal.
static bool sameStamp(uint64_t a,uint64_t b){return (a>b?a-b:b-a)<100;}
static bool sameRpm(float a,float b) {
  if(a<=0 || b<=0)return a==b;
  return std::fabs(60000000.0f/a-60000000.0f/b)<100;
}
template<class T>static void feed(T &d,unsigned us,int v){for(unsigned n=0;n<us/20;++n)d.feed(v);}
template<class T>static void square(T &d,unsigned period,unsigned count,int amp=100) {
  for(unsigned n=0;n<count;++n){feed(d,period/2,140+amp);feed(d,period-period/2,140);}
}
static AnalogTachometer ready(){AnalogTachometer d;StickS3SensorProfile::configure(d);feed(d,1200000,140);return d;}
static Original::AnalogTachometer oldReady(){Original::AnalogTachometer d;d.minMark=80;d.edgeSwingFraction=.7f;feed(d,1200000,140);return d;}
// Feed the exact before/after production detectors together. Without a reset,
// the pulse guard may change only the alternative peak and its diagnostics.
// The two reset-recovery fixtures retain samplewise state/publication/rearm
// equality and use their generated 10-ms timing oracle for recovered RPM,
// because normalizing the swing-ring index corrects the first-window timing.
// Since 0.13.9 three boundary rules differ by design (test_rpm_hardening.cpp):
// marks stopping near the peak end the burst only when the next marks fail to
// continue it, a flat or re-accelerating pair of slow turns waits for a third
// (both at the same boundary), and strong turns after a pull extend REARM
// until its rewind is recognized. All are compared as invariants.
struct OpticalTrace {
  AnalogTachometer current;
  BeforeGuard::AnalogTachometer before;
  float recoveryOracleRpm=0;
  OpticalTrace() {
    StickS3SensorProfile::configure(current);
    before.minMark=80;before.edgeSwingFraction=.7f;
    before.singleTurnPeak=false;before.trackSingleTurnPeak=true;
    level(1200000,140);
  }
  void boundaries()const {
    // The earlier detector ended on the plain gap; this one is still waiting
    // for the marks after it. Results are compared once it ends.
    if(before.phase==BeforeGuard::AnalogTachometer::Phase::Hold &&
       (before.ended==BeforeGuard::AnalogTachometer::End::Gap ||
        before.ended==BeforeGuard::AnalogTachometer::End::Slowdown) &&
       current.phase==AnalogTachometer::Phase::Launch)return;
    // REARM after a pull can only be longer, never shorter.
    if(current.phase==AnalogTachometer::Phase::Hold && current.rewindPending) {
      assert(before.phase!=BeforeGuard::AnalogTachometer::Phase::Launch);
      assert(current.launches==before.launches && current.resultRevision==before.resultRevision);
      assert(sameRpm(current.resultRpm,before.resultRpm) && sameStamp(current.burstEndUs,before.burstEndUs));
      if(before.phase==BeforeGuard::AnalogTachometer::Phase::Hold)assert(current.rearmMs()>=before.rearmMs());
      return;
    }
    // Since 0.13.9 turns after a reset count only once the swing history is
    // back (a turn spanning the noise-to-mark hysteresis switch is mistimed),
    // so a recovered burst confirms up to two turns after the old detector.
    const bool recoveringLater=recoveryOracleRpm>0 &&
      current.phase==AnalogTachometer::Phase::Ready && before.phase==BeforeGuard::AnalogTachometer::Phase::Launch;
    if(recoveringLater){assert(current.launches==before.launches);return;}
    assert(int(current.phase)==int(before.phase) && current.resultValid==before.resultValid);
    assert(current.launches==before.launches && current.resultRevision==before.resultRevision);
    if(recoveryOracleRpm>0) {
      // Reset now normalizes the swing-ring index. The old source's first
      // recovered windows may differ before converging to generated 6000.
      // Keep samplewise state/boundary equality and use the generated 10-ms
      // waveform oracle, allowing one 100-us extraction-grid step per period.
      const float periodUs=60000000.0f/recoveryOracleRpm;
      const float rpmLow=60000000.0f/(periodUs+100),rpmHigh=60000000.0f/(periodUs-100);
      const auto generatedRpm=[&](float rpm){return rpm>=rpmLow-0.01f && rpm<=rpmHigh+0.01f;};
      if(current.peakRpm>0)assert(generatedRpm(current.peakRpm));
      if(current.resultValid)assert(generatedRpm(current.resultRpm));
      if(current.sustainedPeakRpm>0)assert(generatedRpm(current.sustainedPeakRpm));
      if(current.singlePeakRpm>0)assert(generatedRpm(current.singlePeakRpm));
    } else {
      assert(sameRpm(current.peakRpm,before.peakRpm) && sameRpm(current.resultRpm,before.resultRpm));
      assert(sameRpm(current.sustainedPeakRpm,before.sustainedPeakRpm));
    }
    if(recoveryOracleRpm>0)assert(current.burstStartUs+100>before.burstStartUs &&
      current.burstStartUs<=before.burstStartUs+uint64_t(2*60000000.0f/recoveryOracleRpm)+1000);
    else assert(sameStamp(current.burstStartUs,before.burstStartUs));
    assert(sameStamp(current.burstEndUs,before.burstEndUs));
    assert(current.rearmExtensions>=before.rearmExtensions);
    if(current.phase==AnalogTachometer::Phase::Hold)assert(current.rearmMs()>=before.rearmMs());
    else assert(current.rearmMs()==before.rearmMs());
  }
  void level(unsigned us,int value) {
    assert(us%20==0);
    for(unsigned i=0;i<us/20;i++){current.feed(value);before.feed(value);boundaries();}
  }
  void turns(unsigned period,unsigned count,int amplitude=100) {
    for(unsigned i=0;i<count;i++){level(period/2,140+amplitude);level(period-period/2,140);}
  }
  void finish(int value=140) {level(1700000,value);assert(current.launches==1 && current.resultValid);}
  void loss() {current.dataLoss();before.dataLoss();boundaries();}
};
int main() {
  auto config=ready();assert(!config.singleTurnPeak && config.trackSingleTurnPeak);
  for(unsigned tail:{1u,2u,3u}) {
    auto d=ready();auto old=oldReady();square(d,10000,12);square(old,10000,12);
    square(d,4000,tail);square(old,4000,tail);feed(d,1700000,240);feed(old,1700000,240);
    assert(d.launches==1 && d.resultValid && old.launches==1);
    assert(sameRpm(d.resultRpm,old.resultRpm) && sameStamp(d.burstEndUs,old.burstEndUs));
    assert(std::fabs(d.resultRpm-(tail<3?6000:15000))<3 && std::fabs(d.singlePeakRpm-15000)<3);
    assert(RpmEstimator::select(RpmEstimator::Mode::ThreeTurn,d.sustainedPeakRpm,d.singlePeakRpm)==d.resultRpm);
    assert(RpmEstimator::select(RpmEstimator::Mode::SingleTurn,d.sustainedPeakRpm,d.singlePeakRpm)==d.singlePeakRpm);
    const auto copy=HistoryCheckpoint(d);d.dataLoss();copy.restore(d);
    assert(d.resultValid && sameRpm(d.sustainedPeakRpm,old.resultRpm) && std::fabs(d.singlePeakRpm-15000)<3);
  }
  auto separated=ready();square(separated,10000,12);
  for(unsigned i=0;i<3;++i){square(separated,4000,1);square(separated,10000,8);}
  feed(separated,1700000,240);
  assert(separated.launches==1 && std::fabs(separated.sustainedPeakRpm-6000)<3);
  assert(std::fabs(separated.singlePeakRpm-15000)<3); // Never join three separate fast turns.
  auto ramp=ready();auto old=oldReady();square(ramp,10000,12);square(old,10000,12);
  for(unsigned p:{10000u,9600u,9200u,8800u,8400u,8000u,7600u,7200u,6800u,6400u,6000u}) {
    square(ramp,p,1);square(old,p,1);
  }
  feed(ramp,1700000,240);feed(old,1700000,240);
  const float elapsedThree=180000000.f/(6800+6400+6000);
  assert(std::fabs(ramp.resultRpm-elapsedThree)<3 && sameRpm(ramp.resultRpm,old.resultRpm));
  assert(std::fabs(ramp.singlePeakRpm-10000)<3);
  const float arithmeticMean=(60000000.f/6800+60000000.f/6400+60000000.f/6000)/3;
  assert(std::fabs(ramp.resultRpm-arithmeticMean)>20); // Time average, not mean of RPMs.
  for(int amplitude:{30,300}) {
    auto d=ready();auto baseline=oldReady();
    square(d,10000,12);square(baseline,10000,12);
    square(d,4000,1,amplitude);square(baseline,4000,1,amplitude);
    square(d,10000,8);square(baseline,10000,8);
    feed(d,1600000,140);feed(baseline,1600000,140);
    assert(d.launches==1 && sameRpm(d.sustainedPeakRpm,baseline.resultRpm) && sameStamp(d.burstEndUs,baseline.burstEndUs));
    assert(std::fabs(d.singlePeakRpm-6000)<3); // Bad single mark rejected; retain frozen three-turn behavior.
  }
  // A known regular optical turn is subdivided by an extra low notch. Both
  // apparent intervals pass ordinary shape/amplitude checks, but their sum
  // and the next recovered full interval reveal the split. Test weak-but-
  // eligible optics and the larger contrast of normal enclosure readings.
  for(int amplitude:{100,1000}) {
    OpticalTrace notch;notch.turns(15600,12,amplitude);
    notch.level(2000,140+amplitude);notch.level(2100,140);
    notch.level(3700,140+amplitude);notch.level(7800,140);
    notch.turns(15600,8,amplitude);notch.finish();
    assert(std::fabs(notch.before.singlePeakRpm-14634.146f)<3);
    assert(std::fabs(notch.current.singlePeakRpm-3846.1538f)<3);
    assert(notch.current.singlePeakRejected>notch.before.singlePeakRejected);
  }
  // Keep a 10 ms bright plateau's outer edges fixed while inserting a 1 ms dark
  // notch inside it. This gives a known 10 ms optical fundamental with an extra
  // threshold crossing, independently of any replayed estimator's answer.
  OpticalTrace plateau;plateau.turns(10000,12);
  plateau.level(1500,240);plateau.level(1000,140);
  plateau.level(2500,240);plateau.level(5000,140);
  plateau.turns(10000,8);plateau.finish();
  assert(plateau.before.singlePeakRpm>20000 && std::fabs(plateau.current.singlePeakRpm-6000)<3);
  // Split detection must not depend on the first half setting a new maximum.
  OpticalTrace priorPeak;priorPeak.turns(10000,12);priorPeak.turns(6000,1);
  priorPeak.turns(10000,8);priorPeak.turns(8000,1);priorPeak.turns(2000,1);
  priorPeak.turns(10000,8);priorPeak.finish(240);
  assert(std::fabs(priorPeak.before.singlePeakRpm-30000)<3);
  assert(std::fabs(priorPeak.current.singlePeakRpm-10000)<3);
  // In an 8 ms + 2 ms split, [10,10,8] has already corroborated the first
  // interval under unchanged three-turn rules. Reject the unconfirmed 30,000
  // RPM spike, retaining the corroborated 7,500 RPM rather than redefining
  // three-turn acceptance. This is not universal rejection of extra marks.
  OpticalTrace corroboratedHalf;corroboratedHalf.turns(10000,12);
  corroboratedHalf.turns(8000,1);corroboratedHalf.turns(2000,1);
  corroboratedHalf.turns(10000,8);corroboratedHalf.finish(240);
  assert(std::fabs(corroboratedHalf.before.singlePeakRpm-30000)<3);
  assert(std::fabs(corroboratedHalf.current.singlePeakRpm-7500)<3);
  // A speed doubling can also have two intervals summing to the old cadence.
  // When faster motion continues, preserve its maximum. Exactly two final
  // intervals summing to the preceding turn are rejected since 0.13.9: a gear
  // cannot double its speed and stop dead within one turn, and the same
  // samples are a notch splitting the final mark. A single final interval has
  // no split context and stays an arrival-checked surge.
  for(unsigned count:{1u,2u,3u,12u}) {
    OpticalTrace doubling;doubling.turns(10000,12);doubling.turns(5000,count);
    doubling.finish(240);
    assert(std::fabs(doubling.current.singlePeakRpm-(count==2?6000:12000))<3);
    if(count!=2)assert(doubling.current.singlePeakRpm==doubling.before.singlePeakRpm);
  }
  OpticalTrace continuing;continuing.turns(10000,12);continuing.turns(6000,1);
  continuing.turns(4000,1);continuing.turns(3000,8);continuing.finish(240);
  assert(std::fabs(continuing.current.singlePeakRpm-20000)<3);
  assert(continuing.current.singlePeakRpm==continuing.before.singlePeakRpm);
  // A later consistent strong three-turn window corroborates its current fast
  // interval. A subsequent malformed neighbor must not erase that evidence.
  OpticalTrace corroborated;corroborated.turns(10000,12);corroborated.turns(6800,2);
  corroborated.turns(5600,1);corroborated.level(15000,240);corroborated.level(1000,140);
  corroborated.turns(10000,8);corroborated.finish(240);
  assert(std::fabs(corroborated.current.singlePeakRpm-10714.286f)<3);
  assert(corroborated.current.singlePeakRpm==corroborated.before.singlePeakRpm);
  // The same rule preserves the initially confirmed fastest turn too.
  OpticalTrace initial;initial.turns(10000,2);initial.turns(8400,1);
  initial.level(15000,240);initial.level(1000,140);initial.turns(10000,8);initial.finish(240);
  assert(initial.current.singlePeakRpm==initial.before.singlePeakRpm);
  assert(std::fabs(initial.current.singlePeakRpm-7142.857f)<3);
  // Pending unconfirmed maxima need credible following optics. Shape failure
  // and an oversized arrival both discard them; a normal next pull recovers.
  OpticalTrace malformed;malformed.turns(10000,12);malformed.turns(4000,1);
  malformed.level(15000,240);malformed.level(1000,140);
  malformed.turns(10000,8);malformed.finish(240);
  assert(std::fabs(malformed.current.singlePeakRpm-6000)<3);
  OpticalTrace arrival;arrival.turns(10000,12);arrival.turns(4000,1);
  arrival.level(2000,440);arrival.level(2000,140);arrival.turns(10000,8);arrival.finish(240);
  assert(std::fabs(arrival.current.singlePeakRpm-6000)<3);
  OpticalTrace reset;reset.turns(10000,12);reset.turns(4000,1);reset.level(1000,240);
  reset.recoveryOracleRpm=6000;reset.loss();assert(reset.current.singlePeakRpm==0 && !reset.current.resultValid);
  reset.level(1300000,140);reset.turns(10000,12);reset.finish(240);
  assert(std::fabs(reset.current.singlePeakRpm-6000)<3);
  OpticalTrace paused;paused.turns(10000,12);paused.turns(4000,1);paused.level(1000,240);
  paused.recoveryOracleRpm=6000;paused.current.setEnabled(false);paused.before.setEnabled(false);paused.boundaries();
  paused.level(1300000,140);paused.current.setEnabled(true);paused.before.setEnabled(true);
  paused.boundaries();paused.level(1300000,140);paused.turns(10000,12);paused.finish(240);
  assert(std::fabs(paused.current.singlePeakRpm-6000)<3);
  // Finalize terminal evidence before launch/revision publication. Subsequent
  // Hold samples or checkpoint restore must never revise the saved peak.
  OpticalTrace terminal;terminal.turns(10000,12);terminal.turns(4000,1);
  while(!terminal.current.launches)terminal.level(100,240);
  assert(std::fabs(terminal.current.singlePeakRpm-15000)<3);
  const float published=terminal.current.singlePeakRpm;
  const auto publishedRevision=terminal.current.resultRevision;
  terminal.turns(2000,12);terminal.level(500000,140);
  assert(terminal.current.launches==1 && terminal.current.resultRevision==publishedRevision);
  assert(terminal.current.singlePeakRpm==published);
  const HistoryCheckpoint savedTerminal(terminal.current);terminal.current.dataLoss();savedTerminal.restore(terminal.current);
  assert(terminal.current.resultValid && terminal.current.singlePeakRpm==published);
  // Observably identical genuine 4 ms + 6 ms + 10 ms motion cannot be distinguished
  // from a subdivided 10 ms mark. This near-exact recovery pattern is rejected
  // conservatively, and since 0.13.9 so is the same subdivision as the final
  // mark (it read 15,000 1-turn RPM for a 6,000 RPM pull).
  OpticalTrace ambiguous;ambiguous.turns(10000,12);ambiguous.turns(4000,1);
  ambiguous.turns(6000,1);ambiguous.turns(10000,8);ambiguous.finish(240);
  assert(std::fabs(ambiguous.current.singlePeakRpm-6000)<3);
  OpticalTrace terminalSplit;terminalSplit.turns(10000,12);
  terminalSplit.turns(4000,1);terminalSplit.turns(6000,1);terminalSplit.finish(240);
  assert(std::fabs(terminalSplit.current.singlePeakRpm-6000)<3);
  ramp.dataLoss();assert(!ramp.resultValid && ramp.singlePeakRpm==0 && ramp.sustainedPeakRpm==0);
  std::cout<<"PASS: frozen three-adjacent-turn timing oracle, no nonadjacent peak averaging, period-vs-RPM average, simultaneous final peaks, split/recovery rejection, terminal/sustained acceleration, sample-identical launch/rearm boundaries, confirmed-window retention, malformed arrivals and reset/checkpoint publication\n";
}
