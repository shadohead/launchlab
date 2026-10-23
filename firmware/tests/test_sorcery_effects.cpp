// The five Jujutsu Kaisen-inspired celebrations (Rupture..Ravage): the
// shared renderer contract, the device cost model, and one identity property
// per effect. Same fixture as test_physical_effects and test_appearance.
#include "best_effect_ui.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>

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
  const Effect effects[]={Effect::Rupture,Effect::Boundless,Effect::Precision,Effect::Umbra,Effect::Ravage};
  // How each may touch the UI's own pixels, and by when they are exact again:
  // Rupture moves and recolours the readout's rows; Umbra
  // (Umbra) darkens them; the other three only brighten glyph pixels.
  // Boundless also covers every row below the readout's halo (rows 46 on
  // here) until Renderer::covers(), then gives them back exactly.
  const bool moves[]={true,false,false,false,false},dims[]={false,false,false,true,false};
  constexpr int COVERED=46;
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

  // The device cost model (Renderer::modelledUs(), fitted on the M5 at
  // 80 MHz): every frame at most 9 ms (the device budget).
  // Each runs about five seconds (owner, 2026-10-20): an early impact, a
  // sustained middle that keeps moving but costs less than the impact, and
  // a settle. Windows in ms: impact from/to, sustain from/to.
  const unsigned windows[5][4]={{500,1500,1800,3700},{450,1100,1200,3800},{800,1800,1900,3600},{300,1700,2300,3300},{500,1900,2100,3600}};
  const bool report=std::getenv("SORCERY_COSTS")!=nullptr;
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
    const uint32_t dur=duration(effect),cover=BestEffectUI::Renderer::covers(effect);
    assert(dur>=4800 && dur<=5200); // owner, 2026-10-20: about five seconds
    assert(cover*4<=dur*3 && (cover>0)==(effect==Effect::Boundless));
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
          if(ms<cover && y>=COVERED)continue; // covered rows: anything goes
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
    // A custom theme's background: the same pixels change as on black (in
    // covered rows a pixel may land on a UI pixel's own colour, so not there).
    const uint16_t bg=0x0841;Frame themed=base;
    for(auto &p:themed)if(!p)p=swap16(bg);
    for(unsigned ms=0;ms<dur;ms+=50){
      Frame black=frame(effect,ms,accent(Theme::Mint));
      Frame out=themed;renderer.draw({out.data(),135,240},effect,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<(ms<cover?COVERED:240)*135;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
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
        if(ms>=cover)for(int i=47*135;i<135*240;++i)if(light[i]!=swap16(white))assert(out[i]==light[i]);
      }
    }
  }

  const uint16_t cyan=accent(Theme::Classic);
  auto inkRows=[&](const Frame &f){unsigned crimson=0,shifted=0;for(int y=17;y<44;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] && f[i] && f[i]!=base[i] && green(f[i])<20 && red(f[i])>=14)++crimson;if((base[i]!=0)!=(f[i]!=0))++shifted;}return std::make_pair(crimson,shifted);};
  // Rupture: the readout's rows ripple before the hit; the hit frame
  // inverts it (deep crimson digits, still 3:1 on black, in a red blaze);
  // then red-rimmed bolts with dark cores crackle below it, re-struck every
  // frame.
  {
    assert(inkRows(frame(Effect::Rupture,350,cyan)).second>20);
    const Frame hit=frame(Effect::Rupture,700,cyan);
    const auto ink=inkRows(hit);assert(ink.first>1000);
    unsigned blaze=0;for(int y=0;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(!base[i] && hit[i]!=base[i] && red(hit[i])>20 && green(hit[i])<30)++blaze;}
    assert(blaze>300);
    const Frame a=frame(Effect::Rupture,900,cyan),b=frame(Effect::Rupture,950,cyan);
    // A rim pixel with untouched background between it and a rim pixel 3-5 px away across the core.
    unsigned rims=0,cores=0,moved=0;
    for(int y=47;y<120;++y)for(int x=3;x<132;++x){
      const int i=y*135+x;if(base[i])continue;
      const bool lit=a[i]!=base[i] && red(a[i])>18 && green(a[i])<20;rims+=lit;
      if(lit)for(int d=3;d<=5;++d){const int j=i+d;if(x+d<135 && a[j]!=base[j] && red(a[j])>18 && a[i+1]==base[i+1] && a[i+2]==base[i+2]){++cores;break;}}
      moved+=(a[i]!=base[i])!=(b[i]!=base[i]);
    }
    assert(rims>60 && cores>8 && moved>80);
  }
  // Boundless, over the whole screen: the recap below the readout goes
  // dark and two blue eyes open in it; a black-and-white negative (the
  // background white, the eyes dark), then a white splotch that floods
  // every row and the header's background (the readout keeping its own
  // pixels and outline, checked above), ink that eats it, then the void
  // hiding the recap: a bright centre, a rim near the screen's sides and
  // light streaming out; the recap given back by 3.85 s, the last stars
  // gone at the end.
  {
    auto whites=[&](const Frame &f,int y0){unsigned n=0;for(int i=y0*135;i<135*240;++i)if(!halo[i] && red(f[i])>=28 && green(f[i])>=58 && blue(f[i])>=28)++n;return n;};
    unsigned clear=0;for(int i=COVERED*135;i<135*240;++i)clear+=!halo[i];
    const int ey=(COVERED+239)/2-8;
    // Pixels of the eyes' boxes (two 35x19 almonds either side of the centre) passing a test.
    auto eyes=[&](const Frame &f,auto test){unsigned n=0;for(int y=ey-10;y<=ey+10;++y)for(int x=67-42;x<=67+42;++x)n+=test(f[y*135+x]);return n;};
    const Frame opened=frame(Effect::Boundless,400,cyan);
    unsigned shownEarly=0,uiEarly=0;for(int i=50*135;i<135*240;++i)if(base[i]){const int y=i/135;if(y>=ey-12 && y<=ey+12)continue;++uiEarly;shownEarly+=opened[i]==base[i];}
    const unsigned irises=eyes(opened,[](uint16_t p){return blue(p)>=24 && red(p)<blue(p)-6;});
    const Frame negative=frame(Effect::Boundless,500,cyan);
    const unsigned dark=eyes(negative,[](uint16_t p){return red(p)<12 && green(p)<24;});
    if(report)std::printf("boundless eyes shown %u/%u irises %u negative-dark %u\n",shownEarly,uiEarly,irises,dark);
    assert(shownEarly==0 && irises>150 && dark>40 && whites(negative,COVERED)*20>=clear*19);
    const unsigned burst=whites(frame(Effect::Boundless,650,cyan),COVERED),flood=whites(frame(Effect::Boundless,700,cyan),0);
    const unsigned eaten=whites(frame(Effect::Boundless,980,cyan),0),gone=whites(frame(Effect::Boundless,1070,cyan),0);
    // The header floods too, until the ink starts (0.74 s) and gives it back.
    unsigned header=0,headerClear=0;const Frame flooded=frame(Effect::Boundless,700,cyan);
    for(int i=0;i<COVERED*135;++i)if(!halo[i]){++headerClear;header+=red(flooded[i])>=28;}
    if(report)std::printf("boundless clear %u negative %u burst %u flood %u header %u/%u eaten %u gone %u\n",clear,whites(negative,COVERED),burst,flood,header,headerClear,eaten,gone);
    assert(burst>clear/5 && burst<clear*9/10 && flood>=clear && header*10>=headerClear*9 && eaten<flood/2 && gone==0);
    // The void: the recap below the readout hidden, a bright centre, the
    // rim's white near each side on the centre row.
    const Frame open=frame(Effect::Boundless,2000,cyan);
    unsigned shown=0,ui=0;for(int i=50*135;i<135*240;++i)if(base[i]){++ui;shown+=open[i]==base[i];}
    const int cy=(COVERED+239)/2;
    auto bright=[&](int x,int y){const uint16_t p=open[y*135+x];return red(p)>=24 && green(p)>=50 && blue(p)>=28;};
    bool left=false,right=false;for(int x=0;x<40;++x){left|=bright(x,cy);right|=bright(134-x,cy);}
    const Stats held=bgStats(open),closed=bgStats(frame(Effect::Boundless,5150,cyan));
    if(report)std::printf("boundless void shown %u/%u centre %d rim %d %d held %u closed %u\n",shown,ui,bright(67,cy),left,right,held.n,closed.n);
    assert(shown*20<ui && bright(67,cy) && left && right && closed.n<held.n/50);
  }
  // Precision: the guide and the strike land on the 7:3 point of the
  // readout's width (x 10..124, so x 90), not elsewhere.
  {
    const int X7=int(std::lround(10+.7*114));
    const Frame strike=frame(Effect::Precision,1320,cyan);
    unsigned on=0,off=0;
    for(int y=60;y<120;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || strike[i]==base[i])continue;if(std::abs(x-X7)<=1)++on;else ++off;}
    if(report)std::printf("precision on %u off %u\n",on,off);
    assert(on>50 && on>off);
    // The glyphs on that column are heated, and only there.
    unsigned heated=0,elsewhere=0;
    for(int y=17;y<44;++y)for(int x=10;x<125;++x){const int i=y*135+x;if(base[i] && strike[i]!=base[i]){if(std::abs(x-X7)<=1)++heated;else ++elsewhere;}}
    if(report)std::printf("precision heated %u elsewhere %u\n",heated,elsewhere);
    assert(heated>20 && elsewhere==0);
    // The ticks: before the strike both rulers carry eleven ticks.
    const Frame ruled=frame(Effect::Precision,820,cyan);
    unsigned ticks=0;for(int i=0;i<=10;++i){const int x=int(std::lround(10+i*11.4)),y=43+5+3;if(ruled[y*135+x]!=base[y*135+x])++ticks;}
    if(report)std::printf("precision ticks %u\n",ticks);
    assert(ticks>=10);
  }
  // Umbra: the readout darkens from its foot up (the lower rows far more
  // than the upper ones, but never below 3:1, checked above), the pool's lit rim spreads wide below it, and
  // pairs of eyes open in it.
  {
    const Frame sunk=frame(Effect::Umbra,1200,cyan);
    unsigned upper=0,lowerDark=0;
    for(int x=10;x<125;++x){for(int y=17;y<22;++y)upper+=base[y*135+x] && sunk[y*135+x]!=base[y*135+x];for(int y=38;y<44;++y)lowerDark+=base[y*135+x] && 3*green(sunk[y*135+x])<2*green(base[y*135+x]);}
    if(report)std::printf("umbra lowerDark %u upper %u\n",lowerDark,upper);
    assert(lowerDark>400 && upper<lowerDark/4);
    const Stats pool=bgStats(sunk);if(report)std::printf("umbra pool x %d..%d y1 %d\n",pool.x0,pool.x1,pool.y1);assert(pool.x1-pool.x0>110 && pool.y1>60);
    // Eyes, on the header alone (this fixture's replay lines cover them):
    // warm pixels in the cold pool, nothing else in it being redder than blue.
    Frame bare{};std::copy(base.begin(),base.begin()+47*135,bare.begin());
    Frame lit=bare;renderer.draw({lit.data(),135,240},Effect::Umbra,1600,cyan);
    unsigned eyes=0;for(int i=50*135;i<135*240;++i)if(lit[i]!=bare[i] && red(lit[i])>=blue(lit[i])+4 && 2*red(lit[i])>=green(lit[i]))++eyes;
    if(report)std::printf("umbra eyes %u\n",eyes);
    assert(eyes>=6);
  }
  // Ravage: the cut set changes completely from one frame to the next (a
  // storm of single-frame cuts, never held like Storm's), digits are sliced
  // during it, and the final crossed cut spans the screen through the readout.
  {
    const Frame a=frame(Effect::Ravage,1200,cyan),b=frame(Effect::Ravage,1250,cyan);
    unsigned whiteA=0,both=0,sliced=0;
    for(int i=0;i<135*240;++i){
      if(base[i]){sliced+=a[i]!=base[i];continue;}
      const bool wa=a[i]!=base[i] && red(a[i])>12 && green(a[i])>24 && blue(a[i])>12,wb=b[i]!=base[i] && red(b[i])>12 && green(b[i])>24 && blue(b[i])>12;
      whiteA+=wa;both+=wa && wb;
    }
    if(report)std::printf("ravage white %u both %u sliced %u\n",whiteA,both,sliced);
    assert(whiteA>15 && both*5<whiteA && sliced>10);
    const Stats fin=bgStats(frame(Effect::Ravage,1630,cyan));
    if(report)std::printf("ravage final x %d..%d\n",fin.x0,fin.x1);
    assert(fin.x0<8 && fin.x1>126);
  }
  std::cout<<"PASS: five sorcery effects deterministic, halo-clipped, ink-exact by 3/4, custom-background invariant, within the 9 ms cost model, Boundless covering the recap only until it gives it back, with per-effect identity properties\n";
}
