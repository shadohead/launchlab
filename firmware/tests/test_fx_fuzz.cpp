// Malformed and hostile loaded-effect files (fx_effect.h) are refused, or
// drawn safely: under ASan/UBSan, tens of thousands of mutated copies of the
// compiled effects, and random programs built to pass the loader, never read
// outside the file or the canvas, never run more instructions than the
// file's static bound, stay pure functions of time, and never change UI ink
// below the readout's rows or light the UI halo there, nor touch any UI ink
// from the readout end on. Deterministic seeds, so a failure reproduces.
#include "../tools/fx/fx_compiler.h"
#include "../tools/fx/fx_validate.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>

using namespace LoadableFx;

namespace {
struct Rng {
  uint64_t s;
  uint32_t next(){s^=s<<13;s^=s>>7;s^=s<<17;return uint32_t(s>>11);}
  uint32_t below(uint32_t n){return n?next()%n:0;}
  float real(){
    static const float edges[]={0,-0.0f,1,-1,.5f,1e-30f,-1e30f,1e30f,3.4e38f,-3.4e38f,65536,-65536,255,135,240,1e6f};
    if(below(3)==0)return edges[below(sizeof(edges)/sizeof(*edges))];
    return (float(next())/4294967296.0f-.5f)*float(1<<below(24));
  }
};

std::vector<uint8_t> compileFile(const char *name) {
  std::ifstream in(std::string("firmware/tools/fx/effects/")+name+".fx");std::stringstream text;text<<in.rdbuf();
  const auto r=FxCompiler::compile(text.str());
  if(!r.ok)std::fprintf(stderr,"%s:%d: %s\n",name,r.line,r.error.c_str());
  assert(r.ok);
  return r.bytes;
}
void reseal(std::vector<uint8_t> &f) {
  if(f.size()<HEADER_BYTES)return;
  put32(f.data()+24,uint32_t(f.size()));put32(f.data()+28,0);put32(f.data()+28,crc32(f.data(),f.size()));
}

struct Checker {
  FxValidate::Scene scene=FxValidate::fixture();
  std::vector<uint8_t> halo;
  BestEffectUI::Renderer renderer;Engine engine{renderer};
  unsigned draws=0,sounds=0;uint64_t soundSamples=0;
  Sound sound;
  Checker() {
    halo.assign(scene.pixels.size(),0);
    for(int y=0;y<240;++y)for(int x=0;x<135;++x)if(scene.pixels[size_t(y*135+x)])
      for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)if(x+dx>=0 && x+dx<135 && y+dy>=0 && y+dy<240)halo[size_t((y+dy)*135+x+dx)]=1;
  }
  // Loads the bytes from an allocation of exactly their size, so ASan sees
  // any read past the end, and draws an accepted file at a few times.
  Error run(const std::vector<uint8_t> &bytes,Rng &rng) {
    std::unique_ptr<uint8_t[]> exact(new uint8_t[bytes.size()?bytes.size():1]);
    if(!bytes.empty())std::memcpy(exact.get(),bytes.data(),bytes.size());
    Program p;const Error e=load(exact.get(),bytes.size(),p);
    if(e!=Error::None)return e;
    assert(p.maxOps<=MAX_FRAME_OPS && p.maxDraws<=MAX_FRAME_DRAWS);
    for(int k=0;k<2;++k) {
      const uint32_t ms=k==0?rng.below(p.duration):p.readoutEnd+rng.below(p.duration-p.readoutEnd+1);
      std::vector<uint16_t> out=scene.pixels,again=scene.pixels;
      engine.draw({out.data(),135,240},p,ms,uint16_t(rng.next()));
      const Engine::Stats s=engine.stats();
      engine.draw({again.data(),135,240},p,ms,0);  // a different accent
      assert(s.ops<=p.maxOps && s.draws<=p.maxDraws);
      std::vector<uint16_t> same=scene.pixels;const uint16_t accent=uint16_t(rng.next());
      out=scene.pixels;engine.draw({out.data(),135,240},p,ms,accent);
      engine.draw({same.data(),135,240},p,ms,accent);
      assert(out==same);
      for(size_t i=0;i<out.size();++i) {
        if(out[i]==scene.pixels[i])continue;
        const int row=int(i/135);
        const bool header=row<=BestEffectUI::Renderer::NUMBER_BOTTOM+8 && ms<p.readoutEnd;
        if(scene.pixels[i])assert(header);
        else assert(!halo[i] || header);
      }
      ++draws;
    }
    // The start of its sound, if any (all of it now and then), in two
    // slicings: the same samples, within the limiter's bound, and never
    // more instructions than one control run a step and one ahead.
    if(p.hasSound) {
      ++sounds;
      const size_t n=std::min<size_t>(rng.below(8)==0?p.soundSamples():2048,p.soundSamples());
      std::vector<int16_t> a(n),b(n);
      sound.begin(p);assert(sound.run(a.data(),n)==n);
      const uint32_t steps=uint32_t((n+SOUND_STEP-1)/SOUND_STEP);
      assert(sound.work().steps==steps && sound.ops()<=(steps+1)*p.soundLength);
      sound.begin(p);for(size_t at=0;at<n;)at+=sound.run(b.data()+at,std::min<size_t>(n-at,1+rng.below(97)));
      assert(a==b);
      for(int16_t v:a)assert(v>=-31130 && v<=31130);
      soundSamples+=n;
    }
    return e;
  }
};

