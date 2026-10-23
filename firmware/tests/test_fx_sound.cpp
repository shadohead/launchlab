// A loaded effect's sound (fx_effect.h LoadableFx::Sound, the SOUND
// section version 2; docs/FIRMWARE_LOADABLE_EFFECTS.md, "Sound"): native
// voices whose parameters a control block sets every 2 ms, and notes timed
// to the sample. The ported sounds of Orbit, Sparkles and Slash match the
// built-in synth's loudness in every 20 ms window within 1 dB and keep
// their exact samples (pinned, so a later engine plays them unchanged);
// any slicing renders the same samples; parameters glide in a straight
// line between steps; triggers, notes, attack, decay and length land on
// their samples; noise voices sharing a seed share the noise; hostile
// values stay finite and bounded; the loader refuses what a later engine
// would add (Error::Engine) and what is malformed; the compiler reports
// source errors by line.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
#include "../tools/fx/fx_compiler.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

using namespace LoadableFx;

namespace {
std::vector<uint8_t> compileText(const std::string &text) {
  const auto r=FxCompiler::compile(text);
  if(!r.ok)std::cerr<<r.line<<": "<<r.error<<"\n";
  assert(r.ok);return r.bytes;
}
std::vector<uint8_t> compileFile(const std::string &path) {
  std::ifstream in(path);std::stringstream text;text<<in.rdbuf();
  assert(!text.str().empty());
  return compileText(text.str());
}
void reseal(std::vector<uint8_t> &f){put32(f.data()+24,uint32_t(f.size()));put32(f.data()+28,0);put32(f.data()+28,crc32(f.data(),f.size()));}
size_t sectionAt(const std::vector<uint8_t> &f,uint8_t type) {
  for(size_t at=HEADER_BYTES;at+4<=f.size();at+=4+get16(f.data()+at+2))if(f[at]==type)return at;
  return 0;
}
std::vector<int16_t> render(const std::vector<uint8_t> &f,size_t slice=SIZE_MAX,Sound *out=nullptr) {
  Program p;const Error e=load(f.data(),f.size(),p);
  if(e!=Error::None)std::cerr<<"refused: "<<label(e)<<"\n";
  assert(e==Error::None && p.hasSound);
  static Sound sound;Sound &s=out?*out:sound;
  std::vector<int16_t> samples(p.soundSamples());s.begin(p);
  for(size_t at=0;at<samples.size();){const size_t n=s.run(samples.data()+at,std::min(slice,samples.size()-at));assert(n>0);at+=n;}
  assert(s.done() && s.run(samples.data(),1)==0);
  return samples;
}
uint64_t fnv64(const std::vector<int16_t> &v) {
  uint64_t h=0xcbf29ce484222325ull;
  for(int16_t s:v)for(int k=0;k<2;++k){h^=uint8_t(uint16_t(s)>>(8*k));h*=0x100000001b3ull;}
  return h;
}
// The engine's sample of a level v at sample i of n: the limiter's linear part.
int16_t level(float v){return int16_t(v*32767);}
}

