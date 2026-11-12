#pragma once
// The corsair set of 0.18 (Slingshot..Vapor): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Same rules as
// the other sets: additive light on the background outside the UI halo, a
// pure function of elapsed time with fixed seeds, the readout exactly as
// drawn from three quarters of the duration, every frame within the cost
// model's 8 ms.
//   Slingshot: a big fist on a rubber arm winds up at the screen's right
//              edge, yanks back and snaps across under the readout on a
//              twisting arm, knocks the readout's foot sideways with a
//              comic burst and a ring, then rebounds like rubber while focus
//              lines flicker round the hit.
//   Presence:  the air goes still and the readout darkens; then black
//              lightning with red rims tears along the band under it, a wave
//              of will rolls out over the floor, and aftershocks and gold
//              sparks follow.
//   Threefold: three blades line up and dash across the band under the
//              readout; after a beat three cuts open at once where they
//              passed, two crossed and one level, and cool to green wakes
//              that smoke and gleam.
//   Banner:    a furled black flag is hoisted up a mast under the readout,
//              snaps open in the wind and billows, a plain crossed-bones
//              mark on it, while sea spray blows past.
//   Vapor:     the body pumps up in five quickening beats, pink steam
//              puffing from under the readout, which flushes warm; then a
//              burst of steam and bursts of afterimage streaks blur past.
// Touching the readout: Slingshot knocks its foot sideways (exact by 1.3 s);
// Presence darkens it toward crimson, never below 3:1, and gives it back
// at 0.55 s; Vapor sends warm pulses up it (pink-white, brightening only)
// until 1.11 s. The others leave it alone apart from the glint.
namespace BestEffectUI {
namespace corsair {
// 65536/n for n = 1..256: the line stepper divides by a table lookup, since
// a float division is a library call on the ESP32-S3.
constexpr std::array<int32_t,257> recipTable(){std::array<int32_t,257> t{};t[0]=65536;for(int i=1;i<=256;++i)t[i]=65536/i;return t;}
constexpr std::array<int32_t,257> RECIP=recipTable();
}

// A fixed-point line with its weight ramping k0..k1 (Q8), as sorcLine but
// with no division (the steps come from the reciprocal table); points off
// the screen are skipped before the probe.
inline void Renderer::corsairLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1) {
  const int dx8=q8(x1-x0),dy8=q8(y1-y0),n=std::min(256,std::max(1,std::max(std::abs(dx8),std::abs(dy8))>>8));
  const int r=corsair::RECIP[n];
  int x8=q8(x0)+128,y8=q8(y0)+128,wq=k0<<8;
  // |dx8| < (n+1)*256, so dx8*r stays under 2^25: no 64-bit product.
  const int sx=(dx8*r)>>16,sy=(dy8*r)>>16,dw=((k1-k0)*r)>>8;
  for(int i=0;i<=n;++i,x8+=sx,y8+=sy,wq+=dw) {
    const int x=x8>>8,y=y8>>8;
    if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_))addw(x,y,c,wq>>8);
  }
}
// 1/sqrt(dx*dx+dy*dy) without a library call: the bit-level first guess and
// two Newton steps (relative error under 0.0005%), for per-segment normals.
inline float Renderer::corsairInvLen(float dx,float dy) {
  const float v=dx*dx+dy*dy+.01f;uint32_t i;std::memcpy(&i,&v,4);i=0x5f3759dfu-(i>>1);float y;std::memcpy(&y,&i,4);
  y*=1.5f-.5f*v*y*y;y*=1.5f-.5f*v*y*y;return y;
}

