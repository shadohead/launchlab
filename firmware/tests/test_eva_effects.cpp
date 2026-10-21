// The five Evangelion-inspired celebrations (Crimson..Prism): the shared
// renderer contract, the device cost model and one identity property per
// effect. The fixture is test_physical_effects.cpp's, so the checks stay
// comparable; the generic every-theme sweep also runs in test_appearance.
#include "best_effect_ui.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstdlib>

static std::array<uint16_t,135*240> base_{};
static uint16_t swap16(uint16_t v){return uint16_t(v<<8|v>>8);}

int main(){
  using namespace Appearance;
  using Frame=std::array<uint16_t,135*240>;
  // Canvas pixels are stored as LGFX sprites keep them: RGB565 big-endian.
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
  // The readout's ink box in this fixture: x 10..124, y 17..43.
  constexpr int CX=67,CY=30,H=43;

  BestEffectUI::Renderer renderer;
  const Effect effects[]={Effect::Crimson,Effect::Bulwark,Effect::Crux,Effect::Klaxon,Effect::Prism};
  // Each runs about 5 s: an impact within the first 1.6 s, then a sustain
  // (these windows, in ms) that keeps moving, stays off the readout and
  // costs less than the impact, then a settle.
  const unsigned sustain[5][2]={{1800,3800},{2200,3600},{1600,3700},{2400,3600},{2000,3800}};
  auto frame=[&](Effect e,unsigned ms,uint16_t accentColor){
    Frame out=base;renderer.draw({out.data(),135,240},e,ms,accentColor);return out;
  };
  struct Rgb5{int r,g,b;};
  auto rgb=[](uint16_t stored){const uint16_t v=swap16(stored);return Rgb5{v>>11,(v>>5)&63,v&31};};
  auto lit=[&](const Frame &f,int x,int y){const int i=y*135+x;return !base[i] && f[i]!=base[i];};

  for(unsigned e=0;e<5;++e){
    const Effect effect=effects[e];
    const uint32_t dur=duration(effect);
    assert(dur>=4800 && dur<=5200);
    for(unsigned theme:{0u,3u,5u,8u}){
      const uint16_t color=accent(Theme(theme));
      bool drew=false,glinted=false;uint32_t maxPlots=0;double maxCost=0,impact=0,held=0;
      for(unsigned ms=0;ms<=5500;ms+=10){
        auto out=frame(effect,ms,color);
        // The device cost model, fitted to every frame of every effect.
        const double cost=1100+1.8*renderer.plots()+1.0*renderer.probes()+13.0*renderer.scans();
        auto again=frame(effect,ms,color);
        assert(out==again); // deterministic: a pure function of elapsed time
        maxPlots=std::max(maxPlots,renderer.plots());maxCost=std::max(maxCost,cost);
        const bool inSustain=ms>=sustain[e][0] && ms<sustain[e][1];
        if(ms<2000)impact=std::max(impact,cost);
        if(inSustain)held=std::max(held,cost);
        for(int i=0;i<135*240;++i)if(out[i]!=base[i]){
          drew=true;
          if(base[i]){
            // A changed UI pixel: every effect's glint, or Prism's beam pulling
            // the glyphs under its column toward white: brighter in every
            // channel, inside the header rows and before 3/4 of the duration.
            const auto was=rgb(base[i]),now=rgb(out[i]);
            assert(i/135<=46 && ms*4<dur*3 && now.r>=was.r && now.g>=was.g && now.b>=was.b);
            glinted=true;
          }else {
            assert(!halo[i]); // decoration keeps the UI halo
            // The sustain stays off the readout: not even its glyph gaps light up.
            const int x=i%135,y=i/135;
            assert(!(inSustain && x>=10 && x<=124 && y>=17 && y<=43));
          }
        }
        if(ms>=dur)assert(out==base); // nothing is drawn after the duration
      }
      assert(drew && glinted && maxPlots<=4000);
      if(theme==0){
        std::cout<<"  "<<label(effect)<<": worst modelled frame "<<int(maxCost)<<" us (impact "<<int(impact)<<", sustain "<<int(held)<<")\n";
        assert(maxCost<=8000); // under the 10 ms device budget with margin
        assert(held<impact);   // the sustain is cheaper than the impact
      }
      // The sustain never freezes: frames 150 ms apart always differ.
      for(unsigned ms=sustain[e][0];ms+150<sustain[e][1];ms+=150){
        const auto a=frame(effect,ms,color),b=frame(effect,ms+150,color);unsigned moved=0;
        for(int i=0;i<135*240;++i)moved+=a[i]!=b[i];
        assert(moved>=20);
      }
      // The readout is exactly as drawn again well before the end.
      auto settled=frame(effect,dur-100,color);
      for(int i=0;i<135*240;++i)if(base[i])assert(settled[i]==base[i]);
    }
    // A custom theme's background: the same pixels change as on black.
    const uint16_t bg=0x0841;Frame themed=base;
    for(auto &p:themed)if(!p)p=swap16(bg);
    for(unsigned ms:{40u,300u,700u,1000u,1200u,1500u,2000u,3000u,4200u}){
      auto black=frame(effect,ms,accent(Theme::Mint));
      auto out=themed;renderer.draw({out.data(),135,240},effect,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
  }

  const uint16_t cyan=accent(Theme::Classic);
  // Crimson: a crimson wave front crosses the whole width and rises; then a
  // red ring stands around the readout above the sea.
  {
    auto front=[&](unsigned ms,double &meanY,int &width){
      const Frame f=frame(Effect::Crimson,ms,cyan);double sy=0;unsigned n=0;int x0=135,x1=-1;
      for(int y=60;y<240;++y)for(int x=0;x<135;++x)if(lit(f,x,y)){const auto c=rgb(f[y*135+x]);if(c.r*2>c.g){sy+=y;++n;x0=std::min(x0,x);x1=std::max(x1,x);}}
      meanY=n?sy/n:0;width=x1-x0;return n;
    };
    double early,late;int we,wl;
    const unsigned ne=front(250,early,we),nl=front(600,late,wl);
    assert(ne>150 && nl>150 && we>120 && wl>120 && late<early-30); // a full-width wave, rising
    const Frame f=frame(Effect::Crimson,1500,cyan);
    unsigned ring=0,red=0,above=0;
    // Above the sea's surface (horizon at y 76 in this fixture).
    for(int y=0;y<68;++y)for(int x=0;x<135;++x)if(lit(f,x,y)){
      ++above;const auto c=rgb(f[y*135+x]);red+=c.r*2>c.g && c.r>c.b;
      const double d=std::hypot(x-CX,y-CY);if(std::fabs(d-54)<=5)++ring;
    }
    assert(above>150 && ring*10>=above*7 && red*10>=above*8); // most of it a red ring of radius ~54
  }
  // Bulwark: the barrier is made of octagons: long straight runs of orange
  // pixels horizontally, vertically and on both diagonals, and the hits
  // land left, then right, then centre.
  {
    const Frame f=frame(Effect::Bulwark,1150,cyan);
    auto orange=[&](int x,int y){if(x<0 || x>=135 || y<0 || y>=240 || !lit(f,x,y))return false;const auto c=rgb(f[y*135+x]);return c.r>=20 && c.b*3<c.r*2 && c.g<c.r*2;};
    unsigned runs[4]={};const int dx[4]={1,0,1,1},dy[4]={0,1,1,-1};
    for(int d=0;d<4;++d)for(int y=0;y<240;++y)for(int x=0;x<135;++x) {
      if(!orange(x,y) || orange(x-dx[d],y-dy[d]))continue; // the start of a run
      int n=1;while(orange(x+n*dx[d],y+n*dy[d]))++n;
      if(n>=5)++runs[d];
    }
    assert(runs[0]>=6 && runs[1]>=6 && runs[2]>=4 && runs[3]>=4);
    auto centroid=[&](unsigned ms){const Frame g=frame(Effect::Bulwark,ms,cyan);double sx=0;unsigned n=0;
      for(int y=0;y<240;++y)for(int x=0;x<135;++x)if(lit(g,x,y)){sx+=x;++n;}return n?sx/n:-1;};
    const double first=centroid(400),second=centroid(700),third=centroid(1150);
    assert(first>0 && first<CX-15 && second>CX-5 && std::fabs(third-CX)<12);
  }
  // Crux: a cross: at full blaze nearly every lit pixel is on a horizontal
  // arm under the readout or on a vertical arm through its centre, and both
  // arms are long.
  {
    const Frame f=frame(Effect::Crux,900,cyan);const int yc=H+8;
    unsigned arm=0,pillarAbove=0,pillarBelow=0,other=0;int left=135,right=-1;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x)if(lit(f,x,y)){
      if(std::abs(y-yc)<=2){++arm;left=std::min(left,x);right=std::max(right,x);}
      else if(std::abs(x-CX)<=4){if(y<yc)++pillarAbove;else ++pillarBelow;}
      else ++other;
    }
    assert(arm>300 && right-left>=100 && pillarAbove>15 && pillarBelow>150 && other*10<arm+pillarAbove+pillarBelow);
    // Before the burst only a small core glows; after it the cross cools to red.
    const Frame core=frame(Effect::Crux,250,cyan);unsigned n=0;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x)if(lit(core,x,y)){++n;assert(std::abs(x-CX)<=36 && std::abs(y-yc)<=28);}
    assert(n>8);
    const Frame hot=frame(Effect::Crux,800,cyan),cool=frame(Effect::Crux,4200,cyan);
    const auto h=rgb(hot[yc*135+42]),c=rgb(cool[yc*135+42]);
    // Green relative to red, both as fractions of full scale: about 1 for
    // white, about 0.55 for the orange it cools to.
    assert(h.r>0 && c.r>0 && h.g*31>h.r*63*.85 && c.g*31<c.r*63*.7); // white-hot first, orange-red as it goes
    // At the end the arms draw in from their tips.
    const Frame late=frame(Effect::Crux,4500,cyan);int l2=135,r2=-1;
    for(int x=0;x<135;++x)if(lit(late,x,yc)){l2=std::min(l2,x);r2=std::max(r2,x);}
    assert(r2>l2 && r2-l2<right-left-30);
  }
  // Klaxon: a hazard band of stripes right under the readout, a red frame up
  // its sides, a blinking text block and a blue meter on the right.
  {
    const Frame f=frame(Effect::Klaxon,1000,cyan);
    int flips=0;bool was=false;
    for(int x=0;x<135;++x){const bool on=lit(f,x,H+6);if(x && on!=was)++flips;was=on;}
    assert(flips>=24); // stripes, not a solid bar
    unsigned frameSide=0;for(int y=17;y<=43;++y)if(lit(f,6,y)){const auto c=rgb(f[y*135+6]);frameSide+=c.r>c.g/2 && c.r>c.b;}
    assert(frameSide>=20);
    unsigned blue=0;for(int y=H+12;y<=H+18;++y)for(int x=100;x<135;++x)if(lit(f,x,y)){const auto c=rgb(f[y*135+x]);blue+=c.b>c.r;}
    assert(blue>=10);
    auto text=[&](unsigned ms){const Frame g=frame(Effect::Klaxon,ms,cyan);unsigned n=0;
      for(int y=H+12;y<=H+18;++y)for(int x=17;x<=48;++x)if(lit(g,x,y)){const auto c=rgb(g[y*135+x]);n+=c.r>c.b;}return n;};
    unsigned on=0,off=0;for(unsigned ms=600;ms<1800;ms+=20){const unsigned n=text(ms);if(n>40)++on;else if(n==0)++off;}
    assert(on>10 && off>5); // the alert blinks
    // Before the alarm only the blue scan runs: nothing red yet.
    const Frame scan=frame(Effect::Klaxon,380,cyan);unsigned bluish=0,reddish=0;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x)if(lit(scan,x,y)){const auto c=rgb(scan[y*135+x]);bluish+=c.b>c.r;reddish+=c.r>c.b;}
    assert(bluish>100 && reddish==0);
  }
  // Prism: a blue gem under the readout, a particle ring that contracts
  // onto it, and a beam that lights only the readout's centre column.
  {
    const Frame gem=frame(Effect::Prism,400,cyan);unsigned blue=0;
    for(int y=H+10;y<=H+48;++y)for(int x=CX-16;x<=CX+16;++x)if(lit(gem,x,y)){const auto c=rgb(gem[y*135+x]);blue+=c.b*2>c.r*3;}
    assert(blue>150);
    auto spread=[&](unsigned ms){const Frame g=frame(Effect::Prism,ms,cyan);double s=0;unsigned n=0;
      for(int y=H+3;y<240;++y)for(int x=0;x<135;++x)if(lit(g,x,y) && (std::abs(x-CX)>18 || y>H+50)){s+=std::abs(x-CX);++n;}return n?s/n:0.0;};
    assert(spread(450)>35 && spread(900)<spread(450)-10); // the ring draws in
    const Frame fire=frame(Effect::Prism,1500,cyan);unsigned column=0;
    for(int y=17;y<=43;++y)for(int x=0;x<135;++x)if(base[y*135+x] && fire[y*135+x]!=base[y*135+x]){assert(std::abs(x-CX)<=7);++column;}
    assert(column>60);
    unsigned above=0;for(int y=0;y<17;++y)for(int x=CX-6;x<=CX+6;++x)above+=lit(fire,x,y);
    assert(above>=20); // the beam continues above the readout (the label hides most of it here)
  }

  std::cout<<"PASS: five 5 s Evangelion-inspired effects deterministic, halo-clipped, readout exact from 3/4, sustains moving, off the readout and cheaper than their impacts, custom-background invariant, under the 8 ms cost model, with per-effect identity properties\n";
}
