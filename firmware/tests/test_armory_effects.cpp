// The armory set of 0.18 (Swordland..Trace): the shared renderer contract,
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
  const Effect effects[]={Effect::Swordland,Effect::Treasury,Effect::Sunblade,Effect::Aegis,Effect::Trace};
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
  // 80 MHz): every frame at most 8 ms.
  // Each runs about five seconds (owner, 2026-10-20): an early impact, a
  // sustained middle that keeps moving but costs less than the impact, and
  // a settle. Windows in ms: impact from/to, sustain from/to.
  const unsigned windows[5][4]={{500,1250,1400,3600},{700,1400,2100,3500},{1000,1300,1700,3600},{400,1800,2300,3600},{1200,1600,2000,3600}};
  const bool report=std::getenv("ARMORY_COSTS")!=nullptr;
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
    assert(worst<=8000 && sustain<impact);
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
  // Swordland: a flat ring of fire sweeps out across the ground below the
  // readout (wide, not tall), then bronze gears turn (a great wheel under
  // the readout's middle and a half gear at the right edge) above a field
  // of steel blades.
  {
    const Frame ring=frame(Effect::Swordland,650,cyan);
    unsigned fire=0;int fx0=135,fx1=-1,fy0=240,fy1=-1;
    for(int y=47;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || ring[i]==base[i] || red(ring[i])<20 || blue(ring[i])>12 || 2*green(ring[i])<red(ring[i]))continue;
      ++fire;fx0=std::min(fx0,x);fx1=std::max(fx1,x);fy0=std::min(fy0,y);fy1=std::max(fy1,y);}
    if(report)std::printf("swordland fire %u x %d..%d y %d..%d\n",fire,fx0,fx1,fy0,fy1);
    assert(fire>80 && fx1-fx0>110 && fy1-fy0<45 && fy0>46);
    const Frame field=frame(Effect::Swordland,2200,cyan);
    unsigned left=0,right=0,steel=0;
    for(int y=47;y<130;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || field[i]==base[i])continue;
      const bool warm=red(field[i])>=blue(field[i])+4;left+=warm && y<60 && x>40 && x<100;right+=warm && x>124;steel+=!warm && blue(field[i])>=12 && green(field[i])>=24;}
    if(report)std::printf("swordland gears %u %u steel %u\n",left,right,steel);
    assert(left>10 && right>10 && steel>60);
  }
  // Treasury: a wall of golden ripples hangs under the readout with
  // weapons half out of them (gold over a narrow band of rows, steel
  // below each gate); during the volley the weapons strike far below.
  {
    const Frame hold=frame(Effect::Treasury,2600,cyan);
    unsigned gold=0,steel=0,elsewhere=0;
    for(int y=47;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i] || hold[i]==base[i])continue;
      const bool g=red(hold[i])>=20 && green(hold[i])>=28 && blue(hold[i])<=12,st=blue(hold[i])>=18 && green(hold[i])>=36;
      if(y<=43+45){gold+=g;steel+=st;}else elsewhere+=g || st;}
    const Frame volley=frame(Effect::Treasury,1100,cyan);
    unsigned low=0;for(int i=100*135;i<135*240;++i)low+=!base[i] && volley[i]!=base[i];
    if(report)std::printf("treasury gold %u steel %u elsewhere %u low %u\n",gold,steel,elsewhere,low);
    assert(gold>150 && steel>15 && elsewhere*8<gold && low>20);
  }
  // Sunblade: a single upright blade of light stands in one narrow column
  // at the left while it charges; the swing then spreads light across a
  // broad fan from there, and the wave heats the glyphs it crosses.
  {
    const Frame held=frame(Effect::Sunblade,700,cyan);
    unsigned column=0,rest=0;
    for(int i=47*135;i<135*240;++i){if(base[i] || held[i]==base[i])continue;const int x=i%135;if(x>=12 && x<=18)++column;else ++rest;}
    const Stats fan=bgStats(frame(Effect::Sunblade,1150,cyan));
    const Frame wave=frame(Effect::Sunblade,1220,cyan);
    unsigned heated=0;for(int i=0;i<47*135;++i)heated+=base[i] && wave[i]!=base[i];
    if(report)std::printf("sunblade column %u rest %u fan x %d..%d y %d..%d heated %u\n",column,rest,fan.x0,fan.x1,fan.y0,fan.y1,heated);
    assert(column>120 && column>3*rest && fan.x1-fan.x0>100 && fan.y1-fan.y0>60 && heated>40);
  }
  // Aegis: a flower of pink petals blooms; a crimson spear drives in from
  // the right; petals break until a single petal is left standing.
  {
    auto pink=[&](const Frame &f,int x0,int x1){unsigned n=0;for(int y=47;y<240;++y)for(int x=x0;x<x1;++x){const int i=y*135+x;
      if(!base[i] && f[i]!=base[i] && red(f[i])>=16 && blue(f[i])>=8 && 2*green(f[i])<3*red(f[i]) && green(f[i])+4<2*red(f[i]))++n;}return n;};
    const unsigned full=pink(frame(Effect::Aegis,750,cyan),0,135),held=pink(frame(Effect::Aegis,2700,cyan),0,135);
    const Frame drive=frame(Effect::Aegis,1200,cyan);
    unsigned spear=0;for(int y=47;y<240;++y)for(int x=72;x<135;++x){const int i=y*135+x;if(!base[i] && drive[i]!=base[i] && red(drive[i])>=18 && green(drive[i])<16 && blue(drive[i])<12)++spear;}
    if(report)std::printf("aegis full %u held %u spear %u\n",full,held,spear);
    assert(full>150 && held>15 && held*3<full && spear>15);
  }
  // Trace: the blades are first only a blue wireframe (few pixels, blue
  // above red), then fill in solid: many more pixels, a pale blade on the
  // right, all in a narrow band just under the readout.
  {
    const Frame wire=frame(Effect::Trace,1000,cyan),solid=frame(Effect::Trace,2500,cyan);
    unsigned w=0,bluish=0,f=0,pale=0,outside=0;
    for(int y=47;y<240;++y)for(int x=0;x<135;++x){const int i=y*135+x;if(base[i])continue;
      if(wire[i]!=base[i]){++w;bluish+=blue(wire[i])>red(wire[i]);}
      if(solid[i]!=base[i]){++f;pale+=x>67 && red(solid[i])>=20 && green(solid[i])>=44 && blue(solid[i])>=22;outside+=y>43+24;}}
    if(report)std::printf("trace wire %u bluish %u solid %u pale %u outside %u\n",w,bluish,f,pale,outside);
    assert(w>100 && bluish*10>=w*9 && f>2*w && pale>150 && outside*20<f);
  }
  std::cout<<"PASS: five armory effects deterministic, halo-clipped, ink-exact by 3/4, custom-background invariant, within the 8 ms cost model, with per-effect identity properties\n";
}
