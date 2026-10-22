// The aura set of 0.18 (Haymaker..Waterleaf): the shared renderer contract,
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
  const Effect effects[]={Effect::Haymaker,Effect::Palms,Effect::Elastic,Effect::Chains,Effect::Waterleaf};
  // How each may touch the UI's own pixels: `moves` shifts or recolours
  // readout rows freely (exact again by 3/4), `dims` may darken glyphs (never
  // below 3:1); the others only brighten glyph pixels.
  const bool moves[]={false,false,true,false,false},dims[]={false,false,false,false,false};
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
  const unsigned windows[5][4]={{1000,1600,1900,3700},{800,1800,2000,3700},{900,1700,2000,3700},{550,1500,1800,3700},{1250,1700,2000,3700}};
  const bool report=std::getenv("AURA_COSTS")!=nullptr;
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
  // Haymaker: a fist rests well below the readout, then drives up to its
  // foot in under 0.1 s; at the contact the digits flash, but only in the
  // readout's lower rows round the middle.
  {
    auto fistTop=[&](const Frame &f){for(int y=46;y<120;++y)for(int x=55;x<80;++x){const int i=y*135+x;if(!base[i] && f[i]!=base[i] && blue(f[i])>14 && green(f[i])>40)return y;}return 999;};
    const int rest=fistTop(frame(Effect::Haymaker,700,cyan)),cocked=fistTop(frame(Effect::Haymaker,980,cyan)),struck=fistTop(frame(Effect::Haymaker,1080,cyan));
    const Frame hit=frame(Effect::Haymaker,1150,cyan);
    unsigned low=0,high=0,side=0;
    for(int y=17;y<44;++y)for(int x=10;x<125;++x){const int i=y*135+x;if(!base[i] || hit[i]==base[i])continue;if(std::abs(x-67)>40)++side;else if(y>=34)++low;else ++high;}
    if(report)std::printf("haymaker top rest %d cocked %d struck %d heat low %u high %u side %u\n",rest,cocked,struck,low,high,side);
    assert(rest>=56 && cocked>rest && struck<=49 && low>150 && high==0 && side==0);
  }
  // Palms: open hands stand in both side margins through the sustain, and
  // they strike one at a time, alternating sides: a white-hot hand on the
  // left at the first strike, then one on the right as well at the second.
  {
    auto white=[&](const Frame &f,bool left){unsigned n=0;for(int y=46;y<200;++y)for(int x=left?0:68;x<(left?67:135);++x){const int i=y*135+x;if(!base[i] && f[i]!=base[i] && red(f[i])>26 && green(f[i])>54 && blue(f[i])>18)++n;}return n;};
    const Frame s1=frame(Effect::Palms,880,cyan),s2=frame(Effect::Palms,955,cyan);
    const unsigned l1=white(s1,true),r1=white(s1,false),l2=white(s2,true),r2=white(s2,false);
    const Frame held=frame(Effect::Palms,2500,cyan);
    unsigned margins[2]={0,0};
    for(int y=70;y<125;++y)for(int x=0;x<135;++x){const int i=y*135+x;if((x<18 || x>116) && !base[i] && held[i]!=base[i] && red(held[i])>green(held[i])/2)++margins[x>116];}
    if(report)std::printf("palms strike1 %u/%u strike2 %u/%u margins %u %u\n",l1,r1,l2,r2,margins[0],margins[1]);
    assert(l1>20 && r1<l1/4 && r2>r1+20 && l2<l1+10 && margins[0]>40 && margins[1]>40);
  }
  // Elastic: the gum reaches both screen edges while it stretches, and the
  // readout's lower half is pulled taller (its upper half untouched); after
  // the snap the strands hang under the readout, clear of the edges.
  {
    auto pinkAt=[&](const Frame &f,int x0,int x1){unsigned n=0;for(int y=46;y<200;++y)for(int x=x0;x<=x1;++x){const int i=y*135+x;if(!base[i] && f[i]!=base[i] && red(f[i])>20 && blue(f[i])>10 && green(f[i])<red(f[i])*2)++n;}return n;};
    const Frame pulled=frame(Effect::Elastic,1000,cyan),hung=frame(Effect::Elastic,2600,cyan);
    unsigned upper=0,lowerRows=0;
    for(int y=17;y<44;++y)for(int x=10;x<125;++x){const int i=y*135+x;if(pulled[i]!=base[i]){if(y<30)++upper;else ++lowerRows;}}
    const unsigned edgeL=pinkAt(pulled,0,3),edgeR=pinkAt(pulled,131,134),hungEdges=pinkAt(hung,0,8)+pinkAt(hung,126,134),hungMid=pinkAt(hung,9,125);
    if(report)std::printf("elastic edges %u %u upper %u lower %u hung edges %u mid %u\n",edgeL,edgeR,upper,lowerRows,hungEdges,hungMid);
    assert(edgeL>4 && edgeR>4 && upper==0 && lowerRows>100 && hungEdges==0 && hungMid>40);
  }
  // Chains: once locked, rows of links run the full width just above and
  // just below the readout, and link columns run down both screen edges
  // between them; at the lock the whole loop flashes scarlet.
  {
    const Frame held=frame(Effect::Chains,1500,cyan),locked=frame(Effect::Chains,760,cyan);
    auto lit=[&](const Frame &f,int i){return !base[i] && f[i]!=base[i];};
    unsigned topCols=0,botCols=0,edges=0,scarlet=0;
    for(int x=0;x<135;++x){bool top=false,bot=false;for(int y=10;y<=14;++y)top|=lit(held,y*135+x);for(int y=46;y<=52;++y)bot|=lit(held,y*135+x);topCols+=top;botCols+=bot;}
    for(int y=14;y<=48;++y)for(int x:{0,1,2,132,133,134})edges+=lit(held,y*135+x);
    for(int i=0;i<135*60;++i)if(lit(locked,i) && red(locked[i])>18 && green(locked[i])<red(locked[i]))++scarlet;
    if(report)std::printf("chains top %u bottom %u edges %u scarlet %u\n",topCols,botCols,edges,scarlet);
    assert(topCols>100 && botCols>120 && edges>60 && scarlet>200);
  }
  // Waterleaf: a glass stands centred under the readout (its two walls lit
  // either side of the middle, a green leaf on the water), and at the surge
  // water sprays out to both sides of the screen.
  {
    const Frame held=frame(Effect::Waterleaf,2500,cyan),surge=frame(Effect::Waterleaf,1500,cyan);
    auto lit=[&](const Frame &f,int i){return !base[i] && f[i]!=base[i];};
    unsigned wallL=0,wallR=0,leaf=0,sprayL=0,sprayR=0;
    for(int y=60;y<88;++y){for(int x=50;x<=54;++x)wallL+=lit(held,y*135+x);for(int x=80;x<=84;++x)wallR+=lit(held,y*135+x);}
    for(int y=46;y<62;++y)for(int x=52;x<83;++x){const int i=y*135+x;if(lit(held,i) && green(held[i])>2*red(held[i])+6 && green(held[i])>2*blue(held[i]))++leaf;}
    for(int y=46;y<200;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(lit(surge,i)){sprayL+=x<35;sprayR+=x>100;}}
    if(report)std::printf("waterleaf walls %u %u leaf %u spray %u %u\n",wallL,wallR,leaf,sprayL,sprayR);
    assert(wallL>20 && wallR>20 && leaf>=6 && sprayL>4 && sprayR>4);
  }
  std::cout<<"PASS: five aura effects deterministic, halo-clipped, ink-exact by 3/4, custom-background invariant, within the 8 ms cost model, with per-effect identity properties\n";
}
