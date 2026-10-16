#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "appearance.h"

// Personal-best celebrations, drawn into the recap's 16-bit canvas after the
// UI and before it is pushed. Decoration lands only on background pixels at
// least HALO px from every UI pixel, so it reads as a layer behind the UI and
// each label, digit and replay line keeps its pixels and a dark outline. The
// glint is the one exception: it brightens existing header glyph pixels
// toward white without changing their shape.
// Each frame is a pure function of elapsed time with fixed seeds: no heap,
// delay, random input or second framebuffer. The ink mask is the only state.
// Budget: at most about 10 ms per frame at 80 MHz (measured on the device
// with the USB E0-E4 preview), keeping a celebrating frame near 40 ms of the
// 50 ms cadence. Cost model, measured: the canvas is in PSRAM, where a cache
// line miss costs microseconds, so cost follows the canvas rows touched; a
// libm call costs about 1 us and per-pixel float math several cycles. So the
// mask is built per 32-pixel word on first use, pixel loops are fixed point,
// trigonometry comes from a table, time-only terms are computed once per
// frame and geometry stays in float, once per primitive.
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
// Row-major with stride == width, RGB565 big-endian as LGFX sprites store it.
struct Surface {uint16_t *pixels=nullptr;int width=0,height=0;};

class Renderer {
public:
  static constexpr int MAX_W=192,MAX_H=240,HALO=2;
  // Header rows searched for the RPM readout; the label sits above, the
  // replay below.
  static constexpr int NUMBER_TOP=13,NUMBER_BOTTOM=46;
  // The ink the effect frames: the recap's RPM readout by default, or another
  // UI element such as a Settings value. Its bounds are searched in this region.
  struct Anchor {int top=NUMBER_TOP,bottom=NUMBER_BOTTOM,left=0,right=MAX_W-1;};
  // Draws nothing once elapsed reaches Appearance::duration(effect).
  void draw(const Surface &s,Appearance::Effect effect,uint32_t elapsed,uint16_t accent){draw(s,effect,elapsed,accent,Anchor());}
  void draw(const Surface &s,Appearance::Effect effect,uint32_t elapsed,uint16_t accent,const Anchor &anchor) {
    plots_=0;
    if(!s.pixels || s.width<64 || s.width>MAX_W || s.height<NUMBER_BOTTOM+HALO+1 || s.height>MAX_H ||
       elapsed>=Appearance::duration(effect))return;
    px_=s.pixels;w_=s.width;h_=s.height;
    // A new stamp invalidates every cached mask row without clearing them.
    if(++stamp_==0){for(auto &r:inkStamp_)r.fill(0);for(auto &r:blockedStamp_)r.fill(0);stamp_=1;}
    findNumber(anchor);
    accent_=rgb(accent);
    const float t=elapsed*.001f;
    using Appearance::Effect;
    // Orbit and Flames stay near the anchor; the rest may use the whole screen.
    switch(effect) {
      case Effect::Orbit:bottom_=std::min(h_-1,number_.y1+21);orbit(t);break;
      case Effect::Flames:bottom_=std::min(h_-1,number_.y1+31);flames(t);break;
      case Effect::Sparkles:bottom_=h_-1;sparkles(t);break;
      case Effect::Shockwave:bottom_=h_-1;shockwave(t);break;
      case Effect::Lightning:bottom_=h_-1;lightning(t);break;
      default:break;
    }
  }
  // Pixels written by the last draw; a portable cost measure for previews.
  uint32_t plots()const{return plots_;}

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
  uint16_t *px_=nullptr;int w_=0,h_=0,bottom_=-1;uint8_t stamp_=0;
  Box number_{};Rgb accent_{};uint32_t plots_=0;

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
    if(w<5 || blocked(x,y))return;
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
    for(int y=top;y<=bottom;++y) {
      const float centre=head-(y-number_.cy())*.45f;
      const int c8=q8(centre);
      for(int x=std::max(0,int(centre-7));x<=std::min(w_-1,int(centre+7));++x) {
        uint16_t &p=px_[y*w_+x];if(!p)continue;
        // Band weight (1-|x-centre|/7)*0.9 in Q8.
        const int k=((7*256-std::abs(x*256-c8))*33)>>8;if(k<=0)continue;
        const unsigned v=load(p);const int r=v>>11,g=(v>>5)&63,b=v&31;
        p=store(uint16_t((r+(((31-r)*k+128)>>8))<<11|(g+(((63-g)*k+128)>>8))<<5|(b+(((31-b)*k+128)>>8))));++plots_;
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
  // A pixel is blocked when any non-black pixel lies within HALO px (a
  // square). Mask words are built on first use each frame, so thin strokes
  // read only the pixels around them; a word is always built before
  // decoration is written to it, so decoration never masks itself.
  BEST_EFFECT_INLINE uint32_t ink(int y,int i) {
    if(inkStamp_[y][i]!=stamp_){ink_[y][i]=scanWord(y,i);inkStamp_[y][i]=stamp_;}
    return ink_[y][i];
  }
  // Ink of pixels 32i..32i+31 of row y, widened by HALO px: reads the
  // word's pixels and HALO more on each side; each ink pixel sets five bits.
  __attribute__((noinline)) uint32_t scanWord(int y,int i)const {
    static_assert(HALO==2,"the five-bit stamp below is 2*HALO+1 wide");
    const int first=i*32,from=std::max(0,first-HALO),to=std::min(w_-1,first+31+HALO);
    const uint16_t *p=px_+y*w_;uint32_t bits=0;
    for(int x=from;x<=to;++x)if(p[x]) {
      const int d=x-first;bits|=d>=HALO?0x1fu<<(d-HALO):0x1fu>>(HALO-d);
    }
    return bits;
  }
  // The anchor's ink bounds frame every effect; each row stops at its first
  // ink pixel from either side.
  void findNumber(const Anchor &a) {
    const int top=std::max(0,a.top),bottom=std::min(h_-1,a.bottom),first=std::max(0,a.left),last=std::min(w_-1,a.right);
    int x0=w_,y0=h_,x1=-1,y1=-1;
    for(int y=top;y<=bottom;++y) {
      const uint16_t *p=px_+y*w_;int left=first;
      while(left<=last && !p[left])++left;
      if(left>last)continue;
      int right=last;while(!p[right])--right;
      x0=std::min(x0,left);x1=std::max(x1,right);y0=std::min(y0,y);y1=y;
    }
    number_=x1>=0?Box{x0,y0,x1,y1}:Box{first+10,top+6,last-10,std::max(top+6,bottom-5)};
  }

  // ---- Orbit: two comets spin up around the readout, then light the ring ----
  void orbitPoint(float a,float scale,float &x,float &y)const {
    x=number_.cx()+(w_*.5f-3)*scale*cosine(a);y=number_.cy()+23*scale*sine(a);
  }
  // The full ellipse at one-pixel spacing, brighter on the near (lower) side.
  void ellipse(float scale,const Rgb &c,float k) {
    for(int i=0;i<300;++i){float x,y;const float a=i*(2*HALF_TURN/300);orbitPoint(a,scale,x,y);plot(x,y,c,k*(.55f+.45f*sine(a)));}
  }
  void orbit(float t) {
    constexpr float SPIN=1.05f,LAPS=2.3f;
    const float s=std::min(t,SPIN)/SPIN,angle=-HALF_TURN/2+2*HALF_TURN*LAPS*s*s+(t>SPIN?4*HALF_TURN*LAPS/SPIN*(t-SPIN):0);
    const float speed=4*HALF_TURN*LAPS*s/SPIN,fade=t<SPIN?1:1-smooth((t-SPIN)/.25f);
    glint(t,SPIN-.05f,.45f);
    // A faint track gives the ring its perspective before the comets arrive.
    if(t<SPIN)ellipse(1,accent_,.2f*smooth(t/.2f));
    if(fade>0)for(int comet=0;comet<2;++comet) {
      const float head=angle+comet*HALF_TURN,tail=std::min(2.6f,.35f+speed*.085f);
      const int n=int(tail*46);
      for(int i=n;i>=0;--i) {
        const float u=float(i)/n,a=head-tail*u,ca=cosine(a),sa=sine(a);
        const float x=number_.cx()+(w_*.5f-3)*ca,y=number_.cy()+23*sa;
        const float k=fade*(.6f+.4f*sa)*(1-u)*(1-u);const Rgb c=mixed(WHITE,accent_,std::min(1.0f,u*2.5f));
        // Two-pixel stroke: a smooth centre line plus a softer outer edge.
        dot(x,y,c,k*1.5f);plot(x+ca*.9f,y+sa*.9f,c,k*.7f);
      }
      float x,y;orbitPoint(head,1,x,y);spark(x,y,accent_,fade*(1.1f+.3f*sine(head)),3.2f);
    }
    if(t<SPIN)return;
    // Lock-in: the whole ring flashes and expands as it fades.
    const float e=(t-SPIN)/(Appearance::duration(Appearance::Effect::Orbit)*.001f-SPIN),flash=(1-e)*(1-e);
    const Rgb c=mixed(WHITE,accent_,std::min(1.0f,e*2.2f));
    ellipse(1+.06f*easeOut(e),c,flash);ellipse(1+.22f*easeOut(e),c,flash*.45f);
    for(unsigned i=0;i<18;++i) {
      const float a=i*2*HALF_TURN/18+rnd(i,1)*.3f,v=35+45*rnd(i,2),life=.3f+.25f*rnd(i,3),age=t-SPIN;
      if(age>=life)continue;
      float x,y;orbitPoint(a,1,x,y);const float d=v*easeOut(age/life)*.5f,k=1-age/life;
      const float ux=cosine(a),uy=sine(a)*.6f;
      dot(x+ux*d,y+uy*d,mixed(WHITE,accent_,age/life),k);plot(x+ux*(d-2),y+uy*(d-2),accent_,k*.5f);
    }
  }

  // ---- Flames: fire rises behind the readout, then burns down to embers ------
  void flames(float t) {
    constexpr float END=1.8f;
    const float grow=easeOut(t/.32f),die=1-smooth((t-1.05f)/(END-1.05f-.12f)),height=grow*die;
    glint(t,.3f,.5f);
    const int base=std::min(bottom_-2,number_.y1+16);
    // Two noise octaves scroll upward; time offsets in Q8 noise units.
    const int rise1=int(t*80*.10f*256),rise2=int(t*130*.21f*256),sway=int(t*1.7f*256);
    if(height>0)for(int x=0;x<w_;x+=2) {
      // 2x2 cells, one flame tongue per column; noise only below its tip.
      const int tongue=int(height*(14+28*noise(x*18,sway,11)*(1/255.0f))),inv=65536/std::max(1,tongue);
      for(int y=base;y>=0 && base-y<=tongue+2;y-=2) {
        if(blocked(x,y) && blocked(x+1,y) && blocked(x,y+1) && blocked(x+1,y+1))continue;
        const int rise=base-y,n=(158*noise(x*31,y*26+rise1,12)+97*noise(x*67,y*54+rise2,13))>>8;
        // heat=((1.12-rise/tongue)(.45+.85n)-.1)*smooth(rise/12), all in Q8;
        // the smoothstep fades the bottom rows so the fire has no hard floor.
        int heat=((((287-((rise*inv)>>8))*(115+((218*n)>>8)))>>8)-26)*detail::SMOOTH[std::min(255,rise*21)]>>8;
        if(heat<=10)continue;
        heat=std::min(255,heat);
        const Ink c=fire(heat);const int k=std::min(256,(heat*666)>>8);
        addw(x,y,c,k);addw(x+1,y,c,k);addw(x,y+1,c,k);addw(x+1,y+1,c,k);
      }
    }
    for(unsigned i=0;i<16;++i) {
      const float birth=.18f+1.15f*rnd(i,21),life=.45f+.4f*rnd(i,22),age=t-birth;
      if(age<0 || age>=life)continue;
      const float x=4+(w_-8)*rnd(i,23)+3*sine(age*9+i),y=base-4-age*(45+40*rnd(i,24)),k=1-age/life;
      dot(x,y,mixed(rgb8(255,214,120),rgb8(220,60,0),age/life),k);
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
    for(unsigned i=0;i<13;++i) {
      const float birth=.1f+.9f*rnd(i,41),age=t-birth,life=.6f;
      if(age<0 || age>=life)continue;
      const bool header=i<6;
      const float x=header?6+(w_-12)*rnd(i,42):(i%2?5+12*rnd(i,43):w_-6-12*rnd(i,43));
      const float y=header?(rnd(i,44)<.5f?number_.y0-15+6*rnd(i,45):number_.y1+5+12*rnd(i,45)):60+150*rnd(i,44);
      star(x,y,sine(HALF_TURN*age/life),i%2?gold:accent_);
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
    if(t<.2f)glow(42,22,accent_,.85f*(1-t/.2f)*(1-t/.2f));
    for(unsigned i=0;i<12;++i) {
      const float start=.02f*rnd(i,51),age=t-start,life=.34f;if(age<0 || age>=life)continue;
      const float a=2*HALF_TURN*(i+.5f*rnd(i,52))/12,ux=cosine(a),uy=sine(a),u=age/life;
      const float r0=24+190*easeOut(u),len=26*(1-u)+4;
      line(cx+ux*r0,cy+uy*r0*.75f,cx+ux*(r0+len),cy+uy*(r0+len)*.75f,mixed(WHITE,accent_,u),(1-u)*.95f);
    }
    ring(t,0,.95f,1);ring(t,.12f,.85f,.55f);
  }
  void ring(float t,float start,float life,float strength) {
    const float u=(t-start)/life;if(u<=0 || u>=1)return;
    const float cx=number_.cx(),cy=number_.cy(),r=10+250*easeOut(u),width=3+5*u,k=strength*(1-u)*(1-u*.2f)*1.3f;
    const float inner=std::max(0.0f,r-width),r2=r*r,i2=inner*inner;
    const Ink accent=inkOf(accent_);const int kq=q8(k),cx2=roundi(cx*2);
    // s=(r^2-d^2)/(2 r width) approximates (r-d)/width across a thin ring
    // without a per-pixel sqrt; quarter-pixel^2 units, result in Q8.
    const float depth=256/(4*2*r*width);
    for(int y=std::max(0,int(cy-r));y<=std::min(bottom_,int(cy+r)+1);++y) {
      const float dy2=(y-cy)*(y-cy);if(dy2>r2)continue;
      const float xo=std::sqrt(r2-dy2),xi=i2>dy2?std::sqrt(i2-dy2):0;const int base=int((r2-dy2)*4);
      for(int side=-1;side<=1;side+=2)for(int x=int(cx+side*xi);side*(x-cx)<=xo+1;x+=side) {
        if(x<0 || x>=w_)break;
        const int dx=2*x-cx2,s=int((base-dx*dx)*depth);if(s<0 || s>256)continue;
        const int fade=256-s;
        addw(x,y,mixedInk(WHITE_INK,accent,std::min(256,(s*13)>>3)),(((fade*fade)>>8)*kq)>>8);
      }
    }
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
      if(age<.05f)glow(42,22,mixed(accent_,WHITE,.4f),.6f*power[s]*(1-age/.05f));
      for(unsigned b=0;b<2;++b) {
        const uint32_t seed=s*2+b;const bool left=(s+b)%2==0;
        const float x0=left?number_.x0+8+12*rnd(seed,61):number_.x1-8-12*rnd(seed,61),y0=number_.y1+3;
        const float x1=left?4+30*rnd(seed,62):w_-5-30*rnd(seed,62),y1=h_-30-90*rnd(seed,63);
        bolt(x0,y0,x1,y1,seed,k,5);
      }
    }
    // Between strikes, arcs crackle along the readout's top and bottom edges.
    if(t<1.25f)for(unsigned i=0;i<2;++i) {
      const uint32_t frame=uint32_t(t*20),seed=frame*4+i;
      if(rnd(seed,71)<.35f)continue;
      const float y=i?number_.y1+3.5f:number_.y0-3.5f,x=number_.x0+(number_.x1-number_.x0-36)*rnd(seed,72);
      bolt(x,y,x+20+16*rnd(seed,73),y,seed+1000,.75f*(1-t/1.25f),3);
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
  // White core with a glow on the two sides across the stroke.
  void segment(float x0,float y0,float x1,float y1,const Rgb &glowColor,float k) {
    const float dx=x1-x0,dy=y1-y0;const bool steep=std::fabs(dy)>std::fabs(dx);
    const int n=std::max(1,int(std::max(std::fabs(dx),std::fabs(dy))));const float sx=dx/n,sy=dy/n;
    const Ink glow=inkOf(glowColor);const int core=q8(k),side=q8(k*.55f),ox=steep?1:0,oy=steep?0:1;
    for(int i=0;i<n;++i) {
      const int x=roundi(x0+sx*i),y=roundi(y0+sy*i);
      addw(x,y,WHITE_INK,core);addw(x-ox,y-oy,glow,side);addw(x+ox,y+oy,glow,side);
    }
  }
};
}
