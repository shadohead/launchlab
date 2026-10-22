#pragma once
// The alchemy set of 0.18 (Transmute..Fracture): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Same rules as
// the other sets: additive light on the background outside the UI halo, a
// pure function of elapsed time with fixed seeds, the readout exactly as
// drawn from three quarters of the duration, every frame within the cost
// model's 8 ms.
namespace BestEffectUI {

// A one-pixel line in Q8 steps, its weight ramping k0..k1 (Q8); points off
// the screen are skipped before the light probe. The steps divide integers
// (a hardware instruction on the device), never floats (a library call).
inline void Renderer::alchemyLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1) {
  int x8=q8(x0)+128,y8=q8(y0)+128,wq=k0<<8;const int ex=q8(x1)+128-x8,ey=q8(y1)+128-y8;
  const int n=std::max(1,std::max(std::abs(ex),std::abs(ey))>>8),sx=ex/n,sy=ey/n,dw=(k1-k0)*256/n;
  for(int i=0;i<=n;++i,x8+=sx,y8+=sy,wq+=dw) {
    const int x=x8>>8,y=y8>>8;
    if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_))addw(x,y,c,wq>>8);
  }
}
// An arc of the circle of radius r round (cx,cy), from angle a0 through
// `sweep` radians (positive turns clockwise on the screen), one pixel per
// step by incremental rotation, a pixel never lit twice in a row; the end
// point is left out, so a full turn lights its start once.
inline void Renderer::alchemyArc(float cx,float cy,float r,float a0,float sweep,const Ink &c,int kq) {
  if(kq<5 || r<.5f || sweep==0)return;
  const int n=std::max(2,int(std::fabs(sweep)*r*1.15f)+1);
  const float da=sweep/n,cd=cosine(da),sd=sine(da);
  float co=cosine(a0),si=sine(a0);int px=-9999,py=-9999;
  for(int i=0;i<n;++i) {
    const int x=roundi(cx+r*co),y=roundi(cy+r*si);
    if(x!=px || y!=py){if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_))addw(x,y,c,kq);px=x;py=y;}
    const float nc=co*cd-si*sd;si=si*cd+co*sd;co=nc;
  }
}
// The polyline xs/ys (n points) drawn up to fraction p of its n-1 equal
// edges; (hx,hy) is where the pen is. Returns how far into its edge the pen is.
inline float Renderer::alchemyPath(const float *xs,const float *ys,int n,float p,const Ink &c,int kq,float &hx,float &hy) {
  const float at=clamp01(p)*(n-1);const int full=std::min(n-1,int(at));const float f=at-full;
  for(int i=0;i<full;++i)alchemyLine(xs[i],ys[i],xs[i+1],ys[i+1],c,kq,kq);
  hx=xs[full];hy=ys[full];
  if(full<n-1 && f>0){hx=xs[full]+(xs[full+1]-xs[full])*f;hy=ys[full]+(ys[full+1]-ys[full])*f;alchemyLine(xs[full],ys[full],hx,hy,c,kq,kq);}
  return f;
}
// Reaction crackle: a zigzag that crawls along the circle of radius r from
// angle a0 through `span`, stepping `out` px further from the centre by its
// end (a leap), with a white-blue core and a dimmer echo one pixel outside it.
inline void Renderer::alchemyCrackle(float cx,float cy,float r,float a0,float span,float out,uint32_t seed,const Ink &core,const Ink &glow,int kq) {
  if(kq<5)return;
  constexpr int N=6;float xs[N],ys[N],gx[N],gy[N];
  for(int i=0;i<N;++i) {
    const float a=a0+span*i/(N-1),j=i==0?0:(i&1?1.0f:-1.0f)*(1.2f+2.6f*rnd(seed*8+i,481)),rr=r+j+out*i/(N-1);
    const float ca=cosine(a),sa=sine(a);
    xs[i]=cx+rr*ca;ys[i]=cy+rr*sa;gx[i]=cx+(rr+1.3f)*ca;gy[i]=cy+(rr+1.3f)*sa;
  }
  for(int i=0;i+1<N;++i) {
    const int k0=kq*(N-i)/N,k1=kq*(N-1-i)/N+kq/6;
    alchemyLine(xs[i],ys[i],xs[i+1],ys[i+1],core,k0,k1);
    if(i&1)alchemyLine(gx[i],gy[i],gx[i+1],gy[i+1],glow,k0>>1,k1>>1);
  }
}

inline void Renderer::alchemy(unsigned which,float t) {
  switch(which) {
    case 0:alchemyTransmute(t);break;
    case 1:alchemyIgnition(t);break;
    case 2:alchemySpires(t);break;
    case 3:alchemyGate(t);break;
    case 4:alchemyFracture(t);break;
    default:break;
  }
}

