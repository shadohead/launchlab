#pragma once
// Evangelion-inspired celebrations (Crimson..Prism): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Same rules as
// the others: additive light on the background outside the UI halo, a pure
// function of elapsed time with fixed seeds, nothing after the duration.
// Only the beam (Prism) touches the readout's own pixels: it pulls the glyphs
// under its column toward white (never darker in any channel) while it
// fires, and gives them back exactly at 1.85 s of 5.0 s.
// Each runs about 5 s: an early impact (0.4 to 1.1 s), a sustain that keeps
// moving but costs less than the impact, then an unhurried settle.
// Cost: the canvas mask is built per 32-pixel word, so each effect keeps
// its wide strokes to a few rows (the sea surface, the hazard bands, the
// cross bar) and its tall ones inside one word column (the pillar and the
// beam stay within a few pixels of the readout's centre). Strokes step in
// fixed point; particles are dots.
// Palette: NERV orange and red, Unit-01 purple and green, angel blue.
namespace BestEffectUI {

inline void Renderer::eva(unsigned which,float t) {
  switch(which) {
    case 0:evaSphere(t);break;
    case 1:evaBarrier(t);break;
    case 2:evaCross(t);break;
    case 3:evaAlert(t);break;
    case 4:evaBeam(t);break;
    default:break;
  }
}

// The mask scan of a 32-pixel word also reads HALO pixels of each
// neighbouring word, so light written next to a word edge before the
// neighbour's mask row is built would be taken for UI ink there and cut a
// 2-pixel gap into any stroke crossing the edge (x = 32, 64, 96). Every
// write here first builds that neighbour's row, at most one scan per row
// and edge, which a stroke crossing the edge needs anyway.
BEST_EFFECT_INLINE void Renderer::evaAdd(int x,int y,const Ink &c,int w) {
  if(w<5)return;
  if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_)) {
    const int m=x&31;
    if(m>=32-HALO && x+HALO<w_)ink(y,(x>>5)+1);
    else if(m<HALO && x>=HALO)ink(y,(x>>5)-1);
  }
  addw(x,y,c,w);
}
// The renderer's point primitives, written through evaAdd.
inline void Renderer::evaPlot(float x,float y,const Rgb &c,float k){evaAdd(roundi(x),roundi(y),inkOf(c),q8(k));}
inline void Renderer::evaDot(float x,float y,const Rgb &col,float k) {
  const int ix=floori(x),iy=floori(y),ax=q8(x-ix),ay=q8(y-iy),kq=q8(k);const Ink c=inkOf(col);
  evaAdd(ix,iy,c,(kq*(256-ax)*(256-ay))>>16);evaAdd(ix+1,iy,c,(kq*ax*(256-ay))>>16);
  evaAdd(ix,iy+1,c,(kq*(256-ax)*ay)>>16);evaAdd(ix+1,iy+1,c,(kq*ax*ay)>>16);
}
inline void Renderer::evaVdot(int x,float y,const Ink &c,int kq) {
  const int iy=floori(y),ay=q8(y-iy);
  evaAdd(x,iy,c,(kq*(256-ay))>>8);evaAdd(x,iy+1,c,(kq*ay)>>8);
}
inline void Renderer::evaSpark(float x,float y,const Rgb &col,float k,float radius) {
  const int r=int(radius+.999f),ix=roundi(x),iy=roundi(y),kq=q8(k);const float inv=1/(radius*radius);
  const Ink c=inkOf(col);
  for(int dy=-r;dy<=r;++dy)for(int dx=-r;dx<=r;++dx) {
    const float ex=ix+dx-x,ey=iy+dy-y;const int f=q8(1-(ex*ex+ey*ey)*inv);
    if(f>0)evaAdd(ix+dx,iy+dy,mixedInk(c,WHITE_INK,(f*f)>>8),(kq*f)>>8);
  }
}
inline void Renderer::evaEllipse(float cx,float cy,float rx,float ry,const Rgb &c,float k,float density) {
  if(k<=.02f)return;
  const int n=std::max(12,int((rx+ry)*density));const float step=2*HALF_TURN/n;const Ink ink=inkOf(c);const int kq=q8(k);
  for(int i=0;i<n;++i){const float a=i*step;evaAdd(roundi(cx+rx*cosine(a)),roundi(cy+ry*sine(a)),ink,kq);}
}
inline void Renderer::evaTwinkle(float x,float y,const Rgb &c,float k,int arm) {
  evaDot(x,y,c,k);
  for(int d=1;d<=arm;++d){const float f=k*(1-float(d)/(arm+1));evaPlot(x+d+.5f,y,c,f);evaPlot(x-d-.5f,y,c,f);evaPlot(x,y+d+.5f,c,f);evaPlot(x,y-d-.5f,c,f);}
}

