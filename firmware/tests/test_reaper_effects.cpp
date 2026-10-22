// The reaper set of 0.18 (Crescent..Blink): the shared renderer contract,
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
  const Effect effects[]={Effect::Crescent,Effect::Release,Effect::Petalfall,Effect::Mask,Effect::Blink};
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
  // 80 MHz): every frame at most 8 ms.
  // Each runs about five seconds (owner, 2026-10-20): an early impact, a
  // sustained middle that keeps moving but costs less than the impact, and
  // a settle. Windows in ms: impact from/to, sustain from/to.
  const unsigned windows[5][4]={{600,1500,1800,3700},{750,1700,2300,3700},{900,1800,2000,3500},{500,1500,1800,3700},{500,1500,1800,3700}};
  const bool report=std::getenv("REAPER_COSTS")!=nullptr;
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
  // Crescent: a screen-wide arc, short and red-dominant, races up the
  // screen; behind it stays a tall, narrow red rift.
  {
    unsigned redWins=0,strong=0,lit=0;double prev=1e9;bool rising=true;
    for(unsigned ms:{750u,850u,950u,1050u}){
      const Frame f=frame(Effect::Crescent,ms,cyan);
      Frame arc=f;for(int i=0;i<135*240;++i){const int x=i%135;if(x>60 && x<100)arc[i]=base[i];} // leave out the rift's column
      const Stats s=bgStats(arc);
      if(report)std::printf("crescent %u: x %d..%d y %d..%d cy %.1f\n",ms,s.x0,s.x1,s.y0,s.y1,s.cy);
      assert(s.x1-s.x0>100 && s.y1-s.y0<60);rising&=s.cy<prev-15;prev=s.cy;
      for(int i=0;i<135*240;++i)if(!base[i] && f[i]!=base[i]){++lit;redWins+=red(f[i])>=blue(f[i]);strong+=red(f[i])>=blue(f[i])+8;}
    }
    if(report)std::printf("crescent red %u strong %u of %u\n",redWins,strong,lit);
    assert(rising && redWins*10>lit*9 && strong*10>lit*4);
    const Stats rift=bgStats(frame(Effect::Crescent,2200,cyan));
    if(report)std::printf("crescent rift x %d..%d y %d..%d n %u\n",rift.x0,rift.x1,rift.y0,rift.y1,rift.n);
    assert(rift.x1-rift.x0<40 && rift.y1-rift.y0>80 && rift.n>150);
  }
  // Release: a filled column of light centred under the readout, from its
  // foot down past the replay (a gradient, not outlines: most pixels of its
  // middle are lit), narrower in the sustain than at the eruption.
  {
    const Frame f=frame(Effect::Release,1200,cyan);const Stats burst=bgStats(f),held=bgStats(frame(Effect::Release,3000,cyan));
    unsigned lit=0,open=0;
    for(int y=50;y<110;++y)for(int x=67-7;x<=67+7;++x){const int i=y*135+x;if(halo[i])continue;++open;lit+=f[i]!=base[i];}
    if(report)std::printf("release burst x %d..%d y %d..%d n %u fill %u/%u; held x %d..%d n %u\n",burst.x0,burst.x1,burst.y0,burst.y1,burst.n,lit,open,held.x0,held.x1,held.n);
    assert(burst.n>600 && burst.x0>=67-34 && burst.x1<=67+34 && burst.y0<=50 && burst.y1-burst.y0>60 && std::fabs(burst.cx-67)<5);
    assert(lit*10>=open*8 && held.n>300 && held.x1-held.x0<burst.x1-burst.x0 && std::fabs(held.cx-67)<5);
  }
  // Petalfall: a pink river of petals of several pixels each (most lit
  // pixels touch another), whose mass streams round through the sustain.
  {
    unsigned pinkish=0,lit=0,touching=0;double xs[3],ys[3];int k=0;
    for(unsigned ms:{1900u,2400u,2900u}){
      const Frame f=frame(Effect::Petalfall,ms,cyan);const Stats s=bgStats(f);xs[k]=s.cx;ys[k++]=s.cy;
      if(report)std::printf("petalfall %u: x %d..%d y %d..%d c %.1f,%.1f n %u\n",ms,s.x0,s.x1,s.y0,s.y1,s.cx,s.cy,s.n);
      assert(s.n>150 && s.x1-s.x0>35);
      auto on=[&](int x,int y){return x>=0 && x<135 && y>=0 && y<240 && !base[y*135+x] && f[y*135+x]!=base[y*135+x];};
      for(int i=0;i<135*240;++i)if(on(i%135,i/135)){const int x=i%135,y=i/135;++lit;
        pinkish+=red(f[i])>=blue(f[i])+3 && 2*red(f[i])>=green(f[i])+6;touching+=on(x-1,y)||on(x+1,y)||on(x,y-1)||on(x,y+1);}
    }
    if(report)std::printf("petalfall pink %u touching %u of %u\n",pinkish,touching,lit);
    double moved=0;for(int a=0;a<3;++a)for(int c=a+1;c<3;++c)moved=std::max(moved,std::hypot(xs[a]-xs[c],ys[a]-ys[c]));
    assert(pinkish*20>=lit*11 && touching*10>=lit*8 && moved>15);
  }
  // Mask: a shell under the readout with two gold eye slits either side
  // of its centre and red stripes; later its pieces fly well apart. On the
  // header alone (this fixture's replay lines cross the eyes).
  {
    Frame bare{};std::copy(base.begin(),base.begin()+47*135,bare.begin());
    auto onBare=[&](unsigned ms){Frame f=bare;renderer.draw({f.data(),135,240},Effect::Mask,ms,cyan);return f;};
    auto spread=[&](const Frame &f){Stats s;for(int i=50*135;i<135*240;++i)if(f[i]!=bare[i]){++s.n;const int x=i%135,y=i/135;s.x0=std::min(s.x0,x);s.x1=std::max(s.x1,x);s.y0=std::min(s.y0,y);s.y1=std::max(s.y1,y);}return s;};
    const Frame f=onBare(2000);const Stats s=spread(f);
    unsigned goldL=0,goldR=0,redStripe=0;
    for(int i=50*135;i<135*240;++i){if(f[i]==bare[i])continue;const int x=i%135;
      if(red(f[i])>=20 && green(f[i])>=24 && blue(f[i])+8<=red(f[i]) && green(f[i])>=red(f[i]))(x<67?goldL:goldR)++;
      if(red(f[i])>=20 && green(f[i])<=24 && blue(f[i])<=12)++redStripe;}
    const Stats shards=spread(onBare(4100));
    if(report)std::printf("mask x %d..%d y %d..%d n %u gold %u/%u red %u; shards x %d..%d\n",s.x0,s.x1,s.y0,s.y1,s.n,goldL,goldR,redStripe,shards.x0,shards.x1);
    assert(s.n>400 && s.x0>=67-46 && s.x1<=67+46 && s.y0>=45 && s.y1<=112);
    assert(goldL>=12 && goldR>=12 && redStripe>=40 && shards.x1-shards.x0>s.x1-s.x0+10);
  }
  // Blink: the brightest afterimage jumps to a new place at every step,
  // then the cut spans the whole screen width.
  {
    static constexpr unsigned AT[]={270,520,720,870,990,1090};
    double px=-1,py=-1;unsigned jumps=0;
    for(unsigned ms:AT){
      const Frame f=frame(Effect::Blink,ms,cyan);double sx=0,sy=0;unsigned n=0;
      for(int i=0;i<135*240;++i)if(!base[i] && f[i]!=base[i] && red(f[i])>=27 && green(f[i])>=55 && blue(f[i])>=27){sx+=i%135;sy+=i/135;++n;}
      assert(n>=10);const double cx=sx/n,cy=sy/n;
      if(report)std::printf("blink %u: white %u at %.1f,%.1f\n",ms,n,cx,cy);
      if(px>=0 && std::hypot(cx-px,cy-py)>18)++jumps;px=cx;py=cy;
    }
    const Stats cut=bgStats(frame(Effect::Blink,1300,cyan));
    if(report)std::printf("blink jumps %u cut x %d..%d\n",jumps,cut.x0,cut.x1);
    assert(jumps>=4 && cut.x0<6 && cut.x1>128);
  }
  std::cout<<"PASS: five reaper effects deterministic, halo-clipped, ink-exact by 3/4, custom-background invariant, within the 8 ms cost model, with per-effect identity properties\n";
}