// ---- Transmute: a circle is inscribed under the readout stroke by stroke,
// as if by a pen of light: the outer ring clockwise, the inner ring back the
// other way, a triangle inside it, then twelve small runes between the
// rings. It activates: the lines flash white, a glint crosses the header and
// blue-white reaction sparks crackle along the rings and leap off them,
// re-struck every frame. Sustain: the circle glows and breathes, a pulse of
// light chases round each ring the opposite way and wakes the runes it
// passes, a spark crawls along a ring now and then and motes rise off it.
// Settle: the runes wink out one by one and the lines fade.
inline void Renderer::alchemyTransmute(float t) {
  constexpr float ACT=1.1f,FADE=3.8f,END=4.95f,R=35,R2=28,RUNE=31.5f;
  const Box &b=number_;const float cx=b.cx(),cy=b.y1+68.0f;
  const Rgb chalk=mixed(rgb8(175,215,255),accent_,.12f),blue=rgb8(80,165,255),spark=rgb8(215,240,255);
  const int frame=floori(t*20+.001f);
  const float u=t-ACT,flash=u<0?0:1-smooth(u/.45f),fade=1-smooth((t-FADE)/.9f);
  if(t>=END)return;
  const float breathe=u<.4f?1:.88f+.12f*sine(t*3.1f);
  const Ink line=inkOf(mixed(chalk,WHITE,flash*.8f)),pen=inkOf(spark);
  const int kq=q8(fade*(.75f*breathe+.4f*flash));
  auto progress=[&](float at,float len){return clamp01((t-at)/len);};
  // The pen's trail: the last stretch of a stroke burns brighter.
  auto trail=[&](float x,float y){twinkle(x,y,WHITE,1.1f,1);};
  // Rings.
  const float p1=progress(.08f,.42f),p2=progress(.3f,.35f);
  // The outer ring is a bold three-pixel band (a glow row each side of its
  // core), the inner one two pixels, so they read through the replay's halo.
  const Ink glowRow=inkOf(mixed(mixed(blue,chalk,.4f),WHITE,flash*.6f));const int kg=(kq*120)>>8;
  alchemyArc(cx,cy,R,-HALF_TURN/2,2*HALF_TURN*p1,line,kq);
  alchemyArc(cx,cy,R-1,-HALF_TURN/2,2*HALF_TURN*p1,glowRow,kg);alchemyArc(cx,cy,R+1,-HALF_TURN/2,2*HALF_TURN*p1,glowRow,kg);
  alchemyArc(cx,cy,R2,HALF_TURN/2,-2*HALF_TURN*p2,line,kq);
  alchemyArc(cx,cy,R2-1,HALF_TURN/2,-2*HALF_TURN*p2,glowRow,kg);
  if(p1>0 && p1<1){const float a=-HALF_TURN/2+2*HALF_TURN*p1;alchemyArc(cx,cy,R,a-.4f,.4f,pen,q8(.5f));trail(cx+R*cosine(a),cy+R*sine(a));}
  if(p2>0 && p2<1){const float a=HALF_TURN/2-2*HALF_TURN*p2;alchemyArc(cx,cy,R2,a+.4f,-.4f,pen,q8(.5f));trail(cx+R2*cosine(a),cy+R2*sine(a));}
  // The triangle, inscribed in the inner ring from its top.
  {
    float xs[4],ys[4],hx,hy;
    static constexpr float TRI[]={-.5f,1.0f/6,5.0f/6,-.5f};
    const float p3=progress(.55f,.3f);
    for(int i=0;i<4;++i){xs[i]=cx+R2*cosine(TRI[i]*HALF_TURN);ys[i]=cy+R2*sine(TRI[i]*HALF_TURN);}
    if(p3>0){alchemyPath(xs,ys,4,p3,line,kq,hx,hy);if(p3<1)trail(hx,hy);}
  }
  // The runes: small upright marks between the rings, written in turn, woken
  // by the pulse in the sustain, winking out in turn at the end.
  static constexpr uint16_t SHAPES[]={0x0ba,0x1d2,0x155,0x0aa,0x1e7,0x133};
  const float chase=u-.3f,head=-HALF_TURN/2+chase*2.2f,live=smooth(chase/.4f);
  for(int j=0;j<12;++j) {
    const float at=.72f+.026f*j;if(t<at)continue;
    const float out=FADE+.05f*j,a=-HALF_TURN/2+(j+.5f)*(HALF_TURN/6);
    float k=.7f+.9f*(1-smooth((t-at)/.12f))+.6f*flash;
    if(chase>0){float d=head-a;d-=2*HALF_TURN*floori(d/(2*HALF_TURN));if(d<.5f)k+=.7f*live*(1-d/.5f);}
    if(t>out)k=t<out+.12f?k+.8f*sine(HALF_TURN*(t-out)/.12f):0;
    if(k<=0)continue;
    const int rx=roundi(cx+RUNE*cosine(a)),ry=roundi(cy+RUNE*sine(a)),kr=q8(std::min(1.0f,k*.75f));
    const Ink c=inkOf(mixed(mixed(chalk,accent_,.45f),WHITE,std::min(1.0f,flash+(k>1?k-1:0))));
    const uint16_t bits=SHAPES[(j*5)%6];
    for(int q=0;q<9;++q)if((bits>>q)&1)addw(rx+q%3-1,ry+q/3-1,c,kr);
  }
  glint(t,ACT+.02f,.45f);
  // Charging: as the circle closes, stray sparks run along it.
  const Ink core=inkOf(mixed(spark,WHITE,.4f)),glow=inkOf(mixed(blue,accent_,.25f));
  if(t>.85f && t<ACT)for(int j=0;j<2;++j) {
    const uint32_t seed=uint32_t(frame)*16+j;if(rnd(seed,482)>.25f+2*(t-.85f))continue;
    alchemyCrackle(cx,cy,j?R2:R,2*HALF_TURN*rnd(seed,483),.35f+.3f*rnd(seed,484),0,seed,core,glow,q8(.6f));
  }
  if(u<0)return;
  // Reaction sparks: many at the activation, crawling and leaping, re-struck
  // every frame, then a stray one now and then through the sustain.
  if(u<.75f) {
    const float v=u/.75f;const int n=4-int(2*v);
    for(int j=0;j<n;++j) {
      const uint32_t seed=uint32_t(frame)*16+j;const bool leap=j%3==0;
      const float a=2*HALF_TURN*rnd(seed,485);
      alchemyCrackle(cx,cy,leap?R+1:j%3==1?R:R2,a,leap?.12f+.1f*rnd(seed,486):.4f+.5f*rnd(seed,486),leap?8+8*rnd(seed,487):0,seed,core,glow,q8(1-.45f*v));
    }
  } else if(t<FADE+.4f) {
    for(int j=0;j<2;++j) {
      const uint32_t seed=uint32_t(frame)*16+j;if(rnd(seed,488)>.4f)continue;
      alchemyCrackle(cx,cy,j?R2:R,2*HALF_TURN*rnd(seed,489),.3f+.35f*rnd(seed,490),j?0:4*rnd(seed,491),seed,core,glow,q8(.75f*fade));
    }
  }
  // The pulse: a bright head chases round the outer ring, another the
  // other way round the inner ring.
  if(chase>0 && fade>0) {
    const float k=live*fade;const Ink c=inkOf(mixed(spark,accent_,.2f));
    for(int s=0;s<4;++s)alchemyArc(cx,cy,R,head-(s+1)*.14f,.14f,c,q8(k*.85f*(1-s*.22f)));
    const float back=HALF_TURN/2-chase*1.6f;
    for(int s=0;s<3;++s)alchemyArc(cx,cy,R2,back+s*.14f,.14f,c,q8(k*.6f*(1-s*.28f)));
  }
  // Motes rise off the rings.
  for(unsigned i=0;i<18;++i) {
    const float age=t-ACT-.2f-2.7f*rnd(i,492),life=1.1f+.6f*rnd(i,493);if(age<0 || age>=life)continue;
    const float a=2*HALF_TURN*rnd(i,494),r=i%3?R:R2,v=age/life;
    const float x=cx+r*cosine(a)+2.5f*sine(age*4+i),y=cy+r*sine(a)-(12+10*rnd(i,495))*age;
    dot(x,y,i%2?spark:mixed(blue,accent_,.4f),1.1f*sine(HALF_TURN*v)*(1-smooth((t-END+.4f)/.4f)));
  }
}