// One probe per pixel along the longer axis, in fixed point; the end point
// is left out so closed outlines do not double their corners. With `clip`,
// nothing is drawn in the readout's box, not even between its glyphs.
inline void Renderer::evaStroke(float x0,float y0,float x1,float y1,const Ink &c,int kq,bool clip) {
  if(kq<5)return;
  const float dx=x1-x0,dy=y1-y0;const int n=std::max(1,int(std::max(std::fabs(dx),std::fabs(dy))+.5f));
  int x8=q8(x0)+128,y8=q8(y0)+128;const int sx=q8(dx/n),sy=q8(dy/n);
  for(int i=0;i<n;++i){if(!clip || !evaHidden(x8>>8,y8>>8))evaAdd(x8>>8,y8>>8,c,kq);x8+=sx;y8+=sy;}
}
// A regular octagon with flat top, bottom and sides, radius to its vertices.
inline void Renderer::evaOctagon(float cx,float cy,float r,const Ink &c,int kq,bool clip) {
  constexpr float C=.9239f,S=.3827f;
  static constexpr float vx[]={C,S,-S,-C,-C,-S,S,C},vy[]={S,C,C,S,-S,-C,-C,-S};
  for(int k=0;k<8;++k){const int j=(k+1)&7;evaStroke(cx+r*vx[k],cy+r*vy[k],cx+r*vx[j],cy+r*vy[j],c,kq,clip);}
}
// A filled triangle, row spans in float and pixels in integers; the
// half-open spans keep neighbouring faces from adding twice on a shared edge.
inline void Renderer::evaTriangle(float x0,float y0,float x1,float y1,float x2,float y2,const Ink &c,int kq) {
  if(kq<5)return;
  if(y1<y0){std::swap(x0,x1);std::swap(y0,y1);}
  if(y2<y0){std::swap(x0,x2);std::swap(y0,y2);}
  if(y2<y1){std::swap(x1,x2);std::swap(y1,y2);}
  if(y2-y0<.5f)return;
  const int top=std::max(0,int(std::ceil(y0))),end=std::min(h_,int(std::ceil(y2)));
  for(int y=top;y<end;++y) {
    const float yc=float(y);
    const float xa=x0+(x2-x0)*(yc-y0)/(y2-y0);
    const float xb=yc<y1?(y1>y0?x0+(x1-x0)*(yc-y0)/(y1-y0):x1):(y2>y1?x1+(x2-x1)*(yc-y1)/(y2-y1):x1);
    const int from=std::max(0,int(std::ceil(std::min(xa,xb)))),to=std::min(w_,int(std::ceil(std::max(xa,xb))));
    for(int x=from;x<to;++x)evaAdd(x,y,c,kq);
  }
}
// Pulls one readout pixel toward white by k8 (Q8), in every channel upward;
// background pixels are left alone and a lit pixel never takes the
// background's exact value, so it still blocks decoration.
inline void Renderer::evaLighten(int x,int y,int k8) {
  uint16_t &p=px_[y*w_+x];if(p==bg_ || k8<=0)return;
  const unsigned v=load(p);const int r=v>>11,g=(v>>5)&63,b=v&31;
  const uint16_t out=store(uint16_t((r+(((31-r)*k8)>>8))<<11|(g+(((63-g)*k8)>>8))<<5|(b+(((31-b)*k8)>>8))));
  p=out==bg_?uint16_t(out^store(1)):out;++plots_;
}
// A six-row hazard band: thin rails above and below, 45-degree stripes
// between them, scrolled by `scroll` px.
inline void Renderer::evaHazard(int y,int x0,int x1,int scroll,const Ink &stripe,int kq) {
  x0=std::max(0,x0);x1=std::min(w_-1,x1);if(x1<x0 || kq<5)return;
  const int rail=(kq*110)>>8;
  for(int x=x0;x<=x1;++x){evaAdd(x,y,stripe,rail);evaAdd(x,y+5,stripe,rail);}
  for(int r=1;r<=4;++r)for(int x=x0;x<=x1;++x)if(((x+r+scroll)&7)<4)evaAdd(x,y+r,stripe,kq);
}
// A 5x7 pixel face of its own (only the letters the alert needs).
inline void Renderer::evaText(const char *text,int x,int y,const Ink &c,int kq) {
  static constexpr uint8_t A[7]={14,17,17,31,17,17,17},E[7]={31,16,16,30,16,16,31},L[7]={16,16,16,16,16,16,31};
  static constexpr uint8_t R[7]={30,17,17,30,20,18,17},T[7]={31,4,4,4,4,4,4};
  for(;*text;++text,x+=6) {
    const uint8_t *g=*text=='A'?A:*text=='E'?E:*text=='L'?L:*text=='R'?R:*text=='T'?T:nullptr;
    if(!g)continue;
    for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g[row]&(16>>col))evaAdd(x+col,y+row,c,kq);
  }
}

// Inside the readout's box and its halo: light there would show only in
// the gaps between glyphs, so tall and round strokes skip it.
inline bool Renderer::evaHidden(int x,int y)const {
  return x>=number_.x0-HALO && x<=number_.x1+HALO && y>=number_.y0-HALO && y<=number_.y1+HALO;
}