// A file built to pass the loader: random instructions over valid registers,
// random primitives with random arguments, blocks within the frame caps.
std::vector<uint8_t> randomProgram(Rng &rng) {
  std::vector<uint8_t> f(HEADER_BYTES,0);
  put32(f.data(),MAGIC);f[4]=FORMAT;f[5]=ENGINE;
  const uint16_t duration=uint16_t(1000+rng.below(5001));
  put16(f.data()+18,duration);put16(f.data()+20,uint16_t(rng.below(std::min<uint32_t>(2201,duration*3/4+1))));
  auto section=[&](uint8_t type,const std::vector<uint8_t> &body){f.push_back(type);f.push_back(0);f.push_back(uint8_t(body.size()));f.push_back(uint8_t(body.size()>>8));f.insert(f.end(),body.begin(),body.end());};
  section(NAME,{'F','u','z','z'});
  const unsigned consts=1+rng.below(MAX_CONSTS+40);
  std::vector<uint8_t> c(consts*4);
  for(unsigned i=0;i<consts;++i){const float v=rng.real();uint32_t bits;std::memcpy(&bits,&v,4);put32(c.data()+4*i,bits);}
  section(CONSTS,c);
  const unsigned regConsts=std::min(consts,MAX_CONSTS);
  auto readable=[&]()->uint8_t{const unsigned pick=rng.below(3);
    if(pick==0)return uint8_t(rng.below(BUILTINS));
    if(pick==1)return uint8_t(FIRST_TEMP+rng.below(FIRST_CONST-FIRST_TEMP));
    return uint8_t(FIRST_CONST+rng.below(regConsts));};
  std::vector<uint8_t> args;
  for(unsigned i=0;i<600;++i)args.push_back(readable());
  section(ARGS,args);
  std::vector<uint8_t> code;std::vector<uint8_t> blocks;uint32_t ops=0,draws=0;
  const unsigned nblocks=1+rng.below(6);
  for(unsigned b=0;b<nblocks;++b) {
    const unsigned length=1+rng.below(60),count=1+rng.below(rng.below(2)?8:200);
    if(ops+length*count>MAX_FRAME_OPS)break;
    const size_t start=code.size()/4;unsigned blockDraws=0;
    for(unsigned k=0;k<length;++k) {
      uint8_t op=uint8_t(rng.below(OPS));
      if(op==LDX)op=MUL;  // these programs have no tables
      if(op==DRAW && blockDraws*count+count+draws>MAX_FRAME_DRAWS)op=ADD;
      uint8_t d=uint8_t(FIRST_TEMP+rng.below(FIRST_CONST-FIRST_TEMP)),a=readable(),bb=readable();
      if(op==DRAW){d=uint8_t(rng.below(PRIMS));const unsigned at=rng.below(600-16);a=uint8_t(at);bb=uint8_t(at>>8);++blockDraws;}
      if(op==LDK){const unsigned at=rng.below(consts);a=uint8_t(at);bb=uint8_t(at>>8);}
      if(op==CMIX || op==CSEL){d=uint8_t(FIRST_TEMP+rng.below(FIRST_CONST-FIRST_TEMP-2));a=uint8_t(FIRST_TEMP+rng.below(100));bb=uint8_t(FIRST_TEMP+rng.below(100));}
      code.insert(code.end(),{op,d,a,bb});
    }
    ops+=length*count;draws+=blockDraws*count;
    const uint16_t flags=rng.below(4)==0?uint16_t(GUARDED|(FIRST_TEMP+rng.below(40))):0;
    std::vector<uint8_t> e(8);put16(e.data(),uint16_t(start));put16(e.data()+2,uint16_t(length));put16(e.data()+4,uint16_t(count));put16(e.data()+6,flags);
    blocks.insert(blocks.end(),e.begin(),e.end());
  }
  if(code.empty())code={NOP,0,0,0};
  if(blocks.empty()){blocks.assign(8,0);put16(blocks.data()+2,1);put16(blocks.data()+4,1);}
  // Half of them sound: a control block after the blocks' code, voices
  // of any kind this engine knows reading any registers, notes in order.
  if(rng.below(2)) {
    const uint16_t soundMs=uint16_t(1+rng.below(std::min<uint32_t>(MAX_SOUND_MS,duration)));put16(f.data()+22,soundMs);
    const uint32_t samples=uint32_t(soundMs)*SOUND_RATE/1000;
    const unsigned length=rng.below(MAX_SOUND_CODE+1),voices=1+rng.below(MAX_VOICES),events=rng.below(rng.below(2)?8:MAX_SOUND_EVENTS+1);
    const size_t start=code.size()/4;
    for(unsigned k=0;k<length;++k) {
      uint8_t op=uint8_t(rng.below(OPS));
      if(op==LDX || op==DRAW)op=SUB;
      uint8_t d=uint8_t(FIRST_TEMP+rng.below(FIRST_CONST-FIRST_TEMP)),a=readable(),bb=readable();
      if(op==LDK){const unsigned at=rng.below(consts);a=uint8_t(at);bb=uint8_t(at>>8);}
      if(op==CMIX || op==CSEL){d=uint8_t(FIRST_TEMP+rng.below(FIRST_CONST-FIRST_TEMP-2));a=uint8_t(FIRST_TEMP+rng.below(100));bb=uint8_t(FIRST_TEMP+rng.below(100));}
      code.insert(code.end(),{op,d,a,bb});
    }
    std::vector<uint8_t> body(SOUND_HEAD,0);
    body[0]=SOUND_VERSION;body[1]=uint8_t(voices);body[2]=SOUND_STEP_SHIFT;body[3]=1;
    put16(body.data()+4,uint16_t(length?start:0));put16(body.data()+6,uint16_t(length));put16(body.data()+8,uint16_t(events));
    for(unsigned v=0;v<voices;++v) {
      uint8_t r[VOICE_BYTES]={};const bool noise=rng.below(3)==0;
      r[0]=noise?NOISE_VOICE:TONE_VOICE;r[1]=noise?0:uint8_t(rng.below(WAVES));r[2]=uint8_t(rng.below(FILTERS));
      r[3]=uint8_t((rng.below(2)?ENVELOPE:0)|(!noise && rng.below(2)?NOTE_PITCH:0));
      if(noise)put32(r+4,1+rng.next()%0xfffffffeu);
      for(unsigned k=0;k<VOICE_PARAMS;++k)r[8+k]=readable();
      body.insert(body.end(),r,r+VOICE_BYTES);
    }
    std::vector<uint32_t> at(events);for(auto &x:at)x=rng.below(samples);std::sort(at.begin(),at.end());
    for(unsigned e=0;e<events;++e) {
      uint8_t r[EVENT_BYTES]={};put32(r,at[e]);r[4]=uint8_t(rng.below(voices));r[5]=NOTE;
      float level=rng.real(),hz=rng.real();if(!std::isfinite(level))level=1;if(!std::isfinite(hz))hz=0;
      uint32_t bits;std::memcpy(&bits,&level,4);put32(r+8,bits);std::memcpy(&bits,&hz,4);put32(r+12,bits);
      body.insert(body.end(),r,r+EVENT_BYTES);
    }
    if(code.size()/4>MAX_CODE)return randomProgram(rng);
    section(CODE,code);section(BLOCKS,blocks);section(SOUND,body);
    reseal(f);
    return f;
  }
  section(CODE,code);section(BLOCKS,blocks);
  reseal(f);
  return f;
}
}