// A ball of fire of radius r round (fx,fy), in 2x2 cells like Flames: each
// cell's heat falls off with the square of its distance and is broken up by
// value noise scrolling upward (`scroll`, Q8 noise units), times `cool`
// (Q8) plus `boost`; every cell is filled solid except the coolest, which
// light two pixels on a diagonal, so the ball ends in a ragged rim.
inline void Renderer::alchemyFireball(float fx,float fy,float r,int cool,int boost,int scroll) {
  if(r<1.5f)return;
  const int y0=std::max(0,int(fy-r)),y1=std::min(bottom_-1,int(fy+r)),inv=int(65536/(r*r));
  for(int y=y0&~1;y<=y1;y+=2) {
    const float dy=y+1-fy;if(dy*dy>=r*r)continue;
    // One root per row pair; the cell loop is integer only.
    const float span=std::sqrt(r*r-dy*dy);const int dy2=int(dy*dy),xc=roundi(fx)-1;
    const int xa=std::max(0,int(fx-span))&~1,xb=std::min(w_-2,int(fx+span));
    for(int x=xa;x<=xb;x+=2) {
      if(blocked(x,y) && blocked(x+1,y+1) && blocked(x+1,y))continue;
      const int dx=x-xc,q=std::min(256,((dx*dx+dy2)*inv)>>8);
      const int n=(170*noise(x*40,y*40+scroll,511)+86*noise(x*90,y*90+scroll*2,512))>>8;
      int heat=((((256-q)*(150+((260*n)>>8)))>>8)*cool>>8)+boost-((q*q)>>9);
      if(heat<=36)continue;
      heat=std::min(255,heat);
      const Ink c=fire(heat);const int k=std::min(256,(heat*700)>>8);
      addw(x,y,c,k);addw(x+1,y+1,c,k);
      if(heat>70){addw(x+1,y,c,k);addw(x,y+1,c,k);}
    }
  }
}
// ---- Ignition: a finger snaps at the lower left with a flash of rays, and a
// spark races off it along a zigzag through the air, leaving a fuse of tiny
// flickering sparks, to a point under the readout, which ignites: a white
// flash and a fireball blooming out from it, billowing as it burns. Then it
// rolls into smoke: the ball draws in to a smouldering core that keeps
// burning, puffs of smoke lit from below by it roll off and drift up and
// out, and embers rise and drift. Settle: the core goes out and the last
// smoke and embers fade.
inline void Renderer::alchemyIgnition(float t) {
  constexpr float SNAP=.12f,RUN=.16f,IGN=.55f,SHRINK=1.4f,DOUSE=3.8f,END=4.95f;
  const Box &b=number_;
  const float sx=10.0f*w_/135,sy=b.y1+113.0f,cx=b.cx();
  const float fy=b.y1+26-4*smooth((t-IGN)/1.5f);
  const Rgb gold=rgb8(255,226,130),ember=rgb8(255,120,30);
  if(t>=END)return;
  // The snap: a glint at the fingertip, then a flash of short rays.
  if(t<SNAP+.25f) {
    const float g=t<SNAP?smooth(t/SNAP):1-smooth((t-SNAP)/.25f);
    twinkle(sx,sy,mixed(gold,WHITE,.5f),g*1.2f,t<SNAP?1:2);
    if(t>=SNAP && t<SNAP+.18f) {
      const float u=(t-SNAP)/.18f,len=3+7*easeOut(u);const Ink c=inkOf(mixed(WHITE,gold,u));
      for(int i=0;i<6;++i){const float a=i*(HALF_TURN/3)+.3f,ca=cosine(a),sa=sine(a);
        alchemyLine(sx+ca*3,sy+sa*3,sx+ca*len,sy+sa*len,c,q8(1-u),q8(.3f*(1-u)));}
    }
  }
  // The run: a zigzag of six knots from the snap to the ignition point.
  float kx[6],ky[6];
  {
    const float dx=cx-sx,dy=fy+4-sy,inv=1/std::sqrt(dx*dx+dy*dy);
    for(int i=0;i<6;++i){const float f=i/5.0f,side=i==0 || i==5?0:(i&1?1.0f:-1.0f)*(5+5*rnd(i,521));
      kx[i]=sx+dx*f-dy*inv*side;ky[i]=sy+dy*f+dx*inv*side;}
  }
  const float s=(t-RUN)/(IGN-RUN);
  if(s>0 && t<IGN+.45f) {
    // The fuse: a spark every 2 px of path, lit once the head passes,
    // flickering and cooling for 0.4 s.
    const int frame=floori(t*20+.001f);int id=0;
    // Per spark only multiplies: each leg's reciprocals are taken once.
    const Ink emberInk=inkOf(ember);
    for(int seg=0;seg<5;++seg) {
      const float ex=kx[seg+1]-kx[seg],ey=ky[seg+1]-ky[seg],len=std::sqrt(ex*ex+ey*ey);const int n=std::max(1,int(len*.7f));
      const float invN=1.0f/n,nx=-ey/len,ny=ex/len;
      for(int i=0;i<n;++i,++id) {
        const float f=i*invN,g=(seg+f)*.2f,age=t-(RUN+g*(IGN-RUN));if(age<0 || age>=.4f)continue;
        const float v=age*2.5f;
        {const int x=roundi(kx[seg]+ex*f),y=roundi(ky[seg]+ey*f);
         if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_))addw(x,y,emberInk,q8(.45f*(1-v)));}
        if(rnd(uint32_t(id)*64+frame,522)>.8f)continue;
        const float j=(rnd(uint32_t(id)*64+frame,523)-.5f)*3;
        dot(kx[seg]+ex*f+nx*j,ky[seg]+ey*f+ny*j,mixed(gold,ember,v),1.5f*(1-v)*(1-v));
      }
    }
    if(s<1) {
      const float at=s*5;const int seg=std::min(4,int(at));const float f=at-seg;
      const float hx=kx[seg]+(kx[seg+1]-kx[seg])*f,hy=ky[seg]+(ky[seg+1]-ky[seg])*f;
      spark(hx,hy,gold,1.4f,2.2f);twinkle(hx,hy,mixed(gold,WHITE,.6f),1.2f,3);dot(hx-(kx[seg+1]-kx[seg])*.08f,hy-(ky[seg+1]-ky[seg])*.08f,gold,.9f);
    }
  }
  if(t<IGN)return;
  const float u=t-IGN;
  glint(t,IGN+.03f,.45f);
  // The fireball: blooms, billows, draws in to a smouldering core, goes out.
  const float r=u<.35f?30*easeOut(u/.35f):t<SHRINK?30+1.5f*smooth((u-.35f)/(SHRINK-IGN-.35f)):
    t<DOUSE?20+11.5f*(1-smooth((t-SHRINK)/.8f))+1.2f*sine(t*6.1f)*smooth((t-SHRINK-.5f)/.4f):20*(1-smooth((t-DOUSE)/.7f));
  const int cool=q8(t<SHRINK?1-.15f*smooth((t-IGN-.3f)/.6f):.85f-.2f*smooth((t-SHRINK)/2.2f));
  const int boost=int(220*(1-smooth(u/.3f)));
  alchemyFireball(cx,fy,r,cool,boost,int(t*700));
  if(u<.12f)twinkle(cx,fy,WHITE,1.5f*(1-u/.12f),4);
  // Smoke rolls out from under the readout in wisps that drift away to
  // both sides, waving as they go, warm where they leave the fire.
  // Per step the wisp is plain multiply-adds: its wave and envelope turn by
  // incremental rotation, its colour and position step in fixed point.
  const Rgb smoke=mixed(rgb8(150,142,134),accent_,.12f);const Ink smokeInk=inkOf(smoke),emberInk=inkOf(ember);
  const float WC=cosine(.32f),WS=sine(.32f);
  for(unsigned i=0;i<10;++i) {
    const float age=t-(1.0f+.27f*i+.1f*rnd(i,524)),life=1.4f+.4f*rnd(i,525);if(age<0 || age>=life)continue;
    const float v=age/life,side=i%2?1.0f:-1.0f,len=10+26*easeOut(v*1.5f),tail=len*smooth((v-.45f)/.55f);
    const float ex=cx+side*r*.8f,ey=fy+3+4*rnd(i,526),env=(1-smooth((t-END+.5f)/.5f))*(1-v*.5f);
    const int n=int(len-tail-.001f)+1;if(n<1)continue;
    // Envelope sin(pi*f) over the n steps, f=(q-tail)/(len-tail).
    const float de=HALF_TURN/(len-tail+.01f),ec=cosine(de),es=sine(de);float eC=1,eS=0;
    // Wave 2.2*sin(q*.32-age*5+i*1.7).
    const float ph=tail*.32f-age*5+i*1.7f;float wC=cosine(ph),wS=sine(ph);
    // Colour: ember mixed in by .5*(1-q/len)*(1-v), in Q16 per step.
    const float m0=128*(1-v)/len;int mix16=int(m0*(len-tail)*256),dmix=-int(m0*256);
    float x=ex+side*(tail*.9f+age*10),y=ey-tail*.32f-age*3;const float sxs=side*.9f;
    const float kq=.9f*env*256;
    for(int j=0;j<n;++j,x+=sxs,y-=.32f) {
      const int xi=roundi(x);
      if(unsigned(xi)<unsigned(w_)) {
        const int k=int(kq*eS),y8=int((y+2.2f*wS)*256+32768)-32768,iy=y8>>8,ay=y8&255;
        const Ink c=mixedInk(smokeInk,emberInk,mix16>>8);
        const int k1=(k*110)>>8,k2=k>>2;
        // Three vertical splits: the core, a softer row above, a faint one below.
        addw(xi,iy,c,(k*(256-ay))>>8);addw(xi,iy+1,c,(k*ay)>>8);
        {const int y1=y8-333,i1=y1>>8,a1=y1&255;addw(xi,i1,c,(k1*(256-a1))>>8);addw(xi,i1+1,c,(k1*a1)>>8);}
        {const int y2=y8+333,i2=y2>>8,a2=y2&255;addw(xi,i2,c,(k2*(256-a2))>>8);addw(xi,i2+1,c,(k2*a2)>>8);}
      }
      const float ne=eC*ec-eS*es;eS=eS*ec+eC*es;eC=ne;
      const float nw=wC*WC-wS*WS;wS=wS*WC+wC*WS;wC=nw;
      mix16+=dmix;
    }
  }
  // Embers rise and drift off the ball.
  for(unsigned i=0;i<26;++i) {
    const float age=t-IGN-.05f-3.1f*rnd(i,528),life=.9f+.8f*rnd(i,529);if(age<0 || age>=life)continue;
    const float a=HALF_TURN*(.1f+.8f*rnd(i,530)),r0=r*(.5f+.5f*rnd(i,531)),v=age/life,side=cosine(a)<0?-1.0f:1.0f;
    const float x=cx+cosine(a)*r0+side*(8+24*rnd(i,532))*age+2*sine(age*6+i),y=fy+sine(a)*r0-(6+14*rnd(i,533))*age;
    const float k=(1-v)*(.7f+.3f*sine(age*23+i))*(1-smooth((t-END+.4f)/.4f));
    dot(x,y,mixed(gold,ember,std::min(1.0f,v*1.5f)),k*1.6f);
  }
}