// ---- Crimson (Third Impact): a crimson wave rises up the screen and a sphere
// of light breaks out of the red sea it leaves, with a cross-shaped flare,
// and rises round the readout. It lives on: it breathes, two bright arcs
// turn round its rim, orange motes spiral into it and drift up off the
// swelling sea; then it dissolves as the sea sinks away.
inline void Renderer::evaSphere(float t) {
  constexpr float RISE=.8f,IGNITE=.82f,DISSOLVE=3.9f,END=5.0f;
  const Rgb crimson=rgb8(240,22,48),blood=rgb8(160,0,28),lcl=rgb8(255,136,56),pale=rgb8(255,230,218);
  const float cx=number_.cx(),cy=number_.cy();
  const float R=std::min(54.0f,w_*.5f-12),horizon=std::min(float(bottom_-30),cy+R-8);
  sorcMark(1);
  // The sea: rises fast and slows (the wave), holds, then sinks away.
  const float level=t<RISE?h_+4-(h_+4-horizon)*easeOut(t/RISE):t<DISSOLVE?horizon:horizon+(h_+6-horizon)*smooth((t-DISSOLVE)/(END-DISSOLVE-.2f));
  const float flash=t>=IGNITE?1-smooth((t-IGNITE)/.4f):0;
  const int frame=int(t*20);
  if(level<h_+3) {
    const bool rising=t<RISE;
    const float sea=rising?1:t<DISSOLVE?.85f-.15f*smooth((t-IGNITE)/2):.7f*(1-smooth((t-DISSOLVE)/(END-DISSOLVE-.3f)));
    const float chop=rising?1.2f+1.6f*(1-t/RISE):1.0f,swell=rising?0:1.4f*smooth((t-IGNITE)/.8f);
    const Ink crest=inkOf(pale),body=inkOf(crimson),deep=inkOf(blood),glow=inkOf(lcl);
    const int k=q8(sea),spread=t>=IGNITE?int(10+320*(t-IGNITE)):1;
    for(int x=0;x<w_;++x) {
      const float y=level-chop*sine(x*.1f+t*6)-.6f*chop*sine(x*.27f-t*4)-swell*sine(x*.045f-t*1.3f);
      // At ignition a flare runs out along the horizon from the centre;
      // later, glints travel slowly along the crests.
      const int d=std::abs(x-int(cx)),hot=flash>0?std::max(0,256-(d*256)/spread):0;
      const float gl=rising?0:sine(x*.13f-t*2.1f)*sine(x*.05f+t*.7f);
      const int glint8=gl>.55f?int((gl-.55f)*400):0;
      evaVdot(x,y,mixedInk(body,crest,std::min(256,(rising?190:140)+hot+glint8)),(k*(190+((int(flash*66)*hot)>>8)))>>8);
      const int iy=floori(y);
      evaAdd(x,iy+2,body,(k*160)>>8);evaAdd(x,iy+3,body,(k*105)>>8);if(t<DISSOLVE)evaAdd(x,iy+4,deep,(k*70)>>8);
      if(rising)evaAdd(x,iy+5,deep,(k*40)>>8);
      if(hot>48)evaAdd(x,iy-1,crest,(int(flash*256)*hot)>>8);
      // The rising wave throws light upward in flickering rays.
      if(rising && x%5==2) {
        const int n=2+(lattice(x/5,frame,700)>>5);
        for(int s=1;s<=n;++s)evaAdd(x,iy-s,glow,(k*(150-150*s/(n+1)))>>8);
      }
    }
    // Motes rise ahead of the wave, fading.
    if(t<RISE+.3f)for(unsigned i=0;i<12;++i) {
      const float birth=.05f+.6f*rnd(i,701),age=t-birth,life=.45f;if(age<0 || age>=life)continue;
      const float born=h_+4-(h_+4-horizon)*easeOut(birth/RISE);
      const float x=6+(w_-12)*rnd(i,702)+2*sine(age*9+i),y=born-6-(55+40*rnd(i,703))*age,kk=1-age/life;
      if(y>=level-2)continue;
      evaDot(x,y,lcl,kk);evaPlot(x,y+2,crimson,kk*.5f);
    }
    // Through the sustain, motes lift slowly off the sea, swaying.
    for(unsigned i=0;i<12;++i) {
      const float birth=IGNITE+.4f+2.9f*rnd(i,708),age=t-birth,life=1.1f+.5f*rnd(i,709);if(age<0 || age>=life)continue;
      const float u=age/life,x=6+(w_-12)*rnd(i,710)+3*sine(age*3+i*2),y=level-3-(10+8*rnd(i,711))*age;
      if(!evaHidden(roundi(x),roundi(y)))evaDot(x,y,i%3?lcl:pale,.75f*smooth(u/.15f)*(1-u)*sea);
    }
  }
  glint(t,IGNITE+.34f,.45f);
  if(t<IGNITE)return;
  sorcMark(2);
  // The cross-shaped flare where the sphere breaks the surface: the
  // horizontal arm is the flare along the sea, the vertical one a spike.
  if(t<IGNITE+.26f) {
    const float f=1-(t-IGNITE)/.26f;const Ink c=inkOf(pale);const int ix=roundi(cx),top=number_.y1+HALO+1;
    for(int y=top;y<roundi(horizon)+10;++y) {
      const float e=y<horizon?(horizon-y)/(horizon-top+1):(y-horizon)/10.0f;const int k=q8(f*f*(1-e)*1.3f);
      // Wide enough to show on both sides of a thin UI line through the centre.
      static constexpr int PROFILE[4]={256,230,200,150};
      for(int dx=-3;dx<=3;++dx)evaAdd(ix+dx,y,c,(k*PROFILE[std::abs(dx)])>>8);
    }
    if(t<IGNITE+.18f)evaSpark(cx,horizon-1,pale,1.6f*(1-(t-IGNITE)/.18f),3.2f);
  }
  sorcMark(3);
  // The sphere rises out of the sea as it grows; the sea hides what is below
  // its surface. Rim: a hot line with a soft limb inside and a crimson edge.
  const float grow=easeOut((t-IGNITE)/.45f);
  const float sy=horizon+(cy-horizon)*grow;
  sorcMark(4);
  // A slow breath of the radius and brightness through the sustain.
  const float breath=sine((t-IGNITE)*2.1f);
  float r=R*(.25f+.75f*grow)+1.3f*breath*smooth((t-IGNITE-.3f)/.5f),fade=1;
  if(t>=DISSOLVE){const float s=smooth((t-DISSOLVE)/(END-DISSOLVE-.15f));r-=12*s;fade=1-s;}
  if(fade>0) {
    const float glow=fade*(.86f+.08f*breath);const bool lean=t>IGNITE+.6f;
    const Ink core=inkOf(mixed(crimson,pale,.4f+.6f*flash)),limb=inkOf(mixed(crimson,pale,.25f)),edge=inkOf(crimson),halo=inkOf(blood);
    const int n=int(r*6.2832f),kc=q8(glow+.4f*flash),kl=q8(glow*.55f),ke=q8(glow*.6f),kh=q8(glow*.32f);
    // Unit vector stepped by rotation: no table lookup per point.
    const float step=6.2832f/n,cs=cosine(step),sn=sine(step);float ca=1,sa=0;
    const float spin=(t-IGNITE)*1.1f,sc=cosine(spin),ss=sine(spin);
    for(int i=0;i<n;++i) {
      const float x=cx+r*ca,y=sy+r*sa;const int ix=roundi(x),iy=roundi(y);
      if(iy>=0 && y<level-1 && !evaHidden(ix,iy)) {
        // Two brighter arcs turn slowly round the rim: cos(2(a-spin)) > 0.
        const float c1=ca*sc+sa*ss,s1=sa*sc-ca*ss,turn=c1*c1-s1*s1;
        const int arc=turn>0?int(256*turn*turn):0;
        evaAdd(ix,iy,core,(kc*(176+(arc*80>>8)))>>8);
        if(!lean || (i&1))evaAdd(roundi(x-ca*1.2f),roundi(y-sa*1.2f),limb,(kl*(192+(arc>>2)))>>8);
        evaAdd(roundi(x+ca*1.3f),roundi(y+sa*1.3f),edge,ke);
        if((i&3)==0 && t<DISSOLVE)evaAdd(roundi(cx+(r+6)*ca),roundi(sy+(r+6)*sa),halo,(kh*(160+(arc*96>>8)))>>8);
      }
      const float c2=ca*cs-sa*sn;sa=sa*cs+ca*sn;ca=c2;
    }
  }
  sorcMark(5);
  // Motes spiral in from the rim and vanish behind the readout, each pass
  // on a new path; as the sphere dissolves, others drift up off its rim.
  if(t>IGNITE+.15f && t<DISSOLVE+.3f) {
    const float on=smooth((t-IGNITE-.15f)/.2f)*(1-smooth((t-DISSOLVE)/.3f));
    for(unsigned i=0;i<12;++i) {
      const float p=(t-IGNITE)*(.5f+.15f*rnd(i,712))+rnd(i,704);const unsigned pass=unsigned(p);const float v=p-pass;
      const float a=6.2832f*rnd(i*37+pass,705)+2.4f*v;
      const float rr=r*(1-v*v)+14*v*v,x=cx+rr*cosine(a),y=sy+rr*sine(a)*.85f;
      if(y>=level-2 || evaHidden(roundi(x),roundi(y)))continue;
      const float back=a-.12f,rb=rr+5*v+2,k=on*(.55f+.6f*v)*smooth(v/.12f);
      evaDot(x,y,i%3?lcl:pale,k);evaPlot(cx+rb*cosine(back),sy+rb*sine(back)*.85f,crimson,k*.5f);
    }
  }
  if(t>DISSOLVE)for(unsigned i=0;i<10;++i) {
    const float age=t-DISSOLVE-.6f*rnd(i,706),life=.6f;if(age<0 || age>=life)continue;
    const float a=6.2832f*rnd(i,707),x=cx+r*cosine(a),y=sy+r*sine(a)-24*age;
    if(y<level-2)evaDot(x,y,i%2?lcl:crimson,(1-age/life)*.8f);
  }
  sorcMark(6);
}

