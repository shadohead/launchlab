#pragma once
// Naruto-inspired celebrations (Birdsong..Summon): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Same rules as
// the others: additive light on the background outside the UI halo, a pure
// function of elapsed time with fixed seeds, and the readout exactly as drawn
// (only the glint brightens it, and it ends well before 3/4 of the effect).
// Each runs about 5 s: an impact by 1.6 s, a sustained middle that keeps
// moving off the readout's glyphs, and a settle.
//   Birdsong:  crackling lightning gathers in a fist, thrusts into the
//              readout and bursts along it; the fist keeps a pulsing charge
//              whose arcs leap back to the readout.
//   Spiral:    a sphere of spinning light condenses below the readout,
//              grinds into it and releases a spiral blast that turns on as
//              a slowing whirlwind.
//   Pinwheel:  a pair of red eyes opens; three commas spin, blur into a ring
//              and reform as interlocking loops, which turn, breathe and
//              blink before the eyes close.
//   Overdrive: heartbeat by heartbeat a green aura wraps the UI's outline,
//              pebbles lift, the last beat erupts in steam, and the aura
//              breathes on with calmer beats.
//   Summon:    a palm slam spreads lines of script into a seal, and a cloud
//              of smoke bursts out of it, lingers and thins.
// Cost: every frame stays near the readout's rows or in one compact region
// below it (the mask words a frame touches dominate on the device), strokes
// are dots and thin lines, and the eye's fill keeps to two mask words a row.
// Filled areas (the irises, the smoke's body) are lit a row at a time from
// 32-bit masks with one precomputed step per colour (ninja::addBits), and
// no pixel loop calls sqrtf, ceilf or floorf, divides, or tests against
// other shapes: on the device those cost far more than the pixel writes.
namespace BestEffectUI {
namespace ninja {
// A 32x32 bit mask of pixels the eye leaves dark, stamped from discs: the
// iris (radius 15) fits, centred on bit 16 of row 16. 32-bit words: on the
// device a 64-bit shift is a library call.
struct Mask {
  uint32_t row[32]={};int x0=0,y0=0;
  static int floorOf(float v){const int i=int(v);return i-(v<i);}
  static int ceilOf(float v){const int i=int(v);return i+(v>i);}
  // The pixels within r of (x,y). Fixed point (Q10) per pixel, with no sqrt,
  // ceil or floor call in the loop: the discs are small (radius <= 3).
  void disc(float x,float y,float r) {
    if(r<=.3f)return;
    const int top=std::max(0,ceilOf(y-r)-y0),bottom=std::min(31,floorOf(y+r)-y0);
    const int left=std::max(0,ceilOf(x-r)-x0),right=std::min(31,floorOf(x+r)-x0);
    const int xq=int((x-x0)*1024+.5f),yq=int((y-y0)*1024+.5f),r2=int(r*r*1048576);
    for(int v=top;v<=bottom;++v) {
      const int dy=v*1024-yq,rest=r2-dy*dy;if(rest<0)continue;
      int a=left,b=right;
      while(a<=b && (a*1024-xq)*(a*1024-xq)>rest)++a;
      while(b>=a && (b*1024-xq)*(b*1024-xq)>rest)--b;
      if(a<=b)row[v]|=(~0u>>(31-(b-a)))<<a;
    }
  }
};
// isqrt(v) for 0 <= v < 256: the iris's spans, row by row.
constexpr std::array<uint8_t,256> rootTable() {
  std::array<uint8_t,256> t{};
  for(int v=0,r=0;v<256;++v){while((r+1)*(r+1)<=v)++r;t[v]=uint8_t(r);}
  return t;
}
constexpr std::array<uint8_t,256> ROOT=rootTable();
// sqrt(v) for v > 0 by multiplies only (two Newton steps on the inverse
// root, relative error under 5e-6): sqrtf is a library call on the device.
BEST_EFFECT_INLINE float root(float v) {
  uint32_t i;std::memcpy(&i,&v,4);i=0x5f3759dfu-(i>>1);float y;std::memcpy(&y,&i,4);
  const float h=.5f*v;y*=1.5f-h*y*y;y*=1.5f-h*y*y;
  return v*y;
}
// cos and sin of a small angle (|a| <= 0.6) by series, accurate to about
// 1e-6: the table's interpolation error would add up along a rotation.
BEST_EFFECT_INLINE void cosSin(float a,float &c,float &s) {
  const float a2=a*a;
  c=1-a2*(.5f-a2*(1/24.0f-a2*(1/720.0f)));s=a*(1-a2*(1/6.0f-a2*(1/120.0f-a2*(1/5040.0f))));
}
// One step round a circle: (c,s) rotated by the angle whose cosine and sine
// are (cd,sd).
BEST_EFFECT_INLINE void turn(float &c,float &s,float cd,float sd){const float t=c*cd-s*sd;s=s*cd+c*sd;c=t;}
// One ink at one weight as RGB565 channel steps, rounded as addw does.
struct Step {int r,g,b;};
template<class I> BEST_EFFECT_INLINE Step step(const I &c,int w){return {(c.r*w+32768)>>16,(c.g*w+32768)>>16,(c.b*w+32768)>>16};}
// addw's saturating add of one step to pixel row[i] for each bit i of bits
// (byte-swapped, as LGFX stores RGB565); the caller has already checked the
// halo. Returns the number of pixels written.
BEST_EFFECT_INLINE int addBits(uint16_t *row,uint32_t bits,const Step &s) {
  int n=0;
  for(;bits;bits&=bits-1,++n) {
    uint16_t &p=row[__builtin_ctz(bits)];const unsigned v=uint16_t(p<<8|p>>8);
    const unsigned out=unsigned(std::min(31,int(v>>11)+s.r)<<11|std::min(63,int((v>>5)&63)+s.g)<<5|std::min(31,int(v&31)+s.b));
    p=uint16_t(out<<8|out>>8);
  }
  return n;
}
}

inline void Renderer::ninja(unsigned which,float t) {
  switch(which) {
    case 0:ninjaCharge(t);break;
    case 1:ninjaSpiral(t);break;
    case 2:ninjaEye(t);break;
    case 3:ninjaGates(t);break;
    case 4:ninjaSummon(t);break;
    default:break;
  }
}

// A jagged electric arc by midpoint displacement (depth <= 4), white core
// with a coloured glow, fading toward its far end.
inline void Renderer::ninjaArc(float x0,float y0,float x1,float y1,uint32_t seed,const Rgb &glow,float k,int depth,float rough) {
  float xs[17],ys[17];depth=std::max(1,std::min(4,depth));const int n=1<<depth;
  xs[0]=x0;ys[0]=y0;xs[n]=x1;ys[n]=y1;
  for(int step=n;step>1;step/=2)for(int i=0;i+step<=n;i+=step) {
    const int m=i+step/2;const float dx=xs[i+step]-xs[i],dy=ys[i+step]-ys[i],off=(rnd(seed*32+m,311)-.5f)*rough;
    xs[m]=(xs[i]+xs[i+step])*.5f-dy*off;ys[m]=(ys[i]+ys[i+step])*.5f+dx*off;
  }
  for(int i=0;i<n;++i)segment(xs[i],ys[i],xs[i+1],ys[i+1],glow,k*(1-.55f*i/n));
}

// A soft round glow: weight (1-d^2)^2, fixed point per pixel.
inline void Renderer::ninjaGlow(float cx,float cy,float r,const Rgb &col,float k) {
  if(r<1 || k<=.02f)return;
  const Ink c=inkOf(col);const int kq=q8(std::min(1.0f,k));const float inv=1/r;const int step=int(inv*65536);
  for(int y=std::max(0,int(cy-r));y<=std::min(bottom_,int(cy+r)+1);++y) {
    const float dy=(y-cy)*inv;if(dy*dy>=1)continue;
    const int dy2=int(dy*dy*256);const float span=r*std::sqrt(1-dy*dy);
    const int xa=std::max(0,int(cx-span)),xb=std::min(w_-1,int(cx+span)+1);
    int dx16=int((xa-cx)*inv*65536);
    for(int x=xa;x<=xb;++x,dx16+=step) {
      const int dx8=dx16>>8,d2=((dx8*dx8)>>8)+dy2;if(d2>=256)continue;
      addw(x,y,c,(kq*(((256-d2)*(256-d2))>>8))>>8);
    }
  }
}

// ---- Birdsong: lightning gathers in a fist low on the left, crackling
// louder and wider (with the chirping sound); the fist thrusts up into the
// readout along a lightning spear, the charge bursts along the readout's
// edges and through it, and the last arcs crawl away. Unlike Lightning (bolts
// struck down from the readout), the light is one dense crackling ball that
// moves, and the impact is a burst at a point.
inline void Renderer::ninjaCharge(float t) {
  constexpr float THRUST=1.05f,HIT=1.2f,SETTLE=3.9f,END=5.0f;
  const Box &b=number_;
  const Rgb volt=mixed(rgb8(95,165,255),accent_,.2f),pale=rgb8(205,232,255);
  const float hx=b.x0+(b.x1-b.x0)*.2f,hy=b.y1+64.0f,ix=b.cx()+4,iy=b.y1+3.5f;
  // Crackle re-rolls 30 times a second: every device frame differs.
  const uint32_t frame=uint32_t(t*30);
  glint(t,HIT,.42f);
  if(t<HIT) {
    // The ball: at the fist, then rushing along the spear to the readout.
    const float u=t<THRUST?0:(t-THRUST)/(HIT-THRUST),go=u*u;
    const float bx=hx+(ix-hx)*go,by=hy+(iy-hy)*go;
    const float grow=smooth((t-.05f)/.85f),flick=.75f+.25f*rnd(frame,301);
    if(t>=THRUST) {
      // The spear: a bright jagged trail from the fist to the ball.
      ninjaArc(hx,hy,bx,by,frame*7+1,volt,.95f,4,.32f);
      ninjaArc(hx,hy,bx,by,frame*7+2,volt,.45f,3,.5f);
    }
    ninjaGlow(bx,by,7+10*grow,mixed(rgb8(40,110,255),accent_,.15f),(.25f+.45f*grow)*flick);
    spark(bx,by,volt,(.5f+.8f*grow)*flick,2+1.6f*grow);
    const int arcs=4+int(13*grow);
    for(int i=0;i<arcs;++i) {
      const uint32_t s=frame*16+i;
      const float a=2*HALF_TURN*rnd(s,302),len=(7+22*rnd(s,303))*(.45f+.55f*grow)*(t<THRUST?1:.7f);
      ninjaArc(bx,by,bx+len*cosine(a),by+len*sine(a),s,volt,(.55f+.45f*rnd(s,304))*(.5f+.5f*grow),2,.7f);
    }
    // Now and then a long arc snaps out to the floor beside the fist.
    if(t>.35f && t<THRUST && rnd(frame,305)<.45f) {
      const float a=HALF_TURN*(.15f+.7f*rnd(frame,306)),len=22+16*rnd(frame,307);
      ninjaArc(bx,by,bx+len*cosine(a)*1.3f,by+len*sine(a)*.6f,frame*3+9,volt,.6f*grow,3,.55f);
    }
    // Flecks hop off the ball like sparks off a grinder.
    for(unsigned i=0;i<8;++i) {
      const float life=.18f,a0=t-life*rnd(i,308)-(frame%3)*.01f,age=fmodf(a0+8,life)/life;
      const uint32_t s=i+uint32_t((a0+8)/life)*8;
      const float a=2*HALF_TURN*rnd(s,309),d=6+20*age*(.5f+.5f*grow);
      dot(bx+d*cosine(a),by+d*sine(a)+10*age*age,i%2?pale:volt,(1-age)*grow);
    }
    return;
  }
  // Impact: the charge bursts at the readout's lower edge.
  const float a=t-HIT;
  const float burst=a<.06f?1:1-smooth((a-.06f)/.55f);
  if(burst>0) {
    // The spear's trail fades out.
    if(a<.25f)ninjaArc(hx,hy,ix,iy,frame*7+3,volt,.8f*(1-a/.25f),4,.3f);
    // Through the readout: a lance up to above it, seen in the gaps.
    if(a<.3f)ninjaArc(ix,iy,ix-6,b.y0-12.0f,frame*5+4,volt,1-a/.3f,3,.35f);
    if(a<.12f)spark(ix,iy,volt,1.6f*(1-a/.12f),4.2f);
    // Arcs race out along the readout's lower and upper edges.
    const int n=2+int(6*burst);
    for(int i=0;i<n;++i) {
      const uint32_t s=frame*16+i+500;
      const float side=i%2?1.0f:-1.0f,reach=(20+34*rnd(s,312))*easeOut(a/.12f+.2f);
      const bool upper=i%4>=2;
      const float sx=ix,sy=upper?b.y0-3.5f:iy,ex=sx+side*reach,ey=sy+(rnd(s,313)-.5f)*8;
      ninjaArc(upper?ix+side*10:sx,sy,ex,ey,s,volt,burst*(.6f+.4f*rnd(s,314)),3,.45f);
    }
    // And down and away below it.
    for(int i=0;i<3;++i) {
      const uint32_t s=frame*4+i+900;const float ang=HALF_TURN*(.2f+.6f*rnd(s,315)),len=(18+22*rnd(s,316))*burst;
      ninjaArc(ix,iy+1,ix+len*cosine(ang)*1.4f,iy+1+len*sine(ang),s,volt,.7f*burst,3,.5f);
    }
  }
  // Sparks thrown off by the burst fall away.
  for(unsigned i=0;i<16;++i) {
    const float age=a-.05f*rnd(i,321),life=.7f+.6f*rnd(i,322);if(age<0 || age>=life)continue;
    const float ang=2*HALF_TURN*rnd(i,323),v=40+70*rnd(i,324);
    const float x=ix+v*cosine(ang)*age*1.3f,y=iy+v*sine(ang)*age*.6f+60*age*age;
    dot(x,y,i%3?volt:pale,(1-age/life)*(.6f+.4f*sine(t*40+i)));
  }
  // Sustain (about 1.5-3.9 s), then an unhurried settle to 4.9 s: the fist
  // keeps its charge. A smaller crackling ball pulses there, beat by beat,
  // each beat throwing out a few short arcs; on most beats an arc leaps from
  // it up to the readout's lower edge and crackles along it, shedding sparks.
  // Between leaps, static crawls along the readout's edges and motes drift
  // down; the beats grow calmer as the charge bleeds away.
  const float linger=smooth((a-.2f)/.4f)*(1-smooth((t-SETTLE)/(END-.1f-SETTLE)));
  if(linger<=0)return;
  const float calm=1-.35f*smooth((t-1.8f)/2.2f),live=linger*calm;
  const Rgb deep=mixed(rgb8(40,110,255),accent_,.15f);
  constexpr float BEAT=.62f;
  const float clock=std::max(0.0f,t-HIT-.3f);const unsigned beat=unsigned(clock/BEAT);
  const float ph=(clock-beat*BEAT)/BEAT,pk=ph<.06f?ph/.06f:(1-ph)*(1-ph)*(1-ph)/((1-.06f)*(1-.06f)*(1-.06f));
  {
    const float flick=.8f+.2f*rnd(frame,342);
    ninjaGlow(hx,hy,7+4*pk,deep,(.3f+.4f*pk)*live);
    spark(hx,hy,volt,(.6f+.6f*pk)*live*flick,1.8f+1.0f*pk);
    const int arcs=3+int(5*pk*calm);
    for(int i=0;i<arcs;++i) {
      const uint32_t s=frame*16+i+700;
      const float ang=2*HALF_TURN*rnd(s,343),len=(7+13*rnd(s,344))*(.6f+.4f*pk);
      ninjaArc(hx,hy,hx+len*cosine(ang),hy+len*sine(ang),s,volt,(.5f+.5f*rnd(s,345))*live,2,.7f);
    }
  }
  // The leap: on most beats, from the fist to a point under the digits.
  if(clock>0 && rnd(beat,350)<.7f && ph<.4f) {
    const float lx=b.x0+20+(b.x1-b.x0-40)*rnd(beat,351),ly=b.y1+3.5f;
    if(ph<.18f) {
      const float k=.95f*live*(1-ph/.18f*.6f);
      ninjaArc(hx,hy-2,lx,ly+1,beat*9+frame,volt,k,4,.34f);
    }
    // It lands crackling both ways along the edge and sheds sparks.
    const float fade=1-ph/.4f;
    for(int side=0;side<2;++side) {
      const uint32_t s=frame*4+side+beat*31;const float dir=side?1.0f:-1.0f,len=(8+12*rnd(s,352))*(.5f+.5f*easeOut(ph/.1f));
      ninjaArc(lx,ly,lx+dir*len,ly+(rnd(s,353)-.5f)*3,s,volt,.75f*live*fade,2,.6f);
    }
    for(int i=0;i<4;++i) {
      const float ang=HALF_TURN*(.15f+.7f*rnd(beat*4+i,354)),v=25+30*rnd(beat*4+i,355),age=ph*BEAT;
      dot(lx+v*cosine(ang)*age*1.2f,ly+2+v*sine(ang)*age*.5f+50*age*age,i%2?pale:volt,live*fade);
    }
  }
  // Crawlers: short arcs that run along the readout's lower (and now and
  // then upper) edge, each from its own start, at its own speed.
  for(unsigned i=0;i<20;++i) {
    const float birth=HIT+.25f+2.6f*(i+.7f*rnd(i,335))/20,life=.55f+.4f*rnd(i,336),age=t-birth;if(age<0 || age>=life)continue;
    const float q=age/life,dir=rnd(i,337)<.5f?-1.0f:1.0f,x0=b.x0+12+(b.x1-b.x0-24)*rnd(i,338);
    const bool upper=i%4==3;const float y=upper?b.y0-3.5f:b.y1+3.5f,x=x0+dir*(20+25*rnd(i,339))*q;
    const float len=8+7*rnd(frame*16+i,340);
    ninjaArc(x,y,x+dir*len,y+(rnd(frame*16+i,341)-.5f)*3,frame*16+i,volt,.8f*linger*sine(HALF_TURN*q),2,.65f);
  }
  for(unsigned i=0;i<34;++i) {
    const float birth=HIT+.3f+2.8f*rnd(i,329),life=.9f+.8f*rnd(i,330),age=t-birth;if(age<0 || age>=life)continue;
    const float u=age/life,x0=b.x0+6+(b.x1-b.x0-12)*rnd(i,331),y0=b.y1+5+26*rnd(i,332);
    const float x=x0+5*sine(age*1.7f+i)+4*age*(rnd(i,333)-.5f),y=y0+7*age;
    const float k=linger*sine(HALF_TURN*u)*(.55f+.45f*sine(age*(9+5*rnd(i,334))+i));
    if(i%4==0)twinkle(x,y,pale,k,1);else dot(x,y,i%3?volt:pale,k);
  }
}

// A sphere of spinning light: a bright core fading to blue, a thin bright
// shell, three spiral streaks turning inside and four wind arcs outside it,
// turning the other way. squash < 1 flattens it against what it grinds into.
inline void Renderer::ninjaOrb(float cx,float cy,float r,float squash,float spin,float k) {
  if(r<1 || k<=.02f)return;
  const Ink white=WHITE_INK,blue=inkOf(rgb8(35,110,255)),sky=inkOf(rgb8(150,215,255));
  const int kq=q8(std::min(1.0f,k));
  const float ry=r*squash,inv=1/r;
  const int ya=std::max(0,int(cy-ry)),yb=std::min(bottom_,int(cy+ry)+1);
  const int step=int(inv*65536);
  for(int y=ya;y<=yb;++y) {
    const float dy=(y-cy)/ry;if(dy*dy>=1)continue;
    const int dy2=int(dy*dy*256);const float span=r*std::sqrt(1-dy*dy);
    const int xa=std::max(0,int(cx-span)),xb=std::min(w_-1,int(cx+span)+1);
    int dx16=int((xa-cx)*inv*65536);
    for(int x=xa;x<=xb;++x,dx16+=step) {
      const int dx8=dx16>>8,d2=((dx8*dx8)>>8)+dy2;if(d2>=256)continue;
      const int core=((256-d2)*(256-d2))>>8,rim=d2>184?(d2-184)*3:0;
      const Ink c=rim>core?mixedInk(blue,sky,rim):mixedInk(blue,white,core);
      addw(x,y,c,(kq*std::min(256,70+((core*170)>>8)+((rim*150)>>8)))>>8);
    }
  }
  const Rgb streak=rgb8(220,240,255),wind=rgb8(140,205,255);
  for(int arm=0;arm<3;++arm)for(int s=0;s<=14;++s) {
    const float u=s/14.0f,rho=(.22f+.72f*u)*r,a=spin+arm*(2*HALF_TURN/3)+2.4f*u;
    dot(cx+rho*cosine(a),cy+rho*squash*sine(a),streak,k*(.35f+.55f*u));
  }
  for(int arc=0;arc<4;++arc) {
    const float rho=r*(1.14f+.1f*(arc&1)),base=-spin*1.3f+arc*(HALF_TURN/2);
    for(int s=0;s<=10;++s){const float u=s/10.0f,a=base+1.1f*u;dot(cx+rho*cosine(a),cy+rho*squash*sine(a),wind,k*.75f*(1-u));}
  }
}

// ---- Spiral: wisps of light spiral in and condense into a spinning
// sphere below the readout; it rises and grinds into the readout's lower
// edge, throwing sparks along it, then bursts in a spiral blast. Unlike
// Orbit and Spin (comets and blades around an ellipse that frames the whole
// readout), this is a solid sphere at one point with spiral arms.
inline void Renderer::ninjaSpiral(float t) {
  constexpr float FORM=.85f,HIT=1.0f,RELEASE=1.65f,SETTLE=4.1f,END=5.0f;
  const Box &b=number_;
  // It forms in the open space right of the replay box's top corner and is
  // driven up into the readout's centre.
  const float cx=b.cx(),sx=cx+std::min(37.0f,(b.x1-b.x0)*.3f),rest=b.y1+17.0f,press=b.y1+11.0f;
  const Rgb wisp=mixed(rgb8(120,200,255),accent_,.25f),white=rgb8(225,242,255);
  const uint32_t frame=uint32_t(t*30);
  glint(t,HIT,.45f);
  // Spin: the angle's rate climbs through the formation and grind.
  const float spin=-(9*t+5*t*t);
  if(t<RELEASE) {
    const float r=t<FORM?2+9.5f*easeOut(t/FORM):t<HIT?11.5f:13;
    float x=sx,y=rest,squash=1;
    if(t>=FORM && t<HIT){const float u=(t-FORM)/(HIT-FORM),e=u*u;x=sx+(cx-sx)*e;y=rest+(press-rest)*e+6*sine(HALF_TURN*u);squash=1-.06f*u;}
    else if(t>=HIT){x=cx+(rnd(frame,402)-.5f)*1.6f;y=press+(rnd(frame,401)-.5f)*1.6f;squash=.88f;}
    ninjaOrb(x,y,r,squash,spin,t<FORM?.55f+.45f*smooth(t/.5f):1);
    // Formation: six streams spiral in, wave after wave.
    if(t<FORM+.05f)for(int i=0;i<6;++i)for(int wave=0;wave<2;++wave) {
      const float start=wave*.32f+.08f*rnd(i*2+wave,403),p=(t-start)/.45f;
      if(p<0 || p>=1)continue;
      const float a0=i*(2*HALF_TURN/6)+wave*.5f;
      for(int s=0;s<7;++s) {
        const float q=std::max(0.0f,p-s*.035f),rho=r+(40-r)*(1-q)*(1-q),a=a0+3.2f*q;
        dot(sx+rho*cosine(a),rest+rho*.8f*sine(a),s?wisp:white,(1-s/7.0f)*smooth(p/.2f)*(1-smooth((p-.8f)/.2f)));
      }
    }
    // Grinding: sparks fly off tangentially along the readout's lower edge.
    if(t>=HIT)for(unsigned i=0;i<24;++i) {
      const float life=.28f,a0=t-HIT+life*rnd(i,404),cycle=std::floor(a0/life),age=(a0-cycle*life)/life;
      const uint32_t s=i+uint32_t(cycle)*24;const float side=i%2?1.0f:-1.0f;
      const float v=50+70*rnd(s,405),sparkX=cx+side*(10+v*age),sparkY=press+2+(rnd(s,406)-.3f)*10*age+30*age*age;
      dot(sparkX,sparkY,i%3?wisp:white,(1-age)*smooth((t-HIT)/.05f));
    }
    // Scrape lines under the readout, spreading as it grinds.
    if(t>=HIT) {
      const float g=smooth((t-HIT)/.2f);
      const Ink c=inkOf(wisp);
      for(int side=-1;side<=1;side+=2)for(int d=0;d<int(46*g);++d) {
        const float px=cx+side*(14+d);if(px<0 || px>=w_)break;
        vdot(int(px),b.y1+3.6f+.8f*sine(d*.9f+t*40),c,int(200*(1-d/(46.0f*g+1))));
      }
    }
    return;
  }
  // Release: the sphere bursts into three spiral arms that turn outward.
  // Sustain: the arms keep turning ever more slowly, dimmed, as a lingering
  // whirlwind, and motes of wind circle out of it, each on its own path,
  // until the settle fades it all.
  const float a=t-RELEASE,fade=1-smooth((t-SETTLE)/(END-.1f-SETTLE));
  if(a<.16f)ninjaOrb(cx,press,13+7*easeOut(a/.16f),.88f,spin,1-a/.16f);
  // The blast's edge: a ring racing out across the floor.
  if(a<.45f){const float e=a/.45f,rx=16+64*easeOut(e);ellipseAt(cx,press+4,rx,rx*.42f,mixed(white,wisp,e),1.1f*(1-e)*(1-e),1.3f);}
  // Turn rate 7.6 rad/s decaying toward 0.5 rad/s.
  const float turn=-(7.1f/1.1f)*(1-std::exp(-1.1f*a))-.5f*a;
  const float level=fade*(.7f+.3f*(1-smooth((a-.15f)/.9f)));
  for(int arm=0;arm<3;++arm) {
    const float reach=14+46*easeOut(a/.6f)+4*sine(a*1.3f+arm);
    // Denser once the ring is gone and the arms have reached out.
    const int n=a<.45f?30:44;
    for(int s=0;s<=n;++s) {
      const float u=float(s)/n,rho=reach*(.2f+.8f*u),ang=turn+arm*(2*HALF_TURN/3)-2.4f*u;
      const float k=level*(.55f+.65f*(1-u));
      const float x=cx+rho*cosine(ang)*1.2f,y=press+4+rho*.5f*sine(ang);
      const Rgb c=mixed(white,wisp,std::min(1.0f,u*1.6f));
      dot(x,y,c,k);if(a<1.2f)plot(x,y+1.2f,wisp,k*.5f*(1-a/1.2f));
    }
  }
  // Wisps: a few fly off with the blast, then more circle out of the
  // whirlwind through the sustain.
  for(unsigned i=0;i<40;++i) {
    const bool first=i<10;
    const float birth=first?RELEASE+.15f*rnd(i,407):RELEASE+.4f+2.1f*rnd(i,410),life=first?.6f+.3f*rnd(i,408):1.1f+.6f*rnd(i,411);
    const float age=t-birth;if(age<0 || age>=life)continue;
    const float u=age/life,ang=2*HALF_TURN*rnd(i,409)-(first?3:1.6f)*age,d=(first?18:14)+(first?50:38)*easeOut(u);
    dot(cx+d*cosine(ang)*1.15f,press+4+d*.5f*sine(ang)-6*age,i%3?wisp:white,(first?1-u:sine(HALF_TURN*u))*.8f*fade);
  }
}

// ---- Pinwheel: a pair of red eyes opens under the readout, in the
// open space either side of the replay box's top corner. Three commas ride a
// ring round each pupil, spinning faster until they blur into one ring; the
// irises flare and the commas reform as three interlocking loops, which turn
// slowly before the eyes close. The commas and loops are left dark: on a dark
// background they are negative space in the lit iris.
// Draws both irises: the one centred at (ex,ey) and its twin mirrored about
// the readout's centre column. The twins turn together (the same pattern,
// not mirrored), so they share one mask and one set of row spans.
inline void Renderer::ninjaIris(int ex,int ey,float open,float slit,float phi,float m,float u,int flash) {
  constexpr int R=15,PUPIL=3;
  const int eyes[2]={ex,2*roundi(number_.cx())-ex};
  // In the eye's own frame: pixel (dx,dy) is mask bit 16+dx of row 16+dy.
  ninja::Mask mask;mask.x0=-16;mask.y0=-16;
  // Commas: a head on the ring and a tail curling back and outward; the
  // tails stretch as the spin blurs them toward one ring.
  const float ring=9.6f,head=2.7f*(1-m),blur=1+3.4f*smooth((u-.62f)/.38f)*(1-m);
  if(head>.25f)for(int c=0;c<3;++c) {
    const float a=phi+c*(2*HALF_TURN/3);
    mask.disc(ring*cosine(a),ring*sine(a),head);
    const int tail=int(4*blur);
    for(int j=1;j<=tail;++j) {
      const float v=float(j)/(tail+1),aj=a-j*.2f,rj=ring+1.8f*v;
      mask.disc(rj*cosine(aj),rj*sine(aj),head*(1-v)+.3f);
    }
  }
  // Loops: three circles drawn in from each comma, interlocking round the pupil.
  if(m>0)for(int c=0;c<3;++c) {
    const float a=phi+c*(2*HALF_TURN/3),lx=4.6f*cosine(a),ly=4.6f*sine(a);
    const int n=int(40*m);
    for(int s=0;s<=n;++s){const float q=a+s*(2*HALF_TURN/40);mask.disc(lx+6.6f*cosine(q),ly+6.6f*sine(q),1.1f);}
  }
  // Fill the iris, leaving the pupil, the commas, the ring and the loops dark.
  // A row at a time: the iris, the lid opening and the mask make bit masks of
  // the pixels to light, split by colour band, and each band is lit with one
  // precomputed increment (ninja::addBits) instead of addw per pixel.
  const Ink deep=inkOf(rgb8(130,0,18)),red=inkOf(rgb8(255,20,34)),hot=inkOf(rgb8(255,160,130));
  constexpr int inner=PUPIL*PUPIL-2,outer=R*R+R/2;
  // rho=(d2<<8)/outer, the colour's radius: vivid red for 40 <= rho < 214.
  constexpr int MID0=(40*outer+255)>>8,MID1=(214*outer-1)>>8;
  static_assert((MID0<<8)/outer>=40 && ((MID0-1)<<8)/outer<40 && (MID1<<8)/outer<214 && ((MID1+1)<<8)/outer>=214,"band edges");
  const int kq=q8(slit),kDim=kq>>2;
  const int ring0=int((ring-.5f)*(ring-.5f)+.5f),ring1=int((ring+.5f)*(ring+.5f)),ringOn=m<.5f;
  const float lidH=open*(R+1.5f);
  // The lid opening: an almond, widest at the centre; lid[dx] is its half
  // height at |dx|, falling with |dx|.
  float lid[R+1];
  for(int dx=0;dx<=R;++dx)lid[dx]=lidH*(1-dx*dx*(1/600.0f));
  const Ink vivid=flash?mixedInk(red,hot,flash):red;
  const ninja::Step full=ninja::step(vivid,kq),faint=ninja::step(vivid,kDim);
  // Mask columns (bit 16+dx) of dx -h..h, and of dx*dx <= v.
  auto span=[](int h){return h<0?0u:(~0u>>(31-2*h))<<(16-h);};
  auto within=[&](int v){return v<0?0u:span(ninja::ROOT[v]);};
  for(int dy=-R;dy<=R;++dy) {
    const int y=ey+dy;if(y<0 || y>bottom_)continue;
    const int ady=dy<0?-dy:dy,dy2=dy*dy;
    int open2=R;while(open2>=0 && ady>lid[open2])--open2;
    if(open2<0)continue;
    // inner <= d2 <= outer, inside the lid, not masked dark.
    uint32_t bits=span(open2)&within(outer-dy2)&~within(inner-1-dy2)&~mask.row[dy+16];
    // The ring, once the commas blur: dimmed, not dark.
    const uint32_t dim=ringOn?within(ring1-dy2)&~within(ring0-1-dy2):0u;
    // Weights under addw's threshold light nothing and are not probes.
    if(kDim<5)bits&=~dim;
    if(kq<5)bits&=dim;
    if(!bits)continue;
    // Deep at the pupil, vivid in the middle, darker at the limbus.
    const uint32_t mid=within(MID1-dy2)&~within(MID0-1-dy2);
    for(int e=0;e<2;++e) {
      // As addw: the halo words these pixels probe are built, and a
      // neighbour's ink row where light lands within HALO of a word edge,
      // before any pixel of the row is written.
      const int base=eyes[e]-16;
      uint32_t lit=bits,blk=0;
      for(int w=base>>5;w<=(base+31)>>5;++w) {
        const int lo=w*32-base;  // the local bit of the word's pixel 0
        if(!(bits&(lo>=0?~0u<<lo:~0u>>-lo)))continue;
        uint32_t word=~0u;
        if(w>=0 && w*32<w_) {
          if(blockedStamp_[y][w]!=stamp_)buildBlocked(y,w);
          word=blocked_[y][w];if((w+1)*32>w_)word|=~0u<<(w_-w*32);
        }
        blk|=lo>=0?word<<lo:word>>-lo;
      }
      lit&=~blk;
      for(int w=base>>5;w<=(base+31)>>5;++w) {
        const int lo=w*32-base;
        const uint32_t mine=lo>=0?lit>>lo:lit<<-lo;
        if((mine&3u) && w>0)ink(y,w-1);
        if((mine>>30) && (w+1)*32<w_)ink(y,w+1);
      }
      probes_+=__builtin_popcount(bits);
      uint16_t *px=px_+y*w_+base;
      int n=ninja::addBits(px,lit&mid&~dim,full)+ninja::addBits(px,lit&mid&dim,faint);
      for(uint32_t rest=lit&~mid;rest;rest&=rest-1,++n) {
        const int bit=__builtin_ctz(rest),dx=bit-16,rho=((dx*dx+dy2)<<8)/outer;
        Ink c=rho<40?mixedInk(deep,red,rho*256/40):mixedInk(red,deep,std::min(256,(rho-214)*5));
        if(flash)c=mixedInk(c,hot,flash);
        ninja::addBits(px,1u<<bit,ninja::step(c,(dim>>bit)&1u?kDim:kq));
      }
      plots_+=n;
    }
  }
  // The lid edges glow while the eyes open and close.
  if(open<1) {
    const Ink edge=inkOf(rgb8(255,100,90));
    for(int dx=-R;dx<=R;++dx)for(int side=-1;side<=1;side+=2) {
      const float y=ey+side*lid[dx<0?-dx:dx];const int dy=roundi(y)-ey;
      if(dx*dx+dy*dy<=outer)for(int e:eyes)vdot(e+dx,y,edge,int(230*slit));
    }
  }
}
inline void Renderer::ninjaEye(float t) {
  constexpr float OPEN=.4f,MORPH=1.3f,BLINK=3.05f,SHUT=4.25f;
  const Box &b=number_;
  const int ey=roundi(b.y1+17.0f),spread=roundi(std::min(37.0f,(b.x1-b.x0)*.3f));
  const int lx=roundi(b.cx())-spread,rx=roundi(b.cx())+spread;
  glint(t,MORPH,.45f);
  // Lids: open over 0.4 s, one slow blink in the sustain, closed from
  // 4.25 s; the slit lingers, then fades.
  float open=t<OPEN?easeOut(t/OPEN):t<SHUT?1:1-smooth((t-SHUT)/.4f);
  if(t>=BLINK && t<BLINK+.26f)open=1-sine(HALF_TURN*(t-BLINK)/.26f);
  const float slit=t<SHUT+.38f?1:1-smooth((t-SHUT-.38f)/.15f);
  if(slit<=0)return;
  // Spin: accelerating until the morph, then a slow turn that eases off.
  const float u=clamp01((t-.3f)/(MORPH-.3f));
  const float phi=t<MORPH?2*HALF_TURN*(.3f*u+1.9f*u*u*u):2*HALF_TURN*2.2f+(1.6f/.45f)*(1-std::exp(-.45f*(t-MORPH)))+.35f*(t-MORPH);
  const float m=smooth((t-MORPH)/.25f);
  const int flash=t>=MORPH-.04f && t<MORPH+.3f?q8(.55f*(1-smooth((t-MORPH+.04f)/.34f))):0;
  // In the sustain the irises breathe, slowly.
  const float breath=t<MORPH+.4f?1:.86f+.14f*cosine((t-MORPH-.4f)*2.4f);
  ninjaIris(lx,ey,open,slit*breath,phi,m,u,flash);  // and its twin at rx
  // The morph: a flare on each limbus and red sparks thrown off.
  if(t>=MORPH-.04f && t<MORPH+.5f) {
    const float f=1-smooth((t-MORPH+.04f)/.54f);
    const Rgb flare=mixed(rgb8(255,60,60),WHITE,.3f);
    // A ring of 128 points round each limbus (ellipseAt's density 4), the
    // points' offsets shared by the two eyes.
    if(f>.02f) {
      const Ink c=inkOf(flare);const int kq=q8(f);
      for(int i=0;i<128;++i) {
        const float a=i*(2*HALF_TURN/128);const int ox=roundi(16*cosine(a)),oy=ey+roundi(16*sine(a));
        addw(lx+ox,oy,c,kq);addw(rx+ox,oy,c,kq);
      }
    }
    for(unsigned i=0;i<12;++i) {
      const float age=t-MORPH,ang=2*HALF_TURN*rnd(i,501);if(age<0)continue;
      const float d=17+12*easeOut(age/.5f)*(.6f+.4f*rnd(i,502));
      dot((i%2?rx:lx)+d*cosine(ang),ey+d*sine(ang),rgb8(255,70,60),f);
    }
  }
  // Sustain: red embers lift off the eyes' outer rims, drift up and fade.
  for(unsigned i=0;i<18;++i) {
    const float birth=MORPH+.35f+3.1f*rnd(i,503),life=.8f+.6f*rnd(i,504),age=t-birth;if(age<0 || age>=life)continue;
    const bool right=i%2;const float q=age/life,ang=(right?0:HALF_TURN)+(rnd(i,505)-.3f)*1.2f*(right?-1:1);
    const float side=right?float(rx):float(lx),out=right?1.0f:-1.0f;
    const float x=side+17*cosine(ang)+out*6*q+2*sine(age*5+i),y=ey+17*sine(ang)-9*q;
    dot(x,y,i%3?rgb8(255,60,50):rgb8(255,150,110),.75f*sine(HALF_TURN*q));
  }
}

// ---- Overdrive: heartbeat by heartbeat, eight beats accelerating, a
// green aura wraps every glyph of the readout (shells of light just outside
// its outline, one more shell every few beats, shimmering upward), motes are
// drawn up into it and pebbles lift off the floor below; the eighth beat
// erupts in a white-green blaze, a pulse and a burst of steam, and the aura
// dies down as the pebbles fall. The shells follow the UI's own outline, so
// the readout and its label glow like a powered-up silhouette. They come
// from the halo mask itself, a word at a time.
inline void Renderer::ninjaGates(float t) {
  static constexpr float BEATS[8]={.06f,.32f,.53f,.69f,.81f,.9f,.97f,1.03f};
  static constexpr float AFTER[3]={1.95f,2.8f,3.55f};
  constexpr float BURST=1.03f,SETTLE=3.8f,END=4.8f;
  const Box &b=number_;
  const float cx=b.cx(),cy=b.cy(),hw=(b.x1-b.x0)*.5f,hh=(b.y1-b.y0)*.5f;
  const Rgb green=mixed(rgb8(60,255,120),accent_,.12f),mint=rgb8(190,255,210),steam=rgb8(200,240,215);
  glint(t,BURST,.45f);
  // Power: a step per beat, each eased in over 60 ms.
  float power=0;
  for(float beat:BEATS)power+=smooth((t-beat)/.06f)*.125f;
  const float flare=t<BURST?0:1-smooth((t-BURST)/.55f);
  const float fade=1-smooth((t-SETTLE)/(END-.15f-SETTLE));
  // Sustain: the aura holds lower and breathes, and three slower, fainter
  // beats follow, as the heart calms.
  float calm=t<BURST?1:.7f+.3f*(1-smooth((t-BURST)/.7f))+.07f*sine((t-BURST)*3.3f);
  for(float beat:AFTER)if(t>=beat)calm+=.3f*std::exp(-9*(t-beat))*smooth((t-beat)/.04f);
  // Each beat throws a pulse off the readout, wider as the power builds.
  for(int i=0;i<8;++i) {
    const float age=t-BEATS[i],life=i==7?.45f:.26f;if(age<0 || age>=life)continue;
    const float e=age/life,grow=(i==7?20:8+1.5f*i)*easeOut(e);
    ellipseAt(cx,cy,hw+6+grow,hh+6+grow*.4f,i==7?mint:green,(1-e)*(1-e)*(i==7?1:.7f),1.1f);
  }
  for(float beat:AFTER) {
    const float age=t-beat;if(age<0 || age>=.3f)continue;
    const float e=age/.3f;ellipseAt(cx,cy,hw+6+10*easeOut(e),hh+6+4*easeOut(e),green,.45f*(1-e)*(1-e)*fade,1.0f);
  }
  // The aura shells, from the halo mask: shell s is the ring of pixels one
  // step (a 3x3 dilation) outside shell s-1, the first just outside the halo.
  const float strength=(.25f+.75f*power)*(1+.6f*flare)*calm*fade;
  const int shells=t>BURST+.6f?2:std::min(3,1+int(power*2.4f+flare*1.5f));
  if(strength>.03f) {
    constexpr int ROWS=MAX_H<64?MAX_H:64;
    const int words=(w_+31)/32,y0=std::max(0,b.y0-5),y1=std::min({bottom_,b.y1+5,y0+ROWS-3});
    const int n=y1-y0+3;  // local rows y0-1..y1+1
    // One mask, dilated in place a row at a time; `above` keeps the row above
    // as it was before this shell (a second whole mask would not fit the
    // loop task's stack budget).
    uint32_t d[ROWS][WORDS],above[WORDS],was[WORDS];
    const uint32_t lastMask=(w_&31)?(1u<<(w_&31))-1:~0u;
    for(int r=0;r<n;++r) {
      const int y=y0-1+r;
      for(int i=0;i<words;++i) {
        // Rows past the readout's halo start clear; addw still masks them.
        if(y<0 || y>bottom_ || y<b.y0-3 || y>b.y1+3){d[r][i]=0;continue;}
        if(blockedStamp_[y][i]!=stamp_)buildBlocked(y,i);
        d[r][i]=blocked_[y][i];
      }
    }
    // One 3x3 dilation of a mask row, across word boundaries.
    auto spread=[&](const uint32_t *row,int i){
      const uint32_t c=row[i],l=i>0?row[i-1]>>31:0,rr=i+1<words?row[i+1]<<31:0;
      return c|c<<1|l|c>>1|rr;
    };
    const Ink inks[4]={inkOf(mixed(mint,green,.25f)),inkOf(green),inkOf(mixed(green,accent_,.3f)),inkOf(green)};
    static constexpr int LEVEL[4]={256,230,150,90};
    const int tq=int(t*700),kq=q8(std::min(1.5f,strength));
    for(int s=0;s<shells;++s) {
      const int level=(kq*LEVEL[s])>>8;
      std::memcpy(above,d[0],sizeof(above));
      for(int r=1;r<n-1;++r) {
        const int y=y0-1+r;
        std::memcpy(was,d[r],sizeof(was));
        uint32_t ring[WORDS];
        for(int i=0;i<words;++i) {
          // The outer shells climb: they grow from the row below and sideways only.
          uint32_t g=spread(was,i)|(r+1<n?spread(d[r+1],i):0);if(s==0)g|=spread(above,i);
          ring[i]=g&~was[i]&(i==words-1?lastMask:~0u);
          d[r][i]|=ring[i];
        }
        std::memcpy(above,was,sizeof(above));
        for(int i=0;i<words;++i)for(uint32_t bits=ring[i];bits;bits&=bits-1) {
          const int x=i*32+__builtin_ctz(bits);
          // Bands that climb: a triangle wave of height and time, offset per column.
          const int ph=(y*26+(lattice(x>>1,s,61)>>2)+tq)&255,tri=ph<128?ph:255-ph;
          if(s && ((x+y)&1))continue;  // outer shells dithered: a softer glow
          addw(x,y,inks[s],(level*(70+tri))>>8);
        }
      }
    }
  }
  // Motes are drawn up into the aura from below and above the readout.
  for(unsigned i=0;i<26;++i) {
    const float life=.45f+.2f*rnd(i,611),a0=t+life*rnd(i,612),cycle=std::floor(a0/life),age=(a0-cycle*life)/life;
    const uint32_t s=i+uint32_t(cycle)*26;
    const bool top=i%3==0;
    const float x=b.x0+3+(b.x1-b.x0-6)*rnd(s,613)+2*sine(age*9+i),y=top?b.y0-3-8*age:b.y1+13-9*age;
    dot(x,y,i%2?mint:green,strength*.9f*sine(HALF_TURN*age));
  }
  // Pebbles lift off the floor as the power builds, are blasted out by the
  // last beat and fall away.
  for(unsigned i=0;i<10;++i) {
    const float x0=b.x0+4+(b.x1-b.x0-8)*(i+.5f*rnd(i,604))/10,y0=b.y1+15+3*rnd(i,605);
    float x=x0,y=y0;
    const float lift=4+5*rnd(i,606);
    if(t<BURST){y-=power*lift;x+=power*.7f*sine(t*45+i*2.3f);}
    else {
      const float a=t-BURST,vx=(x0-cx)*1.1f,vy=-(40+50*rnd(i,607));
      x+=vx*a;y+=-lift+vy*a+160*a*a;
    }
    const float k=smooth((t-.03f)/.2f)*(1-smooth((t-BURST-.15f)/.4f));
    if(k<=.02f)continue;
    const Rgb stone=mixed(rgb8(150,135,110),green,.2f);
    const int ix=roundi(x),iy=roundi(y);const Ink c=inkOf(stone),rim=inkOf(green);const int kk=q8(k);
    addw(ix,iy,c,kk);addw(ix+1,iy,c,kk);addw(ix,iy+1,c,kk);addw(ix+1,iy+1,c,kk);
    addw(ix,iy-1,rim,int(kk*power));addw(ix+1,iy-1,rim,int(kk*power*.6f));
  }
  // Steam bursts up off the readout and drifts away.
  // Steam: a burst off the readout, then wisps venting through the sustain.
  if(t>=BURST)for(unsigned i=0;i<28;++i) {
    const float birth=i<16?BURST+.12f*rnd(i,608):BURST+.5f+2.4f*rnd(i,614);
    const float age=t-birth,life=.7f+.5f*rnd(i,609);if(age<0 || age>=life)continue;
    const float u=age/life;const bool top=i%3!=0;
    const float x=b.x0+4+(b.x1-b.x0-8)*rnd(i,610)+5*sine(age*5+i),y=top?b.y0-3-9*easeOut(u):b.y1+5+8*u;
    const float r=1+2.2f*u,k=(i<16?.8f:.5f)*(1-u)*smooth(age/.06f)*fade;
    dot(x,y,steam,k);dot(x-r,y+r*.4f,steam,k*.6f);dot(x+r,y+r*.3f,steam,k*.6f);
  }
}

// ---- Summon: a palm slams down under the readout; six lines of
// glowing script race out across the floor and close in a ring; then a
// cloud of smoke bursts out of the seal, billows and thins away. The cloud
// is drawn as puffs with a shared outline (a puff's rim is skipped inside
// another puff) and an ordered-dither body that thins as it clears.
inline void Renderer::ninjaSummon(float t) {
  constexpr float SLAM=.04f,POOF=.62f,SETTLE=3.9f,END=5.0f;
  constexpr int PUFFS=9;
  const Box &b=number_;
  const float px=b.cx(),py=b.y1+13.0f,squash=.32f;
  const Rgb ink=mixed(rgb8(255,214,150),accent_,.2f),smoke=rgb8(230,234,242),shade=mixed(rgb8(150,160,185),accent_,.25f);
  glint(t,POOF,.45f);
  // The slam: a flash and dust kicked sideways.
  if(t>=SLAM && t<SLAM+.2f) {
    const float a=(t-SLAM)/.2f;
    spark(px,py,ink,1.4f*(1-a),3);
    for(int side=-1;side<=1;side+=2)for(int i=0;i<4;++i)dot(px+side*(5+i*5+30*a),py+1-i*.7f-6*a,smoke,(1-a)*(.8f-.15f*i));
  }
  // Script: six lines of glyphs written outward across the floor.
  const float sealFade=1-smooth((t-POOF-.2f)/.5f);
  if(sealFade>0 && t>=SLAM) {
    const Ink glyph=inkOf(ink);
    for(int l=0;l<6;++l) {
      const float ang=l*(2*HALF_TURN/6)+.26f,ca=cosine(ang),sa=sine(ang);
      const float full=50+8*rnd(l,701),len=full*easeOut((t-SLAM-.03f*l)/.42f);
      const int kq=q8(.9f*sealFade);
      for(int g=0;g*4+6<=int(len);++g) {
        // One glyph every 4 px: a short tick, a dot or a cross, by hash.
        const float r=6+g*4,gx=px+r*ca,gy=py+r*sa*squash;
        const int kind=int(hash(uint32_t(l*64+g)*2654435761u)&3);
        const int x=roundi(gx),y=roundi(gy);
        addw(x,y,glyph,kq);
        if(kind==0){addw(x,y-1,glyph,kq);addw(x,y-2,glyph,kq>>1);}
        else if(kind==1){addw(x-1,y,glyph,kq>>1);addw(x+1,y,glyph,kq>>1);addw(x,y-1,glyph,kq>>1);}
        else if(kind==2)addw(x+1,y-1,glyph,kq);
      }
      if(len<full-1)dot(px+len*ca,py+len*sa*squash,WHITE,sealFade);
    }
    // The ring closes round the lines' ends.
    const float ringU=clamp01((t-.42f)/.16f);
    if(ringU>0) {
      const int n=int(150*ringU);const Ink c=inkOf(ink);const int kq=q8(.8f*sealFade);
      float ca,sa,cd,sd;ninja::cosSin(.26f,ca,sa);ninja::cosSin(2*HALF_TURN/150,cd,sd);
      for(int i=0;i<n;++i,ninja::turn(ca,sa,cd,sd))addw(ninja::Mask::floorOf(px+58*ca+.5f),ninja::Mask::floorOf(py+58*squash*sa+.5f),c,(i&3)?kq:kq>>1);
    }
  }
  if(t<POOF)return;
  const float a=t-POOF;
  // The pop: a white flash and burst lines.
  if(a<.1f)ninjaGlow(px,py+3,6+7*a/.1f,smoke,.9f*(1-a/.1f));
  if(a<.16f) {
    const float u=a/.16f;
    for(int i=0;i<10;++i) {
      const float ang=i*(2*HALF_TURN/10)+.3f,r0=10+46*easeOut(u),ca=cosine(ang),sa=sine(ang)*.5f;
      line(px+r0*ca,py+r0*sa,px+(r0+9)*ca,py+(r0+9)*sa,smoke,(1-u)*.9f);
    }
  }
  // The puffs: thrown out along the floor and up, swelling; then, through
  // the sustain, the cloud lingers, each puff drifting apart and bobbing on
  // its own, thinning away by about 3.6 s.
  float X[PUFFS],Y[PUFFS],R[PUFFS],K[PUFFS];
  for(int i=0;i<PUFFS;++i) {
    const float age=std::max(0.0f,a-.04f*rnd(i,702)),life=3.0f+.5f*rnd(i,703),u=std::min(1.0f,age/life);
    const float side=i==0?0:(i%2?1.0f:-1.0f),slot=float((i+1)/2);
    const float out=easeOut(age/.35f);
    const float drift=age<.6f?age:.6f+(age-.6f)*.45f;
    const float dx=side*(12*slot+6*rnd(i,704))*out+side*8*drift+1.6f*sine(age*(1.1f+.5f*rnd(i,707))+i),dy=(i==0?2:(slot>3?-1:9-3*slot))*out-(3+3*rnd(i,705))*drift+1.2f*sine(age*1.4f+2*i);
    X[i]=px+dx;Y[i]=py+2+dy;
    R[i]=(3+(slot>3?7:9+3*rnd(i,706))*out)*(1+.1f*drift);
    K[i]=(1-smooth((u-.25f)/.75f))*smooth(age/.03f);
  }
  static constexpr uint8_t BAYER[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
  const Ink rim=inkOf(smoke),body=inkOf(shade);
  // Body: ordered dither, by screen position, so overlapping puffs share
  // pixels; each pixel belongs to the first puff that covers it. A row at a
  // time: one square root (ninja::root) per puff per row gives its span as
  // bit masks a word wide, and what earlier puffs have not claimed is lit
  // through the puff's dither row (no per-pixel test against other puffs).
  // Not on the readout's rows: it would be masked there anyway.
  {
    int ya=bottom_+1,yb=-1;
    ninja::Step steps[PUFFS];uint32_t dither[PUFFS][4]={};
    for(int i=0;i<PUFFS;++i) {
      if(K[i]<=.02f)continue;
      ya=std::min(ya,int(Y[i]-R[i]));yb=std::max(yb,int(Y[i]+R[i])+1);
      const int density=int(5*K[i]+.5f),kb=q8(.6f*K[i]);
      // Weights under addw's threshold light nothing (the puff still claims).
      // Its dither, row y&3: bit x where BAYER[...] < density, every 4 px.
      if(kb>=5)for(int v=0;v<4;++v)for(int x=0;x<4;++x)if(BAYER[v*4+x]<density)dither[i][v]|=0x11111111u<<x;
      steps[i]=ninja::step(body,kb);
    }
    for(int y=std::max(ya,number_.y1+2);y<=std::min(yb,bottom_);++y) {
      uint32_t claimed[WORDS]={},lit[PUFFS][WORDS];
      for(int i=0;i<PUFFS;++i) {
        for(auto &word:lit[i])word=0;
        if(K[i]<=.02f)continue;
        const float r=R[i],dy=y-Y[i];if(dy*dy>=r*r)continue;
        const float span=ninja::root(r*r-dy*dy);
        const int xa=std::max(0,int(X[i]-span)+1),xb=std::min(w_-1,int(X[i]+span));
        if(xa>xb)continue;
        const uint32_t pattern=dither[i][y&3];
        for(int w=xa>>5;w<=xb>>5;++w) {
          const int lo=std::max(xa,w*32)-w*32,hi=std::min(xb,w*32+31)-w*32;
          const uint32_t covers=(~0u>>(31-(hi-lo)))<<lo;
          lit[i][w]=covers&~claimed[w]&pattern;claimed[w]|=covers;
        }
      }
      // As addw, a word at a time: the word's halo is built, and a
      // neighbour's ink row where light lands within HALO of its edge,
      // before any of its pixels is written; then each puff's pixels get
      // its one step, with no mask lookup per pixel.
      for(int w=0;w*32<w_;++w) {
        uint32_t all=0;for(int i=0;i<PUFFS;++i)all|=lit[i][w];
        if(!all)continue;
        probes_+=__builtin_popcount(all);
        if(blockedStamp_[y][w]!=stamp_)buildBlocked(y,w);
        uint32_t free=~blocked_[y][w];if((w+1)*32>w_)free&=~(~0u<<(w_-w*32));
        if(!(all&free))continue;
        if((all&free&3u) && w>0)this->ink(y,w-1);  // (`ink` here is the script colour)
        if(((all&free)>>30) && (w+1)*32<w_)this->ink(y,w+1);
        uint16_t *row=px_+y*w_+w*32;
        for(int i=0;i<PUFFS;++i)if(lit[i][w]&free)plots_+=ninja::addBits(row,lit[i][w]&free,steps[i]);
      }
    }
  }
  for(int i=0;i<PUFFS;++i) {
    if(K[i]<=.02f)continue;
    const float r=R[i];const int kq=q8(K[i]);
    // Rim: only where no other puff covers it, brighter on top. A faint
    // rim (every weight under addw's threshold) lights nothing.
    if(((kq*255)>>8)<5)continue;
    // The point at angle q of this circle is inside puff j's disc (radius rj
    // = R[j]-.6) when |p - c_j|^2 < rj^2, which on this circle is
    // cos(q)*ux + sin(q)*uy > cut, (ux,uy) = c_j - c_i: a division per
    // pair, then two multiplies per point. Pairs whose disc misses the
    // circle are dropped.
    float ux[PUFFS],uy[PUFFS],cut[PUFFS];int nearN=0;
    for(int j=0;j<PUFFS;++j) {
      if(j==i || K[j]<=.02f)continue;
      const float rj=R[j]-.6f,ex=X[j]-X[i],ey=Y[j]-Y[i],d2=ex*ex+ey*ey;
      // |p-c_j|^2 = r^2+d2-2(p-c_i).(c_j-c_i) < rj^2 iff (p-c_i).(c_j-c_i) > need.
      const float need=(r*r+d2-rj*rj)*.5f;
      if(need>=0 && need*need>=r*r*d2)continue;  // beyond r*|c_j-c_i|: never inside
      ux[nearN]=ex;uy[nearN]=ey;cut[nearN]=need/r;++nearN;
    }
    const int n=std::max(12,int(r*6.6f));
    // The circle's points by rotation, one step per point.
    float cd,sd;ninja::cosSin(2*HALF_TURN/n,cd,sd);
    float ca=1,sa=0;
    for(int s=0;s<n;++s,ninja::turn(ca,sa,cd,sd)) {
      const float y=Y[i]+r*sa;if(y<number_.y1+2)continue;
      const int w=(kq*(205-int(50*sa)))>>8;if(w<5)continue;
      bool inside=false;
      for(int k=0;k<nearN && !inside;++k)inside=ca*ux[k]+sa*uy[k]>cut[k];
      if(!inside)addw(ninja::Mask::floorOf(X[i]+r*ca+.5f),ninja::Mask::floorOf(y+.5f),rim,w);
    }
  }
  // Wisps curl off the cloud through the sustain and the settle.
  const float fade=1-smooth((t-SETTLE)/(END-.1f-SETTLE));
  for(unsigned i=0;i<30;++i) {
    const float birth=POOF+.6f+3.5f*rnd(i,710),life=.8f+.6f*rnd(i,711),age=t-birth;if(age<0 || age>=life)continue;
    const float q=age/life,x=px+(rnd(i,708)-.5f)*110+3*sine(age*4+i)+(rnd(i,712)-.5f)*8*q,y=py+4+22*rnd(i,709)-12*q;
    dot(x,y,i%3?smoke:shade,.7f*sine(HALF_TURN*q)*fade);
  }
}
}
