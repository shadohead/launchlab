#pragma once
// The ripsaw set of 0.18 (Rev..Sharkfin): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Same rules as
// the other sets: additive light on the background outside the UI halo, a
// pure function of elapsed time with fixed seeds, the readout exactly as
// drawn from three quarters of the duration, every frame within the cost
// model's 8 ms.
namespace BestEffectUI {

inline void Renderer::ripsaw(unsigned which,float t) {
  switch(which) {
    case 0:ripsawRev(t);break;
    case 1:ripsawHammer(t);break;
    case 2:ripsawFoxbite(t);break;
    case 3:ripsawDetonate(t);break;
    case 4:ripsawSharkfin(t);break;
    default:break;
  }
}

// ---- Rev: a pull cord yanks, the engine coughs twice and catches, and two
// chainsaw bars burst out of it to either side under the readout, their
// chains racing round with sparks spraying off the noses. Sustain: the
// chains keep racing as the engine revs, sparks spit in bursts and exhaust
// puffs drift up; then the chains run down and the bars retract.
// Chain travel along a bar's perimeter (px) and its speed (px/s): still
// until the engine catches, spun up in 0.2 s, revving through the sustain,
// then run down to rest. Closed form, so every frame stands alone.
inline float Renderer::ripsawChain(float t,float &speed)const {
  constexpr float CATCH=.8f,FULL=1.0f,EASE=3.7f,STOP=4.5f,V=300,REV=.25f,W=2*HALF_TURN/.85f;
  if(t<CATCH){speed=0;return 0;}
  if(t<FULL){const float a=t-CATCH;speed=V*a/(FULL-CATCH);return .5f*speed*a;}
  const float base=.5f*V*(FULL-CATCH);
  auto held=[&](float a){return V*a+V*REV/W*(1-cosine(W*a));};
  if(t<EASE){const float a=t-FULL;speed=V*(1+REV*sine(W*a));return base+held(a);}
  const float v0=V*(1+REV*sine(W*(EASE-FULL))),a=std::min(t,STOP)-EASE;
  speed=v0*(1-a/(STOP-EASE));
  return base+held(EASE-FULL)+v0*a-.5f*v0*a*a/(STOP-EASE);
}
// One level bar from its root at (rx,ry) out to the `side` (+1 right, -1
// left), len px to the start of its rounded nose: a filled steel blade
// 7 px tall with a bright outline, a nose sprocket, and the chain round it,
// links on rows 4 and cutter teeth on rows 5 out from the centre. The chain
// runs out along the top, round the nose and back along the bottom; `phase`
// is its travel in px (the teeth repeat every 5 px).
inline void Renderer::ripsawBar(float rx,float ry,int side,float len,float phase,float k,const Ink &steel,const Ink &chain) {
  if(len<2 || k<=.02f)return;
  constexpr int PERIOD=5;
  const int cy=roundi(ry),x0=roundi(rx),n=int(len),kq=q8(k);
  const int body=(kq*90)>>8,bevel=(kq*150)>>8,edge=(kq*235)>>8,link=(kq*130)>>8,cutter=kq;
  const int ph=floori(phase);
  auto tooth=[&](int s){return ((s-ph)%PERIOD+PERIOD)%PERIOD;};
  for(int i=0;i<=n;++i) {
    const int x=x0+side*i;
    for(int d=-1;d<=1;++d)addw(x,cy+d,steel,body);
    addw(x,cy-2,steel,bevel);addw(x,cy+2,steel,bevel);
    addw(x,cy-3,steel,edge);addw(x,cy+3,steel,edge);
    // Top run: s grows outward. Bottom run: s grows inward from the nose.
    const int top=tooth(i),bot=tooth(2*n+12-i);
    addw(x,cy-4,chain,top<2?cutter:link);addw(x,cy+4,chain,bot<2?cutter:link);
    if(top==0)addw(x,cy-5,chain,cutter);
    if(bot==0)addw(x,cy+5,chain,cutter);
  }
  // The nose: a filled half disc with the outline and chain carried round it.
  const float nx=rx+side*len;
  for(int d=-2;d<=2;++d){const int w=d==0?2:std::abs(d)==1?2:1;for(int j=1;j<=w;++j)addw(roundi(nx)+side*j,cy+d,steel,std::abs(d)==2?bevel:body);}
  for(int j=0;j<12;++j) {
    const float a=(j+.5f)*(HALF_TURN/12),ca=sine(a),sa=-cosine(a); // from the top round to the bottom
    addw(roundi(nx+side*3.3f*ca),roundi(ry+3.3f*sa),steel,edge);
    const int s=n+j;const int tt=tooth(s);
    addw(roundi(nx+side*4.4f*ca),roundi(ry+4.4f*sa),chain,tt<2?cutter:link);
    if(tt==0)addw(roundi(nx+side*5.5f*ca),roundi(ry+5.5f*sa),chain,cutter);
  }
  // The nose sprocket turning with the chain.
  const float turn=phase/2;
  for(int j=0;j<3;++j){const float a=turn+j*(2*HALF_TURN/3);addw(roundi(nx+1.3f*cosine(a)),roundi(ry+1.3f*sine(a)),chain,(kq*3)>>2);}
}
// A puff of smoke: a ring of points that swells and thins.
inline void Renderer::ripsawPuff(float x,float y,float dx,float dy,float age,float life,float size,const Ink &c,float k) {
  const float u=age/life;if(u<0 || u>=1)return;
  const float r=size*(.35f+.65f*easeOut(u)),f=k*(1-u)*(1-u),e=easeOut(u);const int kq=q8(f);x+=dx*e;y+=dy*e;
  const int n=std::max(6,int(r*5));
  for(int i=0;i<n;++i){const float a=i*(2*HALF_TURN/n)+u;addw(roundi(x+r*cosine(a)),roundi(y+r*.8f*sine(a)),c,(i&1)?kq:kq>>1);}
}
inline void Renderer::ripsawRev(float t) {
  constexpr float YANK=.12f,PULLED=.3f,BACK=.42f,CATCH=.8f,BURST=.14f,RETRACT=4.35f,END=5.0f;
  const Box &b=number_;
  const float hx=b.cx(),hy=b.y1+8.5f;
  const Ink steel=inkOf(rgb8(196,210,226)),chain=inkOf(mixed(WHITE,accent_,.3f)),engine=inkOf(rgb8(255,124,24));
  const Rgb hot=rgb8(255,240,170),orange=rgb8(255,140,30),ember=rgb8(205,48,10),smoke=rgb8(150,150,158);
  const float body=smooth(t/.12f)*(1-smooth((t-RETRACT-.25f)/(END-RETRACT-.35f)));
  if(body<=0)return;
  glint(t,CATCH,.45f);
  float speed;const float phase=ripsawChain(t,speed);
  // The engine: a rounded block, dim until it catches, flaring on each cough.
  static constexpr float COUGHS[]={.46f,.62f};
  float flare=0;for(float c:COUGHS)if(t>=c && t<c+.12f)flare=std::max(flare,1-(t-c)/.12f);
  if(t>=CATCH)flare=std::max(flare,1-clamp01((t-CATCH)/.3f));
  const float run=t<CATCH?.7f:.95f+.1f*sine(t*21);
  {
    const int k=q8(body*std::min(1.0f,run+.6f*flare));
    for(int x=-7;x<=7;++x){addw(roundi(hx+x),roundi(hy-4),engine,k);addw(roundi(hx+x),roundi(hy+4),engine,(k*3)>>2);}
    for(int y=-3;y<=3;++y){addw(roundi(hx-9),roundi(hy+y),engine,(k*3)>>2);addw(roundi(hx+9),roundi(hy+y),engine,(k*3)>>2);}
    addw(roundi(hx-8),roundi(hy-4),engine,k>>1);addw(roundi(hx+8),roundi(hy-4),engine,k>>1);
    addw(roundi(hx-8),roundi(hy+4),engine,k>>1);addw(roundi(hx+8),roundi(hy+4),engine,k>>1);
    // A dim body with cooling fins across its face.
    for(int y=-3;y<=3;++y)for(int x=-8;x<=8;++x)addw(roundi(hx+x),roundi(hy+y),engine,(k*((x&3)==1 && y>-3 && y<3?120:36))>>8);
  }
  // The pull cord: from the block's foot down to a T handle; yanked, then it snaps back.
  if(t<CATCH+.1f) {
    const float drop=t<YANK?3:t<PULLED?3+21*easeOut((t-YANK)/(PULLED-YANK)):t<BACK?24-21*smooth((t-PULLED)/(BACK-PULLED)):3;
    const float fade=body*(1-smooth((t-CATCH+.1f)/.2f));
    const float cx0=hx+4,cy0=hy+5,cx1=cx0+(t>=YANK && t<BACK?2*sine(t*60):0),cy1=cy0+drop;
    sorcLine(cx0,cy0,cx1,cy1,steel,q8(.6f*fade),q8(.8f*fade));
    sorcLine(cx1-4,cy1,cx1+4,cy1,engine,q8(.95f*fade),q8(.95f*fade));
    sorcLine(cx1-3,cy1+1,cx1+3,cy1+1,engine,q8(.5f*fade),q8(.5f*fade));
    // A streak along the cord while it whips.
    if(t>=YANK && t<PULLED)for(int i=1;i<=3;++i)addw(roundi(cx1+(i&1?-6:6)),roundi(cy1-3*i),steel,q8(.4f*fade/i));
  }
  // Exhaust: a puff with each cough, then one every 0.55 s while it runs.
  const Ink smokeInk=inkOf(smoke);
  for(float c:COUGHS) {
    ripsawPuff(hx+12,hy+8,9,8,t-c,.55f,6,smokeInk,1.3f*body);
    // A sputter of sparks from the exhaust with each cough.
    const float a=t-c;
    if(a>=0 && a<.25f)for(unsigned i=0;i<6;++i){const float v=40+50*rnd(i+uint32_t(c*100),911),an=.2f+1.1f*rnd(i+uint32_t(c*100),912);
      dot(hx+10+v*a*cosine(an),hy+5+v*a*sine(an)+90*a*a,i&1?hot:orange,1.1f*(1-a/.25f));}
  }
  if(t>=CATCH)for(int p=0;p<7;++p){const float at=CATCH+.15f+p*.55f;const float side=(p&1)?-1:1;ripsawPuff(hx+side*7,hy+9,side*6,10,t-at,.7f,4+(p&1),smokeInk,.7f*body);}
  if(t<CATCH)return;
  // The bars burst out with an overshoot, then hum; they retract in the settle.
  const float a=t-CATCH,out=a<BURST?easeOut(a/BURST)*1.1f:a<BURST+.12f?1.1f-.1f*smooth((a-BURST)/.12f):1;
  const float len=44*out*(1-smooth((t-RETRACT)/.5f)),hum=speed>40?.45f*sine(t*97):0;
  // The chain as drawn: up to 44 px/s (2.2 px a frame), so its teeth are
  // seen racing round instead of strobing.
  const float shown=phase*(44.0f/300);
  for(int side=-1;side<=1;side+=2) {
    const float rx=hx+side*11,ry=hy+hum*side;
    ripsawBar(rx,ry,side,len,shown,body,steel,chain);
    // Motion streaks behind the bursting bars.
    if(a<BURST+.08f){const float f=1-a/(BURST+.08f);for(int j=-1;j<=1;j+=2)sorcLine(rx,ry+j*7,rx+side*len*.8f,ry+j*7,chain,q8(.5f*f),0);}
  }
  // The catch: a flash at the block.
  if(a<.18f)spark(hx,hy,hot,1.3f*(1-a/.18f),4.5f);
  // Sparks spray off each nose: thick at the burst, in revving bursts after.
  const float nose=len+5;
  if(nose>8)for(int side=-1;side<=1;side+=2) {
    const float sx=hx+side*11+side*nose,sy=hy;
    for(unsigned i=0;i<24;++i) {
      const uint32_t id=i*2+(side>0);
      const float period=.24f+.14f*rnd(id,901),life=.16f+.16f*rnd(id,902),clock=t+period*rnd(id,903);
      const unsigned cycle=unsigned(clock/period);const float age=clock-cycle*period,born=t-age;
      if(age>=life || born<CATCH)continue;
      const float ab=born-CATCH,rate=ab<.8f?1:born<RETRACT-.6f?.45f+.3f*sine(born*7.4f):0;
      if(rnd(id*131+cycle,904)>=rate)continue;
      const float v=90+110*rnd(id*7+cycle,905),spread=-1.0f+1.4f*rnd(id*5+cycle,906);
      const float vx=side*spread*v,vy=(.35f+.6f*rnd(id*3+cycle,907))*v;
      const float x=sx+vx*age,y=sy+vy*age+130*age*age,u=age/life;
      const float px=x-vx*.045f,py=y-(vy+260*age)*.045f;
      const Rgb c=u<.4f?mixed(hot,orange,u/.4f):mixed(orange,ember,(u-.4f)/.6f);
      sorcLine(px,py,x,y,inkOf(c),q8(.6f*(1-u)),q8(1.4f*(1-u)));if(u<.5f)dot(x,y,hot,1.1f*(1-2*u));
    }
  }
}

// ---- Hammer: red droplets stream in from all round and gather into a great
// hammer held high under the readout; it winds back, swings down onto the
// floor to the left and lands with a flash, a ring racing out along the
// floor and a crown of red droplets. Sustain: the hammer stands planted in a
// rippling red pool, a sheen sliding over it and drips running off its
// handle; then it melts down into the pool, which drains away.
// A convex polygon, filled a row at a time.
inline void Renderer::ripsawFill(const float *xs,const float *ys,int n,const Ink &c,int kq) {
  if(kq<5 || n>8)return;
  float top=ys[0],bot=ys[0];for(int i=1;i<n;++i){top=std::min(top,ys[i]);bot=std::max(bot,ys[i]);}
  // Each edge's slope once, so a row costs no division.
  float slope[8];
  for(int i=0;i<n;++i){const int j=i+1==n?0:i+1;const float dy=ys[j]-ys[i];slope[i]=dy!=0?(xs[j]-xs[i])/dy:0;}
  for(int y=std::max(0,int(std::ceil(top)));y<=std::min(bottom_,floori(bot));++y) {
    float lo=1e9f,hi=-1e9f;
    for(int i=0;i<n;++i) {
      const int j=i+1==n?0:i+1;const float ya=ys[i],yb=ys[j];
      if((ya<=y && yb>y) || (yb<=y && ya>y)){const float x=xs[i]+(y-ya)*slope[i];lo=std::min(lo,x);hi=std::max(hi,x);}
    }
    for(int x=std::max(0,int(std::ceil(lo))),x1=std::min(w_-1,floori(hi));x<=x1;++x)addw(x,y,c,kq);
  }
}
// A flattened ring, its radius rippling with angle (`wobble` px) and the
// ripple turning with `spin`; `density` points per px of rx+ry (about 3 for
// an unbroken ring).
inline void Renderer::ripsawOval(float cx,float cy,float rx,float ry,const Ink &c,int kq,float wobble,float spin,float density) {
  if(kq<5 || rx<1)return;
  const int n=std::max(16,int((rx+ry)*density));const float step=2*HALF_TURN/n;
  // The angles a, 3a+spin and 5a-1.3 spin advance by rotation, so a point
  // costs a few multiplies: no table lookup or division per point. The step
  // rotations are exact (six libm calls per ring): the table's error would
  // build up round the ring.
  const float cs=std::cos(step),sn=std::sin(step),cs3=std::cos(3*step),sn3=std::sin(3*step),cs5=std::cos(5*step),sn5=std::sin(5*step);
  const float wob=wobble/rx,ax=rx,ay=ry;
  float ca=1,sa=0,c3=cosine(spin),s3=sine(spin),c5=cosine(-spin*1.3f),s5=sine(-spin*1.3f);
  for(int i=0;i<n;++i) {
    const float w=wob!=0?1+wob*s3*(.6f+.4f*s5):1;
    const int x=roundi(cx+ax*w*ca),y=roundi(cy+ay*w*sa);
    if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_))addw(x,y,c,(i&1)?kq:(kq*3)>>2);
    float t=ca*cs-sa*sn;sa=sa*cs+ca*sn;ca=t;
    if(wob!=0){t=c3*cs3-s3*sn3;s3=s3*cs3+c3*sn3;c3=t;t=c5*cs5-s5*sn5;s5=s5*cs5+c5*sn5;c5=t;}
  }
}
// The hammer: a handle from the grip at (px,py) along angle theta and a head
// across its end, in crimson with a bright red rim and a pale sheen along the
// head's leading edge. `fill` scales the body, `sheen` (0..1) places a glint
// sliding along the head (negative: none).
inline void Renderer::ripsawHammerAt(float px,float py,float theta,float k,float fill,float sheen) {
  if(k<=.02f)return;
  constexpr float L=36,HW=1.6f,HT=7,HL=14;
  const float ux=cosine(theta),uy=sine(theta),vx=-uy,vy=ux;
  const Ink body=inkOf(rgb8(150,6,24)),rim=inkOf(rgb8(255,36,48)),pale=inkOf(rgb8(255,186,196));
  const float hx=px+ux*L,hy=py+uy*L;
  float qx[4],qy[4];
  // Handle, pommel to head.
  const float ax=px-ux*4,ay=py-uy*4,bx=hx-ux*HT,by=hy-uy*HT;
  qx[0]=ax+vx*HW;qy[0]=ay+vy*HW;qx[1]=bx+vx*HW;qy[1]=by+vy*HW;qx[2]=bx-vx*HW;qy[2]=by-vy*HW;qx[3]=ax-vx*HW;qy[3]=ay-vy*HW;
  ripsawFill(qx,qy,4,body,q8(.75f*k*fill));
  sorcLine(qx[0],qy[0],qx[1],qy[1],rim,q8(.7f*k),q8(.7f*k));sorcLine(qx[3],qy[3],qx[2],qy[2],rim,q8(.5f*k),q8(.5f*k));
  // A knob at the end of the grip, and wraps along it.
  for(int i=-2;i<=2;++i)sorcLine(ax+vx*i*.9f-ux,ay+vy*i*.9f-uy,ax+vx*i*.9f+ux,ay+vy*i*.9f+uy,rim,q8(.8f*k),q8(.8f*k));
  for(int i=0;i<4;++i){const float d=4+3*i;sorcLine(px+ux*d+vx*2,py+uy*d+vy*2,px+ux*(d+1.5f)-vx*2,py+uy*(d+1.5f)-vy*2,inkOf(rgb8(255,186,196)),q8(.4f*k),q8(.4f*k));}
  // Head.
  qx[0]=hx-ux*HT+vx*HL;qy[0]=hy-uy*HT+vy*HL;qx[1]=hx+ux*HT+vx*HL;qy[1]=hy+uy*HT+vy*HL;
  qx[2]=hx+ux*HT-vx*HL;qy[2]=hy+uy*HT-vy*HL;qx[3]=hx-ux*HT-vx*HL;qy[3]=hy-uy*HT-vy*HL;
  ripsawFill(qx,qy,4,body,q8(.8f*k*fill));
  for(int i=0;i<4;++i){const int j=(i+1)&3;sorcLine(qx[i],qy[i],qx[j],qy[j],rim,q8(k),q8(k));}
  // A band round the head where the handle meets it, and the sheen on its outer edge.
  sorcLine(hx-ux*2+vx*(HL-1),hy-uy*2+vy*(HL-1),hx-ux*2-vx*(HL-1),hy-uy*2-vy*(HL-1),rim,q8(.45f*k),q8(.45f*k));
  sorcLine(qx[1]-ux+vx*-1.5f,qy[1]-uy-vy*1.5f,qx[2]-ux+vx*1.5f,qy[2]-uy+vy*1.5f,pale,q8(.55f*k),q8(.55f*k));
  if(sheen>=0 && sheen<=1){const float s=HL-1-2*(HL-1)*sheen;dot(hx+ux*(HT-1)+vx*s,hy+uy*(HT-1)+vy*s,WHITE,.9f*k);}
}
inline void Renderer::ripsawHammer(float t) {
  constexpr float GATHER=.72f,WIND=.8f,SWING=1.0f,IMPACT=1.14f,MELT=3.7f,END=5.0f,L=36,HT=7,HL=14;
  constexpr float RAISED=-75*HALF_TURN/180,BACK=-95*HALF_TURN/180,DOWN=28*HALF_TURN/180;
  const Box &b=number_;
  // Planted on the left, clear of the replay, the handle rises to the left
  // at 28 degrees and the head's lower corner bites a pixel into the floor;
  // the grip follows from that. Raised, the head stands up over the grip.
  const float floorY=b.y1+80.0f,ix=b.cx()-28,cd=cosine(DOWN),sd=sine(DOWN);
  const float iy=floorY+1-(std::fabs(sd)*HT+std::fabs(cd)*HL),gdx=ix-L*cd,gdy=iy-L*sd;
  const float grx=b.cx()-37,gry=b.y1+62.0f,gbx=grx-3,gby=gry+4;
  const Rgb red=rgb8(255,36,48),deep=rgb8(170,8,26),pale=rgb8(255,186,196);
  const Ink redInk=inkOf(red),deepInk=inkOf(deep);
  glint(t,IMPACT,.42f);
  // The pose: held high, wound back a little, then the swing accelerates down
  // to the floor and rebounds a touch.
  auto pose=[&](float tt,float &gx,float &gy,float &th) {
    gx=grx;gy=gry;th=RAISED;
    if(tt>=WIND && tt<SWING){const float u=smooth((tt-WIND)/(SWING-WIND));gx=grx+(gbx-grx)*u;gy=gry+(gby-gry)*u;th=RAISED+(BACK-RAISED)*u;}
    else if(tt>=SWING && tt<IMPACT){const float u=(tt-SWING)/(IMPACT-SWING),q=u*u;gx=gbx+(gdx-gbx)*q;gy=gby+(gdy-gby)*q;th=BACK+(DOWN-BACK)*q;}
    else if(tt>=IMPACT){const float a=tt-IMPACT;gx=gdx;gy=gdy;th=DOWN-.1f*std::exp(-9*a)*std::fabs(sine(a*26));}
  };
  float px,py,theta;pose(t,px,py,theta);
  // Gathering: droplets stream in onto the hammer's outline and it fills in.
  const float formed=smooth((t-.3f)/(GATHER-.3f)),melt=smooth((t-MELT)/1.0f);
  const float sink=6*melt;
  const float sheen=t>IMPACT+.5f?((t-IMPACT-.5f)*.8f-floori((t-IMPACT-.5f)*.8f))*1.6f:-1;
  if(formed>0)ripsawHammerAt(px,py+sink,theta,formed*(1-melt),formed*(1-.5f*melt),sheen);
  if(t<GATHER+.05f) {
    const float ux=cosine(RAISED),uy=sine(RAISED);
    for(unsigned i=0;i<56;++i) {
      const float arrive=.25f+.45f*rnd(i,921),start=arrive-.3f-.15f*rnd(i,922),u=(t-start)/(arrive-start);
      if(u<0 || u>=1.15f)continue;
      // A target on the hammer: along the handle or across the head.
      const float along=i%3?L-8+16*rnd(i,923):4+24*rnd(i,923),across=i%3?(rnd(i,924)-.5f)*28:(rnd(i,924)-.5f)*3;
      const float tx=grx+ux*along-uy*across,ty=gry+uy*along+ux*across;
      const float an=2*HALF_TURN*rnd(i,925),r=50+40*rnd(i,926);
      const float sx=tx+r*cosine(an),sy=std::max(b.y1+6.0f,ty+r*.8f*std::fabs(sine(an))+20);
      const float e=std::min(1.0f,u),q=e*e,bend=14*(rnd(i,927)-.5f)*sine(HALF_TURN*e);
      const float x=sx+(tx-sx)*q+bend,y=sy+(ty-sy)*q;
      const float k=u<1?.5f+.6f*e:1-(u-1)/.15f;
      dot(x,y,u<1?mixed(red,pale,.3f):red,1.3f*k);dot(x-(tx-sx)*.05f,y-(ty-sy)*.05f,deep,.8f*k);
    }
  }
  // The swing: a smear of arcs traced by the head's outer face over the
  // last 0.14 s, brightest at the head and fading back along the path.
  if(t>=SWING && t<IMPACT+.05f) {
    constexpr int STEPS=12;
    const float from=std::max(SWING,t-.14f),to=std::min(t,IMPACT),fade=1-smooth((t-IMPACT)/.05f);
    float lx[5],ly[5];bool have=false;
    for(int j=0;j<=STEPS;++j) {
      float gx,gy,th;pose(from+(to-from)*j/STEPS,gx,gy,th);
      const float ux=cosine(th),uy=sine(th),vx=-uy,vy=ux,hx=gx+ux*(L+HT),hy=gy+uy*(L+HT);
      const int kq=q8(.75f*fade*j/STEPS);
      for(int e=0;e<5;e+=2) {
        const float off=-HL+e*(HL/2),x=hx+vx*off,y=hy+vy*off;
        if(have)sorcLine(lx[e],ly[e],x,y,e==0 || e==4?inkOf(pale):redInk,kq,kq);
        lx[e]=x;ly[e]=y;
      }
      have=true;
    }
  }
  if(t<IMPACT)return;
  const float a=t-IMPACT;
  // The landing: a flash at the face, impact lines and two rings racing out.
  if(a<.1f)spark(ix,floorY-2,mixed(pale,WHITE,.7f),1.6f*(1-a/.1f),6);
  // Impact lines bursting from the face for a moment.
  if(a<.14f){const float u=a/.14f;for(int i=0;i<8;++i){const float an=-HALF_TURN*(i+.5f)/8,ca=cosine(an),sa=sine(an),r0=10+26*u;
    sorcLine(ix+ca*r0,floorY+sa*r0*.75f,ix+ca*(r0+12),floorY+sa*(r0+12)*.75f,inkOf(WHITE),q8(1-u),q8(.2f*(1-u)));}}
  // A fan of splash streaks thrown up from the face.
  if(a<.22f) {
    const float u=a/.22f;
    for(int i=0;i<9;++i) {
      const float an=-HALF_TURN*(.1f+.8f*(i+.5f*rnd(i,935))/9),ca=cosine(an),sa=sine(an),r0=4+40*easeOut(u),len=(14+10*rnd(i,936))*(1-u);
      sorcLine(ix+ca*r0,floorY+sa*r0*.8f,ix+ca*(r0+len),floorY+sa*(r0+len)*.8f,inkOf(i&1?red:pale),q8(1.1f*(1-u)),q8(.3f*(1-u)));
    }
  }
  const Ink ringInk=inkOf(mixed(red,accent_,.35f));
  for(int r=0;r<2;++r){const float u=(a-.1f*r)/.7f;if(u>0 && u<1)ripsawOval(ix,floorY+1,10+85*easeOut(u),(10+85*easeOut(u))*.24f,ringInk,q8((r?.6f:1)*(1-u)),0,0,2.2f);}
  // The crown: droplets thrown up and out, landing in small splats.
  for(unsigned i=0;i<44;++i) {
    const float vx=(rnd(i,931)-.5f)*190,vy=-(60+150*rnd(i,932)),g=420,land=-2*vy/g;
    const float age=a-.02f*rnd(i,933);if(age<0)continue;
    if(age<land) {
      const float x=ix+vx*age,y=floorY+vy*age+.5f*g*age*age;
      dot(x,y,i%4?red:pale,1.2f);if(i&1)plot(x-vx*.03f,y-(vy+g*age)*.03f,deep,.7f);
    } else if(age<land+.5f) {
      const float u=(age-land)/.5f,x=ix+vx*land;
      sorcLine(x-1-2*u,floorY,x+1+2*u,floorY,deepInk,q8(.7f*(1-u)),q8(.7f*(1-u)));
    }
  }
  // The pool round the head: it spreads, ripples and slowly turns; it drains in the settle.
  const float pool=smooth(a/.4f)*(1-smooth((t-MELT-.3f)/1.0f));
  if(pool>0) {
    const float rx=(14+12*smooth(a/.8f))*(1-.4f*melt);
    ripsawOval(ix+4,floorY+1,rx,rx*.24f,redInk,q8(.75f*pool),1.6f,t*2.2f,3);
    ripsawOval(ix+4,floorY+1,rx*.62f,rx*.62f*.24f,deepInk,q8(.6f*pool),1,-t*1.7f,2);
  }
  // Slow ripples from the pool every 0.7 s through the sustain.
  for(int r=0;r<4;++r){const float u=(a-.7f-.7f*r)/.7f;if(u>0 && u<1)ripsawOval(ix+4,floorY+1,26+30*u,(26+30*u)*.24f,ringInk,q8(.45f*(1-u)*(1-melt)),.8f,t,1.4f);}
  // Drips run down the handle and fall into the pool; in the melt, many at once.
  for(unsigned i=0;i<10;++i) {
    const float period=.9f+.4f*rnd(i,941),clock=a-.4f+period*rnd(i,942);if(clock<0)continue;
    const unsigned cycle=unsigned(clock/period);const float age=clock-cycle*period;
    const float along=4+18*rnd(i*7+cycle,943),x=px+cosine(theta)*along,y0=py+sine(theta)*along+sink+2,fall=floorY-y0;
    const float drop=age<.35f?0:std::min(fall,.5f*300*(age-.35f)*(age-.35f));
    const float k=(age<.35f?age/.35f:1)*(1-smooth((t-END+.6f)/.4f));
    if(drop<fall)dot(x,y0+drop+(age<.35f?age*3:1),i&1?red:pale,.9f*k);
    else{const float u=std::min(1.0f,(age-.35f-std::sqrt(2*fall/300))/.3f);if(u<1)ripsawOval(x,floorY+1,2+5*u,1+1.2f*u,redInk,q8(.6f*(1-u)*k),0,0,3);}
  }
}

