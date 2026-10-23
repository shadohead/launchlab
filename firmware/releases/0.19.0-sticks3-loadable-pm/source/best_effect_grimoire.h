#pragma once
// The grimoire set of 0.18 (Volley..Meadow): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Same rules as
// the other sets: additive light on the background outside the UI halo, a
// pure function of elapsed time with fixed seeds, the readout exactly as
// drawn from three quarters of the duration, every frame within the cost
// model's 8 ms.
// The set's look is quiet spellcraft: thin pale lines of white, sky blue and
// soft gold, small sigils that turn, and light that arrives in one clean
// stroke. None of the five touches the readout apart from the glint.
// Cost: strokes step in fixed point and are clipped to the screen before
// the light probe, particles are dots or single pixels, and each effect keeps
// to the rows its subject needs, so its mask scans stay in a band.
namespace BestEffectUI {

// Divisions and square roots are library calls on the ESP32-S3 (microseconds
// each), so the set's per-point maths uses a table of reciprocals and a
// bit-level reciprocal square root instead.
namespace detail {
constexpr std::array<float,257> grimoireRecipTable() {
  std::array<float,257> r{};r[0]=0;for(int i=1;i<=256;++i)r[i]=1.0f/i;return r;
}
constexpr std::array<float,257> GRIMOIRE_RECIP=grimoireRecipTable();
// The Renderer's hash and rnd(), at compile time.
constexpr uint32_t grimoireHash(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
constexpr float grimoireRnd(uint32_t i,uint32_t salt){return grimoireHash(i*0x9e3779b9u^salt)*(1.0f/4294967296.0f);}
// Meadow's flowers, fixed at compile time so a frame spends nothing on
// hashes: 14 rows of 26 down to 13, back to front. Per flower: its place
// across the row (0..1 of the width), its jitter down from the row, its
// colour, whether it sheds its petals and when, and its leaf side.
struct GrimoireFlower {float across,down,shed;uint8_t row,index,colour,flags;};
constexpr int GRIMOIRE_ROWS=14,GRIMOIRE_FLOWERS=273;  // 26+25+...+13
constexpr std::array<GrimoireFlower,GRIMOIRE_FLOWERS> grimoireMeadowTable() {
  std::array<GrimoireFlower,GRIMOIRE_FLOWERS> f{};int k=0;
  for(int j=0;j<GRIMOIRE_ROWS;++j){const int n=26-j;
    for(int i=0;i<n;++i,++k){const uint32_t id=uint32_t(j*32+i);
      // flags: 1 sheds, 2 has a leaf, 4 leaf on the right, 8 sparks as it pops
      const bool sheds=grimoireRnd(id,657)<.2f;
      f[k]={(i+.1f+.8f*grimoireRnd(id,651))/n,2.2f*(grimoireRnd(id,652)-.5f),sheds?1.75f+2.0f*grimoireRnd(id,658):1e9f,
        uint8_t(j),uint8_t(i),uint8_t(grimoireHash(id*7+3)%3),uint8_t((sheds?1:0)|(id&1?2:0)|(i&2?4:0)|(id%3==0?8:0))};}}
  return f;
}
constexpr std::array<GrimoireFlower,GRIMOIRE_FLOWERS> GRIMOIRE_MEADOW=grimoireMeadowTable();
}
// 1/n for a step count; exact from the table up to 256.
BEST_EFFECT_INLINE float Renderer::grimoireRecip(int n){return n<=256?detail::GRIMOIRE_RECIP[std::max(0,n)]:1.0f/n;}
// 1/sqrt(v) for v > 0, to about 0.2%: the classic bit estimate and two
// Newton steps, no library call.
BEST_EFFECT_INLINE float Renderer::grimoireRsqrt(float v) {
  uint32_t i;std::memcpy(&i,&v,4);i=0x5f3759dfu-(i>>1);float y;std::memcpy(&y,&i,4);
  const float h=.5f*v;y*=1.5f-h*y*y;y*=1.5f-h*y*y;return y;
}

// addw for strokes that cross mask words: a point within HALO px of a word
// edge builds the neighbouring word's mask first, so the stroke is never cut
// by a gap at x = 32, 64 or 96.
BEST_EFFECT_INLINE void Renderer::grimoireAdd(int x,int y,const Ink &c,int w) {
  if(w<5 || unsigned(x)>=unsigned(w_) || unsigned(y)>unsigned(bottom_))return;
  const int m=x&31;
  if(m<HALO){if(x>=32)blocked(x-HALO-1,y);}else if(m>31-HALO && (x|31)+1<w_)blocked(x+HALO+1,y);
  addw(x,y,c,w);
}
// twinkle() without its per-arm division: a dot and arms of `arm` (1..3)
// pixels fading out, the same weights as twinkle().
inline void Renderer::grimoireTwinkle(float x,float y,const Rgb &c,float k,int arm) {
  static constexpr float FALL[4][3]={{0,0,0},{.5f,0,0},{2/3.0f,1/3.0f,0},{.75f,.5f,.25f}};
  dot(x,y,c,k);arm=std::max(0,std::min(3,arm));
  for(int d=1;d<=arm;++d){const float f=k*FALL[arm][d-1];plot(x+d+.5f,y,c,f);plot(x-d-.5f,y,c,f);plot(x,y+d+.5f,c,f);plot(x,y-d-.5f,c,f);}
}
// A one-pixel line in Q8 steps with its weight ramping k0..k1 (Q8). Without
// `last` the end point is left out, so the edges of a closed outline meet
// without lighting their shared corners twice.
inline void Renderer::grimoireLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1,bool last) {
  const float dx=x1-x0,dy=y1-y0;const int n=std::max(1,int(std::max(std::fabs(dx),std::fabs(dy))));
  const float inv=grimoireRecip(n);
  int x8=q8(x0)+128,y8=q8(y0)+128,wq=k0<<8;const int sx=q8(dx*inv),sy=q8(dy*inv),dw=int((k1-k0)*256*inv);
  int px=-9999,py=-9999;
  for(int i=0;i<=n-!last;++i,x8+=sx,y8+=sy,wq+=dw) {
    const int x=x8>>8,y=y8>>8;if(x==px && y==py)continue;px=x;py=y;
    grimoireAdd(x,y,c,wq>>8);
  }
}
// An arc of an axis-aligned ellipse from angle `from` over `sweep` radians,
// about one point per pixel, by incremental rotation (no table per point).
inline void Renderer::grimoireArc(float cx,float cy,float rx,float ry,float from,float sweep,const Ink &c,int kq) {
  if(kq<5 || rx<.5f || sweep<=0)return;
  const int n=std::max(4,int((rx+ry)*3.2f*sweep*(1/(2*HALF_TURN))));const float da=sweep*grimoireRecip(n),cd=cosine(da),sd=sine(da);
  float co=cosine(from),si=sine(from);int px=-9999,py=-9999;
  for(int i=0;i<=n;++i) {
    const int x=roundi(cx+rx*co),y=roundi(cy+ry*si);
    if(x!=px || y!=py){grimoireAdd(x,y,c,kq);px=x;py=y;}
    const float nc=co*cd-si*sd;si=si*cd+co*sd;co=nc;
  }
}
// A magic circle of radius r: an outer rim that draws itself round as it
// opens (open 0..1), a dotted inner ring, six rune ticks between the two
// turning one way and a triangle inside turning the other.
inline void Renderer::grimoireSigil(float cx,float cy,float r,float spin,float open,const Ink &rim,const Ink &rune,int kq) {
  if(open<=.02f || kq<5)return;
  const float rr=r*(.35f+.65f*open);
  grimoireArc(cx,cy,rr,rr,spin,2*HALF_TURN*std::min(1.0f,open*1.25f),rim,kq);
  if(open<.3f)return;
  const int kr=(kq*int(256*smooth((open-.3f)/.5f)))>>8;
  // The inner ring, every other point, so it reads as a second, finer ring.
  {
    const float ri=rr-2.6f;const int n=std::max(8,int(ri*6.3f));const float da=2*HALF_TURN*grimoireRecip(n),cd=cosine(da),sd=sine(da);
    float co=cosine(-spin*.5f),si=sine(-spin*.5f);
    for(int i=0;i<n;++i){if(!(i&1))grimoireAdd(roundi(cx+ri*co),roundi(cy+ri*si),rim,kr*3/4);const float nc=co*cd-si*sd;si=si*cd+co*sd;co=nc;}
  }
  for(int k=0;k<6;++k) {
    const float a=-spin*.7f+k*(HALF_TURN/3);
    grimoireAdd(roundi(cx+cosine(a)*(rr-1.3f)),roundi(cy+sine(a)*(rr-1.3f)),rune,kr);
  }
  const float rt=rr-3.8f;
  if(rt>1.5f) {
    float xs[3],ys[3];
    for(int k=0;k<3;++k){const float a=spin*1.4f+k*(2*HALF_TURN/3);xs[k]=cx+rt*cosine(a);ys[k]=cy+rt*sine(a);}
    for(int k=0;k<3;++k)grimoireLine(xs[k],ys[k],xs[(k+1)%3],ys[(k+1)%3],rune,kr*7/8,kr*7/8,false);
  }
}
// A straight beam from (x0,y0) toward (x1,y1), lit between the fractions
// tail..head of its length: a one-pixel core and `rows`-1 softer rows beside
// it (rows 3: one either side; 2: one, below a shallow beam or right of a
// steep one). Points off the screen cost no probe.
inline void Renderer::grimoireBeam(float x0,float y0,float x1,float y1,float head,float tail,const Ink &core,const Ink &side,int kq,int rows) {
  if(head<=tail || kq<5)return;
  const float dx=x1-x0,dy=y1-y0;const int n=std::max(1,int(std::max(std::fabs(dx),std::fabs(dy))));
  const int i0=int(tail*n),i1=int(head*n);const bool steep=std::fabs(dy)>std::fabs(dx);
  const float inv=grimoireRecip(n);const int sx=q8(dx*inv),sy=q8(dy*inv),ox=steep?1:0,oy=steep?0:1,ks=kq*10/16;
  int x8=q8(x0)+128+sx*i0,y8=q8(y0)+128+sy*i0;
  for(int i=i0;i<=i1;++i,x8+=sx,y8+=sy) {
    const int x=x8>>8,y=y8>>8;
    // The head is a bright tip; the last few points before it taper in.
    const int tip=i1-i<3?kq+((256-kq)*(3-(i1-i))>>2):kq;
    grimoireAdd(x,y,core,tip);
    if(rows>=2)grimoireAdd(x+ox,y+oy,side,ks);
    if(rows>=3)grimoireAdd(x-ox,y-oy,side,ks);
  }
}

inline void Renderer::grimoire(unsigned which,float t) {
  switch(which) {
    case 0:grimoireVolley(t);break;
    case 1:grimoireStarfall(t);break;
    case 2:grimoireMimic(t);break;
    case 3:grimoireScales(t);break;
    case 4:grimoireMeadow(t);break;
    default:break;
  }
}

// ---- Volley: four magic circles open where the screen is clear: two in the
// open corners just under the readout and two below the replay's lower
// corners. Each fires a bold piercing beam with a flare at its muzzle: the
// upper two straight across the clear rows under the readout, so their beams
// cross, the lower two straight up the open strips beside the replay. Then all four fire
// together, the upper beams crossing in a star. Sustain: the circles keep
// turning, motes orbiting them, and fire single shots in turn about four
// times a second. Then each winds shut into a point.
inline void Renderer::grimoireVolley(float t) {
  constexpr int N=4;
  constexpr float FINAL=1.35f,R=9.5f;
  const Box &b=number_;
  const Rgb rimC=mixed(rgb8(196,224,255),accent_,.3f),runeC=rgb8(232,244,255),sideC=mixed(rgb8(140,200,255),accent_,.3f),moteC=rgb8(190,225,255);
  const Ink rim=inkOf(rimC),rune=inkOf(runeC),side=inkOf(sideC),core=WHITE_INK;
  // 0 and 1: the open corners under the readout; 2 and 3: below the
  // replay's lower corners, under the strips beside it.
  const float cx[N]={b.x0+23.0f,b.x1-23.0f,b.x0+11.0f,b.x1-12.0f},cy[N]={b.y1+12.0f,b.y1+12.0f,b.y1+110.0f,b.y1+110.0f};
  // Each circle's line of fire: the upper two across the clear rows (rising
  // a little, so they cross above the replay's top corner), the lower two
  // straight up their strips.
  auto aim=[&](int i,float lift,float &x0,float &y0,float &x1,float &y1) {
    if(i<2){const float dir=i?-1:1;x1=i?-2.0f:w_+1.0f;y1=cy[i]-6+lift;const float dx=x1-cx[i],dy=y1-cy[i],l=std::sqrt(dx*dx+dy*dy);x0=cx[i]+dx/l*R;y0=cy[i]+dy/l*R;(void)dir;}
    else{x0=cx[i];y0=cy[i]-R;x1=cx[i];y1=b.y1+4.0f;}
  };
  glint(t,.58f,.42f);
  // Shots: one per circle in turn, then all four, then single sustain shots.
  struct Shot {int from;float at,lift,k;int rows;};
  Shot shots[2*N+8];int ns=0;
  static constexpr float FIRST[N]={.55f,.7f,.95f,1.07f};
  for(int i=0;i<N;++i)shots[ns++]={i,FIRST[i],0,1,3};
  for(int i=0;i<N;++i)shots[ns++]={i,FINAL+(i<2?0:i==2?.2f:.32f),0,1,3};
  static constexpr int ORDER[8]={0,3,1,2,1,2,0,3};
  for(int j=0;j<8;++j)shots[ns++]={ORDER[j],1.85f+.24f*j,ORDER[j]<2?6*(rnd(j,601)-.5f):0,.8f,2};
  float fire[N]={};
  for(int s=0;s<ns;++s) {
    const Shot &sh=shots[s];const float a=t-sh.at;const int i=sh.from;
    if(a>-.15f && a<.2f)fire[i]=std::max(fire[i],a<0?1+a/.15f:1-a/.2f);
    if(a<0 || a>=.45f)continue;
    float x0,y0,x1,y1;aim(i,sh.lift,x0,y0,x1,y1);
    // The head crosses in 60 ms, holds, then the tail runs out after it.
    const float head=easeOut(a/.06f),tail=smooth((a-.12f)/.2f);
    const float k=sh.k*(a<.12f?1:.95f-.35f*smooth((a-.12f)/.2f));
    grimoireBeam(x0,y0,x1,y1,head,tail,core,side,q8(k),a<.2f?sh.rows:1);
    // The muzzle flare: a hot spark and four short rays.
    if(a<.14f) {
      const float m=1-a/.14f;spark(x0,y0,mixed(runeC,sideC,.3f),m*sh.k*1.2f,sh.rows==3?2.6f:2);
      for(int r=0;r<4;++r){const float an=r*(HALF_TURN/2)+HALF_TURN/4,l=3+5*(1-m);grimoireLine(x0+cosine(an)*2,y0+sine(an)*2,x0+cosine(an)*l,y0+sine(an)*l,rune,q8(m*sh.k),0);}
    }
  }
  // The final volley's upper beams cross in a star.
  {
    const float a=t-FINAL-.04f;
    if(a>=0 && a<.4f) {
      float ax0,ay0,ax1,ay1,bx0,by0,bx1,by1;aim(0,0,ax0,ay0,ax1,ay1);aim(1,0,bx0,by0,bx1,by1);
      // Where the two lines meet.
      const float d1=(ay1-ay0)/(ax1-ax0),d2=(by1-by0)/(bx1-bx0),xc=(by0-ay0+d1*ax0-d2*bx0)/(d1-d2),yc=ay0+d1*(xc-ax0);
      const float u=a/.4f,k=(1-u)*(1-u);
      spark(xc,yc,mixed(runeC,accent_,.2f),1.3f*k,2.4f);
      const int r=int(3+6*easeOut(u));grimoireLine(xc,yc-3,xc,yc-r,side,q8(k*.8f),0);grimoireLine(xc,yc+3,xc,yc+r,side,q8(k*.8f),0);
    }
  }
  // The circles open one by one, turn, breathe, and wind shut at the end;
  // in the sustain two motes orbit each.
  for(int i=0;i<N;++i) {
    const float at=.05f+.1f*i,shut=3.85f+.15f*i;
    const float open=easeOut((t-at)/.3f)*(1-smooth((t-shut)/.32f));
    if(open<=.02f){const float g=t-shut-.3f;if(g>0 && g<.2f)grimoireTwinkle(cx[i],cy[i],runeC,.9f*(1-g/.2f),1);continue;}
    const float dir=i&1?-1:1,spin=dir*(1.1f*t+3.5f*easeOut((t-at)/.7f))+i;
    const float breathe=t>1.6f?.85f+.12f*sine(t*2.6f+i*1.7f):1;
    const int kq=q8(std::min(1.0f,(.78f+.4f*fire[i])*breathe));
    grimoireSigil(cx[i],cy[i],R,spin,open,mixedInk(rim,WHITE_INK,q8(.6f*fire[i])),rune,kq);
    if(fire[i]>0)dot(cx[i]-.5f,cy[i]-.5f,runeC,fire[i]);
    if(t>1.6f)for(int m=0;m<2;++m){const float an=t*(2.4f+.4f*i)*dir+m*HALF_TURN+i,rr=R+2.5f;dot(cx[i]+rr*cosine(an)-.5f,cy[i]+rr*sine(an)*.9f-.5f,moteC,.75f*open*smooth((t-1.6f)/.3f));}
  }
}

// ---- Starfall: dozens of small lights kindle one by one in three flat
// rings under the readout, a sky seen from below, the rings turning against
// each other. They brighten together, then every light falls at once as a
// thin accelerating streak onto a dome-shaped shield over the replay, the
// nearest striking first: each bursts where it hits and the dome's arc
// flashes under the barrage. Sustain: the rings rekindle and keep turning,
// a single light falling now and then to burst on the dome and light up
// the stretch of it round the hit, motes rising off it. Then the lights go out one by one.
inline void Renderer::grimoireStarfall(float t) {
  constexpr int RINGS=3,LIGHTS=42,FALLS=8,CELLS=9;
  constexpr float FIRE=1.1f,FIRST_HIT=FIRE+.16f,DX=50,DY=38;
  static constexpr int COUNT[RINGS]={8,14,20},FIRST[RINGS]={0,8,22};
  static constexpr float RX[RINGS]={24,44,64},RY[RINGS]={5,9,13},STEP[RINGS]={2*HALF_TURN/8,2*HALF_TURN/14,2*HALF_TURN/20};
  const Box &b=number_;
  const float sx=b.cx(),sy=b.y1+17.0f,fx=b.cx(),fy=b.y1+67.0f;
  const Rgb warm=rgb8(255,244,214),halo=mixed(rgb8(186,170,255),accent_,.25f),trail=mixed(rgb8(200,190,255),accent_,.2f),shield=mixed(rgb8(170,205,255),accent_,.35f);
  const Ink trailInk=inkOf(trail);
  // The glint follows the barrage rather than riding on it, so the header's
  // sheen and the bursts never share a frame.
  glint(t,FIRST_HIT+.3f,.42f);
  // A light's place on its ring: the rings turn against each other, and
  // the near (lower) half of each ring is brighter.
  auto place=[&](int i,float at,float &x,float &y)->float {
    const int r=i<8?0:i<22?1:2,j=i-FIRST[r];
    const float a=STEP[r]*(j+.5f*(r&1))+(r==1?-1:1)*.22f*at+.4f*r,sa=sine(a);
    x=sx+RX[r]*cosine(a);y=sy+RY[r]*sa;return .72f+.28f*sa;
  };
  // Where a light falling toward the replay meets the dome, and how long it
  // takes: the same speed for every light, so the nearest strike first.
  // Returns the time; `rate` is its reciprocal (no division or root call).
  auto hit=[&](float x0,float y0,float &hx,float &hy,float &rate)->float {
    const float ex=(x0-fx)*(1/DX),ey=(y0-fy)*(1/DY),inv=grimoireRsqrt(ex*ex+ey*ey);
    hx=fx+(x0-fx)*inv;hy=fy+(y0-fy)*inv;
    const float dx=hx-x0,dy=hy-y0,s=dx*dx+dy*dy+1e-6f,dur=.1f+.004f*s*grimoireRsqrt(s),rr=grimoireRsqrt(dur);
    rate=rr*rr;return dur;
  };
  // A streak from (x0,y0) to (hx,hy), u 0..1, accelerating.
  auto streak=[&](float x0,float y0,float hx,float hy,float u,float k) {
    const float e=u*u,te=std::max(0.0f,e-.35f);
    grimoireLine(x0+(hx-x0)*te,y0+(hy-y0)*te,x0+(hx-x0)*e,y0+(hy-y0)*e,trailInk,0,q8(k*.8f));
    dot(x0+(hx-x0)*e-.5f,y0+(hy-y0)*e-.5f,warm,k);
  };
  // A burst where a light strikes: a flash and, for some, a small ring.
  auto burst=[&](float x,float y,float a,float k,bool ring) {
    if(a<0 || a>=.32f)return;
    const float u=a*(1/.32f),kk=k*(1-u)*(1-u);
   // Arms while it is bright, then a fading point.
    if(u<.5f)grimoireTwinkle(x,y,mixed(WHITE,warm,u),kk*1.4f,u<.2f?2:1);else dot(x,y,mixed(WHITE,warm,u),kk*1.4f);
    if(ring)grimoireArc(x,y,2+7*easeOut(u),2+5.5f*easeOut(u),0,2*HALF_TURN,inkOf(mixed(warm,halo,u)),q8(kk*.8f));
  };
  // Stretches of the dome flash when struck; stretch c sits on the upper arc.
  float cellHit[CELLS];for(int c=0;c<CELLS;++c)cellHit[c]=-9;
  // Stretch c spans angles -pi+c*pi/CELLS .. on the upper arc, left to
  // right, so it is found by comparing x with the stretch edges.
  auto cellOf=[&](float hx,float)->int {
    int c=0;while(c<CELLS-1 && hx>fx+DX*cosine(-HALF_TURN+(c+1)*(HALF_TURN/CELLS)))++c;
    return c;
  };
  // Shooting stars of the sustain: each takes one light and drops it.
  float fallAt[LIGHTS];for(int i=0;i<LIGHTS;++i)fallAt[i]=-9;
  for(int j=0;j<FALLS;++j)fallAt[(j*17+5)%LIGHTS]=1.95f+.27f*j;
  for(int i=0;i<LIGHTS;++i) {
    const int r=i<8?0:i<22?1:2,j=i-FIRST[r];
    float x,y;
    // The barrage: every light falls at once and strikes the dome.
    {
      float x0,y0,hx,hy,rate;const float near=place(i,FIRE,x0,y0),dur=hit(x0,y0,hx,hy,rate),a=t-FIRE;
      if(a>=0 && a<dur)streak(x0,y0,hx,hy,a*rate,std::min(1.0f,near*1.2f));
      if(a>=dur)burst(hx,hy,a-dur,near,i%4==0);
    }
    if(t<FIRE+.1f) {
      // Kindling: inner ring first, each ring swept round; a small flash at birth.
      const float born=.05f+.22f*r+.28f*(1/(2*HALF_TURN))*STEP[r]*j,age=t-born;if(age<0)continue;
      const float near=place(i,std::min(t,FIRE),x,y);
      const float charge=smooth((t-.8f)*(1/.28f)),flash=age<.15f?1-age*(1/.15f):0;
      float k=near*(.7f*smooth(age*(1/.06f))+.5f*flash+.45f*charge*(.85f+.15f*sine(t*60+i)));
      if(t>FIRE)k*=1-(t-FIRE)*10;
      if(flash>0 || charge>.5f)grimoireTwinkle(x,y,mixed(warm,halo,.4f),k,charge>.5f?2:1);else dot(x,y,warm,k);
      continue;
    }
    // The sky rekindles and goes out one by one at the end.
    const float re=1.45f+.9f*rnd(i,611),out=3.85f+.8f*rnd(i,612);
    const float near=place(i,t,x,y);
    float k=1.05f*near*smooth((t-re)*(1/.3f))*(1-smooth((t-out)*4))*(.8f+.2f*sine(t*(3+2*rnd(i,613))+i));
    const float fa=t-fallAt[i];
    if(fa>=0) {
      float x0,y0,hx,hy,rate;place(i,fallAt[i],x0,y0);const float dur=hit(x0,y0,hx,hy,rate)+.08f,rr=grimoireRsqrt(dur);
      if(fa<dur){streak(x0,y0,hx,hy,fa*rr*rr,1);k=0;}
      else{burst(hx,hy,fa-dur,.9f,true);const int c=cellOf(hx,hy);cellHit[c]=std::max(cellHit[c],fallAt[i]+dur);k*=smooth((fa-dur-.5f)*(1/.4f));}
    }
    if(k>.02f){if(rnd(i*31+uint32_t(t*6),614)<.15f)grimoireTwinkle(x,y,mixed(warm,halo,.5f),k*1.5f,1);else dot(x,y,warm,k);}
  }
  // The dome: its upper arc flashes under the barrage and fades; in the
  // sustain a struck stretch of it lights up round each hit and cools.
  {
    const float u=(t-FIRST_HIT)/.7f;
    if(u>0 && u<1)grimoireArc(fx,fy,DX,DY,HALF_TURN*1.15f,HALF_TURN*.7f,inkOf(mixed(WHITE,shield,smooth(u*2))),q8(.9f*(1-u)*(1-u)));
    for(int c=0;c<CELLS;++c) {
      const float a=t-cellHit[c];if(a<0 || a>=.6f || cellHit[c]<FIRST_HIT+.5f)continue;
      const float an=-HALF_TURN+(c+.5f)*(HALF_TURN/CELLS),k=1-a*(1/.6f);
      grimoireArc(fx,fy,DX,DY,an-.32f,.64f,inkOf(mixed(WHITE,shield,a*1.6f)),q8(k*k));
    }
  }
  // Motes of the spent light rise off the dome through the sustain.
  const float endFade=1-smooth((t-4.1f)/.6f);
  for(int i=0;i<16;++i) {
    const float age=t-(FIRST_HIT+.2f+2.4f*rnd(i,615)),life=1.2f+.6f*rnd(i,616);if(age<0 || age>=life)continue;
    const float rl=grimoireRsqrt(life),u=age*rl*rl,an=-HALF_TURN*(.1f+.8f*rnd(i,617)),x=fx+DX*cosine(an)+3*sine(age*3+i),y=fy+DY*sine(an)-20*age;
    plot(x,y,i&1?warm:halo,endFade*sine(HALF_TURN*u)*1.1f);
  }
}

// ---- Mimic: a large treasure chest drawn in outline sits just under the
// readout, over the top of the replay. It rocks twice, its lid creaks open and golden
// light pours out with sparkles; then it slams shut, bares two rows of teeth
// and chomps, tongue out. Sustain: it hops about in place, mouth open in
// the air and snapping shut as it lands, spitting a coin each time that
// arcs out to one side and tumbles down the screen. Then it
// lands, gives a last lazy yawn and fades. No face: only the lid, teeth and tongue.
inline void Renderer::grimoireMimic(float t) {
  constexpr float OPEN=.6f,SNAP=1.3f,HOP0=1.88f,HOP=.62f,HOPS=3,END=4.85f;
  constexpr float W=26,H=17,D=10,LID=7;
  const Box &b=number_;
  // The body's rim sits just below the replay's top corner, so the lid lives
  // in the clear rows under the readout.
  const float cx=b.cx()-5,floor=b.y1+36.0f;
  const Rgb wood=rgb8(232,158,78),gold=rgb8(255,214,96),light=rgb8(255,236,170),tooth=rgb8(250,250,240),tongueC=rgb8(255,118,150);
  const float fade=smooth(t/.25f)*(1-smooth((t-4.4f)/(END-4.4f)));
  glint(t,SNAP,.42f);
  if(fade<=0)return;
  // ---- the pose at time t: lid angle, squash, tilt, hop.
  float lid=0,squash=1,tilt=0,hopX=0,hopY=0,bite=0;
  // Two rocks before it opens, pivoting on one bottom corner.
  for(int r=0;r<2;++r){const float u=(t-.2f-.17f*r)/.17f;if(u>0 && u<1)tilt=(r?-.13f:.13f)*sine(HALF_TURN*u);}
  // The lid creaks open, catching halfway, then is held wide.
  if(t>=OPEN && t<SNAP-.06f)lid=1.5f*(.45f*smooth((t-OPEN)/.12f)+.55f*smooth((t-OPEN-.17f)/.2f));
  else if(t>=SNAP-.06f && t<SNAP)lid=1.5f*(1-(t-SNAP+.06f)/.06f)*(1-(t-SNAP+.06f)/.06f);
  // The slam: squashed flat, springing back.
  if(t>=SNAP && t<SNAP+.25f){const float u=(t-SNAP)/.25f;squash=1-.16f*(1-u)*cosine(u*HALF_TURN*2.5f);}
  // Two chomps with teeth bared.
  for(int c=0;c<2;++c){const float u=(t-SNAP-.2f-.24f*c)/.22f;if(u>0 && u<1){const float o=sine(HALF_TURN*u);lid=std::max(lid,.62f*o);bite=std::max(bite,smooth(u*4));}}
  // Hops: crouch, up with the mouth open, land snapping shut.
  float lastLanding=-9;int hopIndex=-1;
  for(int h=0;h<HOPS;++h) {
    const float at=HOP0+HOP*h,u=(t-at)/HOP;const float side=h==1?-1:1,from=h==0?0:(h==1?4:-4),to=h==1?-4:(h==2?0:4);
    if(t>=at+HOP)lastLanding=at+HOP,hopX=to;
    if(u<0 || u>=1)continue;
    hopIndex=h;
    if(u<.18f){squash=1-.12f*sine(HALF_TURN*u/.18f);hopX=from;}
    else if(u<.78f){const float a=(u-.18f)/.6f;hopY=4*sine(HALF_TURN*a);hopX=from+(to-from)*smooth(a);squash=1+.08f*sine(HALF_TURN*a);
      lid=std::max(lid,.55f*sine(HALF_TURN*std::min(1.0f,a*1.25f)));bite=1;tilt=-.06f*side*sine(2*HALF_TURN*a);}
    else{const float a=(u-.78f)/.22f;squash=1-.14f*sine(HALF_TURN*a);hopX=to;bite=1;}
  }
  if(t>=HOP0+HOP*HOPS && t<4.3f)bite=1-smooth((t-HOP0-HOP*HOPS-.3f)/.3f);
  // A last lazy yawn before it fades.
  {const float u=(t-3.98f)/.45f;if(u>0 && u<1)lid=std::max(lid,.32f*sine(HALF_TURN*u));}
  // ---- projection: oblique depth, squash about the base, tilt about the
  // corner it rocks on, then the hop.
  const float sx=1/std::sqrt(squash),pivot=tilt>0?W:-W,ct=cosine(-tilt),st=sine(-tilt);
  auto P=[&](float x,float y,float z,float &X,float &Y) {
    float px=(x+.45f*z)*sx,py=(y+.45f*z)*squash;
    const float qx=px-pivot*sx;px=pivot*sx+qx*ct-py*st;py=qx*st+py*ct;
    X=cx+hopX+px;Y=floor-hopY-py;
  };
  auto edge=[&](float x0,float y0,float z0,float x1,float y1,float z1,const Ink &c,int k) {
    float X0,Y0,X1,Y1;P(x0,y0,z0,X0,Y0);P(x1,y1,z1,X1,Y1);grimoireLine(X0,Y0,X1,Y1,c,k,k,false);
  };
  const Ink woodInk=inkOf(wood),goldInk=inkOf(gold),toothInk=inkOf(tooth);
  const int kw=q8(.95f*fade),kg=q8(1.0f*fade);
  // The body: front face, right side, metal bands and the lock plate.
  edge(-W,0,0,W,0,0,woodInk,kw);edge(-W,0,0,-W,H,0,woodInk,kw);edge(W,0,0,W,H,0,woodInk,kw);edge(-W,H,0,W,H,0,woodInk,kw);
  edge(W,0,0,W,0,D,woodInk,kw);edge(W,0,D,W,H,D,woodInk,kw);edge(W,H,0,W,H,D,woodInk,kw);
  for(int s2=-1;s2<=1;s2+=2){edge(13.0f*s2,0,0,13.0f*s2,H,0,goldInk,kg*3/4);edge(15.0f*s2,0,0,15.0f*s2,H,0,goldInk,kg*3/4);}
  edge(-3,H-7,0,3,H-7,0,goldInk,kg);edge(-3,H-7,0,-3,H,0,goldInk,kg);edge(3,H-7,0,3,H,0,goldInk,kg);edge(0,H-5,0,0,H-3,0,goldInk,kg);
  // The open body's back rim, seen when the lid is up.
  if(lid>.25f){const int k=(kw*int(256*smooth((lid-.25f)/.3f)))>>8;edge(-W,H,0,-W,H,D,woodInk,k);edge(-W,H,D,W,H,D,woodInk,k);}
  // The lid: an arched top hinged on the body's back edge, rotated open by `lid`.
  const float cl=cosine(lid),sl=sine(lid);
  auto lidPoint=[&](float x,float a,float &X,float &Y) {
    const float z=D*.5f-D*.5f*cosine(a),y=H+LID*sine(a)*(a<HALF_TURN*.5f?1:1),dz=z-D,dy=y-H;
    P(x,H-dz*sl+dy*cl,D+dz*cl+dy*sl,X,Y);
  };
  for(int s2=-1;s2<=1;++s2) {
    // Side profiles at both ends and the gold band in the middle of each half.
    const float x=s2==0?0:W*s2;if(s2==0)continue;
    for(int band=0;band<2;++band) {
      const float bx=band?14.0f*s2:x;const Ink &c=band?goldInk:woodInk;const int k=band?kg*3/4:kw;
      float X0,Y0;lidPoint(bx,0,X0,Y0);
      for(int i=1;i<=8;++i){float X1,Y1;lidPoint(bx,i*(HALF_TURN/8),X1,Y1);grimoireLine(X0,Y0,X1,Y1,c,k,k,i==8);X0=X1;Y0=Y1;}
    }
  }
  {float X0,Y0,X1,Y1;
    lidPoint(-W,0,X0,Y0);lidPoint(W,0,X1,Y1);grimoireLine(X0,Y0,X1,Y1,woodInk,kw,kw);
    lidPoint(-W,HALF_TURN*.5f,X0,Y0);lidPoint(W,HALF_TURN*.5f,X1,Y1);grimoireLine(X0,Y0,X1,Y1,woodInk,kw*3/4,kw*3/4);}
  // Teeth: a zigzag along the body's front rim pointing up, and one along the
  // lid's lip pointing down, shown while it bites.
  if(bite>.05f && lid>.06f) {
    const int k=q8(bite*fade);
    // Solid teeth at the chomps: each a triangle filled with three strokes
    // from its tip, so a tooth still reads where the replay's lines cut
    // through it; in mid-hop, moving, two strokes are enough.
    const bool solid=t<HOP0;
    auto teeth=[&](bool lidRow) {
      for(int i=0;i<8;++i) {
        const float x0=-W+1.5f+i*6.25f,x1=x0+5.5f,xm=(x0+x1)*.5f;float BX0,BY0,BX1,BY1,TX,TY,MX,MY;
        if(lidRow) {
          // Lid teeth hang from the lip: down in the lid's own frame.
          auto lip=[&](float x,float down,float &X,float &Y){const float dz=-D,dy=-down;P(x,H-dz*sl+dy*cl,D+dz*cl+dy*sl,X,Y);};
          lip(x0,0,BX0,BY0);lip(x1,0,BX1,BY1);lip(xm,4.5f,TX,TY);lip(xm,0,MX,MY);
        } else {P(x0,H,0,BX0,BY0);P(x1,H,0,BX1,BY1);P(xm,H+4.5f,0,TX,TY);P(xm,H,0,MX,MY);}
        grimoireLine(TX,TY,BX0,BY0,toothInk,k,k,false);grimoireLine(TX,TY,BX1,BY1,toothInk,k,k,false);if(solid)grimoireLine(TX,TY,MX,MY,toothInk,k*3/4,k*3/4,false);
      }
    };
    teeth(false);teeth(true);
    // The tongue lolls out over the front rim.
    if(lid>.25f) {
      const float lo=smooth((lid-.25f)/.25f);float px0,py0;P(-5,H,3,px0,py0);
      const Ink tg=inkOf(tongueC);const int kt=q8(fade*lo);
      float Xa,Ya,Xb,Yb,Xc,Yc;P(-5,H-2-5*lo,-2.5f*lo,Xa,Ya);P(5,H-2-5*lo,-2.5f*lo,Xb,Yb);P(5,H,3,Xc,Yc);
      grimoireLine(px0,py0,Xa,Ya,tg,kt,kt);grimoireLine(Xa,Ya,Xb,Yb,tg,kt,kt);grimoireLine(Xb,Yb,Xc,Yc,tg,kt,kt);
      float Xm,Ym;P(0,H-1-4*lo,-1.5f*lo,Xm,Ym);grimoireLine(Xm,Ym-1,Xm,Ym-2.5f*lo,tg,kt*2/3,kt/3);
    }
  }
  // Golden light pours out of the open chest: rays fanning up and sparkles.
  const float glow=t>=OPEN && t<SNAP?smooth((t-OPEN-.1f)/.25f):0;
  if(glow>0) {
    const Ink rayInk=inkOf(light);
    for(int r=0;r<7;++r) {
      const float x=-18+6*r,len=(14+9*sine(r*1.9f+t*7))*glow,lean=(r-3)*.35f;
      float X0,Y0;P(x,H+.5f,D*.5f,X0,Y0);
      grimoireLine(X0,Y0,X0+lean*len*.4f,Y0-len,rayInk,q8(.8f*glow*fade),0);
    }
  }
  // Sparkles: rising from the open chest, and thrown out round it at the slam.
  for(int i=0;i<14;++i) {
    const float born=OPEN+.15f+.5f*rnd(i,621),age=t-born,life=.55f+.3f*rnd(i,622);if(age<0 || age>=life)continue;
    float X,Y;P(-18+36*rnd(i,623),H+2,D*.5f,X,Y);const float u=age/life;
   grimoireTwinkle(X+6*(rnd(i,624)-.5f)*age*3,Y-26*age,i%3?gold:mixed(WHITE,accent_,.5f),fade*sine(HALF_TURN*u)*1.2f,1);
  }
  {
    const float a=t-SNAP;
    if(a>=0 && a<.2f) {
      // Snap lines: short strokes bursting out round the shut chest.
      float X,Y;P(0,H*.7f,D*.5f,X,Y);const float u=a/.2f;const int k=q8(fade*(1-u));
      for(int r=0;r<7;++r){const float an=-HALF_TURN*(.05f+.9f*r/6),ca=cosine(an),sa=sine(an),r0=31+9*u;
        grimoireLine(X+ca*r0,Y+sa*r0*.62f,X+ca*(r0+7),Y+sa*(r0+7)*.62f,toothInk,k,k/3);}
    }
  }
  // Landings kick up dust.
  for(int h=0;h<HOPS;++h) {
    const float land=HOP0+HOP*h+HOP*.78f,a=t-land;if(a<0 || a>=.5f)continue;
    const float u=a/.5f,to=h==1?-4:(h==2?0:4);
    for(int s2=-1;s2<=1;s2+=2)plot(cx+to+s2*(W+4+14*easeOut(u)),floor-1-2*sine(HALF_TURN*u),mixed(wood,light,.4f),fade*(1-u)*.9f);
  }
  // Coins: spat out to alternate sides at the slam and at each landing,
  // spinning as they arc out and tumble down the screen.
  for(int c=0;c<4;++c) {
    const float at=c==0?SNAP+.04f:HOP0+HOP*(c-1)+HOP*.8f,a=t-at,life=1.25f;if(a<0 || a>=life)continue;
    const float dir=c&1?-1:1,vx=dir*(20+6*rnd(c,625)),vy=-40-12*rnd(c,626);
    const float x=std::max(3.0f,std::min(float(w_)-4,cx+dir*(W+3)+vx*a)),y=floor-H-4+vy*a+100*a*a,spin=cosine(a*(13+5*rnd(c,627))),k=fade*(1-smooth((a-life+.3f)/.3f));
    const float wdt=.3f+1.9f*std::fabs(spin);
    grimoireArc(x,y,wdt,2.1f,0,2*HALF_TURN,inkOf(gold),q8(k));plot(x-wdt*.4f,y-.8f,light,k*.9f);
  }
  (void)hopIndex;(void)lastLanding;
}

// ---- Scales: a wide golden balance draws itself in the clear rows under
// the readout, its deep pans hanging either side of the replay's top, an orb of mana gathering in each pan: violet on the left, pale
// light on the right. It weighs, swinging, and settles tipped to the left;
// then the right orb flares, swells, and the beam swings over decisively,
// its pan slamming down in a flash and spilling light. Sustain: it holds there, bobbing, the
// bright orb pulsing and drawing wisps of violet over from the beaten pan,
// which dims. Then it fades.
inline void Renderer::grimoireScales(float t) {
  constexpr float DRAW=.35f,HOLD=.95f,TURN=1.25f,END=4.9f,R=42,CHAIN=13,PAN=11,DEEP=6;
  const Box &b=number_;
  const float px=b.cx(),py=b.y1+10.0f;
  const Rgb gold=rgb8(255,206,96),pale=rgb8(255,236,170),violet=rgb8(190,110,255),bright=mixed(rgb8(200,235,255),accent_,.35f);
  const Ink goldInk=inkOf(gold),paleInk=inkOf(pale);
  const float fade=1-smooth((t-4.2f)/(END-4.2f));
  glint(t,TURN+.08f,.45f);
  if(fade<=0)return;
  // The beam's angle (positive: the right end down). It weighs with a damped
  // swing toward the left, holds, then swings over hard with an overshoot.
  float phi;
  if(t<TURN) {
    const float w=smooth((t-DRAW)/(HOLD-DRAW));
    phi=-.12f*w+(t>DRAW?.08f*std::exp(-2.6f*(t-DRAW))*sine((t-DRAW)*15):0)+(t>HOLD?.01f*sine(t*40):0);
  } else {
    const float a=t-TURN;
    phi=-.12f+.25f*(1-std::exp(-12*a)*cosine(a*14))+(a>.6f?.02f*sine((a-.6f)*3.1f)*smooth((a-.6f)/.5f):0);
  }
  const float ca=cosine(phi),sa=sine(phi);
  // The beam grows out from the pivot as it draws in.
  const float grow=easeOut(t/DRAW),reach=R*grow;
  const float lx=px-reach*ca,ly=py-reach*sa,rx=px+reach*ca,ry=py+reach*sa;
  const int kb=q8(fade);
  grimoireLine(lx,ly,rx,ry,goldInk,kb,kb);grimoireLine(lx-sa,ly+ca,rx-sa,ry+ca,inkOf(mixed(gold,pale,.2f)),kb*5/8,kb*5/8);
  // The pivot: a ring with a short finial above it.
  grimoireArc(px,py,2.5f,2.5f,0,2*HALF_TURN,paleInk,kb);
  grimoireLine(px,py-3,px,py-5,goldInk,kb,kb);dot(px-.5f,py-7,pale,fade);
  if(grow<.98f)return;
  // Chains and pans hang straight down from the beam's ends.
  const float drop=easeOut((t-DRAW+.05f)/.25f);
  float panX[2],panY[2];
  for(int side=0;side<2;++side) {
    const float ex=side?rx:lx,ey=side?ry:ly,by=ey+CHAIN*drop;panX[side]=ex;panY[side]=by;
    dot(ex-.5f,ey-.5f,pale,fade);
    grimoireLine(ex,ey+1,ex-PAN+1,by,goldInk,kb*3/4,kb*3/4);grimoireLine(ex,ey+1,ex+PAN-1,by,goldInk,kb*3/4,kb*3/4);
    grimoireLine(ex-PAN,by,ex+PAN,by,goldInk,kb,kb);
    // The bowl under the rim, one pixel per column.
    const int bx0=roundi(ex-PAN+1),bx1=roundi(ex+PAN-1);int prev=roundi(by);
    for(int x=bx0;x<=bx1;++x) {
      const float u=(x-ex)*(1/PAN);const int y=roundi(by+DEEP*drop*std::sqrt(std::max(0.0f,1-u*u)));
      // Steep runs at the bowl's ends are joined so its outline stays whole.
      for(int yy=std::min(prev,y)+1;yy<std::max(prev,y);++yy)grimoireAdd(x<ex?x:x-1,yy,goldInk,kb);
      grimoireAdd(x,y,goldInk,kb);prev=y;
    }
  }
  // The orbs: violet on the left, pale on the right, each sitting in its
  // pan. The right one is held small until the turn, then flares and swells.
  const float gather=smooth((t-DRAW)/(HOLD-DRAW));
  const float after=t>TURN?t-TURN:0;
  const float leftR=(2+3.5f*gather)*(1-.35f*smooth(after/1.6f)),rightR=(2+1.5f*gather)+4*easeOut(after/.3f)-1.2f*smooth((after-.3f)/.5f)+(after>.3f?.4f*sine(after*5):0);
  const float leftK=fade*(.75f+.25f*gather)*(1-.55f*smooth(after/1.6f)),rightK=fade*(t<TURN?.65f+.2f*gather:1);
  for(int side=0;side<2;++side) {
    const float r=side?rightR:leftR,k=side?rightK:leftK;const Rgb &c=side?bright:violet;
    if(r<.5f || k<.02f)continue;
    const float ox=panX[side],oy=panY[side]-r-.5f;
    grimoireArc(ox,oy,r,r,t*2*(side?1:-1),2*HALF_TURN,inkOf(c),q8(k));
    if(r>3.5f)grimoireArc(ox,oy,r*.5f,r*.5f,-t*3,2*HALF_TURN,inkOf(mixed(c,WHITE,.5f)),q8(k*.65f));
    dot(ox-.5f,oy-.5f,mixed(c,WHITE,.6f),k);
  }
  // Weighing: motes of mana drift down into both pans.
  if(t>DRAW && t<TURN)for(int i=0;i<16;++i) {
    const float u=(t-DRAW-.045f*i)/.4f;if(u<=0 || u>=1)continue;
    const int side=i&1;const float tx=panX[side],ty=panY[side]-3;
    const float sx2=tx+(rnd(i,631)-.5f)*36,sy2=ty-14-5*rnd(i,632),e=u*u;
    plot(sx2+(tx-sx2)*e,sy2+(ty-sy2)*e,side?bright:violet,sine(HALF_TURN*u)*fade);
  }
  // The turn: the right orb's flare, a ring rolling off it and rays as its pan slams down.
  if(after>0 && after<.5f) {
    const float u=after/.5f,k=fade*(1-u)*(1-u);const float ox=panX[1],oy=panY[1]-rightR;
    grimoireArc(ox,oy,7+16*easeOut(u),6+11*easeOut(u),0,2*HALF_TURN,inkOf(mixed(WHITE,bright,u)),q8(k));
    for(int r=0;r<8;++r){const float an=r*(HALF_TURN/4)+.2f,c2=cosine(an),s2=sine(an),r0=9+10*u;
      grimoireLine(ox+c2*r0,oy+s2*r0*.8f,ox+c2*(r0+6),oy+s2*(r0+6)*.8f,inkOf(mixed(WHITE,pale,u)),q8(k),0);}
  }
  // Sustain: wisps of violet are drawn over from the beaten pan into the bright orb.
  for(int i=0;i<12;++i) {
    const float at=TURN+.35f+.22f*i,u=(t-at)/.9f;if(u<=0 || u>=1)continue;
    const float x0=panX[0],y0=panY[0]-leftR,x1=panX[1],y1=panY[1]-rightR,e=smooth(u);
    const float x=x0+(x1-x0)*e,y=y0+(y1-y0)*e-14*sine(HALF_TURN*e)-3*sine(u*9+i);
    const Rgb c=mixed(violet,bright,e);dot(x,y,c,fade*sine(HALF_TURN*u)*1.1f);plot(x-(x1-x0)*.04f,y+1,c,fade*sine(HALF_TURN*u)*.5f);
  }
  // The slam spills light over the heavy pan's rim; it drips and falls away.
  for(int i=0;i<14;++i) {
    const float age=t-(TURN+.12f+.5f*rnd(i,636)),life=1.0f+.5f*rnd(i,637);if(age<0 || age>=life)continue;
    const float u=age/life,side=i&1?1:-1,x=panX[1]+side*(6+3*rnd(i,638))+side*(4+10*rnd(i,639))*age,y=panY[1]+2+(8+14*rnd(i,640))*age+45*age*age;
    plot(x,y,mixed(bright,pale,rnd(i,641)),fade*(1-u)*1.2f);plot(x,y-1.5f,bright,fade*(1-u)*.5f);
  }
  // Sparks shed off the winning orb.
  for(int i=0;i<10;++i) {
    const float age=t-(TURN+.4f+2.5f*rnd(i,633)),life=.6f+.4f*rnd(i,634);if(age<0 || age>=life)continue;
    const float u=age/life,an=-HALF_TURN*(.15f+.7f*rnd(i,635));
   grimoireTwinkle(panX[1]+cosine(an)*(rightR+2+10*u),panY[1]-rightR+sine(an)*(rightR+2+10*u),mixed(bright,WHITE,.4f),fade*sine(HALF_TURN*u),1);
  }
}

// ---- Meadow: a seed of light twinkles on the floor below the replay, then
// a wave of light spreads out from it across the whole floor in
// perspective, and a carpet of tiny blue and white flowers pops up behind
// it, small and dense at the back, larger at the front. When the wave
// reaches the edges a shimmer runs over the field. Sustain: a breeze moves
// across, brightening the flowers in a travelling band, and one flower in
// five loses its petals to it, each fluttering up and away. Then the field fades from the edges inward.
inline void Renderer::grimoireMeadow(float t) {
  constexpr int ROWS=14;
  constexpr float SEED=.25f,SPREAD=1.05f,FULL=SEED+SPREAD,END=4.9f,SQUASH=.36f;
  const Box &b=number_;
  const float top=b.y1+75.0f,bottom=std::min(float(bottom_),b.y1+123.0f),ox=b.cx()-14,oy=b.y1+101.0f;
  const Rgb blue=rgb8(110,170,255),white=rgb8(236,246,255),lilac=mixed(rgb8(176,160,255),accent_,.35f),heart=rgb8(255,222,96),leaf=rgb8(86,190,110),wave=rgb8(214,255,236);
  const Rgb petals[3]={blue,white,lilac};
  glint(t,FULL-.05f,.45f);
  // The wave front: an ellipse in the floor's perspective growing out from the seed.
  // Quadratic ease-out, so each flower's moment is one square root.
  constexpr float reach=118;const float su=clamp01((t-SEED)/SPREAD),front=t<SEED?0:reach*(1-(1-su)*(1-su));
  // The settle: the field fades from the edges inward.
  const float closing=t>3.9f?reach*(1-smooth((t-3.9f)/(END-.1f-3.9f))):1e9f;
  // The breeze: a band of light travelling across the field, left to right.
  const float breeze=-30+(t-1.6f)*70;
  // The seed: a point of light that gathers and twinkles before the wave.
  if(t<SEED+.2f){const float k=smooth(t/.12f)*(t<SEED?1:1-(t-SEED)/.2f);grimoireTwinkle(ox,oy,mixed(white,wave,.5f),k*(1+.3f*sine(t*50)),2);}
  // Per frame: the shimmer as the wave completes, the colours as inks, and
  // whether any flower can still be popping (after that no flower needs its
  // distance from the seed until the settle).
  const float shimmer=t>FULL-.1f && t<FULL+.5f?.5f*sine(HALF_TURN*(t-FULL+.1f)*(1/.6f)):0;
  const bool spreading=t<FULL+.2f,settling=t>3.9f;
  const float front2=front*front,closing2=settling?closing*closing:0,petalFade=t>4.3f?1-smooth((t-4.3f)*(1/.4f)):1;
  const Ink petalInk[3]={inkOf(blue),inkOf(white),inkOf(lilac)},heartInk=inkOf(heart),leafInk=inkOf(leaf);
  // Per row: its height and size class.
  float rowY[ROWS],rowSize[ROWS];
  for(int j=0;j<ROWS;++j){const float v=j*(1.0f/(ROWS-1));rowY[j]=top+(bottom-top)*v*(.55f+.45f*v);rowSize[j]=v;}
  const float width=float(w_-4),gustOn=t>1.6f?1.0f:0.0f;
  for(const detail::GrimoireFlower &fl:detail::GRIMOIRE_MEADOW) {
    const int j=fl.row,i=fl.index;const float size=rowSize[j];  // size: 0 at the back, 1 at the front
    const float x=2+width*fl.across,y=rowY[j]+fl.down;
    const float dx=x-ox,dy=(y-oy)*(1/SQUASH),d2=dx*dx+dy*dy;
    // Not reached by the wave yet, or already faded in the settle:
    // squared distances, no root.
    if(front2<d2 || (settling && d2>closing2))continue;
    // When the front passed this flower, needed only while flowers pop.
    float age=1,d=0;
    if(spreading || settling)d=d2*grimoireRsqrt(d2+1e-6f);
    if(spreading){const float q=std::max(1e-6f,1-d*(1/reach));age=t-SEED-SPREAD*(1-q*grimoireRsqrt(q));if(age<0)continue;}
    const float pop=std::min(1.0f,age*(1/.16f)),over=pop<1?1+.35f*sine(HALF_TURN*pop):1;
    const float edgeFade=settling?smooth((closing-d)*.1f):1;
    float k=(.55f+.45f*size)*edgeFade*(pop<1?smooth(pop*2):1);
    // The shimmer, then the breeze band.
    const float gust=gustOn*std::max(0.0f,1-std::fabs(x-breeze-.4f*(y-top))*(1/16.0f));
    k*=1+shimmer+.45f*gust;
    const Ink &ink=petalInk[fl.colour];
    const int ix=roundi(x+gust*.6f),iy=roundi(y);
    // Some flowers lose their petals to the breeze: the petal flutters up
    // and away, and only the gold heart is left.
    const float fly=t-fl.shed;
    if(fly>=0) {
      if(fly<1.1f) {
        const Rgb &c=petals[fl.colour];
        const float u=fly*(1/1.1f),px=x+26*fly+4*sine(fly*8+i),wob=sine(fly*11+j),py=y-3-20*fly+3*wob;
        const float pk=sine(HALF_TURN*std::min(1.0f,u*1.4f))*petalFade;
        plot(px,py,c,pk*1.4f);plot(px+(wob>0?1:0),py+(wob>0?0:1),c,pk*.7f);
      }
      if(size>=.35f)addw(ix,iy,heartInk,q8(k*.8f));
      continue;
    }
    if(size<.35f) {
      // Back rows: one pixel.
      addw(ix,iy,ink,q8(k*(1.0f+.3f*over)));
    } else {
      const int kp=q8(k*over*(size<.7f?.85f:.9f));
      if(size<.7f) {
        // Middle rows: three petals over a gold heart.
        addw(ix,iy,heartInk,q8(k*.8f));addw(ix-1,iy,ink,kp);addw(ix+1,iy,ink,kp);addw(ix,iy-1,ink,kp);
      } else {
        // Front rows: four petals round a gold heart, a leaf below.
        addw(ix,iy,heartInk,q8(k));addw(ix-1,iy,ink,kp);addw(ix+1,iy,ink,kp);addw(ix,iy-1,ink,kp);addw(ix,iy+1,ink,q8(k*over*.6f));
        if(fl.flags&2)addw(ix+(fl.flags&4?1:-1),iy+2,leafInk,q8(k*.7f));
      }
    }
    // A spark as each one pops.
    if(age<.12f && (fl.flags&8))plot(x,y-2-6*age,white,1-age*(1/.12f));
  }
  // The wave of light itself: the front's arc on the floor, with sparkle.
  if(t>=SEED && t<FULL+.1f) {
    const float u=(t-SEED)/SPREAD,k=(t<FULL?1:1-(t-FULL)/.1f)*(1-.4f*u);
    const float rx=front,ry=front*SQUASH;
    if(rx>2) {
      // Stepped by incremental rotation: the point round the ellipse and the
      // sparkle's phase (seven times as fast) each turn by a fixed step.
      const int n=std::max(16,int(rx*2.6f));const Ink c=inkOf(wave);const float step=2*HALF_TURN*grimoireRecip(n);
      const float cd=cosine(step),sd=sine(step),cs=cosine(7*step),ss=sine(7*step);
      float co=1,si=0,pc=cosine(t*30),ps=sine(t*30);
      for(int m=0;m<n;++m) {
        const float x=ox+rx*co,y=oy+ry*si;
        if(y>=top-2 && y<=bottom+2 && x>=0 && x<w_) {
          const int kq=q8(k*(.65f+.35f*ps)),ix=roundi(x),iy=roundi(y);
          grimoireAdd(ix,iy,c,kq);grimoireAdd(ix,iy+1,c,kq/2);
        }
        float nc=co*cd-si*sd;si=si*cd+co*sd;co=nc;
        nc=pc*cs-ps*ss;ps=ps*cs+pc*ss;pc=nc;
      }
    }
  }
}
}
