// Regressions for the 2026-10-17 RPM detector review and the HIL rig's RPM
// warnings. Each case is a waveform a real pull (or its rewind) can produce,
// with the RPM the generator was programmed to, not a replayed estimator.
#include "../LaunchLabMini/sensor_profile.h"
#include "../LaunchLabMini/history_checkpoint.h"
#include "../LaunchLabMini/rpm_estimator.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>
using D=AnalogTachometer;
static int failures=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);++failures;}}while(0)

static void level(D &d,unsigned us,int value){for(unsigned i=0;i<us/20;++i)d.feed(uint16_t(value));}
// One revolution: a mark half (AO pulled up by `amplitude`) and a base half.
static void turn(D &d,unsigned period,int amplitude=100){level(d,period/2,140+amplitude);level(d,period-period/2,140);}
static void turns(D &d,unsigned period,unsigned count,int amplitude=100){for(unsigned i=0;i<count;++i)turn(d,period,amplitude);}
static void turns(D &d,std::initializer_list<unsigned> periods,int amplitude=100){for(unsigned p:periods)turn(d,p,amplitude);}
static D ready(StickS3SensorProfile::Mode mode=StickS3SensorProfile::Mode::Standard){
  D d;StickS3SensorProfile::configure(d,mode);level(d,1200000,140);assert(d.phase==D::Phase::Ready);return d;
}
static void settle(D &d){level(d,1700000,140);}
static bool near(float a,float b,float tolerance=3){return std::fabs(a-b)<=tolerance;}
static float three(const D &d){return RpmEstimator::select(RpmEstimator::Mode::ThreeTurn,d.sustainedPeakRpm,d.singlePeakRpm);}
static float single(const D &d){return RpmEstimator::select(RpmEstimator::Mode::SingleTurn,d.sustainedPeakRpm,d.singlePeakRpm);}

// Host port of the HIL rig's on-device mark generator (hil_optical.h,
// signals.py): keyframes (ms, rpm, depth, noise) evaluated once per 5 ms DMA
// batch, a phase accumulator per sample, about 4-sample linear mark edges and
// triangular noise. Marks pull AO down from the baseline as on the device.
struct Generator {
  struct Key{float ms,rpm,depth,noise;};
  std::vector<Key> keys;float baseline=1800,idleNoise=3,duty=.3f;
  uint32_t phase=0,noiseState=0x9e3779b9u;
  float noise() {
    auto next=[&]{noiseState^=noiseState<<13;noiseState^=noiseState>>17;noiseState^=noiseState<<5;return noiseState;};
    return (float(next()&0xffff)+float(next()&0xffff))*(2.449f/65536.0f)-2.449f;
  }
  bool at(float ms,Key &out)const {
    if(keys.empty() || ms<keys.front().ms || ms>keys.back().ms)return false;
    size_t hi=1;while(hi<keys.size() && keys[hi].ms<ms)++hi;
    if(hi>=keys.size()){out=keys.back();return true;}
    const Key &a=keys[hi-1],&b=keys[hi];const float f=b.ms>a.ms?(ms-a.ms)/(b.ms-a.ms):1;
    out={ms,a.rpm+(b.rpm-a.rpm)*f,a.depth+(b.depth-a.depth)*f,a.noise+(b.noise-a.noise)*f};return true;
  }
  // Play `ms` milliseconds starting at generator time `fromMs`.
  void play(D &d,float fromMs,float ms) {
    const unsigned batches=unsigned(ms/5+.5f);
    for(unsigned b=0;b<batches;++b) {
      Key k{};const bool marks=at(fromMs+b*5+2.5f,k);
      const uint32_t step=marks?uint32_t(k.rpm*(4294967296.0f/60.0f/50000)):0;
      const uint32_t markEnd=uint32_t(duty*4294967296.0f),edge=std::max<uint32_t>(1,step*4);
      for(unsigned i=0;i<250;++i) {
        float value;
        if(marks) {
          phase+=step;float lvl=phase<markEnd?1.0f:0.0f;
          if(phase<edge)lvl=float(phase)/float(edge);
          else if(phase>=markEnd && phase-markEnd<edge)lvl=1.0f-float(phase-markEnd)/float(edge);
          value=baseline-k.depth*lvl+k.noise*noise();
        } else value=baseline+idleNoise*noise();
        d.feed(uint16_t(int(std::max(0.0f,std::min(4095.0f,value))+.5f)));
      }
    }
  }
};
// signals.standard_pull: ramp from max(1500, peak/4) to peak, hold, decay to
// 55% of peak, then the marks stop (the Beyblade released).
static std::vector<Generator::Key> standardPull(float peak,float startMs=0,float depth=1000,float noise=4,
                                                float rampMs=400,float holdMs=120,float decayMs=500) {
  const float start=std::max(1500.0f,peak*.25f),t1=rampMs,t2=t1+holdMs,t3=t2+decayMs;
  return {{startMs,start,depth,noise},{startMs+t1,peak,depth,noise},{startMs+t2,peak,depth,noise},
          {startMs+t3,peak*.55f,depth,noise},{startMs+t3+1,0,0,noise}};
}
// signals.dropout_pull: mark-free windows (start ms, length ms) in that pull.
static std::vector<Generator::Key> dropoutPull(float peak,std::initializer_list<std::pair<float,float>> gaps) {
  auto keys=standardPull(peak);Generator g;g.keys=keys;
  for(auto [start,length]:gaps) {
    Generator::Key k{};g.at(start,k);
    keys.push_back({start,k.rpm,1000,4});keys.push_back({start+.1f,k.rpm,0,4});
    keys.push_back({start+length,k.rpm,0,4});keys.push_back({start+length+.1f,k.rpm,1000,4});
  }
  std::sort(keys.begin(),keys.end(),[](auto &a,auto &b){return a.ms<b.ms;});
  return keys;
}
static D generatorReady(Generator &g){D d;StickS3SensorProfile::configure(d);g.play(d,-1500,1500);assert(d.phase==D::Phase::Ready);return d;}

