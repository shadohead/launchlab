// The phantom set of 0.18 (Timestop..Sequel): the shared renderer contract,
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
  const Effect effects[]={Effect::Timestop,Effect::Zipper,Effect::Rush,Effect::Menace,Effect::Sequel};
  // How each may touch the UI's own pixels: `moves` shifts or recolours
  // readout rows freely (exact again by 3/4), `dims` may darken glyphs (never
  // below 3:1); the others only brighten glyph pixels.
  const bool moves[]={false,true,false,false,false},dims[]={false,false,false,false,true};
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
  // 80 MHz): every frame at most 8 ms.
  // Each runs about five seconds (owner, 2026-10-20): an early impact, a
  // sustained middle that keeps moving but costs less than the impact, and
  // a settle. Windows in ms: impact from/to, sustain from/to.
  const unsigned windows[5][4]={{400,1300,1600,3200},{500,1450,2100,3900},{400,1800,2000,3800},{1400,1900,2100,3800},{500,1300,2000,3900}};
  const bool report=std::getenv("PHANTOM_COSTS")!=nullptr;
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
    assert(worst<=8000 && sustain<impact);
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
  // Timestop: the sphere's rim sweeps down the screen within a few frames;
  // while time is stopped the knives are cold and colourless (no gold),
  // and as it resumes their gold hilts flood back.
  {
    auto lowest=[&](const Frame &f){int y1=-1;for(int i=50*135;i<135*240;++i)if(!base[i] && f[i]!=base[i])y1=i/135;return y1;};
    const int r0=lowest(frame(Effect::Timestop,450,cyan)),r1=lowest(frame(Effect::Timestop,500,cyan)),r2=lowest(frame(Effect::Timestop,550,cyan));
    auto golds=[&](const Frame &f){unsigned n=0;for(int i=47*135;i<135*240;++i)if(!base[i] && f[i]!=base[i] && red(f[i])>blue(f[i])+8 && green(f[i])>20)++n;return n;};
    const unsigned frozen=golds(frame(Effect::Timestop,2400,cyan)),resumed=golds(frame(Effect::Timestop,3420,cyan));
    const Stats held=bgStats(frame(Effect::Timestop,2400,cyan));
    if(report)std::printf("timestop rim %d %d %d gold %u -> %u held %u\n",r0,r1,r2,frozen,resumed,held.n);
    assert(r1>r0+20 && r2>r1+20 && frozen<5 && resumed>40 && held.n>250);
  }
  // Zipper: during the gape the readout's middle rows hold a dark violet
  // gap (between lips of teeth) the whole width of the opened mouth; once
  // shut, a band of gold teeth rests under the readout.
  {
    const Frame gape=frame(Effect::Zipper,1100,cyan);
    unsigned gap=0;int gx0=135,gx1=-1;
    for(int y=17;y<=43;++y)for(int x=0;x<135;++x){const uint16_t v=gape[y*135+x];
      if(v && blue(v)>red(v) && blue(v)<=8 && green(v)<=4){++gap;gx0=std::min(gx0,x);gx1=std::max(gx1,x);}}
    const Frame rest=frame(Effect::Zipper,2600,cyan);
    unsigned teeth=0;int tx0=135,tx1=-1;
    for(int y=46;y<=52;++y)for(int x=0;x<135;++x){const int i=y*135+x;
      if(!base[i] && rest[i]!=base[i] && red(rest[i])>blue(rest[i])+8 && green(rest[i])>20){++teeth;tx0=std::min(tx0,x);tx1=std::max(tx1,x);}}
    if(report)std::printf("zipper gap %u x %d..%d teeth %u x %d..%d\n",gap,gx0,gx1,teeth,tx0,tx1);
    assert(gap>300 && gx1-gx0>90 && teeth>100 && tx1-tx0>100);
  }
  // Rush: the barrage comes from both sides at once, under the readout;
  // the uppercut then lands on the readout's underside, flashing only its
  // lowest glyph rows.
  {
    const Frame jab=frame(Effect::Rush,900,cyan);
    unsigned left=0,right=0;
    for(int y=50;y<=95;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || jab[i]==base[i])continue;left+=x<45;right+=x>=90;}
    const Frame hit=frame(Effect::Rush,1620,cyan);
    unsigned low=0,high=0;
    for(int y=17;y<=43;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] && hit[i]!=base[i])(y>=39?low:high)++;}
    if(report)std::printf("rush jabs left %u right %u, uppercut low %u high %u\n",left,right,low,high);
    assert(left>40 && right>40 && low>40 && high==0);
  }
  // Menace: violet marks stand in both side margins beside the replay,
  // and at the flash they all burn white.
  {
    const Frame tense=frame(Effect::Menace,1200,cyan),burst=frame(Effect::Menace,1580,cyan);
    unsigned left=0,right=0,white=0;
    for(int y=50;y<=150;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i])continue;
      if(tense[i]!=base[i] && red(tense[i])*2>green(tense[i]) && blue(tense[i])*2>green(tense[i])+4){left+=x<30;right+=x>=105;}
      if(burst[i]!=base[i] && red(burst[i])>20 && green(burst[i])>40 && blue(burst[i])>20)++white;}
    if(report)std::printf("menace violet left %u right %u, white at the flash %u\n",left,right,white);
    assert(left>80 && right>80 && white>150);
  }
  // Sequel: at the freeze the readout turns a warm sepia (red up, blue
  // down) and the burst stops dead (the same pixels lit on consecutive
  // frames); then a left-pointing arrow banner rests in the band under the
  // readout, nearly the screen's width, its tip at the left.
  {
    const Frame a=frame(Effect::Sequel,700,cyan),b=frame(Effect::Sequel,750,cyan);
    unsigned warm=0,still=0,lit=0;
    for(int i=0;i<135*240;++i){
      if(base[i]){warm+=a[i]!=base[i] && red(a[i])>red(base[i])+8 && blue(a[i])+6<blue(base[i]);continue;}
      if(i/135>47 && a[i]!=base[i]){++lit;still+=b[i]==a[i];}
    }
    const Frame r=frame(Effect::Sequel,2600,cyan);
    int tipX=135,tipY=0,right=-1;
    for(int y=46;y<=72;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || r[i]==base[i])continue;
      if(x<tipX){tipX=x;tipY=y;}right=std::max(right,x);}
    if(report)std::printf("sequel warm %u frozen %u/%u arrow tip %d,%d right %d\n",warm,still,lit,tipX,tipY,right);
    assert(warm>1000 && lit>60 && still*10>=lit*8 && tipX<=18 && tipY>=55 && tipY<=60 && right>110);
  }
  std::cout<<"PASS: five phantom effects deterministic, halo-clipped, ink-exact by 3/4, custom-background invariant, within the 8 ms cost model, with per-effect identity properties\n";
}
