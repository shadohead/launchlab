#pragma once
// The armory set of 0.18 (Swordland..Trace): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Same rules as
// the other sets: additive light on the background outside the UI halo, a
// pure function of elapsed time with fixed seeds, the readout exactly as
// drawn from three quarters of the duration, every frame within the cost
// model's 8 ms.
namespace BestEffectUI {

inline void Renderer::armory(unsigned which,float t) {
  switch(which) {
    case 0:armorySwordland(t);break;
    case 1:armoryTreasury(t);break;
    case 2:armorySunblade(t);break;
    case 3:armoryAegis(t);break;
    case 4:armoryTrace(t);break;
    default:break;
  }
}

// A pixel of light below the readout: skipped off the screen and on the
// header's rows before it costs a probe or a mask scan, so a shape reaching
// behind the readout (a gear's upper half) costs only its visible part.
inline void Renderer::armoryPlot(int x,int y,const Ink &c,int kq) {
  if(unsigned(x)>=unsigned(w_) || y<=number_.y1+HALO || y>bottom_)return;
  addw(x,y,c,kq);
}
// Steps a single-pixel path from (lx,ly) to (x,y): each new pixel is lit
// once, gaps are bridged and a repeated pixel is skipped, so a closed
// outline sampled by angle has no doubled or missing pixels.
inline void Renderer::armoryTo(int &lx,int &ly,int x,int y,const Ink &c,int kq) {
  const int dx=x-lx,dy=y-ly,n=std::max(std::abs(dx),std::abs(dy));
  if(n==0)return;
  // A step lying wholly where armoryPlot draws nothing (the header rows,
  // below the bottom, off either side) only moves the path: the great
  // gear behind the readout stepped its upper half point by point for
  // nothing.
  if(std::max(ly,y)<=number_.y1+HALO || std::min(ly,y)>bottom_ || std::max(lx,x)<0 || std::min(lx,x)>=w_){lx=x;ly=y;return;}
  touches_+=uint32_t(n);
  if(n>1) {
    // Fixed-point steps in Q16, one integer division per call: a float
    // division per pixel is a library call on the device.
    const int sx=(dx*65536)/n,sy=(dy*65536)/n;int ax=lx*65536+32768,ay=ly*65536+32768;
    for(int i=1;i<n;++i){ax+=sx;ay+=sy;armoryPlot(ax>>16,ay>>16,c,kq);}
  }
  armoryPlot(x,y,c,kq);
  lx=x;ly=y;
}
// A gear: a rim with square teeth standing out of it, a hub and four
// spokes, turned by `phase`. Circles are sampled by an
// incrementally rotated vector (one table lookup each) and each tooth is
// three short radial strokes, so it reads as a solid block.
inline void Renderer::armoryGear(float cx,float cy,float r,int teeth,float phase,const Ink &rim,const Ink &hub,int kq,bool spokes) {
  if(kq<5)return;
  const float out=r+5;
  if(cx+out<0 || cx-out>=w_ || cy+out<number_.y1+HALO || cy-out>bottom_)return;
  auto circle=[&](float rad,const Ink &c,int k) {
    const int n=std::max(12,int(2*HALF_TURN*rad*1.4f));
    const float step=2*HALF_TURN/n,cs=cosine(step),sn=sine(step);
    float ux=1,uy=0;int fx=roundi(cx+rad),fy=roundi(cy),lx=fx,ly=fy;
    for(int i=1;i<n;++i) {
      const float nx=ux*cs-uy*sn;uy=ux*sn+uy*cs;ux=nx;
      armoryTo(lx,ly,roundi(cx+ux*rad),roundi(cy+uy*rad),c,k);
    }
    armoryTo(lx,ly,fx,fy,c,k);
  };
  // A two-pixel rim.
  circle(r,rim,kq);circle(r-1.2f,rim,(kq*5)>>3);
  const float pitch=2*HALF_TURN/teeth,cs=cosine(pitch),sn=sine(pitch);
  float ux=cosine(phase),uy=sine(phase);
  for(int i=0;i<teeth;++i) {
    for(int s=0;s<3;++s) {
      const float o=s-1.0f,x0=cx+ux*(r+1)-uy*o,y0=cy+uy*(r+1)+ux*o;
      int lx=roundi(x0),ly=roundi(y0);armoryPlot(lx,ly,rim,kq);armoryTo(lx,ly,roundi(x0+ux*3),roundi(y0+uy*3),rim,kq);
    }
    const float nx=ux*cs-uy*sn;uy=ux*sn+uy*cs;ux=nx;
  }
  if(!spokes)return;
  const float hr=std::max(2.0f,r*.22f);
  circle(hr,hub,kq);
  for(int s=0;s<4;++s) {
    const float a=phase+(s+.5f)*(2*HALF_TURN/4),ca=cosine(a),sa=sine(a),r0=hr+1.2f,r1=r-1.2f;
    if(r1-r0<1.5f)continue;
    const int k=(kq*5)>>3;int lx=roundi(cx+ca*r0),ly=roundi(cy+sa*r0);armoryPlot(lx,ly,rim,k);
    armoryTo(lx,ly,roundi(cx+ca*r1),roundi(cy+sa*r1),rim,k);
  }
}
// A sword planted tip down at (x,y), leaning `angle` from upright: a blade
// (a second, darker edge line on the nearer, larger ones), a crossguard,
// a grip and a pommel. depth 0 is far and small, 1 near. `hide` px of
// the blade are still under the ground as it rises.
inline void Renderer::armorySword(float x,float y,float angle,float height,float depth,float hide,const Ink &steel,const Ink &edge,const Ink &back,const Ink &hilt,int kq) {
  const float blade=height*.7f-hide,grip=height*.18f,gw=2+2.6f*depth;
  if(kq<5 || blade<1)return;
  const float ux=sine(angle),uy=-cosine(angle),px=-uy,py=ux;
  const float gx=x+ux*blade,gy=y+uy*blade;
  // A three-pixel blade: the steel core between a bright edge and a dark
  // back, both starting a little above the point so the tip tapers.
  int lx=roundi(x),ly=roundi(y);armoryPlot(lx,ly,steel,kq>>1);
  armoryTo(lx,ly,roundi(gx-ux),roundi(gy-uy),steel,kq);
  for(int s=-1;s<=1;s+=2){const float o=s*1.0f,from=hide>0?0:2.5f;
    lx=roundi(x+ux*from+px*o);ly=roundi(y+uy*from+py*o);armoryPlot(lx,ly,s>0?edge:back,kq>>2);
    armoryTo(lx,ly,roundi(gx-ux*1.5f+px*o),roundi(gy-uy*1.5f+py*o),s>0?edge:back,s>0?(kq*3)>>2:(kq*3)>>3);}
  // A two-row crossguard.
  for(int r=0;r<2;++r){lx=roundi(gx-px*gw+ux*r);ly=roundi(gy-py*gw+uy*r);armoryPlot(lx,ly,hilt,kq);armoryTo(lx,ly,roundi(gx+px*gw+ux*r),roundi(gy+py*gw+uy*r),hilt,r?(kq*3)>>2:kq);}
  lx=roundi(gx+ux*2);ly=roundi(gy+uy*2);armoryPlot(lx,ly,hilt,(kq*5)>>3);armoryTo(lx,ly,roundi(gx+ux*grip),roundi(gy+uy*grip),hilt,(kq*5)>>3);
  armoryPlot(roundi(gx+ux*(grip+1.3f)),roundi(gy+uy*(grip+1.3f)),hilt,kq);
}

// ---- Swordland: a spark catches on the ground below the readout and a
// ring of fire sweeps out across it in perspective; behind its front a
// field of swords rises out of the ground, planted at every angle and
// flashing hot as the fire passes, a horizon lights and bronze gears turn
// in the sky above it (one great wheel behind the readout). Sustain: the
// gears keep turning slowly, glints run up one blade after another and
// embers rise off the field. Settle: the swords sink back into the ground
// as the gears and the horizon fade.
inline void Renderer::armorySwordland(float t) {
  constexpr float IGNITE=.3f,SWEEP=.75f,SETTLE=3.75f;
  constexpr int SWORDS=7;
  const Box &b=number_;const float cx=b.cx();
  const float yh=std::min(float(bottom_-44),b.y1+25.0f),gy=yh+14;
  const Rgb steelC=rgb8(205,216,232),rust=rgb8(170,66,28),brass=rgb8(204,118,52),ember=rgb8(255,176,64),hotC=rgb8(255,232,180);
  glint(t,.6f,.45f);
  // The fire front: its radius on the ground, an ellipse squashed to 0.3.
  const float sweep=t<IGNITE?0:easeOut((t-IGNITE)/SWEEP),R=3+80*sweep;
  const float ringK=smooth((t-IGNITE+.06f)/.08f)*(1-smooth((t-IGNITE-SWEEP+.3f)/.32f));
  const int frame=floori(t*20+.001f);
  // Gears: they come out of the dark in the fire's light, kick round and
  // keep turning slowly; the angle is a closed form, so it is continuous.
  const float gearK=smooth((t-1.06f)/.5f)*(1-smooth((t-SETTLE-.15f)/.8f));
  if(gearK>.02f) {
    const float turn=.32f*t+1.3f*easeOut(std::max(0.0f,t-IGNITE)/1.6f);
    const Ink rim=inkOf(brass),hub=inkOf(mixed(brass,accent_,.25f));
    const int k=q8(.6f*gearK);
    armoryGear(cx+10,b.y1-9,27,15,-.42f*turn,rim,hub,q8(.55f*gearK),true);
    armoryGear(w_+1,yh-6,19,11,-turn*1.0f,rim,hub,k,true);
  }
  // The horizon, lit from the centre out as the front passes, then a
  // shimmering line of heat haze that dies with the fire.
  // It goes out with the fire.
  const float horizonK=smooth((t-IGNITE)/.2f)*(1-smooth((t-IGNITE-SWEEP-.1f)/.5f));
  if(horizonK>.02f) {
    const int y=roundi(yh);
    for(int x=0;x<w_;++x) {
      ++touches_;
      const float d=std::fabs(x-cx);if(d>R)continue;
      const float front=std::max(0.0f,1-(R-d)*(1/16.0f))*ringK;
      // Dotted once the front has passed: a heat shimmer, at half the cost
      // (tested before the haze's noise, which a skipped pixel never needs).
      if(front<.05f && ((x+frame)&1))continue;
      const float haze=.7f+.3f*(noise(x*28,frame*40,401)*(1/255.0f));
      armoryPlot(x,y,inkOf(mixed(rust,ember,front)),q8(horizonK*(.42f*haze+.6f*front)));
    }
  }
  // The field: each sword rises out of the ground as the front reaches it,
  // flashes hot and cools to steel; far ones are smaller, higher and hazier.
  const int glintSlot=floori((t-1.35f)/.32f);
  const float glintAge=t-1.35f-glintSlot*.32f;
  const int glintSword=t>=1.35f && t<3.65f?int(hash(uint32_t(glintSlot)*7+3)%SWORDS):-1;
  // Placed by hand across the field: x as a fraction of the width, the
  // planting depth (0 at the horizon, 1 nearest) and the lean in radians.
  // The nearest, largest ones stand at the screen's edges, clear of the replay.
  static constexpr float FIELD[SWORDS][3]={{.07f,1,.2f},{.93f,.95f,-.22f},{.24f,.55f,.16f},{.77f,.6f,-.2f},
    {.38f,.18f,-.12f},{.62f,.24f,.22f},{.5f,0,-.3f}};
  for(int j=0;j<SWORDS;++j) {
    const float depth=FIELD[j][1],x=FIELD[j][0]*(w_-1),angle=FIELD[j][2];
    const float g=yh+3+30*depth,height=22+24*depth;
    const float dx=x-cx,dz=(g-gy)*2,dist=std::sqrt(dx*dx+dz*dz);
    const float risen=std::min(clamp01((R-dist)*(1/18.0f)),1-smooth((t-SETTLE-.55f*rnd(j,414))/.45f));
    if(risen<=0)continue;
    const float e=easeOut(risen),hide=(1-e)*height*.6f;
    if(hide>height*.6f)continue;
    const float hot=R>dist?clamp01(1-(R-dist)*(1/40.0f))*ringK:0;
    const Rgb steel=mixed(mixed(steelC,rust,.55f*(1-depth)),hotC,hot);
    const float ux=sine(angle),uy=-cosine(angle);
    const float k=(.62f+.38f*depth)*std::min(1.0f,e*1.6f);
    // The blade rises along itself: draw from the ground point up.
    armorySword(x,g,angle,height,depth,hide,inkOf(steel),inkOf(mixed(mixed(steel,WHITE,.4f),accent_,.3f)),inkOf(mixed(steel,rust,.6f)),inkOf(mixed(brass,hotC,hot)),q8(k));
    if(j==glintSword && glintAge<.3f) {
      const float u=glintAge/.3f,along=(height*.7f-hide)*u;
      twinkle(x+ux*along,g+uy*along,WHITE,.9f*sine(HALF_TURN*u),1);
    }
  }
  // The spark that catches, and the ring of fire with flame tongues.
  if(t<IGNITE+.05f) {
    const float s=clamp01(t/IGNITE);
    twinkle(cx,gy,mixed(ember,hotC,s),.4f+.7f*s,1+int(2*s));
  }
  if(ringK>.01f) {
    const int n=std::min(180,int(R*2.8f)+24);
    const float step=2*HALF_TURN/n,cs=cosine(step),sn=sine(step);
    float ux=1,uy=0;int lx=roundi(cx+R),ly=roundi(gy);
    for(int i=1;i<=n;++i) {
      {const float nx=ux*cs-uy*sn;uy=ux*sn+uy*cs;ux=nx;}
      const float x=cx+R*ux,y=gy+.26f*R*uy,nearK=.5f+.5f*uy;const int ix=roundi(x),iy=roundi(y);
      if(x<-1 || x>w_ || y>bottom_){lx=ix;ly=iy;continue;}
      armoryTo(lx,ly,ix,iy,fire(220),q8(ringK*(.55f+.45f*nearK)));
      if(i%3)continue;
      const int h=int((2+7*noise(i*70,frame*90,402)*(1/255.0f))*(.45f+.55f*nearK)*std::min(1.0f,R*(1/20.0f)));
      // Heat and weight fall linearly up the tongue: integer steps, no division per pixel.
      const int heat0=210,dh=150/(h+1),k0=q8(ringK*.85f),dk=q8(ringK*.6f)/(h+1);
      for(int d=1;d<=h;++d)armoryPlot(ix,iy-d,fire(heat0-d*dh),k0-d*dk);
    }
  }
  // Embers rise off the field and wink out under the readout.
  const float emberK=smooth((t-.6f)/.4f)*(1-smooth((t-4.3f)/.6f));
  if(emberK>0)for(unsigned i=0;i<18;++i) {
    const float birth=.55f+3.4f*rnd(i,421),life=1.2f+1.0f*rnd(i,422),age=t-birth;
    if(age<0 || age>=life)continue;
    const float u=age/life,x=4+(w_-8)*rnd(i,423)+4*sine(age*2.3f+i)+6*(rnd(i,424)-.5f)*age;
    const float y=yh+4+22*rnd(i,425)-(13+15*rnd(i,426))*age;
    const float k=emberK*sine(HALF_TURN*u)*(.65f+.35f*sine(t*21+i*2.1f));
    const Ink c=inkOf(mixed(ember,rgb8(210,52,12),u));
    armoryPlot(roundi(x),roundi(y),c,q8(k*1.1f));armoryPlot(roundi(x),roundi(y)+1,c,q8(k*.4f));
  }
}

// A closed ellipse as a single-pixel path.
inline void Renderer::armoryRing(float cx,float cy,float rx,float ry,const Ink &c,int kq) {
  if(kq<5 || rx<.6f)return;
  const int n=std::max(8,int(2*HALF_TURN*std::max(rx,ry)*1.4f));
  const float step=2*HALF_TURN/n,cs=cosine(step),sn=sine(step);
  float ux=1,uy=0;const int fx=roundi(cx+rx),fy=roundi(cy);int lx=fx,ly=fy;
  for(int i=1;i<n;++i){const float nx=ux*cs-uy*sn;uy=ux*sn+uy*cs;ux=nx;armoryTo(lx,ly,roundi(cx+ux*rx),roundi(cy+uy*ry),c,kq);}
  armoryTo(lx,ly,fx,fy,c,kq);
}
// A weapon coming out of a portal at (px,py) along the unit direction
// (dx,dy), its tip `out` px in front of the portal: only the part in front
// of the portal is drawn, and the first `from` px of the tip are skipped
// (buried in the ground). kind 0 is a long sword three pixels wide, 1 a
// spear, 2 a broad short blade five pixels wide.
inline void Renderer::armoryWeapon(float px,float py,float dx,float dy,float out,float from,float len,int kind,const Ink &steel,const Ink &gold,int kq) {
  if(kq<5)return;
  const float tx=px+dx*out,ty=py+dy*out,nx=-dy,ny=dx,shown=std::min(len,out);
  auto seg=[&](float s0,float s1,float off,const Ink &c,int k) {
    s0=std::max(s0,from);s1=std::min(s1,shown);if(s1-s0<.5f)return;
    int lx=roundi(tx-dx*s0+nx*off),ly=roundi(ty-dy*s0+ny*off);armoryPlot(lx,ly,c,k);
    armoryTo(lx,ly,roundi(tx-dx*s1+nx*off),roundi(ty-dy*s1+ny*off),c,k);
  };
  auto cross=[&](float s,float half,const Ink &c,int k) {
    if(s<from || s>shown)return;
    int lx=roundi(tx-dx*s-nx*half),ly=roundi(ty-dy*s-ny*half);armoryPlot(lx,ly,c,k);
    armoryTo(lx,ly,roundi(tx-dx*s+nx*half),roundi(ty-dy*s+ny*half),c,k);
  };
  const int dim=(kq*5)>>3;
  if(kind==1) {
    // Spear: a leaf-shaped head, a gold collar and a long shaft.
    seg(0,7,0,steel,kq);seg(1.5f,5.8f,1,steel,dim);seg(1.5f,5.8f,-1,steel,dim);
    cross(7.5f,2,gold,kq);seg(8.5f,len,0,gold,(kq*3)>>2);seg(8.5f,len-1,1,gold,dim>>1);
  } else {
    const float blade=len*(kind==2?.6f:.66f),half=kind==2?2.6f:3.2f;
    // Three pixels wide: a steel core and two edges.
    seg(0,blade,0,steel,kq);seg(2,blade-.5f,1,steel,dim);seg(2.5f,blade-.5f,-1,steel,dim);
    if(kind==2){seg(4,blade-.5f,2,steel,dim>>1);seg(4,blade-.5f,-2,steel,dim>>1);}
    cross(blade+.5f,half,gold,kq);cross(blade+1.5f,half-.5f,gold,dim);seg(blade+2.5f,len*.9f,0,gold,dim);
    if(len*.95f<=shown && len*.95f>=from)armoryPlot(roundi(tx-dx*len*.95f),roundi(ty-dy*len*.95f),gold,kq);
  }
}

// ---- Treasury: golden ripples open in the air under the readout, one
// after another from the centre out, and weapons (swords, spears, broad
// blades) slide out of them tip first. Then the volley: each launches in
// turn, streaks down and strikes the ground with a burst of gold sparks,
// stays stuck a moment and dissolves into gold dust. Sustain: the ripples
// keep spreading and a second rank slides half out and hangs there,
// trembling, while motes of gold are drawn into the gates. Settle: the
// weapons withdraw and the ripples close.
inline void Renderer::armoryTreasury(float t) {
  constexpr int GATES=5;
  constexpr float EMERGE=.24f,VOLLEY=.8f,STEP=.1f,FLIGHT=.1f,SECOND=1.6f,WITHDRAW=3.6f,CLOSE=4.15f;
  const Box &b=number_;const float cx=b.cx();
  const Rgb gold=rgb8(255,192,58),pale=rgb8(255,238,170),steelC=rgb8(216,226,242);
  const Ink goldI=inkOf(gold),paleI=inkOf(pale),steelI=inkOf(steelC),waveI=inkOf(mixed(gold,accent_,.25f));
  // The header gleams as the first rank stands aimed, before the volley.
  glint(t,.48f,.4f);
  // Gates open from the centre outward; rank r of gate i.
  static constexpr int RANK[GATES]={3,1,0,2,4};
  static constexpr float LEN[3]={34,42,27};
  for(int i=0;i<GATES;++i) {
    const int r=RANK[i];
    // A wall of gates: three high, two low between them.
    const float px=20+i*(w_-41)*.25f,py=b.y1+(i&1?23:12);
    const float open=smooth((t-.05f*r)/.2f)*(1-smooth((t-CLOSE-.05f*r)/.4f));
    if(open<=0)continue;
    // The ripple: two rings and a wave spreading out of them every 0.7 s
    // (the first, as the gate opens, bright).
    const float pop=1-smooth((t-.05f*r)/.3f),rx=10.5f*open*(1+.25f*pop),ry=rx*.42f;
    // A bright rim round a soft gold glow that is brightest at the heart.
    armoryRing(px,py,rx,ry,goldI,q8(open*(.75f+.25f*pop)));
    // Its rows and columns are visited once, with the squared distance
    // in Q8 stepped by multiplies: no root or division per pixel.
    if(rx>1){const float gx=rx*.64f,gy=ry*.64f,igx=1/gx,igy=1/gy;const int k0=q8(open*(.55f+.45f*pop));
      for(int y=roundi(py-gy);y<=roundi(py+gy);++y){const float fy=(y-py)*igy;const int fy2=q8(fy*fy);if(fy2>=256)continue;
        for(int x=roundi(px-gx);x<=roundi(px+gx);++x){const float fx=(x-px)*igx;const int d=fy2+q8(fx*fx);if(d<256)armoryPlot(x,y,mixedInk(goldI,paleI,256-d),(k0*(256-d))>>8);}}}
    // The first wave as the gate opens; later ones every second, each gate
    // out of step with the others so at most a few spread at once. No waves
    // during the volley: the strikes carry it.
    const float age=t-.05f*r,later=t-SECOND-.21f*r,wu=age<.7f?age*(1/.7f):later-floori(later);
    if(wu<.6f && (age<.7f || later>0))armoryRing(px,py,rx*(.62f+.9f*wu),ry*(.62f+.9f*wu),waveI,q8(open*(age<.7f?.85f:.5f)*(1-wu*(1/.6f))));
    // The direction from the gate to its strike point, a little outward.
    const float tx=px+(px-cx)*.45f,ty=b.y1+61+8*rnd(i,431);
    float dx=tx-px,dy=ty-py;const float dl=std::sqrt(dx*dx+dy*dy);dx/=dl;dy/=dl;
    const int kind=i%3;const float len=LEN[kind];
    const float launch=VOLLEY+STEP*r,u=(t-launch)/FLIGHT;
    if(u<0) {
      // The first rank slides out tip first and trembles, aimed.
      const float e=easeOut((t-EMERGE-.05f*r)/.3f);
      if(e>0){const float j=e>=1?.5f*sine(t*45+i*1.7f):0;armoryWeapon(px+dx*j,py+dy*j,dx,dy,len*e,0,len,kind,steelI,goldI,q8(.95f));}
    } else if(u<1) {
      // In flight: the whole weapon streaks to its mark, a trail behind it.
      const float travel=(dl-len+3)*u*u,ox=px+dx*travel,oy=py+dy*travel;
      armoryWeapon(ox,oy,dx,dy,len,0,len,kind,inkOf(mixed(steelC,pale,.5f)),goldI,256);
      // A short trail behind it, brightest at the weapon.
      const int n=std::max(1,int(travel)),s0=std::max(1,n-16);int lx=roundi(px+dx*s0),ly=roundi(py+dy*s0);
      const float trail=.8f/(n-s0+1);
      for(int s=s0+1;s<=n;++s)armoryTo(lx,ly,roundi(px+dx*s),roundi(py+dy*s),goldI,q8(trail*(s-s0)));
    } else {
      // Stuck: the tip buried 3 px; a flash ring, sparks, then gold dust.
      const float a=t-launch-FLIGHT,fade=1-smooth((a-.08f)/.2f);
      const float sx=tx+dx*3,sy=ty+dy*3,ox=sx-dx*(dl+3),oy=sy-dy*(dl+3);
      if(fade>0)armoryWeapon(ox,oy,dx,dy,dl+3,3,len,kind,inkOf(mixed(pale,steelC,smooth(a/.2f))),goldI,q8(fade));
      if(a<.1f){const float tail=dl-len+3;const int s0=std::max(1,int(tail)-14);int lx=roundi(px+dx*s0),ly=roundi(py+dy*s0);
        const float fade=.6f*(1-a*10)/(tail-s0);
        for(int s=s0+1;s<=int(tail);++s)armoryTo(lx,ly,roundi(px+dx*s),roundi(py+dy*s),goldI,q8(fade*(s-s0)));}
      if(a<.22f)armoryRing(tx,ty+1,2+7*easeOut(a/.22f),(2+7*easeOut(a/.22f))*.35f,paleI,q8(1-a/.22f));
      for(unsigned k=0;k<5;++k) {
        const float life=.3f+.25f*rnd(i*8+k,432),sa=a-.02f*k;if(sa<0 || sa>=life)continue;
        const float an=HALF_TURN*(1.05f+.9f*rnd(i*8+k,433)),v=40+60*rnd(i*8+k,434);
        const float x=tx+cosine(an)*v*sa,y=ty+sine(an)*v*sa*.7f+60*sa*sa;
        dot(x,y,k%2?gold:pale,1.2f*(1-sa/life));
      }
      for(unsigned k=0;k<5;++k) {
        const float sa=a-.2f-.08f*k,life=.6f;if(sa<0 || sa>=life)continue;
        const float along=len*rnd(i*8+k,435);
        plot(sx-dx*along+2*sine(sa*7+k),sy-dy*along-14*sa,gold,.9f*sine(HALF_TURN*sa/life));
      }
    }
    // The second rank: half out, hanging and trembling, then withdrawn.
    const float e2=easeOut((t-SECOND-.05f*r)/.4f)*(1-smooth((t-WITHDRAW-.05f*(6-r))/.45f));
    if(e2>0) {
      const int kind2=(i+1)%3;const float len2=LEN[kind2],j=.6f*sine(t*(9+2*rnd(i,436))+i*2.3f);
      armoryWeapon(px+dx*j,py+dy*j,dx,dy,len2*.72f*e2,0,len2,kind2,steelI,goldI,q8(.85f));
      // A gleam slides down the blade now and then.
      const float g=(t-2.0f-.29f*i)/2.1f,gu=(g-floori(g))*2.1f;
      if(t>2.0f && gu<.3f){const float s=len2*.72f*e2*(1-gu/.3f);twinkle(px+dx*s,py+dy*s,WHITE,.8f*sine(HALF_TURN*gu/.3f),1);}
    }
  }
  // Motes of gold drift in and vanish into the gates.
  const float moteK=smooth((t-1.5f)/.4f)*(1-smooth((t-WITHDRAW)/.5f));
  if(moteK>0)for(unsigned m=0;m<10;++m) {
    const float life=.9f+.4f*rnd(m,437),clock=t+rnd(m,438)*3,u=clock/life-floori(clock/life);
    const unsigned cyc=unsigned(clock/life),g=(m+cyc*3)%GATES;
    const float px=20+g*(w_-41)*.25f,py=b.y1+(g&1?23:12);
    const float an=2*HALF_TURN*rnd(m*31+cyc,439)+u*2,d=16*(1-u);
    const float x=px+cosine(an)*d,y=py+sine(an)*d*.6f;
    if(y>b.y1+HALO+1)plot(x,y,u<.5f?pale:gold,moteK*sine(HALF_TURN*u)*.9f);
  }
}

// A single-pixel straight stroke (below the readout's rows).
inline void Renderer::armoryStroke(float x0,float y0,float x1,float y1,const Ink &c,int kq) {
  if(kq<5)return;
  int lx=roundi(x0),ly=roundi(y0);armoryPlot(lx,ly,c,kq);armoryTo(lx,ly,roundi(x1),roundi(y1),c,kq);
}
// An arc of radius r round (cx,cy) between two angles measured clockwise
// from straight up, as a single-pixel path.
inline void Renderer::armoryArc(float cx,float cy,float r,float from,float to,const Ink &c,int kq) {
  if(kq<5 || r<1 || to<=from)return;
  // About one sample per pixel of arc; armoryTo bridges any gap.
  const int n=std::max(2,int((to-from)*r*1.05f));
  const float step=(to-from)/n,cs=cosine(step),sn=sine(step);
  float ux=sine(from),uy=-cosine(from);
  int lx=roundi(cx+ux*r),ly=roundi(cy+uy*r);armoryPlot(lx,ly,c,kq);
  for(int i=1;i<=n;++i){const float nx=ux*cs-uy*sn;uy=ux*sn+uy*cs;ux=nx;armoryTo(lx,ly,roundi(cx+ux*r),roundi(cy+uy*r),c,kq);}
}

// ---- Sunblade: motes of gold spiral in from all round and gather into a
// tall blade of light standing on a hilt low at the left edge, which grows
// and pulses as it charges. It draws back, then swings across in one
// stroke, leaving a broad fan of light behind it (ribs from the hilt and a
// bright rim), and a wave of light is released along the fan and sweeps
// up through the screen, heating the header's glyphs gold as it crosses
// them. Sustain: the light settles as gold dust drifting down through the
// fan while the blade, dimmed, eases back and breathes with motes
// rising off it. Settle: the blade and the dust fade.
inline void Renderer::armorySunblade(float t) {
  constexpr float CHARGE=.85f,BACK=1.0f,SWING=1.13f,REST=2.3f,FADE=3.7f;
  // The hilt stands low at the left edge, so the raised blade runs up the
  // clear column between the replay's tilt arrow and its cube rather than
  // through the cube's lines; it is drawn back a little, swung over to the
  // right and raised again, so its fan spans the screen under the readout.
  constexpr float AHOLD=0,A0=-.1f,A1=1.32f,AREST=0;
  const Box &b=number_;
  const float py=std::min(float(bottom_-6),b.y1+100.0f),len=std::min(88.0f,py-b.y1-12);
  const Rgb gold=rgb8(255,198,64),pale=rgb8(255,240,186),deep=rgb8(232,132,26);
  const Ink goldI=inkOf(gold),paleI=inkOf(pale),deepI=inkOf(deep),edgeI=inkOf(mixed(deep,accent_,.12f));
  // The blade's angle from upright: still while charging, drawn back,
  // swung across fast, then (dimmed) brought back upright.
  float angle=AHOLD;
  if(t>=CHARGE && t<BACK)angle=AHOLD+(A0-AHOLD)*smooth((t-CHARGE)/(BACK-CHARGE));
  else if(t>=BACK && t<SWING){const float u=(t-BACK)/(SWING-BACK);angle=A0+(A1-A0)*u*u*(3-2*u);}
  else if(t>=SWING)angle=A1+(AREST-A1)*smooth((t-SWING-.45f)/(REST-SWING-.45f));
  const float grow=easeOut(t/.7f),charge=smooth(t/CHARGE);
  // Brightness: charging up with a quickening pulse, full in the swing,
  // dimmed to a breathing glow, then gone.
  float bladeK=t<CHARGE?(.6f+.4f*charge)*(.82f+.18f*sine(t*(14+30*charge))):t<SWING+.08f?1:
    .7f+.3f*(1-smooth((t-SWING-.08f)/.35f));
  if(t>REST)bladeK*=.85f+.15f*sine((t-REST)*5);
  bladeK*=1-smooth((t-FADE)/.7f);
  const float da=sine(angle),dc=-cosine(angle),nx=-dc,ny=da;
  const float hx=std::min(15.0f,b.x0+10.0f),hy=py,reach=len*grow;
  if(bladeK>.02f) {
    // Core, gold edges and a soft outer glow, tapering to the tip.
    const int k=q8(bladeK);
    // Warm while charging and resting, white-hot in the swing.
    const float hot=t<CHARGE?.35f+.5f*charge:t<SWING+.08f?1:.3f*(1-smooth((t-SWING-.08f)/.3f));
    const Ink core=inkOf(mixed(gold,WHITE,hot)),inner=inkOf(mixed(gold,pale,.3f+.7f*hot));
    armoryStroke(hx+da*3,hy+dc*3,hx+da*reach,hy+dc*reach,core,k);
    for(int side=-1;side<=1;side+=2) {
      // A three-pixel blade: a bright core between two gold rows, warm
      // edges outside them, and a soft outer glow only while it is hot.
      armoryStroke(hx+da*3+nx*side,hy+dc*3+ny*side,hx+da*(reach-2)+nx*side,hy+dc*(reach-2)+ny*side,inner,(k*7)>>3);
      armoryStroke(hx+da*4+nx*2*side,hy+dc*4+ny*2*side,hx+da*(reach-6)+nx*2*side,hy+dc*(reach-6)+ny*2*side,inkOf(mixed(deep,gold,.4f+.6f*hot)),int(k*(.35f+.3f*std::min(1.0f,hot*2))));
      if(hot>.3f && t<SWING)armoryStroke(hx+da*6+nx*3*side,hy+dc*6+ny*3*side,hx+da*(reach-14)+nx*3*side,hy+dc*(reach-14)+ny*3*side,edgeI,int(k*hot*.35f));
    }
    // The crossguard and grip.
    armoryStroke(hx-nx*7,hy-ny*7,hx+nx*7,hy+ny*7,goldI,k);armoryStroke(hx-nx*6-da,hy-ny*6-dc,hx+nx*6-da,hy+ny*6-dc,deepI,(k*3)>>2);
    armoryStroke(hx-da*2,hy-dc*2,hx-da*7,hy-dc*7,deepI,(k*3)>>2);
    plot(hx-da*8.5f,hy-dc*8.5f,gold,bladeK);
  }
  // The gathering: motes spiral in from all round onto the blade.
  if(t<CHARGE+.1f)for(unsigned i=0;i<34;++i) {
    const float birth=.55f*rnd(i,441),u=(t-birth)/.42f;if(u<0 || u>=1)continue;
    const float sx=4+(w_-8)*rnd(i,442),sy=b.y1+8+(py-b.y1-8)*rnd(i,443),along=reach*(.15f+.85f*rnd(i,444));
    const float ex=hx+da*along,ey=hy+dc*along,e=u*u,sw=(1-e)*1.4f*(i%2?1:-1),c=cosine(sw),sn=sine(sw);
    const float rx=(sx-ex)*(1-e),ry=(sy-ey)*(1-e);
    dot(ex+rx*c-ry*sn,ey+rx*sn+ry*c,i%3?gold:pale,.95f*sine(HALF_TURN*u)+.2f);
  }
  // The fan: ribs near the rim at the angles the blade has swept, the
  // newest brightest, gone as the wave leaves, and a bright rim along the
  // tip's path that fades more slowly.
  const float fanK=t<BACK?0:1-smooth((t-SWING)/.4f);
  if(fanK>.02f) {
    const float swept=t<SWING?angle:A1;
    for(int j=0;j<=6;++j) {
      const float a=A0+(A1-A0)*j*(1/6.0f);if(a>swept+.01f)break;
      const float fresh=1-.55f*clamp01((swept-a)*(1/1.6f)),ra=sine(a),rc=-cosine(a);
      // Dotted ribs, fading in toward the rim.
      const int kq=q8(fanK*fresh*.95f*(1-smooth((t-SWING)/.14f)));if(kq<5)break;
      for(float r=len-30+(j&1);r<=len+2;r+=2)armoryPlot(roundi(hx+ra*r),roundi(hy+rc*r),j%2?goldI:paleI,int(kq*(r-len+34)*(1/36.0f)));
    }
    armoryArc(hx,hy,len+5,A0,swept,paleI,q8(fanK));
    armoryArc(hx,hy,len+4,A0,swept,goldI,q8(fanK*.8f));
    // Its warm inner row only while the blade is still sweeping.
    if(t<SWING+.02f)armoryArc(hx,hy,len+2,A0,swept,deepI,q8(fanK*.45f));
  }
  // The wave: released at the end of the swing, it races out along the
  // fan and up through the header, heating the glyphs it crosses.
  const float wa=t-SWING+.03f;
  if(wa>0 && wa<.45f) {
    const float front=len+6+300*wa,k=1-smooth((wa-.25f)/.2f);
    armoryArc(hx,hy,front,A0-.12f,A1+.12f,WHITE_INK,q8(k));
    // Its gold trailing edge thins away as it reaches the header.
    armoryArc(hx,hy,front-2.5f,A0-.12f,A1+.12f,goldI,q8(.8f*k*(1-smooth((front-len-6)*(1/14.0f)))));
    // Glyphs inside the band are pulled toward pale gold (brighten only).
    // Each row visits only its one or two runs inside the band (from two
    // exact integer square roots) instead of every pixel of the readout:
    // the per-pixel loop cost about a millisecond on the device.
    const int lo=int((front-3.5f)*(front-3.5f)),hi=int((front+.5f)*(front+.5f)),k8=q8(.85f*k);
    const int cx=roundi(hx),cy=roundi(hy);
    // The largest a with a*a<=v (v>=0), from a float root corrected exactly.
    auto isqrt=[this](int v){int a=int(std::sqrt(float(v)));++roots_;while(a*a>v)--a;while((a+1)*(a+1)<=v)++a;return a;};
    for(int y=b.y0;y<=b.y1;++y) {
      const int dy=y-cy,dy2=dy*dy;if(dy2>hi)continue;
      // |dx| from `inner` (the smallest with dx*dx+dy2>=lo) to `outer`.
      const int outer=isqrt(hi-dy2),need=lo-dy2;
      int inner=0;if(need>0){inner=isqrt(need);if(inner*inner<need)++inner;}
      if(inner>outer)continue;
      // Left of the centre column, then from it (or past the inner edge) rightward.
      for(int x=std::max(b.x0,cx-outer),x1=std::min(b.x1,cx-std::max(inner,1));x<=x1;++x)sorcHeat(x,y,paleI,k8);
      for(int x=std::max(b.x0,cx+inner),x1=std::min(b.x1,cx+outer);x<=x1;++x)sorcHeat(x,y,paleI,k8);
    }
  }
  glint(t,SWING+.18f,.4f);
  // Gold dust settles through the fan; motes rise off the resting blade.
  const float dustK=smooth((t-SWING)/.3f)*(1-smooth((t-4.1f)/.7f));
  if(dustK>0)for(unsigned i=0;i<34;++i) {
    const float birth=SWING+.05f+2.6f*rnd(i,445),life=1.3f+.8f*rnd(i,446),age=t-birth;
    if(age<0 || age>=life)continue;
    const float a=A0+(A1-A0)*rnd(i,447),r=26+(len-20)*rnd(i,448),u=age/life;
    const float x=hx+sine(a)*r+3*sine(age*2.5f+i),y=hy-cosine(a)*r+(9+8*rnd(i,449))*age;
    const float k=dustK*sine(HALF_TURN*u)*(.6f+.4f*sine(t*19+i*2.3f));
    plot(x,y,i%3?gold:pale,k*1.15f);
  }
  if(t>REST-.3f && bladeK>.02f)for(unsigned i=0;i<8;++i) {
    const float life=1.0f,clock=t+rnd(i,450)*life,u=clock/life-floori(clock/life);
    const float along=len*(.2f+.75f*rnd(i*7+unsigned(clock/life),451));
    plot(hx+da*along+(i%2?3:-3)+u*(i%2?4:-4),hy+dc*along-10*u,pale,bladeK*1.2f*sine(HALF_TURN*u));
  }
}

// The petal's half width along its length, sampled at 32 steps at
// compile time: widest a little past the middle, pointed at both ends.
constexpr std::array<float,33> armoryPetalTable() {
  std::array<float,33> w{};
  for(int i=0;i<=32;++i){const double q=i/32.0;w[i]=float(detail::taylorSine(detail::PI_D*q*(.85+.15*q)-(q>.5?detail::PI_D:0))*(q>.5?-1:1));}
  return w;
}
constexpr std::array<float,33> ARMORY_PETAL=armoryPetalTable();
// A petal of light from (cx,cy) toward `angle` (clockwise from up): two
// edges that meet in a point at each end and a dotted vein; y is squashed
// by `squash` for a flower seen tilted. Steps by the width table: no
// trigonometry or division per sample.
inline void Renderer::armoryPetal(float cx,float cy,float angle,float len,float width,float squash,const Ink &edge,const Ink &vein,int kq,bool veined) {
  if(kq<5 || len<2)return;
  const float dx=sine(angle),dy=-cosine(angle),nx=-dy,ny=dx,du=len*(1/32.0f);
  int lx[2]={roundi(cx),roundi(cx)},ly[2]={roundi(cy),roundi(cy)};armoryPlot(lx[0],ly[0],edge,kq);
  for(int i=1;i<=32;++i) {
    const float u=du*i,w=width*ARMORY_PETAL[i];
    for(int s=0;s<2;++s){const float v=s?w:-w;const int x=roundi(cx+dx*u+nx*v),y=roundi(cy+(dy*u+ny*v)*squash);
      // The two edges meet at the tip: the second does not light it again.
      if(s && i==32 && x==lx[0] && y==ly[0]){lx[1]=x;ly[1]=y;continue;}
      armoryTo(lx[s],ly[s],x,y,edge,kq);}
  }
  if(veined)for(float u=5;u<len-4;u+=2)armoryPlot(roundi(cx+dx*u),roundi(cy+dy*u*squash),vein,kq>>1);
}

// ---- Aegis: a flower of seven petals of pink light blooms under the
// readout, one petal after another round the circle. A crimson spear
// streaks in from the right and strikes its heart; red sparks spray
// while it drills, and petal after petal cracks and shatters into falling
// shards, from the spear's side round the flower. The last petal, at the
// left, holds: it flares, and the spear is thrown back
// and bursts into red sparks. Sustain: the last petal stands and breathes,
// a gleam running round its edge, while shards and motes drift down.
// Settle: it dissolves.
inline void Renderer::armoryAegis(float t) {
  constexpr int PETALS=7;
  // Seen tilted, its heart on the replay cube's top corner (17 px under
  // the readout): squashed to 0.6, the cube's three edges from that corner
  // run out through the gaps between petals, as if behind the flower.
  constexpr float ARRIVE=.78f,BREAK0=.92f,STEP=.15f,REPEL=1.85f,FADE=3.75f,SQ=.6f,LEN=38,WID=12,SPEAR=50;
  const Box &b=number_;const float cx=b.cx(),cy=std::min(float(bottom_-30),b.y1+17.0f);
  const Rgb pink=rgb8(255,118,190),pale=rgb8(255,216,238),violet=mixed(rgb8(186,110,255),accent_,.25f),crimson=rgb8(236,28,46),hot=rgb8(255,176,164);
  const Ink paleI=inkOf(pale),crimsonI=inkOf(crimson),hotI=inkOf(hot);
  const int frame=floori(t*20+.001f);
  // The header gleams as the last petal holds (not in the busy bloom).
  glint(t,REPEL,.4f);
  // Break order: the spear's side first, round to the upper left petal,
  // which holds.
  static constexpr int RANK[PETALS]={4,2,0,1,3,6,5};
  const float hold=t<REPEL?0:1-smooth((t-REPEL-.05f)/.3f);
  for(int i=0;i<PETALS;++i) {
    const float angle=i*(2*HALF_TURN/PETALS),born=.04f+.07f*i,e=easeOut((t-born)/.3f);
    if(e<=0)continue;
    const int r=RANK[i];const float breakAt=BREAK0+STEP*r;
    const float dx=sine(angle),dy=-cosine(angle);
    if(r<6 && t>=breakAt) {
      // Shattered: shards fly outward from the petal and fall.
      const float a=t-breakAt;if(a>=.75f)continue;
      for(unsigned k=0;k<7;++k) {
        const float u=LEN*(.2f+.75f*rnd(i*16+k,461)),side=(rnd(i*16+k,462)-.5f)*2*WID*.7f,life=.45f+.3f*rnd(i*16+k,463);
        if(a>=life)continue;
        const float x0=cx+dx*u-dy*side,y0=cy+(dy*u+dx*side)*SQ,v=30+45*rnd(i*16+k,464);
        const float x=x0+(dx*.8f+(rnd(i*16+k,465)-.5f))*v*a,y=y0+dy*v*a*SQ+70*a*a,kk=1-a/life;
        plot(x,y,k%3?pink:pale,kk*1.2f);plot(x-dx*1.5f,y-dy*1.5f*SQ+1,pink,kk*.5f);
      }
      if(a<.08f)twinkle(cx+dx*LEN*.5f,cy+dy*LEN*.5f*SQ,WHITE,1.2f*(1-a/.08f),3);
      continue;
    }
    // Intact: blooming with a flash, cracking just before it breaks,
    // breathing once it alone holds.
    const float flash=1-smooth((t-born)/.35f),crack=r<6?smooth((t-breakAt+.1f)/.08f):0;
    float k=.8f+.2f*flash;
    if(r==6){k*=1+.5f*hold;if(t>REPEL+.3f)k*=(.85f+.15f*sine((t-REPEL)*4))*(1-smooth((t-FADE)/.8f));}
    if(crack>0)k*=.75f+.25f*sine(t*90+i);
    if(k<.02f)continue;
    const Ink edge=inkOf(mixed(pink,pale,std::max(flash,std::max(crack*.6f,r==6?hold:0.0f))));
    // The last petal swells a little as it holds.
    const float grow=r==6?1+.22f*smooth((t-REPEL)/.3f):1;
    armoryPetal(cx,cy,angle,LEN*e*grow,WID*std::min(1.0f,e*1.3f)*grow,SQ,edge,inkOf(violet),q8(k));
    // Inner edges fading inward: a glowing rim two pixels deep while all
    // seven stand (the device's budget in the bloom and the strike), three
    // deep on the petal that holds once it stands alone.
    // The inner row waits until a petal has unfurled and goes as it cracks.
    if(e>.97f && crack<.5f){armoryPetal(cx+dx*1.5f,cy+dy*1.5f*SQ,angle,(LEN*e-3)*grow,(WID-1.4f)*grow,SQ,inkOf(mixed(pink,pale,.2f)),inkOf(violet),q8(k*.6f),false);
      if(r==6 && t>=REPEL)armoryPetal(cx+dx*3,cy+dy*3*SQ,angle,(LEN*e-6)*grow,(WID-2.8f)*grow,SQ,inkOf(mixed(pink,violet,.5f)),inkOf(violet),q8(k*.32f),false);}
    if(crack>0) {
      // Cracks: three white zigzags spreading from the heart.
      for(int c=0;c<3;++c) {
        const float off=(c-1)*.42f,reach=LEN*(.45f+.45f*crack);
        float px=cx,py=cy;
        for(int seg=1;seg<=4;++seg) {
          const float u=reach*seg*.25f,v=(off+(seg&1?.18f:-.18f))*WID*sine(HALF_TURN*seg*.2f);
          const float qx=cx+dx*u-dy*v,qy=cy+(dy*u+dx*v)*SQ;
          sorcLine(px,py,qx,qy,WHITE_INK,q8(.85f*crack),q8(.85f*crack));px=qx;py=qy;
        }
      }
    }
    // A gleam runs round the last petal's edge through the sustain.
    if(r==6 && t>REPEL+.3f && t<FADE+.5f) {
      const float g=(t-REPEL-.3f)*.8f,q=g-floori(g),side=q<.5f?1:-1,qq=q<.5f?q*2:2-q*2;
      const float u=LEN*qq,w=WID*sine(HALF_TURN*qq*(.85f+.15f*qq))*side;
      twinkle(cx+dx*u-dy*w,cy+(dy*u+dx*w)*SQ,pale,.9f*(1-smooth((t-FADE)/.6f)),1);
    }
  }
  // The heart of the flower.
  const float heartK=smooth(t/.3f)*(1-smooth((t-FADE)/.8f));
  if(heartK>.02f)armoryRing(cx,cy,2.5f,2.5f*SQ+.5f,paleI,q8(heartK));
  // The spear: streaking in, drilling at the heart, then thrown back.
  const float sdx=-(w_+14-cx),sdy=10;const float sl=std::sqrt(sdx*sdx+sdy*sdy),ux=sdx/sl,uy=sdy/sl;
  float tipD=1e9f;  // distance of the tip short of the heart along the path
  if(t>=.5f && t<ARRIVE)tipD=sl*(1-smooth((t-.5f)/(ARRIVE-.5f)));
  else if(t>=ARRIVE && t<REPEL)tipD=-1.2f*std::min(6.0f,(t-ARRIVE)/STEP)+.6f*sine(t*70);
  else if(t>=REPEL && t<REPEL+.3f)tipD=90*(t-REPEL)*(t-REPEL)*11;
  if(tipD<1e8f && t<REPEL+.3f) {
    const float fade=t<REPEL?1:1-(t-REPEL)/.3f;
    const float tx=cx-ux*tipD,ty=cy-uy*tipD,nx=-uy,ny=ux;
    const int k=q8(fade);
    armoryStroke(tx-ux*3,ty-uy*3,tx-ux*SPEAR,ty-uy*SPEAR,crimsonI,k);
    armoryStroke(tx-ux*5+nx,ty-uy*5+ny,tx-ux*(SPEAR-2)+nx,ty-uy*(SPEAR-2)+ny,inkOf(mixed(crimson,hot,.25f)),(k*3)>>2);
    // A red spiral round the shaft and a barbed, hot head.
    for(float u=6;u<SPEAR;u+=2) {
      const float o=1.8f*sine(u*.55f-t*34);
      armoryPlot(roundi(tx-ux*u+nx*o),roundi(ty-uy*u+ny*o),inkOf(mixed(crimson,hot,.3f)),(k*5)>>3);
    }
    armoryStroke(tx,ty,tx-ux*7+nx*3,ty-uy*7+ny*3,hotI,k);armoryStroke(tx,ty,tx-ux*7-nx*3,ty-uy*7-ny*3,hotI,k);
    if(t<ARRIVE){const int n=int(std::min(30.0f,sl-tipD));const float inv=n>0?.6f/n:0;for(int s=0;s<n;s+=2)armoryPlot(roundi(tx-ux*(SPEAR+s)),roundi(ty-uy*(SPEAR+s)),crimsonI,q8(.6f-s*inv));}
  }
  // Sparks spray off the heart while the spear drills, and burst as it is thrown back.
  if(t>=ARRIVE && t<REPEL+.4f) {
    const bool burst=t>=REPEL;
    for(unsigned k=0;k<(burst?18u:6u);++k) {
      const uint32_t seed=burst?k:uint32_t(frame)*8+k;const float born=burst?REPEL:frame*.05f,a=t-born+(burst?0:.05f*rnd(seed,466));
      const float life=.25f+.2f*rnd(seed,467);if(a<0 || a>=life)continue;
      const float an=2*HALF_TURN*rnd(seed,468),v=50+70*rnd(seed,469);
      dot(cx+cosine(an)*v*a,cy+sine(an)*v*a*SQ+40*a*a,k%3?crimson:hot,1.1f*(1-a/life));
    }
  }
  // Motes of pink light drift up off the last petal.
  const float moteK=smooth((t-REPEL)/.4f)*(1-smooth((t-FADE)/.8f));
  if(moteK>0) {
    // The holding petal is the one whose rank is 6.
    int last=0;for(int i=0;i<PETALS;++i)if(RANK[i]==6)last=i;
    const float la=last*(2*HALF_TURN/PETALS),ldx=sine(la),ldy=-cosine(la);
    for(unsigned m=0;m<12;++m) {
      const float life=1.2f+.6f*rnd(m,470),clock=t+rnd(m,471)*life,u=clock/life-floori(clock/life);
      const float along=LEN*(.25f+.65f*rnd(m*7+unsigned(clock/life),472)),across=(rnd(m*5+unsigned(clock/life),473)-.5f)*WID*1.4f;
      const float x=cx+ldx*along-ldy*across+3*sine(u*6+m),y=cy+(ldy*along+ldx*across)*SQ-14*u;
      plot(x,y,m%2?pink:pale,moteK*sine(HALF_TURN*u)*1.2f);
    }
  }
}

// ---- Trace: a pair of short curved blades, laid pommel to pommel in the
// band under the readout, is drawn as a glowing blue wireframe: faint construction lines first, then
// a bright pen traces each outline, the guard, the grip, the pommel ring
// and the fuller, line by line, marking each corner. Then both fill in
// solid from hilt to tip behind a white front, one dark and one pale, each
// shaded across its width, the dark one with a red shell-like lattice.
// Motes fall off them as they fill. Sustain: they ease apart and back, glints run along their edges and sparks crawl over the
// wireframe. Settle: the fill drains back into the wireframe and the lines
// fade out.
inline void Renderer::armoryTrace(float t) {
  constexpr float GUIDE=.05f,PEN=.22f,DRAWN=1.15f,FILL=1.22f,FILLED=1.42f,DRAIN=3.7f,DRAINED=4.15f,END=4.75f;
  constexpr float L=64,BLADE0=17,S=1.3f;
  // The blade's two sides across its length q (0 at the guard, 1 at the
  // clipped point): the cutting edge swells toward the tip; the back stays
  // straight, and the blade bends a little toward its back. Scaled by S.
  static constexpr float Q[7]={0,.25f,.5f,.7f,.85f,.95f,1},EDGE[7]={2.5f,3.6f,4.6f,5,4.4f,2.6f,.6f},BACK[7]={-2.5f,-2.4f,-2.1f,-1.6f,-1,-.2f,.6f};
  // Reciprocals of the knot spacing: no division per lookup.
  static constexpr float QI[6]={4,4,5,1/.15f,10,20};
  auto side=[](const float *k,float q){int i=0;while(i<5 && q>Q[i+1])++i;const float f=clamp01((q-Q[i])*QI[i]);return S*(k[i]+(k[i+1]-k[i])*f);};
  // Segment length without a root: the larger leg plus 0.41 of the smaller
  // (within 8%), which only paces the pen.
  auto span=[](float a,float b){a=std::fabs(a);b=std::fabs(b);return a>b?a+.41f*b:b+.41f*a;};
  auto bend=[](float q){return -2.2f*q*q;};
  const Box &b=number_;const float cx=b.cx(),cy=std::min(float(bottom_-40),b.y1+12.0f);
  const Rgb blue=mixed(rgb8(80,186,255),accent_,.3f),penC=rgb8(225,248,255);
  const Ink blueI=inkOf(blue),dimI=inkOf(mixed(blue,rgb8(40,80,160),.5f)),penI=inkOf(penC);
  const float wire=smooth((t-GUIDE)/.2f)*(t<DRAIN?1:1-smooth((t-DRAINED)/(END-DRAINED-.1f)));
  glint(t,FILL+.05f,.4f);
  const float drawn=(t-PEN)/(DRAWN-PEN),fillFront=t<FILL?-1:t<DRAIN?L*easeOut((t-FILL)/(FILLED-FILL)):L*(1-smooth((t-DRAIN)/(DRAINED-DRAIN)));
  const int frame=floori(t*20+.001f);
  for(int blade=0;blade<2;++blade) {
    // Laid flat in the clear band under the readout, pommel to pommel at
    // its centre, the dark one pointing left and the pale one right, both
    // edges up; in the sustain they ease apart and back.
    const float m=blade?-1:1,a=-m*HALF_TURN*.5f;
    const float bob=t>FILLED?1.3f*(1-cosine((t-FILLED)*2.4f)):0;
    const float dx=sine(a),dy=-cosine(a),nx=-dy,ny=dx;
    const float hx=cx+dx*(1.5f+bob),hy=cy;
    auto X=[&](float u,float v){return hx+dx*u+nx*v*m;};
    auto Y=[&](float u,float v){return hy+dy*u+ny*v*m;};
    // Construction lines: dotted extensions of the edges and the guard.
    const float guideK=smooth((t-GUIDE)/.25f)*(1-smooth((t-FILLED)/.4f));
    if(guideK>.02f) {
      const int k=q8(.4f*guideK);
      for(float u=4;u<=L+6;u+=3)armoryPlot(roundi(X(u,-4.4f)),roundi(Y(u,-4.4f)),dimI,k);
      for(float v=-7;v<=8;v+=3)armoryPlot(roundi(X(15.5f,v)),roundi(Y(15.5f,v)),dimI,k);
      for(float v=-6;v<=7;v+=3)armoryPlot(roundi(X(L-6,v)),roundi(Y(L-6,v)),dimI,k);
    }
    // The outline: polylines in local (u,v), traced in order by the pen.
    float pts[48][2];int n=0,starts[8],ns=0;
    auto begin=[&](){starts[ns++]=n;};
    auto add=[&](float u,float v){pts[n][0]=u;pts[n++][1]=v;};
    begin();for(int i=0;i<7;++i){const float q=Q[i];add(BLADE0+(L-BLADE0)*q,side(BACK,q)+bend(q));}
    begin();for(int i=0;i<7;++i){const float q=Q[i];add(BLADE0+(L-BLADE0)*q,side(EDGE,q)+bend(q));}
    begin();add(14,-5);add(14,5);add(17,5);add(17,-5);add(14,-5);
    begin();add(13.5f,-1.6f);add(4.5f,-1.6f);
    begin();add(4.5f,1.6f);add(13.5f,1.6f);
    begin();for(int i=0;i<=8;++i){const float an=i*(2*HALF_TURN/8);add(2.2f+2.2f*cosine(an),2.2f*sine(an));}
    begin();for(int i=0;i<=4;++i){const float q=.1f+.65f*i*.25f;add(BLADE0+(L-BLADE0)*q,.5f+bend(q));}
    starts[ns]=n;
    float total=0;for(int s=0;s<ns;++s)for(int i=starts[s]+1;i<starts[s+1];++i)total+=span(pts[i][0]-pts[i-1][0],pts[i][1]-pts[i-1][1]);
    // The fill first, so the outline draws over it.
    if(fillFront>0) {
      const bool dark=blade==0;
      const Ink body=inkOf(dark?rgb8(70,76,98):rgb8(196,210,232)),sheen=inkOf(dark?rgb8(150,160,190):WHITE),lattice=inkOf(dark?rgb8(210,48,48):mixed(blue,WHITE,.2f));
      // The blades lie flat (along x, edge up), so each u is one column and
      // its band one run of rows: visit only those pixels, with no division.
      static constexpr int RECIP[16]={0,256,128,85,64,51,43,37,32,28,26,23,21,20,18,17};
      const int ul=std::min(int(L),int(fillFront));
      const bool atFront=t<FILLED+.05f;
      for(int u=2;u<=ul;++u) {
        float lo,hi;
        if(u>=BLADE0){const float q=(u-BLADE0)*(1/(L-BLADE0));lo=side(BACK,q)+bend(q);hi=side(EDGE,q)+bend(q);}
        else if(u>=14){lo=-5;hi=5;}else if(u>=4){lo=-1.6f;hi=1.6f;}else{lo=-2;hi=2;}
        const int x=roundi(hx+dx*u),yTop=-floori(hi-hy),yBottom=floori(hy-lo),rows=yBottom-yTop;
        if(rows<0 || unsigned(x)>=unsigned(w_))continue;
        const bool front=atFront && fillFront-u<3;
        for(int y=yTop;y<=yBottom;++y) {
          if(front){armoryPlot(x,y,WHITE_INK,256);continue;}
          const int lv=roundi(hy-y);const bool line=dark && u>=BLADE0 && (((u+2*lv)%7+7)%7==0 || ((u-2*lv)%7+7)%7==0);
          // Shaded across the width: brightest along the cutting edge (the top row).
          const int across=rows>0?((yBottom-y)*RECIP[std::min(rows,15)]):0;
          armoryPlot(x,y,line?lattice:mixedInk(body,sheen,(across*across*200)>>16),q8(line?.6f:.8f));
        }
      }
    }
    float budget=total*clamp01(drawn);const float invTotal=1/total;
    const float lineK=wire*(t<FILL?.95f:t<DRAIN?.7f+.12f*sine(t*9+blade*2):.85f);
    float penX=-1,penY=-1;
    if(lineK>.02f)for(int s=0;s<ns && budget>0;++s) {
      const bool fuller=s==ns-1;
      int lx=roundi(X(pts[starts[s]][0],pts[starts[s]][1])),ly=roundi(Y(pts[starts[s]][0],pts[starts[s]][1]));
      armoryPlot(lx,ly,blueI,q8(lineK));
      for(int i=starts[s]+1;i<starts[s+1] && budget>0;++i) {
        const float su=pts[i][0]-pts[i-1][0],sv=pts[i][1]-pts[i-1][1],len=span(su,sv),f=budget<len?budget/len:1;budget-=len;
        const float u=pts[i-1][0]+su*f,v=pts[i-1][1]+sv*f;
        if(fuller){if(i&1)armoryPlot(roundi(X(u,v)),roundi(Y(u,v)),blueI,q8(lineK*.6f));lx=roundi(X(u,v));ly=roundi(Y(u,v));}
        else armoryTo(lx,ly,roundi(X(u,v)),roundi(Y(u,v)),blueI,q8(lineK));
        if(f<1){penX=X(u,v);penY=Y(u,v);}
        else if(t<FILL && !fuller && budget<total*.08f) {
          // A tick marks each corner just reached.
          const int k=q8(.7f*(1-budget*12.5f*invTotal));const int qx=roundi(X(pts[i][0],pts[i][1])),qy=roundi(Y(pts[i][0],pts[i][1]));
          armoryPlot(qx-1,qy,penI,k);armoryPlot(qx+1,qy,penI,k);armoryPlot(qx,qy-1,penI,k);armoryPlot(qx,qy+1,penI,k);
        }
      }
    }
    if(penX>=0 && t<DRAWN+.05f)twinkle(penX,penY,penC,1.2f,2);
    // Sustain: a glint runs along each blade's edge now and then; sparks
    // crawl over the wireframe.
    if(t>FILLED && t<DRAIN) {
      const float g=(t-FILLED-.3f-.45f*blade)/1.3f,gu=(g-floori(g))*1.3f;
      if(g>0 && gu<.4f){const float q=gu/.4f,u=BLADE0+(L-BLADE0)*q;twinkle(X(u,side(EDGE,q)+bend(q)),Y(u,side(EDGE,q)+bend(q)),WHITE,.95f*sine(HALF_TURN*q),2);}
      for(unsigned k=0;k<3;++k) {
        const uint32_t seed=uint32_t(frame)*8+k+blade*4;if(rnd(seed,481)<.35f)continue;
        const float q=rnd(seed,482),u=BLADE0+(L-BLADE0)*q,v=rnd(seed,483)<.5f?side(BACK,q)+bend(q)-1.5f:side(EDGE,q)+bend(q)+1.5f;
        armoryPlot(roundi(X(u,v)),roundi(Y(u,v)),penI,q8(.75f));
      }
    }
    // As each blade fills, the spare light of the projection falls off it
    // as a shower of blue motes, and again as the fill drains away.
    for(unsigned k=0;k<14;++k) {
      const uint32_t seed=k+blade*16;const bool late=k>=9;
      const float born=(late?DRAIN+.05f:FILL+.02f)+(late?.35f:.15f)*rnd(seed,484),a=t-born,life=.45f+.25f*rnd(seed,485);
      if(a<0 || a>=life)continue;
      const float u=BLADE0+(L-BLADE0)*rnd(seed,486),x=X(u,0)+(rnd(seed,487)-.5f)*20*a,y=Y(u,0)+4+(20+40*rnd(seed,488))*a+90*a*a;
      dot(x,y,k%3?blue:penC,(1-a/life)*1.1f);
    }
  }
}
}
