#include "../LaunchLabMini/mic_rpm.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace MicRpm;

// The cases mirror core/src/tracker.rs's, through the 16 kHz input the mic
// delivers, so the port is held to the same behaviour as the apps.
static uint32_t state=12345;
static float noise() {
  state=1664525u*state+1013904223u;
  return float(double(state)/4294967296.0*2-1);
}
struct Run {
  std::unique_ptr<Space> space{new Space};
  std::vector<Reading> readings;
  std::vector<float> times;
  void over(const std::vector<float> &samples) {
    Stream stream(space.get());stream.reset();
    for(size_t i=0;i<samples.size();++i) {
      Reading r;
      // The window's centre, as the apps time a reading.
      if(stream.feed(samples[i],r)){readings.push_back(r);times.push_back(float(i+1)/CAPTURE_HZ-0.2f);}
    }
  }
};
// The apps' synthetic spin-down: 110 Hz falling 3 Hz/s with a broadband burst.
static std::vector<float> spinDown() {
  std::vector<float> data(CAPTURE_HZ*12);double phase=0;
  for(size_t i=0;i<data.size();++i) {
    const double t=double(i)/CAPTURE_HZ;
    if(t<1 || t>11){data[i]=noise()*0.00003f;continue;}
    const double hz=110-3*(t-1);
    phase+=2*M_PI*hz/CAPTURE_HZ;
    double v=0;for(int k=1;k<=6;++k)v+=std::sin(phase*k)/k*0.12;
    v+=noise()*0.001;
    if(t>5 && t<5.8)v=noise()*0.28;
    data[i]=float(v);
  }
  return data;
}
static std::vector<float> tones(unsigned seconds,float base,std::initializer_list<std::pair<int,float>> partials) {
  std::vector<float> data(CAPTURE_HZ*seconds);
  for(size_t i=0;i<data.size();++i) {
    const double phase=2*M_PI*base*double(i)/CAPTURE_HZ;
    double v=0;for(const auto &p:partials)v+=p.second*std::sin(phase*p.first);
    data[i]=float(v);
  }
  return data;
}

