// The five physical celebrations (Spin, Storm, Quickdraw, Leaves, Bloom):
// the shared renderer contract plus one identity property per effect.
// The generic every-theme/every-frame sweep also runs in test_appearance;
// here the fixture is the same, so the checks stay comparable.
#include "best_effect_ui.h"
#include <cassert>
#include <cmath>
#include <iostream>

static std::array<uint16_t,135*240> base_{};
static uint16_t swap16(uint16_t v){return uint16_t(v<<8|v>>8);}

int main(){
  using namespace Appearance;
  // Canvas pixels are stored as LGFX sprites keep them: RGB565 big-endian.
  std::array<uint16_t,135*240> &base=base_;
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
  const Effect effects[]={Effect::Spin,Effect::Storm,Effect::Quickdraw,Effect::Leaves,Effect::Bloom};
  // Per-effect pixel-write bounds, with headroom over the peaks measured on
  // this fixture; every bound stays far under the shared 12000 limit.
  const uint32_t plotBound[]={2800,3800,1400,3800,3000};
  auto frame=[&](Effect e,unsigned ms,uint16_t accentColor){
    auto out=base;renderer.draw({out.data(),135,240},e,ms,accentColor);return out;
  };
  // Changed background pixels only: count, centroid and mean distance to the
  // readout centre (67,30 in this fixture).
  struct Stats{unsigned n=0;double cx=0,cy=0,dist=0;};
  auto bgStats=[&](const std::array<uint16_t,135*240> &out){
    Stats s;double sx=0,sy=0,sd=0;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x){
      const int i=y*135+x;
      if(base[i] || out[i]==base[i])continue;
      ++s.n;sx+=x;sy+=y;sd+=std::hypot(x-67.0,y-30.0);
    }
    if(s.n){s.cx=sx/s.n;s.cy=sy/s.n;s.dist=sd/s.n;}
    return s;
  };

  for(unsigned e=0;e<5;++e){
    const Effect effect=effects[e];
    const uint32_t dur=duration(effect);
    assert(dur>=4800 && dur<=5200);
    // Sounds stay a hit and a short tail under the 5 s effect.
    assert(soundDuration(effect)>=1000 && soundDuration(effect)<=2800 && soundDuration(effect)<=dur);
    // Worst frame by the device cost model fit on the M5 (NUMBERS.md):
    // frame us ~= 1111 + 1.82*plots + 1.04*probes + 13.9*scans, bounded at
    // 8.0 ms, inside the 8.5 ms device target with margin for float math.
    double worstUs=0;unsigned worstMs=0;uint32_t wPlots=0,wProbes=0,wScans=0;
    for(unsigned theme:{0u,3u,5u,8u}){
      const uint16_t color=accent(Theme(theme));
      uint32_t maxPlots=0;bool drew=false,glinted=false;
      for(unsigned ms=0;ms<=5300;ms+=10){
        auto out=frame(effect,ms,color);
        auto again=frame(effect,ms,color);
        assert(out==again); // deterministic: a pure function of elapsed time
        maxPlots=std::max(maxPlots,renderer.plots());
        const double estUs=1111+1.82*renderer.plots()+1.04*renderer.probes()+13.9*renderer.scans();
        if(estUs>worstUs){worstUs=estUs;worstMs=ms;wPlots=renderer.plots();wProbes=renderer.probes();wScans=renderer.scans();}
        const bool settledMs=ms*4>=dur*3; // from 3/4 on, UI pixels are exact
        for(int i=0;i<135*240;++i)if(out[i]!=base[i]){
          drew=true;
          if(base[i]){
            // No physical effect moves ink: a changed UI pixel is the
            // glint, brighter in every channel and inside the header rows.
            const uint16_t was=swap16(base[i]),now=swap16(out[i]);
            assert(!settledMs && i/135<=46 && (now>>11)>=(was>>11) && ((now>>5)&63)>=((was>>5)&63) && (now&31)>=(was&31));glinted=true;
          }else assert(!halo[i]); // decoration keeps the UI halo
        }
        if(ms>=dur)assert(out==base); // nothing is drawn after the duration
      }
      assert(drew && glinted && maxPlots<=plotBound[e]); // like the other non-Slash effects, each glints
      // The readout is exactly as drawn again well before the end.
      auto settled=frame(effect,dur-100,color);
      for(int i=0;i<135*240;++i)if(base[i])assert(settled[i]==base[i]);
    }
    std::cout<<label(effect)<<": worst frame "<<unsigned(worstUs)<<" us modelled at "<<worstMs<<" ms (plots "<<wPlots<<", probes "<<wProbes<<", scans "<<wScans<<")\n";
    assert(worstUs<=8000);
    // The sustain lives: decoration is present and moving well past the
    // old 2.5-2.8 s endings, and never frozen between samples.
    {
      const uint16_t cyan0=accent(Theme::Classic);
      unsigned sustainPx=0,alive=0;
      for(unsigned ms:{2600u,3000u,3400u,3800u}){const unsigned n=bgStats(frame(effect,ms,cyan0)).n;sustainPx+=n;alive+=n>0;}
      assert(sustainPx>40 && alive>=3);
      assert(frame(effect,3000,cyan0)!=frame(effect,3300,cyan0));
    }
    // A custom theme's background: the same pixels change as on black.
    const uint16_t bg=0x0841;std::array<uint16_t,135*240> themed=base;
    for(auto &p:themed)if(!p)p=swap16(bg);
    for(unsigned ms:{40u,300u,700u,1000u,1400u,2400u,3300u,4400u}){
      auto black=frame(effect,ms,accent(Theme::Mint));
      auto out=themed;renderer.draw({out.data(),135,240},effect,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
  }

  const uint16_t cyan=accent(Theme::Classic);
  // Spin: the ripcord crosses above the top, the top's body sits in the band
  // below the readout (not ringed around it), its blades move between
  // frames, and the lock flash reaches the disc's left and right ends.
  {
    auto cord=frame(Effect::Spin,400,cyan);
    unsigned cordPx=0;
    for(int y=55;y<=61;++y)for(int x=70;x<135;++x){const int i=y*135+x;if(!base[i] && cord[i]!=base[i])++cordPx;}
    assert(cordPx>12); // the yanked ripcord, right of the stem at disc height
    auto a=frame(Effect::Spin,700,cyan),b=frame(Effect::Spin,1150,cyan);
    unsigned moved=0,band=0,around=0;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x){
      const int i=y*135+x;if(base[i])continue;
      if(y>=52 && y<=108 && b[i]!=base[i])++band;   // the top below the readout
      if(y<50 && b[i]!=base[i])++around;           // nothing ringed around it
      if((a[i]!=base[i])!=(b[i]!=base[i]))++moved;
    }
    assert(band>120 && around<band/4 && moved>80); // a turning top, not a static ring
    auto lock=frame(Effect::Spin,1400,cyan);
    unsigned ends=0;
    for(int y=62;y<=86;++y)for(int x=0;x<135;++x)if((x<38 || x>97) && !base[y*135+x] && lock[y*135+x]!=base[y*135+x])++ends;
    assert(ends>6); // the lock flash reaches the disc's left/right tips
  }
  // Storm: cuts accumulate through the stillness, then the detonation frame
  // lights far more background than the held hairlines did.
  {
    const unsigned few=bgStats(frame(Effect::Storm,700,cyan)).n;
    const unsigned held=bgStats(frame(Effect::Storm,1600,cyan)).n;
    const unsigned boom=bgStats(frame(Effect::Storm,1790,cyan)).n;
    assert(few>15 && held>few*2 && boom>held);
  }
  // Quickdraw: the reticle hugs the readout's rim before any shot, the
  // first shot lights its muzzle corner low on the screen, and its bullet
  // hole is still marked beside the readout long after the shot.
  {
    auto aim=frame(Effect::Quickdraw,500,cyan);
    unsigned rim=0;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x){
      const int i=y*135+x;if(base[i] || aim[i]==base[i])continue;
      if((y>=8 && y<=14) || (y>=46 && y<=52))++rim; // just above/below the readout
    }
    assert(rim>25);
    auto before=bgStats(frame(Effect::Quickdraw,700,cyan));
    auto shot=frame(Effect::Quickdraw,780,cyan);
    unsigned corner=0;
    for(int y=115;y<160;++y)for(int x=0;x<30;++x){const int i=y*135+x;if(!base[i] && shot[i]!=base[i])++corner;}
    assert(before.n>0 && corner>12); // muzzle flash at the lower-left corner
    auto holeAt=[&](unsigned ms){
      auto f=frame(Effect::Quickdraw,ms,cyan);unsigned n=0;
      for(int y=4;y<=14;++y)for(int x=24;x<=34;++x){const int i=y*135+x;if(!base[i] && f[i]!=base[i])++n;}
      return n; // around the first impact point (29,9 in this fixture)
    };
    assert(holeAt(1300)>=holeAt(700)+3); // the smoking bullet hole persists
  }
  // Leaves: leaf-coloured pixels fall, then spread wider once sliced.
  {
    auto leafish=[&](const std::array<uint16_t,135*240> &out){
      Stats s;double sx=0,sy=0;int x0=135,x1=-1;
      for(int y=0;y<240;++y)for(int x=0;x<135;++x){
        const int i=y*135+x;if(base[i] || out[i]==base[i])continue;
        const uint16_t v=swap16(out[i]);const int r=v>>11,g=(v>>5)&63,b=v&31;
        if(g<15 || 3*b>=2*g || r>g+12)continue; // greens and ambers, not cyan
        ++s.n;sx+=x;sy+=y;x0=std::min(x0,x);x1=std::max(x1,x);
      }
      if(s.n){s.cx=sx/s.n;s.cy=sy/s.n;s.dist=x1-x0;}
      return s;
    };
    // Early in the fall the leaves are behind this fixture's dense readout
    // ink, so the fall is sampled at 0.25 s (above it) and 1.05 s (below it).
    auto high=leafish(frame(Effect::Leaves,250,cyan));
    auto falling=leafish(frame(Effect::Leaves,1050,cyan));
    auto sliced=leafish(frame(Effect::Leaves,1800,cyan));
    assert(high.n>15 && falling.n>25 && sliced.n>20);
    assert(falling.cy>high.cy+8);            // the leaves fall
    assert(sliced.cy>falling.cy && sliced.n>=falling.n/2); // halves keep falling
    assert(sliced.dist>falling.dist+5);      // and spread wider once sliced (dist holds the width here)
  }
  // Bloom: buds are small, full bloom is much larger, and after the burst
  // the petal cloud is taller and lower than the flowers were: the petals
  // have spread outward and fallen. (Mean distance to the readout centre
  // cannot show this; the flowers already frame it from below.)
  {
    auto height=[&](const std::array<uint16_t,135*240> &out){
      int y0=240,y1=-1;
      for(int y=0;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(!base[i] && out[i]!=base[i]){y0=std::min(y0,y);y1=std::max(y1,y);}}
      return y1-y0;
    };
    auto buds=frame(Effect::Bloom,250,cyan),full=frame(Effect::Bloom,1500,cyan),burst=frame(Effect::Bloom,2150,cyan);
    auto budsS=bgStats(buds),fullS=bgStats(full),burstS=bgStats(burst);
    assert(budsS.n>5 && fullS.n>budsS.n*3 && burstS.n>40);
    assert(height(full)>height(buds)+10);          // buds open into flowers
    assert(height(burst)>height(full)+5);          // the burst spreads the petals
    assert(burstS.cy>fullS.cy+2);                  // and they fall
  }

  std::cout<<"PASS: five physical effects at 5 s deterministic, halo-clipped, ink-exact from 3/4 duration, custom-background invariant, cost-model bounded at 8.0 ms, sustains alive, with per-effect identity properties\n";
}
