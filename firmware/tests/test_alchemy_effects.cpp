// The alchemy set of 0.18 (Transmute..Fracture): the shared renderer contract,
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
  const Effect effects[]={Effect::Transmute,Effect::Ignition,Effect::Spires,Effect::Gate,Effect::Fracture};
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
  const unsigned windows[5][4]={{1000,1700,1800,3700},{550,1500,1800,3700},{450,1500,1800,3700},{950,1900,1950,3500},{700,1300,1400,3400}};
  const bool report=std::getenv("ALCHEMY_COSTS")!=nullptr;
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
  // Transmute: a circle concentric with the replay (68 px below the
  // readout's foot) is traced stroke by stroke, then stands closed: early on
  // its outer ring lights only some of 24 sectors, in the sustain nearly all
  // of them (the fixture's replay lines and face hide about a third), and most of the
  // decoration lies within the rings.
  {
    auto sectors=[&](const Frame &f,unsigned &onRing,unsigned &all){
      unsigned mask=0;onRing=all=0;
      for(int y=0;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || f[i]==base[i])continue;
        ++all;const double dx=x-67,dy=y-111,d=std::sqrt(dx*dx+dy*dy);
        if(d<=38)++onRing;
        if(std::fabs(d-36)<=1.2)mask|=1u<<int((std::atan2(dy,dx)+M_PI)/(2*M_PI)*24)%24;}
      unsigned n=0;for(int s=0;s<24;++s)n+=(mask>>s)&1;return n;
    };
    unsigned inA,allA,inB,allB;
    const unsigned early=sectors(frame(Effect::Transmute,200,cyan),inA,allA),closed=sectors(frame(Effect::Transmute,2500,cyan),inB,allB);
    if(report)std::printf("transmute sectors %u -> %u, inside %u/%u\n",early,closed,inB,allB);
    assert(early>=2 && closed>=12 && closed>=early*2+3 && inB*10>=allB*8);
  }
  // Ignition: the spark starts low on the left and runs up toward the
  // readout; then a ball of fire (red well above blue) burns centred under
  // the readout.
  {
    const Stats run=bgStats(frame(Effect::Ignition,350,cyan));
    const Frame f=frame(Effect::Ignition,1000,cyan);
    unsigned warm=0;double wx=0,wy=0;
    for(int i=0;i<135*240;++i)if(!base[i] && f[i]!=base[i] && red(f[i])>12 && red(f[i])>=2*blue(f[i])){++warm;wx+=i%135;wy+=i/135;}
    if(warm){wx/=warm;wy/=warm;}
    if(report)std::printf("ignition run %.0f,%.0f (%u px) ball %u at %.0f,%.0f\n",run.cx,run.cy,run.n,warm,wx,wy);
    assert(run.n>5 && run.cx<50 && run.cy>95 && warm>250 && std::fabs(wx-67)<8 && wy<85);
  }
  // Spires: a row of spikes stands on a floor across the screen's width
  // below the replay: the lit band's top edge has many separate peaks (the
  // tips), and nothing is drawn above the replay's middle or below the floor.
  {
    const Frame f=frame(Effect::Spires,2500,cyan);const Stats band=bgStats(f);
    int top[135];for(int x=0;x<135;++x){top[x]=240;for(int y=95;y<170;++y){const int i=y*135+x;if(!base[i] && f[i]!=base[i]){top[x]=y;break;}}}
    unsigned peaks=0;int last=-10;
    for(int x=3;x<132;++x){if(top[x]>=200)continue;bool peak=true;
      for(int d=-3;d<=3;++d)if(d && top[x+d]<top[x])peak=false;
      if(peak && top[x]+5<std::min(top[x-3],top[x+3])+1 && x-last>5){++peaks;last=x;}}
    if(report)std::printf("spires peaks %u band y %d..%d x %d..%d\n",peaks,band.y0,band.y1,band.x0,band.x1);
    assert(peaks>=5 && band.y0>100 && band.y1<=165 && band.x1-band.x0>115);
  }
  // Gate: once open, the two doors stand at both screen edges (their hinge
  // edges lit down the side strips), a long eye lies just beneath the
  // readout, and before it opens little is there (beside the seam).
  {
    const Frame open=frame(Effect::Gate,2500,cyan);
    unsigned left=0,right=0,eye=0,early=0;
    for(int y=50;y<100;++y){left+=open[y*135+3]!=base[y*135+3];right+=open[y*135+124]!=base[y*135+124];}
    for(int y=48;y<61;++y)for(int x=45;x<90;++x){const int i=y*135+x;if(std::abs(x-67)>2)eye+=!base[i] && open[i]!=base[i];}
    const Frame shut=frame(Effect::Gate,900,cyan);for(int y=48;y<61;++y)for(int x=45;x<90;++x){const int i=y*135+x;if(std::abs(x-67)>2)early+=!base[i] && shut[i]!=base[i];}
    if(report)std::printf("gate posts %u %u eye %u (before %u)\n",left,right,eye,early);
    assert(left>=40 && right>=40 && eye>=40 && early<eye/4);
  }
  // Fracture: red cracks run out of a point left of the readout's centre
  // (the palm, 16 px left, 36 below its foot) all round: the warm pixels
  // reach out on every side of it and across most of the screen's width,
  // and the web is gone again before the end.
  {
    auto warm=[&](const Frame &f,unsigned quad[4],int &x0,int &x1){
      unsigned n=0;x0=135;x1=-1;
      for(int y=45;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;
        if(base[i] || f[i]==base[i] || red(f[i])<10 || red(f[i])<blue(f[i])+8)continue;
        ++n;x0=std::min(x0,x);x1=std::max(x1,x);quad[(x>51)+2*(y>83)]++;}
      return n;
    };
    unsigned q[4]={},qe[4]={};int x0,x1,e0,e1;
    const unsigned web=warm(frame(Effect::Fracture,2000,cyan),q,x0,x1),gone=warm(frame(Effect::Fracture,4850,cyan),qe,e0,e1);
    if(report)std::printf("fracture web %u (%u %u %u %u) x %d..%d, end %u\n",web,q[0],q[1],q[2],q[3],x0,x1,gone);
    assert(web>250 && q[0]>15 && q[1]>15 && q[2]>15 && q[3]>15 && x1-x0>100 && gone*10<web);
  }
  std::cout<<"PASS: five alchemy effects deterministic, halo-clipped, ink-exact by 3/4, custom-background invariant, within the 9 ms cost model, with per-effect identity properties\n";
}