// ---- Foxbite: a hand in the fox sign draws itself in light at the right and
// frames a point under the readout; a fox head of pale light comes down over
// that point, its jaws open wide, and snaps shut with a flash, shaking once.
// Sustain: the head burns away into foxfire, wisps of pale flame circling
// slowly under the readout while the fox's eyes linger, blink and fade;
// then the wisps rise and go out.
// A polyline of n points (x,y pairs) placed at (ox,oy) and scaled by
// (sx,sy); `drawn` (0..1) draws only that leading share of it.
inline void Renderer::ripsawPath(const float *pts,int n,float ox,float oy,float sx,float sy,const Ink &c,int kq,float drawn) {
  if(kq<5 || n<2 || drawn<=0)return;
  const float segs=(n-1)*std::min(1.0f,drawn);
  for(int i=0;i+1<n && i<segs;++i) {
    const float f=std::min(1.0f,segs-i);
    const float x0=ox+pts[2*i]*sx,y0=oy+pts[2*i+1]*sy,x1=ox+pts[2*i+2]*sx,y1=oy+pts[2*i+3]*sy;
    sorcLine(x0,y0,x0+(x1-x0)*f,y0+(y1-y0)*f,c,kq,kq);
  }
}
// The fox's head, face on, nose down: tall ears, wide cheeks, slanted eyes
// under red markings and a muzzle whose lower jaw drops `open` px. Unit
// coordinates, scaled by (sx,sy) round (hx,hy); the mouth is at (0,16).
// `dissolve` (0..1) burns the outline away from the chin upward.
inline void Renderer::ripsawFoxHead(float hx,float hy,float sx,float sy,float open,float k,float dissolve) {
  if(k<=.02f)return;
  static constexpr float SIDE[]={-7,-17, -16,-33, -23,-13, -27,-3, -24,5, -15,10, -12,11};
  static constexpr float BROW[]={-7,-17, 0,-14, 7,-17};
  static constexpr float EAR[]={-10,-18, -15,-29, -19,-15};
  static constexpr float EYE[]={-17,-7, -12,-3, -6,-2};
  static constexpr float MARK[]={-19,-11, -13,-9, -9,-7};
  static constexpr float BRIDGE[]={-6,-1, -3,6, -1,8};
  static constexpr float NOSE[]={-2,8, 0,10};
  static constexpr float LIP[]={-12,11, -6,13, 0,14};
  static constexpr float INNER[]={-21,-11, -24,-3, -21,4, -14,8};
  const Ink pale=inkOf(rgb8(255,246,232)),warm=inkOf(mixed(rgb8(255,176,96),accent_,.25f)),red=inkOf(rgb8(240,34,44)),hot=inkOf(WHITE);
  const int kq=q8(k),soft=q8(.5f*k);
  // The burn: rows below the line are gone.
  const float cut=hy+(18-52*dissolve)*sy;
  auto seg=[&](float x0,float y0,float x1,float y1,const Ink &c,int kk,bool thick) {
    if(dissolve>0){if(y0>cut && y1>cut)return;if(y0>cut){x0=x1+(x0-x1)*(cut-y1)/(y0-y1);y0=cut;}if(y1>cut){x1=x0+(x1-x0)*(cut-y0)/(y1-y0);y1=cut;}}
    sorcLine(x0,y0,x1,y1,c,kk,kk);
    // Two pixels thick, so the strokes survive the gaps round the replay's lines.
    if(thick)sorcLine(x0,y0+1,x1,y1+1,c,(kk*5)>>3,(kk*5)>>3);
  };
  auto path=[&](const float *p,int n,const Ink &c,int kk,bool thick) {
    for(int side=-1;side<=1;side+=2)for(int i=0;i+1<n;++i)
      seg(hx+side*p[2*i]*sx,hy+p[2*i+1]*sy,hx+side*p[2*i+2]*sx,hy+p[2*i+3]*sy,c,kk,thick);
  };
  path(SIDE,7,pale,kq,true);path(BROW,3,pale,(kq*3)>>2,false);path(EAR,3,red,(kq*3)>>2,false);
  path(MARK,3,red,kq,true);path(BRIDGE,3,pale,(kq*3)>>2,false);path(EYE,3,hot,kq,true);path(NOSE,2,hot,kq,true);
  path(INNER,4,warm,soft,false);path(LIP,3,pale,kq,false);
  // The lower jaw drops `open` px below the lip; a row of teeth on each.
  const float o=open/sy;
  const float JAW[]={-12,11, -7,13.5f+o*.6f, 0,15+o};
  path(JAW,3,pale,kq,true);
  for(int side=-1;side<=1;side+=2)for(int i=0;i<3;++i) {
    const float u=-10.5f+3.3f*i,lipY=11+(u+12)*(3/12.0f),jawY=11+(u+12)*(4/12.0f)*(1+o*.15f)+(u>-7?o*.6f+(u+7)*(1.5f+.4f*o)/7:o*.6f*(u+12)/5);
    seg(hx+side*u*sx,hy+lipY*sy,hx+side*(u+.8f)*sx,hy+(lipY+2.4f)*sy,hot,kq,false);
    seg(hx+side*(u+1.6f)*sx,hy+jawY*sy,hx+side*(u+.8f)*sx,hy+(jawY-2.4f)*sy,hot,(kq*3)>>2,false);
  }
}
// A wisp of foxfire: a bright core and a flame flickering up from it.
inline void Renderer::ripsawWisp(float x,float y,float size,float t,uint32_t seed,const Ink &core,const Ink &flame,float k) {
  if(k<=.02f)return;
  const int kq=q8(k);
  addw(roundi(x),roundi(y),core,kq);addw(roundi(x)+1,roundi(y),core,kq>>1);addw(roundi(x),roundi(y)+1,core,kq>>1);addw(roundi(x)-1,roundi(y),core,kq>>1);
  const float lick=size*(1+.25f*sine(t*13+seed));
  for(int i=1;i<=int(lick);++i) {
    const float u=i/lick,sway=1.6f*u*u*sine(t*7+seed*1.7f+u*3);
    addw(roundi(x+sway),roundi(y-i),flame,int(kq*(1-u)));
    if(i<lick*.6f){addw(roundi(x+sway-1),roundi(y-i),flame,int(kq*(1-u)*.6f));addw(roundi(x+sway+1),roundi(y-i),flame,int(kq*(1-u)*.6f));}
  }
}
inline void Renderer::ripsawFoxbite(float t) {
  constexpr float SIGN=.35f,AIM=.5f,DESCEND=.62f,SNAP=1.12f,BURN=1.65f,RISE=3.75f,END=5.0f;
  const Box &b=number_;
  const float tx=b.cx(),ty=b.y1+35.0f;
  const Ink pale=inkOf(rgb8(255,246,232)),warm=inkOf(mixed(rgb8(255,176,96),accent_,.25f)),red=inkOf(rgb8(240,34,44));
  const Ink fireCore=inkOf(mixed(rgb8(220,240,255),accent_,.2f)),fireFlame=inkOf(mixed(rgb8(120,180,255),accent_,.45f));
  glint(t,SNAP,.42f);
  // The hand sign: two raised fingers for ears, the others meeting the
  // thumb for the snout, an eye of light in between; it points left.
  static constexpr float HAND[]={0,0, 8,-3, 10,-5, 11,-15, 14,-6, 16,-6, 18,-15, 20,-5, 22,0, 22,10, 16,12, 6,4, 0,0};
  static constexpr float MOUTH[]={0,0, 7,1};
  const float hx=b.x1-30.0f,hy=b.y1+25.0f;
  const float signK=smooth(t/.1f)*(1-smooth((t-SNAP)/.35f));
  if(signK>0) {
    const float drawn=std::min(1.0f,t/SIGN),glow=t>SIGN?.8f+.2f*sine(t*30):.8f;
    ripsawPath(HAND,13,hx,hy,1,1,pale,q8(signK*glow),drawn);
    ripsawPath(MOUTH,2,hx,hy,1,1,warm,q8(.8f*signK),drawn);
    if(t>SIGN){dot(hx+11,hy-1,WHITE,signK*(.7f+.3f*sine(t*24)));}
    if(t<SIGN)dot(hx+HAND[2*int(drawn*12)],hy+HAND[2*int(drawn*12)+1],WHITE,1);
  }
  // The aim: a line of light from the sign's eye to the target, and a ring closing on it.
  if(t>=AIM && t<SNAP) {
    const float u=clamp01((t-AIM)/.12f),fade=1-smooth((t-DESCEND-.15f)/.25f);
    const float ex=hx+11,ey=hy-1;
    for(int i=0;i<24;++i){const float f=i/24.0f;if(f>u)break;if(i&1)continue;addw(roundi(ex+(tx-ex)*f),roundi(ey+(ty-ey)*f),warm,q8(.6f*fade));}
    const float r=3+10*(1-u);ripsawOval(tx,ty,r,r*.7f,red,q8(.8f*fade*u),0,0,3);
  }
  // The head comes down over the target, swelling, its jaws opening; snaps; holds and shakes.
  if(t>=DESCEND && t<BURN+.75f) {
    const float d=clamp01((t-DESCEND)/(SNAP-DESCEND)),e=easeOut(d);
    // Wide and short, so the whole head fits between the readout and the replay.
    float sx=.5f+.35f*e,sy=.38f+.24f*e,open=0,shake=0;
    if(t<SNAP)open=8*smooth(d/.75f);
    else{const float a=t-SNAP,pulse=1+.07f*std::exp(-14*a)*sine(a*40+1.6f);open=a<.05f?8*(1-a/.05f):0;sx*=pulse;sy*=pulse;shake=2.2f*std::exp(-7*a)*sine(a*48);}
    const float hy0=ty-16*sy-(1-e)*12;
    const float k=smooth((t-DESCEND)/.12f),dissolve=clamp01((t-BURN)/.7f);
    ripsawFoxHead(tx+shake,hy0,sx,sy,open,k*(1-dissolve*.3f),dissolve);
    // The burning edge sheds sparks of foxfire.
    if(dissolve>0 && dissolve<1)for(int i=0;i<8;++i) {
      const float cut=hy0+(18-52*dissolve)*sy,x=tx+(rnd(i+uint32_t(t*20)*8,951)-.5f)*44*sx;
      dot(x,cut-2*rnd(i,952),i&1?mixed(rgb8(220,240,255),accent_,.2f):rgb8(255,246,232),.9f);
    }
  }
  // The snap: a flash between the jaws and bite marks flying out.
  if(t>=SNAP && t<SNAP+.25f) {
    const float a=(t-SNAP)/.25f;
    if(a<.6f)spark(tx,ty,rgb8(255,240,220),1.4f*(1-a/.6f),4.5f);
    for(int i=0;i<8;++i){const float an=i*(2*HALF_TURN/8)+.2f,ca=cosine(an),sa=sine(an)*.7f,r0=8+22*easeOut(a);
      sorcLine(tx+ca*r0,ty+sa*r0,tx+ca*(r0+6*(1-a)),ty+sa*(r0+6*(1-a)),i&1?pale:red,q8(1-a),q8(.3f*(1-a)));}
  }
  // The eyes linger after the head burns away, blink once and fade.
  if(t>=BURN+.4f && t<RISE) {
    const float k=smooth((t-BURN-.4f)/.3f)*(1-smooth((t-RISE+.6f)/.6f)),blink=std::fabs(t-2.85f)<.07f?.1f:1;
    const float sx=.85f,sy=.62f,ey=ty-16*sy;
    for(int side=-1;side<=1;side+=2){sorcLine(tx+side*16*sx,ey-6*sy,tx+side*12*sx,ey-3*sy,red,q8(.9f*k*blink),q8(.9f*k*blink));
      sorcLine(tx+side*12*sx,ey-3*sy,tx+side*7*sx,ey-2*sy,red,q8(.9f*k*blink),q8(.5f*k*blink));}
  }
  // Foxfire: wisps born from the burning head circle slowly under the readout, then rise and go out.
  if(t>=BURN) {
    const float ring=smooth((t-BURN)/.5f),endK=1-smooth((t-RISE)/(END-RISE-.2f));
    for(int i=0;i<7;++i) {
      const float phase=i*(2*HALF_TURN/7)+(t-BURN)*.9f,rx=46+5*sine(t*.7f+i),ry=11;
      const float x=tx+rx*cosine(phase)*ring,y=ty-6+ry*sine(phase)*ring+1.5f*sine(t*3+i*2)-std::max(0.0f,t-RISE)*(14+4*i);
      const float depth=.65f+.35f*sine(phase);
      ripsawWisp(x,y,4+3*depth,t,uint32_t(i*31),fireCore,fireFlame,endK*(.7f+.4f*depth)*ring);
    }
  }
}

