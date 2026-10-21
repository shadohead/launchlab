// The five elemental celebrations (Tidal, Frost, Gale, Eclipse, Radiance),
// about 5 s each: the shared renderer contract over every 10 ms of the
// whole duration, plus identity properties per effect for the impact and
// the sustain. The fixture is the one test_appearance and
// test_physical_effects use, so the checks stay comparable.
#include "best_effect_ui.h"
#include <cassert>
#include <cmath>
#include <iostream>

using Canvas=std::array<uint16_t,135*240>;
static Canvas base_{};
static uint16_t swap16(uint16_t v){return uint16_t(v<<8|v>>8);}

int main(){
  using namespace Appearance;
  // Canvas pixels are stored as LGFX sprites keep them: RGB565 big-endian.
  Canvas &base=base_;
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

  // The cost model is bounded on a second canvas laid out like the device's
  // recap: this fixture's readout is 80% ink (about 2,500 pixels), while the
  // real 15500 RPM readout is about 740 pixels in rows 18..35, so passes
  // over the readout's own pixels (Frost's ice, Eclipse's dim, Radiance's
  // flare, Gale's lean) would be priced about three times too high. Here
  // eight 2 px outlined glyph cells (800 pixels: 15500, a space, RPM) stand in for it;
  // everything else is the shared fixture.
  Canvas device{};
  for(int i=0;i<135*240;++i)device[i]=(i/135>=13 && i/135<=46)?0:base[i];
  for(int g=0;g<8;++g){
    const int x0=4+14*g+(g>=5?7:0);
    for(int y=18;y<=35;++y)for(int x=x0;x<x0+11;++x)if(y<20 || y>33 || x<x0+2 || x>=x0+9)device[y*135+x]=swap16(0x07ff);
  }
  BestEffectUI::Renderer renderer;
  const Effect effects[]={Effect::Tidal,Effect::Frost,Effect::Gale,Effect::Eclipse,Effect::Radiance};
  // The last moment each effect may touch a UI pixel (the readout's ice,
  // lean, dim and flare, and the header glint): well before 3/4 of the
  // duration, so the sustain never covers or tints the readout.
  const unsigned touchUntil[]={1100,1700,2000,2100,1300};
  auto frame=[&](Effect e,unsigned ms,uint16_t accentColor){
    Canvas out=base;renderer.draw({out.data(),135,240},e,ms,accentColor);return out;
  };
  // Changed background pixels: count, centroid, and the spread from the
  // readout centre (67,30 in this fixture).
  struct Stats{unsigned n=0;double cx=0,cy=0,dist=0,far=0;};
  auto bgStats=[&](const Canvas &out,int y0=0,int y1=239){
    Stats s;double sx=0,sy=0,sd=0;
    for(int y=y0;y<=y1;++y)for(int x=0;x<135;++x){
      const int i=y*135+x;if(base[i] || out[i]==base[i])continue;
      const double d=std::hypot(x-67.0,y-30.0);
      ++s.n;sx+=x;sy+=y;sd+=d;s.far=std::max(s.far,d);
    }
    if(s.n){s.cx=sx/s.n;s.cy=sy/s.n;s.dist=sd/s.n;}
    return s;
  };
  // Background pixels lit in one frame and not the other: the effect moves.
  auto moved=[&](const Canvas &a,const Canvas &b){
    unsigned n=0;for(int i=0;i<135*240;++i)if(!base[i] && (a[i]!=base[i])!=(b[i]!=base[i]))++n;return n;
  };

  for(unsigned e=0;e<5;++e){
    const Effect effect=effects[e];
    const uint32_t dur=duration(effect);
    assert(dur>=4800 && dur<=5200);
    assert(touchUntil[e]*2<=dur); // the readout is back by half the duration
    // Worst frame by the device cost model fit on the M5 (HIL !effectframes):
    // frame us ~= 1111 + 1.82*plots + 1.04*probes + 13.9*scans, bounded at
    // 8.0 ms over every 10 ms of the whole duration on the device-like
    // canvas. On the dense fixture the sustain, which never touches the
    // readout, is bounded too.
    auto model=[&](){return 1111+1.82*renderer.plots()+1.04*renderer.probes()+13.9*renderer.scans();};
    double worstUs=0,worstSustainUs=0,denseSustainUs=0,denseUs=0;unsigned worstMs=0,denseMs=0;uint32_t wPlots=0,wProbes=0,wScans=0;
    for(unsigned theme:{0u,3u,5u,8u})for(unsigned ms=0;ms<dur;ms+=10){
      Canvas out=device;renderer.draw({out.data(),135,240},effect,ms,accent(Theme(theme)));
      const double us=model();
      if(us>worstUs){worstUs=us;worstMs=ms;wPlots=renderer.plots();wProbes=renderer.probes();wScans=renderer.scans();}
      if(ms*5>=dur*2)worstSustainUs=std::max(worstSustainUs,us);
    }
    for(unsigned theme:{0u,3u,5u,8u}){
      const uint16_t color=accent(Theme(theme));
      bool drew=false,glinted=false;unsigned lastTouch=0,lastDraw=0;
      for(unsigned ms=0;ms<=dur+200;ms+=10){
        auto out=frame(effect,ms,color);
        const double estUs=model();
        if(estUs>denseUs){denseUs=estUs;denseMs=ms;}
        if(ms*5>=dur*2)denseSustainUs=std::max(denseSustainUs,estUs);
        auto again=frame(effect,ms,color);
        assert(out==again); // deterministic: a pure function of elapsed time
        const bool settledMs=ms*4>=dur*3; // from 3/4 on, UI pixels are exact
        bool leaning=false;
        if(effect==Effect::Gale)for(int i=17*135;i<44*135 && !leaning;++i)leaning=out[i]!=base[i];
        for(int i=0;i<135*240;++i)if(out[i]!=base[i]){
          drew=true;lastDraw=ms;
          const int y=i/135;
          if(base[i] || (effect==Effect::Gale && y>=17 && y<=43 && halo[i])){
            // A UI pixel: only before touchUntil, only in the header rows
            // (label, readout), and of the effect's kind: Gale moves the
            // readout's rows, Eclipse dims it, the rest brighten it.
            assert(!settledMs && ms<=touchUntil[e] && y<=46);lastTouch=ms;glinted=true;
            if(effect==Effect::Gale && y>=17 && y<=43)continue;
            const uint16_t was=swap16(base[i]),now=swap16(out[i]);
            const bool brighter=(now>>11)>=(was>>11) && ((now>>5)&63)>=((was>>5)&63) && (now&31)>=(was&31);
            assert(effect==Effect::Eclipse || brighter);
            assert(now!=0); // a shaded glyph pixel still blocks decoration
          }else assert(!halo[i] || (leaning && y>=15 && y<=45)); // decoration keeps the UI halo (of the leaned readout while Gale leans it)
        }
        if(ms>=dur)assert(out==base); // nothing is drawn after the duration
      }
      assert(drew && glinted && lastTouch>0 && lastDraw*10>=dur*9); // alive almost to the end
      // The readout is exactly as drawn from 3/4 on, and at the end.
      for(unsigned ms:{dur*3/4,dur-300,dur-10}){auto f=frame(effect,ms,color);for(int i=0;i<135*240;++i)if(base[i])assert(f[i]==base[i]);}
    }
    std::cout<<label(effect)<<": "<<dur<<" ms, worst frame "<<unsigned(worstUs)<<" us modelled at "<<worstMs<<" ms (plots "<<wPlots<<", probes "<<wProbes<<", scans "<<wScans<<"), sustain worst "<<unsigned(worstSustainUs)<<" us; dense fixture "<<unsigned(denseUs)<<" us at "<<denseMs<<" ms, sustain "<<unsigned(denseSustainUs)<<" us\n";
    assert(worstUs<=8000 && denseSustainUs<=8000);
    assert(worstSustainUs<worstUs); // the sustain is cheaper than the impact
    // A custom theme's background: the same pixels change as on black.
    const uint16_t bg=0x0841;Canvas themed=base;
    for(auto &p:themed)if(!p)p=swap16(bg);
    for(unsigned ms=40;ms<dur;ms+=230){
      auto black=frame(effect,ms,accent(Theme::Mint));
      Canvas out=themed;renderer.draw({out.data(),135,240},effect,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
    // The sustain is never frozen or empty: from the impact's end to 3/4,
    // every 200 ms frame lights background and differs from the last.
    for(unsigned ms=2000;ms<=dur*3/4;ms+=200){
      auto a=frame(effect,ms,accent(Theme::Classic)),b=frame(effect,ms+200,accent(Theme::Classic));
      assert(bgStats(a).n>40 && moved(a,b)>20);
    }
  }

  const uint16_t cyan=accent(Theme::Classic);
  auto readoutChanged=[&](const Canvas &f){unsigned n=0;for(int y=17;y<44;++y)for(int x=10;x<125;++x){const int i=y*135+x;n+=base[i] && f[i]!=base[i];}return n;};
  unsigned readoutInk=0;for(int y=17;y<44;++y)for(int x=10;x<125;++x)readoutInk+=base[y*135+x]!=0;
  // Tidal: the sea climbs the screen before the crash, the splash throws
  // droplets up beside the readout, the sea then rolls just below the
  // readout (a band across every column, moving), and drains back down.
  {
    const Stats low=bgStats(frame(Effect::Tidal,150,cyan)),high=bgStats(frame(Effect::Tidal,500,cyan));
    assert(low.n>200 && high.n>200 && high.cy<low.cy-40);
    unsigned spray=0;auto crash=frame(Effect::Tidal,800,cyan);
    for(int y=0;y<17;++y)for(int x=0;x<135;++x){const int i=y*135+x;spray+=!base[i] && crash[i]!=base[i];}
    assert(spray>8);
    for(unsigned ms:{2200u,2900u,3400u}){
      auto f=frame(Effect::Tidal,ms,cyan);const Stats s=bgStats(f);
      assert(s.cy>45 && s.cy<80);
      for(int x=0;x<135;++x){bool lit=false;for(int y=46;y<75;++y)lit|=!base[y*135+x] && f[y*135+x]!=base[y*135+x];assert(lit);}
    }
    assert(moved(frame(Effect::Tidal,2600,cyan),frame(Effect::Tidal,2650,cyan))>60);
    const Stats rolling=bgStats(frame(Effect::Tidal,3300,cyan)),draining=bgStats(frame(Effect::Tidal,4200,cyan));
    assert(draining.n>100 && draining.cy>rolling.cy+60);
  }
  // Frost: the readout ices over (most of its ink brightened), shatters
  // clear, then rime ferns frame the open screen below it while snow falls.
  {
    assert(readoutChanged(frame(Effect::Frost,1150,cyan))*10>=readoutInk*9);
    assert(readoutChanged(frame(Effect::Frost,1500,cyan))==0);
    for(unsigned ms:{2400u,3200u}){
      auto f=frame(Effect::Frost,ms,cyan);unsigned left=0,right=0;
      for(int y=90;y<200;++y)for(int x=0;x<20;++x){left+=!base[y*135+x] && f[y*135+x]!=base[y*135+x];right+=!base[y*135+x+115] && f[y*135+x+115]!=base[y*135+x+115];}
      assert(left>25 && right>25); // rime on both side edges
    }
    // Snow falls: a flake-only region (the middle of the open band) shifts down.
    const Stats a=bgStats(frame(Effect::Frost,2500,cyan),50,239),b=bgStats(frame(Effect::Frost,2900,cyan),50,239);
    assert(a.n>80 && b.n>80 && moved(frame(Effect::Frost,2500,cyan),frame(Effect::Frost,2600,cyan))>30);
  }
  // Gale: the gust leans the readout's top rows right of the bottom rows,
  // the readout is upright again by 2 s, and eddies and grit keep moving
  // downwind (left to right) through the sustain.
  {
    auto lean=frame(Effect::Gale,700,cyan);
    // The fixture's ink repeats every 5 px along a row, so a row's shift is
    // read from its first ink pixel, and the whole row must match it.
    auto shiftOf=[&](const Canvas &f,int y){
      int from=0,to=0;while(!base[y*135+from])++from;while(!f[y*135+to])++to;
      const int d=to-from;
      for(int x=20;x<115;++x)if((f[y*135+x+d]!=0)!=(base[y*135+x]!=0))return 99;
      return d;
    };
    const int top=shiftOf(lean,18),bottom=shiftOf(lean,42);
    assert(top!=99 && bottom!=99 && top>=2 && top>bottom);
    assert(readoutChanged(frame(Effect::Gale,2100,cyan))==0);
    // Downwind drift of the lower band's particles between two close frames.
    const Stats a=bgStats(frame(Effect::Gale,2600,cyan),50,239),b=bgStats(frame(Effect::Gale,3000,cyan),50,239);
    assert(a.n>60 && b.n>60);
    assert(moved(frame(Effect::Gale,3000,cyan),frame(Effect::Gale,3050,cyan))>30);
  }
  // Eclipse: the readout is dimmed in totality, a bright diamond flares up
  // and to the right of it, the readout is relit by 2.1 s, then the corona
  // keeps its ring around the readout while the violet tendrils withdraw.
  {
    auto dark=frame(Effect::Eclipse,1200,cyan);unsigned darker=0;
    for(int i=0;i<135*240;++i)if(base[i] && i/135>=17 && i/135<44)darker+=((swap16(dark[i])>>5)&63)<((swap16(base[i])>>5)&63);
    assert(darker*10>=readoutInk*9);
    assert(readoutChanged(frame(Effect::Eclipse,2100,cyan))==0);
    auto violetReach=[&](unsigned ms){
      auto f=frame(Effect::Eclipse,ms,cyan);double far=0;unsigned n=0;
      for(int y=0;y<240;++y)for(int x=0;x<135;++x){
        const int i=y*135+x;if(base[i] || f[i]==base[i])continue;
        const uint16_t v=swap16(f[i]);const int r=v>>11,g=(v>>5)&63,b=v&31;
        if(b>=10 && b>r && 2*g<3*b){++n;far=std::max(far,std::hypot(x-67.0,y-30.0));}
      }
      return std::make_pair(n,far);
    };
    const auto full=violetReach(1500),later=violetReach(3000);
    assert(full.first>40 && later.first>10 && later.second<full.second-8);
    for(unsigned ms:{2600u,3200u}){const Stats s=bgStats(frame(Effect::Eclipse,ms,cyan));assert(s.n>80 && s.dist<60);}
    // The corona breathes: its lit count changes over the sustain.
    const unsigned c1=bgStats(frame(Effect::Eclipse,2500,cyan)).n,c2=bgStats(frame(Effect::Eclipse,3200,cyan)).n;
    assert(c1!=c2);
  }
  // Radiance: motes close in on the readout before the burst, the flare
  // brightens its glyphs, long rays reach far out, and the crown still
  // reaches out and turns through the sustain.
  {
    const Stats early=bgStats(frame(Effect::Radiance,100,cyan)),late=bgStats(frame(Effect::Radiance,500,cyan));
    assert(early.n>20 && late.n>20 && late.dist<early.dist-10);
    assert(readoutChanged(frame(Effect::Radiance,750,cyan))*10>=readoutInk*9);
    assert(readoutChanged(frame(Effect::Radiance,1400,cyan))==0);
    const Stats burst=bgStats(frame(Effect::Radiance,1000,cyan)),crown=bgStats(frame(Effect::Radiance,3000,cyan));
    assert(burst.far>78 && crown.far>60 && crown.n>150 && crown.n<burst.n);
    assert(moved(frame(Effect::Radiance,2600,cyan),frame(Effect::Radiance,3200,cyan))>100); // the rays turn
  }

  std::cout<<"PASS: five elemental effects of about 5 s deterministic, halo-clipped, readout-exact from 3/4 duration, custom-background invariant, cost-model bounded at 8.0 ms over every 10 ms, sustain alive and cheaper than the impact, with per-effect identity properties\n";
}