// A stretched rubber arm from (sx,sy) to the wrist (fx,fy), hw px either
// side of its axis and bowed by `wobble` px at the middle (a standing wave
// with fixed ends): lit edges round a dim body, with one strand of light
// winding round it whose phase is `spin` (bright on the near side). Drawn a
// pixel at a time along its major axis, as a span across it, so the edges
// have no gaps or double-lit pixels; the body is a checker dither, and
// off-screen pixels are skipped.
inline void Renderer::corsairArm(float sx,float sy,float fx,float fy,float hw,float wobble,float spin,const Ink &edge,const Ink &fill,const Ink &core,int kq) {
  const float dx=fx-sx,dy=fy-sy,len=std::sqrt(dx*dx+dy*dy);if(len<1 || kq<5)return;
  const bool flat=std::fabs(dx)>=std::fabs(dy);
  const float major=flat?dx:dy,minor=flat?dy:dx,m0=flat?sx:sy,c0=flat?sy:sx,stretch=len/std::fabs(major);
  const int lim=flat?w_-1:bottom_,other=flat?bottom_:w_-1;
  const int from=std::max(0,int(std::ceil(std::min(m0,m0+major)))),to=std::min(lim,int(std::floor(std::max(m0,m0+major))));
  const int kf=(kq*110)>>8;
  // Along the axis the arch's and the twist's angles grow linearly, so
  // their (cos, sin) advance by rotation: no division or table per column.
  const float imaj=1/major,s0=(from-m0)*imaj,sgn=major>0?1.0f:-1.0f,da=HALF_TURN*imaj,dt=.42f*stretch*sgn;
  float ac=cosine(HALF_TURN*s0),as=sine(HALF_TURN*s0),tc=cosine(spin+std::fabs(from-m0)*.42f*stretch),ts=sine(spin+std::fabs(from-m0)*.42f*stretch);
  const float acd=cosine(da),asd=sine(da),tcd=cosine(dt),tsd=sine(dt);
  for(int m=from;m<=to;++m) {
    const float s=(m-m0)*imaj,arch=as,c=c0+minor*s+wobble*arch*stretch,h=hw*stretch*(1-.25f*arch),stripe=c+h*.75f*ts;
    const int lo=roundi(c-h),hi=roundi(c+h),ks=(kq*(150+int(106*tc)))>>8,st=roundi(stripe);
    {float u=ac*acd-as*asd;as=as*acd+ac*asd;ac=u;u=tc*tcd-ts*tsd;ts=ts*tcd+tc*tsd;tc=u;}
    for(int q=std::max(0,lo);q<=std::min(other,hi);++q) {
      const int x=flat?m:q,y=flat?q:m;
      if(q==st)addw(x,y,core,ks);else if(q==lo || q==hi)addw(x,y,edge,kq);else if((m+q)&1)addw(x,y,fill,kf);
    }
  }
}
// A fist facing (dx,dy), r px across the knuckles: a lit rim round a warm
// body, flatter across the knuckles, with three knuckle creases and the
// thumb's fold left dark as negative space.
inline void Renderer::corsairFist(float x,float y,float dx,float dy,float r,const Ink &edge,const Ink &fill,int kq) {
  if(kq<5)return;
  const float nx=-dy,ny=dx,a=r*1.05f,b=r*.95f;
  // The thumb's fold: from the top of the front face back across the side.
  const float t0u=.5f*a,t0v=-.2f*b,t1u=-.35f*a,t1v=.42f*b,tu=t1u-t0u,tv=t1v-t0v,tl=1/(tu*tu+tv*tv),ia=1/a,ib=1/b;
  const int x0=std::max(0,int(x-r-2)),x1=std::min(w_-1,int(x+r+2)),y0=std::max(0,int(y-r-2)),y1=std::min(bottom_,int(y+r+2));
  for(int py=y0;py<=y1;++py)for(int px=x0;px<=x1;++px) {
    const float ex=px-x,ey=py-y,lu=ex*dx+ey*dy,lv=ex*nx+ey*ny;
    // u/(1-0.14u^2) by its series to the third term: no division per pixel.
    float u=lu*ia;const float v=lv*ib;if(u>0){const float q=.14f*u*u;u*=1+q*(1+q);}
    const float d=u*u+v*v;if(d>=1)continue;
    if(d>.6f){addw(px,py,edge,kq);continue;}
    // Creases between the four knuckles, on the front face.
    if(u>.3f && (std::fabs(lv)<.65f || std::fabs(std::fabs(lv)-b*.5f)<.65f))continue;
    const float f=std::max(0.0f,std::min(1.0f,((lu-t0u)*tu+(lv-t0v)*tv)*tl)),gu=lu-t0u-tu*f,gv=lv-t0v-tv*f;
    if(gu*gu+gv*gv<.3f)continue;
    addw(px,py,fill,(kq*int(215-45*v))>>8);
  }
}
// A comic-book impact burst: a star outline of `points` spikes with seeded
// lengths, inner radius `inner` times the outer, squashed to the screen's
// perspective; a bright core line with a glow row under it.
inline void Renderer::corsairStar(float x,float y,float r,float inner,int points,uint32_t seed,const Ink &c,const Ink &glow,int kq) {
  if(kq<5 || r<1)return;
  float px=0,py=0;
  for(int i=0;i<=2*points;++i) {
    const int j=i%(2*points);const float an=j*(HALF_TURN/points)+.3f*rnd(seed,511);
    const float rr=j&1?r*inner*(.85f+.3f*rnd(seed*64+j,512)):r*(.75f+.45f*rnd(seed*64+j,513));
    const float qx=x+rr*cosine(an),qy=y+rr*.78f*sine(an);
    if(i){corsairLine(px,py,qx,qy,c,kq,kq);corsairLine(px,py+1,qx,qy+1,glow,kq>>1,kq>>1);}
    px=qx;py=qy;
  }
}
// A soft round flash: light falling off smoothly from the centre to radius
// r (squashed vertically by `squash`), in fixed point.
inline void Renderer::corsairFlash(float x,float y,float r,float squash,const Ink &c,int kq) {
  if(kq<5 || r<1)return;
  // Squared distance in Q16 of the radius: one float product per row, then
  // integers per pixel (column distance in Q8).
  const float ry=r*squash,isq=1/squash,fin=65536/(r*r);const int inv=int(fin),cx8=q8(x);
  const int x0=std::max(0,int(x-r)),x1=std::min(w_-1,int(x+r)+1);
  for(int py=std::max(0,int(y-ry));py<=std::min(bottom_,int(y+ry)+1);++py) {
    const float dy=(py-y)*isq;const int dy2=int(dy*dy*fin);if(dy2>=65536)continue;
    for(int px=x0;px<=x1;++px) {
      const int dx8=(px<<8)-cx8,d=((((dx8*dx8)>>8)*inv)>>8)+dy2;if(d>=65536)continue;
      const int f=(65536-d)>>8;addw(px,py,c,(((f*f)>>8)*kq)>>8);
    }
  }
}
// An axis-aligned ellipse of single pixels, about 1.6 per pixel of radius.
inline void Renderer::corsairRing(float cx,float cy,float rx,float ry,const Ink &c,int kq) {
  if(kq<5)return;
  // The angle advances by rotating (c,s) a fixed step: no trigonometry per point.
  const int n=std::max(16,int((rx+ry)*1.6f));const float step=2*HALF_TURN/n,cd=cosine(step),sd=sine(step);
  float c0=1,s0=0;
  for(int i=0;i<n;++i) {
    const int x=roundi(cx+rx*c0),y=roundi(cy+ry*s0);
    if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_))addw(x,y,c,kq);
    const float t=c0*cd-s0*sd;s0=s0*cd+c0*sd;c0=t;
  }
}
// Manga focus lines: `lines` thin strokes from the screen's side edges
// toward (cx,cy), up to `reach` px long and tapering inward, seeded by
// `seed`. Lines that would start above the readout's foot are skipped.
inline void Renderer::corsairFocus(float cx,float cy,int lines,uint32_t seed,float reach,const Ink &c,int kq) {
  if(kq<5)return;
  const float top=number_.y1+6.0f;
  for(int i=0;i<lines;++i) {
    // From the left or right edge, within 40 degrees of level: the lines
    // keep to the edge's mask words.
    const uint32_t s=seed*64+i;const bool left=i&1;
    const float an=(rnd(s,521)-.5f)*1.4f,ux=(left?-1:1)*cosine(an),uy=sine(an);
    const float d=(left?cx:w_-1-cx)/std::fabs(ux),ex=cx+ux*d,ey=cy+uy*d;
    if(ey<top || ey>bottom_ || d<12)continue;
    const float len=std::min(d-8,reach*(.45f+.55f*rnd(s,522)));
    corsairLine(ex,ey,ex-ux*len,ey-uy*len,c,kq,0);
  }
}

// A wave of pressure rolling out over the floor: the near half of an
// ellipse round (cx,cy), a bright edge two rows deep with a glow row in the
// `back` colour above it. Its edge ripples a little (seeded), and points
// off the screen are skipped before a probe.
inline void Renderer::corsairWave(float cx,float cy,float rx,float ry,const Ink &front,const Ink &back,int kq,uint32_t seed) {
  if(kq<5 || rx<2)return;
  // Only the near half and a little more: the far half runs behind the
  // readout and the header, where it would cost mask words for nothing.
  // The angle and the ripple's phase advance by rotation: no trigonometry
  // per point.
  const int n=std::max(16,int(4.8f*std::max(rx,ry)));const float step=1.2f*HALF_TURN/n,ph=6*rnd(seed,551),a0=-.1f*HALF_TURN;
  const float cd=cosine(step),sd=sine(step),rcd=cosine(7*step),rsd=sine(7*step);
  float c=cosine(a0),s=sine(a0),rc=cosine(a0*7+ph),rs=sine(a0*7+ph);
  int lx=-1,ly=-1;
  for(int i=0;i<=n;++i) {
    const float rip=1+.035f*rs,x=cx+rx*rip*c,y=cy+ry*rip*s;
    {const float t=c*cd-s*sd;s=s*cd+c*sd;c=t;const float u=rc*rcd-rs*rsd;rs=rs*rcd+rc*rsd;rc=u;}
    if(x<0 || x>=w_ || y<1 || y>=bottom_)continue;
    const int px=int(x+.5f),py=int(y+.5f);if(px==lx && py==ly)continue;lx=px;ly=py;
    // A bright core two rows deep with a glow row above it.
    addw(px,py,front,kq);addw(px,py+1,front,(kq*200)>>8);addw(px,py-1,back,(kq*130)>>8);
  }
}