static void trackedSpinDown() {
  Run run;run.over(spinDown());
  unsigned good=0;
  for(size_t i=0;i<run.readings.size();++i) {
    const float t=run.times[i];const Reading &r=run.readings[i];
    if(t>1.8f && t<4.8f && r.valid){++good;assert(std::fabs(r.hz-(110-3*(t-1)))<1.5f);}
    if(t>5.25f && t<5.55f)assert(!r.valid);   // the burst is withheld
    if(t>11.3f)assert(!r.valid);              // the quiet tail clears
  }
  assert(good>=13);  // 0.2 s apart once locked
}
static void silenceAndToneAreNotMeasurements() {
  Run silent;silent.over(std::vector<float>(CAPTURE_HZ*2,0.f));
  for(const auto &r:silent.readings)assert(!r.valid && r.reason==Reason::Quiet);
  Run tone;tone.over(tones(2,100,{{1,0.2f}}));
  for(const auto &r:tone.readings)assert(!r.valid);
}
static void clippingIsWithheld() {
  std::vector<float> square(CAPTURE_HZ*2);
  for(size_t i=0;i<square.size();++i)square[i]=std::sin(2*M_PI*100*double(i)/CAPTURE_HZ)>=0?1.f:-1.f;
  Run run;run.over(square);
  for(const auto &r:run.readings)assert(!r.valid);
}
// A strong upper harmonic cannot pass as a tripled speed.
static void dominantThirdHarmonic() {
  for(float a:{0.03f,0.01f,0.003f}) {
    Run run;run.over(tones(2,40,{{1,a},{2,a*0.5f},{3,0.1f},{6,0.05f},{9,0.03f},{12,0.02f}}));
    bool any=false;
    for(const auto &r:run.readings) {
      assert(!r.valid || std::fabs(r.hz-40)<2);
      any|=r.valid || r.reason==Reason::HarmonicAmbiguity;
    }
    assert(any);
  }
}
// The band the tracker reads survives and what would fold into it does not.
static void decimatorBands() {
  const auto gain=[](float hz) {
    Decimator d;float peak=0;
    for(unsigned i=0;i<CAPTURE_HZ;++i) {
      float out;
      if(d.feed(std::sin(2*float(M_PI)*hz*float(i)/CAPTURE_HZ),out) && i>CAPTURE_HZ/2)peak=std::max(peak,std::fabs(out));
    }
    return peak;
  };
  for(float hz:{50.f,350.f,1000.f,1500.f})assert(std::fabs(gain(hz)-1)<0.06f);
  for(float hz:{2800.f,3200.f,5000.f,7500.f})assert(gain(hz)<0.01f);
}
// Input chunking cannot exist here (a sample at a time), but a reset must
// forget a launch: the second run is the first run exactly.
static void resetForgetsTheLast() {
  const auto data=spinDown();
  std::unique_ptr<Space> space(new Space);
  Stream stream(space.get());
  const auto pass=[&]{std::vector<float> hz;Reading r;for(float v:data)if(stream.feed(v,r))hz.push_back(r.hz);return hz;};
  stream.reset();const auto first=pass();
  stream.reset();const auto second=pass();
  assert(first==second && !first.empty());
}
// A falling tone with its harmonics, after launch noise: how the mic hears a spin.
struct Spin {float hz=110,fall=3,seconds=8,noise=0.5f;int harmonics=6;};
static std::vector<float> heard(const Spin &spin) {
  std::vector<float> data(size_t(float(CAPTURE_HZ)*(spin.noise+spin.seconds)));double phase=0;
  for(size_t i=0;i<data.size();++i) {
    const double t=double(i)/CAPTURE_HZ;
    if(t<spin.noise){data[i]=noise()*0.3f;continue;}
    phase+=2*M_PI*(spin.hz-spin.fall*(t-spin.noise))/CAPTURE_HZ;
    double v=noise()*0.001;for(int k=1;k<=spin.harmonics;++k)v+=std::sin(phase*k)/k*0.12;
    data[i]=float(v);
  }
  return data;
}
struct Seeded {
  std::unique_ptr<Space> space{new Space};
  std::vector<Reading> readings;
  std::vector<float> times;
  unsigned valid=0;
  unsigned searched(Search search)const{unsigned n=0;for(const auto &r:readings)n+=r.search==search;return n;}
  void over(const std::vector<float> &samples,float opticalRpm) {
    Stream stream(space.get());stream.reset(opticalRpm);
    for(float v:samples) {
      Reading r;
      if(stream.feed(v,r)){readings.push_back(r);times.push_back(stream.seconds());valid+=r.valid;}
    }
  }
};
static void opticalSeedsTheSearch() {
  Seeded run;run.over(heard({}),6900);
  assert(run.valid>=25 && run.searched(Search::Full)==0 && run.searched(Search::Prior)>0 && run.searched(Search::Near)>run.searched(Search::Prior));
  for(size_t i=0;i<run.readings.size();++i) {
    const auto &r=run.readings[i];if(!r.valid)continue;
    const float truth=110-3*(run.times[i]-0.5f);
    assert(std::fabs(r.hz-truth)<1.5f && r.cycles==1 && r.rpm==r.hz*60);
  }
}
static void readingsSpreadOutOnceLocked() {
  Seeded run;run.over(heard({}),6900);
  float locked=0;
  for(size_t i=1;i<run.times.size();++i) {
    const float gap=run.times[i]-run.times[i-1];
    if(run.readings[i-1].valid && run.readings[i].valid){assert(std::fabs(gap-0.2f)<0.001f);++locked;}
    if(i<4)assert(std::fabs(gap-0.1f)<0.001f);
  }
  assert(locked>=20);
}
// The Bey's tone can be its blades, not its turns: 2 or 3 sound cycles per
// turn, read from Hz against the optical turn rate, with no first-harmonic energy.
static void cyclesPerTurnComeFromOptical() {
  for(unsigned cycles:{1u,2u,3u}) {
    Seeded run;run.over(heard({100.f*float(cycles),2.f*float(cycles),8,0.5f,4}),6300);
    assert(run.valid>=15);
    for(const auto &r:run.readings)if(r.valid) {
      assert(r.cycles==cycles);
      assert(r.rpm<=6300*PRIOR_TOP && r.rpm>4500);
    }
  }
}
// A spin keeps its count as it slows: at the end this tone sits where one cycle
// a turn would also fit, and reading it so would double the RPM. It spends a
// second there, so where the 0.2 s readings fall cannot decide the test.
static void aSpinKeepsItsCycles() {
  Seeded run;run.over(heard({200,12,9,0.5f,4}),6300);
  float lowest=1e9f;
  for(const auto &r:run.readings)if(r.valid){assert(r.cycles==2);lowest=std::min(lowest,r.hz);}
  assert(lowest<105*PRIOR_TOP);
}
// The prior wrong by far too much low: nothing plotted at all, as it would
// claim a speed the launch never reached.
static void aToneAboveThePriorIsNotTheSpin() {
  for(uint32_t seed:{1u,2u,3u,4u,5u,6u,7u,8u}) {
    state=seed;
    Seeded run;run.over(heard({190,2,6}),3000);
    assert(!run.valid && run.searched(Search::Full)>0);
    unsigned faster=0;for(const auto &r:run.readings)faster+=r.reason==Reason::FasterThanOptical;
    assert(faster>0);
  }
}
// The prior too high: nothing fits under it, the wide search finds the tone
// after PRIOR_MISSES readings, and the tracker stays with it.
static void aWrongPriorFallsBackToTheFullSearch() {
  Seeded run;run.over(heard({88,1,8}),14000);
  assert(run.searched(Search::Full)>0 && run.valid>=20);
  float first=0;
  for(size_t i=0;i<run.readings.size();++i) {
    const auto &r=run.readings[i];if(!r.valid)continue;
    if(!first)first=run.times[i];
    assert(std::fabs(r.hz-(88-(run.times[i]-0.5f)))<1.5f && r.cycles==1);
  }
  assert(first<2.5f);
}
// The prior at twice the spin: its second harmonic sits under the prior. The
// interleaved fundamental keeps it from ever reading as the spin.
static void anUpperHarmonicUnderThePriorIsNotTheSpin() {
  for(uint32_t seed:{1u,2u,3u,4u}) {
    state=seed;
    Seeded run;run.over(heard({80,1,6}),9600);
    for(const auto &r:run.readings)assert(!r.valid || r.hz<100);
  }
}
static void noPriorSearchesEverything() {
  Seeded run;run.over(heard({}),0);
  assert(run.valid>=25 && run.searched(Search::Prior)==0 && run.searched(Search::Full)>0);
  for(size_t i=0;i<run.readings.size();++i)if(run.readings[i].valid){
    assert(std::fabs(run.readings[i].hz-(110-3*(run.times[i]-0.5f)))<1.5f && run.readings[i].cycles==1);
  }
}
// However wrong the sound, no reading tops the launch's peak.
static void noReadingBeatsTheOpticalPeak() {
  for(float hz:{60.f,95.f,140.f,200.f,260.f,330.f})for(float optical:{2500.f,5200.f,7000.f,11000.f}) {
    Seeded run;run.over(heard({hz,1,3}),optical);
    for(const auto &r:run.readings)if(r.valid)assert(r.rpm<=optical*PRIOR_TOP);
  }
}
// The cheap exits: quiet and clipped windows never reach the transform, noise
// never reaches a search.
static void aWindowWithNothingInItCostsNothing() {
  Seeded quiet;quiet.over(std::vector<float>(CAPTURE_HZ,0.f),6900);
  for(const auto &r:quiet.readings)assert(r.search==Search::None && r.reason==Reason::Quiet);
  std::vector<float> loud(CAPTURE_HZ*2);for(auto &v:loud)v=noise()*0.3f;
  Seeded burst;burst.over(loud,6900);
  for(const auto &r:burst.readings)assert(r.search==Search::None && r.reason==Reason::BroadbandNoise);
}
// One lost reading drops the lock and the near search finds the spin again.
static void aBurstDoesNotLoseTheSpin() {
  auto data=heard({110,1,8});
  for(size_t i=CAPTURE_HZ*4;i<CAPTURE_HZ*4+CAPTURE_HZ*3/5;++i)data[i]=noise()*0.28f;
  Seeded run;run.over(data,6900);
  float after=0;for(size_t i=0;i<run.times.size();++i)if(run.readings[i].valid && run.times[i]>4.6f && !after)after=run.times[i];
  assert(after && after<6 && run.searched(Search::Full)==0);
}
// The owner's recorded spins, from about 12 s on: a clean tone whose first
// harmonics are under 200 Hz, with mic floor above. The apps' 200-1,800 Hz
// flatness called every window of it noise.
static void aQuietSpinIsNotNoise() {
  for(uint32_t seed:{1u,2u,3u}) {
    state=seed;
    std::vector<float> data(CAPTURE_HZ*8);double phase=0;
    for(size_t i=0;i<data.size();++i) {
      phase+=2*M_PI*(72-0.5*double(i)/CAPTURE_HZ)/CAPTURE_HZ;
      data[i]=float(noise()*0.003+0.002*(std::sin(phase)+0.6*std::sin(2*phase)+0.3*std::sin(3*phase)));
    }
    Seeded run;run.over(data,6900);
    assert(run.valid>=25);
    for(size_t i=0;i<run.readings.size();++i)if(run.readings[i].valid)assert(std::fabs(run.readings[i].hz-(72-0.5f*run.times[i]))<1.5f);
  }
}
// Impacts about a second apart, as in the owner's recorded battles: the spin
// is trusted again in the first clear window, where three in a row is more
// than the gap between two impacts holds.
static void aSpinIsTrustedAgainBetweenImpacts() {
  for(uint32_t seed:{1u,2u,3u}) {
    state=seed;
    auto data=heard({100,2,8});
    for(size_t i=0;i<data.size();++i)if(double(i)/CAPTURE_HZ>2.5 && std::fmod(double(i)/CAPTURE_HZ-2.5,1.0)<0.3)data[i]=noise()*0.25f;
    Seeded run;run.over(data,6900);
    unsigned after=0;
    for(size_t i=0;i<run.readings.size();++i)if(run.readings[i].valid) {
      assert(std::fabs(run.readings[i].hz-(100-2*(run.times[i]-0.5f)))<1.5f);
      after+=run.times[i]>2.5f;
    }
    assert(after>=21);
  }
}
// A louder steady tone 5% above the spin (another Bey, a hum) is not where a
// slowing spin goes next: no reading is more than 3% above the one before.
static void aSpinNeverSpeedsUpToAToneAboveIt() {
  for(uint32_t seed:{1u,2u,3u}) {
    state=seed;
    auto data=heard({100,2,8});double phase=0;
    for(size_t i=CAPTURE_HZ*3;i<data.size();++i) {
      phase+=2*M_PI*99.0/CAPTURE_HZ;
      for(int k=1;k<=6;++k)data[i]+=float(std::sin(phase*k)/k*0.24);
    }
    Seeded run;run.over(data,6900);
    float last=0;
    for(size_t i=0;i<run.readings.size();++i)if(run.readings[i].valid) {
      const float hz=run.readings[i].hz;
      assert(std::fabs(hz-(100-2*(run.times[i]-0.5f)))<1.5f && (!last || hz<=last*NEAR_ABOVE));
      last=hz;
    }
    assert(run.valid>=25);
  }
}
static void fastLogIsCloseEnough() {
  for(float x=1e-18f;x<1e4f;x*=1.37f)assert(std::fabs(log2Fast(x)-std::log2(x))<0.00012f);
}
static void pcmAndFloatMatch() {
  const auto data=heard({110,3,3});
  std::vector<int16_t> pcm;for(float v:data)pcm.push_back(int16_t(std::lround(std::min(std::max(v,-1.f),0.99f)*32768)));
  std::unique_ptr<Space> a(new Space),b(new Space);
  Stream one(a.get()),many(b.get());one.reset(6900);many.reset(6900);
  std::vector<float> fed,block;Reading r;
  for(int16_t v:pcm)if(one.feed(float(v)*(1.f/32768),r))fed.push_back(r.hz);
  size_t at=0;while(at<pcm.size()){bool ready;at+=many.feed(pcm.data()+at,std::min<size_t>(pcm.size()-at,1000),r,ready);if(ready)block.push_back(r.hz);}
  assert(fed==block && !fed.empty());
}
// A kept spin (USB `V`) is the 4 kHz line itself: fed back through line() it
// gives the readings the unit made, so a recording tests a gate exactly.
static void aTapeReplaysTheReadings() {
  const auto data=heard({110,3,6});
  std::vector<int16_t> pcm;for(float v:data)pcm.push_back(int16_t(std::lround(std::min(std::max(v,-1.f),0.99f)*32768)));
  std::vector<int16_t> kept(TRACK_HZ*8);
  Tape tape;tape.samples=kept.data();tape.capacity=uint32_t(kept.size());
  std::unique_ptr<Space> a(new Space),b(new Space);
  Stream live(a.get()),replay(b.get());live.reset(6900);replay.reset(6900);live.record(&tape);
  std::vector<Reading> heardLive,heardAgain;Reading r;
  for(int16_t v:pcm)if(live.feed(float(v)*(1.f/32768),r))heardLive.push_back(r);
  for(uint64_t i=0;i<tape.written;++i)if(replay.line(float(kept[i])/32768.f,r))heardAgain.push_back(r);
  assert(heardLive.size()==heardAgain.size() && heardLive.size()>20);
  unsigned valid=0;
  for(size_t i=0;i<heardLive.size();++i) {
    assert(heardLive[i].valid==heardAgain[i].valid && heardLive[i].reason==heardAgain[i].reason && heardLive[i].search==heardAgain[i].search);
    assert(std::fabs(heardLive[i].hz-heardAgain[i].hz)<0.05f);valid+=heardLive[i].valid;
  }
  assert(valid>10);
  // Sound the ring lost is kept as silence, so a sample's place stays its time
  // since the release (a replay then reads those windows as quiet).
  const uint64_t before=tape.written;live.skip(CAPTURE_HZ);
  assert(tape.written==before+TRACK_HZ && kept[before%tape.capacity]==0);
}
static void summaryKeepsTheFirstAccepted() {
  Summary s;
  Reading a;a.reason=Reason::BroadbandNoise;s.add(a);
  a.reason=Reason::Acquiring;s.add(a);
  for(float hz:{115.f,114.f,113.f,112.f,111.f,110.f,109.f}) {
    Reading r;r.valid=true;r.hz=hz;r.reason=Reason::Tracking;s.add(r);
  }
  assert(s.frames==9 && s.valid==7 && s.firstFrame==2);
  assert(s.firstHz()==115.f && s.settledHz()==113.f);
  assert(s.withheld[unsigned(Reason::BroadbandNoise)]==1 && s.withheld[unsigned(Reason::Acquiring)]==1);
  Summary none;assert(none.firstHz()==0 && none.settledHz()==0);
}
int main() {
  trackedSpinDown();silenceAndToneAreNotMeasurements();clippingIsWithheld();dominantThirdHarmonic();
  decimatorBands();resetForgetsTheLast();summaryKeepsTheFirstAccepted();
  opticalSeedsTheSearch();readingsSpreadOutOnceLocked();cyclesPerTurnComeFromOptical();aSpinKeepsItsCycles();aToneAboveThePriorIsNotTheSpin();
  aWrongPriorFallsBackToTheFullSearch();anUpperHarmonicUnderThePriorIsNotTheSpin();noPriorSearchesEverything();noReadingBeatsTheOpticalPeak();
  aWindowWithNothingInItCostsNothing();aBurstDoesNotLoseTheSpin();
  aQuietSpinIsNotNoise();aSpinIsTrustedAgainBetweenImpacts();aSpinNeverSpeedsUpToAToneAboveIt();fastLogIsCloseEnough();pcmAndFloatMatch();aTapeReplaysTheReadings();
  std::puts("mic_rpm tests passed");
}