// ---- Bulwark (the barrier): two blue strikes hit the field beside the readout
// and a third, bigger one below it; each hit flashes nested orange octagons
// into being from the inside out, which ripple outward. The big one's rings
// stay as a standing field, a slow ripple of light running out through them
// and sparks drifting along it, until it fades from the outside in.
inline void Renderer::evaBarrier(float t) {
  constexpr float SETTLE=3.7f,END=4.8f;
  struct Hit {float at,x,y,fromX,fromY,r0,gap,ripple,life;int rings;};
  const float cx=number_.cx(),H=number_.y1;
  const Hit hits[]={
    {.32f,number_.x0+24.0f,H+12,-8,H+70,3,4.5f,7,.75f,4},
    {.64f,number_.x1-24.0f,H+12,float(w_+8),H+76,3,4.5f,7,.75f,4},
    {1.0f,cx,H+22,cx-34,std::min(float(h_-20),H+130),5,5.5f,11,0,6}};
  const Rgb orange=rgb8(255,160,36),amber=rgb8(255,104,8),hot=rgb8(255,240,190),angel=rgb8(120,190,255);
  glint(t,.34f,.42f);
  for(unsigned h=0;h<3;++h) {
    const Hit &hit=hits[h];const float age=t-hit.at;
    // The strike: a blue-white bolt crossing to the field in 0.14 s.
    if(age>=-.14f && age<.02f) {
      const float u=std::min(1.0f,(age+.14f)/.14f),hx=hit.fromX+(hit.x-hit.fromX)*u,hy=hit.fromY+(hit.y-hit.fromY)*u;
      const float dx=hit.x-hit.fromX,dy=hit.y-hit.fromY,len=std::sqrt(dx*dx+dy*dy),ux=dx/len,uy=dy/len;
      const Ink c=inkOf(angel),white=WHITE_INK;
      for(int s=0;s<26;++s){const int k=q8(1-s/26.0f);evaAdd(roundi(hx-ux*s),roundi(hy-uy*s),s<4?white:c,k);if(s<10)evaAdd(roundi(hx-ux*s+uy),roundi(hy-uy*s-ux),c,k>>1);}
    }
    if(age<0)continue;
    if(age<.12f)evaSpark(hit.x,hit.y,hot,1.4f*(1-age/.12f),h==2?3.2f:2.2f);
    // Rings pop in from the inside out, white-hot, then cool to orange and
    // ride outward on the ripple.
    const bool field=h==2;
    for(int j=0;j<hit.rings;++j) {
      const float a=age-.03f*j;if(a<0)continue;
      float k,r;
      if(!field) {
        const float life=hit.life-.06f*j;if(a>=life)continue;
        r=hit.r0+hit.gap*j+hit.ripple*easeOut(a/life);
        k=(a<.05f?1:1-smooth((a-.05f)/(life-.05f)))*(1-.07f*j);
      } else {
        // The impact eases into the standing field: a ripple of light runs
        // outward through the rings, at a gently varying pace, and the
        // field fades from its outer ring inward.
        r=hit.r0+hit.gap*j+hit.ripple*easeOut(std::min(1.0f,a/.9f));
        const float run=(t-1.4f)*.75f+.08f*sine(t*.9f)-j*.16f,wave=sine(6.2832f*run);
        // The two outer rings belong to the impact only: the standing field
        // keeps four, which is cheaper to draw than the impact was.
        const float keep=j==hit.rings-1?1-smooth((a-.25f)/.4f):j==hit.rings-2?1-smooth((a-.55f)/.5f):1;
        const float level=keep*(.3f+.32f*(wave>0?wave*wave:0))*(1-smooth((t-SETTLE+.1f*j)/(END-SETTLE-.3f)));
        k=(a<.05f?1:1+(level-1)*smooth((a-.05f)/.85f))*(1-.07f*j);
        if(k<=.02f)continue;
      }
      const Ink c=inkOf(mixed(hot,j<hit.rings-2?orange:amber,std::min(1.0f,a/.1f)));
      // The standing field keeps clear of the readout, even its glyph gaps.
      evaOctagon(hit.x,hit.y,r,c,q8(k),field && a>.6f);
      // The newest rings are doubled while they are hot.
      if(a<.2f && j<3)evaOctagon(hit.x,hit.y,r-1,c,q8(k*.5f*(1-a/.2f)));
    }
    // The deflected strike sprays along the field's face.
    for(unsigned i=0;i<(h==2?12u:6u);++i) {
      const unsigned seed=h*16+i;const float life=.3f+.25f*rnd(seed,711);if(age>=life)continue;
      const float a=6.2832f*rnd(seed,712),v=50+70*rnd(seed,713),d=v*easeOut(age/life)*.6f,ca=cosine(a),sa=sine(a)*.6f;
      const float k=(1-age/life)*.95f;
      evaDot(hit.x+d*ca,hit.y+d*sa,i%2?hot:angel,k);evaPlot(hit.x+(d-3)*ca,hit.y+(d-3)*sa,angel,k*.45f);
    }
  }
  // Sparks drift slowly along the standing field's edges and fade.
  const Hit &big=hits[2];
  for(unsigned i=0;i<10;++i) {
    const float birth=1.5f+2.4f*rnd(i,714),age=t-birth,life=.9f+.5f*rnd(i,715);if(age<0 || age>=life)continue;
    const int j=1+int(3*rnd(i,716));const float r=big.r0+big.gap*j+big.ripple;
    // A point travelling along an octagon edge: corner angles 22.5 + 45 k degrees.
    const float side=std::floor(8*rnd(i,717)),u=std::fmod(rnd(i,718)+age*(rnd(i,719)<.5f?.35f:-.35f)+1,1.0f);
    const float a0=(22.5f+45*side)*(HALF_TURN/180),a1=a0+HALF_TURN/4;
    const float x=big.x+r*(cosine(a0)+(cosine(a1)-cosine(a0))*u),y=big.y+r*(sine(a0)+(sine(a1)-sine(a0))*u);
    const float k=smooth(age/.2f)*(1-age/life)*(1-smooth((t-SETTLE)/.5f));
    if(!evaHidden(roundi(x),roundi(y)))evaDot(x,y,i%3?hot:orange,k);
  }
}