// ---- Spires: two palms of light swing in and clap above the floor with a
// flash, press down onto it, and a reaction races out along the floor both
// ways; behind its front a row of faceted stone spikes bursts up, tallest
// in the middle, leaning outward, kicking up dust. Sustain: they stand,
// light running up their edges one after another, grit trickling down their
// faces and a spark snapping between their feet now and then. Settle: they
// crumble from the outside in, tips breaking off and tumbling, and sink.
inline void Renderer::alchemySpires(float t) {
  constexpr float CLAP=.3f,PRESS=.45f,SPEED=240,HOLD=3.55f,END=4.9f;constexpr int N=7;
  const Box &b=number_;const float cx=b.cx(),fy=b.y1+116.0f;
  const Rgb light=rgb8(232,212,178),mid=rgb8(168,148,124),dark=rgb8(112,96,82),dust=rgb8(190,176,152);
  const Rgb react=mixed(rgb8(150,212,255),accent_,.25f);
  if(t>=END)return;
  const int frame=floori(t*20+.001f);
  // The palms: in from both sides, the clap, then down onto the floor.
  if(t<PRESS+.2f) {
    const float in=t<CLAP?1-smooth(t/CLAP)*smooth(t/CLAP):0,drop=smooth((t-CLAP)/(PRESS-CLAP));
    const float k=smooth(t/.1f)*(1-smooth((t-PRESS)/.2f)),py=fy-22+11*drop;
    for(int side=-1;side<=1;side+=2) {
      const float px=cx+side*(3.5f+34*in);
      ellipseAt(px,py,4,10,mixed(react,WHITE,.3f),k,2.5f);
      ellipseAt(px-side*3,py+3,2,3.5f,react,k*.8f,2.5f);
      for(int f=0;f<3;++f)alchemyLine(px-side*(1.5f-1.5f*f),py-10,px-side*(1.5f-1.5f*f),py-5,inkOf(react),q8(.5f*k),q8(.2f*k));
      if(t<CLAP)for(int l=0;l<3;++l){const float ly=py-4+4*l,x0=px+side*5;
        alchemyLine(x0,ly,x0+side*(4+10*in),ly,inkOf(react),q8(.6f*k),0);}
    }
    if(t>=CLAP && t<CLAP+.15f)twinkle(cx,fy-24+11*drop,WHITE,1.5f*(1-(t-CLAP)/.15f),5);
  }
  if(t<PRESS)return;
  const float u=t-PRESS,front=SPEED*u;
  glint(t,PRESS+.02f,.45f);
  if(u<.18f)twinkle(cx,fy-1,WHITE,1.5f*(1-u/.18f),5);
  // The floor: the reaction's bright front races out both ways, cooling to
  // a dim line of rubble behind it.
  const float floorK=1-smooth((t-HOLD-.6f)/.6f);
  {
    const Ink hot=inkOf(mixed(react,WHITE,.5f)),cold=inkOf(mid);
    for(int x=0;x<w_;++x) {
      const float d=std::fabs(x-cx);if(d>front)continue;
      const float behind=front-d,heat=std::max(0.0f,1-behind/30);
      const int k=q8(floorK*(.3f+.9f*heat)*(x&1?1:.75f));
      addw(x,roundi(fy+1),heat>.1f?mixedInk(cold,hot,int(256*heat)):cold,k);
      if(heat>.6f)addw(x,roundi(fy+2),hot,k>>1);
    }
    // Crackle at both fronts while they run.
    if(front<cx+20)for(int side=-1;side<=1;side+=2) {
      const float x0=cx+side*front;uint32_t seed=uint32_t(frame)*4+(side>0);
      float px=x0,py=fy;
      for(int j=1;j<=4;++j){const float nx=x0-side*3*j,ny=fy-1.5f-3*rnd(seed*8+j,541)*(j&1?1:.3f);
        alchemyLine(px,py,nx,ny,inkOf(WHITE),q8(.9f),q8(.5f));px=nx;py=ny;}
    }
  }
  // The spikes.
  const Ink lit=inkOf(light),ridge=inkOf(mid),shade=inkOf(dark);
  int glintSpike=-1;float glintAt=0;
  if(t>1.2f && t<HOLD){const float p=(t-1.2f)/.32f;glintSpike=int(hash(uint32_t(p)*7+3)%N);glintAt=p-floori(p);}
  for(int i=0;i<N;++i) {
    const float x=7+i*(w_-14.0f)/(N-1)+(rnd(i,542)-.5f)*4,d=std::fabs(x-cx);
    const float rise=PRESS+d/SPEED,e=(t-rise)/.14f;if(e<=0)continue;
    const float g=e<1?easeOut(e)+.12f*sine(HALF_TURN*e):1;
    const float h=(40-18*std::min(1.0f,d/68))*(.85f+.25f*rnd(i,543)),w=5+2*rnd(i,544),lean=(x-cx)*.09f+(rnd(i,545)-.5f)*3;
    const int order=int((70-d)/7);const float crumble=t-(HOLD+.07f*order),c=smooth(crumble/.5f);
    const float H=h*g,keep=1-c,tx=x+lean,ty=fy-H;
    if(keep>0.02f && H>1) {
      // Row by row from the foot: a solid lit left face, a ridge, a dark
      // right face in shadow, solid edges; only the part below `keep` of the
      // height while it crumbles. The lit face burns brighter while the
      // reaction is fresh.
      const float k=1-smooth((t-END+.6f)/.6f),xm0=x+w*.25f;
      const int kl=q8(k),kf=q8((t<rise+.5f?.75f:.6f)*k),kr=q8(.75f*k);
      for(int y=roundi(fy)-1;y>=roundi(fy-H*keep);--y) {
        const float f=(fy-y)/H;if(unsigned(y)>unsigned(bottom_))continue;
        const int xl=roundi(x-w+(tx-x+w)*f),xm=roundi(xm0+(tx-xm0)*f),xr=roundi(x+w+(tx-x-w)*f);
        if(unsigned(xl)<unsigned(w_))addw(xl,y,lit,kl);
        // The lit face: solid, shading from bright at the edge to the ridge.
        const int span=std::max(1,xm-xl);
        for(int xx=xl+1;xx<xm;++xx)if(unsigned(xx)<unsigned(w_))addw(xx,y,lit,kf-(kf*(xx-xl)*5)/(span*8));
        if(xm>xl && xm<xr && unsigned(xm)<unsigned(w_))addw(xm,y,ridge,kr);
        if(xr>xl && unsigned(xr)<unsigned(w_))addw(xr,y,shade,kl);
      }
      // Light runs up the lit edge.
      if(i==glintSpike && c==0) {
        const float gk=sine(HALF_TURN*glintAt);
        for(int q=0;q<7;++q){const float f=glintAt-q*.025f;if(f<0)break;
          addw(roundi(x-w+(tx-x+w)*f),roundi(fy-H*f),WHITE_INK,q8(gk*(1-q/7.0f)));}
        twinkle(x-w+(tx-x+w)*glintAt,fy-H*glintAt,WHITE,.8f*gk,1);
      }
    }
    // Dust kicked up as it bursts out.
    for(int q=0;q<4;++q) {
      const float age=t-rise-.02f*q,life=.5f+.3f*rnd(i*8+q,546);if(age<0 || age>=life)continue;
      const float side=q&1?1.0f:-1.0f,vx=side*(12+30*rnd(i*8+q,547)),vy=-(18+30*rnd(i*8+q,548));
      dot(x+side*w+vx*age,fy-1+vy*age+90*age*age,dust,.9f*(1-age/life));
    }
    // Chunks break off the tip and tumble down as it crumbles.
    if(crumble>0)for(int q=0;q<4;++q) {
      const float age=crumble-.09f*q,life=.6f;if(age<0 || age>=life)continue;
      const float f=1-.17f*(q+1),sx=x+(tx-x)*f,sy=fy+(ty-fy)*f,vx=(rnd(i*8+q,549)-.5f)*36;
      const float px=sx+vx*age,py=std::min(fy-1,sy-10*age+150*age*age),spin=age*14+q;
      const float k=1-age/life;
      alchemyLine(px-1.5f*cosine(spin),py-1.5f*sine(spin),px+1.5f*cosine(spin),py+1.5f*sine(spin),q&1?lit:ridge,q8(k),q8(k*.7f));
    }
  }
  // Sustain: grit trickles down the faces, and a spark snaps between feet.
  for(unsigned i=0;i<26;++i) {
    const float age=t-1.1f-2.4f*rnd(i,550),life=.7f+.4f*rnd(i,551);if(age<0 || age>=life)continue;
    const int s=int(hash(i*13+5)%N);const float x=7+s*(w_-14.0f)/(N-1)-3+6*rnd(i,552),y=fy-8-20*rnd(i,553)+60*age*age+6*age;
    if(y<fy)dot(x,y,dust,.8f*(1-age/life));
  }
  if(t>1.2f && t<HOLD+.2f) {
    const uint32_t seed=uint32_t(frame);
    if(rnd(seed,554)<.3f) {
      const int s=int(rnd(seed,555)*(N-1));const float x0=7+s*(w_-14.0f)/(N-1)+5,x1=x0+(w_-14.0f)/(N-1)-10;
      float px=x0,py=fy-1;for(int j=1;j<=4;++j){const float nx=x0+(x1-x0)*j/4,ny=fy-1-(j<4?2.5f*rnd(seed*8+j,556):0);
        alchemyLine(px,py,nx,ny,inkOf(mixed(react,WHITE,.4f)),q8(.8f),q8(.8f));px=nx;py=ny;}
    }
  }
}