// Black lightning, bold: a jagged bolt (midpoint displacement, 16
// segments, `rough` of its length at most off the line) whose 3 px core is
// left dark between two bright rims, each with a dimmer glow row outside it.
inline void Renderer::corsairBlackBolt(float x0,float y0,float x1,float y1,uint32_t seed,const Ink &rim,const Ink &glow,int kq,float rough) {
  if(kq<5)return;
  float xs[17],ys[17];xs[0]=x0;ys[0]=y0;xs[16]=x1;ys[16]=y1;
  for(int step=16;step>1;step/=2)for(int i=0;i+step<=16;i+=step) {
    const int m=i+step/2;const float dx=xs[i+step]-xs[i],dy=ys[i+step]-ys[i];
    const float off=(rnd(seed*32+m,591)-.5f)*rough*(step==2?1.6f:1);
    xs[m]=(xs[i]+xs[i+step])*.5f-dy*off;ys[m]=(ys[i]+ys[i+step])*.5f+dx*off;
  }
  for(int i=0;i<16;++i) {
    const float dx=xs[i+1]-xs[i],dy=ys[i+1]-ys[i],inv=corsairInvLen(dx,dy),nx=-dy*inv,ny=dx*inv;
    for(int side=-1;side<=1;side+=2) {
      corsairLine(xs[i]+side*2*nx,ys[i]+side*2*ny,xs[i+1]+side*2*nx,ys[i+1]+side*2*ny,rim,kq,kq);
      corsairLine(xs[i]+side*3*nx,ys[i]+side*3*ny,xs[i+1]+side*3*nx,ys[i+1]+side*3*ny,glow,kq>>1,kq>>1);
    }
  }
}

// A sword cut through the air: a lens-shaped slit `half` px either side of
// (cx,cy) along `angle`, `width` px across at its middle and tapering to
// points, a bright core with glow rows either side once it is wide enough;
// nothing lands in the gaps between the readout's glyphs.
// `gleam` (0..1, or <0 for none) places a brighter run along it.
inline void Renderer::corsairCut(float cx,float cy,float angle,float half,float width,const Ink &core,const Ink &glow,int kq,float gleam) {
  if(kq<5 || half<1)return;
  const float ux=cosine(angle),uy=sine(angle);const bool flat=std::fabs(ux)>=std::fabs(uy);
  // Across the cut, step whole pixels on the minor axis.
  const int n=int(half);
  for(int i=-n;i<=n;++i) {
    const float u=float(i)/half,h=width*(1-u*u),x=cx+ux*i,y=cy+uy*i;
    if(x<-3 || x>w_+2 || y<-3 || y>bottom_+3)continue;
    const float g=gleam<0?0:std::max(0.0f,1-std::fabs(u-(gleam*2-1))*5);
    const int k=std::min(256,int(kq*(.55f+.45f*(1-u*u))*(1+.8f*g)));
    const int px=roundi(x),py=roundi(y);
    for(int o=-2;o<=2;++o) {
      const float d=std::fabs(float(o));if(d>h+.6f)continue;
      const int qx=flat?px:px+o,qy=flat?py+o:py;
      if(unsigned(qx)>=unsigned(w_) || unsigned(qy)>unsigned(bottom_) || corsairInBox(qx,qy))continue;
      if(d<=h*.5f+.3f)addw(qx,qy,core,k);else addw(qx,qy,glow,(k*int(200*std::min(1.0f,h+.6f-d)))>>8);
    }
  }
}
// A straight blade `half` px either side of (cx,cy): a bright edge row and
// a steel row beside it, with a gleam (0..1) running along it.
inline void Renderer::corsairBlade(float cx,float cy,float angle,float half,const Ink &edge,const Ink &steel,int kq,float gleam) {
  if(kq<5)return;
  const float ux=cosine(angle),uy=sine(angle),nx=-uy,ny=ux;const int n=int(half);
  for(int i=-n;i<=n;++i) {
    const float u=float(i)/half,g=std::max(0.0f,1-std::fabs(u-(gleam*2-1))*4);
    const int k=std::min(256,int(kq*(.7f+.6f*g)));
    const int x=roundi(cx+ux*i),y=roundi(cy+uy*i);
    if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_))addw(x,y,edge,k);
    const int sx=roundi(cx+ux*i+nx),sy=roundi(cy+uy*i+ny);
    if(unsigned(sx)<unsigned(w_) && unsigned(sy)<=unsigned(bottom_))addw(sx,sy,steel,(k*170)>>8);
  }
}

// Flushes the readout's glyphs in rows y0..y1 toward `target` by k8 (Q8),
// brightening only (sorcHeat: every channel up on a dark background, toward
// black on a light one); the readout is one colour, so the result is
// remembered.
inline void Renderer::corsairFlush(const Ink &target,int k8,int y0,int y1) {
  if(k8<=0)return;
  const Box &b=number_;uint16_t lastIn=bg_,lastOut=bg_;
  for(int y=std::max(b.y0,y0);y<=std::min(b.y1,y1);++y) {
    uint16_t *row=px_+y*w_;
    for(int x=b.x0;x<=b.x1;++x) {
      const uint16_t p=row[x];if(p==bg_)continue;
      if(p!=lastIn){lastIn=p;sorcHeat(x,y,target,k8);lastOut=row[x];--plots_;}
      row[x]=lastOut;++plots_;
    }
  }
}

// A soft wisp of steam rising `height` px from (x0,y0): each row a soft
// span (brightest at its middle) w0 px half-wide at the base widening to w1
// at the top, swaying side to side by up to `sway` px in curls that climb
// with `phase`, fading in at the base and out toward the top.
inline void Renderer::corsairWisp(float x0,float y0,float height,float w0,float w1,float sway,float phase,const Ink &c,int kq) {
  if(kq<5 || height<1)return;
  const int n=int(height);
  for(int j=0;j<=n;++j) {
    const float v=float(j)/n,y=y0-j;if(y<0 || y>bottom_)continue;
    const float cx=x0+sway*v*sine(phase-v*5),hw=w0+(w1-w0)*v;
    const int kr=int(kq*std::min(1.0f,v*5)*(1-v)*(1.15f-.3f*v));if(kr<5)continue;
    const int iy=roundi(y),inv=int(65536/(hw*hw));
    for(int x=std::max(0,int(cx-hw));x<=std::min(w_-1,int(cx+hw)+1);++x) {
      const float dx=x-cx;const int d=int(dx*dx*inv);if(d>=65536)continue;
      addw(x,iy,c,(kr*((65536-d)>>8))>>8);
    }
  }
}

inline void Renderer::corsair(unsigned which,float t) {
  switch(which) {
    case 0:corsairSlingshot(t);break;
    case 1:corsairPresence(t);break;
    case 2:corsairThreefold(t);break;
    case 3:corsairBanner(t);break;
    case 4:corsairVapor(t);break;
    default:break;
  }
}

