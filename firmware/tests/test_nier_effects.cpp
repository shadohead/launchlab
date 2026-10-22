// The five NieR:Automata-inspired celebrations (Breach..Glitch): the shared
// renderer contract, the device cost model's bound on every frame, and one or
// more identity properties per effect. The fixture is test_physical_effects'.
#include "best_effect_ui.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <algorithm>
#include <array>

static std::array<uint16_t,135*240> base_{};
static uint16_t swap16(uint16_t v){return uint16_t(v<<8|v>>8);}
using Frame=std::array<uint16_t,135*240>;
struct Rgb565{int r,g,b;};
static Rgb565 split(uint16_t stored){const uint16_t v=swap16(stored);return {v>>11,(v>>5)&63,v&31};}

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
  // The readout's ink box in this fixture.
  constexpr int BX0=10,BX1=124,BY0=17,BY1=43;

  BestEffectUI::Renderer renderer;
  const Effect effects[]={Effect::Breach,Effect::Bladework,Effect::Moonpetal,Effect::Barrage,Effect::Glitch};
  const char *names[]={"Breach","Bladework","Moonpetal","Barrage","Glitch"};
  auto frame=[&](Effect e,unsigned ms,uint16_t accentColor){
    Frame out=base;renderer.draw({out.data(),135,240},e,ms,accentColor);return out;
  };
  auto model=[&]{return renderer.modelledUs();};

  for(unsigned e=0;e<5;++e){
    const Effect effect=effects[e];
    const bool tears=effect==Effect::Glitch; // rotates readout rows inside its box
    const uint32_t dur=duration(effect);
    assert(dur>=4800 && dur<=5200);
    for(unsigned theme:{0u,3u,5u,8u}){
      const uint16_t color=accent(Theme(theme));
      bool drew=false,glinted=false,tore=false;uint32_t maxPlots=0;
      for(unsigned ms=0;ms<=5600;ms+=10){
        auto out=frame(effect,ms,color);
        auto again=frame(effect,ms,color);
        assert(out==again); // deterministic: a pure function of elapsed time
        maxPlots=std::max(maxPlots,renderer.plots());
        const bool settled=ms*4>=dur*3;
        for(int i=0;i<135*240;++i)if(out[i]!=base[i]){
          drew=true;const int x=i%135,y=i/135;
          if(tears && y>=BY0 && y<=BY1 && x>=BX0 && x<=BX1){
            // Torn readout rows: only inside the box, and never once settled.
            assert(!settled);tore=true;continue;
          }
          if(base[i]){
            // Otherwise a changed UI pixel is the glint: brighter in every
            // channel, in the header rows, before the settle.
            const Rgb565 was=split(base[i]),now=split(out[i]);
            assert(y<=46 && !settled && now.r>=was.r && now.g>=was.g && now.b>=was.b);glinted=true;
          }else assert(!halo[i]); // decoration keeps the UI halo
        }
        if(ms>=dur)assert(out==base); // nothing is drawn after the duration
        if(settled)for(int i=0;i<135*240;++i)if(base[i])assert(out[i]==base[i]);
      }
      assert(drew && glinted && tore==tears && maxPlots<=12000);
    }
    // A custom theme's background: the same pixels change as on black.
    const uint16_t bg=0x0841;Frame themed=base;
    for(auto &p:themed)if(!p)p=swap16(bg);
    for(unsigned ms:{40u,300u,700u,800u,1000u,1350u,1500u,1700u,2100u,2700u,3300u,3900u,4300u}){
      auto black=frame(effect,ms,accent(Theme::Mint));
      auto out=themed;renderer.draw({out.data(),135,240},effect,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
    // The device cost model on every frame (theme 0): at most 8 ms.
    double worst=0;unsigned at=0,plots=0,probes=0,scans=0;
    for(unsigned ms=0;ms<dur;ms+=10){
      frame(effect,ms,accent(Theme::Classic));const double us=model();
      if(us>worst){worst=us;at=ms;plots=renderer.plots();probes=renderer.probes();scans=renderer.scans();}
    }
    std::printf("%s worst frame %.0f us at %u ms (plots %u, probes %u, scans %u)\n",names[e],worst,at,plots,probes,scans);
    assert(worst<=8000);
    // The impact lands early and the sustain (2.2-3.9 s) costs less than it.
    double impact=0,sustain=0;
    for(unsigned ms=300;ms<2000;ms+=10){frame(effect,ms,accent(Theme::Classic));impact=std::max(impact,model());}
    for(unsigned ms=2200;ms<3900;ms+=10){frame(effect,ms,accent(Theme::Classic));sustain=std::max(sustain,model());}
    assert(sustain<impact);
    // The sustain keeps living: never empty, never frozen between frames.
    for(unsigned ms=2000;ms<3900;ms+=50){
      auto a=frame(effect,ms,accent(Theme::Classic)),b=frame(effect,ms+50,accent(Theme::Classic));
      assert(a!=base && a!=b);
    }
  }

  const uint16_t cyan=accent(Theme::Classic);
  auto lit=[&](const Frame &out,int x,int y){const int i=y*135+x;return !base[i] && out[i]!=base[i];};
  auto isOrange=[&](const Frame &out,int i){const Rgb565 c=split(out[i]);return c.r>=20 && c.g>=12 && c.g<=46 && c.b<=10;};
  auto isWhite=[&](const Frame &out,int i){const Rgb565 c=split(out[i]);return c.r>=26 && c.g>=54 && c.b>=26;};
  // Breach: a scanline sweeps the arena in, orange square shots climb from the
  // ship toward the core, and the break bursts wider than the arena's shots.
  {
    auto scan=frame(Effect::Breach,150,cyan);unsigned fullRow=0;
    for(int y=45;y<110;++y){unsigned n=0;for(int x=0;x<135;++x)n+=lit(scan,x,y);fullRow=std::max(fullRow,n);}
    assert(fullRow>=100);
    unsigned orange=0;int oy0=240,oy1=0;
    for(unsigned ms:{700u,850u,1000u,1150u}){
      auto f=frame(Effect::Breach,ms,cyan);
      for(int y=44;y<106;++y)for(int x=0;x<135;++x)if(lit(f,x,y) && isOrange(f,y*135+x)){++orange;oy0=std::min(oy0,y);oy1=std::max(oy1,y);}
    }
    assert(orange>=30 && oy1-oy0>=25); // shots in flight up the arena
    // Bright light level with the core (y1+13 = 56 here), not the dim arena.
    auto spread=[&](unsigned ms){auto f=frame(Effect::Breach,ms,cyan);int x0=135,x1=-1;
      for(int y=54;y<=58;++y)for(int x=0;x<135;++x)if(lit(f,x,y)){const Rgb565 c=split(f[y*135+x]);if(c.r+c.b>=24){x0=std::min(x0,x);x1=std::max(x1,x);}}return x1-x0;};
    assert(spread(1500)>spread(1250)+30); // the "complete" diamond spreads out
  }
  // Bladework: five slashes land in different places, each with a sepia
  // afterimage; the finisher holds bright over a broad band below the readout.
  {
    const unsigned at[]={340,470,600,730,860};double cx[5],cy[5];
    for(int s=0;s<5;++s){
      auto f=frame(Effect::Bladework,at[s],cyan);double sx=0,sy=0;unsigned n=0,sepia=0;
      for(int i=0;i<135*240;++i){if(base[i] || f[i]==base[i])continue;
        const Rgb565 c=split(f[i]);if(c.r>=20 && c.g>=40 && c.b>=20){sx+=i%135;sy+=i/135;++n;}
        if(c.r>=c.b+3 && c.g>=8)++sepia;}
      assert(n>=12 && sepia>=6);cx[s]=sx/n;cy[s]=sy/n;
    }
    for(int a=0;a<5;++a)for(int b=a+1;b<5;++b)assert(std::hypot(cx[a]-cx[b],cy[a]-cy[b])>6);
    auto hold=frame(Effect::Bladework,1200,cyan);unsigned n=0;int x0=135,x1=-1;
    for(int y=44;y<110;++y)for(int x=0;x<135;++x)if(lit(hold,x,y)){++n;x0=std::min(x0,x);x1=std::max(x1,x);}
    assert(n>=120 && x1-x0>=90);
    // The sustain keeps the finisher's X as an afterimage below the readout
    // and stays busy with motes, echoes and glints until its settle.
    for(unsigned ms=1800;ms<3900;ms+=50){
      auto f=frame(Effect::Bladework,ms,cyan);unsigned count=0,sepia=0;
      for(int y=44;y<130;++y)for(int x=0;x<135;++x)if(lit(f,x,y)){++count;const Rgb565 c=split(f[y*135+x]);sepia+=c.r>=c.b+2;}
      assert(count>=80 && sepia>=30);
    }
  }
  // Moonpetal: one blue-white flower centred under the readout, small as a bud,
  // much larger at full bloom, held through the sustain, fading in its settle.
  {
    auto measure=[&](unsigned ms,double &cx,int &width,unsigned &blue){
      auto f=frame(Effect::Moonpetal,ms,cyan);double sx=0;unsigned n=0;int x0=135,x1=-1;blue=0;
      for(int i=0;i<135*240;++i){if(base[i] || f[i]==base[i])continue;const Rgb565 c=split(f[i]);
        sx+=i%135;++n;x0=std::min(x0,i%135);x1=std::max(x1,i%135);if(c.b*2>=c.r*2+2 && c.g>=c.r*2)++blue;}
      cx=n?sx/n:0;width=x1-x0;return n;
    };
    double cBud,cFull,cLate;int wBud,wFull,wLate;unsigned bBud,bFull,bLate;
    const unsigned bud=measure(550,cBud,wBud,bBud),full=measure(1600,cFull,wFull,bFull),late=measure(4800,cLate,wLate,bLate);
    assert(bud>=20 && full>=bud*3 && wFull>=wBud+20 && std::fabs(cFull-67)<6);
    assert(bFull*2>=full); // pale blue, not pink
    // The settle is dimmer: summed light falls well below full bloom.
    auto energy=[&](unsigned ms){auto f=frame(Effect::Moonpetal,ms,cyan);long s=0;for(int i=0;i<135*240;++i)if(!base[i]){const Rgb565 c=split(f[i]);s+=c.r+c.g+c.b;}return s;};
    // Held open through the sustain, far dimmer late in the settle.
    assert(energy(4800)*3<energy(1600) && energy(3000)*2>energy(1600));(void)late;(void)cLate;(void)wLate;(void)bLate;
  }
  // Barrage: a stream, not a few shots: many separate tracers in flight at
  // once; the orange orbs thin out; then one long beam.
  {
    auto stream=frame(Effect::Barrage,950,cyan);
    unsigned runs=0;
    for(int y=44;y<100;++y){bool in=false;for(int x=0;x<135;++x){const bool l=lit(stream,x,y);if(l && !in)++runs;in=l;}}
    assert(runs>=14);
    // Orange light away from the big orb (centre 39,73 in this fixture).
    auto orangeCount=[&](unsigned ms){auto f=frame(Effect::Barrage,ms,cyan);unsigned n=0;
      for(int i=0;i<135*240;++i)if(!base[i] && f[i]!=base[i] && std::hypot(i%135-39.0,i/135-73.0)>10){const Rgb565 c=split(f[i]);n+=c.r>=12 && c.b*3<=c.r && c.g<=c.r*2+4;}return n;};
    assert(orangeCount(900)>orangeCount(1500)+20);
    // The beam: a long white line from the drone to the big orb.
    auto beam=frame(Effect::Barrage,1700,cyan);unsigned white=0;int x0=135,x1=-1;
    for(int y=44;y<90;++y)for(int x=0;x<135;++x)if(lit(beam,x,y) && isWhite(beam,y*135+x)){++white;x0=std::min(x0,x);x1=std::max(x1,x);}
    assert(white>=40 && x1-x0>=50);
  }
  // Glitch: menu rules frame the readout before the glitch; during it thin
  // bands of readout rows jog a little, readable throughout; it snaps back exactly.
  {
    auto rules=frame(Effect::Glitch,450,cyan);unsigned top=0,bottom=0;
    for(int x=0;x<135;++x){top+=lit(rules,x,BY0-4);bottom+=lit(rules,x,BY1+5);}
    assert(top>=60 && bottom>=110);
    // The RPM stays readable on every frame of the glitch: each readout row's
    // ink is its own row rotated by at most 3 px inside the box (colour
    // aside), at most four rows move at once, and some glyph edges carry the
    // fringe tint (same shape, new colour). This fixture's ink repeats every
    // 5 px, so a shift by a multiple of 5 would hide; none is that large.
    unsigned moved=0,tinted=0;
    for(unsigned ms=600;ms<=1600;ms+=10){
      auto glitch=frame(Effect::Glitch,ms,cyan);unsigned rows=0;
      for(int y=BY0;y<=BY1;++y){
        auto inkAt=[&](const Frame &f,int x){return f[y*135+x]!=0;};
        int shift=99;
        for(int s=-3;s<=3 && shift==99;++s){bool same=true;
          for(int x=BX0;x<=BX1 && same;++x){const int n=BX1-BX0+1,src=BX0+((x-BX0-s)%n+n)%n;same=inkAt(glitch,x)==inkAt(base,src);}
          if(same)shift=s;}
        assert(shift!=99);
        rows+=shift!=0;
        for(int x=BX0;x<=BX1;++x)tinted+=shift==0 && base[y*135+x] && glitch[y*135+x]!=base[y*135+x];
      }
      assert(rows<=4);moved+=rows;
    }
    assert(moved>=40 && tinted>=100);
    for(unsigned ms:{1200u,1450u,1600u}){auto f=frame(Effect::Glitch,ms,cyan);for(int i=0;i<135*240;++i)if(base[i])assert(f[i]==base[i] || i/135<=BY1);}
    // Every readout pixel is exact again from 1.56 s, long before 3/4 (3.6 s).
    for(unsigned ms=1560;ms<4800;ms+=10){auto f=frame(Effect::Glitch,ms,cyan);for(int y=BY0;y<=BY1;++y)for(int x=BX0;x<=BX1;++x)if(base[y*135+x])assert(f[y*135+x]==base[y*135+x]);}
  }
  std::cout<<"PASS: five NieR-inspired effects deterministic, halo-clipped, ink-exact at the end, custom-background invariant, within the 8 ms cost model, with per-effect identity properties\n";
}