// A dark hand: an arm curving from (sx,sy), bowing `bend` px to one side
// on its way to (tx,ty), drawn out to `reach` (0..1) of its length and
// waving by `wave` px toward its tip. Only its outline is lit: two rims
// about six pixels apart at the shoulder and four at the wrist, so a thick
// dark body shows between them. At full reach a palm opens at the tip with
// four fingers, curled by `grip` (0 open, 1 closed).
inline void Renderer::alchemyHand(float sx,float sy,float tx,float ty,float bend,float reach,float wave,float grip,const Ink &rim,int kq) {
  if(reach<=0 || kq<5)return;
  const float dx=tx-sx,dy=ty-sy,len=std::sqrt(dx*dx+dy*dy),nx=-dy/len,ny=dx/len;
  const int n=std::max(4,int(len*reach));
  float px=sx,py=sy,ax=0,ay=-1;
  for(int i=1;i<=n;++i) {
    const float s=reach*i/n,b=4*s*(1-s)*bend+wave*s*sine(s*7.0f);
    const float x=sx+dx*s+nx*b,y=sy+dy*s+ny*b;
    const float ex=x-px,ey=y-py,el=std::sqrt(ex*ex+ey*ey)+.001f;ax=ex/el;ay=ey/el;
    const float hw=3.2f-1.2f*s;const int k=int(kq*(.6f+.4f*s));
    const int xa=roundi(x-ay*hw),ya=roundi(y+ax*hw),xb=roundi(x+ay*hw),yb=roundi(y-ax*hw);
    if(unsigned(xa)<unsigned(w_) && unsigned(ya)<=unsigned(bottom_))addw(xa,ya,rim,k);
    if(unsigned(xb)<unsigned(w_) && unsigned(yb)<=unsigned(bottom_))addw(xb,yb,rim,k);
    px=x;py=y;
  }
  if(reach<.8f)return;
  // The palm: an open ring at the wrist, and the fingers off its far side.
  const float open=smooth((reach-.8f)/.2f),base=std::atan2(ay,ax),pr=3.6f*open;
  const float cxp=px+ax*pr,cyp=py+ay*pr;
  alchemyArc(cxp,cyp,pr,base+HALF_TURN*.35f,HALF_TURN*1.3f,rim,kq);
  for(int f=0;f<4;++f) {
    const float a=base+(f-1.5f)*.5f*(1-.45f*grip),c=(f<2?-1.0f:1.0f)*1.2f*grip,l=6*open;
    const float fx=cxp+pr*cosine(a),fy=cyp+pr*sine(a);
    const float mx=fx+l*.5f*cosine(a),my=fy+l*.5f*sine(a),ex=mx+l*.5f*cosine(a+c),ey=my+l*.5f*sine(a+c);
    alchemyLine(fx,fy,mx,my,rim,kq,kq);alchemyLine(mx,my,ex,ey,rim,kq,(kq*3)>>2);
  }
}