// ---- Slingshot: a big fist on a rubber arm shoots in along the clear band
// under the readout from the right edge and winds up there: the arm
// twists tighter and tighter while the fist turns and trembles. It yanks
// back off the edge, then snaps across on the stretched, twisting arm and
// lands under the readout's middle: the hit knocks the readout's foot
// sideways with a flash, a comic burst, a ring and a spray of dust.
// Sustain: the arm springs back like rubber, overshoots off the edge and
// rebounds out twice, smaller each time, while focus lines flicker round
// the point of impact and the burst's print fades; then the fist withdraws.
inline void Renderer::corsairSlingshot(float t) {
  constexpr float IN=.22f,WIND=.74f,BACK=.84f,LAUNCH=.88f,HIT=.98f,RETRACT=1.3f,WITHDRAW=3.75f,GONE=4.1f;
  const Box &b=number_;
  const float Y=b.y1+13.0f,sx=w_+12.0f,r=10,wx0=w_-42.0f,tx=b.cx()-4,ty=b.y1+14.0f,rx=w_-36.0f;
  const Rgb edge=rgb8(255,150,92),body=rgb8(230,112,72),core=rgb8(255,232,196),skin=rgb8(255,190,140),rim=rgb8(255,242,226);
  const Ink edgeInk=inkOf(edge),bodyInk=inkOf(body),coreInk=inkOf(core),skinInk=inkOf(skin),rimInk=inkOf(rim);
  const float a=t-HIT;
  // Where the fist is, which way it faces, and how the arm bows and turns.
  float fx=w_+r+2,fy=Y,dx=-1,dy=0,wobble=0,spin=t*9;
  if(t<IN){fx=w_+r+2-(w_+r+2-wx0)*easeOut(t/IN);}
  else if(t<WIND) {
    // The wind-up: the arm twists ever faster, the fist turns to and fro
    // and trembles, drawing back a little as the tension builds.
    const float u=(t-IN)/(WIND-IN),turn=.45f*u*sine(u*u*40);
    fx=wx0+8*u*u+.8f*u*sine(t*95);fy=Y+1.5f*u*u*sine(t*70);dx=-cosine(turn);dy=sine(turn);
    wobble=1.5f*u*sine(t*30);spin=t*(10+120*u*u);
  } else if(t<BACK){const float u=smooth((t-WIND)/(BACK-WIND));fx=wx0+8+(w_+r+4-wx0-8)*u;spin=t*130;}
  else if(t>=LAUNCH && t<HIT){const float u=easeOut((t-LAUNCH)/(HIT-LAUNCH));fx=w_+r+4+(tx-(w_+r+4))*u;fy=Y+(ty-Y)*u;spin=t*130;}
  else if(t>=HIT && t<RETRACT) {
    // Taut after the hit: a short recoil and a wobble along the arm.
    const float e=std::exp(-6*a);fx=tx+3*e*sine(a*28);fy=ty;wobble=4*e*cosine(a*34);spin=HIT*130+a*20;
  } else if(t>=RETRACT && t<GONE) {
    // A rubber band's rebound: the reach swings past the rest point (off
    // the edge) and out again, twice, smaller each time.
    const float u=t-RETRACT,e=std::exp(-.55f*u),ext=e*cosine(2*HALF_TURN*u/1.1f);
    const float out=t<WITHDRAW?ext:ext-(t-WITHDRAW)/(GONE-WITHDRAW)*1.2f;
    fx=rx+(tx-rx)*out;fy=Y+(ty-Y)*std::max(0.0f,std::min(1.0f,out))+(t>2.2f?1.2f*sine((t-2.2f)*3):0);
    wobble=-3.5f*e*sine(2*HALF_TURN*u/1.1f);spin=HIT*130+u*14;
  }
  if(fx<w_+r) {
    const float wx=fx-dx*r*.85f,wy=fy-dy*r*.85f;
    const float len=std::sqrt((wx-sx)*(wx-sx)+(wy-Y)*(wy-Y)),hw=std::max(2.7f,4.6f-len*.02f);
    corsairArm(sx,Y,wx,wy,hw,wobble,spin,edgeInk,bodyInk,coreInk,256);
    // A cuff at the wrist, then the fist over it.
    {const float nx=-dy,ny=dx,c=hw+1.5f;const Ink cuff=inkOf(mixed(edge,accent_,.5f));
     corsairLine(wx+nx*c,wy+ny*c,wx-nx*c,wy-ny*c,cuff,256,256);corsairLine(wx+nx*c+dx,wy+ny*c+dy,wx-nx*c+dx,wy-ny*c+dy,cuff,200,200);}
    corsairFist(fx,fy,dx,dy,r,rimInk,skinInk,256);
    // Twist marks: two arcs turning round the fist while it winds up.
    if(t>=IN && t<BACK) {
      const float u=std::min(1.0f,(t-IN)/(WIND-IN)),k=smooth(u*3)*(t<WIND?1:1-(t-WIND)/(BACK-WIND)),ph=t*(6+30*u);
      for(int arc=0;arc<2;++arc)for(int q=0;q<9;++q) {
        const float an=ph+arc*HALF_TURN+q*.12f;
        addw(roundi(fx+(r+5)*cosine(an)),roundi(fy+(r+4)*.8f*sine(an)),coreInk,q8(k*(.9f-.08f*q)));
      }
    }
  }
  // On the rebounds, short speed lines trail the fist while it moves fast.
  if(t>=RETRACT && t<WITHDRAW) {
    const float u=t-RETRACT,v=std::exp(-.55f*u)*sine(2*HALF_TURN*u/1.1f);
    const float k=smooth((std::fabs(v)-.25f)/.3f)*(fx<w_-6?1:0);
    if(k>.02f)for(int i=0;i<4;++i) {
      const float off=(i<2?-1:1)*(6+4*(i&1)),dir=v>0?-1:1,ln=12+10*rnd(i,534);
      const float hx=fx+dir*(r+2),hy=fy+off;
      corsairLine(hx,hy,hx+dir*ln,hy,inkOf(mixed(WHITE,accent_,.4f)),q8(.8f*k),0);
    }
  }
  // The snap: speed lines streaming behind the flying fist.
  if(t>=LAUNCH && t<HIT+.05f) {
    const float k=t<HIT?1:1-(t-HIT)/.05f;
    for(int i=0;i<6;++i) {
      const float off=(i<3?-1:1)*(8+4*(i%3))+(rnd(i,531)-.5f)*2,ln=26+30*rnd(i,532),lag=4+12*rnd(i,533);
      const float hx=fx+lag,hy=fy+off;
      corsairLine(hx,hy,hx+ln,hy,i&1?coreInk:inkOf(mixed(WHITE,accent_,.4f)),q8(.85f*k),0);
    }
  }
  if(a<0)return;
  // The hit knocks the readout's lower rows sideways, its foot most, in a
  // damped shake that is over by about 1.2 s.
  if(a<.3f) {
    const float shake=-3.8f*std::exp(-10*a)*cosine(a*30);
    const int from=b.y1-int((b.y1-b.y0)*.22f);
    for(int y=from;y<=b.y1;++y)sorcShiftRow(y,roundi(shake*(.35f+.65f*(y-from)/std::max(1.0f,float(b.y1-from)))));
  }
  glint(t,HIT+.5f,.4f);
  const float hx=tx-r,hy=ty-1;
  // A soft flash at the knuckles, a comic burst snapping open round it.
  if(a<.1f)corsairFlash(hx,hy,6+4*easeOut(a/.05f),.8f,WHITE_INK,q8(1-a/.1f));
  if(a>=.03f && a<.33f) {
    const float u=(a-.03f)/.3f,k=1-u*u;
    corsairStar(hx,hy,12+22*easeOut(u/.5f),.5f,9,3,inkOf(mixed(rgb8(255,220,90),WHITE,.35f)),inkOf(rgb8(255,150,40)),q8(k));
  }
  // A ring rolls out from the hit in the screen's perspective.
  if(a>=.06f && a<.61f){const float u=(a-.06f)/.55f,rr=14+80*easeOut(u);const Ink c=inkOf(mixed(WHITE,accent_,.35f+.4f*u));
    corsairRing(hx,hy+2,rr,rr*.42f,c,q8(.95f*(1-u)*(1-u)));}
  // Focus lines flicker round the hit, re-drawn ten times a second.
  const float focus=smooth((a-.6f)/.15f)*(1-smooth((t-2.6f)/.9f));
  if(focus>.02f)corsairFocus(hx,hy,10,uint32_t(floori(t*10)),32,inkOf(mixed(WHITE,accent_,.4f)),q8(focus));
  // The burst's print lingers at the hit, breathing and fading.
  {const float k=smooth((a-.34f)/.15f)*(1-smooth((t-2.4f)/.7f))*(.38f+.14f*sine(a*7));
   if(k>.02f)corsairStar(hx,hy+2,24+2*sine(a*3),.5f,9,3+uint32_t(floori(a*2.5f))%2,inkOf(mixed(rgb8(255,200,90),accent_,.35f)),inkOf(rgb8(150,70,20)),q8(k));}
  // Dust kicked off the hit: out and up, then falling and settling.
  const float endFade=1-smooth((t-4.1f)/.7f);
  for(unsigned i=0;i<18;++i) {
    const float age=a-.08f-.08f*rnd(i,541),life=1.2f+1.8f*rnd(i,542);if(age<0 || age>=life)continue;
    const float an=HALF_TURN*(1.05f+.9f*rnd(i,543)),v=40+90*rnd(i,544),drag=1-std::exp(-3*age);
    const float x=hx+cosine(an)*v*drag/3+(i&1?5:-3)*age,y=hy+sine(an)*v*.6f*drag/3+14*age*age,u=age/life;
    const Rgb c=i%3==0?WHITE:i%3==1?core:mixed(accent_,WHITE,.4f);
    dot(x,y,c,endFade*(1-u)*(.7f+.3f*sine(age*19+i))*1.4f);
  }
}

