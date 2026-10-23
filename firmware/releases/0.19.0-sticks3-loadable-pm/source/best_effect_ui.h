#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "appearance.h"
#include "palette.h"

// Personal-best celebrations, drawn into the recap's 16-bit canvas after the
// UI and before it is pushed. Decoration lands only on background pixels (the
// theme's background color; black for the built-ins) at
// least HALO px from every UI pixel, so it reads as a layer behind the UI and
// each label, digit and replay line keeps its pixels and a dark outline. Three
// exceptions: the glint brightens existing header glyph pixels toward white
// without changing their shape; Slash cuts the anchor's ink in two,
// slides the halves apart along the cut and puts them back, so its last
// frames show the readout exactly as drawn; and Boundless covers the rows
// below the anchor's halo for a while (covers()), the anchor keeping its
// pixels and outline, and gives them back exactly.
// Each frame is a pure function of elapsed time with fixed seeds: no heap,
// delay, random input or second framebuffer. The ink mask is the only state.
// Budget: at most about 10 ms per frame at 80 MHz (measured on the device
// with the USB E<id> preview), keeping a celebrating frame near 40 ms of the
// 50 ms cadence. Cost model (modelledUs()), fitted to every frame of every
// effect on the device (HIL !effectframes, 2026-10-20): about 1.1 ms, plus
// 1.8 us per pixel written, 1 us per light probe (an addw past its weight
// test) and 13 us per mask word scanned; per-pixel float math and table
// trigonometry cost several times a fixed-point step. Full-screen work,
// fitted to Boundless's first full-screen cut on the device (2026-10-21,
// 40 ms where the three terms said 7.6): 0.36 us per step of a loop outside
// addw, a pixel visited one at a time or an edge stepped (touches(): 0.29 to
// visit, 0.07 to write), 2.3 us per float square root (roots(), counted by
// root()), and per span fill 1.21 us plus 0.082 us a pixel stored, two per
// 32-bit store (spans(), fills(); fitted to its later cuts, where a 4 KB
// look-up table in flash also cost 5.6 us a read). Every pixel loop, edge
// step, span and root an effect runs must be counted in one of them; check_ui_render holds the model to the recorded
// device frames. So the mask is built per 32-pixel word on first
// use, effects that reach across the screen pay for every word they touch,
// pixel loops are fixed point, trigonometry comes from a table, time-only
// terms are computed once per frame and geometry stays in float, once per
// primitive.
// Loaded effects (fx_effect.h) draw with this renderer's primitives.
namespace LoadableFx {class Engine;struct Math;}
namespace BestEffectUI {
namespace detail {
constexpr double PI_D=3.14159265358979323846;
constexpr double taylorSine(double x){double term=x,sum=x;for(int n=1;n<12;++n){term*=-x*x/((2*n)*(2*n+1));sum+=term;}return sum;}
constexpr std::array<float,257> sineTable() {
  std::array<float,257> t{};
  for(int i=0;i<=256;++i){double a=i*2*PI_D/256;if(a>PI_D)a-=2*PI_D;t[i]=float(taylorSine(a));}
  return t;
}
constexpr std::array<float,257> SINE=sineTable();
constexpr std::array<uint8_t,256> smoothTable() {
  std::array<uint8_t,256> t{};
  for(int i=0;i<256;++i){const double v=i/255.0;t[i]=uint8_t(v*v*(3-2*v)*255+.5);}
  return t;
}
// Smoothstep in Q8, for the fixed-point noise.
constexpr std::array<uint8_t,256> SMOOTH=smoothTable();
}
#define BEST_EFFECT_INLINE inline __attribute__((always_inline))
// BEST_EFFECT_PROFILE (a device debug build only): the CPU cycles of each
// stage of a draw, read with the cycle counter at marks (sorcMark()), for
// HIL `!effectstages`. Compiled out otherwise.
#if defined(BEST_EFFECT_PROFILE) && defined(ESP_PLATFORM)
#include <esp_cpu.h>
#define BEST_EFFECT_CYCLES() uint32_t(esp_cpu_get_cycle_count())
#elif defined(BEST_EFFECT_PROFILE)
#define BEST_EFFECT_CYCLES() uint32_t(0)
#endif
// Row-major with stride == width, RGB565 big-endian as LGFX sprites store it.
struct Surface {uint16_t *pixels=nullptr;int width=0,height=0;};

class Renderer {
  friend class LoadableFx::Engine;friend struct LoadableFx::Math;
public:
  static constexpr int MAX_W=192,MAX_H=240,HALO=2;
  // Header rows searched for the RPM readout; the label sits above, the
  // replay below.
  static constexpr int NUMBER_TOP=13,NUMBER_BOTTOM=46;
  // The ink the effect frames: the recap's RPM readout by default, or another
  // UI element such as a Settings value. Its bounds are searched in this region.
  struct Anchor {int top=NUMBER_TOP,bottom=NUMBER_BOTTOM,left=0,right=MAX_W-1;};
  // Draws nothing once elapsed reaches Appearance::duration(effect). Every
  // pixel not of the theme's `background` color is UI ink.
  void draw(const Surface &s,Appearance::Effect effect,uint32_t elapsed,uint16_t accent){draw(s,effect,elapsed,accent,Anchor());}
  void draw(const Surface &s,Appearance::Effect effect,uint32_t elapsed,uint16_t accent,const Anchor &anchor,uint16_t background=0) {
    plots_=probes_=scans_=fills_=spans_=touches_=roots_=0;bg_=store(background);
#ifdef BEST_EFFECT_PROFILE
    for(auto &s:stage)s=0;
    markAt_=BEST_EFFECT_CYCLES();
#endif
    if(!fits(s) || elapsed>=Appearance::duration(effect))return;
    prepare(s,anchor,accent);
    const float t=elapsed*.001f;
    sorcMark(0);
    using Appearance::Effect;
    // Orbit and Flames stay near the anchor; the rest may use the whole screen.
    switch(effect) {
      case Effect::Orbit:bottom_=std::min(h_-1,number_.y1+21);orbit(t);break;
      case Effect::Flames:bottom_=std::min(h_-1,number_.y1+31);flames(t);break;
      case Effect::Sparkles:bottom_=h_-1;sparkles(t);break;
      case Effect::Shockwave:bottom_=h_-1;shockwave(t);break;
      case Effect::Lightning:bottom_=h_-1;lightning(t);break;
      case Effect::Slash:bottom_=h_-1;slash(t);break;
      case Effect::Tidal:bottom_=h_-1;tidal(t);break;
      case Effect::Frost:bottom_=h_-1;frost(t);break;
      case Effect::Gale:bottom_=h_-1;gale(t);break;
      case Effect::Eclipse:bottom_=h_-1;eclipse(t);break;
      case Effect::Radiance:bottom_=h_-1;radiance(t);break;
      case Effect::Spin:bottom_=h_-1;spin(t);break;
      case Effect::Storm:bottom_=h_-1;storm(t);break;
      case Effect::Quickdraw:bottom_=h_-1;quickdraw(t);break;
      case Effect::Leaves:bottom_=h_-1;leaves(t);break;
      case Effect::Bloom:bottom_=h_-1;bloom(t);break;
      default: {
        // The anime-inspired sets: five per series, each drawn by its own header.
        const unsigned e=unsigned(effect);bottom_=h_-1;
        if(e>=unsigned(Effect::Crimson) && e<=unsigned(Effect::Prism))eva(e-unsigned(Effect::Crimson),t);
        if(e>=unsigned(Effect::Sunwheel) && e<=unsigned(Effect::Butterfly))slayer(e-unsigned(Effect::Sunwheel),t);
        if(e>=unsigned(Effect::Birdsong) && e<=unsigned(Effect::Summon))ninja(e-unsigned(Effect::Birdsong),t);
        if(e>=unsigned(Effect::Breach) && e<=unsigned(Effect::Glitch))nier(e-unsigned(Effect::Breach),t);
        if(e>=unsigned(Effect::Rupture) && e<=unsigned(Effect::Ravage))sorcery(e-unsigned(Effect::Rupture),t);
        // 0.18's six sets, the same way.
        if(e>=unsigned(Effect::Haymaker) && e<=unsigned(Effect::Waterleaf))aura(e-unsigned(Effect::Haymaker),t);
        if(e>=unsigned(Effect::Transmute) && e<=unsigned(Effect::Fracture))alchemy(e-unsigned(Effect::Transmute),t);
        if(e>=unsigned(Effect::Timestop) && e<=unsigned(Effect::Sequel))phantom(e-unsigned(Effect::Timestop),t);
        if(e>=unsigned(Effect::Swordland) && e<=unsigned(Effect::Trace))armory(e-unsigned(Effect::Swordland),t);
        if(e>=unsigned(Effect::Volley) && e<=unsigned(Effect::Meadow))grimoire(e-unsigned(Effect::Volley),t);
        if(e>=unsigned(Effect::Rev) && e<=unsigned(Effect::Sharkfin))ripsaw(e-unsigned(Effect::Rev),t);
        if(e>=unsigned(Effect::Slingshot) && e<=unsigned(Effect::Vapor))corsair(e-unsigned(Effect::Slingshot),t);
        if(e>=unsigned(Effect::Crescent) && e<=unsigned(Effect::Blink))reaper(e-unsigned(Effect::Crescent),t);
        break;
      }
    }
  }
  // The device cost of the primitives a frame is built from, for HIL
  // `!effectbench`: the cycles `clock` (the cycle counter) reads over `n`
  // calls of each. BENCH_LOOP is the bare loop every other one includes. The
  // light lands on `s` (the recap), so the caller restores it afterwards.
  enum Bench:uint8_t {BENCH_LOOP,BENCH_SINE,BENCH_RND,BENCH_ROUNDI,BENCH_FLOORI,BENCH_Q8,BENCH_SMOOTH,BENCH_DIVIDE,
    BENCH_COLOUR,BENCH_ROOT,BENCH_WEAK,BENCH_BLOCKED,BENCH_ADDW,BENCH_PLOT,BENCH_TOUCH,BENCH_OPS};
  static constexpr const char *BENCH_NAMES[BENCH_OPS]={"loop","sine","rnd","roundi","floori","q8","smooth","divide",
    "colour","root","weak","blocked","addw","plot","touch"};
  template<class Clock> bool bench(const Surface &s,uint16_t accent,uint16_t background,uint32_t n,Clock clock,uint32_t cycles[BENCH_OPS]) {
    plots_=probes_=scans_=fills_=spans_=touches_=roots_=0;bg_=store(background);
    if(!fits(s) || n==0)return false;
    prepare(s,Anchor(),accent);bottom_=h_-1;
    // A pixel of the anchor's ink, and open background with no UI near it.
    int ix=-1,iy=-1,ox=-1,oy=-1;
    for(int y=number_.y0;y<=number_.y1 && ix<0;++y)for(int x=number_.x0;x<=number_.x1;++x)if(px_[y*w_+x]!=bg_){ix=x;iy=y;break;}
    for(int y=number_.y1+4*HALO;y<h_ && ox<0;++y)for(int x=8;x<w_-8;++x)if(!blocked(x,y) && !blocked(x+1,y)){ox=x;oy=y;break;}
    if(ix<0 || ox<0)return false;
    volatile float sinkF=0;volatile int sinkI=0;const float inv=1.0f/n;int x=0;
    auto measure=[&](Bench op,auto body){const uint32_t start=clock();for(uint32_t i=0;i<n;++i)body(i);cycles[op]=clock()-start;};
    measure(BENCH_LOOP,[&](uint32_t i){sinkF=sinkF+float(i)*.37f;});
    measure(BENCH_SINE,[&](uint32_t i){sinkF=sinkF+sine(float(i)*.37f);});
    measure(BENCH_RND,[&](uint32_t i){sinkF=sinkF+rnd(i,7);});
    measure(BENCH_ROUNDI,[&](uint32_t i){sinkI=sinkI+roundi(float(i)*.37f);});
    measure(BENCH_FLOORI,[&](uint32_t i){sinkI=sinkI+floori(float(i)*.37f);});
    measure(BENCH_Q8,[&](uint32_t i){sinkI=sinkI+q8(float(i)*inv);});
    measure(BENCH_SMOOTH,[&](uint32_t i){sinkF=sinkF+smooth(float(i)*inv);});
    measure(BENCH_DIVIDE,[&](uint32_t i){sinkF=sinkF+1.0f/(float(i)+1.5f);});
    measure(BENCH_COLOUR,[&](uint32_t i){sinkI=sinkI+inkOf(mixed(WHITE,accent_,float(i)*inv)).g;});
    measure(BENCH_ROOT,[&](uint32_t i){sinkF=sinkF+root(float(i)+1.5f);});
    measure(BENCH_WEAK,[&](uint32_t i){addw(ox,oy,WHITE_INK,int(i&3));});
    measure(BENCH_BLOCKED,[&](uint32_t){addw(ix,iy,WHITE_INK,64);});
    measure(BENCH_ADDW,[&](uint32_t i){addw(ox,oy,WHITE_INK,8+int(i&7));});
    measure(BENCH_PLOT,[&](uint32_t i){plot(ox+.3f,oy-.2f,WHITE,.03f+float(i&7)*.01f);});
    measure(BENCH_TOUCH,[&](uint32_t){++touches_;sinkI=sinkI+(px_[oy*w_+x]!=bg_);if(++x==w_)x=0;});
    return true;
  }
  // Pixels written by the last draw; a portable cost measure for previews.
  uint32_t plots()const{return plots_;}
  // The last draw's other work, for the host cost model: light probes
  // (addw calls past the weight test) and ink mask words scanned.
  uint32_t probes()const{return probes_;}
  uint32_t scans()const{return scans_;}
  // Background pixels set outright by span fills (two per 32-bit store, no
  // per-pixel test): far cheaper per pixel than a plot, so counted apart.
  uint32_t fills()const{return fills_;}
  // Span fills (sorcSpan calls): each has a fixed cost as well as its pixels.
  uint32_t spans()const{return spans_;}
  // Steps of a loop outside addw, one per pixel visited one at a time or per
  // edge step (each about as costly on the device), and float square roots.
  uint32_t touches()const{return touches_;}
  uint32_t roots()const{return roots_;}
  // The last draw's modelled device time in us (see the cost model above),
  // plus the speaker's task on the drawing core while a sound plays
  // (`sounding`: Sound Low against Off, the same frames, 2026-10-22).
  double modelledUs(bool sounding=false)const{
    return 1100+1.8*plots_+probes_+13.0*scans_+.082*fills_+1.21*spans_+.36*touches_+2.3*roots_+(sounding?SOUND_US:0);
  }
  static constexpr double SOUND_US=400;
  // Until when (ms) an effect may hide the UI below its anchor's halo: it
  // then owns those rows outright and gives them back exactly at that time,
  // well before 3/4 of its duration; 0 for every effect that never does.
  // Only Boundless covers (its negative, ink and void).
#ifdef BEST_EFFECT_PROFILE
  // Cycles per stage of the last draw: setup, glint, Boundless's dark, eyes,
  // header mask, disc edges, header fill, covered-row fill, band edges, band
  // fill, detail light (ripples, knots, flare, streams, far stars), stars.
  static constexpr int STAGES=12;
  uint32_t stage[STAGES]={};
#endif
  static constexpr uint32_t BOUNDLESS_COVER=3850;
  static constexpr uint32_t covers(Appearance::Effect e){return e==Appearance::Effect::Boundless?BOUNDLESS_COVER:0;}

private:
  static constexpr unsigned WORDS=(MAX_W+31)/32;
  using Row=std::array<uint32_t,WORDS>;
  struct Rgb {float r,g,b;};  // RGB565 channel scale: 0..31, 0..63, 0..31
  struct Ink {int r,g,b;};    // the same channels in Q8, for pixel loops
  struct Box {int x0,y0,x1,y1;float cx()const{return (x0+x1)*.5f;}float cy()const{return (y0+y1)*.5f;}};
  static constexpr float HALF_TURN=3.14159265f;
  static constexpr Rgb WHITE{31,63,31};
  static constexpr Ink WHITE_INK{31*256,63*256,31*256};

