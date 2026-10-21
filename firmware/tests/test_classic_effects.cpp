// The six classic celebrations (Orbit, Flames, Sparkles, Shockwave,
// Lightning, Slash) at their 5 s lengths: the shared renderer contract,
// the device cost-model bound on every frame, a living sustain, and one
// identity property per effect. Same fixture as test_physical_effects.
#include "best_effect_ui.h"
#include <cassert>
#include <cmath>
#include <iostream>

static std::array<uint16_t,135*240> base_{};
static uint16_t swap16(uint16_t v){return uint16_t(v<<8|v>>8);}

int main(){
  using namespace Appearance;
  std::array<uint16_t,135*240> &base=base_;
  auto ui=[&](int x,int y,uint16_t c){base[y*135+x]=swap16(c);};
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
  const Effect effects[]={Effect::Orbit,Effect::Flames,Effect::Sparkles,Effect::Shockwave,Effect::Lightning,Effect::Slash};
  const uint32_t plotBound[]={1400,1800,1300,1500,1400,3200};
  auto frame=[&](Effect e,unsigned ms,uint16_t accentColor){
    auto out=base;renderer.draw({out.data(),135,240},e,ms,accentColor);return out;
  };
  struct Stats{unsigned n=0;double cx=0,cy=0;};
  auto bgStats=[&](const std::array<uint16_t,135*240> &out){
    Stats s;double sx=0,sy=0;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x){
      const int i=y*135+x;
      if(base[i] || out[i]==base[i])continue;
      ++s.n;sx+=x;sy+=y;
    }
    if(s.n){s.cx=sx/s.n;s.cy=sy/s.n;}
    return s;
  };
  // Brightness of the changed background pixels, not just their count: a
  // sustain can change many pixels and still be invisible at 1x if each
  // one is only a few channel units above black. light sums the RGB565
  // channel units (max 125 per pixel); bright counts pixels at 50+.
  struct Light{unsigned n=0,bright=0;long light=0;};
  auto bgLight=[&](const std::array<uint16_t,135*240> &out){
    Light s;
    for(int i=0;i<135*240;++i){
      if(base[i] || out[i]==base[i])continue;
      const uint16_t v=swap16(out[i]);const long sum=(v>>11)+((v>>5)&63)+(v&31);
      ++s.n;s.light+=sum;s.bright+=sum>=50;
    }
    return s;
  };

  for(unsigned e=0;e<6;++e){
    const Effect effect=effects[e];
    const bool slash=effect==Effect::Slash;
    const uint32_t dur=duration(effect);
    assert(dur>=4800 && dur<=5200);
    // Sounds stay a hit and a short tail under the 5 s effect.
    assert(soundDuration(effect)>=1000 && soundDuration(effect)<=2800 && soundDuration(effect)<=dur);
    // Worst frame by the device cost model fit on the M5 (NUMBERS.md),
    // bounded at 8.0 ms over the whole 5 s, sustain included.
    double worstUs=0;unsigned worstMs=0;uint32_t wPlots=0,wProbes=0,wScans=0;
    for(unsigned theme:{0u,3u,5u,8u}){
      const uint16_t color=accent(Theme(theme));
      uint32_t maxPlots=0;bool drew=false,glinted=false;
      for(unsigned ms=0;ms<=5300;ms+=10){
        auto out=frame(effect,ms,color);
        auto again=frame(effect,ms,color);
        assert(out==again);
        maxPlots=std::max(maxPlots,renderer.plots());
        const double estUs=1111+1.82*renderer.plots()+1.04*renderer.probes()+13.9*renderer.scans();
        if(estUs>worstUs){worstUs=estUs;worstMs=ms;wPlots=renderer.plots();wProbes=renderer.probes();wScans=renderer.scans();}
        const bool settledMs=ms*4>=dur*3; // from 3/4 on, UI pixels are exact
        for(int i=0;i<135*240;++i)if(out[i]!=base[i]){
          drew=true;
          if(base[i]){
            assert(!settledMs);
            if(slash)assert(i/135>=13-6 && i/135<=46+6); // only the cut rows move
            else {
              const uint16_t was=swap16(base[i]),now=swap16(out[i]);
              assert(i/135<=46 && (now>>11)>=(was>>11) && ((now>>5)&63)>=((was>>5)&63) && (now&31)>=(was&31));glinted=true;
            }
          }else if(!slash || i/135<13-6 || i/135>46+6)assert(!halo[i]);
          // Slash may move ink onto background just outside the readout rows.
        }
        if(ms>=dur)assert(out==base);
      }
      assert(drew && glinted==!slash && maxPlots<=plotBound[e]);
    }
    std::cout<<label(effect)<<": worst frame "<<unsigned(worstUs)<<" us modelled at "<<worstMs<<" ms (plots "<<wPlots<<", probes "<<wProbes<<", scans "<<wScans<<")\n";
    assert(worstUs<=8000);
    // The sustain lives: decoration is present and moving well past the
    // old 1.3-1.8 s endings, and never frozen between samples.
    {
      const uint16_t cyan=accent(Theme::Classic);
      unsigned sustainPx=0,alive=0;
      for(unsigned ms:{2600u,3000u,3400u,3800u}){const unsigned n=bgStats(frame(effect,ms,cyan)).n;sustainPx+=n;alive+=n>0;}
      assert(sustainPx>40 && alive>=3);
      assert(frame(effect,3000,cyan)!=frame(effect,3300,cyan));
    }
    // A custom theme's background: the same pixels change as on black.
    const uint16_t bg=0x0841;std::array<uint16_t,135*240> themed=base;
    for(auto &p:themed)if(!p)p=swap16(bg);
    for(unsigned ms:{40u,300u,700u,1200u,2600u,3800u,4600u}){
      auto black=frame(effect,ms,accent(Theme::Mint));
      auto out=themed;renderer.draw({out.data(),135,240},effect,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
  }

  const uint16_t cyan=accent(Theme::Classic);
  // Orbit: the comets are still on their ellipse around the readout deep
  // into the sustain, and they have moved between frames.
  {
    auto a=frame(Effect::Orbit,2500,cyan),b=frame(Effect::Orbit,2600,cyan);
    unsigned band=0,moved=0;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x){
      const int i=y*135+x;if(base[i])continue;
      if(y<=62 && a[i]!=base[i])++band;
      if((a[i]!=base[i])!=(b[i]!=base[i]))++moved;
    }
    assert(band>40 && moved>20);
  }
  // Flames: a full blaze early, then a clearly lower burn in the sustain,
  // still alight at 3 s. (This fixture's dense readout clips the flame
  // band, so the centroid barely moves; the count is the signal here.)
  {
    auto full=bgStats(frame(Effect::Flames,700,cyan)),low=bgStats(frame(Effect::Flames,3000,cyan));
    assert(full.n>400 && low.n>60 && low.n<full.n/2);
  }
  // Sparkles: the burst is dense, and glitter/stars still appear later.
  {
    assert(bgStats(frame(Effect::Sparkles,150,cyan)).n>100);
    unsigned late=0;
    for(unsigned ms:{2200u,2600u,3000u,3400u,3800u})late+=bgStats(frame(Effect::Sparkles,ms,cyan)).n;
    assert(late>25);
  }
  // Shockwave: the first ring reaches far down the screen, and a fainter
  // echo ripple rolls out in the sustain.
  {
    auto ring=frame(Effect::Shockwave,350,cyan);
    unsigned far=0;
    for(int y=120;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(!base[i] && ring[i]!=base[i])++far;}
    assert(far>10);
    assert(bgStats(frame(Effect::Shockwave,1950,cyan)).n>30);
    // The sustain is clearly visible, not just present: at every 250 ms
    // sample from 1.0 s to 3.75 s the echo/pulse/dust put real light on
    // screen. The round 3 echoes failed this at most samples (light
    // 250-1400, bright pixels in single figures) and read as empty at 1x.
    for(unsigned ms=1000;ms<=3750;ms+=250){
      const Light s=bgLight(frame(Effect::Shockwave,ms,cyan));
      assert(s.n>=60 && s.light>=2200 && s.bright>=10);
    }
  }
  // Lightning: bolts land below the readout at the first strike, and a
  // distant strike flashes low in the sustain.
  {
    auto countBelow=[&](unsigned ms){
      auto f=frame(Effect::Lightning,ms,cyan);unsigned n=0;
      for(int y=60;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(!base[i] && f[i]!=base[i])++n;}
      return n;
    };
    assert(countBelow(30)>40 && countBelow(1720)>8);
  }
  // Slash: open mid-swing, the readout exactly restored long before the
  // settle, and the cooling seam still in the air beside it.
  {
    auto open=frame(Effect::Slash,600,cyan);
    unsigned changed=0;for(int y=13;y<=46;++y)for(int x=0;x<135;++x)changed+=open[y*135+x]!=base[y*135+x];
    assert(changed>300);
    auto closed=frame(Effect::Slash,1500,cyan);
    for(int i=0;i<135*240;++i)if(base[i])assert(closed[i]==base[i]);
    unsigned seam=0;
    auto late=frame(Effect::Slash,2200,cyan);
    for(int y=0;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(!base[i] && (x<8 || x>126) && late[i]!=base[i])++seam;}
    assert(seam>3);
    // The cooling seam is clearly visible to about 3.8 s, and no readout
    // (UI) pixel is touched from 1.5 s on. This fixture's readout is far
    // denser than real glyphs, so most of the seam hides in its halo and
    // only the ends and the embers count here; the round 3 afterimage
    // managed light 180-610 at these samples and read as over at 1.5 s.
    for(unsigned ms=1500;ms<=3750;ms+=250){
      auto f=frame(Effect::Slash,ms,cyan);
      for(int i=0;i<135*240;++i)if(base[i])assert(f[i]==base[i]);
      const Light s=bgLight(f);
      assert(s.n>=25 && s.light>=900 && s.bright>=6);
    }
  }

  std::cout<<"PASS: six classic effects at 5 s deterministic, halo-clipped, ink-exact from 3/4 duration, custom-background invariant, cost-model bounded at 8.0 ms, sustains alive, with per-effect identity properties\n";
}