// ---- Presence: the air goes still: the readout darkens toward a deep
// crimson (never below 3:1) while motes drift in round it and hang frozen. Then the will is released: the readout snaps back
// with a glint and black lightning with red rims tears along the band under
// it and down both sides of the screen; as it dies a wave of pressure rolls
// out over the floor with more of it crackling along its front.
// A second, weaker wave follows. Sustain: smaller aftershocks roll out every
// 0.8 s with a stray crackle, the glow breathes under the readout and gold
// sparks drift up through the pressure. Then it fades.
inline void Renderer::corsairPresence(float t) {
  constexpr float RELEASE=.55f,SETTLE=3.8f,END=4.9f;
  const Box &b=number_;const float cx=b.cx(),cy=b.y1+8.0f;
  const Rgb crimson=rgb8(235,22,44),deep=rgb8(130,0,24),hot=rgb8(255,196,206),violet=mixed(rgb8(120,40,210),accent_,.3f),gold=rgb8(255,206,90);
  const Ink crimsonInk=inkOf(crimson),deepInk=inkOf(deep),hotInk=inkOf(hot),violetInk=inkOf(violet);
  const int frame=floori(t*20+.001f);
  const float a=t-RELEASE,endFade=1-smooth((t-SETTLE)/(END-SETTLE-.1f));
  // The hush: the readout darkens toward a deep crimson, then snaps back.
  if(t<RELEASE)shadeAnchor(inkOf(rgb8(90,0,14)),q8(.8f*smooth(t/(RELEASE-.05f))),false);
  glint(t,RELEASE+.02f,.42f);
  // A dark red glow breathes under the readout once the wave has passed.
  {
    const float k=.32f*smooth((a-.7f)/.5f)*(.75f+.25f*sine(a*5))*(1-smooth((t-3.1f)/.6f));
    if(k>.02f)corsairFlash(cx,cy+1,24,.35f,inkOf(mixed(deep,crimson,.35f)),q8(k));
  }
  // Frozen motes: hung in the air through the hush, blown out by the wave.
  for(unsigned i=0;i<14;++i) {
    const float an=2*HALF_TURN*rnd(i,561),d0=24+44*rnd(i,562),x0=cx+cosine(an)*d0,y0=cy+6+sine(an)*d0*.5f;
    const float born=.05f+.3f*rnd(i,563);if(t<born)continue;
    const float in=1-.25f*smooth((t-born)/.35f);
    float x=cx+(x0-cx)*in,y=cy+6+(y0-cy-6)*in;const float push=a>0?easeOut(a/.6f)*(60+30*rnd(i,564)):0;
    x+=cosine(an)*push;y+=sine(an)*push*.42f;
    const float k=smooth((t-born)/.15f)*(a>0?1-smooth(a/.6f):1);
    if(k>.02f)dot(x,y,mixed(gold,hot,.4f),.9f*k);
  }
  if(a<0)return;
  // The waves: the release, a weaker echo, then aftershocks every 0.75 s.
  for(int i=0;i<5;++i) {
    const float born=i==0?.34f:i==1?.64f:1.45f+.8f*(i-2),life=i<2?.75f:.75f,age=a-born;if(age<0 || age>=life)continue;
    const float u=age/life,reach=i==0?165:i==1?140:80-12*(i-2),rx=12+reach*easeOut(u),k=(i==0?1:i==1?.75f:.62f)*(1-u)*(1-.3f*u)*endFade;
    corsairWave(cx,cy,rx,rx*.42f,inkOf(mixed(hot,crimson,.15f+.75f*u)),i<2?violetInk:deepInk,q8(std::min(1.0f,k*1.2f)),uint32_t(frame/2*7+i));
    // Black lightning crackles along the front, re-struck every frame:
    // short bolts lying along the ring on its near (lower) half.
    const int bolts=i==0?2:i==1?1:(rnd(uint32_t(frame)*5+i,565)<.4f?1:0);
    for(int j=0;j<bolts;++j) {
      const uint32_t seed=uint32_t(frame)*16+uint32_t(i)*4+j;
      const float an=HALF_TURN*(.08f+.84f*rnd(seed,566)),len=.22f+.18f*rnd(seed,567);
      const float x0=cx+rx*cosine(an),y0=cy+rx*.42f*sine(an),x1=cx+rx*cosine(an+len),y1=cy+rx*.42f*sine(an+len);
      sorcRimBolt(x0,y0-1,x1,y1-1,seed,crimsonInk,deepInk,q8(std::min(1.0f,k*1.4f)),2);
    }
  }
  // Black lightning tears along the clear band under the readout, from its
  // middle out to both edges, and down both sides of the screen, re-struck
  // every other frame, while the wave leaves; a stray one now and then after.
  {
    const uint32_t f2=uint32_t(frame/2);
    const float strong=smooth(a/.04f)*(1-smooth((a-.24f)/.16f));
    const bool stray=strong<.05f && a>.9f && t<SETTLE-.3f && rnd(f2,592)<.3f;
    const float k=strong>=.05f?strong:stray?.75f*endFade:0;
    if(k>.02f) {
      const float y=b.y1+7.0f;
      for(int side=0;side<2;++side) {
        if(stray && side!=int(f2&1))continue;
        const float ex=side?w_-3.0f:2.0f,mx=cx+(side?6:-6);
        corsairBlackBolt(mx,y,ex,y+3*rnd(f2*2+side,593),f2*2+side,crimsonInk,deepInk,q8(k),.24f);
        if(!stray && a>.05f)corsairBlackBolt(ex+(side?-5:5),y+3,ex+(side?-6:6)+(rnd(f2*2+side,594)-.5f)*8,y+40+14*rnd(f2*2+side,595),f2*2+side+77,crimsonInk,deepInk,q8(k*.9f),.3f);
      }
    }
  }
  // Gold sparks drift up through the pressure after the release.
  for(unsigned i=0;i<16;++i) {
    const float age=a-.7f-2.4f*rnd(i,581),life=.9f+.7f*rnd(i,582);if(age<0 || age>=life)continue;
    const float x=10+(w_-20)*rnd(i,583)+3*sine(age*4+i),y=b.y1+20+70*rnd(i,584)-14*age,u=age/life;
    twinkle(x,y,mixed(gold,WHITE,.3f*(1-u)),endFade*sine(HALF_TURN*u)*(.8f+.2f*sine(age*23+i)),i%3==0?2:1);
  }
}