// ---- Detonate: a ring pin sits in a small loop under the readout; it is
// tugged, pops out with a click and flies off, the loop blinks faster three
// times, and explosions go off one after another round the readout, each a
// rolling fireball cooling from white through orange to red, flinging sparks,
// ending in one big blast below the readout. Sustain: the blasts roll into
// smoke that swells and rises with embers glowing in it, and a few late
// pops flare inside it; then the smoke thins away.
// One explosion `age` s after it goes off: a white flash, then a fireball
// swelling and rolling (a radial heat falling off from a white-yellow core
// through orange to a red rim, its edge broken up by scrolling noise),
// sparks flung out, and then a soft cloud of smoke that rises and thins
// over smokeLife s, its body a sparse speckle of grey falling off to its edge.
// noise(xq+i*stepq,yq,salt) for i in 0..n-1, exactly as noise() gives it,
// hashing the four lattice corners once per cell instead of per pixel: a
// fireball or a cloud reads a few hundred values a row, about four per cell.
inline void Renderer::ripsawNoiseRow(int xq,int stepq,int n,int yq,uint32_t salt,uint8_t *out) {
  const int iy=yq>>8,v=detail::SMOOTH[yq&255];
  int cell=0,a=0,b=0,c=0,d=0;bool have=false;
  for(int i=0;i<n;++i,xq+=stepq) {
    const int ix=xq>>8;
    if(!have || ix!=cell){cell=ix;have=true;a=lattice(ix,iy,salt);b=lattice(ix+1,iy,salt);c=lattice(ix,iy+1,salt);d=lattice(ix+1,iy+1,salt);}
    const int u=detail::SMOOTH[xq&255],top=a+(((b-a)*u)>>8),bottom=c+(((d-c)*u)>>8);
    out[i]=uint8_t(top+(((bottom-top)*v)>>8));
  }
}
// The widest |dx| with dx*dx+ey2 <= limit, walked on from the last row's
// answer in mx (rows are visited in order, so the walk is short); -1 if none.
inline int Renderer::ripsawSpan(int limit,int ey2,int &mx) {
  const int room=limit-ey2;if(room<0)return -1;
  while(mx>0 && mx*mx>room)--mx;
  while((mx+1)*(mx+1)<=room)++mx;
  return mx;
}
inline void Renderer::ripsawBlast(float cx,float cy,float size,float age,uint32_t seed,float smokeLife) {
  if(age<0)return;
  const Rgb white=rgb8(255,250,225),yellow=rgb8(255,214,80),orange=rgb8(255,128,24),red=rgb8(215,40,16);
  const int salt=int(seed*977);
  if(age<.06f)spark(cx,cy,white,1.5f*(1-age/.06f),std::min(5.0f,size*.35f));
  constexpr float FIRE=.5f;
  uint8_t row[MAX_W+2];
  if(age<FIRE) {
    const float u=age/FIRE,r=size*(.35f+.65f*easeOut(age/.22f)+.06f*u),fade=1-smooth((u-.55f)/.45f);
    const int cool8=int((1.2f-.85f*u)*256);
    const int ir=int(r*1.2f)+1,icx=roundi(cx),icy=roundi(cy),inv=int(65536/(r*r)),roll=int(age*900),kq=q8(fade);
    // The heat ramp's colours, once per blast, 4 heat steps apart.
    Ink ramp[64];for(int i=0;i<64;++i)ramp[i]=fire(i*4+2);
    // Only the columns with d2 <= 356 (256 at the radius, noise adds up to
    // 99) can light, so only those are visited. The noise is read on every
    // other column and every other row and held for the pixel after.
    const int limit=(357*256-1)/std::max(1,inv),e0=(-ir)&~1,cells=(ir-e0)/2+1;int mx=0,key=-1;
    for(int dy=-ir;dy<=ir;++dy) {
      const int y=icy+dy;if(unsigned(y)>unsigned(bottom_))continue;
      const int ey=(dy*298)>>8; // squashed like the floor
      const int span=std::min(ir,ripsawSpan(limit,ey*ey,mx));if(span<0)continue;
      const int from=std::max(-span,-icx),to=std::min(span,w_-1-icx);if(from>to)continue;
      const int k2=(dy+ir)>>1;
      if(k2!=key){key=k2;ripsawNoiseRow((icx+e0+salt)*60,120,cells,(icy-ir+2*k2)*60+roll,seed,row);}
      for(int dx=from;dx<=to;++dx) {
        const int d2=((dx*dx+ey*ey)*inv)>>8;
        // The edge breaks up: noise pushes it out or in by up to 40%.
        const int h=256-d2+((row[((dx&~1)-e0)>>1]-128)*100>>7);
        if(h<=0)continue;
        const int heat=std::min(255,(h*cool8)>>8);
        if(heat<18)continue;
        addw(icx+dx,y,ramp[heat>>2],(kq*std::min(256,heat*3))>>8);
      }
    }
  }
  if(age<.45f)for(int i=0;i<8;++i) {
    const float an=2*HALF_TURN*rnd(seed*16+i,962),v=size*(5+4*rnd(seed*16+i,963)),u=age/.45f;
    const float x=cx+cosine(an)*v*age,y=cy+sine(an)*v*age*.86f+90*age*age;
    dot(x,y,u<.4f?yellow:orange,1.3f*(1-u));
  }
  // Smoke: a soft speckled cloud, densest at its heart, rising and swelling.
  const float sa=age-.35f;
  if(sa>0 && sa<smokeLife) {
    const float u=sa/smokeLife,k=smooth(sa/.35f)*(1-smooth((u-.25f)/.75f));
    const float r=size*(.68f+.3f*easeOut(sa/1.4f)),y=cy-9*easeOut(sa/1.5f)-2.5f*sa;
    const Ink smokeInk=inkOf(mixed(mixed(rgb8(150,146,150),accent_,.15f),orange,.3f*(1-smooth(sa/.6f))));
    const int ir=int(r)+1,icx=roundi(cx),icy=roundi(y),inv=int(65536/(r*r)),drift=int(sa*500),kq=q8(.6f*k);
    // A speckle needs noise (at most 255) + falloff > 300, so falloff > 45:
    // only the columns inside that ellipse (d2 < 211) are visited.
    const int limit=(211*256-1)/std::max(1,inv),e0=(-ir)&~1,cells=(ir-e0)/2+1;int mx=0,key=-1;
    // The cloud's noise is read on every other column and every other row.
    if(kq>=5)for(int dy=-ir;dy<=ir;++dy) {
      const int yy=icy+dy;if(unsigned(yy)>unsigned(bottom_))continue;
      const int ey=(dy*298)>>8;
      const int span=std::min(ir,ripsawSpan(limit,ey*ey,mx));if(span<0)continue;
      const int from=std::max(-span,-icx),to=std::min(span,w_-1-icx);if(from>to)continue;
      const int k2=(dy+ir)>>1;
      if(k2!=key){key=k2;ripsawNoiseRow((icx+e0+salt)*70+drift,140,cells,(icy-ir+2*k2)*70-drift,seed+7,row);}
      for(int dx=from;dx<=to;++dx) {
        const int fall=256-(((dx*dx+ey*ey)*inv)>>8);if(fall<=0)continue;
        // A speckle: a pixel only where cloud noise beats the falloff's threshold.
        const int w=row[((dx&~1)-e0)>>1]+fall-300;if(w<=0 || (lattice(dx,dy,seed+11)&3)==0)continue;
        addw(icx+dx,yy,smokeInk,(kq*std::min(256,w*3+60))>>8);
      }
    }
    for(int i=0;i<3;++i) {
      const float ph=sa*(2+rnd(seed*8+i,964)*2)+i*2.1f,glow=.5f+.5f*sine(ph*3);
      const float ex=cx+(rnd(seed*8+i,965)-.5f)*r,ey=y+(rnd(seed*8+i,966)-.5f)*r*.7f-3*sa;
      dot(ex,ey,mixed(orange,red,u),k*glow*1.1f);
    }
  }
}
inline void Renderer::ripsawDetonate(float t) {
  constexpr float TUG=.12f,POP=.32f,FIRST=.62f,BIG=1.42f,END=5.0f;
  const Box &b=number_;
  const float cx=b.cx(),ly=b.y1+9.0f;
  const Ink steel=inkOf(rgb8(196,206,222)),warn=inkOf(rgb8(255,48,30));
  glint(t,BIG,.42f);
  sorcMark(1);
  // The loop the pin sits in, blinking red after the pull, faster and faster.
  const float loopK=smooth(t/.12f)*(1-smooth((t-FIRST)/.08f));
  if(loopK>0) {
    float blink=0;static constexpr float BEEPS[]={.38f,.48f,.55f};
    for(float at:BEEPS)if(t>=at && t<at+.05f)blink=1;
    ripsawOval(cx,ly,2.5f,2.5f,blink>0?warn:steel,q8(loopK),0,0,3);
    if(blink>0)spark(cx,ly,rgb8(255,90,60),.9f,3);
  }
  sorcMark(2);
  // The pin and its ring: at rest, tugged, then popped out and flung away spinning.
  if(t<FIRST) {
    float px=cx+3,py=ly,ang=0;
    if(t>=TUG && t<POP){const float u=(t-TUG)/(POP-TUG);px+=3*u*u+.6f*sine(t*90)*u;}
    else if(t>=POP){const float a=t-POP;px+=3+70*a;py+=-30*a+120*a*a;ang=a*22;}
    const float k=smooth(t/.12f)*(1-smooth((t-POP-.12f)/.15f));
    const float ca=cosine(ang),sa=sine(ang);
    sorcLine(px,py,px+7*ca,py+7*sa,steel,q8(k),q8(k));
    ripsawOval(px+11*ca,py+11*sa,4,4,steel,q8(k),0,0,3);
    if(t>=POP && t<POP+.08f)spark(cx+3,ly,rgb8(255,250,225),1.2f*(1-(t-POP)/.08f),3);
  }
  sorcMark(3);
  // The chain: blasts round the readout, left and right in turn, then the big one.
  struct Blast{float dx,dy,size,at;};
  static constexpr Blast CHAIN[]={{0,14,12,FIRST},{-40,30,14,.82f},{40,34,14,1.02f},{0,24,16,BIG}};
  for(unsigned i=0;i<4;++i){const Blast &q=CHAIN[i];ripsawBlast(cx+q.dx,b.y1+q.dy,q.size,t-q.at,i+1,i==3?2.4f:1.35f);}
  sorcMark(4);
  // Late pops flaring inside the smoke.
  static constexpr Blast POPS[]={{-30,26,6,2.2f},{32,38,6,2.8f}};
  sorcMark(5);
  // Embers drift up out of the smoke through the sustain, flickering as they cool.
  const float emberK=smooth((t-1.5f)/.3f)*(1-smooth((t-3.6f)/.8f));
  if(emberK>0)for(unsigned i=0;i<14;++i) {
    const float life=.9f+.6f*rnd(i,971),clock=t+life*rnd(i,972);const unsigned cycle=unsigned(clock/life);const float age=clock-cycle*life,u=age/life;
    const Blast &q=CHAIN[(i+cycle)%4];const uint32_t sd=i*37+cycle;
    const float x=cx+q.dx+(rnd(sd,973)-.5f)*q.size*1.4f+3*sine(age*5+i),y=b.y1+q.dy+(rnd(sd,974)-.3f)*q.size-22*age;
    dot(x,y,mixed(rgb8(255,200,90),rgb8(220,60,16),u),emberK*sine(HALF_TURN*u)*(.7f+.3f*sine(t*29+i)));
  }
  for(unsigned i=0;i<2;++i){const Blast &q=POPS[i];const float a=t-q.at;if(a>=0 && a<.6f)ripsawBlast(cx+q.dx,b.y1+q.dy,q.size,a,20+i,0);}
  (void)END;
  sorcMark(6);
}