// ---- Crux (the cross): a red core pulses under the readout, faster and
// faster, then bursts into a tall cross of fire: the arm shoots up through
// the readout, down behind the replay and out to both sides. It burns on,
// breathing and flickering, cooling slowly from white-hot to orange while
// embers lift off it, then reddens and draws in from its tips.
inline void Renderer::evaCross(float t) {
  constexpr float DET=.36f,BLAZE=1.3f,FADE=3.8f,END=5.0f;
  const float cx=number_.cx(),H=number_.y1;
  const int gx=roundi(cx),yc=int(H+8);
  const float up=yc+2.0f,down=std::max(10.0f,std::min(float(bottom_-40),H+80)-yc),L=std::min(58.0f,w_*.5f-8);
  const Rgb white=rgb8(255,250,236),orange=rgb8(255,142,30),red=rgb8(225,30,20),ember=rgb8(255,110,40);
  glint(t,DET+.5f,.42f);
  if(t<DET+.04f) {
    // The core: a red orb with a hot centre, pulsing faster, drawing sparks in.
    const float u=clamp01(t/DET),beat=.65f+.35f*sine(t*(14+46*u)),k=smooth(t/.1f)*(t<DET?1:1-(t-DET)/.04f);
    evaSpark(cx,yc,mixed(red,white,.2f+.5f*u),k*beat*(.9f+.5f*u),2+1.6f*u);
    for(unsigned i=0;i<10;++i) {
      const float v=std::fmod(t*2.2f+rnd(i,720),1.0f),a=6.2832f*rnd(i,721),rr=34*(1-v)+3;
      evaDot(cx+rr*cosine(a),yc+rr*sine(a)*.7f,i%2?orange:red,k*v);
    }
  }
  if(t<DET)return;
  const float d=t-DET;
  if(d<.14f)evaSpark(cx,yc,white,1.7f*(1-d/.14f),4);
  // Cooling: white-hot to orange through the sustain, to red as it goes.
  const float cool=.75f*smooth((t-BLAZE)/(FADE-BLAZE))+.25f*smooth((t-FADE)/.6f);
  const float out=smooth((t-FADE)/(END-FADE-.15f)),fade=1-out;
  // The cross breathes: slow swells of brightness, never in step on both arms.
  const float breath=t<BLAZE?1:.82f+.12f*sine((t-BLAZE)*2.3f)+.06f*sine((t-BLAZE)*5.1f);
  if(fade>0) {
    // Across an arm: a white core, orange edges and a soft red rim, in Q8.
    static constexpr int PROFILE[5]={256,232,180,110,50};
    const Ink core=inkOf(mixed(white,orange,cool)),edge=inkOf(mixed(orange,red,cool)),rim=inkOf(red);
    const Ink *shade[5]={&core,&core,&edge,&edge,&rim};
    const int kf=q8(fade*breath),frame=int(t*20);
    // After the blaze the arms burn a pixel narrower: lighter to draw.
    const int wide=t<BLAZE?4:3;
    // The vertical arm, its tips racing out and, at the end, drawing in;
    // rows behind the readout are hidden by its ink, so they are skipped.
    const float shrink=1-.85f*out,aU=up*easeOut(d/.14f)*shrink,aD=down*easeOut(d/.2f)*shrink;
    for(int y=std::max(0,int(yc-aU));y<=std::min(bottom_,int(yc+aD));++y) {
      if(std::abs(y-yc)<=2 || (y>=number_.y0-HALO && y<=number_.y1+HALO))continue;
      const float e=y<yc?(yc-y)/std::max(1.0f,aU):(y-yc)/std::max(1.0f,aD);
      const int taper=e<.8f?256:int(256*(1-e)*5),flick=200+(lattice(y/3,frame,723)>>2);
      const int k=(kf*taper)>>8,hw=e<.85f?wide:wide-1;
      for(int dx=-hw;dx<=hw;++dx){const int a=std::abs(dx);evaAdd(gx+dx,y,*shade[a],((k*PROFILE[a]>>8)*(a>1?flick:256))>>8);}
    }
    // The cross arm spreads both ways from the centre, tapering at its tips.
    const float reach=L*easeOut((d-.03f)/.16f)*shrink;
    if(reach>1)for(int x=std::max(0,int(cx-reach));x<=std::min(w_-1,int(cx+reach));++x) {
      const float e=std::fabs(x-cx)/reach;const int taper=e<.75f?256:int(256*(1-e)*4);
      const int k=(kf*taper)>>8,flick=200+(lattice(x/3,frame,724)>>2);
      for(int dy=-2;dy<=2;++dy){const int a=std::abs(dy)*2;if(wide==3 && a==4)continue;evaAdd(x,yc+dy,*shade[a],((k*PROFILE[a]>>8)*(a>1?flick:256))>>8);}
    }
  }
  // Embers lift off the arms through the sustain and as it cools.
  for(unsigned i=0;i<26;++i) {
    const float a=t-(BLAZE-.3f+3.4f*rnd(i,725)),life=.8f+.4f*rnd(i,729);if(a<0 || a>=life)continue;
    const bool arm=i%3!=0;const float span=L*(1-.85f*out);
    const float x0=arm?cx+(rnd(i,726)-.5f)*2*span*.9f:cx+(rnd(i,726)-.5f)*8,y0=arm?yc:yc+6+(down-6)*rnd(i,727);
    const float x=x0+4*sine(a*5+i),y=y0-(14+18*rnd(i,728))*a;
    // Embers vanish behind the readout rather than drift through its glyph gaps.
    if(!evaHidden(roundi(x),roundi(y)))evaDot(x,y,i%2?ember:red,(1-a/life)*.9f);
  }
}