int main() {
  const char *names[]={"orbit","flames","sparkles","shockwave","slash","rupture","worst","opbench",
                       "idiom/orbit","idiom/sparkles","idiom/slash","idiom/rupture","idiom/tidal","idiom/crimson","idiom/bladework",
                       "idiom/overdrive","idiom/summon","idiom/boundless017","idiom/sunblade","idiom/waterleaf"};
  std::vector<std::vector<uint8_t>> corpus;
  for(const char *n:names)corpus.push_back(compileFile(n));
  Checker checker;Rng rng{0x9e3779b97f4a7c15ull};
  std::map<Error,unsigned> outcomes;
  auto record=[&](Error e){++outcomes[e];};

  // Mutations of real files; most are resealed so they reach the structural checks.
  for(unsigned iter=0;iter<30000;++iter) {
    std::vector<uint8_t> f=corpus[rng.below(unsigned(corpus.size()))];
    const unsigned kind=rng.below(9);
    switch(kind) {
      case 0:for(unsigned k=0,n=1+rng.below(8);k<n;++k)f[rng.below(unsigned(f.size()))]^=uint8_t(1u<<rng.below(8));break;
      case 1:{static const uint8_t v[]={0,1,0x7f,0x80,0xff,0xfe,32,160,255};f[rng.below(unsigned(f.size()))]=v[rng.below(9)];break;}
      case 2:f.resize(rng.below(unsigned(f.size())));break;
      case 3:for(unsigned k=0,n=1+rng.below(64);k<n;++k)f.push_back(uint8_t(rng.next()));break;
      case 4:{const size_t at=rng.below(unsigned(f.size())),n=std::min<size_t>(f.size()-at,1+rng.below(32));f.insert(f.begin()+long(at),f.begin()+long(at),f.begin()+long(at+n));break;}
      case 5:{const auto &g=corpus[rng.below(unsigned(corpus.size()))];const size_t cut=rng.below(unsigned(std::min(f.size(),g.size())));
              f.resize(cut);f.insert(f.end(),g.begin()+long(cut),g.end());break;}
      case 6:{// one instruction byte: op, register or argument offset
              const size_t at=HEADER_BYTES+rng.below(unsigned(f.size()-HEADER_BYTES));f[at]=uint8_t(rng.next());break;}
      case 7:{// a header field
              const size_t at=4+rng.below(20);f[at]=uint8_t(rng.next());break;}
      default:{// a section length
              size_t at=HEADER_BYTES;unsigned skip=rng.below(5);
              while(skip-- && at+4<=f.size())at+=4+get16(f.data()+at+2);
              if(at+4<=f.size())put16(f.data()+at+2,uint16_t(rng.next()));break;}
    }
    if(rng.below(10)<7)reseal(f);
    record(checker.run(f,rng));
  }
  // Programs built to pass the loader.
  unsigned built=0,accepted=0;
  for(unsigned iter=0;iter<3000;++iter) {
    const auto f=randomProgram(rng);++built;
    const Error e=checker.run(f,rng);record(e);accepted+=e==Error::None;
  }
  std::printf("test_fx_fuzz: %u files drawn %u times, %u sounds rendered (%llu samples); outcomes:",unsigned(30000+built),checker.draws,
              checker.sounds,(unsigned long long)checker.soundSamples);
  for(const auto &o:outcomes)std::printf(" %s=%u",label(o.first),o.second);
  std::printf("; %u of %u generated programs loaded\n",accepted,built);
  assert(outcomes[Error::None]>1000 && accepted>built/2 && checker.sounds>2000);
  assert(outcomes[Error::Checksum]>1000 && outcomes[Error::Register]>100 && outcomes[Error::Size]>100);
  std::puts("test_fx_fuzz: ok");
}