  // Mask words cover 32 pixels of a row and are built on first use per frame;
  // a stamp per word marks the frame that built it.
  std::array<Row,MAX_H> ink_{},blocked_{};
  std::array<std::array<uint8_t,WORDS>,MAX_H> inkStamp_{},blockedStamp_{};
  uint16_t *px_=nullptr,bg_=0;int w_=0,h_=0,bottom_=-1;uint8_t stamp_=0;
  // Charges the cycles since the last mark to stage s (BEST_EFFECT_PROFILE).
#ifdef BEST_EFFECT_PROFILE
  uint32_t markAt_=0;
  void sorcMark(int s){const uint32_t c=BEST_EFFECT_CYCLES();stage[s]+=c-markAt_;markAt_=c;}
#else
  void sorcMark(int){}
#endif
  Box number_{};Rgb accent_{};uint32_t plots_=0,probes_=0,scans_=0,fills_=0,spans_=0,touches_=0,roots_=0;

  // ---- colour and pixels ----------------------------------------------------
  static Rgb rgb(uint16_t c){return {float(c>>11),float((c>>5)&63),float(c&31)};}
  static constexpr Rgb rgb8(int r,int g,int b){return {r*31/255.0f,g*63/255.0f,b*31/255.0f};}
  static Rgb mixed(const Rgb &a,const Rgb &b,float k){return {a.r+(b.r-a.r)*k,a.g+(b.g-a.g)*k,a.b+(b.b-a.b)*k};}
  static constexpr Ink inkOf(const Rgb &c){return {int(c.r*256),int(c.g*256),int(c.b*256)};}
  // k8 in Q8: 0 keeps a, 256 gives b.
  static BEST_EFFECT_INLINE Ink mixedInk(const Ink &a,const Ink &b,int k8){return {a.r+(((b.r-a.r)*k8)>>8),a.g+(((b.g-a.g)*k8)>>8),a.b+(((b.b-a.b)*k8)>>8)};}
  static int q8(float v){return int(v*256);}
  static uint16_t load(uint16_t stored){return uint16_t(stored<<8|stored>>8);}
  static uint16_t store(uint16_t value){return uint16_t(value<<8|value>>8);}
  static float clamp01(float v){return v<0?0:v>1?1:v;}
  // sqrt(v), 0 for v <= 0, by multiplies only (sqrtf is a library call on
  // the device): the bit-level inverse-root estimate and two Newton steps.
  // Still about 2.3 us on the device, so it is counted (roots()).
  float root(float v) {
    if(v<=0)return 0;
    ++roots_;
    uint32_t i;__builtin_memcpy(&i,&v,4);i=0x5f375a86u-(i>>1);float r;__builtin_memcpy(&r,&i,4);
    const float h=.5f*v;r*=1.5f-h*r*r;r*=1.5f-h*r*r;
    return v*r;
  }
  static float easeOut(float v){v=1-clamp01(v);return 1-v*v*v;}
  static float smooth(float v){v=clamp01(v);return v*v*(3-2*v);}
  static int floori(float v){const int i=int(v);return i-(v<i);}
  static int roundi(float v){return floori(v+.5f);}
  static float sine(float a) {
    const float u=a*(256/(2*HALF_TURN));const int i=floori(u);const float f=u-i;const int j=i&255;
    return detail::SINE[j]+(detail::SINE[j+1]-detail::SINE[j])*f;
  }
  static float cosine(float a){return sine(a+HALF_TURN/2);}

  BEST_EFFECT_INLINE bool blocked(int x,int y) {
    if(unsigned(x)>=unsigned(w_) || unsigned(y)>unsigned(bottom_))return true;
    const int i=x>>5;
    if(blockedStamp_[y][i]!=stamp_)buildBlocked(y,i);
    return (blocked_[y][i]>>(x&31))&1u;
  }
  __attribute__((noinline)) void buildBlocked(int y,int i) {
    uint32_t b=0;
    for(int r=std::max(0,y-HALO);r<=std::min(h_-1,y+HALO);++r)b|=ink(r,i);
    blocked_[y][i]=b;blockedStamp_[y][i]=stamp_;
  }
  // Additive light on a background pixel, w in Q8 (256 = full); saturates.
  BEST_EFFECT_INLINE void addw(int x,int y,const Ink &c,int w) {
    if(w<5)return;
    ++probes_;if(blocked(x,y))return;
    // A neighbouring word's mask reads HALO pixels into this word: build it
    // now, from UI pixels only, before this light lands within its reach.
    const int edge=x&31;
    if(edge<HALO){if(x>=32)ink(y,(x>>5)-1);}
    else if(edge>=32-HALO && (x|31)+1<w_)ink(y,(x>>5)+1);
    uint16_t &p=px_[y*w_+x];const unsigned v=load(p);
    const int r=std::min(31,int(v>>11)+((c.r*w+32768)>>16)),g=std::min(63,int((v>>5)&63)+((c.g*w+32768)>>16)),b=std::min(31,int(v&31)+((c.b*w+32768)>>16));
    p=store(uint16_t(r<<11|g<<5|b));++plots_;
  }
  void add(int x,int y,const Rgb &c,float k){addw(x,y,inkOf(c),q8(k));}
  void plot(float x,float y,const Rgb &c,float k){addw(roundi(x),roundi(y),inkOf(c),q8(k));}
  // Sub-pixel point split over four pixels, so slow motion stays smooth at 20 fps.
  void dot(float x,float y,const Rgb &col,float k) {
    const int ix=floori(x),iy=floori(y),ax=q8(x-ix),ay=q8(y-iy),kq=q8(k);const Ink c=inkOf(col);
    addw(ix,iy,c,(kq*(256-ax)*(256-ay))>>16);addw(ix+1,iy,c,(kq*ax*(256-ay))>>16);
    addw(ix,iy+1,c,(kq*(256-ax)*ay)>>16);addw(ix+1,iy+1,c,(kq*ax*ay)>>16);
  }
  // Hot white core with a coloured halo.
  void spark(float x,float y,const Rgb &col,float k,float radius) {
    const int r=int(radius+.999f),ix=roundi(x),iy=roundi(y),kq=q8(k);const float inv=1/(radius*radius);
    const Ink c=inkOf(col);
    touches_+=uint32_t((2*r+1)*(2*r+1));
    for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx) {
      const float ex=ix+dx-x,ey=iy+dy-y;const int f=q8(1-(ex*ex+ey*ey)*inv);
      if(f>0)addw(ix+dx,iy+dy,mixedInk(c,WHITE_INK,(f*f)>>8),(kq*f)>>8);
    }
  }
  // One pixel per step: for fast strokes, where sub-pixel smoothing is invisible.
  void line(float x0,float y0,float x1,float y1,const Rgb &c,float k) {
    const int n=std::max(1,int(std::max(std::fabs(x1-x0),std::fabs(y1-y0))));
    const float sx=(x1-x0)/n,sy=(y1-y0)/n;
    for(int i=0;i<=n;++i)plot(x0+sx*i,y0+sy*i,c,k);
  }
  // Pull the header glyphs (label and RPM) toward white inside a diagonal band.
  void glint(float t,float start,float length) {
    const float u=(t-start)/length;if(u<=0 || u>=1)return;
    // From the label just above the anchor (the recap's NEW BEST) to its foot.
    const int top=std::max(0,number_.y0-18),bottom=number_.y1+1;
    const float travel=number_.x1-number_.x0+(bottom-top+1)*.45f+28,head=number_.x0-14+easeOut(u)*travel;
    // The sheen pushes ink away from the background: toward white when the
    // ink is lighter (every built-in theme), toward black on a light custom
    // background, so it never washes the readout out.
    const unsigned bgv=load(bg_);const int bgLight=2*(bgv>>11)+((bgv>>5)&63)+2*(bgv&31);
    for(int y=top;y<=bottom;++y) {
      const float centre=head-(y-number_.cy())*.45f;
      const int c8=q8(centre);
      for(int x=std::max(0,int(centre-7));x<=std::min(w_-1,int(centre+7));++x) {
        ++touches_;
        uint16_t &p=px_[y*w_+x];if(p==bg_)continue;
        // Band weight (1-|x-centre|/7)*0.9 in Q8.
        const int k=((7*256-std::abs(x*256-c8))*33)>>8;if(k<=0)continue;
        const unsigned v=load(p);const int r=v>>11,g=(v>>5)&63,b=v&31;
        if(2*r+g+2*b>=bgLight)p=store(uint16_t((r+(((31-r)*k+128)>>8))<<11|(g+(((63-g)*k+128)>>8))<<5|(b+(((31-b)*k+128)>>8))));
        else p=store(uint16_t((r-((r*k+128)>>8))<<11|(g-((g*k+128)>>8))<<5|(b-((b*k+128)>>8))));
        ++plots_;
      }
    }
  }