// ---- Gate: two tall stone doors rise out of the floor, meeting in the
// middle and filling the screen from under the readout to the replay's
// middle: thick lit edges, fluted faces and carved bands. They swing open toward us onto
// darkness, light flaring along their parting edges, to narrow slabs in the
// strips at the screen's sides; a long eye opens in the band just beneath
// the readout, a pulse ringing out from it, and three thick dark hands,
// outlined in light, reach up out of the gap toward the readout, fingers
// splaying. Sustain: the hands grope and clutch, the eye looks about and
// blinks. Settle: the hands snap back, the doors slam shut with a flash
// down the seam, and they sink into the floor.
inline void Renderer::alchemyGate(float t) {
  constexpr float RISE=.42f,OPEN=.45f,SWING=.45f,EYE=.95f,REACH=1.1f,RECALL=3.5f,SHUT=3.8f,SLAM=.28f,SINK=4.35f,END=4.95f;
  const Box &b=number_;const float cx=b.cx(),top=b.y1+4.0f,fy=b.y1+60.0f,H=fy-top;
  // Hinges at the screen's edges, each door inside one mask word column
  // once open; closed, the two doors meet at the centre.
  const float HL=3,HR=w_-11.0f,WL=cx-HL,WR=HR-cx;
  const Rgb stone=rgb8(214,204,186),edgeC=rgb8(246,238,222),carve=rgb8(150,138,122),rimC=mixed(rgb8(205,186,245),accent_,.25f),pale=rgb8(240,236,246);
  if(t>=END)return;
  const float lift=t<SINK?easeOut(t/RISE):1-smooth((t-SINK)/(END-SINK-.05f)),dy=(1-lift)*(H+6);
  const float theta=t<SHUT?1.38f*smooth((t-OPEN)/SWING):1.38f*(1-smooth((t-SHUT)/SLAM));
  const float fade=1-smooth((t-END+.25f)/.25f);
  // Door segments, risen by `dy` and clipped at the floor.
  auto seg=[&](float x0,float y0,float x1,float y1,const Ink &c,int k){
    y0+=dy;y1+=dy;if(y0>fy-1 && y1>fy-1)return;
    if(y0>fy-1){x0+=(x1-x0)*(y0-fy+1)/(y0-y1);y0=fy-1;}
    if(y1>fy-1){x1+=(x0-x1)*(y1-fy+1)/(y1-y0);y1=fy-1;}
    alchemyLine(x0,y0,x1,y1,c,k,k);
  };
  // The parting edges flare as the doors open and again as they slam.
  const float opening=t>=OPEN && t<OPEN+SWING?sine(HALF_TURN*(t-OPEN)/SWING):0,slam=t>=SHUT+SLAM && t<SHUT+SLAM+.3f?1-(t-SHUT-SLAM)/.3f:0;
  const float flare=std::max(opening,slam);
  const Ink st=inkOf(stone),ed=inkOf(mixed(edgeC,WHITE,flare)),cv=inkOf(carve),glowC=inkOf(mixed(stone,rimC,.3f));
  const int ks=q8(.9f*fade),ke=q8(fade),kg=q8(.5f*fade),kc=q8(.6f*fade);
  const float cw=cosine(theta),grow=sine(theta)*4;
  for(int side=-1;side<=1;side+=2) {
    const float hx=side<0?HL:HR,W=side<0?WL:WR,fx=hx-side*W*cw;
    // The free edge: a bright core with a glow row each side, its top and
    // foot grown a little in perspective; the hinge edge one pixel.
    seg(fx,top-grow,fx,fy+grow,ed,ke);
    seg(fx+side,top-grow,fx+side,fy+grow,glowC,kg);seg(fx-side,top-grow,fx-side,fy+grow,ed,(ke*170)>>8);
    seg(hx,top,hx,fy,st,ks);
    // The face between: three carved vertical flutes that close up as the
    // door turns edge-on, brightest beside the free edge.
    for(int q=1;q<=3;++q) {
      const float f=q/4.0f,x=hx+(fx-hx)*f;
      seg(x,top+2-grow*f,x,fy+grow*f,st,(kc*(120+int(100*f)))>>8);
    }
    // Top edge and two carved bands, each a lit row over a dark groove.
    seg(hx,top,fx,top-grow,st,ks);
    for(int r=0;r<2;++r){const float f=.34f+.33f*r,y=top+H*f;seg(hx,y,fx,y+(f-.5f)*2*grow,st,kc);seg(hx,y+2,fx,y+2+(f-.5f)*2*grow,cv,kc>>1);}
  }
  // Dust where they break the floor.
  if(t<RISE+.15f || t>SINK)for(unsigned i=0;i<10;++i) {
    const float ph=(t<SINK?t:t-SINK)*2.4f+rnd(i,561),u=ph-floori(ph);
    const float x=HL+(HR-HL)*rnd(i+uint32_t(ph)*16,562),y=fy-1-6*u+8*u*u;
    dot(x,y,carve,.8f*(1-u)*fade);
  }
  // The slam: a flash down the seam.
  if(slam>0){seg(cx,top,cx,fy,WHITE_INK,q8(slam));twinkle(cx,top+H*.5f+dy,WHITE,slam*1.3f,4);}
  if(t<EYE)return;
  // The eye: a long almond in the band beneath the readout; bold lids, a
  // solid iris round a dark slit pupil; it looks about and blinks.
  const float ey=b.y1+10.0f;
  float lid=smooth((t-EYE)/.18f)*(1-smooth((t-RECALL)/.25f));
  for(int k=0;k<2;++k){const float at=2.45f+.85f*k;if(t>at && t<at+.18f)lid*=std::fabs(1-2*(t-at)/.18f);}
  if(lid>.02f) {
    const float hh=5*lid,look=4.5f*sine((t-EYE)*1.25f)*smooth((t-EYE-.4f)/.4f);
    const Ink lidInk=inkOf(pale),iris=inkOf(rimC);const int kl=q8(fade),ki=q8(.95f*fade),ix=roundi(cx+look);
    for(int x=roundi(cx-16);x<=roundi(cx+16);++x) {
      const float u=(x-cx)/16,o=hh*(1-u*u);
      addw(x,roundi(ey-o),lidInk,kl);addw(x,roundi(ey-o)-1,iris,(kl*150)>>8);
      if(o>.6f)addw(x,roundi(ey+o),lidInk,kl);
      for(int y=roundi(ey-o)+1;y<roundi(ey+o);++y){const int ddx=x-ix,ddy=y-roundi(ey),d2=ddx*ddx+ddy*ddy;
        if(d2<=17 && (std::abs(ddx)>=1 || d2>9))addw(x,y,iris,(ki*(150+d2*6))>>8);}
    }
  }
  if(t<EYE+.35f){const float u=(t-EYE)/.35f;ellipseAt(cx,ey+2,18+30*easeOut(u),5+22*easeOut(u),rimC,.9f*(1-u)*(1-u),1.8f);}
  glint(t,EYE+.3f,.45f);
  // The hands: up out of the gap, toward the readout either side of the eye.
  static constexpr float HAND[3][4]={{-20,40,-30,-7},{20,40,30,7},{-3,50,-9,4}};
  if(t>=REACH) {
    const Ink rim=inkOf(rimC);
    for(int i=0;i<3;++i) {
      const float out=easeOut((t-REACH-.1f*i)/.45f),grope=.12f*smooth((t-REACH-.7f)/.4f)*(1+sine(t*2.1f+i*1.7f));
      const float held=1-.12f*(1+sine(RECALL*2.1f+i*1.7f)),r=t<RECALL?out-grope:held*(1-smooth((t-RECALL)/.28f));if(r<=0)continue;
      const float sx=cx+HAND[i][0],sy=b.y1+HAND[i][1],tx=cx+HAND[i][2]+2*sine(t*1.9f+i),ty=b.y1+(i<2?12.0f:20.0f)+1.5f*sine(t*2.2f+i);
      const float wave=1.6f*sine(t*2.7f+i*1.3f)*smooth((t-REACH-.4f)/.5f);
      alchemyHand(sx,sy,tx,ty,HAND[i][3],r,wave,.5f+.5f*sine(t*2.3f+i*1.9f),rim,q8(fade));
    }
  }
}