// ---- Threefold: three blades line up at the left of the band under the
// readout, two crossed and one level across them, a gleam running along
// each in turn while a green aura smoulders off them. They dash across the
// screen in a white streak and are gone; a beat of stillness; then three
// cuts open at once where they passed, two crossed and one level, crossing
// under the readout a little left of its middle (clear of the replay's
// centre line), white-hot and spraying sparks. Sustain: the
// cuts cool to green wakes that slowly close, a gleam runs along each in
// turn and green smoke curls up off them. Then they fade.
inline void Renderer::corsairThreefold(float t) {
  constexpr float DASH=.52f,PASSED=.62f,CUT=.95f,SETTLE=3.7f,END=4.8f;
  const Box &b=number_;const float Y=b.y1+14.0f,sx=38,cx=b.cx()-14,cy=b.y1+24.0f;
  const Rgb steel=rgb8(170,196,220),green=rgb8(70,255,140),pale=rgb8(225,255,232),cyan=rgb8(70,225,215),lime=rgb8(220,255,110);
  const Ink whiteInk=WHITE_INK,steelInk=inkOf(steel),greenInk=inkOf(mixed(green,accent_,.15f)),paleInk=inkOf(pale);
  const float endFade=1-smooth((t-SETTLE)/(END-SETTLE-.1f));
  static constexpr float ANG[3]={-.6f,.6f,0},HALF[3]={20,20,24};
  // The stance: three blades, two crossed and one level across them.
  if(t<PASSED) {
    const float in=smooth(t/.18f),x=t<DASH?sx:sx+(w_+40-sx)*smooth((t-DASH)/(PASSED-DASH));
    if(t<DASH)for(int k=0;k<3;++k) {
      const float g=(t-.12f-.11f*k)/.14f;
      corsairBlade(x,Y,ANG[k],HALF[k]*in,whiteInk,steelInk,q8(.9f*in),g>0 && g<1?g:-1);
      if(g>0 && g<1)twinkle(x+cosine(ANG[k])*HALF[k]*(g*2-1),Y+sine(ANG[k])*HALF[k]*(g*2-1),WHITE,sine(HALF_TURN*g)*1.2f,3);
    }
    // The dash: a white streak with green edges from the stance to the head.
    if(t>=DASH) {
      const float u=(t-DASH)/(PASSED-DASH),tail=std::max(0.0f,sx-20+(w_+20-sx)*smooth((u-.3f)/.7f));
      for(int px=std::max(0,int(tail));px<=std::min(w_-1,int(x));++px) {
        const float f=std::min(1.0f,(px-tail)/std::max(1.0f,x-tail));
        const int k=q8(.95f*f);
        addw(px,roundi(Y),whiteInk,k);addw(px,roundi(Y)-1,greenInk,(k*150)>>8);addw(px,roundi(Y)+1,greenInk,(k*150)>>8);
        if(f>.6f){addw(px,roundi(Y)-2,greenInk,(k*60)>>8);addw(px,roundi(Y)+2,greenInk,(k*60)>>8);}
      }
    }
  }
  // The dash's afterglow lingers along the band through the beat.
  if(t>=PASSED && t<CUT+.1f) {
    const float k=.35f*(1-smooth((t-PASSED)/(CUT+.1f-PASSED)));
    for(int px=2;px<w_-2;px+=1)addw(px,roundi(Y),greenInk,q8(k*(.6f+.4f*sine(px*.21f+t*3))));
  }
  // The aura: soft green smoke curling up off the blades, then the cuts.
  for(unsigned i=0;i<16;++i) {
    const float birth=.05f+3.4f*rnd(i,601),life=.9f+.5f*rnd(i,602),age=t-birth;if(age<0 || age>=life)continue;
    const bool early=birth<DASH-.2f;if(!early && birth<CUT+.25f)continue;
    float x0,y0;
    if(early){x0=sx+(rnd(i,603)-.5f)*36;y0=Y+(rnd(i,604)-.5f)*16;}
    else{const int k=int(i%3);const float s=(rnd(i,603)-.5f)*1.6f*HALF[k]*2.6f;x0=cx+cosine(ANG[k])*s;y0=cy+sine(ANG[k])*s;}
    const float u=age/life,x=x0+3*sine(age*3+i),y=y0-14*age-4*u*u;
    if(y<b.y1+4)continue;
    const Rgb c=mixed(mixed(cyan,green,smooth(u*2)),lime,smooth((u-.5f)*2));
    corsairFlash(x,y,4+4*u,.85f,inkOf(c),q8((early?.5f:.42f)*sine(HALF_TURN*u)*endFade));
  }
  if(t<CUT)return;
  const float a=t-CUT;
  glint(t,CUT+.02f,.42f);
  // The cuts: open at once, white-hot, then cooling to green wakes that
  // slowly close.
  for(int k=0;k<3;++k) {
    const float v=a-.03f*k;if(v<0)continue;
    const float open=easeOut(v/.06f),half=(k==2?66:56)*open;
    const float width=2.2f-.8f*smooth((v-.02f)/.08f)-.5f*smooth((v-.4f)/.9f)-.4f*smooth((v-1.6f)/1.4f);
    const float heat=smooth((v-.1f)/.6f),fade=endFade*(1-.25f*smooth((v-1.5f)/1.5f));
    const Ink core=inkOf(mixed(WHITE,pale,heat)),glow=inkOf(mixed(pale,green,.3f+.7f*heat));
    // A gleam runs along each cut in turn through the sustain.
    const float gt=(a-1.1f-.55f*k)/.5f,gp=gt-floori(gt*(1/2.0f))*2.0f;
    corsairCut(cx,cy,ANG[k],half,std::max(.5f,width),core,glow,q8(fade),a>1 && gp<1?gp:-1);
  }
  // Sparks spray off the cuts as they open.
  for(unsigned i=0;i<24;++i) {
    const int k=int(i%3);const float age=a-.03f*k-.02f*rnd(i,611),life=.4f+.4f*rnd(i,612);if(age<0 || age>=life)continue;
    const float s=(rnd(i,613)-.5f)*2*55,x0=cx+cosine(ANG[k])*s,y0=cy+sine(ANG[k])*s;
    const float side=i&4?1:-1,v=40+60*rnd(i,614),nx=-sine(ANG[k])*side,ny=cosine(ANG[k])*side;
    const float x=x0+nx*v*age,y=y0+ny*v*age*.8f+60*age*age,u=age/life;
    dot(x,y,i%3==0?WHITE:mixed(pale,i&1?green:accent_,.5f),(1-u)*1.2f);
  }
}