int main() {
  // ---- P1: a rewind that begins inside REARM must never become a launch.
  // After a pull the ripcord rewinds the gear, accelerating from rest. The
  // first burst after a pull is that rewind; a single optical channel cannot
  // tell its direction. Real rewinds are irregular while they speed up
  // (sticks3-0.2.0 three pulls: 33.2, 51.2, 19.0, 24.9, 21.4, 11.8, 11.2 ms).
  const std::initializer_list<unsigned> irregularRewind={33200,51200,19000,24900,21400,11800,11200,16500,15900,24300,25800};
  const std::initializer_list<unsigned> smoothRewind={60000,45000,30000,22000,18000,16000,15000,15000,15000,15000,15000,15000,15000,15000,15000,15000,15000};
  for(unsigned holdMs:{0u,300u,600u,800u,900u,950u,980u}) {
    for(int shape=0;shape<2;++shape) {
      D d=ready();turns(d,10000,12);level(d,std::max(holdMs*1000,60000u),140);
      CHECK(d.launches==1 && d.phase==D::Phase::Hold);
      turns(d,shape?smoothRewind:irregularRewind,shape?100:110);settle(d);
      if(d.launches!=1)std::printf("rewind %u ms after the pull (%s) became launch %u: %.0f RPM\n",holdMs,shape?"smooth":"irregular",d.launches,d.resultRpm);
      CHECK(d.launches==1 && d.resultValid && near(three(d),6000) && near(single(d),6000));
    }
  }
  // Trade-off: a burst whose first strong edge comes after a full quiet
  // second is a new launch, so every real pull a second after the previous
  // pull's marks stop is measured. A rewind held back that long is counted.
  {
    D d=ready();turns(d,10000,12);level(d,1100000,140);CHECK(d.phase==D::Phase::Ready);
    turns(d,10000,12);settle(d);CHECK(d.launches==2 && near(three(d),6000));
  }
  // The owner's rapid dry pulls (qre1113 comparison, 53.0-60.0 s): pull, its
  // rewind at once, then the next pull about a second after the rewind
  // stopped. A rewind seen inside REARM must not delay the next real pull.
  {
    D d=ready();turns(d,{30400,16000,11000,9400,9300,9500,10300,11300,12500,15100,19500});
    level(d,40000,140);CHECK(d.launches==1);const float first=three(d);
    turns(d,{69700,29700,29400,23300,20500,18800,17500,17600,34400,36300,80100},1200);
    level(d,1020000,140);
    turns(d,{30400,16000,11000,9400,9300,9500,10300,11300,12500,15100,19500});settle(d);
    CHECK(d.launches==2 && near(three(d),first) && near(first,60000000.0f/((9400+9300+9500)/3.0f),1));
  }
  std::puts("PASS: rewinds inside REARM never launch; next pull after a seen rewind and after a quiet second measured");

  // ---- P1: a sharp pull changes period by more than 12% per turn. It still
  // has to produce a launch; three turns with a monotonic speed change are a
  // valid three-turn time average.
  for(float factor:{.90f,.85f,.80f}) {
    D d=ready();std::vector<unsigned> periods;
    for(float p=40000;p>4500;p*=factor)periods.push_back(unsigned(p/100)*100);
    periods.push_back(4500);
    for(unsigned p:periods)turn(d,p);
    // Spin-down after the peak: periods grow by 8% per turn.
    for(float p=4500*1.08f;p<20000;p*=1.08f)turn(d,unsigned(p/100)*100);
    settle(d);
    if(d.launches!=1)std::printf("sharp pull %.2f/turn: launches=%u\n",factor,d.launches);
    CHECK(d.launches==1 && near(single(d),60000000.0f/4500));
    const unsigned n=unsigned(periods.size());
    const float window=180000000.0f/(periods[n-1]+periods[n-2]+periods[n-3]);
    CHECK(three(d)>=window-3 && three(d)<=60000000.0f/4500+3);
  }
  // Review probe W: nine strong turns shaped like the real captures, sharper.
  for(auto profile:{std::vector<unsigned>{18000,13000,9000,6500,5000,5500,7000,11000,20000},
                    std::vector<unsigned>{18000,12000,8000,6000,5000,5300,6000,8000,14000,30000}}) {
    D d=ready();for(unsigned p:profile)turn(d,p);settle(d);
    if(d.launches!=1)std::printf("short sharp profile: launches=%u\n",d.launches);
    CHECK(d.launches==1 && near(single(d),12000) && three(d)>9000);
  }
  std::puts("PASS: sharp monotonic acceleration/deceleration forms a launch");

  // ---- P1: missed marks must not end the burst on a gap. One turn's mark
  // can fall below the edge hysteresis while the launcher is yanked.
  {
    D d=ready();turns(d,20000,10);level(d,20000,140);turns(d,5000,14);settle(d);
    if(!near(three(d),12000))std::printf("one missed mark at 3000 RPM: %.0f end=%s\n",three(d),d.endName());
    CHECK(d.launches==1 && near(three(d),12000) && near(single(d),12000));
  }
  {
    D d=ready();turns(d,10000,10);level(d,20000,140);turns(d,5000,14);settle(d);
    if(!near(three(d),12000))std::printf("two missed marks at 6000 RPM: %.0f end=%s\n",three(d),d.endName());
    CHECK(d.launches==1 && near(three(d),12000) && near(single(d),12000));
  }
  // HIL rig `weak`: an 8000 RPM pull with a 20 ms mark dropout in the ramp
  // reported 5.5-6.3k on hardware. Whether the dropout swallows one mark or
  // two depends on its phase, so sweep it through the ramp (5000-7000 RPM).
  for(float start=200;start<=300;start+=10)for(float length:{15.0f,20.0f,25.0f}) {
    Generator g;g.keys=dropoutPull(8000,{{start,length}});D d=generatorReady(g);g.play(d,0,2600);
    if(std::fabs(three(d)-8000)>8000*.03f)std::printf("HIL dropout %.0f+%.0f ms: launches=%u three=%.0f single=%.0f end=%s\n",start,length,d.launches,three(d),single(d),d.endName());
    CHECK(d.launches==1 && std::fabs(three(d)-8000)<=8000*.03f && std::fabs(single(d)-8000)<=8000*.03f);
  }
  {
    Generator g;g.keys=dropoutPull(8000,{{200,15},{450,15}});D d=generatorReady(g);g.play(d,0,2600);
    CHECK(d.launches==1 && std::fabs(three(d)-8000)<=8000*.03f);
  }
  std::puts("PASS: one or two missed marks keep the burst open");
  // ---- P2: a two-turn hesitation (cord slip, re-grip) mid-pull must not end
  // the burst before the harder second half.
  for(unsigned slow:{14000u,15000u,18000u}) {
    D d=ready();turns(d,10000,10);turns(d,slow,2);turns(d,6000,12);settle(d);
    if(!near(three(d),10000))std::printf("2-turn hesitation at %u us: %.0f end=%s\n",slow,three(d),d.endName());
    CHECK(d.launches==1 && near(three(d),10000) && near(single(d),10000));
  }
  std::puts("PASS: a two-turn hesitation keeps the burst open");

  // ---- P2: window strength used the weakest of three marks. Real marks sit
  // close to the floor (sticks3-0.2.0: 81-85 counts against 80; TCRT 62.7-71.4
  // against 60), so one slightly weak mark at the peak lost the peak.
  for(auto mode:{StickS3SensorProfile::Mode::Standard,StickS3SensorProfile::Mode::Tcrt}) {
    const int floor=int(StickS3SensorProfile::minMark(mode)),good=floor*5/4,dip=floor*9/10;
    D d=ready(mode);turns(d,10000,10,good);
    for(unsigned i=0;i<9;++i)turn(d,6000,i%3==1?dip:good);
    turns(d,10000,6,good);settle(d);
    if(!near(three(d),10000))std::printf("one %d-count mark per window at the peak (%s): %.0f\n",dip,StickS3SensorProfile::name(mode),three(d));
    CHECK(d.launches==1 && near(three(d),10000));
  }
  std::puts("PASS: one slightly weak mark per window does not lose the peak");

  // ---- P2: a completed result survives an ADC overflow, read error or
  // invalid frame; a pull right after it is measured.
  {
    D d=ready();turns(d,8000,12);settle(d);CHECK(d.launches==1 && near(d.resultRpm,7500));
    const uint32_t revision=d.resultRevision;
    HistoryCheckpoint::sampleGap(d);
    CHECK(d.resultValid && near(d.resultRpm,7500) && near(three(d),7500) && d.resultRevision==revision);
    CHECK(d.phase==D::Phase::Ready);
    turns(d,6000,12);settle(d);CHECK(d.launches==2 && near(three(d),10000));
    // During a burst the samples on both sides of the gap cannot be joined.
    turns(d,8000,6);CHECK(d.phase==D::Phase::Launch);HistoryCheckpoint::sampleGap(d);
    CHECK(!d.resultValid && d.phase==D::Phase::Settling && d.launches==2);
  }
  std::puts("PASS: sample gaps keep a completed result and Ready; an interrupted burst is discarded");

  // ---- P3: a checkpoint taken in REARM resumed with a full second of quiet
  // and dropped the pending rewind. Quiet served and the rewind expectation
  // now survive it.
  {
    D d=ready();turns(d,10000,12);level(d,700000,140);CHECK(d.phase==D::Phase::Hold);
    const uint32_t before=d.rearmMs();HistoryCheckpoint saved(d);saved.restore(d);
    CHECK(d.phase==D::Phase::Hold && d.rearmMs()<=before+1 && d.rewindPending);
    level(d,before*1000+60000,140);CHECK(d.phase==D::Phase::Ready);
    turns(d,6000,12);settle(d);CHECK(d.launches==2 && near(three(d),10000));
  }
  {
    // The rewind that follows the checkpoint is still absorbed.
    D d=ready();turns(d,10000,12);level(d,600000,140);HistoryCheckpoint saved(d);saved.restore(d);
    level(d,300000,140);turns(d,smoothRewind);settle(d);CHECK(d.launches==1 && near(three(d),6000));
  }
  // A delayed rewind released while a checkpoint has stopped sampling: the
  // samples before restore are lost, and edge history restarts. REARM counts
  // the pause as elapsed time but cannot end until it has sampled long enough
  // to see a rewind in progress, so a rewind released inside the first
  // second is still absorbed (later ones count without a checkpoint too).
  for(unsigned checkpointMs:{300u,700u,800u,950u})for(unsigned pauseMs:{250u,400u})for(unsigned releaseIn:{0u,100u,200u,240u}) {
    if(releaseIn>=pauseMs || checkpointMs+releaseIn>=980)continue;
    std::vector<uint16_t> rewind;
    for(unsigned p:smoothRewind)for(unsigned i=0;i<p/20;++i)rewind.push_back(uint16_t(i<p/40?240:140));
    D d=ready();turns(d,10000,12);level(d,checkpointMs*1000,140);CHECK(d.phase==D::Phase::Hold);
    HistoryCheckpoint saved(d);saved.restore(d,uint64_t(pauseMs)*1000);
    for(size_t i=(pauseMs-releaseIn)*50;i<rewind.size();++i)d.feed(rewind[i]);
    settle(d);
    if(d.launches!=1)std::printf("rewind released %u ms into a %u ms checkpoint at %u ms became launch %u (%.0f RPM)\n",releaseIn,pauseMs,checkpointMs,d.launches,d.resultRpm);
    CHECK(d.launches==1 && near(three(d),6000));
  }
  std::puts("PASS: REARM checkpoints keep served quiet and the pending rewind");

  // ---- HIL rig `history_save_pull`: a 9000 RPM pull scheduled `delay` ms
  // from the start of a history write that stops the ADC for `stop` ms (the
  // slowed 400 ms write plus real NVS time, 450-690 ms on the two rig M5s).
  // Nothing of the pull is sampled before the write. Turns inside the stop
  // are lost; the reading must be the speed of the first turns sampled
  // afterwards, never above it. The rig spread between M5s (7.2-7.9k at
  // -150 ms, 8.6-9.1k at +0 ms) is the NVS write time, not the detector.
  for(float delay:{-150.0f,0.0f,100.0f})for(float stop:{450.0f,500.0f,530.0f,560.0f,580.0f,620.0f,660.0f,690.0f})
  for(int single:{0,1}) {
    Generator g;g.keys=standardPull(6000);D d=generatorReady(g);d.singleTurnPeak=single;g.play(d,0,4000);
    const float writeAt=4000;g.keys.push_back({writeAt-.2f,0,0,4});
    for(auto k:standardPull(9000,writeAt+delay))g.keys.push_back(k);
    HistoryCheckpoint saved(d);Generator::Key k{};g.at(writeAt+stop,k);
    { D lost;g.play(lost,writeAt,stop); } // advance the generator; samples lost
    saved.restore(d,uint64_t(stop*1000));g.play(d,writeAt+stop,2500);
    const float observable=k.rpm>0?std::min(9000.0f,stop-delay<=520?9000.0f:k.rpm):0;
    const float reading=single?d.singlePeakRpm:d.sustainedPeakRpm;
    // One 100-us edge-grid step above the true turn is the known 1-turn
    // quantization (normal pulls read 9091 for 9000 too).
    const float ceiling=60000000.0f/(60000000.0f/observable-100)+1;
    const bool ok=d.launches==2 && reading<=ceiling && reading>=observable*(single?.97f:.95f);
    if(!ok)std::printf("ADC stop %.0f ms, pull %+.0f ms: launches=%u %s=%.0f, first sampled turns %.0f\n",stop,delay,d.launches,single?"1-turn":"3-turn",reading,observable);
    CHECK(ok);
  }
  std::puts("PASS: HIL history_save_pull: the first turns sampled after an ADC stop, never above them");

  // ---- HIL rig `rearm`: a 9000 RPM pull a given time after a 6000 RPM
  // pull's marks stop, optionally with a settings Apply checkpoint (250 ms
  // stop) 0.3 s after the first result. The synthetic pulls have no rewind,
  // so a second pull inside the first second is indistinguishable from a
  // delayed rewind and is absorbed (trade-off); from 1.1 s on it is measured,
  // and an Apply between them no longer pushes that boundary out.
  for(int apply=0;apply<2;++apply)for(float gap:{0.8f,1.1f,1.2f,1.6f,2.0f,3.0f}) {
    Generator g;auto first=standardPull(6000);const float duration=first.back().ms;
    g.keys=first;g.keys.push_back({duration+gap*1000-.2f,0,0,4});
    for(auto k:standardPull(9000,duration+gap*1000))g.keys.push_back(k);
    D d=generatorReady(g);float t=0;
    if(apply) {
      while(d.launches<1 && t<3000){g.play(d,t,5);t+=5;}
      g.play(d,t,300);t+=300;HistoryCheckpoint saved(d);t+=250;saved.restore(d,250000);
    }
    g.play(d,t,duration+gap*1000+2500-t);
    const bool measured=d.launches==2 && std::fabs(three(d)-9000)<=9000*.03f;
    if(measured!=(gap>1))std::printf("HIL rearm %.1f s%s: launches=%u three=%.0f\n",gap,apply?" +Apply":"",d.launches,three(d));
    CHECK(measured==(gap>1));
  }
  std::puts("PASS: HIL rearm: second pulls from 1.1 s measured, with or without an Apply checkpoint");

  if(failures){std::printf("%d check(s) failed\n",failures);return 1;}
  return 0;
}