// A short zigzag spark from (x0,y0) to (x1,y1): four legs, alternating sides.
inline void Renderer::alchemyZap(float x0,float y0,float x1,float y1,uint32_t seed,const Ink &c,int kq) {
  const float dx=x1-x0,dy=y1-y0,len=std::sqrt(dx*dx+dy*dy)+.01f,nx=-dy/len,ny=dx/len;
  float px=x0,py=y0;
  for(int i=1;i<=4;++i) {
    const float f=i/4.0f,j=i==4?0:(i&1?1.0f:-1.0f)*(1+2*rnd(seed*8+i,571));
    const float x=x0+dx*f+nx*j,y=y0+dy*f+ny*j;
    alchemyLine(px,py,x,y,c,kq,i==4?kq>>1:kq);px=x;py=y;
  }
}

// ---- Fracture: a palm print glows red below the readout, red sparks
// crackling round it; then cracks race out of it across the background in a
// branching web, white-hot at their tips and glowing red from within, and
// at full reach the web flashes and sparks leap along it. Sustain: waves of
// heat pulse out from the palm along the cracks, a spark snaps along one
// now and then, and grit glows as it sifts out of them. Settle: from the
// palm outward the web breaks up, its pieces lifting and tumbling away as
// they cool, into dust.
inline void Renderer::alchemyFracture(float t) {
  constexpr float PRESS=.35f,SPEED=150,FULL=.9f,CRUMBLE=3.4f,END=4.95f;constexpr int MAINS=6,KNOTS=12;
  // The palm sits under the readout's left half, in the open face of the
  // replay between its lines, where they do not cut it.
  const Box &b=number_;const float px=b.cx()-16,py=b.y1+36.0f;
  const Rgb red=rgb8(255,40,24),blood=rgb8(150,0,10),hot=rgb8(255,200,150);
  const Ink bloodInk=inkOf(blood);
  if(t>=END)return;
  const int frame=floori(t*20+.001f);
  // The palm print: a palm and five fingers, pulsing while it charges.
  {
    const float k=smooth(t/.25f)*(1-smooth((t-CRUMBLE)/.6f))*(t<FULL?.8f+.4f*sine(t*30):.75f+.15f*sine(t*5));
    if(k>.02f) {
      const Rgb c=mixed(red,hot,t<FULL?.4f:.1f);
      // A glowing palm, hottest at its heart.
      const Ink heart=inkOf(mixed(hot,red,.3f)),rimInk=inkOf(red);const int kq=q8(k);
      const int pcx=roundi(px),pcy=roundi(py+4);
      for(int dy=-8;dy<=8;++dy)for(int dx=-7;dx<=7;++dx) {
        const int q=(dx*dx*256)/49+(dy*dy*256)/64;if(q>=256)continue;
        addw(pcx+dx,pcy+dy,mixedInk(heart,rimInk,q),(kq*(235-((110*q)>>8)))>>8);
      }
      static constexpr float FING[5][3]={{-7,0,-.9f},{-3.5f,-4,-.35f},{0,-5,0},{3.5f,-4,.35f},{7,0,.75f}};
      for(int f=0;f<5;++f){const float bx=px+FING[f][0],by=py+FING[f][1],a=-HALF_TURN/2+FING[f][2]*.5f,l=f==0 || f==4?5:7;
        alchemyLine(bx,by,bx+l*cosine(a),by+l*sine(a),inkOf(c),q8(k),q8(k*.6f));
        alchemyLine(bx+1,by,bx+1+l*cosine(a),by+l*sine(a),inkOf(c),q8(k*.6f),q8(k*.35f));}
    }
  }
  if(t<PRESS) {
    // Charging: red sparks crackle round the palm.
    for(int j=0;j<3;++j){const uint32_t seed=uint32_t(frame)*8+j;if(rnd(seed,572)<.3f)continue;
      alchemyCrackle(px,py+3,10+2*rnd(seed,573),2*HALF_TURN*rnd(seed,574),.6f+.6f*rnd(seed,575),3*rnd(seed,576),seed,inkOf(mixed(red,hot,.5f)),bloodInk,q8(.5f+.5f*t/PRESS));}
    return;
  }
  const float u=t-PRESS,reach=SPEED*u;
  if(t>=FULL)glint(t,FULL+.02f,.45f);
  // The web: six main cracks out of the palm all round (bent away from
  // the readout), the ones running right and sideways longer, each a
  // jagged run of knots with a branch or two.
  // Every segment keeps its unit direction (from the table sine) and its
  // length, so drawing it needs no root, arctangent or division.
  const float flash=t<FULL?0:1-smooth((t-FULL)/.35f),cool=1-smooth((t-CRUMBLE+.4f)/.6f),waveT=(t-FULL)*40;
  const Ink redInk=inkOf(red);
  for(int m=0;m<MAINS;++m) {
    float xs[KNOTS],ys[KNOTS],ds[KNOTS],dc[KNOTS],dsn[KNOTS],dl[KNOTS],da[KNOTS];
    float a=HALF_TURN*(2.0f*(m+.3f)/MAINS+(rnd(m,577)-.5f)*.2f);
    const int knots=std::min(KNOTS,5+int(5*std::max(0.0f,cosine(a))+2*std::fabs(cosine(a))+.5f));
    xs[0]=px+8*cosine(a);ys[0]=py+4+8*sine(a);ds[0]=0;
    for(int k=1;k<knots;++k) {
      a+=(rnd(m*16+k,578)-.5f)*.8f;
      // Keep out of the readout: bend away from straight up.
      if(sine(a)<-.55f)a+=cosine(a)>0?.4f:-.4f;
      const float l=5+4*rnd(m*16+k,579),ca=cosine(a),sa=sine(a);
      xs[k]=xs[k-1]+l*ca;ys[k]=ys[k-1]+l*sa;ds[k]=ds[k-1]+l;dc[k-1]=ca;dsn[k-1]=sa;dl[k-1]=l;da[k-1]=a;
    }
    // Branches: off the third and sixth knots, the second only on some.
    for(int br=-1;br<2;++br) {
      float bx[5],by[5],bd[5],bc[5],bs[5],bl[5];int n=knots;
      const float *X=xs,*Y=ys,*D=ds,*DC=dc,*DS=dsn,*DL=dl;
      if(br>=0) {
        const int at=br?5:2;if(at+1>=knots || (br && rnd(m,580)<.7f))continue;
        float ba=da[at]+(rnd(m*4+br,581)<.5f?-1:1)*(.6f+.4f*rnd(m*4+br,582));
        bx[0]=xs[at];by[0]=ys[at];bd[0]=ds[at];
        for(int k=1;k<5;++k){ba+=(rnd(m*64+br*8+k,583)-.5f)*.8f;if(sine(ba)<-.55f)ba+=cosine(ba)>0?.4f:-.4f;
          const float l=4+3*rnd(m*64+br*8+k,584),ca=cosine(ba),sa=sine(ba);
          bx[k]=bx[k-1]+l*ca;by[k]=by[k-1]+l*sa;bd[k]=bd[k-1]+l;bc[k-1]=ca;bs[k-1]=sa;bl[k-1]=l;}
        X=bx;Y=by;D=bd;DC=bc;DS=bs;DL=bl;n=5;
      }
      for(int k=0;k+1<n;++k) {
        const float d0=D[k],d1=D[k+1];if(d0>=reach)break;
        // The segment's life: cut in, glow, crumble from the palm outward.
        const float crumbleAt=CRUMBLE+d0*(.9f/80)+.15f*rnd(m*32+k+br*7,585),c=t-crumbleAt;
        const float run=reach<d1?reach-d0:DL[k];
        float x1=X[k]+DC[k]*run,y1=Y[k]+DS[k]*run;
        if(c<0) {
          // Heat: white at the running tip, then a red glow with waves of
          // heat pulsing outward through the sustain.
          const float behind=reach-d1,tipHeat=std::max(0.0f,1-behind*(1/18.0f));
          const float wave=t<FULL+.3f?0:.5f+.5f*sine((d0-waveT)*.18f);
          const int heat=int(110+120*tipHeat+70*flash+40*wave*cool-40*(1-cool));
          const int kw=q8((br>=0?.8f:1)*(.75f+.25f*cool));
          const Ink core=fire(std::min(255,std::max(40,heat)));
          alchemyLine(X[k],Y[k],x1,y1,core,kw,kw);
          // Lit from within: a red glow along one side and every other
          // stretch of the other, a bold stroke.
          const float gx=-DS[k],gy=DC[k];
          const Ink &glow=flash>.3f?redInk:bloodInk;
          alchemyLine(X[k]+gx,Y[k]+gy,x1+gx,y1+gy,glow,(kw*150)>>8,(kw*150)>>8);
          if(!(k&1))alchemyLine(X[k]-gx,Y[k]-gy,x1-gx,y1-gy,glow,(kw*110)>>8,(kw*110)>>8);
        } else if(c<.75f) {
          // Crumbling: the piece lifts, then tumbles down as it cools.
          const float v=c*(1/.75f),mx=(X[k]+x1)*.5f,my=(Y[k]+y1)*.5f,side=rnd(m*32+k,586)<.5f?-1.0f:1.0f;
          const float ox=side*(4+8*rnd(m*32+k,587))*c,oy=-8*c+70*c*c,spin=side*c*(4+4*rnd(m*32+k,588));
          const float hx=(x1-X[k])*.4f,hy=(y1-Y[k])*.4f,cs=cosine(spin),sn=sine(spin);
          const float rx=hx*cs-hy*sn,ry=hx*sn+hy*cs;
          const int kk=q8(.9f*(1-v));
          alchemyLine(mx+ox-rx,my+oy-ry,mx+ox+rx,my+oy+ry,fire(int(150-100*v)),kk,kk);
          if(v<.7f)dot(mx+ox*.4f,my-2*c+40*c*c,mixed(red,blood,v),.8f*(1-v));
        }
      }
    }
  }
  // Sparks leap along the web at full reach, then snap now and then.
  if(t>=FULL-.1f && t<CRUMBLE) {
    const int n=t<FULL+.4f?3:1;
    for(int j=0;j<n;++j) {
      const uint32_t seed=uint32_t(frame)*8+j;if(t>=FULL+.4f && rnd(seed,589)>.35f)continue;
      const float a=2*HALF_TURN*rnd(seed,590),d=10+40*rnd(seed,591),l=8+10*rnd(seed,592);
      const float x0=px+d*cosine(a),y0=py+4+d*sine(a),x1=px+(d+l)*cosine(a+.2f),y1=py+4+(d+l)*sine(a+.2f);
      alchemyZap(x0,y0,x1,y1,seed,inkOf(mixed(red,hot,.6f)),q8(t<FULL+.4f?1:.75f));
    }
  }
  // Grit glows as it sifts out of the cracks.
  for(unsigned i=0;i<20;++i) {
    const float age=t-FULL-2.4f*rnd(i,593),life=.7f+.5f*rnd(i,594);if(age<0 || age>=life)continue;
    const float a=2*HALF_TURN*rnd(i,595),d=12+45*rnd(i,596),v=age/life;
    dot(px+d*cosine(a)+(rnd(i,597)-.5f)*8*age,py+4+d*sine(a)+20*age*age,mixed(hot,red,v),.9f*(1-v));
  }
}
}
