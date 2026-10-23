// The corsair set of 0.18 (Slingshot..Vapor): the shared renderer contract,
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
  const Effect effects[]={Effect::Slingshot,Effect::Presence,Effect::Threefold,Effect::Banner,Effect::Vapor};
  // How each may touch the UI's own pixels: `moves` shifts or recolours
  // readout rows freely (exact again by 3/4), `dims` may darken glyphs (never
  // below 3:1); the others only brighten glyph pixels.
  const bool moves[]={true,false,false,false,false},dims[]={false,true,false,false,false};
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
  const unsigned windows[5][4]={{500,1500,1800,3700},{500,1500,1800,3700},{500,1500,1800,3700},{500,1500,1800,3700},{800,1850,1900,3700}};
  const bool report=std::getenv("CORSAIR_COSTS")!=nullptr;
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
  // Slingshot: in flight a rubber arm spans the band below the readout from
  // the screen's right edge to the readout's left half; at the hit the
  // readout's foot is knocked sideways while its top rows stay put.
  {
    const Frame fly=frame(Effect::Slingshot,960,cyan);
    int x0=135,x1=-1;unsigned cols=0;
    for(int x=0;x<135;++x){bool lit=false;for(int y=47;y<75;++y){const int i=y*135+x;lit|=!base[i] && fly[i]!=base[i];}if(lit){++cols;x0=std::min(x0,x);x1=std::max(x1,x);}}
    if(report)std::printf("slingshot arm x %d..%d cols %u\n",x0,x1,cols);
    assert(x0<60 && x1>128 && cols>65);
    const Frame hit=frame(Effect::Slingshot,1010,cyan);
    unsigned top=0,foot=0;
    for(int x=0;x<135;++x){for(int y=17;y<25;++y){const int i=y*135+x;top+=base[i] && hit[i]!=base[i];}for(int y=38;y<44;++y){const int i=y*135+x;foot+=base[i] && hit[i]!=base[i];}}
    if(report)std::printf("slingshot knock foot %u top %u\n",foot,top);
    assert(foot>100 && top==0);
  }
  // Presence: through the hush the readout darkens (most glyph pixels lose
  // green, never below 3:1, checked above) with almost nothing drawn below
  // it; at the release red-rimmed lightning spans the band under the readout
  // from edge to edge.
  {
    const Frame hush=frame(Effect::Presence,450,cyan);
    unsigned dark=0,lit=0;
    for(int i=0;i<135*240;++i){if(base[i]){if(i<47*135 && 5*green(hush[i])<4*green(base[i]))++dark;}else if(i>=50*135 && hush[i]!=base[i])++lit;}
    if(report)std::printf("presence hush dark %u lit %u\n",dark,lit);
    assert(dark>1500 && lit<200);
    const Frame burst=frame(Effect::Presence,650,cyan);
    int x0=135,x1=-1;unsigned reds=0;
    for(int y=45;y<62;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || burst[i]==base[i])continue;if(red(burst[i])>12 && green(burst[i])<24){++reds;x0=std::min(x0,x);x1=std::max(x1,x);}}
    if(report)std::printf("presence band red %u x %d..%d\n",reds,x0,x1);
    assert(reds>150 && x0<12 && x1>122);
  }
  // Threefold: nothing is drawn below the band during the beat after the
  // dash; then three cuts open at once: lit pixels lie along three lines
  // through one point (two diagonals and a level one), far more than off
  // them.
  {
    const Frame beat=frame(Effect::Threefold,800,cyan);
    unsigned below=0;for(int i=62*135;i<135*240;++i)below+=!base[i] && beat[i]!=base[i];
    const Frame cut=frame(Effect::Threefold,1060,cyan);
    const double cx=(10+124)*.5-14,cy=43+24;
    unsigned on[3]={},off=0;
    for(int y=47;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || cut[i]==base[i])continue;
      const double dx=x-cx,dy=y-cy;bool hit=false;
      for(int k=0;k<3;++k){const double an=k==2?0:(k?.6:-.6),d=std::fabs(-std::sin(an)*dx+std::cos(an)*dy);if(d<=2.5){++on[k];hit=true;break;}}
      off+=!hit;}
    if(report)std::printf("threefold below %u on %u %u %u off %u\n",below,on[0],on[1],on[2],off);
    assert(below<40 && on[0]>60 && on[1]>60 && on[2]>60 && off*2<on[0]+on[1]+on[2]);
  }
  // Banner: nothing flies before the snap, only the mast and the furled
  // flag at its side; once open, a mast stands at a sixth of the readout's
  // width and the flag's two hems run from it most of the way across.
  {
    const Frame furled=frame(Effect::Banner,700,cyan),flying=frame(Effect::Banner,2000,cyan);
    const int mx=int(10+114*.17);unsigned wide=0,mast=0,hem=0;
    for(int i=47*135;i<135*240;++i){const int x=i%135;if(!base[i] && furled[i]!=base[i] && std::abs(x-mx)>6)++wide;}
    for(int y=52;y<110;++y){const int i=y*135+mx;mast+=!base[i] && flying[i]!=base[i];}
    for(int x=mx+4;x<135;++x){bool lit=false;for(int y=46;y<60;++y){const int i=y*135+x;lit|=!base[i] && flying[i]!=base[i];}hem+=lit;}
    if(report)std::printf("banner furled wide %u mast %u hem cols %u\n",wide,mast,hem);
    assert(wide<20 && mast>30 && hem>65);
  }
  // Vapor: a beat warms only a band of the readout's rows (a pulse running
  // up it), and a burst of speed lays long level streaks through the band
  // under the readout.
  {
    const Frame beat=frame(Effect::Vapor,360,cyan);
    int r0=240,r1=-1;unsigned warm=0;
    for(int y=17;y<44;++y)for(int x=10;x<125;++x){const int i=y*135+x;if(base[i] && beat[i]!=base[i]){++warm;r0=std::min(r0,y);r1=std::max(r1,y);}}
    const Frame burst=frame(Effect::Vapor,1200,cyan);
    unsigned runs=0;
    for(int y=46;y<70;++y){int run=0,best=0;for(int x=0;x<135;++x){const int i=y*135+x;run=!base[i] && burst[i]!=base[i]?run+1:0;best=std::max(best,run);}runs+=best>=40;}
    if(report)std::printf("vapor warm %u rows %d..%d runs %u\n",warm,r0,r1,runs);
    assert(warm>200 && r1-r0<=11 && runs>=3);
  }
  std::cout<<"PASS: five corsair effects deterministic, halo-clipped, ink-exact by 3/4, custom-background invariant, within the 9 ms cost model, with per-effect identity properties\n";
}
