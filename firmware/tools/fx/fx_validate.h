#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
// The engine counts each primitive's calls and steps for the cost model (host builds only).
#ifndef LLFX_COUNTS
#define LLFX_COUNTS
#endif
#include "fx_effect.h"
#include "palette.h"

// The validator the web editor and the tools run before a file may go to a
// device (docs/FIRMWARE_LOADABLE_EFFECTS.md, "Performance guarantee"). It
// loads the file as the device does, then plays every 10 ms of it, and 1 s
// past its end, on each scene and accent, and refuses it when a frame:
//  - differs between two draws (not a pure function of time),
//  - changes UI ink below the header and readout rows, or anywhere from the readout end,
//  - lights a background pixel inside the UI halo (outside the readout rows
//    of a file that moves the readout, before its readout end),
//  - leaves the header less than 90% as readable at 3:1 (check_ui_render's rule),
//  - is not the clean recap from the duration on,
//  - or costs more than the budget by the device cost model;
// and when its code, arguments and constants exceed the 4 KB the device
// copies into internal RAM to play it.
namespace FxValidate {
using LoadableFx::Program;

struct Scene {std::vector<uint16_t> pixels;int width=135,height=240;uint16_t background=0;const char *name="fixture";};
inline uint16_t swap16(uint16_t v){return uint16_t(v<<8|v>>8);}

// The host suite's recap stand-in: a label, a dense readout (rows 17-43),
// replay lines, a box and a footer, as in test_classic_effects.
inline Scene fixture(uint16_t background=0) {
  Scene s;s.background=background;s.pixels.assign(135*240,swap16(background));
  auto ui=[&](int x,int y,uint16_t c){s.pixels[size_t(y*135+x)]=swap16(c);};
  for(int y=2;y<10;++y)for(int x=44;x<91;++x)if((x+y)%3)ui(x,y,0x07ff);
  for(int y=17;y<44;++y)for(int x=10;x<125;++x)if((x+y)%5)ui(x,y,0x07ff);
  for(int i=0;i<90;++i){ui(20+i,60+i/3,0x18c3);ui(110-i,60+i/3,0x18c3);}
  for(int y=110;y<150;++y)for(int x=50;x<85;++x)ui(x,y,0x1947);
  for(int y=164;y<172;++y)for(int x=40;x<95;++x)if((x*y)%4)ui(x,y,0x7bef);
  for(int x=26;x<109;++x)ui(x,178,0x39e7);
  return s;
}

// Device cost of a frame's effect pass at 80 MHz. The pixel work by the
// built-in effects' calibrated model (Renderer::modelledUs(): pixels written,
// light probes and mask words scanned), and the rest by what the engine did:
// 0.46 us an instruction, and per primitive a cost a call and a cost a step
// of its own loop (a point, column, particle or pixel visited). Fitted on the
// M5 at 80 MHz to 1,888 unguarded frames of 19 files (HIL spike run 5,
// 2026-10-21), each coefficient pulled toward 10 us a call and 0.36 us a step
// in proportion to how rarely it was seen: off by 0.5 ms rms a frame, 1 ms
// at the 95th percentile (design note, "Device frame times"). A primitive
// no file used then costs 10 us a call.
// The renderer's pixel terms (pixels written, light probes, mask words
// scanned, span fills) as Renderer::modelledUs() weighed them when the costs
// below were fitted (run 5: the renderer at -Os, 2026-10-21). Kept until the
// files are re-measured with the renderer at -O2, which only makes them
// cheaper on the device, so the budget check stays on the safe side.
inline double pixelUs(uint32_t plots,uint32_t probes,uint32_t scans,uint32_t fills,uint32_t spans) {
  return 1100+1.8*plots+probes+13.0*scans+.082*fills+1.21*spans;
}
struct Cost {
  double opUs=.46;
  // In LoadableFx::Prim order: clip plot dot vdot spark twinkle star line fline segment ellipse glow bolt rimbolt fire prime
  // glint shiftrow shade cut heatline blade emit stroke trail stamp aura disc runes puff cloud arc heatring vessel rays.
  double callUs[LoadableFx::PRIMS]={10.5,9,8.9,10,33.2,15.5,0,11.2,0,10,12.3,10,10,9.8,10,12.6,2.7,2.6,11.3,10.3,10.8,10,59.1,9,0.5,10.2,10,10,8,10.3,9.9,8.1,10,10,10};
  double stepUs[LoadableFx::PRIMS]={0,0,0,0,0,0,0,0,0,0,2.09,2.09,0,0,1.61,0,0,0,0,0,0,18.35,3.24,0.42,1.58,0.84,0.44,1.91,0,0,0.98,0,3.78,1.04,0};
  double us(const LoadableFx::Engine &engine)const{
    const LoadableFx::Engine::Stats &s=engine.stats();
    double us=pixelUs(s.plots,s.probes,s.scans,s.fills,s.spans)+opUs*s.ops;
    for(unsigned k=0;k<LoadableFx::PRIMS;++k)us+=callUs[k]*engine.primCalls[k]+stepUs[k]*engine.primTouches[k];
    return us;
  }
};

constexpr size_t RAM_COPY=LoadableFx::HOT_BYTES;
// The frame budget: the device's guard cuts an effect pass short at 9 ms.
// On the recap run 5 measured (15,500 RPM, NEW BEST, default theme; drawn by
// tools/ui_render/fx_preview.h), the model's slowest frame against this
// budget sorts the twelve idiomatic files as the device did: the six it
// played within 9 ms pass, the six over it fail.
constexpr double BUDGET_US=9000;

struct Report {
  bool ok=false;std::string problem;LoadableFx::Error error=LoadableFx::Error::None;
  double worstUs=0;unsigned worstMs=0;uint32_t worstOps=0,maxOps=0,maxDraws=0;unsigned frames=0;
  bool movesReadout=false;
  uint32_t songLoad=0,songWorstMs=0;  // a song's busiest block, hundredths of a percent of core 0 (songLoad())
};

// A song's busiest block by the render cost model (LoadableFx::SONG_KIND_COST):
// the music task's share of core 0 there, in hundredths of a percent, and
// where it falls. Renders the whole sound; 0 for a file without a song.
struct SongLoad {uint32_t worst=0,worstMs=0;};
inline SongLoad songLoad(const Program &p) {
  using namespace LoadableFx;
  SongLoad r;
  if(!p.hasSong)return r;
  static Sound s;s.begin(p);
  int16_t block[SONG_COST_BLOCK];Sound::Work before=s.work();uint32_t at=0;
  while(!s.done()) {
    const size_t n=s.run(block,SONG_COST_BLOCK);
    if(!n)break;
    const Sound::Work &w=s.work();uint64_t cost=uint64_t(SONG_BASE_COST)*n+uint64_t(SONG_FILTER_COST)*(w.filterSamples-before.filterSamples);
    for(unsigned k=0;k<SONG_KINDS;++k)cost+=uint64_t(SONG_KIND_COST[k])*(w.kindSamples[k]-before.kindSamples[k]);
    const uint32_t share=uint32_t(cost/n);
    if(share>r.worst){r.worst=share;r.worstMs=uint32_t(uint64_t(at)*1000/SOUND_RATE);}
    before=w;at+=uint32_t(n);
  }
  return r;
}
inline std::string percent(uint32_t hundredths){return std::to_string(hundredths/100)+"."+std::to_string(hundredths%100/10)+"%";}

// Whether the file draws a primitive that moves readout pixels.
inline bool movesReadout(const Program &p) {
  for(unsigned i=0;i<p.codeLength;++i) {
    const uint8_t *in=p.code+4*i;
    if(in[0]==LoadableFx::DRAW && (in[1]==LoadableFx::SHIFTROW || in[1]==LoadableFx::CUT))return true;
  }
  return false;
}

inline Report check(const std::vector<uint8_t> &file,const std::vector<Scene> &scenes,const Cost &cost=Cost(),double budgetUs=BUDGET_US) {
  Report r;Program p;
  r.error=LoadableFx::load(file.data(),file.size(),p);
  if(r.error!=LoadableFx::Error::None){r.problem=std::string("refused: ")+LoadableFx::label(r.error);return r;}
  r.maxOps=p.maxOps;r.maxDraws=p.maxDraws;r.movesReadout=movesReadout(p);
  // What the device copies into internal RAM to play the file: its code,
  // primitive arguments and constant pool (docs: "Internal RAM budget").
  const size_t playing=LoadableFx::hotBytes(p);
  if(playing>RAM_COPY){r.problem="too large for internal RAM: "+std::to_string(playing)+" bytes of code, arguments and constants (at most "+std::to_string(RAM_COPY)+")";return r;}
  // A song plays on core 0 beside the optical owner and the IMU: its busiest
  // block must fit the render budget (docs/FIRMWARE_SOUND_0_20.md, "Render budget").
  // A song plays as long as a celebration at most (0.20.1); the device
  // still plays an older, longer one's first SONG_PLAY_MS, faded.
  if(p.songCut()) {
    char seconds[16];std::snprintf(seconds,sizeof(seconds),"%.1f",double(p.songTotal)/LoadableFx::SOUND_RATE);
    r.problem="song too long: "+std::string(seconds)+" s (at most "+std::to_string(LoadableFx::SONG_PLAY_MS/1000)+" s, as long as a celebration)";return r;
  }
  const SongLoad load=songLoad(p);r.songLoad=load.worst;r.songWorstMs=load.worstMs;
  if(load.worst>LoadableFx::SONG_BUDGET){
    r.problem="song too heavy: "+percent(load.worst)+" of core 0 at "+std::to_string(load.worstMs)+" ms (at most "+percent(LoadableFx::SONG_BUDGET)+")";return r;
  }
  BestEffectUI::Renderer renderer;LoadableFx::Engine engine(renderer);
  const uint16_t accents[]={0x07ff,0xc5bf,0xfa8a,0xffff};
  constexpr int ANCHOR_BOTTOM=BestEffectUI::Renderer::NUMBER_BOTTOM,HEADER=ANCHOR_BOTTOM+1,NEAR=8;
  auto fail=[&](const std::string &why,const Scene &s,unsigned ms){r.problem=why+" ("+s.name+", "+std::to_string(ms)+" ms)";return r;};
  for(const Scene &s:scenes) {
    const size_t n=s.pixels.size();
    std::vector<uint8_t> halo(n,0);
    for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x)if(s.pixels[size_t(y*s.width+x)]!=swap16(s.background))
      for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)if(x+dx>=0 && x+dx<s.width && y+dy>=0 && y+dy<s.height)halo[size_t((y+dy)*s.width+x+dx)]=1;
    unsigned readableBase=0;
    for(size_t i=0;i<size_t(HEADER*s.width);++i)readableBase+=Palette::contrast100(swap16(s.pixels[i]),s.background)>=300;
    for(uint16_t accent:accents)for(unsigned ms=0;ms<=p.duration+1000u;ms+=10) {
      std::vector<uint16_t> out=s.pixels,again=s.pixels;
      engine.draw({out.data(),s.width,s.height},p,ms,accent,BestEffectUI::Renderer::Anchor(),s.background);
      const LoadableFx::Engine::Stats stats=engine.stats();const double us=cost.us(engine);
      engine.draw({again.data(),s.width,s.height},p,ms,accent,BestEffectUI::Renderer::Anchor(),s.background);
      ++r.frames;
      if(out!=again)return fail("not a pure function of time",s,ms);
      if(ms>=p.duration && out!=s.pixels)return fail("not the clean recap after the end",s,ms);
      if(us>r.worstUs){r.worstUs=us;r.worstMs=ms;}
      r.worstOps=std::max(r.worstOps,stats.ops);
      if(us>budgetUs)return fail("over the frame budget: "+std::to_string(int(us))+" us modelled",s,ms);
      unsigned readable=0;
      for(size_t i=0;i<n;++i) {
        const int row=int(i)/s.width;
        if(row<HEADER)readable+=Palette::contrast100(swap16(out[i]),s.background)>=300;
        if(out[i]==s.pixels[i])continue;
        // The header (label and readout, which the glint reaches) and the rows the readout moves into.
        const bool nearReadout=row<=ANCHOR_BOTTOM+NEAR && ms<p.readoutEnd;
        if(s.pixels[i]!=swap16(s.background)) {
          if(!nearReadout)return fail("changes UI ink away from the readout or after the readout end",s,ms);
        } else if(halo[i] && !(nearReadout && r.movesReadout))return fail("lights the UI halo",s,ms);
      }
      if(readable*10<readableBase*9)return fail("hides the readout",s,ms);
    }
  }
  r.ok=true;return r;
}
}
