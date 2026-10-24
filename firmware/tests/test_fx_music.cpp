// Engine level 2's music (fx_effect.h, docs/FIRMWARE_SOUND_0_20.md): pulse,
// wavetable, FM, drum and stepped-noise voices and the SONG section.
// A file says level 2 only when it uses them, so every 0.19 source still
// compiles to level 1, and a level-1 file cannot use them. Each voice
// matches an independent reference; the drum bank's samples are pinned (it
// is append-only); song rows land on their samples, waits, voice changes,
// volumes, note offs and the order play as written; any slicing renders the
// same samples; the loader refuses every malformed song, and mutated songs
// under ASan/UBSan never read outside the file nor render a non-finite or
// unbounded sample; the compiler reports song errors by line; the example
// songs' samples are pinned.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
#include "../tools/fx/fx_compiler.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace LoadableFx;

namespace {
std::vector<uint8_t> compileText(const std::string &text) {
  const auto r=FxCompiler::compile(text);
  if(!r.ok)std::fprintf(stderr,"%d: %s\n",r.line,r.error.c_str());
  assert(r.ok);return r.bytes;
}
std::string readFile(const std::string &path){std::ifstream in(path);std::stringstream s;s<<in.rdbuf();assert(!s.str().empty());return s.str();}
FxCompiler::Result compileResult(const std::string &text){return FxCompiler::compile(text);}
void reseal(std::vector<uint8_t> &f){put32(f.data()+24,uint32_t(f.size()));put32(f.data()+28,0);put32(f.data()+28,crc32(f.data(),f.size()));}
size_t sectionAt(const std::vector<uint8_t> &f,uint8_t type) {
  for(size_t at=HEADER_BYTES;at+4<=f.size();at+=4+get16(f.data()+at+2))if(f[at]==type)return at;
  return 0;
}
std::vector<int16_t> render(const std::vector<uint8_t> &f,size_t slice=SIZE_MAX) {
  Program p;const Error e=load(f.data(),f.size(),p);
  if(e!=Error::None)std::fprintf(stderr,"refused: %s\n",label(e));
  assert(e==Error::None && p.hasSound);
  static Sound s;std::vector<int16_t> out(p.soundSamples());s.begin(p);
  for(size_t at=0;at<out.size();){const size_t n=s.run(out.data()+at,std::min(slice,out.size()-at));assert(n>0);at+=n;}
  assert(s.done());
  return out;
}
uint64_t fnv64(const std::vector<int16_t> &v) {
  uint64_t h=0xcbf29ce484222325ull;
  for(int16_t s:v)for(int k=0;k<2;++k){h^=uint8_t(uint16_t(s)>>(8*k));h*=0x100000001b3ull;}
  return h;
}
// A file with a header and these voices, optional notes and a song, with a
// silent frame (the sound is what is tested).
std::string effect(const std::string &body,int soundMs=1000) {
  return "effect \"Music test\"\nuid 0x4c4c4d5400000001\nduration 3000\nreadout 900\n"+(soundMs?"sound "+std::to_string(soundMs)+"\n":"")+
         "frame { x = W }\n"+body;
}
// The engine's first sample at or after a time on the 16 kHz clock.
uint32_t sampleAt(float seconds){uint32_t s=uint32_t(double(seconds)*SOUND_RATE);while(Sound::timeOf(s)<seconds)++s;return s;}
// The limiter's linear part: |v| <= .6 renders v*32767.
int16_t linear(float v){return int16_t(v*32767);}

// An IMA ADPCM decoder written from the specification, for the bank.
std::vector<int16_t> decodeBank(unsigned entry) {
  static const int STEP[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,
    279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
    5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
  static const int INDEX[16]={-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
  const auto &e=Drums::BANK[entry];std::vector<int16_t> out;int pred=0,idx=0;
  for(uint32_t i=0;i<e.samples;++i) {
    const int nib=(e.data[i/2]>>((i%2)*4))&15,step=STEP[idx];
    int diff=step>>3;if(nib&1)diff+=step>>2;if(nib&2)diff+=step>>1;if(nib&4)diff+=step;
    pred+=nib&8?-diff:diff;pred=std::max(-32768,std::min(32767,pred));idx=std::max(0,std::min(88,idx+INDEX[nib]));
    out.push_back(int16_t(pred));
  }
  return out;
}
}

int main() {
  // ---- levels ----
  // Every 0.19 source compiles to level 1 exactly as before; music says 2.
  const char *ported[]={"firmware/tools/fx/effects/idiom/orbit.fx","firmware/tools/fx/effects/idiom/sparkles.fx",
    "firmware/tools/fx/effects/idiom/slash.fx","firmware/tools/fx/effects/idiom/tidal.fx","firmware/tools/fx/effects/orbit.fx"};
  for(const char *path:ported){const auto f=compileText(readFile(path));assert(f[5]==1);}
  const char *songs[]={"firmware/tools/fx/effects/music/victory_lap.fx","firmware/tools/fx/effects/music/night_spin.fx",
    "firmware/tools/fx/effects/music/ripcord.fx"};
  for(const char *path:songs){const auto f=compileText(readFile(path));assert(f[5]==2 && sectionAt(f,SONG) && (f[sectionAt(f,SONG)+1]&1));}
  {
    // A level-1 header using a level-2 voice or stepped noise is refused as a later engine's.
    for(const char *voice:{"voice p = pulse()\nnote(p, at: .1)\n","voice w = wave(points: [0, 1, 0, -1])\nnote(w, at: .1)\n",
                           "voice f = fm()\nnote(f, at: .1)\n","voice d = drum(kick)\nnote(d, at: .1)\n","voice nz = noise(step: 2000)\nnote(nz, at: .1)\n"}) {
      auto f=compileText(effect(voice));assert(f[5]==2);
      Program p;assert(load(f.data(),f.size(),p)==Error::None);
      f[5]=1;reseal(f);assert(load(f.data(),f.size(),p)==Error::Engine);
    }
    // A level-1 file is still refused by a 0.19 engine only above its level: a level-3 file is refused here.
    auto f=compileText(effect("voice p = pulse()\nnote(p, at: .1)\n"));f[5]=3;reseal(f);
    Program p;assert(load(f.data(),f.size(),p)==Error::Engine);
  }

  // ---- voices ----
  {
    // Pulse: the right frequency and duty, centred, its edges band-limited.
    const auto f=compileText(effect("voice p = pulse(duty: .25, hz: 500, gain: .5)\n"));
    const auto s=render(f);
    // The duty by the mean: a pulse of +-a with duty d averages a*(2d-1).
    unsigned rises=0;double mean=0;
    for(size_t i=1600;i<14400;++i){rises+=s[i-1]<0 && s[i]>=0;mean+=s[i];}
    mean/=12800;
    assert(rises>=399 && rises<=401);                                  // 0.8 s at 500 Hz
    assert(std::fabs(mean/linear(.5f)-(2*.25-1))<.02);
    // Band-limited: a naive pulse jumps between +-.5 in one sample, this one never.
    int maxStep=0;for(size_t i=1601;i<14400;++i)maxStep=std::max(maxStep,std::abs(s[i]-s[i-1]));
    assert(maxStep<linear(1.0f)*0.97f);
    // The duty's default is .5.
    const auto g=render(compileText(effect("voice p = pulse(hz: 250, gain: .5)\n")));
    double m2=0;for(size_t i=1600;i<14400;++i)m2+=g[i];
    assert(std::fabs(m2/12800/linear(.5f))<.02);
  }
  {
    // Wavetable: points [0, 1, 0, -1] at 100 Hz read with interpolation: a triangle.
    const auto s=render(compileText(effect("voice w = wave(points: [0, 1, 0, -1], hz: 100, gain: .5)\n")));
    // Sample i is at phase i*100/16000 turns: point index 4*phase.
    for(uint32_t i:{1000u,1010u,1020u,1030u,1040u,1050u,1077u,1123u}) {
      const double turns=std::fmod(double(i)*100/16000,1.0),pos=turns*4;const int k=int(pos);const double fr=pos-k;
      const double pts[5]={0,1,0,-1,0};const double want=(pts[k]+(pts[k+1]-pts[k])*fr)*.5;
      assert(std::abs(s[i]-linear(float(want)))<=40);
    }
  }
  {
    // FM with index 0 is a sine; with an index it is bounded and periodic at the carrier.
    const auto plain=render(compileText(effect("voice f = fm(index: 0, hz: 400, gain: .5)\n")));
    const auto sine=render(compileText(effect("voice f = tone(hz: 400, gain: .5)\n")));
    for(size_t i=200;i<15000;++i)assert(std::abs(plain[i]-sine[i])<=2);
    const auto bell=render(compileText(effect("voice f = fm(ratio: 2, index: 3, hz: 400, gain: .5)\n")));
    // 400 Hz with a 2:1 modulator repeats every 40 samples (within the sine
    // table's 1,024-point steps, which a modulated phase crosses quickly).
    double diff=0,power=0;
    for(size_t i=1000;i<14000;++i){const double d=bell[i]-bell[i+40];diff+=d*d;power+=double(bell[i])*bell[i];}
    assert(diff<power*1e-3);
    int peak=0;for(int16_t v:bell)peak=std::max(peak,std::abs(int(v)));
    assert(peak<=linear(.5f)+2);
  }
  {
    // Drum: the bank's samples as decoded from the specification, at rate 1;
    // at rate 2 it ends in half the samples; it stops at its end.
    for(unsigned k=0;k<Drums::COUNT;++k) {
      const std::string name=Drums::BANK[k].name;
      const auto s=render(compileText(effect("voice d = drum("+name+", gain: .5)\nnote(d, at: 0)\n",1000)));
      const auto ref=decodeBank(k);
      const uint32_t fade=SOUND_RATE/500;  // the limiter's 2 ms fade-in
      for(size_t i=fade;i<ref.size()-1;++i){const float want=float(ref[i])/32768*.5f;if(std::fabs(want)<.55f)assert(std::abs(s[i]-linear(want))<=2);}
      for(size_t i=ref.size();i<s.size();++i)assert(s[i]==0);
      const auto fast=render(compileText(effect("voice d = drum("+name+", gain: .5, rate: 2)\nnote(d, at: 0)\n",1000)));
      size_t last=0;for(size_t i=0;i<fast.size();++i)if(fast[i])last=i;
      assert(last<=ref.size()/2+2);
    }
    // The bank is part of the format: its entries never change (append only).
    const uint64_t pinned[]={0xeac09904d367ca77ull,0x87beb49d86f90970ull,0x718f99eace4044b0ull,0x849662e4882ebe91ull,
                             0x3810040eae5d3741ull,0xf44f0a3174a33a71ull,0x2c6b3ff8c248d872ull,0xa9b20e1728344ca9ull};
    static_assert(sizeof(pinned)/sizeof(*pinned)==Drums::COUNT,"one pinned hash per drum");
    for(unsigned k=0;k<Drums::COUNT;++k) {
      const uint64_t h=fnv64(decodeBank(k));
      if(h!=pinned[k])std::fprintf(stderr,"drum %s hash %016llx\n",Drums::BANK[k].name,(unsigned long long)h);
      assert(h==pinned[k]);
    }
  }
  {
    // Stepped noise: a new value at most 2000 times a second, held between.
    const auto s=render(compileText(effect("voice nz = noise(step: 2000, gain: .4)\n")));
    unsigned changes=0;for(size_t i=1;i<16000;++i)changes+=s[i]!=s[i-1] && i>32 && i<15600;
    assert(changes>=1500 && changes<=2001);
  }

  // ---- songs ----
  {
    // A row lands on its sample: tempo 120 at 4 rows a beat is 2000 samples a
    // row. A tone without attack starts on its note's sample exactly.
    const auto f=compileText(effect("voice a = tone(gain: .5)\nvoice b = tone(gain: .5)\n"
      "song(tempo: 120, rows: 4) {\n pattern p {\n  a: \"a4 . . . . off . .\"\n  b: \". . c5 . . . . .\"\n }\n order p p\n}\n",0));
    Program p;assert(load(f.data(),f.size(),p)==Error::None && p.hasSong && p.songRowSamples==2000);
    assert(p.soundSamples()==2u*8*2000);
    const auto s=render(f);
    assert(s.size()==32000);
    // a sounds from sample 0 (after the limiter's fade-in), b from row 2.
    assert(s[0]==0 && s[100]!=0);
    // Row 2's note: up to it only a, from it a + b.
    const auto solo=render(compileText(effect("voice a = tone(gain: .5)\nvoice b = tone(gain: .5)\n"
      "song(tempo: 120, rows: 4) {\n pattern p {\n  a: \"a4 . . . . off . .\"\n }\n order p p\n}\n",0)));
    for(size_t i=0;i<4000;++i)assert(s[i]==solo[i]);
    assert(s[4000]!=solo[4000] || s[4001]!=solo[4001]);
    // The off at row 5 releases a from the next step on: e-fold every 160 samples.
    const uint32_t off=5*2000,step=off/SOUND_STEP*SOUND_STEP==off?off:(off/SOUND_STEP+1)*SOUND_STEP;
    double before=0,after=0;for(uint32_t i=step+800;i<step+1000;++i)after+=double(solo[i])*solo[i];for(uint32_t i=off-400;i<off-200;++i)before+=double(solo[i])*solo[i];
    assert(after<before*1e-3);
    // The order repeats the pattern: its second play equals its first (past the fade-in).
    for(size_t i=200;i<16000-800;++i)assert(std::abs(solo[i]-solo[i+16000])<=1);
  }
  {
    // Volume: v8 halves the level a note starts at; a voice change plays the other voice.
    const auto loud=render(compileText(effect("voice a = tone(gain: .4, decay: .1)\n"
      "song(tempo: 120, rows: 4) {\n pattern p {\n  a: \"a4 . . .\"\n }\n order p\n}\n",0)));
    const auto soft=render(compileText(effect("voice a = tone(gain: .4, decay: .1)\n"
      "song(tempo: 120, rows: 4) {\n pattern p {\n  a: \"v8+a4 . . .\"\n }\n order p\n}\n",0)));
    for(size_t i=400;i<4000;++i)assert(std::abs(soft[i]*2-loud[i])<=2);
    const auto swapped=render(compileText(effect("voice a = tone(gain: .4)\nvoice b = pulse(gain: .2)\n"
      "song(tempo: 120, rows: 4) {\n pattern p {\n  x: \"a:a4 . b:a4 .\"\n }\n order p\n}\n",0)));
    // Until row 2 only the tone sounds; from row 2 the pulse joins it (the tone keeps ringing).
    double tone=0,both=0;for(size_t i=1000;i<3000;++i)tone+=double(swapped[i])*swapped[i];for(size_t i=4600;i<6600;++i)both+=double(swapped[i])*swapped[i];
    assert(both>tone);
  }
  {
    // Any slicing renders the same samples, songs and drums included.
    for(const char *path:songs) {
      const auto f=compileText(readFile(path));
      const auto whole=render(f);
      for(size_t slice:{size_t(1),size_t(7),size_t(32),size_t(333),size_t(2048)})assert(render(f,slice)==whole);
    }
  }
  {
    // The example songs, pinned: a later engine must play them unchanged.
    const uint64_t pinned[]={0x4f7022400aa1ec39ull,0x95ae5b94fa9c3c37ull,0xaf217fcf31c0fe1cull};
    for(size_t k=0;k<3;++k) {
      const auto s=render(compileText(readFile(songs[k])));
      int peak=0;for(int16_t v:s)peak=std::max(peak,std::abs(int(v)));
      std::printf("%s: %zu samples (%.1f s), peak %d, hash %016llx\n",songs[k],s.size(),double(s.size())/SOUND_RATE,peak,(unsigned long long)fnv64(s));
      assert(fnv64(s)==pinned[k]);
    }
  }

  // ---- the loader refuses malformed songs ----
  {
    const auto good=compileText(effect("voice a = pulse(gain: .3)\nvoice b = drum(kick)\n"
      "song(tempo: 120, rows: 4) {\n pattern p {\n  a: \"c5 . e5 off\"\n  b: \"b . b .\"\n }\n pattern q {\n  a: \"g4 . .\"\n }\n order p q p\n}\n",0));
    Program p;assert(load(good.data(),good.size(),p)==Error::None);
    const size_t at=sectionAt(good,SONG),body=at+4;
    auto mutate=[&](auto change,Error want) {
      auto f=good;change(f);reseal(f);Program q;const Error e=load(f.data(),f.size(),q);
      if(e!=want)std::fprintf(stderr,"wanted %s, got %s\n",label(want),label(e));
      assert(e==want);
    };
    mutate([&](auto &f){f[at+1]=0;},Error::Section);                  // not required: a 0.19 engine would ignore it
    mutate([&](auto &f){f[body]=2;},Error::Engine);                    // a later song version
    mutate([&](auto &f){f[body+1]=0;},Error::Section);                 // no channels
    mutate([&](auto &f){f[body+1]=9;},Error::Section);                 // too many channels
    mutate([&](auto &f){f[body+2]=0;},Error::Section);                 // no patterns
    mutate([&](auto &f){f[body+3]=1;},Error::Section);                 // reserved byte
    mutate([&](auto &f){put16(f.data()+body+4,399);},Error::Section);  // rows too short
    mutate([&](auto &f){put16(f.data()+body+4,16001);},Error::Section);// rows too long
    mutate([&](auto &f){put16(f.data()+body+6,0);},Error::Section);    // empty order
    mutate([&](auto &f){f[body+SONG_HEAD]=7;},Error::Section);         // a channel's voice out of range
    const size_t order=body+SONG_HEAD+2;
    mutate([&](auto &f){f[order]=5;},Error::Section);                  // an order entry past the patterns
    const size_t pattern=order+3;
    mutate([&](auto &f){f[pattern]=0;},Error::Section);                // a pattern without rows
    mutate([&](auto &f){f[pattern]=1;},Error::Section);                // events past its rows
    const size_t stream=pattern+3;  // channel a's first event (c5)
    mutate([&](auto &f){f[stream]=0xF7;},Error::Engine);               // a later engine's event
    mutate([&](auto &f){f[stream]=0x80;},Error::Section);              // a wait of no rows
    mutate([&](auto &f){f[stream]=0xC5;},Error::Section);              // a voice out of range
    mutate([&](auto &f){f[stream]=0xBF;},Error::Section);              // a wait past the rows
    mutate([&](auto &f){f.insert(f.begin()+long(at+4+get16(f.data()+at+2)),uint8_t(0));put16(f.data()+at+2,uint16_t(get16(f.data()+at+2)+1));},Error::Section);
    // A song without a sound to play it on.
    mutate([&](auto &f){f[sectionAt(f,SOUND)]=0x7f;},Error::Section);
  }
  {
    // A song longer than 120 s is refused.
    // At 60 bpm and a row a beat, a row is a second: 150 rows are 150 s.
    std::string rows;for(int k=0;k<149;++k)rows+=". ";
    const auto r=compileResult(effect("voice a = pulse()\nsong(tempo: 60, rows: 1) {\n pattern p {\n  a: \"c4 "+rows+"\"\n }\n order p\n}\n",0));
    assert(!r.ok && r.error.find("at most 120 s")!=std::string::npos);
  }

  // ---- fuzzing songs ----
  {
    // Mutated copies of every example song: refused, or rendered finite and bounded, never out of the file.
    uint64_t seed=0x9e3779b97f4a7c15ull;auto next=[&]{seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return uint32_t(seed>>11);};
    unsigned played=0,refused=0;
    for(const char *path:songs) {
      const auto base=compileText(readFile(path));const size_t song=sectionAt(base,SONG),songEnd=song+4+get16(base.data()+song+2);
      const size_t sound=sectionAt(base,SOUND);
      for(int round=0;round<1500;++round) {
        auto f=base;
        const unsigned edits=1+next()%6;
        for(unsigned k=0;k<edits;++k) {
          // Mostly the song, sometimes the voices.
          const bool inSong=next()%4!=0;
          const size_t lo=inSong?song+4:sound+4+SOUND_HEAD,hi=inSong?songEnd:sound+4+SOUND_HEAD+size_t(base[sound+5])*VOICE_BYTES;
          if(hi>lo)f[lo+next()%(hi-lo)]=uint8_t(next());
        }
        reseal(f);Program p;
        if(load(f.data(),f.size(),p)!=Error::None){++refused;continue;}
        ++played;Sound s;s.begin(p);std::vector<int16_t> out(std::min<uint32_t>(p.soundSamples(),SOUND_RATE*8));
        for(size_t at=0;at<out.size();)at+=s.run(out.data()+at,std::min<size_t>(1000,out.size()-at));
        for(int16_t v:out)assert(std::abs(int(v))<=32767);
      }
    }
    std::printf("fuzzed songs: %u played, %u refused\n",played,refused);
    assert(played>200 && refused>200);
  }

  // ---- the compiler ----
  {
    struct Bad {std::string body;const char *message;};
    const Bad bad[]={
      {"song(tempo: 120) {\n pattern p {\n  a: \"c4\"\n }\n order p\n}\n","declare a song's voices first"},
      {"voice a = pulse()\nsong(tempo: 120) {\n pattern p {\n  a: \"h4\"\n }\n order p\n}\n","unknown token 'h4'"},
      {"voice a = pulse()\nsong(tempo: 120) {\n pattern p {\n  a: \"c4\"\n }\n order q\n}\n","unknown pattern 'q'"},
      {"voice a = pulse()\nsong(tempo: 120) {\n pattern p {\n  a: \"c4\"\n }\n}\n","needs its order"},
      {"voice a = pulse()\nsong(tempo: 9999) {\n pattern p {\n  a: \"c4\"\n }\n order p\n}\n","400-16000 fit"},
      {"voice a = pulse()\nsong(tempo: 120) {\n pattern p {\n  a: \"v17+c4\"\n }\n order p\n}\n","a volume is v1-v16"},
      {"voice a = pulse(octave: 1)\n","a pulse voice has no octave"},
      {"voice a = drum(cowbell)\n","names a drum first"},
      {"voice a = wave(gain: 1)\n","needs its points"},
      {"voice a = wave(points: [0, 2])\n","-1 to 1"},
      {"voice a = noise(hz: 100)\n","stepped noise has"},
      {"voice a = fm(duty: .5)\n","a fm voice has no duty"},
      {"voice a = tone(rate: 2)\n","a tone voice has no rate"},
    };
    for(const Bad &b:bad) {
      const auto r=compileResult(effect(b.body,0));
      if(r.ok || r.error.find(b.message)==std::string::npos)std::fprintf(stderr,"expected '%s', got '%s'\n",b.message,r.error.c_str());
      assert(!r.ok && r.error.find(b.message)!=std::string::npos && r.line>0);
    }
    // A voice named like a volume (v1, v12) is the voice: a declared name wins
    // over the vN volume token, which still works for names nobody declared.
    // The names never reach the file, so both spellings compile the same bytes.
    const char *named="voice v1 = drum(kick)\nvoice v2 = pulse()\nsong(tempo: 120) {\n pattern p {\n  beat: \"v1 . v3+v1 v1+v2:c4\"\n  v2: \"c4 v3+e4 . off\"\n }\n order p\n}\n";
    const char *plain="voice kd = drum(kick)\nvoice pl = pulse()\nsong(tempo: 120) {\n pattern p {\n  beat: \"kd . v3+kd kd+pl:c4\"\n  pl: \"c4 v3+e4 . off\"\n }\n order p\n}\n";
    const auto a=compileResult(effect(named,0)),b=compileResult(effect(plain,0));
    if(!a.ok)std::fprintf(stderr,"voices named like volumes: %s (line %d)\n",a.error.c_str(),a.line);
    assert(a.ok && b.ok && a.bytes==b.bytes);
  }
  std::printf("PASS: level gating (0.19 sources stay level 1), pulse/wave/fm/drum/stepped-noise voices against references, "
              "the drum bank pinned, song rows on their samples with waits, voices, volumes, offs and the order, any slicing, "
              "the loader's refusals, song fuzzing and the compiler's messages\n");
}
