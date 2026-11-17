#include "../LaunchLabMini/audio_metadata.h"
#include "../LaunchLabMini/mic_rpm.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>

using namespace AudioMetadata;
static void summaries(){
  Recorder r;r.reset(12);
  MicRpm::Reading reading;reading.level=-38;reading.flatness=.25f;
  reading.candidateHz=101.5f;reading.candidateCycles=2;reading.support=6;
  reading.score=46;reading.margin=5;reading.reason=MicRpm::Reason::Acquiring;
  reading.search=MicRpm::Search::Prior;
  std::fill(std::begin(reading.audio.bands),std::end(reading.audio.bands),77);
  auto m=measure(reading);
  assert(m[0]==203 && !m[1] && m[2]==90 && m[3]==64 && m[6]==(7|16|128));
  r.add(400,650,m);
  m[2]=95;m[3]=100;m[4]=47;m[8]=90;
  r.add(400,440,m);
  m[0]=m[1]=0;m[6]=3;m[3]=200;
  r.add(400,441,m);
  m[6]=2;m[3]=255; // Clipping has no spectrum, not zero flatness.
  r.add(400,441,m);
  std::vector<uint8_t> bytes(MAX_BYTES);
  size_t n=r.pack(bytes.data(),82000,1200,3);
  assert(valid(bytes.data(),n) && n==HEADER+202*RECORD_BYTES+4);
  assert(get32(bytes.data()+12)==1200 && get32(bytes.data()+16)==3);
  auto row=bytes.data()+HEADER+RECORD_BYTES;
  assert(bytes[HEADER+16]==0 && row[16]==4 && row[17]==0x11 && row[18]==13);
  assert(row[0]==203 && row[2]==95 && row[3]==121 && row[4]==47 && row[8]==90);
  auto before=bytes;
  r.freeze();r.add(1000,1000,m);assert(r.pack(bytes.data(),82000,1200,3)==n && bytes==before);
  // Completed windows beyond a delayed stop cannot leak into its final bin.
  r.pack(bytes.data(),350,0,0);assert(bytes[HEADER+RECORD_BYTES+16]==0);
  r.reset(13);assert(r.launch()==13);
  n=r.pack(bytes.data(),0,0,0);assert(n==36 && valid(bytes.data(),n));
  n=pack(bytes.data(),nullptr,300000,0,0,false);
  assert(n==MAX_BYTES && valid(bytes.data(),n) && !bytes[30]);
  bytes[22]=0;put32(bytes.data()+n-4,crc(bytes.data(),n-4));assert(!valid(bytes.data(),n));
}
static void fullSession(){
  Recorder r;r.reset(1);Measurement m{};m[3]=m[4]=m[5]=255;m[6]=1;m[2]=40;
  // Acquisition can process ten windows per second. Every one contributes,
  // including quiet/broadband windows and those outside accepted RPM data.
  for(unsigned ms=400;ms<=300000;ms+=100)r.add(ms,ms+1,m);
  r.add(300001,300001,m);
  std::vector<uint8_t> bytes(MAX_BYTES);auto n=r.pack(bytes.data(),300000,0,0);
  assert(n==MAX_BYTES && valid(bytes.data(),n));
  unsigned frames=0;
  for(unsigned i=0;i<MAX_RECORDS;++i)frames+=bytes[HEADER+i*RECORD_BYTES+16];
  assert(frames==2997 && bytes[HEADER+149*RECORD_BYTES+16]==2 && bytes[HEADER+150*RECORD_BYTES+16]==10);
  assert(begin(150)==30000 && end(419)==300000 && records(30001)==151 && records(30000)==150);
  for(unsigned duration:{0u,1u,199u,200u,30000u,30001u,82000u,300000u,300001u}){
    std::vector<uint8_t> exact(packedBytes(duration)+16,0xa5);
    assert(r.pack(exact.data(),duration,0,0)==packedBytes(duration));
    assert(valid(exact.data(),packedBytes(duration)));
    for(unsigned i=packedBytes(duration);i<exact.size();++i)assert(exact[i]==0xa5);
  }
  r.reset(2);r.add(400,5500,m);r.pack(bytes.data(),1000,0,0);
  assert(bytes[HEADER+RECORD_BYTES+18]==255); // Do not invent availability for a long stall.
}
static void spectrum(){
  auto space=std::make_unique<MicRpm::Space>();MicRpm::Tracker tracker(space.get());
  std::vector<float> signal(MicRpm::WINDOW);
  for(unsigned i=0;i<signal.size();++i)signal[i]=.2f*std::sin(2*float(M_PI)*100*i/MicRpm::TRACK_HZ);
  tracker.reset(6000,false);auto normal=tracker.analyze(signal.data(),.2f);
  for(auto b:normal.audio.bands)assert(b==0);
  tracker.reset(6000,true);auto analysis=tracker.analyze(signal.data(),.2f);
  assert(analysis.audio.bands[1]>analysis.audio.bands[0]+20 && analysis.audio.bands[1]>analysis.audio.bands[2]+20);
  assert(normal.valid==analysis.valid && normal.reason==analysis.reason && normal.candidateHz==analysis.candidateHz && normal.score==analysis.score);
  std::fill(signal.begin(),signal.end(),0.f);
  auto quiet=tracker.analyze(signal.data(),.4f);assert(quiet.reason==MicRpm::Reason::Quiet);
  assert(measure(quiet)[3]==255 && !quiet.audio.bands[0]);
  std::fill(signal.begin(),signal.end(),1.f);
  auto clipped=tracker.analyze(signal.data(),.6f);assert(clipped.reason==MicRpm::Reason::Clipping && !clipped.audio.bands[0]);
  uint32_t seed=1;
  for(auto &v:signal){seed=1664525u*seed+1013904223u;v=float(double(seed)/4294967296.0-.5)*.5f;}
  auto noise=tracker.analyze(signal.data(),.8f);
  assert(noise.reason==MicRpm::Reason::BroadbandNoise && noise.audio.bands[0] && measure(noise)[3]!=255);
}
int main(){summaries();fullSession();spectrum();std::puts("PASS: full-session audio metadata, gaps, timing, spectrum and ordinary mic isolation");}
