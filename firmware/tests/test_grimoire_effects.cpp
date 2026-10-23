// The grimoire set of 0.18 (Volley..Meadow): the shared renderer contract,
// the device cost model, and one identity property per effect. Same fixture
// as test_sorcery_effects and test_appearance.
#include "best_effect_ui.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

using Frame=std::array<uint16_t,135*240>;
static Frame base_{};
static uint16_t swap16(uint16_t v){return uint16_t(v<<8|v>>8);}
static int red(uint16_t stored){return swap16(stored)>>11;}
static int green(uint16_t stored){return (swap16(stored)>>5)&63;}
static int blue(uint16_t stored){return swap16(stored)&31;}

int main(){
  using namespace Appearance;
  Frame &base=base_;
  auto ui=[&](int x,int y,uint16_t c){base[y*135+x]=swap16(c);};
  // Label, RPM glyphs with holes, replay wireframe and faces, lower labels.
  for(int y=2;y<10;++y)for(int x=44;x<91;++x)if((x+y)%3)ui(x,y,0x07ff);
  for(int y=17;y<44;++y)for(int x=10;x<125;++x)if((x+y)%5)ui(x,y,0x07ff);
  for(int i=0;i<90;++i){ui(20+i,60+i/3,0x18c3);ui(110-i,60+i/3,0x18c3);}
  for(int y=110;y<150;++y)for(int x=50;x<85;++x)ui(x,y,0x1947);
  for(int y=164;y<172;++y)for(int x=40;x<95;++x)if((x*y)%4)ui(x,y,0x7bef);
  for(int x=26;x<109;++x)ui(x,178,0x39e7);
  std::array<bool,135*240> halo{};
  for(int y=0;y<240;++y)for(int x=0;x<135;++x)if(base[y*135+x])
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)if(x+dx>=0 && x+dx<135 && y+dy>=0 && y+dy<240)halo[(y+dy)*135+x+dx]=true;

  BestEffectUI::Renderer renderer;
  const Effect effects[]={Effect::Volley,Effect::Starfall,Effect::Mimic,Effect::Scales,Effect::Meadow};
  // How each may touch the UI's own pixels: `moves` shifts or recolours
  // readout rows freely (exact again by 3/4), `dims` may darken glyphs (never
  // below 3:1); the others only brighten glyph pixels.
  const bool moves[]={false,false,false,false,false},dims[]={false,false,false,false,false};
  auto frame=[&](Effect e,unsigned ms,uint16_t accentColor){
    Frame out=base;renderer.draw({out.data(),135,240},e,ms,accentColor);return out;
  };
  struct Stats{unsigned n=0;double cx=0,cy=0;int y0=240,y1=-1,x0=135,x1=-1;};
  auto bgStats=[&](const Frame &out){
    Stats s;double sx=0,sy=0;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x){
      const int i=y*135+x;if(base[i] || out[i]==base[i])continue;
      ++s.n;sx+=x;sy+=y;s.y0=std::min(s.y0,y);s.y1=std::max(s.y1,y);s.x0=std::min(s.x0,x);s.x1=std::max(s.x1,x);
    }
    if(s.n){s.cx=sx/s.n;s.cy=sy/s.n;}
    return s;
  };

  // The device cost model (fitted on the M5 at 80 MHz, 2026-10-20):
  // The device cost model (Renderer::modelledUs(), fitted on the M5 at
  // 80 MHz): every frame at most 9 ms (the device budget).
  // Each runs about five seconds (owner, 2026-10-20): an early impact, a
  // sustained middle that keeps moving but costs less than the impact, and
  // a settle. Windows in ms: impact from/to, sustain from/to.
  const unsigned windows[5][4]={{500,1800,1900,3700},{800,1800,1900,3800},{500,1800,1900,3700},{500,1700,1800,3800},{300,1700,1800,3800}};
  const bool report=std::getenv("GRIMOIRE_COSTS")!=nullptr;
  for(unsigned e=0;e<5;++e){
    const Effect effect=effects[e];double worst=0,impact=0,sustain=0;unsigned at=0,wp=0,wq=0,ws=0;
    for(unsigned ms=0;ms<duration(effect);ms+=10){
      frame(effect,ms,accent(Theme::Classic));
      const double us=renderer.modelledUs();
      if(us>worst){worst=us;at=ms;wp=renderer.plots();wq=renderer.probes();ws=renderer.scans();}
      if(ms>=windows[e][0] && ms<windows[e][1])impact=std::max(impact,us);
      if(ms>=windows[e][2] && ms<windows[e][3])sustain=std::max(sustain,us);
    }
    if(report)std::printf("%s: worst %.0f us at %u ms (plots %u probes %u scans %u); impact %.0f, sustain %.0f\n",label(effect),worst,at,wp,wq,ws,impact,sustain);
    assert(worst<=9000 && sustain<impact);
    // The sustain is never frozen: every frame differs from the one before.
    for(unsigned ms=windows[e][2];ms<windows[e][3];ms+=50){
      const Frame a=frame(effect,ms,accent(Theme::Classic)),b=frame(effect,ms+50,accent(Theme::Classic));
      unsigned moved=0;for(int i=0;i<135*240;++i)moved+=!base[i] && a[i]!=b[i];
      assert(moved>=8);
    }
  }

  for(unsigned e=0;e<5;++e){
    const Effect effect=effects[e];
    const uint32_t dur=duration(effect);
    assert(dur>=4800 && dur<=5200); // owner, 2026-10-20: about five seconds
    for(unsigned theme:{0u,3u,5u,8u}){
      const uint16_t color=accent(Theme(theme));
      bool drew=false,touched=false,lower=false;
      for(unsigned ms=0;ms<=dur+200;ms+=10){
        Frame out=frame(effect,ms,color),again=frame(effect,ms,color);
        assert(out==again); // a pure function of elapsed time
        // check_ui_render's legibility rule: the header keeps at least 90%
        // of the pixels it had at 3:1 on the background.
        unsigned readable=0,readableBase=0;
        for(int i=0;i<47*135;++i){readableBase+=Palette::contrast100(swap16(base[i]),0)>=300;readable+=Palette::contrast100(swap16(out[i]),0)>=300;}
        assert(readable*10>=readableBase*9);
        // A recoloured readout pixel never drops below 3:1 (or its own contrast).
        if(!moves[e])for(int i=0;i<47*135;++i)if(base[i] && out[i]!=base[i])
          assert(Palette::contrast100(swap16(out[i]),0)>=std::min(Palette::contrast100(swap16(base[i]),0),300u));
        const bool settled=ms*4>=dur*3;
        for(int i=0;i<135*240;++i)if(out[i]!=base[i]){
          const int y=i/135;drew=true;lower|=y>80;
          // Moved readout rows: only the readout's rows and the halo round them.
          if(moves[e] && y>=13-6 && y<=46+6){assert(!settled || !base[i]);touched|=base[i]!=0;continue;}
          if(base[i]){
            // A changed UI pixel: header rows only, before 3/4 of the
            // duration, and brighter in every channel unless the effect dims.
            assert(y<=46 && !settled);touched=true;
            if(!dims[e])assert(red(out[i])>=red(base[i]) && green(out[i])>=green(base[i]) && blue(out[i])>=blue(base[i]));
            assert(out[i]!=0); // a recoloured glyph pixel never becomes background
          }else assert(!halo[i]); // decoration keeps the UI halo
        }
        if(ms>=dur)assert(out==base); // nothing after the duration
      }
      assert(drew && touched && lower);
      // The readout exactly as drawn at the end.
      for(unsigned ms=dur*3/4+10;ms<dur;ms+=10){Frame out=frame(effect,ms,color);for(int i=0;i<135*240;++i)if(base[i])assert(out[i]==base[i]);}
    }
    // A custom theme's background: the same pixels change as on black.
    const uint16_t bg=0x0841;Frame themed=base;
    for(auto &p:themed)if(!p)p=swap16(bg);
    for(unsigned ms=0;ms<dur;ms+=50){
      Frame black=frame(effect,ms,accent(Theme::Mint));
      Frame out=themed;renderer.draw({out.data(),135,240},effect,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
    // A light custom theme (dark navy ink on white, like Paper): light on it
    // only washes out, so whatever the effect does to the readout must keep
    // the header legible by the same rule, and the UI below it untouched.
    {
      const uint16_t white=0xffff,navy=0x0010;Frame light=base;
      for(auto &p:light)p=swap16(p?navy:white);
      unsigned readableBase=0;for(int i=0;i<47*135;++i)readableBase+=Palette::contrast100(swap16(light[i]),white)>=300;
      for(unsigned ms=0;ms<dur;ms+=10){
        Frame out=light;renderer.draw({out.data(),135,240},effect,ms,accent(Theme::Classic),BestEffectUI::Renderer::Anchor(),white);
        unsigned readable=0;for(int i=0;i<47*135;++i)readable+=Palette::contrast100(swap16(out[i]),white)>=300;
        assert(readable*10>=readableBase*9);
        for(int i=47*135;i<135*240;++i)if(light[i]!=swap16(white))assert(out[i]==light[i]);
      }
    }
  }

  const uint16_t cyan=accent(Theme::Classic);(void)cyan;(void)bgStats;
  // Identity: one property per effect that tells it apart from every other.
  // Volley: before any shot only the four circles show, each a compact
  // ring round its own centre (two in the corners under the readout, two
  // below the replay's lower corners); a beam then spans the screen's width;
  // the final volley's upper beams cross in a star under the readout.
  {
    const Frame open=frame(Effect::Volley,450,cyan);
    const int cx[4]={10+23,124-23,10+11,124-12},cy[4]={43+12,43+12,43+110,43+110};
    unsigned inside[4]={},outside=0;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || open[i]==base[i])continue;
      bool in=false;for(int c=0;c<4;++c)if(std::abs(x-cx[c])<=11 && std::abs(y-cy[c])<=11){++inside[c];in=true;break;}
      outside+=!in;}
    if(report)std::printf("volley circles %u %u %u %u outside %u\n",inside[0],inside[1],inside[2],inside[3],outside);
    assert(outside==0 && std::min({inside[0],inside[1],inside[2],inside[3]})>30);
    const Frame shot=frame(Effect::Volley,640,cyan);
    unsigned columns=0;
    for(int x=0;x<135;++x){bool lit=false;for(int y=44;y<240 && !lit;++y){const int i=y*135+x;lit=!base[i] && shot[i]!=base[i];}columns+=lit;}
    if(report)std::printf("volley beam columns %u\n",columns);
    assert(columns>=110);
    const Frame final=frame(Effect::Volley,1420,cyan);
    unsigned focus=0;
    for(int y=43+3;y<=43+14;++y)for(int x=62;x<=72;++x){const int i=y*135+x;focus+=!base[i] && final[i]!=base[i];}
    if(report)std::printf("volley focus %u\n",focus);
    assert(focus>=12);
  }
  // Starfall: before the fall the sky is many separate small lights in a
  // band under the readout and nothing lower; then they all strike a dome
  // well below it.
  {
    const Frame sky=frame(Effect::Starfall,1000,cyan);
    std::array<bool,135*240> seen{};unsigned lights=0;int lowest=0;
    for(int i=0;i<135*240;++i){
      if(seen[i] || base[i] || sky[i]==base[i])continue;
      ++lights;int stack[512],n=0;stack[n++]=i;seen[i]=true;
      while(n){const int j=stack[--n],x=j%135,y=j/135;lowest=std::max(lowest,y);
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int xx=x+dx,yy=y+dy;if(xx<0 || xx>=135 || yy<0 || yy>=240)continue;const int k=yy*135+xx;
          if(!seen[k] && !base[k] && sky[k]!=base[k] && n<512){seen[k]=true;stack[n++]=k;}}}
    }
    if(report)std::printf("starfall lights %u lowest %d\n",lights,lowest);
    assert(lights>=25 && lowest<43+40);
    const Frame struck=frame(Effect::Starfall,1400,cyan);
    unsigned dome=0;for(int y=43+28;y<43+60;++y)for(int x=0;x<135;++x){const int i=y*135+x;dome+=!base[i] && struck[i]!=base[i];}
    if(report)std::printf("starfall dome %u\n",dome);
    assert(dome>80);
  }
  // Mimic: one large chest centred just under the readout; it bares
  // white teeth as it chomps, and it hops: its outline rises and falls
  // through the sustain while it stays the same size.
  {
    const Stats rest=bgStats(frame(Effect::Mimic,1000,cyan));
    if(report)std::printf("mimic box x %d..%d y %d..%d cx %.1f\n",rest.x0,rest.x1,rest.y0,rest.y1,rest.cx);
    assert(rest.x1-rest.x0>=45 && rest.x1-rest.x0<=75 && std::fabs(rest.cx-67)<12 && rest.y1<43+42);
    const Frame bite=frame(Effect::Mimic,1550,cyan);
    unsigned teeth=0;for(int i=0;i<135*240;++i)if(!base[i] && bite[i]!=base[i] && red(bite[i])>24 && green(bite[i])>50 && blue(bite[i])>24)++teeth;
    if(report)std::printf("mimic teeth %u\n",teeth);
    assert(teeth>=12);
    int lowTop=0,highTop=240;
    for(unsigned ms=1900;ms<3700;ms+=50){const Stats s=bgStats(frame(Effect::Mimic,ms,cyan));lowTop=std::max(lowTop,s.y0);highTop=std::min(highTop,s.y0);}
    if(report)std::printf("mimic hop %d..%d\n",highTop,lowTop);
    assert(lowTop-highTop>=3);
  }
  // Scales: a balance under the readout first settles with its left pan
  // low, then reverses and holds with its right pan low.
  {
    auto lean=[&](unsigned ms){
      const Frame f=frame(Effect::Scales,ms,cyan);double ly=0,ry=0;unsigned ln=0,rn=0;
      for(int y=44;y<120;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || f[i]==base[i])continue;if(x<50){ly+=y;++ln;}else if(x>84){ry+=y;++rn;}}
      return (ry/std::max(1u,rn))-(ly/std::max(1u,ln));
    };
    const double before=lean(1150),after=lean(2600);
    if(report)std::printf("scales lean before %.1f after %.1f\n",before,after);
    assert(before<-3 && after>6);
  }
  // Meadow: the carpet spreads out from one point (half way through the
  // spread the screen's sides are still bare), then covers the whole width
  // of the floor with tiny flowers out to both edges.
  {
    const Frame half=frame(Effect::Meadow,450,cyan),full=frame(Effect::Meadow,2500,cyan);
    unsigned sides=0,middle=0,total=0,bands[9]={};
    for(int y=43+70;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i])continue;
      if(half[i]!=base[i]){if(x<12 || x>122)++sides;else ++middle;}
      if(full[i]!=base[i]){++total;++bands[x/15];}}
    // The fixture's replay block covers the middle bands; the outer ones are open floor.
    const unsigned thin=std::min({bands[0],bands[1],bands[7],bands[8]});
    if(report)std::printf("meadow half sides %u middle %u full %u thinnest band %u\n",sides,middle,total,thin);
    assert(sides==0 && middle>40 && total>500 && thin>25);
  }
  std::cout<<"PASS: five grimoire effects deterministic, halo-clipped, ink-exact by 3/4, custom-background invariant, within the 9 ms cost model, with per-effect identity properties\n";
}