// ---- Banner: a mast stands up at the left of the band under the readout
// with a glint running up it, and a furled black flag is hoisted to its
// top; then it snaps open in the wind with a whip crack, throwing spray off
// its fly end. The flag is the dark of the background inside lit edges,
// with a plain crossed-bones mark and the light catching its folds.
// Sustain: it billows, gusting, its folds rolling out to the fly end, while
// sea spray and streaks of wind blow past. Then the wind drops, the flag
// sags and fades.
inline void Renderer::corsairBanner(float t) {
  constexpr float RAISE=.22f,HOISTED=.82f,SNAP=.86f,SETTLE=3.75f,END=4.9f;
  const Box &b=number_;
  const float mx=b.x0+(b.x1-b.x0)*.17f,top=b.y1+7.0f,H=34,L=std::min(84.0f,w_-mx-14),foot=b.y1+72.0f;
  const Rgb edgeC=mixed(rgb8(236,232,255),accent_,.18f),glowC=mixed(rgb8(120,130,190),accent_,.3f),wood=rgb8(214,170,112),bone=rgb8(246,238,214),spray=rgb8(190,240,255);
  const Ink edgeInk=inkOf(edgeC),glowInk=inkOf(glowC),woodInk=inkOf(wood),boneInk=inkOf(bone);
  const float endFade=1-smooth((t-SETTLE-.5f)/(END-SETTLE-.6f));
  // The mast: fades in from the foot up, a glint running up it.
  {
    const float k=smooth(t/RAISE)*endFade,reach=foot-(foot-top+3)*easeOut(t/RAISE);
    if(k>.02f) {
      for(int y=std::max(0,int(reach));y<=std::min(bottom_,int(foot));++y) {
        const float g=t<RAISE+.4f?std::max(0.0f,1-std::fabs(y-(foot-(foot-top)*smooth((t-.1f)/.45f)))*.15f):0;
        addw(roundi(mx),y,woodInk,q8(k*(.9f+.6f*g)));addw(roundi(mx)-1,y,woodInk,q8(k*(.55f+.4f*g)));addw(roundi(mx)+1,y,woodInk,q8(k*.3f));
      }
      corsairFlash(mx-.5f,top-3,2.6f,1,woodInk,q8(k));
    }
  }
  // Wind: 0 before the snap, a hard flap at it, then a steady gusting billow,
  // dropping in the settle.
  const float a=t-SNAP,wind=a<0?0:(1-smooth((t-SETTLE)/.7f))*(.75f+.25f*sine(a*1.3f));
  // The header glints as the flag nears the top.
  glint(t,.4f,.42f);
  // The furled flag: a narrow roll beside the mast, hoisted up it.
  if(t>=RAISE && t<SNAP+.05f) {
    const float u=smooth((t-RAISE)/(HOISTED-RAISE)),y0=foot-8-(foot-8-top)*u,k=smooth((t-RAISE)/.1f)*(t<SNAP?1:1-(t-SNAP)/.05f);
    for(int r=0;r<3;++r)corsairLine(mx+2+r*1.5f,y0+(r&1)*2,mx+2+r*1.5f+.6f*sine(t*9+r),y0+H*.85f,r==1?edgeInk:glowInk,q8(k),q8(k*.6f));
  }
  if(a<0)return;
  // The cloth: u along the flag from the mast (0) to the fly end (1), v down
  // it. It opens with an overshoot, a wave travelling out along it; the
  // flap is hard at the snap and settles to a billow; it sags as the wind
  // drops.
  const float open=a<.25f?easeOut(a/.25f)*(1+.08f*sine(HALF_TURN*a/.25f)):1;
  const float amp=(2.2f+5.5f*std::exp(-5*a))*(.35f+.65f*wind),sag=(1-wind)*10;
  const float phase=a*2*HALF_TURN*1.4f;
  auto at=[&](float u,float v,float &x,float &y){
    const float w=2*HALF_TURN*1.5f*u-phase+.55f*v;
    x=mx+1+L*open*u*(1-.04f*(1-wind))+1.2f*u*cosine(w);y=top+H*v*(1-.12f*u*(1-wind))+amp*u*sine(w)+sag*u*u;
  };
  const int kq=q8(endFade);
  // Lit edges: the top and bottom hems, each a smooth line split over two
  // rows column by column, with a glow row inside the cloth; and the fly
  // end, split over two columns, with a glow column inside.
  // The samples step along the cloth with the wave's (cos, sin) advanced by
  // rotation, so no trigonometry or division runs per sample.
  const float Lx=L*open*(1-.04f*(1-wind)),shrink=.12f*(1-wind);
  {
    const Ink edgeHot=inkOf(mixed(edgeC,WHITE,.3f));
    const int n=3*int(L);const float du=1.0f/n,dw=2*HALF_TURN*1.5f*du,cd=cosine(dw),sd=sine(dw);
    for(int e=0;e<2;++e) {
      int last=-1;float c=cosine(-phase+.55f*e),sn=sine(-phase+.55f*e);
      for(int i=0;i<=n;++i) {
        const float u=i*du,x=mx+1+Lx*u+1.2f*u*c,y=top+H*e*(1-shrink*u)+amp*u*sn+sag*u*u;
        {const float t=c*cd-sn*sd;sn=sn*cd+c*sd;c=t;}
        const int ix=roundi(x);
        if(ix==last || unsigned(ix)>=unsigned(w_))continue;last=ix;
        vdot(ix,y,edgeHot,kq);addw(ix,roundi(y)+(e?-2:2),glowInk,(kq*90)>>8);
      }
    }
    int last=-1;const int m=3*int(H);const float dv=1.0f/m,dw2=.55f*dv,cd2=cosine(dw2),sd2=sine(dw2);
    float c=cosine(3*HALF_TURN-phase),sn=sine(3*HALF_TURN-phase);
    for(int i=0;i<=m;++i) {
      const float v=i*dv,x=mx+1+Lx+1.2f*c,y=top+H*v*(1-shrink)+amp*sn+sag;
      {const float t=c*cd2-sn*sd2;sn=sn*cd2+c*sd2;c=t;}
      const int iy=roundi(y);
      if(iy==last || unsigned(iy)>unsigned(bottom_))continue;last=iy;
      const int ix=floori(x),ax=q8(x-ix);
      addw(ix,iy,edgeHot,(kq*(256-ax))>>8);addw(ix+1,iy,edgeHot,(kq*ax)>>8);addw(ix-2,iy,glowInk,(kq*90)>>8);
    }
  }
  // Folds: the light catches each crest of the wave as a soft band down the
  // cloth, stronger toward the fly end where the flag moves most.
  // On a crest the wave's sine is 1 and its cosine 0, so the cloth's
  // position there needs no trigonometry.
  constexpr float INV_TURN=1/(2*HALF_TURN),INV_H=1/34.0f;
  for(int n=0;n<3;++n) {
    for(int i=1;i<int(H);++i) {
      const float v=i*INV_H,u=(n+.25f+(phase-.55f*v)*INV_TURN)*(1/1.5f);
      if(u<.06f || u>.95f)continue;
      const float x=mx+1+Lx*u,y=top+H*v*(1-shrink*u)+amp*u+sag*u*u;const int ix=roundi(x),iy=roundi(y);
      const int k=q8(endFade*(.15f+.4f*u)*(.45f+.55f*amp/7.7f)*smooth(std::min(v,1-v)*8));
      addw(ix,iy,glowInk,k);addw(ix-1,iy,glowInk,(k*120)>>8);addw(ix+1,iy,glowInk,(k*120)>>8);
    }
  }
  // The mark: two crossed bones, knobbed at both ends, riding the cloth.
  {
    const float k=endFade*smooth((a-.1f)/.15f);
    if(k>.02f)for(int s=-1;s<=1;s+=2) {
      float x0,y0,x1,y1;at(.6f-.19f,.5f-.34f*s,x0,y0);at(.6f+.19f,.5f+.34f*s,x1,y1);
      const float dx=x1-x0,dy=y1-y0,l=std::sqrt(dx*dx+dy*dy)+.01f,nx=-dy/l,ny=dx/l;
      corsairLine(x0,y0,x1,y1,boneInk,q8(k),q8(k));corsairLine(x0+nx,y0+ny,x1+nx,y1+ny,boneInk,q8(k*.75f),q8(k*.75f));
      for(int e=0;e<2;++e){const float ex=e?x1:x0,ey=e?y1:y0,ox=(e?1:-1)*dx/l;
        dot(ex+nx*1.6f+ox*.6f,ey+ny*1.6f,bone,k);dot(ex-nx*1.2f+ox*.6f,ey-ny*1.2f,bone,k);}
    }
  }
  // The snap: spray thrown off the fly end, and a crack of light along it.
  if(a<.16f){float x,y;at(1,.5f,x,y);corsairFlash(x+2,y,4+7*a/.16f,1.3f,inkOf(mixed(spray,WHITE,.5f)),q8((1-a/.16f)*.85f));}
  for(unsigned i=0;i<12;++i) {
    const float age=a-.02f*rnd(i,621),life=.5f+.5f*rnd(i,622);if(age<0 || age>=life)continue;
    float x0,y0;at(1,rnd(i,623),x0,y0);
    const float x=x0+(50+70*rnd(i,624))*age,y=y0+(rnd(i,625)-.6f)*40*age+60*age*age,u=age/life;
    dot(x,y,mixed(WHITE,spray,u),(1-u)*1.2f*endFade);
  }
  // Sea spray and streaks of wind blow past through the sustain.
  for(unsigned i=0;i<14;++i) {
    const float life=.7f+.5f*rnd(i,631),clock=a-.3f+rnd(i,632)*3;if(clock<0)continue;
    const unsigned cycle=unsigned(clock/life);const float age=clock-cycle*life,u=age/life;
    const uint32_t seed=i*31+cycle;
    const float y=b.y1+6+56*rnd(seed,633)+6*sine(u*3+i),x=-6+(w_+12)*u+8*rnd(seed,634);
    const float k=wind*endFade*sine(HALF_TURN*u)*(rnd(seed,635)<.75f?1:0);if(k<.03f)continue;
    if(i%4==0)corsairLine(x-10,y,x,y,inkOf(mixed(spray,accent_,.3f)),0,q8(.6f*k));
    else dot(x,y,mixed(spray,WHITE,.4f),.9f*k);
  }
}

