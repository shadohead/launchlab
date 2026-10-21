// The five Naruto-inspired celebrations (Birdsong..Summon): the shared
// renderer contract, the device cost model and one identity property per
// effect. Same fixture as test_physical_effects and test_appearance.
#include "best_effect_ui.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>
#include <array>
#include <cstdlib>

static std::array<uint16_t,135*240> base_{};
static uint16_t swap16(uint16_t v){return uint16_t(v<<8|v>>8);}
using Frame=std::array<uint16_t,135*240>;

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
  // The UI halo, and each pixel's distance to the nearest ink (capped).
  std::array<bool,135*240> halo{};std::array<uint8_t,135*240> inkDist{};
  for(auto &d:inkDist)d=255;
  for(int y=0;y<240;++y)for(int x=0;x<135;++x)if(base[y*135+x])
    for(int dy=-8;dy<=8;++dy)for(int dx=-8;dx<=8;++dx){
      const int xx=x+dx,yy=y+dy;if(xx<0 || xx>=135 || yy<0 || yy>=240)continue;
      const int d=std::max(std::abs(dx),std::abs(dy));uint8_t &m=inkDist[yy*135+xx];if(d<m)m=uint8_t(d);
      if(d<=2)halo[yy*135+xx]=true;
    }

  BestEffectUI::Renderer renderer;
  const Effect effects[]={Effect::Birdsong,Effect::Spiral,Effect::Pinwheel,Effect::Overdrive,Effect::Summon};
  auto frame=[&](Effect e,unsigned ms,uint16_t accentColor){
    auto out=base;renderer.draw({out.data(),135,240},e,ms,accentColor);return out;
  };
  const uint16_t cyan=accent(Theme::Classic);
  // The device cost model, fitted to every frame of every effect on the M5
  // (HIL !effectframes): the effect pass at 80 MHz in microseconds.
  auto modelUs=[&]{return 1100+1.8*renderer.plots()+1.0*renderer.probes()+13.0*renderer.scans();};

  for(unsigned e=0;e<5;++e){
    const Effect effect=effects[e];
    const uint32_t dur=duration(effect);
    assert(dur>=4800 && dur<=5200);
    // Cost: every frame of theme 0 on this fixture within 8 ms by the model.
    // The sustain (2.2 s until the settle at 3.8 s) is cheaper than the
    // impact (the first 2 s), and keeps living: every frame lights pixels and
    // differs from the one 100 ms before.
    double worst=0,impact=0,sustain=0;Frame previous{};
    for(unsigned ms=0;ms<dur;ms+=10){
      auto out=frame(effect,ms,cyan);const double us=modelUs();worst=std::max(worst,us);
      if(ms<2000)impact=std::max(impact,us);
      if(ms>=2200 && ms<3800) {
        sustain=std::max(sustain,us);
        if(ms%100==0) {
          unsigned lit=0,changed=0;
          for(int i=0;i<135*240;++i){lit+=!base[i] && out[i]!=base[i];changed+=ms>2200 && out[i]!=previous[i];}
          assert(lit>=25 && (ms==2200 || changed>=10));
          previous=out;
        }
      }
    }
    assert(worst<=8000 && sustain<impact);
    for(unsigned theme:{0u,3u,5u,8u}){
      const uint16_t color=accent(Theme(theme));
      bool drew=false,glinted=false;
      for(unsigned ms=0;ms<=dur+300;ms+=10){
        auto out=frame(effect,ms,color);
        auto again=frame(effect,ms,color);
        assert(out==again); // deterministic: a pure function of elapsed time
        for(int i=0;i<135*240;++i)if(out[i]!=base[i]){
          drew=true;
          if(base[i]){
            // No Ninja effect moves ink: a changed UI pixel is the glint,
            // brighter in every channel, in the header rows, before 3/4.
            const uint16_t was=swap16(base[i]),now=swap16(out[i]);
            assert(i/135<=46 && ms*4<dur*3 && (now>>11)>=(was>>11) && ((now>>5)&63)>=((was>>5)&63) && (now&31)>=(was&31));glinted=true;
          }else assert(!halo[i]); // decoration keeps the UI halo
        }
        if(ms>=dur)assert(out==base); // nothing is drawn after the duration
      }
      assert(drew && glinted);
      // The readout is exactly as drawn again well before the end.
      for(unsigned ms=dur*3/4;ms<dur;ms+=10){
        auto settled=frame(effect,ms,color);
        for(int i=0;i<135*240;++i)if(base[i])assert(settled[i]==base[i]);
      }
    }
    // A custom theme's background: the same pixels change as on black.
    const uint16_t bg=0x0841;Frame themed=base;
    for(auto &p:themed)if(!p)p=swap16(bg);
    for(unsigned ms:{40u,300u,700u,1000u,1250u,1700u,2200u,3000u,4000u,4600u}){
      auto black=frame(effect,ms,accent(Theme::Mint));
      auto out=themed;renderer.draw({out.data(),135,240},effect,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
  }

  // Lit background pixels, with their RGB565 channels scaled to 0..1.
  struct Lit{int x,y;double r,g,b;};
  auto lit=[&](const Frame &out){
    std::vector<Lit> v;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x){
      const int i=y*135+x;if(base[i] || out[i]==base[i])continue;
      const uint16_t c=swap16(out[i]);v.push_back({x,y,(c>>11)/31.0,((c>>5)&63)/63.0,(c&31)/31.0});
    }
    return v;
  };
  struct Stats{double n=0,cx=0,cy=0;int x0=135,x1=-1,y0=240,y1=-1;};
  auto stats=[&](const std::vector<Lit> &v){
    Stats s;s.n=double(v.size());
    for(const auto &p:v){s.cx+=p.x;s.cy+=p.y;s.x0=std::min(s.x0,p.x);s.x1=std::max(s.x1,p.x);s.y0=std::min(s.y0,p.y);s.y1=std::max(s.y1,p.y);}
    if(s.n){s.cx/=s.n;s.cy/=s.n;}
    return s;
  };
  // The fixture's readout: x 10..124, y 17..43, centre (67,30).
  constexpr int Y1=43;

  // Charge: lightning gathers in a blue-white ball low on the left, then
  // bursts along the readout's edges.
  {
    auto charge=lit(frame(Effect::Birdsong,800,cyan));auto s=stats(charge);
    unsigned blue=0;for(const auto &p:charge)blue+=p.b>=p.r;
    assert(s.n>120 && blue>=s.n*.9);
    assert(s.cx<52 && s.cy>Y1+45 && s.x1-s.x0<70 && s.y1-s.y0<70); // a compact ball, low on the left
    auto burst=stats(lit(frame(Effect::Birdsong,1260,cyan)));
    assert(burst.n>300 && burst.cy<Y1+20 && burst.x1-burst.x0>90); // at the readout, spread along it
    // Sustain: the fist (33,107 here) keeps a pulsing charge, and arcs leap
    // from it up toward the readout now and then.
    unsigned fistMin=~0u,fistMax=0,leaps=0;
    for(unsigned ms=1800;ms<3800;ms+=10){
      unsigned fist=0,mid=0;
      for(const auto &p:lit(frame(Effect::Birdsong,ms,cyan))){
        fist+=std::abs(p.x-33)<14 && std::abs(p.y-107)<14;
        mid+=p.x>=48 && p.y>=Y1+20 && p.y<=Y1+40;
      }
      fistMin=std::min(fistMin,fist);fistMax=std::max(fistMax,fist);leaps+=mid>=6;
    }
    assert(fistMin>=15 && fistMax>=fistMin*2 && leaps>=12);
  }
  // Spiral: a round sphere forms in the space right of the replay's corner,
  // then drives into the readout's centre and releases a wide spiral.
  {
    const double sx=67+34.2,sy=Y1+17;
    auto orb=lit(frame(Effect::Spiral,700,cyan));
    unsigned near=0,quad[4]={};
    for(const auto &p:orb){const double dx=p.x-sx,dy=p.y-sy;if(dx*dx+dy*dy<=17*17){++near;++quad[(dx>=0)+2*(dy>=0)];}}
    assert(orb.size()>250 && near>=orb.size()*.8);
    for(unsigned q:quad)assert(q>=near*.15); // round: every quadrant lit
    auto press=stats(lit(frame(Effect::Spiral,1300,cyan)));
    assert(std::fabs(press.cx-67)<10 && press.cy<Y1+16);
    auto blast=stats(lit(frame(Effect::Spiral,1900,cyan)));
    assert(blast.x1-blast.x0>100 && blast.n>150);
  }
  // Pinwheel: two red eyes, mirrored about the readout's centre; dark commas
  // in each iris spin, blur into a ring and reform as a three-fold pattern.
  {
    const int ey=Y1+17,ex[2]={67-34,67+34};
    auto eyes=lit(frame(Effect::Pinwheel,900,cyan));
    unsigned red=0,side[2]={};
    for(const auto &p:eyes){if(p.r>=.5 && p.r>=2*p.g && p.r>=2*p.b)++red;++side[p.x>67];}
    assert(eyes.size()>500 && red>=eyes.size()*.9);
    assert(std::abs(int(side[0])-int(side[1]))<=int(eyes.size()/8));
    // In the comma band (radius 6.5..12.5) of a free, open iris: the share
    // of pixels left dark.
    auto darkShare=[&](const Frame &out,int e,double r0,double r1){
      unsigned dark=0,all=0;
      for(int y=ey-13;y<=ey+13;++y)for(int x=ex[e]-13;x<=ex[e]+13;++x){
        const int i=y*135+x;const double d=std::hypot(x-ex[e],y-ey);
        if(d<r0 || d>r1 || halo[i] || base[i])continue;++all;dark+=out[i]==base[i];
      }
      return all?double(dark)/all:1.0;
    };
    for(int e=0;e<2;++e) {
      const double commas=darkShare(frame(Effect::Pinwheel,800,cyan),e,6.5,12.5);
      const double ring=darkShare(frame(Effect::Pinwheel,1240,cyan),e,9.6,10.6);
      assert(commas>.12 && commas<.55 && ring>.8);
    }
    // The commas turn between frames.
    auto a=frame(Effect::Pinwheel,800,cyan),b=frame(Effect::Pinwheel,900,cyan);
    unsigned moved=0;for(int i=0;i<135*240;++i)if(!base[i] && (a[i]==base[i])!=(b[i]==base[i]))++moved;
    assert(moved>40);
    // The reformed pattern is three-fold: dark pixels rotated by 120 degrees
    // about the pupil land on dark pixels (or next to one).
    auto loops=frame(Effect::Pinwheel,1800,cyan);
    for(int e=0;e<2;++e) {
      unsigned dark=0,match=0;const double c=std::cos(2*M_PI/3),s=std::sin(2*M_PI/3);
      for(int y=ey-13;y<=ey+13;++y)for(int x=ex[e]-13;x<=ex[e]+13;++x){
        const double dx=x-ex[e],dy=y-ey,d=std::hypot(dx,dy);const int i=y*135+x;
        if(d<4.5 || d>13 || halo[i] || loops[i]!=base[i])continue;
        const int rx=int(std::lround(ex[e]+dx*c-dy*s)),ry=int(std::lround(ey+dx*s+dy*c));
        bool hit=false,free=false;
        for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox){const int j=(ry+oy)*135+rx+ox;if(!halo[j]){free=true;hit|=loops[j]==base[j];}}
        if(!free)continue;++dark;match+=hit;
      }
      assert(dark>20 && match>=dark*.8);
    }
  }
  // Gates: a green aura hugs the UI's outline just outside its halo, and
  // grows beat by beat until the last.
  {
    auto aura=lit(frame(Effect::Overdrive,900,cyan));
    unsigned green=0,hug=0;
    for(const auto &p:aura){if(p.g>p.r && p.g>p.b)++green;const uint8_t d=inkDist[p.y*135+p.x];hug+=d>=3 && d<=5;}
    assert(aura.size()>300 && green>=aura.size()*.85 && hug>=aura.size()*.6);
    // The light added, summed over every lit pixel, beat by beat.
    auto energy=[&](unsigned ms){double sum=0;for(const auto &p:lit(frame(Effect::Overdrive,ms,cyan)))sum+=p.r+p.g+p.b;return sum;};
    const double first=energy(150),later=energy(750),last=energy(1080);
    assert(first>50 && later>first*1.4 && last>later*1.2);
  }
  // Summon: a wide, flat seal of warm script under the readout, then a much
  // larger grey-white cloud.
  {
    auto seal=lit(frame(Effect::Summon,450,cyan));auto s=stats(seal);
    unsigned warm=0;for(const auto &p:seal)warm+=p.r>=p.b;
    assert(s.n>150 && s.x1-s.x0>90 && s.y1-s.y0<45 && s.cy>Y1 && warm>=s.n*.8);
    auto cloud=lit(frame(Effect::Summon,1000,cyan));auto c=stats(cloud);
    unsigned grey=0;for(const auto &p:cloud)grey+=std::fabs(p.r-p.b)<.25 && std::fabs(p.g-p.r)<.3;
    assert(c.n>s.n*1.5 && c.x1-c.x0>90 && grey>=c.n*.8);
  }

  std::cout<<"PASS: five Ninja effects deterministic, halo-clipped, glint-only on UI pixels, ink-exact from 3/4, custom-background invariant, within 8 ms by the device cost model, with per-effect identity properties\n";
}