int main() {
  using Appearance::Effect;
  // ---- the ported sounds ----
  // Each against the built-in synth: loudness in every 20 ms window where
  // the native sound is above 300 rms (about -41 dBFS) within 1 dB, the
  // waveform correlation, any slicing, and the samples pinned (FNV-1a of
  // the little-endian samples): a later engine must render them unchanged.
  // nativeMs: the built-in synth's render on the device, Orbit's measured
  // (spike run 2), the others by the same count of operations as the model.
  struct Port {const char *path;Effect native;uint64_t hash;double maxDb,minCorrelation,nativeMs;};
  const Port ports[]={
    {"firmware/tools/fx/effects/idiom/orbit.fx",Effect::Orbit,0xaa082d1ba7de25a9ull,.1,.999,106},
    {"firmware/tools/fx/effects/idiom/sparkles.fx",Effect::Sparkles,0xda4079322523f1e4ull,.1,.999,490},
    {"firmware/tools/fx/effects/idiom/slash.fx",Effect::Slash,0xe4730fee0f6055dfull,.5,.999,200},
    {"firmware/tools/fx/effects/orbit.fx",Effect::Orbit,0xaa082d1ba7de25a9ull,.1,.999,106},
  };
  for(const Port &port:ports) {
    const auto f=compileFile(port.path);
    Sound sound;const auto got=render(f,SIZE_MAX,&sound);
    assert(got.size()==BestEffectSound::samples(port.native));
    std::vector<int16_t> want(got.size());BestEffectSound::render(port.native,want.data(),want.size());
    for(size_t slice:{size_t(1),size_t(31),size_t(32),size_t(250),size_t(333)})assert(render(f,slice)==got);
    double dot=0,ww=0,gg=0,worst=0;unsigned windows=0;
    for(size_t i=0;i<got.size();++i){dot+=double(want[i])*got[i];ww+=double(want[i])*want[i];gg+=double(got[i])*got[i];}
    for(size_t lo=0;lo+320<=got.size();lo+=320) {
      double a=0,b=0;for(size_t i=lo;i<lo+320;++i){a+=double(want[i])*want[i];b+=double(got[i])*got[i];}
      if(a>320.0*300*300){++windows;worst=std::max(worst,std::fabs(10*std::log10(b/a)));}
    }
    const double correlation=dot/std::sqrt(ww*gg);const auto &w=sound.work();
    Program p;load(f.data(),f.size(),p);
    std::printf("%s: %zu samples, %u voices, %u notes, %u instructions a step; loudness within %.3f dB in %u windows, correlation %.5f; "
                "%u steps, %u instructions, %u voice samples (%u noise, %u filtered, %u with partials), %u idle noise; hash %016llx\n",
                port.path,got.size(),p.soundVoices,p.soundEvents,p.soundLength,worst,windows,correlation,w.steps,w.ops,w.voiceSamples,
                w.noiseSamples,w.filterSamples,w.partialSamples,w.idleNoise,(unsigned long long)fnv64(got));
    assert(worst<port.maxDb && worst<1 && windows>20 && correlation>port.minCorrelation);
    // The static bound: one control run a step and one ahead.
    assert(w.steps==(got.size()+SOUND_STEP-1)/SOUND_STEP && w.ops<=(w.steps+1)*p.soundLength);
    // The device cost model (docs/FIRMWARE_LOADABLE_EFFECTS.md, "Sound"):
    // 0.46 us an instruction, and the voices' work in operations of 75 ns,
    // the unit that makes the built-in synth's Orbit (1.41 million
    // operations by its code) take the 0.106 s the device measured.
    const double operations=16.0*got.size()+22.0*w.voiceSamples+3.0*w.noiseSamples+6.0*w.filterSamples+5.0*w.partialSamples+
                            6.0*w.attackSamples+7.0*w.idleNoise+50.0*w.steps*p.soundVoices;
    const double modelledMs=(.46*w.ops+.075*operations)/1000;
    std::printf("  modelled on the device: %.0f ms (%.0f ms of it the control block); the built-in synth: %.0f ms\n",modelledMs,.46*w.ops/1000,port.nativeMs);
    assert(modelledMs<2*port.nativeMs);
    assert(fnv64(got)==port.hash);
  }

  // ---- what the voices do ----
  const std::string head="effect \"S\"\nduration 2000; frame { clip(H) }\nsound 200\n";  // 3,200 samples, 100 steps
  // Gain moves in a straight line from one step's value to the next: a
  // square at 0 Hz reads -1 every sample, so a sample is -gain.
  {
    const auto s=render(compileText(head+"voice v = tone(wave: square)\nsound {\n v(gain: .5*(i % 2 == 1))\n}\n"));
    for(uint32_t k=1;k<87;++k) {  // clear of the limiter's 2 ms in and 25 ms out
      const float g0=.5f*float(k%2==1),g1=.5f*float((k+1)%2==1),dg=(g1-g0)*(1.0f/SOUND_STEP);float g=g0;
      for(uint32_t j=0;j<SOUND_STEP;++j,g+=dg)assert(s[k*SOUND_STEP+j]==level(-g));
    }
  }
  // A note lands on the first sample at or after its time; length stops it.
  {
    const auto s=render(compileText(head+"voice v = tone(wave: square, length: .005)\nnote(v, at: .0123, level: .4)\n"));
    uint32_t at=0;while(Sound::timeOf(at)<.0123f)++at;
    assert(at==197 && s[at-1]==0 && s[at]==level(-.4f) && s[at+79]==level(-.4f) && s[at+80]==0);
  }
  // A trigger above 0 restarts the envelope on the step's first sample, the
  // attack is a smoothstep, and the decay is e^(-rate t) to within 1e-4.
  {
    const auto s=render(compileText(head+"voice v = tone(wave: square, attack: .002, decay: 5.5)\nsound {\n v(trigger: (i == 10)*.5)\n}\n"));
    assert(s[319]==0 && s[320]==0);  // the attack starts from 0
    for(uint32_t j=1;j<32;++j){const float u=j/32.0f;assert(std::abs(s[320+j]-level(-.5f*u*u*(3-2*u)*std::exp(-5.5f*j/16000)))<=2);}
    // A second's decay: the factor must be as exact as the built-in synth's
    // std::exp (Math::exp near 1 made it 4% quieter after half a second).
    const auto t=render(compileText("effect \"S\"\nduration 2000; frame { clip(H) }\nsound 1500\nvoice v = tone(wave: square, decay: 5.5)\nnote(v, at: 0, level: .5)\n"));
    const double after=-t[8000]/32767.0,want=.5*std::exp(-5.5*.5);  // half a second
    assert(std::fabs(after/want-1)<2e-3);
  }
  // A voice played by notes takes their frequency, times bend: a saw's step
  // from one sample to the next is twice the frequency over 16 kHz.
  {
    const auto s=render(compileText(head+"voice v = tone(wave: saw, gain: .25)\nnote(v, at: .01, hz: 500)\nsound {\n v(bend: 1 + (t >= .05))\n}\n"));
    // Most steps between samples (all but the saw's jumps) at 25 and 94 ms: 500 Hz, then 1,000 Hz.
    auto rises=[&](uint32_t from,float hz){unsigned n=0;for(uint32_t i=from;i<from+32;++i)n+=std::abs((s[i+1]-s[i])-level(.25f*2*hz/16000))<=1;return n;};
    assert(rises(400,500)>=28 && rises(1500,1000)>=26);
  }
  // A tone restarts from phase 0 at each note: the sample after a note is the sine's first, 0.
  {
    const auto s=render(compileText(head+"voice v = tone(attack: 0)\nnote(v, at: .02, hz: 1000)\nnote(v, at: .05, hz: 1000)\n"));
    assert(s[320]==0 && s[800]==0 && s[321]!=0 && s[801]==s[321]);
  }
  // Noise runs every sample, sounding or not: a voice that starts at 50 ms
  // plays the seed's xorshift sequence from its 801st value, as the synth's
  // one noise generator does; a second voice of that seed hears the same.
  {
    const auto s=render(compileText(head+"voice a = noise(seed: 12345)\nvoice b = noise(seed: 12345)\n"
                                         "sound {\n a(gain: .2*(t >= .05))\n b(gain: .1*(t >= .1))\n}\n"));
    uint32_t x=12345;std::vector<float> noise(3200);
    for(float &v:noise){x^=x<<13;x^=x>>17;x^=x<<5;v=float(int32_t(x))*(1.0f/2147483648.0f);}
    for(uint32_t i=1700;i<1800;++i){const float v=.2f*noise[i]+.1f*noise[i];assert(std::abs(s[i]-level(v))<=1);}
  }
  // Filters: a low-pass at k=1 passes the noise unchanged; a high-pass at k=1 silences it.
  {
    const auto low=render(compileText(head+"voice a = noise(seed: 7, filter: low, k: 1, gain: .3)\n"));
    const auto plain=render(compileText(head+"voice a = noise(seed: 7, gain: .3)\n"));
    const auto high=render(compileText(head+"voice a = noise(seed: 7, filter: high, k: 1, gain: .3)\n"));
    assert(low==plain && std::all_of(high.begin(),high.end(),[](int16_t v){return v==0;}));
  }
  // Hostile values are clamped: every sample is finite and within the
  // limiter's 0.95 of full scale, whatever the file asks for.
  {
    const auto s=render(compileText(head+"voice v[4] = tone(octave: 9, cubic: -9)\nvoice w = noise(seed: 1, filter: band, k2: 1)\n"
      "notes 4 {\n note(v[i], at: .001*i, level: 8, hz: 7999)\n}\nsound {\n big = 1e30*(i + 1)\n nan = big*big - big*big\n"
      " v(gain: big, bend: nan, attack: -big, decay: nan, length: big, trigger: (i % 3 == 0)*big)\n w(gain: -big, k: nan)\n}\n"));
    for(int16_t v:s)assert(v>=-31130 && v<=31130);
  }
  // A silent sound with nothing to play is all zeros, and a sound without
  // a control block plays its notes.
  {
    const auto s=render(compileText(head+"voice v = tone(hz: 440, gain: 0)\n"));
    assert(std::all_of(s.begin(),s.end(),[](int16_t v){return v==0;}));
  }

  // ---- the file ----
  const auto orbit=compileFile("firmware/tools/fx/effects/idiom/orbit.fx");
  const size_t sound=sectionAt(orbit,SOUND)+4;
  assert(sound>4);
  auto refused=[&](std::vector<uint8_t> f,Error want){reseal(f);Program q;const Error e=load(f.data(),f.size(),q);
    if(e!=want)std::cerr<<"got "<<label(e)<<" want "<<label(want)<<"\n";assert(e==want);};
  {Program q;assert(load(orbit.data(),orbit.size(),q)==Error::None && q.soundVoices==4 && q.soundEvents==3 && q.soundLength>0);}
  // What a later engine adds is refused as needing it, never misplayed.
  {auto f=orbit;f[5]=ENGINE+1;refused(f,Error::Engine);}                         // a newer engine level
  {auto f=orbit;f[sound]=SOUND_VERSION+1;refused(f,Error::Engine);}              // a newer sound
  {auto f=orbit;f[sound+2]=SOUND_STEP_SHIFT+1;refused(f,Error::Engine);}         // another control rate
  {auto f=orbit;f[sound+3]=2;refused(f,Error::Engine);}                          // two outputs
  {auto f=orbit;put16(f.data()+sound+10,1);refused(f,Error::Engine);}            // a sample bank
  const size_t voice=sound+SOUND_HEAD,event=voice+4*VOICE_BYTES;
  {auto f=orbit;f[voice]=VOICE_TYPES;refused(f,Error::Engine);}                  // a voice type it does not know
  {auto f=orbit;f[voice+1]=WAVES;refused(f,Error::Engine);}                      // a waveform
  {auto f=orbit;f[voice+2]=FILTERS;refused(f,Error::Engine);}                    // a filter
  {auto f=orbit;f[voice+3]=0x80;refused(f,Error::Engine);}                       // a voice flag
  {auto f=orbit;f[voice+18]=1;refused(f,Error::Engine);}                         // a parameter past the ten it knows
  {auto f=orbit;f[event+5]=EVENT_KINDS;refused(f,Error::Engine);}                // an event kind
  {auto f=orbit;f[event+6]=1;refused(f,Error::Engine);}
  // The prototype's per-sample block (version 1) is not played.
  {auto f=orbit;f[sound]=1;refused(f,Error::Section);}
  // Malformed.
  {auto f=orbit;f[sound+1]=0;refused(f,Error::Section);}                         // no voices
  {auto f=orbit;f[sound+1]=MAX_VOICES+1;refused(f,Error::Section);}
  {auto f=orbit;f[sound+1]=3;refused(f,Error::Section);}                         // the records no longer fit the length
  {auto f=orbit;put16(f.data()+sound+6,MAX_SOUND_CODE+1);refused(f,Error::Section);}  // a control block over the cap
  {auto f=orbit;put16(f.data()+sound+4,0xfff0);refused(f,Error::Section);}       // outside the code
  {auto f=orbit;put16(f.data()+sound+8,4);refused(f,Error::Section);}            // more notes than the section holds
  {auto f=orbit;put32(f.data()+event,27200);refused(f,Error::Section);}  // a note at the sound's end
  {auto f=orbit;put32(f.data()+event+EVENT_BYTES,0);refused(f,Error::Section);}  // notes out of order
  {auto f=orbit;f[event+4]=4;refused(f,Error::Section);}                         // a voice it does not have
  {auto f=orbit;put32(f.data()+event+8,0x7f800000);refused(f,Error::Section);}   // an infinite level
  {auto f=orbit;put32(f.data()+voice+4,1);refused(f,Error::Section);}            // a tone with a seed
  {auto f=orbit;f[voice+8]=BUILTINS;refused(f,Error::Register);}                 // a parameter in an undefined register
  {auto f=orbit;f[voice+8]=255;refused(f,Error::Register);}                      // past the constants
  {auto f=orbit;put16(f.data()+18,1000);put16(f.data()+20,700);put16(f.data()+22,0);refused(f,Error::Section);}  // voices without a length
  {
    // A control block may not draw.
    Program q;load(orbit.data(),orbit.size(),q);auto f=orbit;
    const size_t code=sectionAt(f,CODE)+4+size_t(q.soundStart)*4;f[code]=DRAW;f[code+1]=PLOT;f[code+2]=0;f[code+3]=0;refused(f,Error::Primitive);
  }
  {
    // A noise voice needs a seed.
    auto f=compileText(head+"voice a = noise(seed: 3)\n");const size_t at=sectionAt(f,SOUND)+4+SOUND_HEAD;
    put32(f.data()+at+4,0);refused(f,Error::Section);
  }
  // The placed (playing) program copies only the blocks' code to internal
  // RAM: the control block stays in the file, and the placed program draws only.
  {
    Program q;assert(load(orbit.data(),orbit.size(),q)==Error::None);
    const size_t all=size_t(q.codeLength)*4+size_t(q.constCount)*4+q.argsLength;
    alignas(4) static uint8_t hot[HOT_BYTES];
    assert(hotBytes(q)==all-size_t(q.soundLength)*4 && place(q,hot,sizeof(hot)) && !q.hasSound && q.soundSamples()==0);
  }

  // ---- the compiler ----
  auto fails=[&](const std::string &body,int line,const char *what) {
    const auto r=FxCompiler::compile(head+body);
    if(r.ok || r.line!=line || r.error.find(what)==std::string::npos)std::cerr<<"compile: line "<<r.line<<" '"<<r.error<<"' want "<<line<<" '"<<what<<"'\n";
    assert(!r.ok && r.line==line && r.error.find(what)!=std::string::npos);
  };
  fails("sound {\n x = 1\n}\n",4,"declare the sound's voices");
  fails("voice v = tone()\nsound {\n out = sin(t)\n}\n",6,"not out");
  fails("voice v = tone(decay: 3)\n",4,"no note or trigger plays it");
  fails("voice v = tone(hz: 2)\nnote(v, at: .1, hz: 440)\n",4,"give it bend");
  fails("voice v = tone(bend: 2)\nnote(v, at: .1)\n",4,"no note gives it a frequency");
  fails("voice v = tone()\nnote(v, at: .2)\n",5,"after the sound's end");
  fails("voice v = noise(hz: 3)\n",4,"no hz");
  fails("voice v = noise(seed: 0)\n",4,"a seed is a whole number");
  fails("voice v = noise(seed: 1.5)\n",4,"a seed is a whole number");
  fails("voice v = tone(wave: wobble)\n",4,"wave is sine");
  fails("voice v = tone(gain: t)\n",4,"known when compiling");
  fails("voice v = tone(gain: 1)\nsound {\n v(gain: t)\n}\n",6,"set both");
  fails("voice v[17] = tone()\n",4,"1-16 voices");
  fails("voice v[9] = tone()\nvoice w[8] = tone()\n",5,"at most 16 voices");
  fails("voice v[2] = tone()\nnote(v[2], at: .1)\n",5,"voices 0-1");
  fails("voice v[2] = tone()\nsound {\n v[2](gain: 1)\n}\n",6,"voices 0-1");
  fails("voice v = tone()\nframe {\n v(gain: 1)\n}\n",6,"only the sound block");
  fails("voice v = tone()\nnote(w, at: .1)\n",5,"unknown voice");
  fails("voice v = tone()\nnote(v, level: 1)\n",5,"needs 'at'");
  fails("voice v = tone()\nnote(v, at: t)\n",5,"known when compiling");
  fails("voice v = tone()\nnote(v, at: .1, level: 9)\n",5,"level is 0-8");
  fails("voice v = tone()\nsound {\n v(volume: 1)\n}\n",6,"no parameter 'volume'");
  {
    // At most 128 instructions a step.
    std::string body="voice v = tone()\nsound {\n";
    for(int k=0;k<70;++k)body+=" a"+std::to_string(k)+" = sin(t*"+std::to_string(k+2)+")\n";
    body+=" v(gain: a0 + a69)\n}\n";
    const auto r=FxCompiler::compile(head+body);assert(!r.ok && r.error.find("at most 128 instructions")!=std::string::npos);
  }
  // Notes to a whole array go to a free voice: the first whose last note
  // has ended (its length), else the one that started longest ago.
  {
    const auto f=compileText(head+"voice v[2] = tone(length: .01)\nnote(v, at: .0, hz: 100)\nnote(v, at: .005, hz: 200)\nnote(v, at: .02, hz: 300)\n"
                                    "note(v, at: .021, hz: 400)\nnote(v, at: .022, hz: 500)\n");
    Program q;assert(load(f.data(),f.size(),q)==Error::None && q.soundEvents==5);
    const uint8_t want[]={0,1,0,1,0};  // 0 free at 20 ms; 1 free at 21 ms; 22 ms: both busy, 0 started first
    for(unsigned e=0;e<5;++e)assert(q.soundEvent[e*EVENT_BYTES+4]==want[e]);
  }
  std::puts("test_fx_sound: ok");
}
