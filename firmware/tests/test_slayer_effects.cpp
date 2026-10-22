// The five Demon Slayer-inspired celebrations (Sunwheel..Butterfly: Sunwheel,
// Torrent, Blitz, Tigerclaw, Butterfly): the shared renderer contract, the
// device cost model on every frame, and one identity property per effect.
// The fixture is test_physical_effects' recap, so the checks stay comparable.
#include "best_effect_ui.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

static std::array<uint16_t,135*240> base_{};
static uint16_t swap16(uint16_t v){return uint16_t(v<<8|v>>8);}
using Canvas=std::array<uint16_t,135*240>;

int main(){
  using namespace Appearance;
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

  BestEffectUI::Renderer renderer;
  const Effect effects[]={Effect::Sunwheel,Effect::Torrent,Effect::Blitz,Effect::Tigerclaw,Effect::Butterfly};
  auto frame=[&](Effect e,unsigned ms,uint16_t accentColor){
    Canvas out=base;renderer.draw({out.data(),135,240},e,ms,accentColor);return out;
  };
  // The device cost model (HIL !effectframes fit, 2026-10-20), in us.
  auto model=[&]{return renderer.modelledUs();};

  for(unsigned e=0;e<5;++e){
    const Effect effect=effects[e];
    const uint32_t dur=duration(effect);
    assert(dur>=4800 && dur<=5200);
    // The cost bound: every frame of theme 0 on this fixture, by the model.
    // The sustain (2.0-3.5 s) costs less than the impact (0.4-1.6 s), keeps
    // living (each 100 ms differs from the last) and is never empty.
    {
      double worst=0,impact=0,sustain=0;const uint16_t color=accent(Theme::Classic);
      Canvas previous{};
      for(unsigned ms=0;ms<dur;ms+=10){
        auto out=frame(effect,ms,color);const double m=model();worst=std::max(worst,m);
        if(ms>=400 && ms<=1600)impact=std::max(impact,m);
        if(ms>=2000 && ms<=3500){
          sustain=std::max(sustain,m);
          if(ms%100==0){assert(out!=base);if(ms>2000)assert(out!=previous);previous=out;}
        }
      }
      assert(worst<=8000 && sustain<impact);
    }
    for(unsigned theme:{0u,3u,5u,8u}){
      const uint16_t color=accent(Theme(theme));
      bool drew=false,glinted=false;
      for(unsigned ms=0;ms<=dur+200;ms+=10){
        auto out=frame(effect,ms,color);
        auto again=frame(effect,ms,color);
        assert(out==again); // deterministic: a pure function of elapsed time
        assert(renderer.plots()<=12000);
        const bool settled=ms*4>=dur*3;
        for(int i=0;i<135*240;++i)if(out[i]!=base[i]){
          drew=true;
          if(base[i]){
            // No Slayer effect moves or recolours ink: a changed UI pixel is
            // the glint, brighter in every channel, in the header rows, and
            // gone by three quarters of the duration.
            const uint16_t was=swap16(base[i]),now=swap16(out[i]);
            assert(i/135<=46 && !settled && (now>>11)>=(was>>11) && ((now>>5)&63)>=((was>>5)&63) && (now&31)>=(was&31));glinted=true;
          }else assert(!halo[i]); // decoration keeps the UI halo
        }
        if(ms>=dur)assert(out==base); // nothing is drawn after the duration
        if(settled)for(int i=0;i<135*240;++i)if(base[i])assert(out[i]==base[i]);
      }
      assert(drew && glinted);
    }
    // An unhurried settle: the last 150 ms add little light.
    {
      auto out=frame(effect,dur-60,accent(Theme::Classic));long light=0;
      for(int i=0;i<135*240;++i)if(!base[i] && out[i]!=base[i]){const uint16_t v=swap16(out[i]);light+=(v>>11)+((v>>5)&63)/2+(v&31);}
      assert(light<1500);
    }
    // A custom theme's background: the same pixels change as on black.
    const uint16_t bg=0x0841;Canvas themed=base;
    for(auto &p:themed)if(!p)p=swap16(bg);
    for(unsigned ms:{40u,300u,700u,1000u,1300u,1800u,2300u,3000u,3800u,4500u}){
      auto black=frame(effect,ms,accent(Theme::Mint));
      auto out=themed;renderer.draw({out.data(),135,240},effect,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
  }

  const uint16_t cyan=accent(Theme::Classic);
  // Lit background pixels of a frame, with their colour channels.
  struct Lit{int x,y,r,g,b;};
  auto lit=[&](const Canvas &out){
    std::vector<Lit> v;
    for(int y=0;y<240;++y)for(int x=0;x<135;++x){
      const int i=y*135+x;if(base[i] || out[i]==base[i])continue;
      const uint16_t c=swap16(out[i]);v.push_back({x,y,(c>>11)*8,((c>>5)&63)*4,(c&31)*8});
    }
    return v;
  };
  // Sunwheel: the arc sweeps round and closes into a ring of fire, centred
  // 10 px below the readout's centre with a radius of about 50: at 0.7 s the
  // lit sectors are a partial arc, at 1.3 s every visible sector is lit, and
  // the ring is fire coloured (red over blue).
  {
    auto sectors=[&](unsigned ms){
      unsigned mask=0,near=0,all=0,warm=0;
      for(const Lit &p:lit(frame(Effect::Sunwheel,ms,cyan))){
        const double dx=p.x-67.0,dy=p.y-40.0,r=std::hypot(dx,dy);++all;
        if(p.r>p.b+40)++warm;
        if(r<45 || r>57)continue;++near;
        mask|=1u<<int((std::atan2(dy,dx)+M_PI)/(2*M_PI)*12)%12;
      }
      assert(warm*10>=all*9);
      return std::make_pair(mask,near*10>=all*6);
    };
    auto sweeping=sectors(700),closed=sectors(1300);
    // Sectors 0..11 run clockwise from the left; the lower half (6..11 is
    // angles 0..pi below the centre) is never hidden by the readout.
    const unsigned lower=0x0fc0u;
    assert(closed.second && (closed.first&lower)==lower && __builtin_popcount(closed.first)>=8);
    assert(__builtin_popcount(sweeping.first&lower)>=2 && (sweeping.first&lower)!=lower);
  }
  // Torrent: a blue serpent swims in from the left under the readout (its
  // lit centroid moves right), breaks into a crest at the right, then flows
  // on as a stream across the whole width.
  {
    auto stats=[&](unsigned ms){
      double sx=0;unsigned n=0,blue=0;
      for(const Lit &p:lit(frame(Effect::Torrent,ms,cyan))){sx+=p.x;++n;if(p.b>p.r)++blue;}
      assert(n>0 && blue*10>=n*8);return sx/n;
    };
    const double early=stats(600),late=stats(1000);
    assert(late>early+15);
    unsigned crest=0;
    for(const Lit &p:lit(frame(Effect::Torrent,1500,cyan)))if(p.x>=100 && p.y>=46 && p.y<=66)++crest;
    assert(crest>60);
    int x0=135,x1=-1;for(const Lit &p:lit(frame(Effect::Torrent,3000,cyan))){x0=std::min(x0,p.x);x1=std::max(x1,p.x);}
    assert(x0<12 && x1>122 && stats(3000)>40 && stats(3000)<95);
  }
  // Blitz: stillness at the left edge, then one dash lights the full width
  // in a narrow band of rows, in yellow.
  {
    auto still=lit(frame(Effect::Blitz,400,cyan));
    assert(!still.empty());
    for(const Lit &p:still)assert(p.x<30);
    auto dash=lit(frame(Effect::Blitz,920,cyan));
    int x0=135,x1=-1;unsigned inBand=0,yellow=0;
    for(const Lit &p:dash){x0=std::min(x0,p.x);x1=std::max(x1,p.x);if(std::abs(p.y-53)<=12)++inBand;if(p.r>p.b+40 || (p.r>200 && p.g>200))++yellow;}
    assert(x0<=2 && x1>=132 && inBand*10>=dash.size()*9 && yellow*10>=dash.size()*8);
    // Every column of the dash's core row is lit: no gaps at mask-word edges.
    auto out=frame(Effect::Blitz,920,cyan);
    for(int x=0;x<135;++x)assert(out[53*135+x]!=base[53*135+x] || out[52*135+x]!=base[52*135+x]);
    // The filament hangs across the width through the sustain, no higher.
    auto hang=lit(frame(Effect::Blitz,2600,cyan));int h0=135,h1=-1;
    for(const Lit &p:hang){h0=std::min(h0,p.x);h1=std::max(h1,p.x);assert(std::abs(p.y-53)<=16);}
    assert(h0<10 && h1>125);
  }
  // Tigerclaw: three fire claw marks side by side below the readout, cut
  // by tiger stripes: most rows hold three separate runs, and some rows of
  // a claw are dark where a stripe crosses it.
  {
    for(unsigned ms:{1000u,3000u}){
    auto out=frame(Effect::Tigerclaw,ms,cyan);
    unsigned three=0,rows=0,striped=0;
    for(int y=48;y<=100;++y){
      int runs=0;bool on=false;
      for(int x=0;x<135;++x){const int i=y*135+x;const bool l=!base[i] && out[i]!=base[i];if(l && !on)++runs;on=l;}
      if(runs==0)continue;++rows;if(runs>=3)++three;else ++striped;
    }
    assert(rows>=30 && three*2>=rows && striped>=3);
    unsigned warm=0,all=0;for(const Lit &p:lit(out)){++all;if(p.r>p.b+60)++warm;}
    assert(all>150 && warm*10>=all*9);
    }
  }
  // Butterfly: violet butterflies gather below the readout, dance there,
  // then rise away.
  {
    auto stats=[&](unsigned ms,double &cy){
      unsigned n=0,violet=0;double sy=0;
      for(const Lit &p:lit(frame(Effect::Butterfly,ms,cyan))){++n;sy+=p.y;if(p.b>p.g && p.r>p.g-20)++violet;}
      cy=n?sy/n:0;return std::make_pair(n,violet);
    };
    double hover=0,dancing=0,rising=0;
    auto h=stats(800,hover),d=stats(2600,dancing),r=stats(4150,rising);
    assert(h.first>250 && h.second*10>=h.first*7);
    assert(d.first>250 && std::fabs(dancing-hover)<12);
    assert(r.first>0 && rising<hover-20);
    // Five separate butterflies: count lit clusters in the hover frame.
    auto out=frame(Effect::Butterfly,800,cyan);std::array<int,135*240> label{};int clusters=0;
    for(int i=0;i<135*240;++i){
      if(base[i] || out[i]==base[i] || label[i])continue;
      ++clusters;std::vector<int> stack{i};label[i]=clusters;unsigned size=0;
      while(!stack.empty()){const int j=stack.back();stack.pop_back();++size;
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){const int x=j%135+dx,y=j/135+dy;if(x<0||x>=135||y<0||y>=240)continue;
          const int k=y*135+x;if(!base[k] && out[k]!=base[k] && !label[k]){label[k]=clusters;stack.push_back(k);}}}
      if(size<12)--clusters;  // stray sparkle, not a butterfly
    }
    assert(clusters>=4);
  }

  std::cout<<"PASS: five Slayer effects deterministic, halo-clipped, ink-exact from 3/4, custom-background invariant, model cost <= 8 ms on every frame, with per-effect identity properties\n";
}