// ---- Klaxon (the alarm): a blue scan sweeps down and finds its pattern;
// a red frame snaps round the readout, a hazard band slams in under it and
// the alert blinks fast. The alarm then holds: the frame breathes, the band
// creeps, the alert glows steady and the blue signal meter wanders, until
// it stands down.
inline void Renderer::evaAlert(float t) {
  constexpr float ALERT=.45f,HELD=1.9f,STAND=3.7f,END=4.8f;
  const Rgb blue=rgb8(40,140,255),ice=rgb8(170,215,255),red=rgb8(255,34,30),orange=rgb8(255,150,0);
  const int H=number_.y1,x0=number_.x0,x1=number_.x1;
  const int band=H+4,panel=H+12;
  const int fl=std::max(1,x0-4),fr=std::min(w_-2,x1+4),ft=std::max(0,number_.y0-4);
  // Anticipation: the scan line sweeps down past the readout.
  if(t<ALERT+.05f) {
    const float y=-2+(panel+10)*smooth(t/.42f),k=t<ALERT?1:1-(t-ALERT)/.05f;
    const Ink c=inkOf(blue),bright=inkOf(ice);
    for(int x=0;x<w_;++x) {
      evaVdot(x,y,(x&3)?c:bright,q8(.9f*k));
      if(!(x&1)){evaAdd(x,roundi(y-2),c,q8(.35f*k));evaAdd(x,roundi(y-4),c,q8(.15f*k));}
    }
    // Targeting ticks gather at the corners where the frame will be.
    const float g=easeOut(t/ALERT)*k,o=10*(1-g);
    evaTwinkle(fl+o,ft-o*.3f,ice,.8f*g,1);evaTwinkle(fr-o,ft-o*.3f,ice,.8f*g,1);
    evaTwinkle(fl+o,band+o*.3f,ice,.8f*g,1);evaTwinkle(fr-o,band+o*.3f,ice,.8f*g,1);
  }
  glint(t,ALERT+.02f,.4f);
  if(t<ALERT)return;
  const float a=t-ALERT,out=smooth((t-STAND)/(END-STAND-.15f)),on=1-out;
  // From the blinking alarm to the held one.
  const float held=smooth((t-HELD)/.4f);
  // The first frames of the alarm flash white.
  const float white=a<.1f?1-a/.1f:0;
  // The frame: red rails around the readout, blinking at about 3.5 Hz, then
  // lit steady with a slow breath.
  {
    const bool lit=std::fmod(a,.28f)<.19f;
    const float blink=lit?1.0f:.3f,breath=.72f+.18f*sine((t-HELD)*2.4f);
    const Ink c=inkOf(mixed(red,WHITE,white*.7f));const int kq=q8(on*(blink+(breath-blink)*held));
    for(int x=fl;x<=fr;++x)evaAdd(x,ft,c,kq);
    for(int y=ft+1;y<band;++y){evaAdd(fl,y,c,kq);evaAdd(fr,y,c,kq);}
    for(int i=1;i<=3;++i){evaAdd(fl+i,ft+1,c,kq);evaAdd(fr-i,ft+1,c,kq);}
  }
  // The hazard band: slams in from the left, scrolls (slower once held),
  // and leaves to the right.
  {
    const float in=easeOut(a/.14f),scroll=a<HELD-ALERT?a*36:(HELD-ALERT)*36+(a-HELD+ALERT)*14;
    const Ink c=inkOf(mixed(orange,WHITE,white*.8f));
    evaHazard(band,int(w_*out),int(w_*in)-1,int(scroll),c,q8(.9f-.15f*held));
  }
  // The panel: a blinking bold alert on the left, then steady and glowing;
  // a blue signal meter on the right that wanders like a live reading.
  {
    const bool lit=std::fmod(a,.36f)<.24f;
    const float glow=.75f+.25f*sine((t-HELD)*3.1f);
    const float k=t<HELD?(lit?1.0f:0.0f):glow*held+(lit?1.0f:0.0f)*(1-held);
    if(k>.02f) {
      const Ink c=inkOf(mixed(red,WHITE,white));const int kq=q8(on*k);
      evaText("ALERT",17,panel,c,kq);evaText("ALERT",18,panel,c,kq);
    }
    const Ink c=inkOf(blue),tip=inkOf(ice);
    for(int b=0;b<7;++b) {
      const int n8=noise(int((a*2.6f+b*.43f)*256),b*311,760);
      const float level=on*(.15f+.85f*n8/255.0f)*smooth(a/.2f);
      const int x=w_-9-b*4,n=int(1+6*level);
      for(int r=0;r<n;++r){evaAdd(x,panel+6-r,r==n-1?tip:c,q8(on));evaAdd(x+1,panel+6-r,c,q8(on*.75f));}
    }
  }
}

