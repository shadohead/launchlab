#pragma once
// Demon Slayer-inspired celebrations (Sunwheel..Butterfly): Renderer members declared in
// best_effect_ui.h, which includes this file after the class.
// The breathing forms are drawn like woodblock prints: bold flat strokes,
// light outlines with darker fills between them, and hand-drawn curls at
// the tips of flames and waves, never a soft glow. Every stroke is a brush:
// a polyline of knots, offset across its width into a few parallel bands,
// each stroked in fixed point. Geometry is float once per knot; the pixel
// loops are integer steps.
namespace BestEffectUI {
namespace slayer {
constexpr int KNOTS=72;
// Row and knot maths, forced inline: the device build (-Os) calls the
// Renderer's small helpers out of line, and std::sqrt, std::floor, std::fmod
// and float division are library calls there, so the loops below use these.
BEST_EFFECT_INLINE int floorOf(float v){const int i=int(v);return i-(v<i);}
// The Renderer's table sine and cosine.
BEST_EFFECT_INLINE float sineOf(float a) {
  const float u=a*(256/(2*3.14159265f));const int i=floorOf(u);const float f=u-i;const int j=i&255;
  return detail::SINE[j]+(detail::SINE[j+1]-detail::SINE[j])*f;
}
BEST_EFFECT_INLINE float cosineOf(float a){return sineOf(a+3.14159265f/2);}
// 1/sqrt(v) for v > 0 by the bit-level estimate and two Newton steps
// (relative error about 5e-6): multiplies only. __builtin_memcpy, as the
// device build's -fno-builtin-memcpy turns std::memcpy into a call.
BEST_EFFECT_INLINE float rroot(float v) {
  uint32_t i;__builtin_memcpy(&i,&v,4);i=0x5f375a86u-(i>>1);float r;__builtin_memcpy(&r,&i,4);
  const float h=.5f*v;r*=1.5f-h*r*r;r*=1.5f-h*r*r;
  return r;
}
// sqrt(v), 0 for v <= 0.
BEST_EFFECT_INLINE float root(float v){return v>0?v*rroot(v):0;}
}

// A stroke pixel: plain addw, which already builds the neighbouring mask
// word's ink row before light lands within HALO of a word edge (a word's
// ink scan reads HALO pixels past its edges, so a word built after
// decoration was written just across its edge would take that decoration
// for UI ink). A blocked pixel writes nothing, so it needs no neighbour
// built: doing it here as well cost a second edge test per pixel.
BEST_EFFECT_INLINE void Renderer::lit(int x,int y,const Ink &c,int w){addw(x,y,c,w);}
// A fixed-point line, endpoints in Q8 px, weight lerped k0 to k1 (Q8); the
// end point is left to the next segment. Lines wholly off screen are skipped.
BEST_EFFECT_INLINE void Renderer::inkLine(int x0,int y0,int x1,int y1,const Ink &c,int k0,int k1) {
  if((y0<-768 && y1<-768) || (y0>(h_+3)<<8 && y1>(h_+3)<<8) || (x0<-768 && x1<-768) || (x0>(w_+3)<<8 && x1>(w_+3)<<8))return;
  if(k0<5 && k1<5)return;
  // Behind the readout every pixel is masked: skip strokes wholly inside it.
  const Box &b=number_;
  if(x0>=b.x0<<8 && x1>=b.x0<<8 && x0<=b.x1<<8 && x1<=b.x1<<8 && y0>=(b.y0+1)<<8 && y1>=(b.y0+1)<<8 && y0<=(b.y1-1)<<8 && y1<=(b.y1-1)<<8)return;
  const int dx=x1-x0,dy=y1-y0,n=std::max(1,(std::max(std::abs(dx),std::abs(dy))+128)>>8);
  const int sx=dx/n,sy=dy/n,sk=(k1-k0)*256/n;
  int x=x0+128,y=y0+128,k=k0*256;
  for(int i=0;i<n;++i,x+=sx,y+=sy,k+=sk)lit(x>>8,y>>8,c,k>>8);
}
// Knots x,y (px), half widths hw (px) and weights k (Q8). Each band strokes
// the polyline offset by `offset` half widths along the knot normals
// (positive to the left of travel in screen space), its weight band.k*k/256.
inline void Renderer::brush(const float *x,const float *y,const float *hw,const int *k,int n,const Band *bands,int nb) {
  if(n<2)return;
  n=std::min(n,slayer::KNOTS);
  float nx[slayer::KNOTS],ny[slayer::KNOTS];
  for(int i=0;i<n;++i) {
    const int a=std::max(0,i-1),b=std::min(n-1,i+1);
    const float dx=x[b]-x[a],dy=y[b]-y[a],l=dx*dx+dy*dy,inv=l>1e-6f?hw[i]*slayer::rroot(l):0;
    nx[i]=dy*inv;ny[i]=-dx*inv;
  }
  for(int j=0;j<nb;++j) {
    const Band &b=bands[j];
    int px=q8(x[0]+nx[0]*b.offset),py=q8(y[0]+ny[0]*b.offset),pk=(k[0]*b.k)>>8;
    for(int i=1;i<n;++i) {
      const int qx=q8(x[i]+nx[i]*b.offset),qy=q8(y[i]+ny[i]*b.offset),qk=(k[i]*b.k)>>8;
      inkLine(px,py,qx,qy,b.ink,pk,qk);px=qx;py=qy;pk=qk;
    }
    lit((px+128)>>8,(py+128)>>8,b.ink,pk);
  }
}
// A flame tongue or wave curl: from (x,y) heading `angle`, its steps
// shortening and turning by `hook` more toward the tip, so the tip rolls up
// into a curl, the hand-drawn motif of both fire and foam. Width tapers to
// the tip, so two edge bands meet there.
inline void Renderer::tongue(float x,float y,float angle,float length,float width,float hook,int k,const Band *bands,int nb) {
  if(length<1.5f || k<5)return;
  constexpr int N=12;
  float xs[N],ys[N],hw[N];int ks[N];
  const float step=length*(1/(N-1.0f))*1.45f;
  float a=angle;
  for(int i=0;i<N;++i) {
    const float s=i*(1/(N-1.0f));
    xs[i]=x;ys[i]=y;hw[i]=width*(1-s);ks[i]=k;
    const float st=step*(1-.7f*s);x+=slayer::cosineOf(a)*st;y+=slayer::sineOf(a)*st;a+=hook*(.15f+1.7f*s*s);
  }
  brush(xs,ys,hw,ks,N,bands,nb);
}

// A solid circular band, outer radius r, `width` px thick, split into nz
// colour zones from the outside in (flat woodblock fills, no gradient),
// limited to the arc from `from` sweeping `sweep` radians (clockwise on
// screen). Visited row span by row span, only over the rows the arc
// reaches: one multiply-only root per row, integer tests per pixel; the
// spans behind the readout are skipped.
inline void Renderer::band(float cx,float cy,float r,float width,float from,float sweep,const Ink *zones,int nz,int k) {
  if(k<5 || width<.5f || r<2)return;
  const float ri=std::max(0.0f,r-width),r2=r*r,i2=ri*ri;
  // Squared radii in Q4 (quarter pixels squared): zone z covers d2 in (edge[z+1],edge[z]].
  int edge[9];nz=std::min(nz,8);
  for(int z=0;z<=nz;++z){const float rz=r-width*z/nz;edge[z]=int(rz*rz*16);}
  const bool all=sweep>=2*HALF_TURN-.001f,wedge=sweep<HALF_TURN;
  const int ux=q8(cosine(from)),uy=q8(sine(from)),hx=q8(cosine(from+sweep)),hy=q8(sine(from+sweep));
  // The arc's rows: its sine range over the sweep, at the outer radius
  // where it reaches away from the centre row and the inner one where it
  // stays on one side, with a row to spare for the rounded edge vectors.
  int y0=std::max(0,int(cy-r)),y1=std::min(bottom_,int(cy+r)+1);
  if(!all) {
    auto within=[&](float a){float d=a-from;d-=2*HALF_TURN*slayer::floorOf(d*(1/(2*HALF_TURN)));return d<=sweep;};
    const float s0=sine(from),s1=sine(from+sweep);
    const float lo=within(-HALF_TURN/2)?-1:std::min(s0,s1),hi=within(HALF_TURN/2)?1:std::max(s0,s1);
    y0=std::max(y0,floori(cy+(lo<0?r:ri)*lo)-1);y1=std::min(y1,floori(cy+(hi>0?r:ri)*hi)+2);
  }
  // Quarter pixels from the centre, truncated toward it: 4x minus the
  // centre rounded up right of it and rounded down left of it.
  const int split=-slayer::floorOf(-cx),up4=-slayer::floorOf(-cx*4),down4=slayer::floorOf(cx*4);
  const Box &b=number_;
  for(int y=y0;y<=y1;++y) {
    const float dy=y-cy,dy2=dy*dy;if(dy2>=r2)continue;
    const float xo=slayer::root(r2-dy2),xi=i2>dy2?slayer::root(i2-dy2):0;
    const bool behind=y>b.y0 && y<b.y1;
    const int qy=int(dy*4),qy2=qy*qy;
    for(int side=-1;side<=1;side+=2) {
      int x0=int(cx+side*xi),x1=int(cx+side*(xo+1));if(side<0)std::swap(x0,x1);
      x0=std::max(0,x0);x1=std::min(w_-1,x1);
      if(x0>x1)continue;
      // Inside the arc: past the start edge and short of the head (a wedge
      // for sweeps under a half turn, the complement otherwise). Both tests
      // are linear in qx, which rises with x, so a test that holds (or
      // fails) at both ends of the span holds (or fails) all along it.
      bool test=!all;
      if(test) {
        const int qa=4*x0-(x0>=split?up4:down4),qb=4*x1-(x1>=split?up4:down4);
        const bool pa=ux*qy-uy*qa>=0,pb=ux*qy-uy*qb>=0,sa=hx*qy-hy*qa<=0,sb=hx*qy-hy*qb<=0;
        if(wedge) {
          if((!pa && !pb) || (!sa && !sb))continue;
          test=!(pa && pb && sa && sb);
        } else {
          test=!((pa && pb) || (sa && sb));
        }
      }
      for(int x=x0;x<=x1;++x) {
        if(behind && x>=b.x0 && x<=b.x1){x=b.x1;continue;}
        const int qx=4*x-(x>=split?up4:down4),d2=qx*qx+qy2;
        if(d2>edge[0] || d2<=edge[nz])continue;
        if(test) {
          const bool past=ux*qy-uy*qx>=0,shy=hx*qy-hy*qx<=0;
          if(wedge?!(past && shy):(!past && !shy))continue;
        }
        int z=0;while(z<nz-1 && d2<=edge[z+1])++z;
        lit(x,y,zones[z],k);
      }
    }
  }
}

// ---- Sunwheel, after the sun-god fire dance: the blade's tip
// ignites, one flaming arc sweeps a full circle around the readout and
// closes into a swirling ring of fire, its outer edge a crown of curling
// flame tongues; it flares, then keeps turning and breathing while embers
// drift up off it, and finally burns out into rising embers.
inline void Renderer::sunwheel(float t) {
  constexpr float IGNITE=.4f,CLOSE=1.05f,BURN=3.7f,END=4.95f;
  const Rgb crimson=rgb8(225,34,14),orange=rgb8(255,124,20),gold=rgb8(255,206,72);
  const float cx=number_.cx(),cy=number_.cy()+10,R=std::min(47.0f,w_*.35f);
  constexpr float A0=HALF_TURN*.22f;  // below the readout's right end
  // The blade: the head sweeps once round, fast through the bottom.
  const float sweep=smooth((t-IGNITE)/(CLOSE-IGNITE)),head=A0+2*HALF_TURN*sweep;
  // The closed ring keeps turning, slowing from the blade's speed to a drift.
  const float spin=t>CLOSE?.5f*(t-CLOSE)+.8f*(1-std::exp(-1.5f*(t-CLOSE))):0;
  const float burn=1-smooth((t-BURN)/(END-.2f-BURN));
  const float flare=t<CLOSE?0:1-smooth((t-CLOSE)/.3f);
  glint(t,CLOSE+.12f,.45f);
  // Ignition: the blade's tip catches with three small licks, and sparks
  // are drawn in toward it.
  const float sx=cx+R*cosine(A0),sy=cy+R*sine(A0);
  const Band tip[]={{1,inkOf(orange),240},{-1,inkOf(crimson),240}};
  if(t<IGNITE+.08f) {
    const float g=smooth(t/IGNITE)*(t<IGNITE?1:1-(t-IGNITE)/.08f);
    for(int j=0;j<3;++j)tongue(sx-4+4*j,sy+3,-HALF_TURN/2+(j-1)*.4f,(7+5*(j==1))*g*(.8f+.2f*sine(t*23+j*2)),1.8f,(j-1)*.2f-.25f,256,tip,2);
    dot(sx,sy+4,mixed(gold,WHITE,.5f),g*1.3f);
    // The circle the blade will trace, sketched in faint embers.
    for(int i=0;i<52;++i){const float a=A0+i*(2*HALF_TURN/52);plot(cx+(R-.5f)*cosine(a),cy+(R-.5f)*sine(a),mixed(crimson,orange,.3f),.85f*g*(.55f+.45f*sine(t*14+i*1.3f)));}
    for(unsigned i=0;i<10;++i) {
      const float u=clamp01(t/IGNITE-.06f*i),a=2*HALF_TURN*rnd(i,401),r=48*(1-u*u)+2;
      if(u>0 && u<1)dot(sx+r*cosine(a),sy+4+r*.8f*sine(a),i%2?gold:orange,.9f*u);
    }
  }
  if(t<IGNITE)return;
  // Through the sustain, embers drift up off the ring, swaying.
  for(unsigned i=0;i<22;++i) {
    const float age=t-(CLOSE+.25f+2.7f*rnd(i,413)),life=.75f+.4f*rnd(i,414);if(age<0 || age>=life)continue;
    const float a=HALF_TURN*(.05f+.9f*rnd(i,415)),x=cx+R*cosine(a)+3*sine(age*4+i*1.7f),y=cy+R*sine(a)-(10+12*rnd(i,416))*age;
    dot(x,y,i%3?orange:gold,std::min(1.0f,age*6)*(1-age/life)*.85f);
  }
  // Embers lift off the ring as it burns out, outliving it a little.
  for(unsigned i=0;i<18;++i) {
    const float age=t-BURN+.1f-.55f*rnd(i,409),life=.45f+.25f*rnd(i,410);if(age<0 || age>=life)continue;
    const float a=2*HALF_TURN*rnd(i,411),x=cx+R*cosine(a)+4*sine(age*6+i),y=cy+R*sine(a)-(18+16*rnd(i,412))*age;
    dot(x,y,i%2?orange:gold,(1-age/life)*.9f);
  }
  if(burn<=0)return;
  // The ring: a solid band, crimson rims around orange and a gold core,
  // swept behind the head; the closed ring flares white-gold, then thins
  // and fades as it burns out.
  // Settled into the sustain the band is a little narrower and breathes.
  const float settle=smooth((t-CLOSE-.3f)/.6f),breathe=1-.12f*settle*(.5f+.5f*sine((t-CLOSE)*2.4f));
  const float width=(3.8f-.6f*settle)*(.35f+.65f*burn),trail=t<CLOSE?head-A0:2*HALF_TURN;
  const Ink zones[]={inkOf(crimson),inkOf(orange),inkOf(mixed(gold,WHITE,.6f*flare)),inkOf(mixed(crimson,orange,.35f))};
  band(cx,cy,R+1.5f,width,A0,trail,zones,4,q8(std::min(1.0f,.4f+.6f*burn)*std::min(1.0f,burn*3)*(1+.25f*flare)*breathe));
  // The blade's last stretch of arc burns white along the band's middle.
  if(t<CLOSE+.15f) {
    const float hot=t<CLOSE?1:1-(t-CLOSE)/.15f,len=std::min(trail,1.3f);
    for(float s=0;s<len;s+=1/R) {
      const float a=head-s,u=s/len;
      addw(roundi(cx+(R-.5f)*cosine(a)),roundi(cy+(R-.5f)*sine(a)),WHITE_INK,q8(hot*(1-u*u)));
    }
  }
  // The crown: flame tongues stand on the lower half of the ring's outer
  // edge, leaning back against the turn and curling at their tips; they
  // light as the blade passes, ride the turning ring (rising in on one side,
  // dying away on the other) and stretch at the flare.
  const Band flame[]={{1,inkOf(crimson),250},{.33f,inkOf(orange),250},{-.33f,inkOf(gold),230},{-1,inkOf(mixed(crimson,orange,.4f)),250}};
  constexpr int TONGUES=8;
  for(int j=0;j<TONGUES;++j) {
    const float base=A0+(j+.5f)*(2*HALF_TURN/TONGUES)+.12f*rnd(j,402);
    const float lit=t<CLOSE?clamp01((head-base)*(1/.5f)):1,a=base+spin;
    const float low=smooth((sine(a)-.12f)*(1/.35f));
    if(lit*low<=0)continue;
    const float flicker=.8f+.2f*sine(t*(9+3*rnd(j,403))+j*2.1f);
    const float len=(19+7*rnd(j,404))*lit*low*flicker*(1+.25f*flare-.15f*settle)*(.35f+.65f*burn);
    const float bx=cx+(R+.5f)*cosine(a),by=cy+(R+.5f)*sine(a);
    tongue(bx,by,a-.25f,len,2.4f,-.3f,q8(std::min(1.0f,.6f+.4f*burn)*std::min(1.0f,burn*3)),flame,4);
  }
  // The blade's head: a white-hot point riding the arc.
  if(t<CLOSE+.05f)spark(cx+R*cosine(head),cy+R*sine(head),gold,1.25f*(t<CLOSE?1:1-(t-CLOSE)/.05f),2.2f);
  // The flare throws sparks off the ring.
  for(unsigned i=0;i<12;++i) {
    const float age=t-CLOSE-.04f*rnd(i,405),life=.4f+.25f*rnd(i,406);if(age<0 || age>=life)continue;
    const float a=2*HALF_TURN*rnd(i,407),d=R+4+(30+40*rnd(i,408))*easeOut(age/life);
    const float x=cx+d*cosine(a),y=cy+d*sine(a)+30*age*age;
    dot(x,y,i%3?gold:mixed(gold,accent_,.5f),1-age/life);
  }
}

// ---- Torrent, after the water-breathing dragon: ripples ring
// out at the left edge, then a water serpent, drawn as a chain of solid
// half rings in flat woodblock colours (a foam rim, cyan, blue, a deep
// belly), swims in under the readout, its body a travelling S. At the
// right it rears into a great curling crest with clawed foam fingers,
// throwing spray over the readout. The crest sinks back into the stream,
// which keeps flowing under the readout, humps of uneven size passing,
// foam curling off them and drops leaping, until it thins away.
inline void Renderer::torrent(float t) {
  constexpr float SWIM=.4f,BREAK=1.15f,SINK=2.1f,EBB=3.8f,END=4.9f,FLOW=13;
  const Rgb foam=rgb8(232,250,255),cyan=rgb8(110,205,255),blue=rgb8(36,118,255),deep=rgb8(30,86,235);
  const float r=std::min(11.0f,w_*.085f),line=number_.y1+20.0f,end=w_-r*1.6f;
  const float ebb=1-smooth((t-EBB)/(END-EBB));
  // Foam always on the serpent's back: the outer rim of a hump, the inner rim of a dip.
  const Ink zones[]={inkOf(foam),inkOf(cyan),inkOf(blue),inkOf(blue),inkOf(deep)},under[]={inkOf(deep),inkOf(blue),inkOf(blue),inkOf(cyan),inkOf(foam)};
  const Ink rimmed[]={inkOf(foam),inkOf(foam),inkOf(cyan),inkOf(blue)};
  // Ripples ring out at the left edge, where the serpent will come in.
  if(t<SWIM+.3f)for(int ring=0;ring<3;++ring) {
    const float u=(t-.11f*ring)/.5f;if(u<=0 || u>=1)continue;
    const float rad=3+15*easeOut(u);
    ellipseAt(4,line,rad*.4f,rad,mixed(cyan,accent_,.3f),.9f*(1-u),2.4f);
  }
  // The head runs in from the left edge, fast then slowing as it rears up;
  // after the break the whole stream keeps flowing right at a walk.
  const float run=t<SWIM?0:easeOut((t-SWIM)/(BREAK-SWIM)),flow=t<BREAK?0:FLOW*(t-BREAK);
  const float head=-2*r+(end+2*r)*run+flow;
  // Once the crest has sunk the stream runs on to the screen's edge.
  const float limit=t<SINK?end-r*.5f:end-r*.5f+(w_+r-end)*smooth((t-SINK)/.5f);
  const float calm=smooth((t-BREAK-.1f)/.5f);  // the sustained stream is slimmer and cheaper
  if(t>=SWIM && ebb>0) {
    // Hump k from the head: its own radius, centres spaced by the radii.
    float x=head;
    for(int k=0;k<12;++k) {
      const float rk=r*(1-calm*(.18f-.3f*rnd(k,428))),xc=x-rk;x-=2*rk;
      if(xc-rk>limit)continue;
      if(xc+rk<0)break;
      // Five humps swim in; behind them more join from the left edge.
      if(k>=5 && t<BREAK)break;
      const float taper=t<BREAK?1-.13f*k:1,width=(4.6f*taper-1.2f*calm)*(.3f+.7f*ebb);
      const bool up=k%2==0;
      band(xc,line,rk,width,up?HALF_TURN:0,HALF_TURN,up?zones:under,5,q8((t<BREAK?1-.08f*k:.92f)*std::min(1.0f,ebb*2)));
      // Foam curls off a hump's top from time to time.
      if(up && t>BREAK+.3f) {
        const float cycle=t*.7f+rnd(k,429)*3.0f,phase=cycle-1.6f*slayer::floorOf(cycle*(1/1.6f)),g=phase<.8f?sine(phase*(HALF_TURN/.8f)):0;
        if(g>.05f){const Band curl[]={{1,inkOf(foam),255},{-1,inkOf(cyan),200}};
          tongue(xc+rk*.3f,line-rk-1,-HALF_TURN*.8f,(4+3*rnd(k,430))*g*ebb,1.1f,-.6f,q8(g*ebb),curl,2);}
      }
    }
  }
  // Foam fingers run back along the top of the first hump.
  if(t>=SWIM && t<BREAK+.1f) {
    const Band claw[]={{1,inkOf(foam),255},{-1,inkOf(cyan),200}};
    const float k=t<BREAK?1:1-(t-BREAK)/.1f;
    for(int j=0;j<3;++j){const float a=-HALF_TURN*(.25f+.22f*j),x=head-r+(r+1)*cosine(a),y=line+(r+1)*sine(a);
      tongue(x,y,a-1.1f,(5+2*j)*k,1.2f,.6f,q8(k),claw,2);}
  }
  glint(t,BREAK-.02f,.45f);
  if(t<BREAK-.12f)return;
  // The crest: the head rears up into a thick curl rolling over to the
  // left, toward the readout, its rim clawed with foam; then it sinks.
  const float b=t-BREAK+.12f,grow=easeOut(b/.3f),roll=.5f*std::max(0.0f,t-BREAK),sink=1-smooth((t-SINK+.3f)/.6f);
  const float rad=(r*.6f+r*.72f*grow)*(.45f+.55f*sink),ccx=end-r*.3f,ccy=number_.y1+5+rad;
  if(sink>0) {
    band(ccx,ccy,rad,(3+2.6f*grow)*(.5f+.5f*sink),HALF_TURN*(1.6f-.7f*grow)-roll,HALF_TURN*(.7f+.7f*grow),rimmed,4,q8(sink));
    const Band claw[]={{1,inkOf(foam),255},{-1,inkOf(cyan),210}};
    for(int j=0;j<5;++j) {
      const float a=HALF_TURN*(.9f+.14f*j)-roll,len=(4+3*rnd(j,422))*grow*sink;
      tongue(ccx+(rad+.5f)*cosine(a),ccy+(rad+.5f)*sine(a),a-.4f,len,1.1f,-.7f,q8(sink),claw,2);
    }
  }
  if(t<BREAK)return;
  // Spray thrown up from the crest over the readout, falling back.
  for(unsigned i=0;i<30;++i) {
    const float a=t-BREAK-.08f*rnd(i,423);if(a<=0 || a>.62f)continue;
    const float vx=-30-130*rnd(i,424),vy=-(60+120*rnd(i,425)),x=ccx+vx*a,y=ccy-rad+vy*a+230*a*a;
    const float k=1-smooth((a-.25f)/.35f);if(k<=0 || y>h_)continue;
    const Rgb c=i%3?cyan:(i%2?foam:mixed(cyan,accent_,.5f));
    plot(x-vx*.02f,y-(vy+460*a)*.02f,c,k*.6f);dot(x,y,c,k*1.3f);
  }
  // Through the sustain, drops leap from the stream and fall back.
  for(unsigned i=0;i<16;++i) {
    const float a=t-(SINK+2.1f*rnd(i,426)),life=.55f;if(a<0 || a>=life)continue;
    const float x=8+(w_-16)*rnd(i,427)+12*a,y=line-r-2-(70-130*a)*a;
    dot(x,y,i%3?cyan:(i%2?foam:mixed(cyan,accent_,.5f)),.9f*(1-smooth((a-.35f)/.2f))*ebb);
  }
}

// ---- Blitz, after the thunder-breathing first form: a long
// stillness at the left edge, the sheathed blade crackling with ever more
// yellow sparks, a held breath, then one blinding dash straight across the
// screen under the readout: a white core wrapped in flat, angular yellow
// lightning with forks. The trail re-flashes once with the clap, then
// hangs as a live filament: it crackles, pulses of charge run along it and
// sparks drift off it, until it flickers out.
inline void Renderer::blitz(float t) {
  constexpr float HOLD=.62f,DASH=.8f,CROSS=.1f,CLAP=1.02f,FADE=3.6f,END=4.65f;
  const Rgb yellow=rgb8(255,226,40),amber=rgb8(255,160,24),pale=rgb8(255,248,190);
  const float y=number_.y1+9.5f,x0=5;
  const uint32_t epoch=uint32_t(t*13);  // the crackle redraws 13 times a second
  // The stance: the blade lies sheathed at the left edge, crackling, quiet
  // for a breath before the dash.
  if(t<DASH) {
    const float charge=smooth(t/HOLD),breath=t<HOLD?0:smooth((t-HOLD)/(DASH-HOLD));
    line(x0-3,y,x0+14,y,mixed(amber,pale,breath),.35f+.6f*charge);
    line(x0-3,y+1,x0+14,y+1,amber,.25f+.4f*charge);
    if(t<HOLD)for(unsigned j=0;j<3;++j) {
      const uint32_t seed=epoch*7+j;if(rnd(seed,431)>.35f+.6f*charge)continue;
      float xs[4],ys[4],hw[4];int ks[4];
      const float ax=x0-2+18*rnd(seed,432),ay=y+(rnd(seed,433)-.5f)*14*charge;
      for(int i=0;i<4;++i){xs[i]=ax+(i-1.5f)*3.5f;ys[i]=ay+(i%2?2.5f:-2.5f)*(.5f+rnd(seed*4+i,434));hw[i]=.5f;ks[i]=q8(.6f+.4f*charge);}
      const Band spark[]={{0,inkOf(yellow),255}};
      brush(xs,ys,hw,ks,4,spark,1);
    }
    return;
  }
  const float d=t-DASH,head=-4+(w_+8)*clamp01(d/CROSS);
  const float core=d<CROSS?1:1-smooth((d-CROSS)/.25f);
  const float clap=t<CLAP?0:1-smooth((t-CLAP)/.18f);
  const float hang=1-smooth((t-FADE)/(END-FADE)),live=smooth((t-1.3f)/.4f);
  glint(t,DASH+.03f,.4f);
  // The core: a white bar along the dash, two pixels with a yellow skin.
  if(core>0) {
    const Ink white=WHITE_INK,skin=inkOf(yellow),glow=inkOf(amber);
    const int kq=q8(core);
    for(int x=0;x<=std::min(w_-1,int(head));++x) {
      const int u=d<CROSS?std::max(0,std::min(256,int((head-x)*8))):256;  // the head's leading edge
      const int k=(kq*std::min(256,64+u))>>8;
      lit(x,int(y),white,k);lit(x,int(y)+1,white,k);
      lit(x,int(y)-1,skin,k);lit(x,int(y)+2,skin,(k*200)>>8);
      if(x&1){lit(x,int(y)-2,glow,k>>1);lit(x,int(y)+3,glow,k>>1);}
    }
  }
  // The lightning: two flat angular strands wrapped around the dash, each
  // a zigzag redrawn with the crackle, shrinking as it hangs; forks break
  // off at some corners. They flash again with the clap.
  const float bright=std::max(core,clap)*.6f+(.4f+.25f*live)*hang,amp=(4+4*std::max(core,clap))*(1-.4f*live)*(.45f+.55f*hang);
  // Pulses of charge run along the filament now and then.
  float pulse=-1000;
  for(int m=0;m<3;++m){const float at=1.55f+.85f*m+.3f*rnd(m,449);if(t>=at && t<at+.6f)pulse=(t-at)*(w_+40)/.6f-20;}
  if(hang>0)for(unsigned s=0;s<(live<1?2u:1u);++s) {
    constexpr int N=26;float xs[N],ys[N],hw[N];int ks[N];int n=0;
    const uint32_t seed=epoch*3+s*101;
    // Irregular corners: long and short legs, the side mostly alternating.
    float x=-3-6*rnd(seed,435),side=s?1:-1;
    for(int i=0;i<N && x<=head+2;++i) {
      const uint32_t k=seed*N+i;
      const float boost=std::max(0.0f,1-std::fabs(x-pulse)*(1/14.0f));
      xs[n]=x;ys[n]=y+.5f+side*amp*(.25f+.75f*rnd(k,436))*(1+boost);hw[n]=.55f;
      ks[n]=q8((bright*(t<DASH+CROSS?1:.6f+.4f*rnd(k,437))+.7f*boost*hang)*(s?1-live:1));++n;
      x+=4+11*rnd(k,447);if(rnd(k,448)<.8f)side=-side;
    }
    const Band strand[]={{-1,inkOf(yellow),255},{1,inkOf(pale),235}},thin[]={{0,inkOf(amber),255}};
    if(s==0)brush(xs,ys,hw,ks,n,strand,2);else brush(xs,ys,hw,ks,n,thin,1);
    for(int i=2;i<n-1;i+=3) {
      if(rnd(seed*N+i,438)<.45f+.4f*live)continue;
      const float up=ys[i]<y?-1:1,len=(5+8*rnd(seed*N+i,439))*(.5f+.5f*bright);
      float fx[3]={xs[i],xs[i]+len*.5f,xs[i]+len*.6f+3},fy[3]={ys[i],ys[i]+up*len*.6f,ys[i]+up*len};
      float fw[3]={.5f,.5f,.5f};int fk[3]={ks[i],ks[i],ks[i]>>1};
      const Band fork[]={{0,inkOf(yellow),230}};
      brush(fx,fy,fw,fk,3,fork,1);
    }
  }
  // The landing: sparks spray back off the right edge, and embers of the
  // charge drift along the trail.
  for(unsigned i=0;i<20;++i) {
    const float a=d-CROSS*.8f-.05f*rnd(i,440),life=.45f+.35f*rnd(i,441);if(a<0 || a>=life)continue;
    const float vx=-(40+150*rnd(i,442)),vy=(rnd(i,443)-.5f)*130,x=w_-3+vx*a,yy=y+vy*a+60*a*a,k=1-a/life;
    dot(x,yy,i%3?yellow:mixed(pale,accent_,.4f),k);
  }
  for(unsigned i=0;i<30;++i) {
    const float a=t-(CLAP+.1f+2.9f*rnd(i,444)),life=.5f+.3f*rnd(i,450);if(a<0 || a>=life)continue;
    twinkle(8+(w_-16)*rnd(i,445),y+(rnd(i,446)-.5f)*16+(i%2?-12:9)*a,i%2?yellow:pale,(1-a/life)*.8f,1);
  }
}
// ---- Tigerclaw, after the flame-breathing tiger: embers rise
// and three claw tips glint at the top of the screen, then a tiger's swipe
// rakes three crescent claw marks of fire down through the readout, each a
// solid woodblock stroke (a crimson outer rim, orange, a gold heart) cut
// by dark wedge-shaped tiger stripes. The marks keep burning, slimmer,
// their stripes crawling, flames licking and curling off them and embers
// floating up, then burn down to nothing from their tips.
inline void Renderer::tigerclaw(float t) {
  constexpr float RAKE=.5f,STAGGER=.06f,LONG=.17f,STRUCK=RAKE+2*STAGGER+LONG,BURN=3.7f,END=5.05f;
  const Rgb crimson=rgb8(215,30,10),orange=rgb8(255,120,16),gold=rgb8(255,212,80);
  const float cx=number_.cx(),top=-6,bottom=number_.y1+70.0f,span=bottom-top;
  const float burn=1-smooth((t-BURN)/(END-.15f-BURN)),flare=t<STRUCK?0:1-smooth((t-STRUCK)/.3f);
  glint(t,STRUCK+.1f,.45f);
  // Claw j: centre line x(y), slanting down to the left with a slight bow.
  const float perRow=1/span;
  auto clawX=[&](int j,float y){const float v=(y-top)*perRow;return cx+20+(j-1)*16.0f-42*v+7*(1-(2*v-1)*(2*v-1));};
  // Before the swipe: embers drift up toward the claw tips, which glint.
  if(t<RAKE+.05f) {
    const float g=smooth(t/RAKE);
    for(unsigned i=0;i<14;++i) {
      const float u=clamp01(t/RAKE-.05f*(i%5));if(u<=0 || u>=1)continue;
      const float x=8+(w_-16)*rnd(i,451)+5*sine(t*5+i),y=h_-20-(h_-60)*u*(.6f+.4f*rnd(i,452));
      dot(x,y,i%2?orange:gold,.85f*u);
    }
    for(int j=0;j<3;++j)twinkle(clawX(j,top+8),top+8,gold,g*(.6f+.4f*sine(t*20+j*2)),2);
  }
  if(t<RAKE || burn<=0)return;
  const Box &b=number_;
  // The marks: each row of each claw is one span with flat zones across it.
  const Ink rim=inkOf(crimson),mid=inkOf(orange),heart=inkOf(mixed(gold,WHITE,.6f*flare)),hot=WHITE_INK;
  const int kq=q8((.5f+.5f*burn)*std::min(1.0f,burn*3));
  // Per claw: its rows, tip and width this frame. The rows are then visited
  // once for all three claws, so the row's profile and bow are computed once;
  // on the device a float division, sqrt, floor or fmod is a library call,
  // so the row and pixel steps use none.
  int lo[3],hi[3],first=bottom_+1,last=-1;float reach[3],thick[3];bool rising[3];
  for(int j=0;j<3;++j) {
    const float u=(t-RAKE-STAGGER*j)*(1/LONG);
    lo[j]=0;hi[j]=-1;if(u<=0)continue;
    reach[j]=top+span*easeOut(std::min(1.0f,u));rising[j]=u<1;
    // Burning down from both tips: the stroke keeps its middle longest.
    const float shrink=.5f*(1-burn),from=top+span*shrink*.9f,to=std::min(reach[j],bottom-span*shrink);
    const float calm=smooth((t-STRUCK-.3f)/.8f);
    thick[j]=(4.8f+1.2f*rnd(j,453))*(1-.28f*calm)*(.55f+.45f*burn);
    lo[j]=std::max(0,int(from));hi[j]=std::min(bottom_,int(to));
    first=std::min(first,lo[j]);last=std::max(last,hi[j]);
  }
  const int drift=q8(t*3.5f+1100);
  for(int y=first;y<=last;++y) {
    if(y>b.y0 && y<b.y1)continue;  // behind the readout: masked anyway
    const float v=(y-top)*perRow,s=slayer::sineOf(HALF_TURN*v);
    if(s<=0)continue;
    const float profile=slayer::root(s),centre=cx+20-42*v+7*(1-(2*v-1)*(2*v-1));
    for(int j=0;j<3;++j) {
      if(y<lo[j] || y>hi[j])continue;
      const float width=thick[j]*profile;
      if(width<.6f)continue;
      const float xc=centre+(j-1)*16.0f,xl=xc-width*.5f;
      const int x0=std::max(0,slayer::floorOf(xl)),x1=std::min(w_-1,int(xc+width*.5f));
      const bool tip=rising[j] && reach[j]-y<5;
      // Tiger stripes: dark wedges every 11 rows, slanting, entering from
      // alternate edges and narrowing toward the other. In Q8, offset by
      // 100 periods so it stays positive: the period and the phase in it.
      const int along=(y<<8)+q8(.45f*(xc-cx))+drift,band=along/(11*256),phase=((along-band*11*256)<<8)/(11*256);
      const bool fromLeft=band&1;
      const int xl8=q8(xl),inv=(1<<24)/q8(width);  // across the span in Q8, fixed point
      for(int x=x0;x<=x1;++x) {
        const int f=std::max(0,std::min(255,((x*256-xl8)*inv)>>16));
        const int depth=fromLeft?255-f:f;
        if(phase<(depth*80>>8)-12)continue;  // inside a stripe
        const Ink &c=tip?hot:f<70||f>200?rim:f<120||f>165?mid:heart;
        lit(x,y,c,tip?256:kq);
      }
    }
  }
  if(t<STRUCK-.05f)return;
  // Flames lick off the marks' right edges and curl back over them.
  const Band flame[]={{1,inkOf(crimson),250},{-1,inkOf(gold),250}};
  for(int i=0;i<5;++i) {
    const int j=i%3;const float v=.45f+.4f*((i/3)+.5f*rnd(i,454))/2;
    const float y=top+span*v,x=clawX(j,y)+2.5f;
    const float grow=smooth((t-STRUCK+.05f)/.25f),flicker=.75f+.25f*sine(t*(10+4*rnd(i,455))+i*1.9f);
    const float len=(9+6*rnd(i,456))*grow*flicker*(.3f+.7f*burn)*(1+.2f*flare);
    tongue(x,y,-HALF_TURN*.38f,len,1.4f,-.34f,q8(.6f+.4f*burn),flame,2);
  }
  // The strike throws sparks; the burning marks shed rising embers.
  for(unsigned i=0;i<10;++i) {
    const float a=t-STRUCK+.04f-.06f*rnd(i,457),life=.35f+.25f*rnd(i,458);if(a<0 || a>=life)continue;
    const int j=i%3;const float y0=top+span*(.35f+.6f*rnd(i,459)),x0=clawX(j,y0);
    const float vx=-50+100*rnd(i,460),vy=-40-60*rnd(i,461);
    dot(x0+vx*a,y0+vy*a+90*a*a,i%3?gold:mixed(gold,accent_,.5f),1-a/life);
  }
  for(unsigned i=0;i<32;++i) {
    const float a=t-(STRUCK+.2f+3.0f*rnd(i,462)),life=.6f+.3f*rnd(i,464);if(a<0 || a>=life)continue;
    const int j=i%3;const float y0=top+span*(.4f+.55f*rnd(i,463));
    dot(clawX(j,y0)+3*sine(a*7+i),y0-30*a,i%2?orange:gold,(1-a/life)*.9f);
  }
}
// ---- Butterfly, after the insect-breathing butterfly dance:
// violet butterflies, woodblock-flat (a pale rim around a violet fill,
// teal at the wing tips), flutter in from the screen's edges and hover
// around the readout. Then the dance: four needle-thin stings dart in, one
// after another, each leaving a wisteria hexagon blooming where it struck.
// The butterflies dance on around the readout in slow, wandering loops,
// shedding glittering dust, then take off together and rise away.
inline void Renderer::butterfly(float t) {
  constexpr float ARRIVE=.85f,STING=.92f,GAP=.13f,STUNG=STING+3*GAP,DANCE=1.45f,LIFT=3.55f,END=4.9f;
  constexpr int FLIES=5;
  const Rgb violet=rgb8(150,70,255),lilac=rgb8(236,212,255),teal=rgb8(70,232,205),wisteria=rgb8(205,160,255);
  const float cx=number_.cx(),y1=number_.y1;
  const Ink fill=inkOf(mixed(violet,accent_,.15f)),rim=inkOf(lilac);
  // Hover places around the readout's lower edge and sides.
  static constexpr float HOME[FLIES][2]={{-47,13},{46,17},{-22,33},{23,37},{0,16}};
  glint(t,STUNG+.02f,.45f);
  // Butterfly i's flight at time tt: in from an edge, a bobbing hover, the
  // dance's wandering loops (incommensurate rates, so no loop repeats), and
  // the take-off, up and outward, faster and faster.
  auto fly=[&](int i,float tt,float &x,float &y) {
    const float side=i%2?1:-1,start=.06f*i,u=smooth((tt-start)/(ARRIVE-start));
    const float ox=side*(w_*.5f+12),oy=50+70*rnd(i,471),dance=smooth((tt-DANCE)/.6f);
    x=cx+HOME[i][0]+ox*(1-u)+6*(1-u)*sine(tt*7+i*2)+3*sine(tt*2.3f+i*2.9f)+dance*(10+5*rnd(i,474))*sine(tt*(1.1f+.3f*rnd(i,475))+i*1.9f);
    y=y1+HOME[i][1]+oy*(1-u)+2.5f*sine(tt*3.1f+i*1.7f)+dance*(5+3*rnd(i,476))*sine(tt*(1.7f+.4f*rnd(i,477))+i*1.1f);
    const float lift=tt<LIFT?0:tt-LIFT;
    if(lift>0){x+=side*(25+20*rnd(i,472))*lift*lift;y-=(60+60*rnd(i,473))*lift*lift+16*lift;}
    return u;
  };
  const float fade=1-smooth((t-LIFT-.6f)/(END-LIFT-.6f));
  for(int i=0;i<FLIES;++i) {
    float x,y;const float u=fly(i,t,x,y);
    // The dance: butterfly i stings when its turn comes, darting to the
    // readout's edge and back along a needle.
    if(i<4) {
      const float a=t-STING-GAP*i;
      if(a>=0 && a<.12f) {
        const float tx=cx+(HOME[i][0]*.5f),ty=y1+7,k=1-a/.12f,dart=a<.04f?a/.04f:1-(a-.04f)/.08f;
        line(x,y,tx,ty,mixed(lilac,WHITE,.6f),k*1.3f);
        x+=(tx-x)*dart*.85f;y+=(ty-y)*dart*.85f;
        twinkle(tx,ty,WHITE,k*1.3f,3);
      }
      // The wisteria hexagon blooms where the sting struck.
      if(a>=0 && a<.55f) {
        const float tx=cx+(HOME[i][0]*.5f),ty=y1+7,r=3+10*easeOut(a/.55f),k=(1-a/.55f)*1.2f;
        for(int e=0;e<6;++e){const float a0=e*HALF_TURN/3+HALF_TURN/6,a1=a0+HALF_TURN/3;
          line(tx+r*cosine(a0),ty+r*sine(a0)*.75f,tx+r*cosine(a1),ty+r*sine(a1)*.75f,mixed(wisteria,WHITE,.25f),k);}
      }
    }
    if(fade<=0 || u<=0 || y<-12)continue;
    // Wings: open factor from the flap, faster in flight than in the hover.
    const float rate=t>LIFT?14:u<1?11:t>DANCE?8:6.5f,flap=.25f+.75f*std::fabs(cosine(t*rate+i*1.3f));
    const int kq=q8(fade*std::min(1.0f,u*3));
    for(int wing=0;wing<4;++wing) {
      // Upper wings sweep up and out, lower ones hang down and out; the
      // flap narrows them across the body.
      const bool upper=wing<2;const float sgn=wing%2?1:-1;
      const float len=upper?5.6f:3.8f,wid=upper?3.4f:2.4f,tilt=upper?-.65f:.75f;
      const float ca=cosine(tilt),sa=sine(tilt);
      const float ax=sgn*ca*flap,ay=sa;  // the wing's long axis, foreshortened by the flap
      const float wx=x+ax*len*.95f,wy=y+ay*len*.95f+(upper?-.5f:1.5f);
      const float alen=std::max(.8f,len*std::sqrt(ax*ax+ay*ay)),inv=1/std::sqrt(ax*ax+ay*ay);
      const float ux=ax*inv,uy=ay*inv,ia=1/(alen*alen),ib=1/(wid*wid*(.5f+.5f*flap)*(.5f+.5f*flap));
      const int r=int(std::max(alen,wid)+1);
      for(int py=int(wy)-r;py<=int(wy)+r;++py)for(int px=int(wx)-r;px<=int(wx)+r;++px) {
        const float dx=px-wx,dy=py-wy,along=dx*ux+dy*uy,across=-dx*uy+dy*ux,d=along*along*ia+across*across*ib;
        if(d>1)continue;
        const bool tipZone=upper && along>alen*.35f;
        lit(px,py,d>.5f?(tipZone?inkOf(teal):rim):fill,kq);
      }
    }
    // The body: a pale stroke between the wings.
    for(int d=-3;d<=3;++d)plot(x,y+d,lilac,fade*(d==3?.6f:1));
    // Dust: glitter shed along the flight path, drifting down as it fades.
    if(t>DANCE)for(int d=1;d<=4;++d) {
      const float back=d*(t>LIFT?.045f:.11f);float px,py;fly(i,t-back,px,py);
      dot(px+2*sine(t*9+d*2.3f+i),py+4+9*back,d%2?wisteria:mixed(lilac,accent_,.4f),fade*smooth((t-DANCE)/.3f)*(1-d*.2f));
    }
  }
}

inline void Renderer::slayer(unsigned which,float t) {
  switch(which) {
    case 0:sunwheel(t);break;
    case 1:torrent(t);break;
    case 2:blitz(t);break;
    case 3:tigerclaw(t);break;
    case 4:butterfly(t);break;
    default:break;
  }
}
}