// ---- Sharkfin: a fin breaks the floor under the readout as if it were
// water and cuts a circle through it, a foaming wake behind and chips of
// floor kicked up; it dips under, the shark leaps out in an arc across the
// floor and dives back in, each time with a splash of spray and chips and a
// ring. Sustain: the fin surfaces again and circles slowly, rings spreading
// where it dived; then it sinks and the floor goes still.
// A dorsal fin standing `rise` (0..1) out of the floor at (x,y), raked
// back from its travel direction `dir` (+1 right, -1 left).
inline void Renderer::ripsawFin(float x,float y,float dir,float scale,float rise,float k) {
  if(k<=.02f || rise<=.02f)return;
  const float h=12*scale*rise,w=6*scale;
  const float lx=x+dir*w,tx=x-dir*w,ax=x-dir*3.5f*scale,ay=y-h;
  const Ink body=inkOf(rgb8(80,160,190)),edge=inkOf(rgb8(185,236,250)),hot=inkOf(WHITE);
  float qx[3]={lx,ax,tx},qy[3]={y,ay,y};
  ripsawFill(qx,qy,3,body,q8(.55f*k));
  sorcLine(lx,y,ax,ay,hot,q8(k),q8(k));
  // The trailing edge curves in toward the root.
  const float mx=(ax+tx)*.5f+dir*1.6f*scale,my=(ay+y)*.5f+.5f*scale;
  sorcLine(ax,ay,mx,my,edge,q8(.85f*k),q8(.85f*k));sorcLine(mx,my,tx,y,edge,q8(.85f*k),q8(.7f*k));
  // Foam where it cuts the floor.
  sorcLine(tx-dir*2,y+1,lx+dir*3,y+1,inkOf(mixed(WHITE,accent_,.3f)),q8(.9f*k),q8(.9f*k));
}
// The shark: a side view facing along (ux,uy), drawn only above floorY.
inline void Renderer::ripsawShark(float x,float y,float ux,float uy,float k,int floorY) {
  if(k<=.02f)return;
  static constexpr float OUT[]={20,0, 12,-5, 2,-7, -3,-15, -5,-7, -12,-5, -17,-2, -25,-11, -21,0, -24,7, -17,2, -8,5, -2,6, -6,12, 2,7, 12,5, 20,0};
  static constexpr float CORE[]={20,0, 12,-5, -12,-5, -17,-1, -17,1, -8,5, 12,5};
  const Ink body=inkOf(rgb8(70,150,185)),edge=inkOf(rgb8(185,236,250)),hot=inkOf(WHITE);
  const int keep=bottom_;bottom_=std::min(bottom_,floorY);
  auto tf=[&](float px,float py,float &sx,float &sy){sx=x+px*ux-py*uy;sy=y+px*uy+py*ux;};
  float cx[7],cy[7];for(int i=0;i<7;++i)tf(CORE[2*i],CORE[2*i+1],cx[i],cy[i]);
  ripsawFill(cx,cy,7,body,q8(.4f*k));
  for(int i=0;i<16;++i){float x0,y0,x1,y1;tf(OUT[2*i],OUT[2*i+1],x0,y0);tf(OUT[2*i+2],OUT[2*i+3],x1,y1);sorcLine(x0,y0,x1,y1,i<6?hot:edge,q8(k),q8(k));}
  float ex,ey;tf(13,-1.5f,ex,ey);dot(ex,ey,WHITE,k);
  for(int g=0;g<3;++g){float x0,y0,x1,y1;tf(7-1.6f*g,-2,x0,y0);tf(6-1.6f*g,2.5f,x1,y1);sorcLine(x0,y0,x1,y1,edge,q8(.6f*k),q8(.6f*k));}
  bottom_=keep;
}
// Spray and chips of floor thrown up where the shark breaks through, and a ring.
inline void Renderer::ripsawSplash(float x,float y,float age,float size,uint32_t seed) {
  if(age<0 || age>1.0f)return;
  const Rgb spray=mixed(rgb8(200,240,255),accent_,.3f),chip=rgb8(178,158,128);
  for(int i=0;i<18;++i) {
    const float vx=(rnd(seed*32+i,981)-.5f)*90*size,vy=-(40+90*rnd(seed*32+i,982))*size,g=330,life=-2*vy/g;
    if(age>=life)continue;
    const float px=x+vx*age,py=y+vy*age+.5f*g*age*age,u=age/life;
    if(i%3==0){const int ix=roundi(px),iy=roundi(py);const Ink c=inkOf(chip);const int kq=q8(1.1f*(1-u*.5f));
      addw(ix,iy,c,kq);addw(ix+((i+int(age*20))&1),iy+1,c,kq);}
    else{dot(px,py,i&1?spray:WHITE,1.1f*(1-u*.4f));plot(px-vx*.025f,py-(vy+g*age)*.025f,spray,.5f);}
  }
  for(int r=0;r<2;++r){const float u=(age-.08f*r)/.8f;if(u>0 && u<1){const float rr=6+34*size*easeOut(u);ripsawOval(x,y+1,rr,rr*.26f,inkOf(spray),q8(.8f*(1-u)),.6f,u*4,2.5f);}}
}
inline void Renderer::ripsawSharkfin(float t) {
  constexpr float SURFACE=.2f,DIP=.95f,LEAP=1.1f,DIVE=1.72f,BACK=2.0f,SINK=3.7f,END=5.0f;
  const Box &b=number_;
  const float cx=b.cx(),cy=b.y1+36.0f,rx=46,ry=11;
  glint(t,LEAP,.42f);
  // The fin's angle round the circle: one fast lap, then slow laps after the dive.
  float phi,rise;
  if(t<DIP+.1f){const float u=clamp01(t/(DIP+.1f));phi=-.1f+2*HALF_TURN*(.55f*u+.45f*u*u)*.9f;rise=smooth((t-SURFACE+.15f)/.2f)*(1-smooth((t-DIP)/.1f));}
  else{phi=1.4f+(t-BACK)*1.5f;rise=smooth((t-BACK)/.25f)*(1-smooth((t-SINK)/.5f));}
  const float speed=t<DIP+.1f?1:.45f;
  const float fx=cx+rx*cosine(phi),fy=cy+ry*sine(phi),dir=sine(phi)>0?-1.0f:1.0f,depth=.8f+.25f*sine(phi);
  // The wake: foam on both sides of the path behind the fin, spreading and fading.
  if(rise>0)for(int j=1;j<=16;++j) {
    const float ph=phi-j*(.07f*speed+.03f),spread=.35f*j*depth,f=rise*(1-j/17.0f);
    const float px=cx+rx*cosine(ph),py=cy+ry*sine(ph),nx=-cosine(ph)*ry/rx,ny=-sine(ph);
    const float nn=1/std::sqrt(nx*nx+ny*ny+.01f);
    const Ink foam=inkOf(mixed(WHITE,accent_,.35f+.02f*j));
    addw(roundi(px+nx*nn*spread),roundi(py+ny*nn*spread*.5f),foam,q8(.8f*f));
    addw(roundi(px-nx*nn*spread),roundi(py-ny*nn*spread*.5f),foam,q8(.8f*f));
    if(j&1)addw(roundi(px),roundi(py+1),foam,q8(.35f*f));
  }
  if(rise>0)ripsawFin(fx,fy,dir,depth,rise,smooth(t/.15f));
  // Chips of floor kicked up off the fin's root while it cuts.
  if(rise>.3f)for(unsigned i=0;i<6;++i) {
    const float period=.35f,clock=t+period*rnd(i,991);const unsigned cycle=unsigned(clock/period);const float age=clock-cycle*period;
    const float bornPhi=phi-age*(t<DIP+.1f?5:1.5f),bx=cx+rx*cosine(bornPhi),by=cy+ry*sine(bornPhi);
    const float vx=(rnd(i*7+cycle,992)-.5f)*40,vy=-(25+30*rnd(i*7+cycle,993)),px=bx+vx*age,py=by+vy*age+150*age*age;
    if(py>by+1)continue;
    const int ix=roundi(px),iy=roundi(py);const Ink c=inkOf(i&1?rgb8(178,158,128):rgb8(200,240,255));
    addw(ix,iy,c,q8(rise));addw(ix+1,iy,c,q8(.5f*rise));
  }
  // The surfacing: a crack of foam where the fin first breaks the floor.
  if(t>=SURFACE-.15f && t<SURFACE+.35f){const float u=(t-SURFACE+.15f)/.5f;ripsawOval(cx+rx*cosine(-.1f),cy+ry*sine(-.1f)+1,3+14*u,(3+14*u)*.3f,inkOf(mixed(WHITE,accent_,.3f)),q8(.8f*(1-u)),.5f,0,2.5f);}
  // The leap: out at the left front of the floor, over in an arc, back in at the right.
  const float floorY=cy+ry,ax=cx-32,bx=cx+30,ARC=42;
  if(t>=LEAP-.05f && t<DIVE+.12f) {
    const float u=(t-LEAP+.05f)/(DIVE+.12f-LEAP+.05f),x=ax-8+(bx+8-ax+8)*u,y=floorY+12-ARC*4*u*(1-u);
    const float dx=bx+16-ax,dy=-ARC*4*(1-2*u),n=1/std::sqrt(dx*dx+dy*dy);
    ripsawShark(x,y,dx*n,dy*n,1,int(floorY));
    // Spray streaming off it while it is in the air.
    for(int i=1;i<=4;++i){const float back=i*.035f,uu=u-back;if(uu<0)continue;
      dot(ax-8+(bx+8-ax+8)*uu-6*n*dx,floorY+12-ARC*4*uu*(1-uu)+3,mixed(WHITE,accent_,.3f),.6f*(1-i/5.0f));}
  }
  ripsawSplash(ax,floorY,t-LEAP,1,1);
  ripsawSplash(bx,floorY,t-DIVE,.8f,2);
  // Rings keep spreading from the dive point through the sustain.
  for(int r=0;r<4;++r){const float u=(t-DIVE-.5f-.55f*r)/1.3f;if(u>0 && u<1){const float rr=8+40*u;ripsawOval(bx,floorY+1,rr,rr*.26f,inkOf(mixed(rgb8(200,240,255),accent_,.35f)),q8(.5f*(1-u)*(1-smooth((t-SINK)/.8f))),.8f,t,1.6f);}}
  (void)END;
}
}