// ---- Prism (the beam): a blue octahedron fades in under the readout and
// turns; a ring of particles spirals in to it while it charges, then it
// fires a white beam straight up through the readout, which flares white
// along the beam. The beam thins out and a ring rolls off; the gem turns on,
// its belt glowing, glints running down its edges and a loose ring of
// motes orbiting it, until it fades and the motes drift away.
inline void Renderer::evaBeam(float t) {
  constexpr float CHARGE=.25f,FIRE=1.05f,CEASE=1.6f,FADE=3.9f,END=5.0f;
  const Rgb deep=rgb8(16,60,200),bright=rgb8(110,200,255),edgeC=rgb8(205,240,255),pink=rgb8(255,130,210);
  const float cx=number_.cx(),H=number_.y1;
  const float oy=H+29,a=16,c=19,e=4.2f;
  const float topY=oy-c;
  const float show=smooth(t/.25f)*(1-smooth((t-FADE)/(END-FADE-.15f)));
  const float charge=t<CHARGE?0:t<FIRE?smooth((t-CHARGE)/(FIRE-CHARGE)):1-smooth((t-FIRE)/.5f);
  // After the shot the belt keeps a slow pulse.
  const float pulse=smooth((t-CEASE)/.4f)*(.3f+.2f*sine((t-CEASE)*2.6f));
  // The gem: four equator vertices turning, seen from slightly above.
  if(show>0) {
    const float phi=.4f+.9f*t+.9f*smooth((t-CHARGE)/(FIRE-CHARGE));
    float vx[4],vy[4],vs[4];
    for(int k=0;k<4;++k){const float ang=phi+k*HALF_TURN/2;vx[k]=cx+a*cosine(ang);vs[k]=sine(ang);vy[k]=oy+e*vs[k];}
    // Front faces only: those whose middle faces the viewer.
    for(int k=0;k<4;++k) {
      const int j=(k+1)&3;const float mid=phi+(k+.5f)*HALF_TURN/2;if(sine(mid)<=0)continue;
      const float lit=.5f+.5f*cosine(mid-2.2f);
      const Ink upper=inkOf(mixed(deep,bright,.15f+lit*.8f)),lower=inkOf(mixed(deep,bright,lit*.4f));
      evaTriangle(cx,topY,vx[k],vy[k],vx[j],vy[j],upper,q8(show*(.75f+.25f*charge)));
      evaTriangle(cx,oy+c,vx[k],vy[k],vx[j],vy[j],lower,q8(show*(.7f+.2f*charge)));
    }
    // Edges: the silhouette and the front ridges, brightening as it charges;
    // the equator glows pink-white with the charge, then with the pulse.
    const Ink edge=inkOf(edgeC),belt=inkOf(mixed(edgeC,pink,.5f));const int ke=q8(show*(.6f+.4f*charge));
    for(int k=0;k<4;++k) {
      if(vs[k]<-.75f)continue;
      evaStroke(cx,topY,vx[k],vy[k],edge,ke);evaStroke(vx[k],vy[k],cx,oy+c,edge,ke);
      const int j=(k+1)&3;if(vs[k]+vs[j]>0)evaStroke(vx[k],vy[k],vx[j],vy[j],belt,q8(show*(.45f+.55f*std::max(charge,pulse))));
    }
    // Glints run down the front ridges now and then, each on a new ridge.
    if(t>CEASE+.2f) {
      const float p=(t-CEASE-.2f)*.8f;const unsigned run=unsigned(p);const float u=p-run;
      int k=int(4*rnd(run,741));if(vs[k]<0)k=(k+2)&3;
      const bool lowerHalf=rnd(run,742)<.5f;
      const float gx=lowerHalf?vx[k]+(cx-vx[k])*u:cx+(vx[k]-cx)*u,gy=lowerHalf?vy[k]+(oy+c-vy[k])*u:topY+(vy[k]-topY)*u;
      evaTwinkle(gx,gy,edgeC,show*smooth(u/.15f)*(1-smooth((u-.7f)/.3f)),1);
    }
  }
  // Charging: particles spiral in on a tilted ring around the gem.
  if(t>CHARGE*.6f && t<FIRE+.05f) {
    const float u=clamp01((t-CHARGE*.6f)/(FIRE-CHARGE*.6f)),pull=u*u*u;
    const float spin=1.2f*t+3.5f*u*u,k=smooth(u/.2f)*(t<FIRE?1:1-(t-FIRE)/.05f);
    for(unsigned i=0;i<30;++i) {
      const float ang=i*(2*HALF_TURN/30)+spin+.2f*rnd(i,731),rr=(60+8*rnd(i,732))*(1-pull)+a*.8f*pull;
      const float s=sine(ang),x=cx+rr*cosine(ang),y=oy+rr*.36f*s;
      const float kk=k*(s>0?1:.5f)*(.75f+.35f*u);
      if(s>0 && i%3==0)evaTwinkle(x,y,edgeC,kk,1);else evaDot(x,y,i%4?bright:edgeC,kk*1.3f);
      evaPlot(x+rr*.08f*s,y-rr*.03f*cosine(ang),bright,kk*.4f);
    }
    // The muzzle at the top vertex gathers light.
    evaSpark(cx,topY,edgeC,(.4f+.9f*u)*k,1.5f+1.4f*u);
  }
  // After the shot a loose ring of motes gathers again and orbits slowly,
  // each at its own drifting radius; at the end they drift away.
  if(t>CEASE+.1f) {
    const float in=smooth((t-CEASE-.1f)/.6f),away=smooth((t-FADE)/(END-FADE-.2f));
    for(unsigned i=0;i<16;++i) {
      const float ang=i*(2*HALF_TURN/16)+.3f*rnd(i,733)+(.45f+.2f*rnd(i,734))*t;
      const float rr=(30+10*rnd(i,735)+4*sine(t*(.7f+.5f*rnd(i,736))+i))*(1+.8f*away)+(1-in)*25;
      const float s=sine(ang),k=in*(1-away)*(s>0?1.1f:.5f)*(.7f+.3f*sine(t*(2+rnd(i,737))+i*1.3f));
      if(s>.3f && i%4==0)evaTwinkle(cx+rr*cosine(ang),oy+rr*.36f*s,edgeC,k,1);else evaDot(cx+rr*cosine(ang),oy+rr*.36f*s,i%4?bright:edgeC,k);
    }
  }
  glint(t,FIRE+.02f,.4f);
  // The beam: from the top vertex straight up off the screen.
  if(t>=FIRE && t<CEASE+.25f) {
    const float f=t-FIRE;
    const float hw=f<.04f?5.5f*f/.04f:t<CEASE?5.2f+.6f*sine(f*40):5.8f*(1-smooth((t-CEASE)/.25f));
    const int hw8=q8(hw),x0=std::max(0,int(cx-hw-1)),x1=std::min(w_-1,int(cx+hw+1)),c8=q8(cx);
    const Ink core=WHITE_INK,glowC=inkOf(mixed(bright,pink,.25f+.25f*sine(f*23)));
    const int bottom=std::max(0,roundi(topY)-1);
    for(int y=0;y<=bottom;++y) {
      const bool behind=y>=number_.y0 && y<=number_.y1;
      for(int x=x0;x<=x1;++x) {
        const int dist=std::abs(x*256+128-c8),in=hw8-dist;if(in<=0)continue;
        // In from the beam's edge, in Q8: a two-pixel blue rim, white inside.
        const int k=std::min(256,in>>1),centre=std::min(256,std::max(0,in-256*2)>>1);
        if(behind && px_[y*w_+x]!=bg_){evaLighten(x,y,std::min(230,k));continue;}
        evaAdd(x,y,centre>0?mixedInk(glowC,core,centre):glowC,k);
      }
    }
    // Where it leaves the gem and where it pierces the readout: flares.
    if(f<.3f) {
      const int k=q8(1-f/.3f),ix=roundi(cx);
      for(int s=-16;s<=16;++s)evaAdd(ix+s,roundi(topY),core,(k*(256-std::abs(s)*15))>>8);
      for(int s=-44;s<=44;++s)evaVdot(ix+s,number_.y1+HALO+1.5f,s%3?glowC:core,(k*(256-std::abs(s)*5))>>8);
    }
  }
  // A ring rolls off the gem as the beam ceases.
  if(t>=CEASE && t<CEASE+.6f) {
    const float u=(t-CEASE)/.6f,rx=18+56*easeOut(u);
    evaEllipse(cx,oy,rx,rx*.36f,mixed(bright,edgeC,.3f),.8f*(1-u),1.6f);
  }
}
}