// ---- Vapor: the body pumps up: five beats, faster and faster, each
// sending a warm pulse up through the readout and pushing a puff of pink
// steam up from under it. On the last the steam surges all along the
// readout's foot, a hot pulse runs up the readout, and the speed comes:
// bursts of afterimage streaks blur sideways through the band under the
// readout, each leaving a puff where it stops. Sustain: steam keeps rising off the body
// in slower breaths, under the readout and up both sides of the screen,
// and a burst of speed blurs past now and then. Then the steam thins away.
inline void Renderer::corsairVapor(float t) {
  constexpr float GEAR=.98f,SETTLE=3.8f,END=4.95f;
  static constexpr float BEATS[]={0,.3f,.53f,.71f,.84f,GEAR};
  static constexpr float BURSTS[]={1.12f,1.42f,1.72f,2.3f,2.9f,3.45f};
  const Box &b=number_;const float cx=b.cx(),foot=b.y1+5.0f;
  const Rgb pink=rgb8(255,150,190),pale=rgb8(255,214,228),hot=rgb8(255,236,242);
  const float endFade=1-smooth((t-SETTLE)/(END-SETTLE-.15f));
  // The flush: each beat sends a warm band up through the readout's rows
  // like a pulse, the last one taller and hotter.
  for(float at:BEATS) {
    const float u=(t-at)/.13f;if(u<0 || u>=1)continue;
    const bool gear=at==GEAR;const float half=gear?5:4,yc=b.y1+half-(b.y1-b.y0+2*half)*u;
    corsairFlush(inkOf(hot),q8((gear?.9f:.35f+.35f*at)*sine(HALF_TURN*u)),int(yc-half),int(yc+half));
  }
  // Steam: soft wisps rising off the body from vents under the readout and
  // in both side strips, curling as they climb. Before the gear-up they puff
  // up on the beats; after it they breathe on a slower pulse.
  {
    float puff=0;
    if(t<GEAR+.1f)for(int n=1;n<6;++n){const float u=t-BEATS[n];if(u>=0)puff=std::max(puff,(n==5?.95f:.45f+.1f*n)*std::exp(-4*u));}
    else puff=(.55f+.25f*sine((t-GEAR)*(2*HALF_TURN/.7f)))*smooth((t-GEAR-.1f)/.3f);
    // A burst of speed blows the steam thin for a moment.
    for(float at:BURSTS){const float u=t-at;if(u>=0 && u<.4f)puff*=.45f+.55f*smooth(u/.4f);}
    puff*=endFade;
    static constexpr float VENT[4]={.12f,.38f,.62f,.88f};
    for(int i=0;i<6;++i) {
      const bool sideStrip=i>=4;if(sideStrip && t<GEAR)continue;
      const float x=sideStrip?(i&1?w_-10.0f:10.0f):b.x0+(b.x1-b.x0)*VENT[i];
      const float y=sideStrip?foot+70:foot+24+3*rnd(i,652),h=sideStrip?28:18+6*puff+3*sine(t*1.7f+i*2.1f);
      const float k=1.5f*puff*(.75f+.25f*sine(t*3.1f+i*1.7f))*(sideStrip?.85f:1);
      corsairWisp(x,y,h,1.2f+.6f*puff,2.6f+1.8f*puff,3,t*5+i*1.3f,inkOf(mixed(pale,pink,.35f+.2f*sine(t+i))),q8(k));
    }
  }
  // Bursts of speed: a bundle of afterimage streaks blurs sideways through
  // the band under the readout, alternating direction, fading behind.
  for(int n=0;n<6;++n) {
    const float age=t-BURSTS[n],life=n<3?.42f:.36f;if(age<0 || age>=life)continue;
    const float dir=n&1?-1:1,u=age/life,head=easeOut(std::min(1.0f,age/.12f)),fade=(1-u)*(n<3?1:.75f)*endFade;
    for(int s=0;s<4;++s) {
      const uint32_t seed=uint32_t(n)*8+s;
      const float y=foot+5+5.5f*s+(rnd(seed,661)-.5f)*1.5f,len=.35f+.35f*rnd(seed,662);
      const float x0=dir>0?-8+(w_+16)*(head-len)*.9f:w_+8-(w_+16)*(head-len)*.9f,x1=dir>0?-8+(w_+16)*head:w_+8-(w_+16)*head;
      const Ink core=inkOf(mixed(WHITE,pink,.15f+.45f*u)),glow=inkOf(mixed(pink,accent_,.25f));
      corsairLine(x0,y,x1,y,core,0,q8(fade));corsairLine(x0,y+1,x1,y+1,glow,0,q8(fade*.7f));
    }
    // A puff where the burst stops.
    if(age>.1f){const float v=(age-.1f)/(life-.1f);corsairFlash(dir>0?w_-12.0f:12.0f,foot+12-6*v,4+5*v,.8f,inkOf(pale),q8(.6f*(1-v)*endFade));}
  }
}
}