  // ---- deterministic randomness --------------------------------------------
  static uint32_t hash(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
  static float rnd(uint32_t i,uint32_t salt){return hash(i*0x9e3779b9u^salt)*(1.0f/4294967296.0f);}
  static BEST_EFFECT_INLINE int lattice(int x,int y,uint32_t salt) {
    uint32_t h=uint32_t(x)*0x27d4eb2du^uint32_t(y)*0x165667b1u^salt*0x9e3779b9u;h^=h>>15;h*=0x2c1b3c6du;h^=h>>12;
    return int(h&255);
  }
  // Value noise in fixed point: coordinates in Q8 (256 = one lattice cell), result 0..255.
  static int noise(int x,int y,uint32_t salt) {
    const int ix=x>>8,iy=y>>8,u=detail::SMOOTH[x&255],v=detail::SMOOTH[y&255];
    const int a=lattice(ix,iy,salt),b=lattice(ix+1,iy,salt),c=lattice(ix,iy+1,salt),d=lattice(ix+1,iy+1,salt);
    const int top=a+(((b-a)*u)>>8),bottom=c+(((d-c)*u)>>8);
    return top+(((bottom-top)*v)>>8);
  }

  // ---- UI mask ---------------------------------------------------------------
  // A pixel is blocked when any non-background pixel lies within HALO px (a
  // square). Mask words are built on first use each frame, so thin strokes
  // read only the pixels around them; a word is always built before
  // decoration is written to it or within HALO px of it (addw), so
  // decoration never masks itself.
  BEST_EFFECT_INLINE uint32_t ink(int y,int i) {
    if(inkStamp_[y][i]!=stamp_){ink_[y][i]=scanWord(y,i);inkStamp_[y][i]=stamp_;++scans_;}
    return ink_[y][i];
  }
  // Ink of pixels 32i..32i+31 of row y, widened by HALO px: reads the
  // word's pixels and HALO more on each side; each ink pixel sets five bits.
  // The slice is copied out in one memcpy and skipped a pixel pair at a time
  // where both are background: measured on the device, a word scan read
  // pixel by pixel cost about 17 us and this one about 13 us, and an effect
  // that reaches across the screen scans a few hundred words per frame.
  __attribute__((noinline)) uint32_t scanWord(int y,int i)const {
    static_assert(HALO==2,"the five-bit stamp below is 2*HALO+1 wide");
    const int first=i*32,from=std::max(0,first-HALO),to=std::min(w_-1,first+31+HALO),n=to-from+1;
    uint32_t pairs[(32+2*HALO+1)/2+1];pairs[n/2]=uint32_t(bg_)*0x10001u;
    std::memcpy(pairs,px_+y*w_+from,size_t(n)*2);
    const uint16_t *p=reinterpret_cast<const uint16_t *>(pairs);const uint32_t both=uint32_t(bg_)*0x10001u;
    uint32_t bits=0;
    for(int k=0;k<n;k+=2) {
      if(pairs[k>>1]==both)continue;
      for(int x=k;x<k+2 && x<n;++x)if(p[x]!=bg_) {
        const int d=from+x-first;bits|=d>=HALO?0x1fu<<(d-HALO):0x1fu>>(HALO-d);
      }
    }
    return bits;
  }
  // The anchor's ink bounds frame every effect; each row stops at its first
  // ink pixel from either side.
  bool fits(const Surface &s)const{return s.pixels && s.width>=64 && s.width<=MAX_W && s.height>=NUMBER_BOTTOM+HALO+1 && s.height<=MAX_H;}
  void prepare(const Surface &s,const Anchor &anchor,uint16_t accent) {
    px_=s.pixels;w_=s.width;h_=s.height;
    // A new stamp invalidates every cached mask row without clearing them.
    if(++stamp_==0){for(auto &r:inkStamp_)r.fill(0);for(auto &r:blockedStamp_)r.fill(0);stamp_=1;}
    findNumber(anchor);
    accent_=rgb(accent);
  }
  void findNumber(const Anchor &a) {
    const int top=std::max(0,a.top),bottom=std::min(h_-1,a.bottom),first=std::max(0,a.left),last=std::min(w_-1,a.right);
    int x0=w_,y0=h_,x1=-1,y1=-1;
    for(int y=top;y<=bottom;++y) {
      const uint16_t *p=px_+y*w_;int left=first;
      while(left<=last && p[left]==bg_)++left;
      if(left>last)continue;
      int right=last;while(p[right]==bg_)--right;
      x0=std::min(x0,left);x1=std::max(x1,right);y0=std::min(y0,y);y1=y;
    }
    number_=x1>=0?Box{x0,y0,x1,y1}:Box{first+10,top+6,last-10,std::max(top+6,bottom-5)};
  }

  // ---- Orbit: two comets spin up around the readout, then light the ring ----
  void orbitPoint(float a,float scale,float &x,float &y)const {
    x=number_.cx()+(w_*.5f-3)*scale*cosine(a);y=number_.cy()+23*scale*sine(a);
  }
  // The full ellipse, brighter on the near (lower) side. Stepped every
  // other pixel: at track brightness the gaps read as a dotted rail.
  void ellipse(float scale,const Rgb &c,float k) {
    for(int i=0;i<190;++i){float x,y;const float a=i*(2*HALF_TURN/190);orbitPoint(a,scale,x,y);plot(x,y,c,k*(.55f+.45f*sine(a)));}
  }
  void orbit(float t) {
    constexpr float SPIN=1.05f,LAPS=2.3f,SETTLE=4.0f;
    // Angle: quadratic spin-up to SPIN, then a long sustain whose rate sags
    // slowly, then a faster exponential run-down in the settle. Each leg is
    // the closed-form integral of its rate, so the angle stays continuous.
    constexpr float RATE=4*HALF_TURN*LAPS/SPIN,K1=.22f,K2=1.2f;
    float angle,speedF; // speedF: the current rate as a fraction of RATE
    if(t<SPIN){const float s=t/SPIN;angle=2*HALF_TURN*LAPS*s*s;speedF=s;}
    else if(t<SETTLE){const float a=t-SPIN;angle=2*HALF_TURN*LAPS+RATE*(1-std::exp(-K1*a))/K1;speedF=std::exp(-K1*a);}
    else{const float aS=SETTLE-SPIN,b=t-SETTLE,fS=std::exp(-K1*aS);
      angle=2*HALF_TURN*LAPS+RATE*(1-fS)/K1+RATE*fS*(1-std::exp(-K2*b))/K2;speedF=fS*std::exp(-K2*b);}
    angle-=HALF_TURN/2;
    const float speed=RATE*speedF;
    const float fade=1-smooth((t-4.15f)/.75f);
    glint(t,SPIN-.05f,.45f);
    // A faint track gives the ring its perspective; it lingers, dimmer,
    // through the sustain while the comets keep turning on it.
    const float trackK=t<SPIN?.2f*smooth(t/.2f):.13f*(1-smooth((t-SPIN)/3.0f)+.25f);
    if(fade>0)ellipse(1,accent_,trackK*fade);
    if(fade>0)for(int comet=0;comet<2;++comet) {
      const float head=angle+comet*HALF_TURN,tail=std::min(2.2f,.35f+speed*.075f);
      const int n=std::max(4,int(tail*38));
      for(int i=n;i>=0;--i) {
        const float u=float(i)/n,a=head-tail*u,ca=cosine(a),sa=sine(a);
        const float x=number_.cx()+(w_*.5f-3)*ca,y=number_.cy()+23*sa;
        const float k=fade*(.6f+.4f*sa)*(1-u)*(1-u);const Rgb c=mixed(WHITE,accent_,std::min(1.0f,u*2.5f));
        // One plot per point, a softer outer edge every other point: the old
        // four-probe dot plus edge cost about three times this.
        plot(x,y,c,k*1.5f);if(!(i&1))plot(x+ca*.9f,y+sa*.9f,c,k*.7f);
      }
      float x,y;orbitPoint(head,1,x,y);twinkle(x,y,accent_,fade*(1.1f+.3f*sine(head)),1);
    }
    if(t<SPIN)return;
    // Lock-in: the whole ring flashes once and expands a little.
    if(t<SPIN+.5f) {
      const float e=(t-SPIN)/.5f,flash=(1-e)*(1-e);
      const Rgb c=mixed(WHITE,accent_,std::min(1.0f,e*2.2f));
      ellipse(1+.06f*easeOut(e),c,flash);
      for(unsigned i=0;i<18;++i) {
        const float a=i*2*HALF_TURN/18+rnd(i,1)*.3f,v=35+45*rnd(i,2),life=.3f+.25f*rnd(i,3),age=t-SPIN;
        if(age>=life)continue;
        float x,y;orbitPoint(a,1,x,y);const float d=v*easeOut(age/life)*.5f,k=1-age/life;
        const float ux=cosine(a),uy=sine(a)*.6f;
        plot(x+ux*d,y+uy*d,mixed(WHITE,accent_,age/life),k);plot(x+ux*(d-2),y+uy*(d-2),accent_,k*.5f);
      }
    }
    // Sustain: the ring slowly dissolves. Dust motes are shed from the ring
    // one by one, drift outward and along the orbit, and wink out, so the
    // ring thins into drifting specks while the comets still turn.
    for(unsigned i=0;i<14;++i) {
      const float birth=1.3f+2.3f*rnd(i,4),life=1.1f+.8f*rnd(i,5),age=t-birth;
      if(age<0 || age>=life)continue;
      const float a0=2*HALF_TURN*rnd(i,6),u=age/life;
      float x,y;orbitPoint(a0+.35f*age,1+.1f*easeOut(u),x,y);
      const float k=fade*sine(HALF_TURN*u);
      plot(x+cosine(a0)*7*easeOut(u),y+sine(a0)*3*easeOut(u)-3*u,mixed(WHITE,accent_,u),k*1.1f);
      plot(x,y,accent_,k*.5f);
    }
  }

  // ---- Flames: fire rises behind the readout, then burns down to embers ------
  void flames(float t) {
    // Full blaze to 1.3 s, then the fire settles to a lower, slower burn
    // for the sustain and dies down to embers in the last second.
    const float grow=easeOut(t/.32f);
    const float level=t<1.3f?1.0f:t<3.9f?1-.35f*smooth((t-1.3f)/1.2f):.65f*(1-smooth((t-3.9f)/1.05f));
    const float height=grow*level;
    glint(t,.3f,.5f);
    const int base=std::min(bottom_-2,number_.y1+16);
    // Two noise octaves scroll upward; time offsets in Q8 noise units. The
    // sustain scrolls a little slower, like a fire burning lower.
    const float scrollT=t<1.3f?t:1.3f+(t-1.3f)*.7f;
    const int rise1=int(scrollT*80*.10f*256),rise2=int(scrollT*130*.21f*256),sway=int(scrollT*1.7f*256);
    if(height>0.01f)for(int x=0;x<w_;x+=2) {
      // 2x2 cells, one flame tongue per column; noise only below its tip.
      // Shorter tongues and a higher heat cutoff than the 1.8 s version:
      // about a third fewer cells pass, which is most of the saving.
      const int tongue=int(height*(10+19*noise(x*18,sway,11)*(1/255.0f))),inv=65536/std::max(1,tongue);
      for(int y=base;y>=0 && base-y<=tongue+2;y-=2) {
        ++touches_;
        const int rise=base-y;
        // heat=((1.12-rise/tongue)(.45+.85n)-.1)*smooth(rise/12), all in Q8;
        // the smoothstep fades the bottom rows so the fire has no hard floor.
        auto heatAt=[&](int n){return ((((287-((rise*inv)>>8))*(115+((218*n)>>8)))>>8)-26)*detail::SMOOTH[std::min(255,rise*21)]>>8;};
        // The hottest the cell can be (noise 0 or 255, whichever is hotter):
        // a cell that cannot pass needs neither its mask nor its noise.
        if(std::max(heatAt(0),heatAt(255))<=34)continue;
        if(blocked(x,y) && blocked(x+1,y) && blocked(x,y+1) && blocked(x+1,y+1))continue;
        const int n=(158*noise(x*31,y*26+rise1,12)+97*noise(x*67,y*54+rise2,13))>>8;
        int heat=heatAt(n);
        if(heat<=34)continue;
        heat=std::min(255,heat);
        const Ink c=fire(heat);const int k=std::min(256,(heat*666)>>8);
        // Cool cells are dithered (two diagonal pixels): the flame edges
        // were already noise, and this halves their probe count.
        if(heat>110){addw(x,y,c,k);addw(x+1,y,c,k);addw(x,y+1,c,k);addw(x+1,y+1,c,k);}
        else{addw(x,y,c,k);addw(x+1,y+1,c,k);}
      }
    }
    // Embers rise for as long as the fire lives: each slot recycles with a
    // new column per cycle, thinning as the fire dies in the settle.
    const float emberK=grow*(t<4.35f?1:1-smooth((t-4.35f)/.65f));
    if(emberK>0)for(unsigned i=0;i<18;++i) {
      const float life=.8f+.6f*rnd(i,22),clock=t+rnd(i,25)*7.0f;
      const unsigned cycle=unsigned(clock/life);const float age=clock-cycle*life;
      if(rnd(i*13+cycle,26)>.25f+.75f*emberK)continue;
      const unsigned seed=i*29+cycle*7;
      const float x=4+(w_-8)*rnd(seed,23)+3*sine(age*9+seed),y=base-4-age*(38+34*rnd(seed,24)),u=age/life;
      const Rgb c=mixed(rgb8(255,214,120),rgb8(220,60,0),u);
      plot(x,y,c,(1-u)*emberK);plot(x,y+2,c,(1-u)*emberK*.45f);
    }
  }
  // Heat 0..255 to a black-red-orange-yellow-white ramp. constexpr: a
  // function-local static with a runtime initializer takes a mutex per call.
  static Ink fire(int heat) {
    static constexpr Ink ramp[]={inkOf(rgb8(0,0,0)),inkOf(rgb8(110,8,0)),inkOf(rgb8(225,48,0)),inkOf(rgb8(255,138,12)),inkOf(rgb8(255,214,80)),inkOf(rgb8(255,250,210))};
    const int p=heat*5,i=std::min(4,p>>8);return mixedInk(ramp[i],ramp[i+1],p-(i<<8));
  }

  // ---- Sparkles: a confetti burst from the readout and twinkling stars ------
  void sparkles(float t) {
    glint(t,.12f,.45f);
    const Rgb gold=rgb8(255,208,72),colors[]={accent_,WHITE,gold};
    // Linear drag with gravity, closed form; the time terms are shared by all particles.
    constexpr float DRAG=1.6f,G=170;
    float travel[3],fall[3];
    for(int j=0;j<3;++j){const float at=std::max(0.0f,t-.025f*j);travel[j]=(1-std::exp(-DRAG*at))/DRAG;fall[j]=G/DRAG*(travel[j]-at);}
    for(unsigned i=0;i<56;++i) {
      const float life=1.0f+.55f*rnd(i,31);if(t>=life)continue;
      const float a=2*HALF_TURN*rnd(i,32),speed=110+140*rnd(i,33),ca=cosine(a),sa=sine(a);
      const float x0=number_.cx()+ca*(number_.x1-number_.x0)*.42f,y0=number_.cy()+sa*(number_.y1-number_.y0)*.42f;
      const float vx=ca*speed,vy=sa*speed*.8f-55;
      const float k=t<life*.55f?1:1-(t-life*.55f)/(life*.45f),flicker=.7f+.3f*sine(t*38+i*2.4f);
      const Rgb c=colors[i%3];
      plot(x0+vx*travel[2],y0+vy*travel[2]+fall[2],c,k*.4f);
      dot(x0+vx*travel[1],y0+vy*travel[1]+fall[1],c,k*.75f);
      spark(x0+vx*travel[0],y0+vy*travel[0]+fall[0],c,k*flicker*1.5f,2.1f);
    }
    // Twinkling stars keep appearing sparsely right through the sustain,
    // each seeded to its own moment, so no pattern visibly repeats.
    const float lateFade=1-smooth((t-4.35f)/.55f);
    for(unsigned i=0;i<26;++i) {
      const float birth=.1f+3.9f*rnd(i,41),age=t-birth,life=.6f+.3f*rnd(i,46);
      if(age<0 || age>=life)continue;
      const bool header=i%13<6;
      const float x=header?6+(w_-12)*rnd(i,42):(i%2?5+12*rnd(i,43):w_-6-12*rnd(i,43));
      const float y=header?(rnd(i,44)<.5f?number_.y0-15+6*rnd(i,45):number_.y1+5+12*rnd(i,45)):60+150*rnd(i,44);
      star(x,y,sine(HALF_TURN*age/life)*lateFade,i%2?gold:accent_);
    }
    // Sustain: a slow rain of glitter settles out of the burst. Motes are
    // shed around the readout one by one, flutter down with a sway and a
    // hard flicker, and wink out near where they land.
    for(unsigned i=0;i<18;++i) {
      const float birth=.85f+2.7f*rnd(i,47),life=1.5f+.5f*rnd(i,48),age=t-birth;
      if(age<0 || age>=life)continue;
      const float u=age/life;
      const float x=6+(w_-12)*rnd(i,49)+4*sine(age*5+i*1.9f),y=number_.y1+6+90*rnd(i,50)+(16+12*rnd(i,51))*age;
      const float k=lateFade*sine(HALF_TURN*u)*(.65f+.35f*sine(t*31+i*2.7f));
      const Rgb c=colors[i%3];
      plot(x,y,c,k*1.3f);plot(x,y-2,c,k*.5f);
    }
  }
  void star(float x,float y,float size,const Rgb &c) {
    const float arm=1+8*size;
    for(int i=1;i<=int(arm);++i){const float k=size*(1-i/(arm+1));
      plot(x+i,y,c,k);plot(x-i,y,c,k);plot(x,y+i,c,k);plot(x,y-i,c,k);
      if(i<=arm*.45f){plot(x+i*.7f,y+i*.7f,c,k*.5f);plot(x-i*.7f,y+i*.7f,c,k*.5f);plot(x+i*.7f,y-i*.7f,c,k*.5f);plot(x-i*.7f,y-i*.7f,c,k*.5f);}}
    spark(x,y,c,size*1.3f,2.2f);
  }

  // A soft elliptical glow behind the readout, visited row span by row span.
  // The costliest primitive: every canvas row it covers is read and written.
  void glow(float rx,float ry,const Rgb &col,float k) {
    const float cx=number_.cx(),cy=number_.cy();const Ink c=inkOf(col);const int kq=q8(k);
    // Distances in half pixels, squared distance in Q16 of the radius.
    const int cx2=roundi(cx*2),irx=int(65536/(4*rx*rx));
    for(int y=std::max(0,int(cy-ry));y<=std::min(bottom_,int(cy+ry));++y) {
      const float dy=(y-cy)/ry;const int dy2=int(dy*dy*65536);if(dy2>=65536)continue;
      const float span=rx*std::sqrt(1-dy*dy);
      for(int x=std::max(0,int(cx-span));x<=std::min(w_-1,int(cx+span)+1);++x) {
        const int dx=2*x-cx2,d=dx*dx*irx+dy2;if(d>=65536)continue;
        const int f=(65536-d)>>8;addw(x,y,c,(((f*f)>>8)*kq)>>8);
      }
    }
  }

  // ---- Shockwave: an impact flash, burst lines and two rings that sweep the screen
  void shockwave(float t) {
    glint(t,.06f,.42f);
    const float cx=number_.cx(),cy=number_.cy();
    // A smaller, shorter flash than the old 42x22 glow: about a third of
    // its filled area, and gone before the rings reach the edges.
    // The impact flash as two expanding ellipse strokes, not a filled
    // glow: a filled ellipse scans every mask word it covers.
    if(t<.18f){const float fk=.9f*(1-t/.18f)*(1-t/.18f);ellipseAt(cx,cy,20+90*t,11+48*t,mixed(accent_,WHITE,.4f),fk,2.2f);ellipseAt(cx,cy,13+60*t,7+32*t,WHITE,fk*.7f,2.2f);}
    for(unsigned i=0;i<8;++i) {
      const float start=.05f*rnd(i,51),age=t-start,life=.26f;if(age<0 || age>=life)continue;
      const float a=2*HALF_TURN*(i+.5f*rnd(i,52))/8,ux=cosine(a),uy=sine(a),u=age/life;
      const float r0=24+120*easeOut(u),len=22*(1-u)+4;
      line(cx+ux*r0,cy+uy*r0*.75f,cx+ux*(r0+len),cy+uy*(r0+len)*.75f,mixed(WHITE,accent_,u),(1-u)*.95f);
    }
    ring(t,0,.95f,1,250);ring(t,.18f,.85f,.55f,250);
    // Sustain: three echo ripples roll out one by one, each weaker and
    // reaching less far so its arcs stay on screen, drawn with a fourth
    // band so they read as a solid stroke where they cross open
    // background. Under them the last ring lingers around the readout as
    // a slow pulse, so no moment between echoes is empty, while dust
    // knocked loose by the blast drifts outward, catching the light as
    // it settles. The round 3 echoes (strength .2-.3, reach up to 190)
    // spent most of their life off-screen and were invisible at 1x.
    const float endFade=1-smooth((t-4.5f)/.4f);
    // The echoes never overlap: two large rings at once would scan the
    // mask words of most of the screen twice over. The pulse covers the
    // short gaps between them.
    ring(t,1.1f,1.15f,.7f*endFade,120,4);ring(t,2.35f,1.15f,.6f*endFade,105,4);ring(t,3.6f,1.15f,.5f*endFade,90,4);
    if(t>.8f) {
      const float env=smooth((t-.8f)/.3f)*(1-smooth((t-4.05f)/.55f))*(1-.15f*smooth((t-.8f)/3.0f));
      if(env>.01f) {
        const float beat=sine((t-.8f)*4.4f),pulse=.85f+.15f*beat;
        const float rx=(number_.x1-number_.x0)*.5f+9+1.5f*beat,ry=(number_.y1-number_.y0)*.5f+10+beat;
        ellipseAt(cx,cy,rx,ry,mixed(accent_,WHITE,.35f),env*pulse*.95f,2.2f);
      }
    }
    for(unsigned i=0;i<20;++i) {
      const float birth=.05f+1.5f*rnd(i,53),life=2.4f+1.4f*rnd(i,54),age=t-birth;
      if(age<0 || age>=life)continue;
      const float a=2*HALF_TURN*rnd(i,55),v=16+34*rnd(i,56),u=age/life,ca=cosine(a),sa=sine(a);
      const float d=v*age*(1-.08f*age);
      const float x=cx+ca*d,y=cy+sa*d*.75f+6*age;
      const float k=endFade*sine(HALF_TURN*u)*(.7f+.3f*sine(t*17+i*2.3f));
      const Rgb c=mixed(WHITE,accent_,u);
      plot(x,y,c,k*1.25f);plot(x-ca*2,y-sa*1.5f,c,k*.55f);
    }
  }
  // An expanding ring as three concentric strokes. The 1.3 s version filled
  // the annulus by scanning its whole bounding box row by row - a few
  // thousand probes and hundreds of mask words per frame. The strokes
  // touch only the words they cross, and off-screen points are skipped
  // before they cost a probe.
  void ring(float t,float start,float life,float strength,float reach,int bands=3) {
    const float u=(t-start)/life;if(u<=0 || u>=1)return;
    const float cx=number_.cx(),cy=number_.cy(),r=10+reach*easeOut(u),width=3+5*u,k=strength*(1-u)*(1-u*.2f)*1.3f;
    if(k<=.02f)return;
    const Ink accent=inkOf(accent_);
    // The sustain echoes pass bands=4: one more stroke inside the same
    // width, so the fainter ring still reads as a solid band. The main
    // rings keep the original three strokes and weights.
    static constexpr float OFF3[]={0,.3f,.65f},F3[]={.45f,1,.6f};
    static constexpr int MIX3[]={170,70,170};
    static constexpr float OFF4[]={0,.22f,.45f,.7f},F4[]={.5f,1,.85f,.55f};
    static constexpr int MIX4[]={170,60,120,190};
    const float *off=bands==4?OFF4:OFF3,*bf=bands==4?F4:F3;
    const int *mix=bands==4?MIX4:MIX3,nb=bands==4?4:3;
    // The four-band echoes step a little wider (about 2 px, like the main
    // rings at their cap) to pay for the extra stroke.
    const int n=bands==4?std::min(300,std::max(24,int(r*2.6f))):std::min(460,std::max(24,int(r*3.8f)));const float step=2*HALF_TURN/n;
    // Along each ray the strokes move outward from the readout, which is on
    // screen, so where the innermost stroke is off screen every stroke is:
    // only the arcs where it can be on screen are stepped (on the device
    // the off-screen points of a large ring cost about 2 ms a frame).
    int arcs[6][2];const int narcs=ringArcs(cx,cy,std::max(1.0f,r-width*off[nb-1]),.72f,n,arcs);
    for(int arc=0;arc<narcs;++arc)for(int i=arcs[arc][0];i<=arcs[arc][1];++i) {
      const float a=i*step,ca=cosine(a),sa=sine(a);
      for(int band=0;band<nb;++band) {
        ++touches_;
        const float rr=r-width*off[band];if(rr<1)continue;
        // Squashed to the screen's perspective, like the burst lines: the
        // ring touches about a quarter fewer mask rows than a circle.
        const float x=cx+rr*ca,y=cy+rr*sa*.72f;
        if(x<0 || x>=w_ || y<0 || y>bottom_)continue;
        addw(roundi(x),roundi(y),mixedInk(WHITE_INK,accent,mix[band]),q8(k*bf[band]));
      }
    }
  }

  // The steps i (of n round the circle) where a point at radius r from
  // (cx, cy), squashed vertically by `squash`, can be on screen, as up to
  // six increasing ranges with two steps of margin; the count is returned.
  int ringArcs(float cx,float cy,float r,float squash,int n,int (&arcs)[6][2])const {
    constexpr float TURN=2*HALF_TURN;
    // Angles where x = cx + r cos a lies on the screen, then y = cy + r squash sin a.
    float xa[2][2],ya[3][2];int nx=0,ny=0;
    const float c0=-cx/r,c1=(w_-cx)/r,s0=-cy/(r*squash),s1=(bottom_+1-cy)/(r*squash);
    if(c0<=-1 && c1>=1){xa[0][0]=0;xa[0][1]=TURN;nx=1;}
    else {
      const float lo=std::acos(std::min(1.0f,std::max(-1.0f,c1))),hi=std::acos(std::min(1.0f,std::max(-1.0f,c0)));
      xa[0][0]=lo;xa[0][1]=hi;xa[1][0]=TURN-hi;xa[1][1]=TURN-lo;nx=2;
    }
    if(s0<=-1 && s1>=1){ya[0][0]=0;ya[0][1]=TURN;ny=1;}
    else {
      const float lo=std::asin(std::min(1.0f,std::max(-1.0f,s0))),hi=std::asin(std::min(1.0f,std::max(-1.0f,s1)));
      // sin a in [s0, s1]: a in [lo, hi] and [pi - hi, pi - lo], kept in
      // 0..2pi (a range below 0 wraps to the top end).
      auto add=[&](float a0,float a1){if(a0<=a1){ya[ny][0]=a0;ya[ny][1]=a1;++ny;}};
      if(hi<0)add(TURN+lo,TURN+hi);
      else if(lo<0){add(0,hi);add(TURN+lo,TURN);}
      else add(lo,hi);
      add(HALF_TURN-hi,HALF_TURN-lo);
    }
    const float perStep=n/TURN;int count=0;
    for(int i=0;i<nx;++i)for(int j=0;j<ny;++j) {
      const float a0=std::max(xa[i][0],ya[j][0]),a1=std::min(xa[i][1],ya[j][1]);if(a0>a1)continue;
      const int s=std::max(0,int(a0*perStep)-2),e=std::min(n-1,int(a1*perStep)+2);
      arcs[count][0]=s;arcs[count][1]=e;++count;
    }
    // In order, without overlaps, so no point is stepped twice.
    for(int k=1;k<count;++k)for(int j=k;j>0 && arcs[j-1][0]>arcs[j][0];--j){std::swap(arcs[j-1][0],arcs[j][0]);std::swap(arcs[j-1][1],arcs[j][1]);}
    int merged=0;
    for(int k=0;k<count;++k) {
      if(merged && arcs[k][0]<=arcs[merged-1][1]+1)arcs[merged-1][1]=std::max(arcs[merged-1][1],arcs[k][1]);
      else{arcs[merged][0]=arcs[k][0];arcs[merged][1]=arcs[k][1];++merged;}
    }
    return merged;
  }

  // ---- Lightning: bolts strike from the readout down behind the replay ------
  void lightning(float t) {
    static constexpr float strikes[]={0,.24f,.5f,.86f},power[]={1,.8f,.95f,.6f};
    glint(t,.02f,.4f);
    for(unsigned s=0;s<4;++s) {
      // Strikes never overlap: each one's afterglow ends as the next begins.
      const float age=t-strikes[s];if(age<0 || age>=.24f)continue;
      // Return-stroke flicker: bright, dip, re-flash, then a fading afterglow.
      const float k=power[s]*(age<.05f?1:age<.08f?.3f:age<.13f?.85f:.85f*(1-(age-.13f)/.11f));
      // The flash glow only on the two strongest strikes, and smaller: the
      // old 42x22 glow on every strike was most of this effect's probes.
      if(age<.05f && (s==0 || s==2))ellipseAt(number_.cx(),number_.cy(),22,12,mixed(accent_,WHITE,.4f),.7f*power[s]*(1-age/.05f),2.2f);
      for(unsigned b=0;b<2;++b) {
        const uint32_t seed=s*2+b;const bool left=(s+b)%2==0;
        const float x0=left?number_.x0+8+12*rnd(seed,61):number_.x1-8-12*rnd(seed,61),y0=number_.y1+3;
        // Bolts end behind the replay instead of near the screen foot:
        // about half the stroke steps and mask rows, same strike read.
        // Only the first bolt branches; the second is a plainer companion.
        const float x1=left?4+30*rnd(seed,62):w_-5-30*rnd(seed,62),y1=number_.y1+42+58*rnd(seed,63);
        bolt(x0,y0,x1,y1,seed,k,b==0?5:4);
      }
    }
    // Sustain: the storm recedes. Three distant single bolts strike low at
    // the screen edges, fainter and shorter each time, while static keeps
    // crackling along the readout's edges at a whisper and charged dust
    // drifts up and winks out.
    static constexpr float farStrikes[]={1.7f,2.65f,3.5f,4.15f},farPower[]={.45f,.35f,.26f,.18f};
    for(unsigned s=0;s<4;++s) {
      const float age=t-farStrikes[s];if(age<0 || age>=.24f)continue;
      const float k=farPower[s]*(age<.05f?1:age<.08f?.3f:age<.13f?.85f:.85f*(1-(age-.13f)/.11f));
      const uint32_t seed=40+s;const bool left=s%2==0;
      const float x0=left?6+22*rnd(seed,64):w_-7-22*rnd(seed,64),y0=number_.y1+34+20*rnd(seed,65);
      bolt(x0,y0,x0+(left?-4:4)-14*(rnd(seed,66)-.5f)*2,y0+52+36*rnd(seed,67),seed,k,4);
    }
    // Between strikes, arcs crackle along the readout's top and bottom
    // edges: strong while the storm is overhead, a faint static after.
    bool striking=false;for(unsigned s=0;s<4;++s)striking|=t>=strikes[s] && t<strikes[s]+.1f;
    if(t<4.85f && !striking)for(unsigned i=0;i<2;++i) {
      const uint32_t frame=uint32_t(t*20),seed=frame*4+i;
      if(rnd(seed,71)<(t<1.25f?.35f:.35f))continue;
      const float y=i?number_.y1+3.5f:number_.y0-3.5f,x=number_.x0+(number_.x1-number_.x0-36)*rnd(seed,72);
      const float ck=t<1.25f?.75f*(1-t/1.25f):.38f*(1-smooth((t-4.2f)/.65f));
      bolt(x,y,x+20+16*rnd(seed,73),y,seed+1000,ck,3);
    }
    const float endFade=1-smooth((t-4.45f)/.5f);
    for(unsigned i=0;i<16;++i) {
      const float birth=1.15f+3.1f*rnd(i,74),life=1.1f+.7f*rnd(i,75),age=t-birth;
      if(age<0 || age>=life)continue;
      const float u=age/life,x=8+(w_-16)*rnd(i,76)+3*sine(age*4+i),y=number_.y1+70*rnd(i,77)-14*age;
      plot(x,y,mixed(accent_,WHITE,.35f),endFade*sine(HALF_TURN*u)*(.6f+.4f*sine(t*27+i*3.1f)));
    }
  }
  // Midpoint displacement, fixed depth; one branch per bolt at full depth.
  void bolt(float x0,float y0,float x1,float y1,uint32_t seed,float k,int depth) {
    float xs[33],ys[33];const int n=1<<depth;xs[0]=x0;ys[0]=y0;xs[n]=x1;ys[n]=y1;
    for(int step=n;step>1;step/=2)for(int i=0;i+step<=n;i+=step) {
      const int m=i+step/2;const float dx=xs[i+step]-xs[i],dy=ys[i+step]-ys[i];
      const float off=(rnd(seed*64+m,81)-.5f)*.55f;
      xs[m]=(xs[i]+xs[i+step])*.5f-dy*off;ys[m]=(ys[i]+ys[i+step])*.5f+dx*off;
    }
    const Rgb glow=mixed(accent_,WHITE,.15f);
    for(int i=0;i<n;++i)segment(xs[i],ys[i],xs[i+1],ys[i+1],glow,k);
    if(depth==5) {
      const int from=8+int(14*rnd(seed,82));const float dx=x1-x0,dy=y1-y0,side=rnd(seed,83)<.5f?-1:1;
      bolt(xs[from],ys[from],xs[from]+dx*.35f+side*dy*.25f,ys[from]+dy*.35f-side*dx*.12f,seed+500,k*.55f,4);
    }
  }
  // ---- Slash: a blade streak cuts the readout, the halves slide apart and back
  // The cut rises to the right through the anchor's centre: direction
  // (C,-S), the upper half on the side of normal (-S,-C), tan = 0.3.
  // Fixed point: S8/C8 in Q8, side values in half pixels times Q8.
  static constexpr int S8=73,C8=245;
  static constexpr float SLOPE=.3f,COS=C8/256.0f,SIN=S8/256.0f;
  float cutY(float x)const{return number_.cy()-(x-number_.cx())*SLOPE;}
  void slash(float t) {
    constexpr float SWING=.12f,SPLIT=.16f,OPEN=.3f,CLOSE=.95f,SHUT=.35f,FUSE=.18f;
    const float shut=CLOSE+SHUT;
    // Halves: easeOut apart, a slow drift while open, smooth back together.
    // They part across the cut more than they slide along it, so the
    // readout stays on screen and reads as cut in two.
    float along=0;
    if(t>=SPLIT && t<shut) {
      const float open=easeOut((t-SPLIT)/OPEN)*(1+.08f*std::min(1.0f,(t-SPLIT)/(CLOSE-SPLIT)));
      along=3.2f*open*(t<CLOSE?1:1-smooth((t-CLOSE)/SHUT));
    }
    const float apart=std::min(3.2f,along);
    // The blade's head crosses the screen along the cut line, fast.
    const float sweep=easeOut(t/SWING),head=-24+(w_+48)*sweep;
    // Hot edges: glyph pixels along the cut glow behind the blade and cool
    // down; when the halves meet again the seam flashes once.
    int heat=t<.32f?q8(1-smooth((t-.1f)/.22f)):0;
    if(t>=shut-.06f && t<shut+FUSE)heat=q8(.55f*(1-smooth((t-shut+.06f)/(FUSE+.06f))));
    if(heat>0 || along>0)cut(along,apart,heat,t<SWING?roundi(head):w_);
    blade(t,head);
    streak(t,head,SWING);
    // Sparks fly off both edges of the cut while the halves open.
    const Rgb gold=rgb8(255,214,110),colors[]={WHITE,accent_,gold};
    for(unsigned i=0;i<18;++i) {
      const float birth=SPLIT-.06f+.22f*rnd(i,91),age=t-birth,life=.35f+.3f*rnd(i,92);
      if(age<0 || age>=life)continue;
      const float x0=number_.x0+(number_.x1-number_.x0)*rnd(i,93),y0=cutY(x0),side=i%2?1:-1;
      // Out along the normal, with some of the blade's direction and gravity.
      const float speed=70+110*rnd(i,94),drift=40+90*rnd(i,95);
      const float vx=-side*SIN*speed+COS*drift,vy=-side*COS*speed-SIN*drift;
      const float x=x0+vx*age,y=y0+vy*age+120*age*age,k=1-age/life;
      const Rgb c=colors[i%3];
      plot(x-vx*.025f,y-vy*.025f-6*age,c,k*.45f);
      twinkle(x,y,c,k*1.3f,1);
    }
    // Sustain: the cut is closed, but its seam glows in the air beside
    // the digits while it cools - white at the fuse, through the accent,
    // still a clear line at about 3.8 s, gone by 4.5 s. It is drawn only
    // through addw, so the halo keeps it off the readout's own pixels
    // and every glyph is exact from the fuse on. A shimmer travels along
    // it and twice a brighter blade glint sweeps its length (background
    // only, never the glyph-brightening glint()). Embers are shed mostly
    // off the seam's ends, where the line runs in open background, and
    // drop clear of the glyph band. The round 3 afterimage (one dotted
    // row at strength .3) was almost invisible and read as over at 1.5 s.
    if(t>shut) {
      const float endFade=1-smooth((t-4.0f)/.5f);
      const float cool=smooth((t-shut)/1.3f);
      const float seamK=(1-.5f*smooth((t-shut)/2.6f))*endFade;
      if(seamK>.01f) {
        const Ink seam=inkOf(mixed(WHITE,accent_,.15f+.85f*cool));
        const Ink side=inkOf(mixed(WHITE,accent_,.35f+.65f*cool));
        // The two glint sweeps' boosted segment heads, if any is live.
        float head1=-1e3f,head2=-1e3f;
        if(t>=2.3f && t<2.8f)head1=-10+(w_+20)*easeOut((t-2.3f)/.5f);
        if(t>=3.35f && t<3.85f)head2=-10+(w_+20)*easeOut((t-3.35f)/.5f);
        for(int x=2;x<w_-2;++x) {
          const int y=roundi(cutY(float(x)));
          float boost=1;
          const float d=std::min(head1-x,head2-x),dMax=std::max(head1-x,head2-x);
          if(dMax>=0 && dMax<26)boost=1+.9f*(1-dMax/26);
          else if(d>=0 && d<26)boost=1+.9f*(1-d/26);
          const int kq=q8(seamK*(.8f+.35f*sine(x*.09f-t*3.1f))*boost);
          addw(x,y,seam,kq);addw(x,y-1,side,(kq*102)>>8);addw(x,y+1,side,(kq*102)>>8);
        }
      }
      for(unsigned i=0;i<22;++i) {
        const float birth=shut+.05f+2.5f*rnd(i,96),life=1.0f+.6f*rnd(i,97),age=t-birth;
        if(age<0 || age>=life)continue;
        const float x0=i%3==0?4+22*rnd(i,98):i%3==1?w_-5-22*rnd(i,98):8+(w_-16)*rnd(i,98),side=i%2?1:-1,u=age/life;
        // Embers drop clear of the glyph band quickly, so they stay
        // visible below the readout instead of hiding in its halo.
        const float x=x0+(rnd(i,99)*8-4)*age,y=cutY(x0)-side*COS*10*age+62*age*age;
        const float k=endFade*sine(HALF_TURN*u)*(.75f+.35f*sine(t*23+i*2.9f));
        plot(x,y,colors[i%3],k*1.35f);plot(x,y-2,colors[i%3],k*.6f);
      }
      for(unsigned i=0;i<5;++i) {
        const float birth=shut+.3f+2.9f*rnd(i,100),age=t-birth;
        if(age<0 || age>=.5f)continue;
        const float x=14+(w_-28)*rnd(i,101);
        twinkle(x,cutY(x),mixed(WHITE,accent_,.25f),endFade*sine(HALF_TURN*age/.5f)*1.15f,2);
      }
    }
  }
  // The blade's line across the readout itself, over its glyphs: white behind
  // the head, then fading in the opening cut.
  void blade(float t,float head) {
    if(t>=.26f)return;
    const int k=q8(t<.16f?1:1-smooth((t-.16f)/.1f));
    for(int x=std::max(0,number_.x0-2);x<=std::min({w_-1,number_.x1+2,int(head)});++x) {
      const int y=roundi(cutY(float(x)));if(y<0 || y>=h_)continue;
      uint16_t &p=px_[y*w_+x];p=heated(p,WHITE_INK,k);++plots_;
    }
  }
  // The streak: a white core with an accent glow along the cut line, fading
  // toward its tail, then an afterimage across the screen.
  void streak(float t,float head,float swing) {
    const float fade=t<swing?1:1-smooth((t-swing)/.17f);
    if(fade<=0)return;
    const float tail=t<swing?head-70:-24;
    const Ink core=WHITE_INK,edge=inkOf(accent_),halo=inkOf(mixed(accent_,WHITE,.3f));
    for(int x=std::max(0,int(tail));x<=std::min(w_-1,int(head));++x) {
      const float u=t<swing?(head-x)/70.0f:0;
      const float k=fade*(1-u)*(1-u)*(t<swing?1:.65f);
      const int kq=q8(k),y=roundi(cutY(float(x)));
      addw(x,y,core,kq);addw(x,y-1,edge,(kq*150)>>8);addw(x,y+1,edge,(kq*150)>>8);
      addw(x,y-2,halo,(kq*50)>>8);addw(x,y+2,halo,(kq*50)>>8);
    }
  }
  // Cuts the anchor's ink along the line: the upper half moves by
  // along*(C,-S)+apart*(-S,-C), the lower half the opposite way. In place:
  // each half is copied in the order that reads every source pixel before
  // it can be overwritten, and only onto its own side of the cut (or
  // outside the anchor, where only ink is copied). Glyph pixels within two
  // pixels of the cut, left of `reach`, are pulled toward a hot white.
  void cut(float along,float apart,int heat,int reach) {
    const int dx=roundi(along*COS-apart*SIN),dy=roundi(-along*SIN-apart*COS);
    const Ink hot=inkOf(mixed(WHITE,accent_,.3f));
    move(dx,dy,true,hot,heat,reach);move(-dx,-dy,false,hot,heat,reach);
  }
  void move(int dx,int dy,bool upper,const Ink &hot,int heat,int reach) {
    const Box &b=number_;
    const int cx2=b.x0+b.x1,cy2=b.y0+b.y1;
    const int top=std::max(0,std::min(b.y0,b.y0+dy)),bottom=std::min(h_-1,std::max(b.y1,b.y1+dy));
    const int left=std::max(0,std::min(b.x0,b.x0+dx)),right=std::min(w_-1,std::max(b.x1,b.x1+dx));
    // A source row below its destination is read first going down, and so on.
    const int ystep=dy<0?1:-1,xstep=dx>0?-1:1;
    const int shift=2*(dx*S8+dy*C8);
    for(int y=ystep>0?top:bottom;y>=top && y<=bottom;y+=ystep) {
      const int sy=y-dy;const bool rowIn=y>=b.y0 && y<=b.y1,srcRow=sy>=b.y0 && sy<=b.y1;
      // Only this half's columns: the destination on its side, or a source
      // on its side. The cut crosses x where (2x-cx2)*S8 = -(2y-cy2)*C8.
      const int cutX=(cx2*S8-(2*y-cy2)*C8)/(2*S8),srcCutX=(cx2*S8-(2*sy-cy2)*C8)/(2*S8)+dx;
      const int edge=std::max(cutX,srcCutX);
      const int from=upper?left:std::max(left,std::min(cutX,srcCutX)-1),to=upper?std::min(right,edge+1):right;
      // Every pixel of this half's span is visited, written or not.
      touches_+=uint32_t(std::max(0,to-from+1));
      for(int x=xstep>0?from:to;x>=from && x<=to;x+=xstep) {
        const int side=(2*x-cx2)*S8+(2*y-cy2)*C8,sx=x-dx;
        // Upper half: side < 0; lower: side >= 0. The source is side-shift.
        const bool destIn=rowIn && x>=b.x0 && x<=b.x1,destMine=upper?side<0:side>=0;
        const int was=side-shift;
        const bool source=srcRow && sx>=b.x0 && sx<=b.x1 && (upper?was<0:was>=0);
        uint16_t &p=px_[y*w_+x];
        if(source) {
          uint16_t v=px_[sy*w_+sx];
          if(v!=bg_ && heat>0 && sx<=reach) {
            const int d=std::abs(was);
            if(d<640)v=heated(v,hot,(heat*(256-((d*2)/5)))>>8);
          }
          // Skip the write when the pixel already holds the value: the
          // readout is drawn in flat colours, so most copied pixels are
          // unchanged by the move and need not be written (or counted).
          if((destIn?destMine:v!=bg_) && p!=v){p=v;++plots_;}
        } else if(destIn && destMine && p!=bg_){p=bg_;++plots_;}
      }
    }
  }
  static uint16_t heated(uint16_t stored,const Ink &c,int k) {
    const unsigned v=load(stored);const int r=v>>11,g=(v>>5)&63,b=v&31;
    const int tr=c.r>>8,tg=c.g>>8,tb=c.b>>8;
    return store(uint16_t((r+(((tr-r)*k)>>8))<<11|(g+(((tg-g)*k)>>8))<<5|(b+(((tb-b)*k)>>8))));
  }

  // ---- Elemental effects, defined in best_effect_elemental.h ----------------
  void tidal(float t);
  void frost(float t);
  void gale(float t);
  void eclipse(float t);
  void radiance(float t);
  // shadeAnchor never takes the readout below this contrast on the background.
  static constexpr uint32_t READOUT_CONTRAST100=300;
  void shadeAnchor(const Ink &target,int k8,bool lighten);
  void leanAnchor(float shear);
  void ellipseAt(float cx,float cy,float rx,float ry,const Rgb &c,float k,float density);
  void vdot(int x,float y,const Ink &c,int kq);
  void twinkle(float x,float y,const Rgb &c,float k,int arm);
  void fern(float x,float y,float dx,float dy,float length,float grown,int depth,uint32_t seed,const Rgb &c,float k,float from=0);

  // ---- Physical effects, defined in best_effect_physical.h ------------------
  void spin(float t);
  void storm(float t);
  void quickdraw(float t);
  void leaves(float t);
  void bloom(float t);

  // ---- Evangelion-inspired effects, defined in best_effect_eva.h: Crimson..Prism --------
  void eva(unsigned which,float t);
  void evaSphere(float t);
  void evaBarrier(float t);
  void evaCross(float t);
  void evaAlert(float t);
  void evaBeam(float t);
  void evaStroke(float x0,float y0,float x1,float y1,const Ink &c,int kq,bool clip=false);
  void evaOctagon(float cx,float cy,float r,const Ink &c,int kq,bool clip=false);
  void evaTriangle(float x0,float y0,float x1,float y1,float x2,float y2,const Ink &c,int kq);
  void evaLighten(int x,int y,int k8);
  void evaHazard(int y,int x0,int x1,int scroll,const Ink &stripe,int kq);
  void evaText(const char *text,int x,int y,const Ink &c,int kq);
  bool evaHidden(int x,int y)const;
  void evaAdd(int x,int y,const Ink &c,int w);
  void evaPlot(float x,float y,const Rgb &c,float k);
  void evaDot(float x,float y,const Rgb &c,float k);
  void evaVdot(int x,float y,const Ink &c,int kq);
  void evaSpark(float x,float y,const Rgb &c,float k,float radius);
  void evaEllipse(float cx,float cy,float rx,float ry,const Rgb &c,float k,float density);
  void evaTwinkle(float x,float y,const Rgb &c,float k,int arm);

  // ---- Demon Slayer-inspired effects, defined in best_effect_slayer.h: Sunwheel..Butterfly --------
  void slayer(unsigned which,float t);
  void sunwheel(float t);
  void torrent(float t);
  void blitz(float t);
  void tigerclaw(float t);
  void butterfly(float t);
  // A woodblock brush: each band strokes the knot polyline offset across it.
  struct Band {float offset;Ink ink;int k;};
  void lit(int x,int y,const Ink &c,int w);
  void inkLine(int x0,int y0,int x1,int y1,const Ink &c,int k0,int k1);
  void brush(const float *x,const float *y,const float *hw,const int *k,int n,const Band *bands,int nb);
  void tongue(float x,float y,float angle,float length,float width,float hook,int k,const Band *bands,int nb);
  void band(float cx,float cy,float r,float width,float from,float sweep,const Ink *zones,int nz,int k);

  // ---- Naruto-inspired effects, defined in best_effect_ninja.h: Birdsong..Summon --------
  void ninja(unsigned which,float t);
  void ninjaCharge(float t);
  void ninjaSpiral(float t);
  void ninjaEye(float t);
  void ninjaGates(float t);
  void ninjaSummon(float t);
  void ninjaArc(float x0,float y0,float x1,float y1,uint32_t seed,const Rgb &glow,float k,int depth,float rough);
  void ninjaOrb(float cx,float cy,float r,float squash,float spin,float k);
  void ninjaGlow(float cx,float cy,float r,const Rgb &c,float k);
  void ninjaIris(int ex,int ey,float open,float slit,float phi,float m,float u,int flash);

  // ---- NieR:Automata-inspired effects, defined in best_effect_nier.h: Breach..Glitch --------
  void nier(unsigned which,float t);
  void nierBreach(float t);
  void nierFlurry(float t);
  void nierMoon(float t);
  void nierBarrage(float t);
  void nierGlitch(float t);
  void nierAdd(int x,int y,const Ink &c,int w);
  void nierRing(float cx,float cy,float rx,float ry,const Ink &c,int kq);
  void nierOrb(float x,float y,const Ink &c,int kq);
  void nierSquare(int x,int y,int half,const Ink &c,int kq,bool filled);
  void nierLine(int x0,int y0,int x1,int y1,const Ink &c,int kq,int yMin,int yMax);
  void nierCrescent(const float *arc,float h,float tail,float width,float k,const Ink &edge,const Ink &body,float dx,float dy);
  void nierPetal(float fx,float fy,float phi,float length,float width,float rise,float curl,const Ink &edge,const Ink &body,int kq);
  void nierTear(int y0,int y1,int shift);

  // ---- Jujutsu Kaisen-inspired effects, defined in best_effect_sorcery.h: Rupture..Ravage --------
  void sorcery(unsigned which,float t);
  void sorcRupture(float t);
  void sorcBoundless(float t);
  void sorcPrecision(float t);
  void sorcUmbra(float t);
  void sorcRavage(float t);
  void sorcLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1);
  void sorcRimBolt(float x0,float y0,float x1,float y1,uint32_t seed,const Ink &rim,const Ink &outer,int k,int depth);
  void sorcShiftRow(int y,int d);
  void sorcPrime(int y0,int y1);
  void sorcHeatLine(float x0,float y0,float x1,float y1,const Ink &target,int k8);
  void sorcHeat(int x,int y,const Ink &target,int k8);
  uint16_t sorcShade(unsigned v,const Ink &target,int k8)const;
  void sorcRing(float s,const Ink &c,int kq,bool split);
  struct SorcDisc {float x,y,r;};
  void sorcSplotch(const SorcDisc *ink,int inks,const SorcDisc *holes,int nholes,int y0,int y1,uint16_t v,int raw,int under);
  void sorcSpan(uint16_t *row,int x0,int x1,uint16_t sv);
  void sorcEyes(int raw,float cx,float ey,float open,float glow,bool negative);
  uint16_t sorcNegate(uint16_t stored)const;
  void sorcLight(int x,int y,const Ink &c,int w);
  uint16_t sorcVoidColor(const Rgb &l,float f)const;
  void sorcVoidLight(int x,int y,const Ink &c,int w,int raw,bool above=true);
  void sorcVoidLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1,int raw,bool above=true);
  void sorcVoidField(int raw,float cx,float cy,float rx,float ry,const float *rho,const Rgb *light,int n,float lum);

  // ---- 0.18's sets, one header each (the inspiration of each effect is in
  // docs/FIRMWARE_CELEBRATION_EFFECTS.md). Each set's helpers are declared
  // in its own block, prefixed with the set's name.
  // best_effect_aura.h: Haymaker..Waterleaf
  void aura(unsigned which,float t);
  void auraHaymaker(float t);
  void auraPalms(float t);
  void auraElastic(float t);
  void auraChains(float t);
  void auraWaterleaf(float t);
  // best_effect_alchemy.h: Transmute..Fracture
  void alchemy(unsigned which,float t);
  void alchemyTransmute(float t);
  void alchemyIgnition(float t);
  void alchemySpires(float t);
  void alchemyGate(float t);
  void alchemyFracture(float t);
  // best_effect_phantom.h: Timestop..Sequel
  void phantom(unsigned which,float t);
  void phantomTimestop(float t);
  void phantomZipper(float t);
  void phantomRush(float t);
  void phantomMenace(float t);
  void phantomSequel(float t);
  // best_effect_armory.h: Swordland..Trace
  void armory(unsigned which,float t);
  void armorySwordland(float t);
  void armoryTreasury(float t);
  void armorySunblade(float t);
  void armoryAegis(float t);
  void armoryTrace(float t);
  // best_effect_grimoire.h: Volley..Meadow
  void grimoire(unsigned which,float t);
  void grimoireVolley(float t);
  void grimoireStarfall(float t);
  void grimoireMimic(float t);
  void grimoireScales(float t);
  void grimoireMeadow(float t);
  // best_effect_ripsaw.h: Rev..Sharkfin
  void ripsaw(unsigned which,float t);
  void ripsawRev(float t);
  void ripsawHammer(float t);
  void ripsawFoxbite(float t);
  void ripsawDetonate(float t);
  void ripsawSharkfin(float t);
  // best_effect_corsair.h: Slingshot..Vapor
  void corsair(unsigned which,float t);
  void corsairSlingshot(float t);
  void corsairPresence(float t);
  void corsairThreefold(float t);
  void corsairBanner(float t);
  void corsairVapor(float t);
  // best_effect_reaper.h: Crescent..Blink
  void reaper(unsigned which,float t);
  void reaperCrescent(float t);
  void reaperRelease(float t);
  void reaperPetalfall(float t);
  void reaperMask(float t);
  void reaperBlink(float t);
  // ---- set helpers: aura
  void auraPath(const float *xy,int n,float ox,float oy,float sx,float sy,const Ink &c,int kq);
  void auraFlame(float cx,float cy,float rx,float ry,float rise,int scroll,uint32_t salt,const Ink &c,int kq);
  void auraHeatSpot(float cx,float cy,float rx,float ry,const Ink &target,int k8);
  void auraSprite(const struct AuraMask &m,int w,int h,float cx,float cy,int scale,bool mirror,const Ink &rim,const Ink &body,int kRim,int kBody);
  void auraStretchReadout(float d);
  void auraLink(float x,float y,bool vertical,bool open,const Ink &c,const Ink &hi,int kq);
  void auraGum(float ax,float ay,float ex,float ey,float bow,float hum,float w0,float wMid,float w1,int kq);
  void auraBlob(float cx,float cy,float rx,float ry,int kq);
  // ---- set helpers: alchemy
  void alchemyLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1);
  void alchemyArc(float cx,float cy,float r,float a0,float sweep,const Ink &c,int kq);
  float alchemyPath(const float *xs,const float *ys,int n,float p,const Ink &c,int kq,float &hx,float &hy);
  void alchemyFireball(float fx,float fy,float r,int cool,int boost,int scroll);
  void alchemyHand(float sx,float sy,float tx,float ty,float bend,float reach,float wave,float grip,const Ink &rim,int kq);
  void alchemyZap(float x0,float y0,float x1,float y1,uint32_t seed,const Ink &c,int kq);
  void alchemyCrackle(float cx,float cy,float r,float a0,float span,float out,uint32_t seed,const Ink &core,const Ink &glow,int kq);
  // ---- set helpers: phantom
  // Whether this frame paints strokes as ink (a light background), set
  // once per frame by phantom() so the pixel path does not recompute it.
  bool phantomInk_=false;
  bool phantomPaper()const;
  void phantomAdd(int x,int y,const Ink &c,int w);
  void phantomDot(float x,float y,const Ink &c,int kq);
  void phantomLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1);
  void phantomTwinkle(float x,float y,const Ink &c,int kq,int arm);
  void phantomEllipse(float cx,float cy,float rx,float ry,const Ink &c,int kq,float from=0,float sweep=2*HALF_TURN);
  void phantomKnife(float gx,float gy,float dx,float dy,const Ink &blade,const Ink &hilt,int kq,float shine);
  void phantomPut(int x,int y,uint16_t value);
  void phantomTeeth(int x0,int x1,int upper,int lower,int k8,bool overlay);
  void phantomPull(float x,float y,float swing,int k8,bool overlay);
  void phantomFist(float x,float y,int dx,int dy,int scale,const Ink &edge,const Ink &fill,int k8,bool arm);
  void phantomPunch(float x,float y,bool mirror,int scale,bool ghost,const Ink &tint,int k8);
  void phantomGlyph(int x,int y,int shape,int scale,const Ink &edge,const Ink &fill,const Ink &shade,int k8);
  void phantomArrow(float x,float y,int length,const Ink &edge,const Ink &top,const Ink &bottom,int k8,float shine);
  void phantomTint(const Ink &target,int k8,int parity);
  void phantomBurst(float cx,float cy,float r0,float r1,int spikes,float turn,const Ink &c,int k8,float fromA,float toA);
  // ---- set helpers: armory
  void armoryPlot(int x,int y,const Ink &c,int kq);
  void armoryTo(int &lx,int &ly,int x,int y,const Ink &c,int kq);
  void armoryGear(float cx,float cy,float r,int teeth,float phase,const Ink &rim,const Ink &hub,int kq,bool spokes);
  void armorySword(float x,float y,float angle,float height,float depth,float hide,const Ink &steel,const Ink &edge,const Ink &back,const Ink &hilt,int kq);
  void armoryRing(float cx,float cy,float rx,float ry,const Ink &c,int kq);
  void armoryWeapon(float px,float py,float dx,float dy,float out,float from,float len,int kind,const Ink &steel,const Ink &gold,int kq);
  void armoryStroke(float x0,float y0,float x1,float y1,const Ink &c,int kq);
  void armoryArc(float cx,float cy,float r,float from,float to,const Ink &c,int kq);
  void armoryPetal(float cx,float cy,float angle,float len,float width,float squash,const Ink &edge,const Ink &vein,int kq,bool veined=true);
  // ---- set helpers: grimoire
  static float grimoireRsqrt(float v);
  static float grimoireRecip(int n);
  void grimoireAdd(int x,int y,const Ink &c,int w);
  void grimoireTwinkle(float x,float y,const Rgb &c,float k,int arm);
  void grimoireLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1,bool last=true);
  void grimoireArc(float cx,float cy,float rx,float ry,float from,float sweep,const Ink &c,int kq);
  void grimoireSigil(float cx,float cy,float r,float spin,float open,const Ink &rim,const Ink &rune,int kq);
  void grimoireBeam(float x0,float y0,float x1,float y1,float head,float tail,const Ink &core,const Ink &side,int kq,int rows);
  // ---- set helpers: ripsaw
  void ripsawBar(float rx,float ry,int side,float len,float phase,float k,const Ink &steel,const Ink &chain);
  float ripsawChain(float t,float &speed)const;
  void ripsawPuff(float x,float y,float dx,float dy,float age,float life,float size,const Ink &c,float k);
  void ripsawFill(const float *xs,const float *ys,int n,const Ink &c,int kq);
  void ripsawHammerAt(float px,float py,float theta,float k,float fill,float sheen);
  void ripsawOval(float cx,float cy,float rx,float ry,const Ink &c,int kq,float wobble,float spin,float density);
  void ripsawPath(const float *pts,int n,float ox,float oy,float sx,float sy,const Ink &c,int kq,float drawn=1);
  void ripsawFoxHead(float hx,float hy,float sx,float sy,float open,float k,float dissolve);
  void ripsawWisp(float x,float y,float size,float t,uint32_t seed,const Ink &core,const Ink &flame,float k);
  void ripsawBlast(float cx,float cy,float size,float age,uint32_t seed,float smokeLife);
  void ripsawFin(float x,float y,float dir,float scale,float rise,float k);
  void ripsawShark(float x,float y,float ux,float uy,float k,int floorY);
  void ripsawSplash(float x,float y,float age,float size,uint32_t seed);
  static void ripsawNoiseRow(int xq,int stepq,int n,int yq,uint32_t salt,uint8_t *out);
  static int ripsawSpan(int limit,int ey2,int &mx);
  // ---- set helpers: corsair
  bool corsairInBox(int x,int y)const{const Box &b=number_;return x>=b.x0-2 && x<=b.x1+2 && y>=b.y0-2 && y<=b.y1+2;}
  void corsairLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1);
  static float corsairInvLen(float dx,float dy);
  void corsairArm(float sx,float sy,float fx,float fy,float hw,float wobble,float spin,const Ink &edge,const Ink &fill,const Ink &core,int kq);
  void corsairFist(float x,float y,float dx,float dy,float r,const Ink &edge,const Ink &fill,int kq);
  void corsairStar(float x,float y,float r,float inner,int points,uint32_t seed,const Ink &c,const Ink &glow,int kq);
  void corsairFlash(float x,float y,float r,float squash,const Ink &c,int kq);
  void corsairRing(float cx,float cy,float rx,float ry,const Ink &c,int kq);
  void corsairFocus(float cx,float cy,int lines,uint32_t seed,float reach,const Ink &c,int kq);
  void corsairWave(float cx,float cy,float rx,float ry,const Ink &front,const Ink &back,int kq,uint32_t seed);
  void corsairBlackBolt(float x0,float y0,float x1,float y1,uint32_t seed,const Ink &rim,const Ink &glow,int kq,float rough);
  void corsairCut(float cx,float cy,float angle,float half,float width,const Ink &core,const Ink &glow,int kq,float gleam);
  void corsairBlade(float cx,float cy,float angle,float half,const Ink &edge,const Ink &steel,int kq,float gleam);
  void corsairFlush(const Ink &target,int k8,int y0,int y1);
  void corsairWisp(float x0,float y0,float height,float w0,float w1,float sway,float phase,const Ink &c,int kq);
  // ---- set helpers: reaper
  void reaperAdd(int x,int y,const Ink &c,int w);
  void reaperVdot(int x,float y,const Ink &c,int kq);
  void reaperHdot(float x,int y,const Ink &c,int kq);
  void reaperTongue(float bx,float by,float tx,float ty,float half,const Ink &c,int kq);
  static float reaperRsqrt(float v);

  // White core with a glow on the two sides across the stroke.
  void segment(float x0,float y0,float x1,float y1,const Rgb &glowColor,float k) {
    const float dx=x1-x0,dy=y1-y0;const bool steep=std::fabs(dy)>std::fabs(dx);
    const int n=std::max(1,int(std::max(std::fabs(dx),std::fabs(dy))));const float sx=dx/n,sy=dy/n;
    const Ink glow=inkOf(glowColor);const int core=q8(k),side=q8(k*.55f),ox=steep?1:0,oy=steep?0:1;
    for(int i=0;i<n;++i) {
      const int x=roundi(x0+sx*i),y=roundi(y0+sy*i);
      // The side glow on every other step: a third fewer probes on long
      // strokes (bolts, blade flashes), indistinguishable at 20 fps.
      addw(x,y,WHITE_INK,core);if(!(i&1)){addw(x-ox,y-oy,glow,side);addw(x+ox,y+oy,glow,side);}
    }
  }
};
}
#include "best_effect_elemental.h"
#include "best_effect_physical.h"
#include "best_effect_eva.h"
#include "best_effect_slayer.h"
#include "best_effect_ninja.h"
#include "best_effect_nier.h"
#include "best_effect_sorcery.h"
#include "best_effect_aura.h"
#include "best_effect_alchemy.h"
#include "best_effect_phantom.h"
#include "best_effect_armory.h"
#include "best_effect_grimoire.h"
#include "best_effect_ripsaw.h"
#include "best_effect_corsair.h"
#include "best_effect_reaper.h"
