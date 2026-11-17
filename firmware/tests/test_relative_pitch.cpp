#include "../LaunchLabMini/mic_rpm.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>
using namespace MicRpm;

namespace {
uint32_t randomState=12345;
float noise() {
  randomState=1664525u*randomState+1013904223u;
  return float(double(randomState)/4294967296.0*2-1);
}
enum class Tail {TwoHarmonics,MissingFundamental,SingleHarmonic,Silence,Noise,Clipping,Octave,LowerOctave,ThirdHarmonic,LargeDrop};
std::vector<float> signal(Tail tail,bool anchored=true,float seconds=6,unsigned cycles=1,uint32_t seed=12345) {
  randomState=seed;
  std::vector<float> samples(size_t(TRACK_HZ*seconds));
  double phase=0;
  for(size_t i=0;i<samples.size();++i) {
    const float t=float(i)/TRACK_HZ;
    float hz=(110-2*t)*float(cycles);
    if(t>=2 && tail==Tail::Octave)hz*=2;
    if(t>=2 && tail==Tail::LowerOctave)hz*=0.5f;
    if(t>=2 && tail==Tail::ThirdHarmonic)hz*=3;
    if(t>=2 && tail==Tail::LargeDrop)hz*=0.62f;
    phase+=2*M_PI*hz/TRACK_HZ;
    const bool strong=anchored && t<2;
    if(!strong && tail==Tail::Silence){samples[i]=0;continue;}
    if(!strong && tail==Tail::Noise){samples[i]=noise()*0.3f;continue;}
    if(!strong && tail==Tail::Clipping){samples[i]=std::sin(phase)>=0?1.f:-1.f;continue;}
    const unsigned first=(!strong && tail==Tail::MissingFundamental)?2:1;
    const unsigned harmonics=(strong || tail==Tail::LowerOctave)?6:tail==Tail::SingleHarmonic?1:tail==Tail::MissingFundamental?3:2;
    float value=noise()*0.0003f;
    for(unsigned h=first;h<=harmonics;++h)value+=float(std::sin(phase*h))*0.08f/float(h);
    samples[i]=value;
  }
  return samples;
}
bool sameOriginal(const Reading &a,const Reading &b) {
  return a.hz==b.hz && a.rpm==b.rpm && a.candidateHz==b.candidateHz && a.competingHz==b.competingHz &&
    a.level==b.level && a.score==b.score && a.margin==b.margin && a.flatness==b.flatness && a.valid==b.valid &&
    a.reason==b.reason && a.search==b.search && a.candidateCycles==b.candidateCycles && a.support==b.support && a.cycles==b.cycles;
}
struct Frame {float time;Reading reading;};
std::vector<Frame> run(const std::vector<float> &samples,float prior=6900) {
  std::unique_ptr<Space> a(new Space{}),b(new Space{});
  Stream off(a.get()),on(b.get());off.reset(prior);on.reset(prior,true);
  std::vector<Frame> frames;
  for(float value:samples) {
    Reading x,y;const bool ready=off.line(value,x);
    assert(on.line(value,y)==ready);
    if(!ready)continue;
    assert(sameOriginal(x,y) && off.seconds()==on.seconds());
    assert(x.relativeRpm==0 && x.pitchConfidence==0);
    if(y.relativeRpm>0)assert(!y.valid && y.pitchConfidence>0 && (y.reason==Reason::WeakHarmonics || y.reason==Reason::Ambiguous));
    else assert(y.pitchConfidence==0);
    frames.push_back({on.seconds(),y});
  }
  return frames;
}
void anchoredHarmonicsBridgeOnlyBriefly() {
  for(unsigned cycles:{1u,2u})for(Tail tail:{Tail::TwoHarmonics,Tail::MissingFundamental}) {
    const auto frames=run(signal(tail,true,6,cycles));
    unsigned relative=0,valid=0;float lastAnchor=0,firstRelative=0;
    for(const Frame &f:frames) {
      const Reading &r=f.reading;
      if(r.valid){++valid;lastAnchor=f.time;}
      if(r.relativeRpm>0) {
        ++relative;if(!firstRelative)firstRelative=f.time;
        assert(lastAnchor>0 && f.time-lastAnchor<=2.001f);
        assert(std::fabs(r.relativeRpm-(110-2*f.time)*60)<120);
        assert(r.rpm==0 && r.hz==0 && r.cycles==0);
      }
      if(f.time-lastAnchor>2.001f)assert(r.relativeRpm==0);
    }
    assert(valid>=5 && relative>=3 && firstRelative>2.1f);
  }
}
void twoHarmonicsCannotStartAnUnanchoredSpin() {
  // This prefix has no normal lock. Longer random noise can occasionally
  // satisfy the unchanged normal tracker's third-harmonic test.
  for(const auto &f:run(signal(Tail::TwoHarmonics,false,3)))assert(!f.reading.valid && f.reading.relativeRpm==0);
}
void aSingleToneCannotExtendTheAnchor() {
  for(const auto &f:run(signal(Tail::SingleHarmonic)))if(f.time>2.3f)assert(f.reading.relativeRpm==0);
}
void rejectedSignalAndHarmonicJumpsCannotRecover() {
  for(uint32_t seed:{1u,2u,3u,12345u})for(Tail tail:{Tail::Silence,Tail::Noise,Tail::Clipping,Tail::Octave,Tail::LowerOctave,Tail::ThirdHarmonic,Tail::LargeDrop}) {
    const auto frames=run(signal(tail,true,6,1,seed));
    for(const auto &f:frames)if(f.time>2.3f)assert(f.reading.relativeRpm==0 && f.reading.pitchConfidence==0);
  }
}
void resetForgetsTheAnchorAndDefaultsOff() {
  std::unique_ptr<Space> space(new Space{});Stream stream(space.get());Reading r;
  stream.reset(6900,true);
  for(float v:signal(Tail::TwoHarmonics))stream.line(v,r);
  stream.reset(6900,true);
  for(float v:signal(Tail::TwoHarmonics,false,3))if(stream.line(v,r))assert(!r.valid && r.relativeRpm==0 && r.pitchConfidence==0);
  stream.reset(6900);
  for(float v:signal(Tail::TwoHarmonics))if(stream.line(v,r))assert(r.relativeRpm==0 && r.pitchConfidence==0);
}
void skippedAudioCannotKeepAnAnchor() {
  std::unique_ptr<Space> space(new Space{});Stream stream(space.get());Reading r;
  stream.reset(6900,true);
  for(float v:signal(Tail::TwoHarmonics,true,1.9f))stream.line(v,r);
  assert(r.valid);
  stream.skip(CAPTURE_HZ/10);
  // A short skipped interval still lies within the normal two-second TTL;
  // the discontinuity itself must invalidate the spectral anchor.
  unsigned withheld=0;
  for(float v:signal(Tail::TwoHarmonics,false,1.5f))if(stream.line(v,r)) {
    if(r.valid)break; // A new normal lock can legitimately establish a new anchor.
    assert(r.relativeRpm==0);++withheld;
  }
  assert(withheld>=3);
}
void anchorExpiresEvenWithAContinuousMeasuredPitch() {
  const auto strong=signal(Tail::TwoHarmonics,true,1),weak=signal(Tail::TwoHarmonics,false,1);
  for(float delay:{0.f,2.1f}) {
    std::unique_ptr<Space> space(new Space{});Tracker tracker(space.get());tracker.reset(6900,true);Reading r;
    for(unsigned i=0;i<3;++i)r=tracker.analyze(strong.data()+i*HOP,.2f+.1f*i);
    assert(r.valid);
    unsigned recovered=0;
    for(unsigned i=0;i<4;++i) {
      r=tracker.analyze(weak.data()+i*HOP,.6f+.1f*i+delay);
      assert(!r.valid);recovered+=r.relativeRpm>0;
    }
    if(delay==0)assert(recovered>=1);
    else assert(recovered==0);
  }
}
void inactiveRecoveryDoesNotReadTheSpectrum() {
  RelativePitch::Tracker tracker;
  unsigned reads=0;
  auto db=[&](float hz) {
    ++reads;
    const float offset=hz-100.f*std::round(hz/100.f);
    return -30.f-offset*offset;
  };
  auto contrast=[&](float) { ++reads;return 20.f; };
  assert(tracker.recover(.1f,1.f,db,contrast).rpm==0 && reads==0);
  tracker.anchor(100,6000,1,1,db,contrast);
  assert(reads>0);reads=0;
  for(float at:{0.f,1.f,3.01f,float(NAN)})
    assert(tracker.recover(at,1,db,contrast).rpm==0 && reads==0);
  tracker.recover(1.1f,1,db,contrast);
  assert(reads>0);reads=0;
  tracker.reset();
  assert(tracker.recover(1.2f,1,db,contrast).rpm==0 && reads==0);
}
}
int main() {
  anchoredHarmonicsBridgeOnlyBriefly();twoHarmonicsCannotStartAnUnanchoredSpin();aSingleToneCannotExtendTheAnchor();
  rejectedSignalAndHarmonicJumpsCannotRecover();resetForgetsTheAnchorAndDefaultsOff();skippedAudioCannotKeepAnAnchor();
  anchorExpiresEvenWithAContinuousMeasuredPitch();
  inactiveRecoveryDoesNotReadTheSpectrum();
  std::puts("relative_pitch tests passed");
  return 0;
}
