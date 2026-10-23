// The ripsaw set of 0.18 (Rev..Sharkfin): the shared renderer contract,
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
  const Effect effects[]={Effect::Rev,Effect::Hammer,Effect::Foxbite,Effect::Detonate,Effect::Sharkfin};
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
  const unsigned windows[5][4]={{800,1600,1800,3700},{1100,1800,1900,3700},{700,1600,1900,3700},{600,1900,2000,3700},{1000,1800,2000,3700}};
  const bool report=std::getenv("RIPSAW_COSTS")!=nullptr;
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
  // Rev: two level bars reach out to both screen edges in a band just under
  // the readout (most of the light within 14 rows of its foot, nearly mirror
  // symmetric about its centre), the chains racing round them change them
  // every frame, and nothing reaches wide before the engine catches.
  {
    const Frame a=frame(Effect::Rev,2000,cyan),b=frame(Effect::Rev,2050,cyan);
    unsigned band=0,all=0,left=0,right=0,moved=0;
    for(int y=44;y<240;++y)for(int x=0;x<135;++x){
      const int i=y*135+x;if(base[i] || a[i]==base[i])continue;
      ++all;if(y>43+14)continue;
      ++band;left+=x<67;right+=x>67;moved+=a[i]!=b[i];
    }
    const Stats s=bgStats(a),pre=bgStats(frame(Effect::Rev,600,cyan));
    if(report)std::printf("rev band %u/%u left %u right %u moved %u x %d..%d pre %d..%d\n",band,all,left,right,moved,s.x0,s.x1,pre.x0,pre.x1);
    assert(band*10>=all*6 && left>80 && right>80 && 5*std::abs(int(left)-int(right))<int(left+right) && moved>30);
    assert(s.x0<12 && s.x1>122 && pre.x1-pre.x0<40);
  }
  // Hammer: a red mass (red far above green and blue) stands left of the
  // centre while raised and while planted, the landing throws a flat ring
  // wide across the floor, and the hammer moves down between the two.
  {
    auto reds=[&](const Frame &f,double &cx,double &cy){unsigned n=0;double sx=0,sy=0;
      for(int i=47*135;i<135*240;++i)if(!base[i] && f[i]!=base[i] && red(f[i])>=14 && green(f[i])<20){++n;sx+=i%135;sy+=i/135;}
      cx=n?sx/n:0;cy=n?sy/n:0;return n;};
    double rx,ry,px,py;const unsigned raised=reds(frame(Effect::Hammer,950,cyan),rx,ry),planted=reds(frame(Effect::Hammer,2200,cyan),px,py);
    const Stats ring=bgStats(frame(Effect::Hammer,1450,cyan));
    if(report)std::printf("hammer raised %u (%.0f,%.0f) planted %u (%.0f,%.0f) ring x %d..%d y %d..%d\n",raised,rx,ry,planted,px,py,ring.x0,ring.x1,ring.y0,ring.y1);
    assert(raised>150 && planted>150 && rx<60 && px<60 && py>ry+10 && ring.x0<10 && ring.x1>110);
  }
  // Foxbite: the hand sign draws itself alone at the right first; the head
  // comes down with two ears rising either side of the centre, red inside
  // and pale outside; after it burns away, wisps of cool foxfire (bluer than
  // red) circle wide under the readout.
  {
    const Stats sign=bgStats(frame(Effect::Foxbite,400,cyan));
    const Frame head=frame(Effect::Foxbite,1300,cyan),fire=frame(Effect::Foxbite,2800,cyan);
    unsigned earL=0,earR=0,redEar=0,cool=0;int coolX0=135,coolX1=-1;
    for(int y=46;y<62;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || head[i]==base[i])continue;
      if(x>=40 && x<64)++earL;if(x>70 && x<=94)++earR;redEar+=red(head[i])>20 && green(head[i])<20;}
    for(int i=46*135;i<135*240;++i)if(!base[i] && fire[i]!=base[i] && blue(fire[i])>red(fire[i])+4){++cool;coolX0=std::min(coolX0,i%135);coolX1=std::max(coolX1,i%135);}
    if(report)std::printf("foxbite sign x %d..%d n %u ears %u/%u red %u cool %u x %d..%d\n",sign.x0,sign.x1,sign.n,earL,earR,redEar,cool,coolX0,coolX1);
    assert(sign.n>20 && sign.x0>85 && earL>25 && earR>25 && 3*std::abs(int(earL)-int(earR))<int(earL+earR) && redEar>10);
    assert(cool>30 && coolX0<35 && coolX1>100);
  }
  // Detonate: before anything goes off only the small pin is drawn, just
  // under the readout's centre; then the blasts go off one side and then the
  // other (the white-hot light's centre swings left, then right), and the
  // last, biggest one lands at the centre.
  {
    const Stats pin=bgStats(frame(Effect::Detonate,200,cyan));
    // White-hot pixels left of x 50 and right of x 84.
    auto hot=[&](unsigned ms,unsigned &left,unsigned &right,unsigned &mid){const Frame f=frame(Effect::Detonate,ms,cyan);left=right=mid=0;
      for(int i=44*135;i<135*240;++i)if(!base[i] && f[i]!=base[i] && red(f[i])>=28 && green(f[i])>=56 && blue(f[i])>=16){const int x=i%135;left+=x<50;right+=x>84;mid+=x>=50 && x<=84;}};
    unsigned l1,r1,m1,l2,r2,m2,l3,r3,m3;hot(850,l1,r1,m1);hot(1030,l2,r2,m2);hot(1460,l3,r3,m3);
    if(report)std::printf("detonate pin %u x %d..%d y %d..%d hot %u/%u/%u %u/%u/%u %u/%u/%u\n",pin.n,pin.x0,pin.x1,pin.y0,pin.y1,l1,m1,r1,l2,m2,r2,l3,m3,r3);
    assert(pin.n>15 && pin.n<120 && pin.x0>60 && pin.x1<90 && pin.y1<60);
    assert(l1>12 && l1>3*r1 && r2>12 && r2>l2 && m3>40 && m3>l3+r3);
  }
  // Sharkfin: before the leap only a small fin and its wake skim the floor
  // (little light high up under the readout); mid-leap a body of light
  // about 40 px long rides high across the centre; after the dive, rings
  // spread wide across the floor while the fin circles again.
  {
    auto high=[&](const Frame &f,Stats &s){unsigned n=0;Stats st;for(int y=46;y<64;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || f[i]==base[i])continue;
      ++n;st.x0=std::min(st.x0,x);st.x1=std::max(st.x1,x);st.y0=std::min(st.y0,y);st.y1=std::max(st.y1,y);}s=st;return n;};
    Stats skim,leap;const unsigned nSkim=high(frame(Effect::Sharkfin,600,cyan),skim),nLeap=high(frame(Effect::Sharkfin,1450,cyan),leap);
    const Stats after=bgStats(frame(Effect::Sharkfin,2900,cyan));
    if(report)std::printf("sharkfin skim %u leap %u x %d..%d after x %d..%d n %u\n",nSkim,nLeap,leap.x0,leap.x1,after.x0,after.x1,after.n);
    assert(nSkim<50 && nLeap>100 && leap.x1-leap.x0>=34 && leap.x1-leap.x0<=60 && std::abs((leap.x0+leap.x1)/2-67)<14);
    assert(after.x1-after.x0>60 && after.n>40);
  }
  std::cout<<"PASS: five ripsaw effects deterministic, halo-clipped, ink-exact by 3/4, custom-background invariant, within the 9 ms cost model, with per-effect identity properties\n";
}
