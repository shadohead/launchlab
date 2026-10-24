#pragma once
// Physical celebrations: Renderer members declared in best_effect_ui.h, which
// includes this file after the class. All five draw additively on background
// pixels only (addw and the glint), move no UI ink, and avoid glow()/ring()
// and spark(): strokes are plot/line paths and small twinkles, points that
// would land on the readout box are skipped before addw, and each effect
// stays in a shallow row band, so probes and mask scans stay low.
namespace BestEffectUI {

// ---- Spin: a ripcord yank launches a three-blade top below the readout; it
// spins up until the blades blur, holds at full speed grinding sparks off
// its tip, then precesses as it settles --------------------------------------
inline void Renderer::spin(float t) {
  // The sustain is the long held spin: full speed from the 1.35 s lock to
  // 3.45 s, the top slowly travelling a few pixels as real tops do, then a
  // precessing run-down through the settle.
  constexpr float LOCK=1.35f,HELD=3.45f;
  glint(t,LOCK-.03f,.34f);
  const float cx=number_.cx();
  const float body=smooth(t/.25f)*(1-smooth((t-4.5f)/.45f));
  if(body<=0)return;
  // Disc centre, precessing in a small circle while it settles.
  // Slightly left of centre, so the stem and hub clear the replay's centre edge.
  float ccx=cx-7,ccy=number_.y1+28.0f,rys=18;
  if(t>LOCK && t<=HELD){ccx+=2.5f*sine((t-LOCK)*.9f);ccy+=1.2f*sine((t-LOCK)*1.3f+1);}
  if(t>HELD) {
    const float wob=2.2f+1.2f*smooth((t-HELD)/.9f),wa=(t-HELD)*9;
    ccx+=wob*cosine(wa)*.8f+2.5f*sine((HELD-LOCK)*.9f);ccy+=wob*.4f*sine(wa);rys*=1+.12f*sine(wa);
  }
  const float rx=40,ry=rys;
  const Rgb steel=mixed(rgb8(215,220,230),accent_,.3f);
  // Ripcord: a slow tension slide, then the yank that launches the top, then
  // a fading whip trail where the cord flicked away.
  if(t<.55f) {
    // The cord runs from the stem cap to the pulling hand off-screen right.
    const float head=t<.3f?cx+14+32*(t/.3f):cx+46+(w_-cx+24)*((t-.3f)/.25f)*((t-.3f)/.25f);
    const float y=ccy-13,fade=body*(1-smooth((t-.46f)/.09f));
    for(int x=int(ccx);x<=std::min(w_-1,int(head));++x) {
      plot(float(x),y,steel,fade*.8f);
      if((x&3)==0)plot(float(x),y-1.4f,steel,fade*1.1f);
    }
    if(head<w_)dot(head,y,steel,fade*1.2f);
  } else if(t<.8f) {
    const float fade=body*(1-(t-.55f)/.25f)*.5f;
    for(int i=0;i<=26;++i){const float u=i/26.0f;plot(ccx+u*56,ccy-13-15*u*u+3*sine(u*5),steel,fade*(1-u*.5f));}
  }
  // Angle: quadratic spin-up from the yank to LOCK, full rate while held,
  // then an exponential run-down. spd is the rate as a fraction of full.
  constexpr float LAPS=2.2f,RATE=2*HALF_TURN*2*LAPS/(LOCK-.3f);
  float angle,spd;
  if(t<LOCK){const float s=clamp01((t-.3f)/(LOCK-.3f));angle=2*HALF_TURN*LAPS*s*s;spd=s;}
  else if(t<HELD){angle=2*HALF_TURN*LAPS+RATE*(t-LOCK);spd=1;}
  else{const float a=t-HELD;angle=2*HALF_TURN*LAPS+RATE*(HELD-LOCK)+RATE*(1-std::exp(-.85f*a))/.85f;spd=std::exp(-.85f*a);}
  // The top's body: rim, a thicker front edge, an underside cone to the tip,
  // a stem and cap above the hub, so the silhouette reads as a top at 1x.
  const Rgb rimC=mixed(accent_,WHITE,.22f);
  for(int i=0;i<110;++i) {
    const float a=i*(2*HALF_TURN/110),ca=cosine(a),sa=sine(a);
    plot(ccx+rx*ca,ccy+ry*sa,rimC,body*(sa>0?1.0f:.5f));
    if(sa>0)plot(ccx+rx*ca,ccy+ry*sa+2.5f,rimC,body*.4f);
    plot(ccx+rx*.58f*ca,ccy+ry*.58f*sa,rimC,body*.16f); // a faint inner disc line
  }
  const float tipY=ccy+ry+7;
  line(ccx-rx*.5f,ccy+ry*.7f,ccx,tipY,rimC,body*.8f);
  line(ccx+rx*.5f,ccy+ry*.7f,ccx,tipY,rimC,body*.8f);
  plot(ccx,tipY+1,WHITE,body);
  line(ccx,ccy-5,ccx,ccy-13,steel,body*1.0f);
  line(ccx-3,ccy-13,ccx+3,ccy-13,steel,body*1.0f);
  plot(ccx,ccy,WHITE,body*1.3f);
  plot(ccx-2,ccy,accent_,body*.9f);plot(ccx+2,ccy,accent_,body*.9f);
  plot(ccx,ccy-2,accent_,body*.9f);plot(ccx,ccy+2,accent_,body*.9f);
  plot(ccx-1.4f,ccy-1.4f,accent_,body*.7f);plot(ccx+1.4f,ccy+1.4f,accent_,body*.7f);
  // Blade teeth travel along the front rim, so rotation reads even blurred.
  {
    const float spacing=HALF_TURN/7,off=angle-spacing*int(angle/spacing);
    for(float a=off;a<HALF_TURN;a+=spacing)plot(ccx+(rx+1.5f)*cosine(a),ccy+(ry+1)*sine(a),WHITE,body*.75f);
  }
  // Three blades: solid sectors at three radii behind a bright leading
  // spoke; the sector smears longer with speed into a motion blur, stepped
  // finely enough that the arcs stay continuous.
  const float arc=.55f+1.25f*spd;
  const int steps=std::max(12,int(arc*30));
  for(int b=0;b<3;++b) {
    const float head=angle+b*(2*HALF_TURN/3),ca=cosine(head),sa=sine(head);
    line(ccx+rx*.3f*ca,ccy+ry*.3f*sa,ccx+rx*.97f*ca,ccy+ry*.97f*sa,mixed(WHITE,accent_,.2f),body*1.0f);
    for(int i=0;i<=steps;++i) {
      const float u=float(i)/steps,a=head-arc*u,caa=cosine(a),saa=sine(a);
      const float k=body*(1-.6f*u)*(.6f+.4f*saa);
      const Rgb c=mixed(WHITE,accent_,std::min(1.0f,u*1.6f));
      plot(ccx+rx*.93f*caa,ccy+ry*.93f*saa,c,k*1.25f);
      plot(ccx+rx*.74f*caa,ccy+ry*.74f*saa,c,k*.95f);
      plot(ccx+rx*.52f*caa,ccy+ry*.52f*saa,c,k*.6f);
    }
  }
  // Lock flash: one expanding rim stroke as full speed is reached.
  if(t>=LOCK && t<LOCK+.5f) {
    const float e=(t-LOCK)/.5f,flash=(1-e)*(1-e),grow=1+.16f*easeOut(e);
    const Rgb c=mixed(WHITE,accent_,std::min(1.0f,e*2));
    for(int i=0;i<96;++i){const float a=i*(2*HALF_TURN/96);plot(ccx+rx*grow*cosine(a),ccy+ry*grow*sine(a),c,flash*(.6f+.4f*sine(a)));}
  }
  // Sparks grind off the tip while it runs at full speed.
  if(t>=LOCK-.1f) {
    const Rgb gold=rgb8(255,200,90);
    const float grind=body*(t<HELD?1:spd);
    for(unsigned i=0;i<10;++i) {
      const float life=.26f+.14f*rnd(i,113),age=fmodf(std::max(0.0f,t-LOCK+.1f)+rnd(i,114)*life,life);
      const float vx=(rnd(i,115)-.5f)*95,vy=-(12+42*rnd(i,116));
      plot(ccx+vx*age,ccy+ry+2+vy*age+150*age*age,i%3?gold:WHITE,grind*(1-age/life));
    }
    if(grind>0)twinkle(ccx,ccy+ry+2,gold,grind*(.6f+.4f*sine(t*31)),1);
  }
}

// ---- Storm: cuts appear in the air one by one, hold through a long beat of
// stillness, then all detonate together into falling shards -----------------
inline void Renderer::storm(float t) {
  constexpr float DETONATE=1.75f;
  constexpr int CUTS=10;
  glint(t,DETONATE-.03f,.32f);
  const float cx=number_.cx(),cy=number_.cy();
  const float endFade=1-smooth((t-4.55f)/.45f);
  const Rgb steel=mixed(rgb8(125,175,255),accent_,.3f),pale=mixed(steel,WHITE,.45f);
  // A cut stroke that skips the readout box, where every point is blocked.
  auto stroke=[&](float x0,float y0,float x1,float y1,const Rgb &c,float k,bool thick){
    const float dx=x1-x0,dy=y1-y0;const int n=std::max(1,int(std::max(std::fabs(dx),std::fabs(dy))));
    const int ox=std::fabs(dy)>std::fabs(dx)?1:0,oy=ox?0:1;
    for(int i=0;i<=n;++i) {
      const float x=x0+dx*i/n,y=y0+dy*i/n;
      if(x>number_.x0-3 && x<number_.x1+3 && y>number_.y0-3 && y<number_.y1+3)continue;
      plot(x,y,c,k);
      if(thick && !(i&1)){plot(x-ox,y-oy,steel,k*.55f);plot(x+ox,y+oy,steel,k*.55f);}
    }
  };
  // Anticipation: cold motes drift inward toward the readout and vanish.
  if(t<.5f) {
    const float k=smooth(t/.12f)*(1-smooth((t-.34f)/.16f));
    for(unsigned i=0;i<8;++i) {
      const float a=2*HALF_TURN*rnd(i,121)+t*1.2f,r=(56+24*rnd(i,122))*(1-.5f*(t/.5f));
      const float x=cx+r*cosine(a),y=cy+r*.6f*sine(a);
      if(x<number_.x0-3 || x>number_.x1+3 || y<number_.y0-3 || y>number_.y1+3)plot(x,y,steel,k*1.1f);
    }
  }
  for(int i=0;i<CUTS;++i) {
    const float appear=.45f+.85f*(i+.6f*rnd(i,131))/CUTS;
    const float px=16+(w_-32)*rnd(i,132);
    const float py=i<6?std::max(6.0f,cy-30)+62*rnd(i,133):cy+28+28*rnd(i,133);
    const float a=(rnd(i,134)-.5f)*2.0f,ca=cosine(a),sa=sine(a),half=19+14*rnd(i,135);
    const float x0=px-ca*half,y0=py-sa*half,x1=px+ca*half,y1=py+sa*half;
    const float age=t-appear;
    if(age<-.07f)continue;
    if(age<0) { // a pinprick of light where the cut is about to open
      twinkle(px,py,pale,(age+.07f)/.07f*.8f,1);continue;
    }
    if(t<DETONATE) {
      if(age<.06f) { // the cut slashes in from one end
        const float u=easeOut(age/.06f);
        stroke(x0,y0,x0+(x1-x0)*u,y0+(y1-y0)*u,WHITE,.95f,true);
      } else { // then it hangs in the air as a hairline, bright at its heart
        const float hold=(.5f+.09f*sine(t*7+i*1.7f))*endFade;
        stroke(x0,y0,x1,y1,steel,hold,false);
        plot(px,py,pale,hold*1.7f);
        // In the stillness each heart pulses once in turn.
        if(t>1.35f)twinkle(px,py,pale,std::max(0.0f,sine((t-1.35f)*4-i*.9f))*.6f,1);
      }
      continue;
    }
    // Detonation: every cut flashes at once, then the hairlines cool
    // slowly through the sustain, their hearts pulsing now and then.
    const float da=t-DETONATE;
    const float flash=(da<.5f?std::max(0.0f,1-da/.5f):.2f*(1-smooth((da-.5f)/2.7f)))*endFade;
    if(da<.07f)stroke(x0,y0,x1,y1,WHITE,1.0f,true);
    else if(flash>.015f){stroke(x0,y0,x1,y1,pale,flash*.75f,false);
      if(da>=.5f)twinkle(px,py,pale,std::max(0.0f,sine(da*1.9f-i*1.31f))*.35f*endFade,1);}
    if(da<.15f)twinkle(px,py,steel,(1-da/.15f)*1.1f,2);
    // Shards burst sideways off the cut and tumble down.
    for(unsigned j=0;j<3;++j) {
      const unsigned seed=i*3+j;
      const float life=.5f+.35f*rnd(seed,141);
      if(da>=life)continue;
      const float along=(rnd(seed,142)-.5f)*2*half,side=j==1?1.0f:-1.0f,v=(36+52*rnd(seed,143))*side;
      const float bx=px+ca*along,by=py+sa*along,d=v*da*(1-.4f*da);
      const float x=bx-sa*d,y=by+ca*d+80*da*da,k=(1-da/life)*endFade;
      plot(x,y,j==1?pale:steel,k*1.2f);plot(x+sa*2,y-ca*2-3*da,steel,k*.5f);
    }
  }
  // Sustain: after the burst shards land, a slow rain of smaller fragments
  // keeps sifting down through the cut band, tumbling as they fall.
  if(t>DETONATE)for(unsigned i=0;i<20;++i) {
    const float birth=DETONATE+.2f+2.1f*rnd(i,145),life=1.1f+.6f*rnd(i,146),age=t-birth;
    if(age<0 || age>=life)continue;
    const float u=age/life;
    const float x=10+(w_-20)*rnd(i,147)+2.5f*sine(age*3+i*1.7f),y=cy-14+52*rnd(i,148)+(20+14*rnd(i,149))*age;
    const float a=age*3.2f+i*1.3f,dx=1.8f*cosine(a),dy=1.8f*sine(a);
    const float k=sine(HALF_TURN*u)*endFade;
    if(x<number_.x0-3 || x>number_.x1+3 || y<number_.y0-3 || y>number_.y1+3)line(x-dx,y-dy,x+dx,y+dy,i%3?pale:steel,k*.9f);
  }
}

// ---- Quickdraw: a reticle snaps onto the readout, three shots ring out
// from the lower corners with a recoil kick, bullet holes smoke beside the
// readout, casings flip away and smoke curls off the muzzles ----------------
inline void Renderer::quickdraw(float t) {
  constexpr float SHOTS[]={.75f,1.05f,1.35f},LOCK=1.55f;
  glint(t,LOCK-.05f,.32f);
  const float cx=number_.cx(),cy=number_.cy();
  const Rgb steel=mixed(rgb8(175,195,210),accent_,.25f),flashCol=rgb8(255,190,70),gold=rgb8(255,208,72);
  const float settle=1-smooth((t-4.25f)/.5f);
  // Reticle: corner brackets frame the readout box, sliding in, kicking
  // outward at each shot, and snapping tight at LOCK.
  float kick=0;
  for(unsigned s=0;s<3;++s){const float a=t-SHOTS[s];if(a>=0 && a<.2f)kick=std::max(kick,3.5f*(1-a/.2f));}
  if(settle>0) {
    const float in=easeOut(t/.6f),snap=t>=LOCK?2.0f:0.0f;
    // Through the sustain the held reticle breathes very slightly, like a
    // shooter's aim settling, instead of freezing on the readout.
    const float out=(1-in)*16-snap+kick+(t>=LOCK?1.1f*sine((t-LOCK)*1.6f):0.0f);
    const float lx=std::max(1.0f,number_.x0-5-out),rx2=std::min(float(w_-2),number_.x1+5+out),ty=std::max(1.0f,number_.y0-6-out*.6f),by=number_.y1+6+out*.6f;
    const Rgb c=t>=LOCK && t<LOCK+.12f?mixed(steel,WHITE,.65f):steel;
    const float k=smooth(t/.08f)*settle*.95f;
    line(lx,ty,lx+11,ty,c,k);line(lx,ty,lx,ty+8,c,k);
    line(rx2,ty,rx2-11,ty,c,k);line(rx2,ty,rx2,ty+8,c,k);
    line(lx,by,lx+11,by,c,k);line(lx,by,lx,by-8,c,k);
    line(rx2,by,rx2-11,by,c,k);line(rx2,by,rx2,by-8,c,k);
    line(cx,ty-5,cx,ty+3,c,k*.9f);line(cx,by+5,cx,by-3,c,k*.9f);
    line(lx-5,cy,lx+3,cy,c,k*.9f);line(rx2+5,cy,rx2-3,cy,c,k*.9f);
    if(sine(t*16)>-.2f)dot(cx,cy,mixed(flashCol,WHITE,.3f),k);
  }
  auto tracer=[&](float x0,float y0,float x1,float y1,const Rgb &c,float k){
    const float dx=x1-x0,dy=y1-y0;const int n=std::max(1,int(std::max(std::fabs(dx),std::fabs(dy))));
    for(int i=0;i<=n;++i){const float x=x0+dx*i/n,y=y0+dy*i/n;
      if(x>number_.x0-3 && x<number_.x1+3 && y>number_.y0-3 && y<number_.y1+3)continue;
      plot(x,y,c,k);}
  };
  const float muzzleY=std::min(float(h_-20),cy+100);
  const Rgb smoke=rgb8(150,160,175);
  for(unsigned s=0;s<3;++s) {
    const float age=t-SHOTS[s];
    const bool left=s!=1;
    const float mx=left?11.0f:w_-12.0f,my=muzzleY;
    // Impact points walk around the readout, clear of its halo.
    const float ix=s==0?cx-38:s==1?cx+34:cx+6;
    const float iy=s==0?number_.y0-8.0f:s==1?number_.y1+9.0f:number_.y0-8.0f;
    if(age>=0 && age<.13f) { // muzzle flash: a white core and six spikes
      const float k=1-age/.13f;
      dot(mx,my,mixed(flashCol,WHITE,.6f),k*2);
      for(int spike=0;spike<6;++spike) {
        const float a=spike*(HALF_TURN/3)+rnd(s,151)*.7f,len=(8+7*rnd(s*6+spike,152))*(0.5f+.5f*k);
        line(mx,my,mx+len*cosine(a),my+len*sine(a),mixed(flashCol,WHITE,.45f),k*1.1f);
      }
    }
    if(age>=0 && age<.07f) { // the tracer, hot and double, then an afterglow
      tracer(mx,my,ix,iy,mixed(flashCol,WHITE,.7f),1.1f);
      tracer(mx,my+1,ix,iy+1,flashCol,.55f);
    } else if(age>=.07f && age<.24f) {
      tracer(mx,my,ix,iy,flashCol,.35f*(1-(age-.07f)/.17f));
    }
    if(age>=0 && age<.18f)twinkle(ix,iy,flashCol,(1-age/.18f)*1.5f,3); // impact burst
    // Sparks ricochet off the impact, each with a short trail.
    for(unsigned j=0;j<7;++j) {
      const unsigned seed=s*7+j;
      const float life=.32f+.26f*rnd(seed,161);
      if(age<0 || age>=life)continue;
      const float a=2*HALF_TURN*rnd(seed,162),v=34+66*rnd(seed,163);
      const float vx=v*cosine(a),vy=v*sine(a);
      const float x=ix+vx*age,y=iy+vy*age+115*age*age,k=(1-age/life);
      plot(x,y,j%2?gold:WHITE,k*1.2f);plot(x-vx*.03f,y-vy*.03f,j%2?gold:WHITE,k*.5f);
    }
    // The bullet hole stays behind: a bright-rimmed mark that smokes, then
    // fades with the reticle. It is decoration beside the readout only.
    if(age>=.05f) {
      const float hk=std::min(1.0f,(age-.05f)*8)*settle;
      for(int q=0;q<8;++q){const float a=q*(HALF_TURN/4);plot(ix+2.6f*cosine(a),iy+2.6f*sine(a),mixed(steel,WHITE,.4f),hk*.55f);}
      // The fresh hole glows, then cools to a faint ember through the sustain.
      plot(ix,iy,flashCol,hk*(std::max(0.0f,.8f-age)+.16f*(1-smooth((t-2.2f)/1.7f))*(.7f+.3f*sine(t*13+s*2.1f))));
      for(unsigned j=0;j<3;++j) { // smoke curls off the hole
        const float pa=fmodf(age*.55f+j*.34f,1.0f);
        plot(ix+2.5f*sine(age*5+j*2.2f),iy-3-13*pa,smoke,hk*.35f*(1-pa));
      }
    }
    // The spent casing flips up and away from the muzzle.
    if(age>=.04f && age<.6f) {
      const float ca=age-.04f,dir=left?1.0f:-1.0f;
      const float x=mx+dir*26*ca,y=my-52*ca+150*ca*ca,a=ca*17+s;
      const float dx=2.2f*cosine(a),dy=2.2f*sine(a),k=1-ca/.56f;
      line(x-dx,y-dy,x+dx,y+dy,gold,k*.95f);
    }
    // Smoke: puffs rise off the muzzle, widening and curling.
    for(unsigned j=0;j<5;++j) {
      const float pa=age-.05f-.09f*j,life=.95f;
      if(pa<0 || pa>=life)continue;
      const float u=pa/life,r=1.2f+2.4f*u;
      const float x=mx+4.5f*sine(pa*6+j*2.1f),y=my-4-26*u,k=.3f*(1-u)*smooth(pa/.08f)*settle;
      plot(x,y,smoke,k);plot(x-r,y+r*.5f,smoke,k*.8f);plot(x+r,y+r*.4f,smoke,k*.8f);
    }
  }
  // Sustain: thin smoke keeps curling off both muzzles in slow wisps while
  // the reticle holds, long after the shot puffs have dispersed.
  if(t>LOCK && settle>0)for(unsigned m=0;m<2;++m) {
    const float mx=m?11.0f:w_-12.0f;
    for(unsigned j=0;j<3;++j) {
      const float pa=fmodf(t-LOCK+j*.53f+m*.31f,1.6f),u=pa/1.6f;
      plot(mx+3.5f*sine(pa*5+j*2.3f+m*3),muzzleY-3-22*u,smoke,.22f*(1-u)*smooth(pa/.15f)*settle);
    }
  }
}

// ---- Leaves: leaves flutter down around the readout, a blade flash
// slices every one, and the halves tumble apart ----------------------------
inline void Renderer::leaves(float t) {
  constexpr float CUT=1.15f,CUT2=1.28f;
  constexpr int LEAVES=11; // seven for the cut, four late leaves for the sustain
  glint(t,CUT-.03f,.32f);
  const float cx=number_.cx(),cy=number_.cy();
  const float fade=1-smooth((t-4.4f)/.5f);
  static constexpr int PALETTE[3][3]={{90,205,85},{165,225,95},{245,175,65}};
  auto clear=[&](float x,float y){return x<number_.x0-3 || x>number_.x1+3 || y<number_.y0-3 || y>number_.y1+3;};
  // Two crossing blade flashes through the leaf band below the readout.
  for(int cutNo=0;cutNo<2;++cutNo) {
    const float start=cutNo?CUT2:CUT,age=t-start;
    if(age<0 || age>=.24f)continue;
    const float slope=cutNo?.3f:-.34f,ym=cy+(cutNo?46:38);
    const float head=-16+(w_+32)*easeOut(std::min(1.0f,age/.09f));
    const float tail=age<.09f?head-64:-16,k=(age<.09f?1:1-(age-.09f)/.15f)*fade;
    const float x0=std::max(-16.0f,tail),x1=std::min(float(w_+16),head);
    segment(x0,ym+(x0-cx)*slope,x1,ym+(x1-cx)*slope,accent_,k*.9f);
  }
  // Leaf fragments scatter off the first cut.
  for(unsigned i=0;i<10;++i) {
    const float age=t-CUT,life=.45f+.3f*rnd(i,211);
    if(age<0 || age>=life)continue;
    const float x0=14+(w_-28)*rnd(i,212),y0=cy+38-(x0-cx)*.34f;
    const float vx=(rnd(i,213)-.5f)*70,vy=-25-45*rnd(i,214);
    const auto &p=PALETTE[i%3];
    plot(x0+vx*age,y0+vy*age+85*age*age,rgb8(p[0],p[1],p[2]),(1-age/life)*fade*1.2f);
  }
  for(int i=0;i<LEAVES;++i) {
    // The first seven leaves fall into the blade band for the cut; four
    // later leaves drift in whole during the sustain, slower and uncut,
    // so the air stays alive while the sliced halves settle.
    const bool late=i>=7;
    const float birth=late?1.85f+.55f*(i-7)+.3f*rnd(i,210):0.0f,lt=t-birth;
    if(lt<0)continue;
    const float leafK=fade*(late?smooth(lt/.3f):1.0f);
    const float x0=13+(w_-26)*rnd(i,201),y0=-12+14*rnd(i,202);
    // Each cut leaf's fall speed puts it inside the blade band at the cut.
    const float vy=(cy+34+16*rnd(i,209)-y0)/CUT;
    const float swayAmp=5+5*rnd(i,204),swayHz=2.1f+1.4f*rnd(i,205),phase=6.28f*rnd(i,206);
    const float len=14+4*rnd(i,207),wid=len*.48f;
    const auto &p=PALETTE[i%3];
    const Rgb leaf=rgb8(p[0],p[1],p[2]),vein=mixed(leaf,WHITE,.45f);
    const float yAt=late?-14+(24+9*rnd(i,211))*lt:y0+vy*std::min(t,CUT)+vy*.5f*std::max(0.0f,t-CUT);
    const float xAt=x0+swayAmp*sine(t*swayHz+phase);
    float ang=phase+.8f*sine(t*swayHz*.8f+phase)+t*.35f;
    const float flip=.35f+.65f*std::fabs(sine(t*2.6f+phase*2));
    const int halves=late || t<CUT?1:2;
    for(int h=0;h<halves;++h) {
      float hx=xAt,hy=yAt,ha=ang;
      if(halves==2) { // halves part across the cut and tumble away
        const float age=t-CUT,sep=easeOut(age/.3f)*7.0f,dir=h?1.0f:-1.0f;
        hx+=dir*sep*.8f;hy+=dir*sep*.45f+age*4*h;
        // The tumble is fast just after the cut, then eases to a lazy turn.
        ha+=dir*(age<1?age:1+.25f*(age-1))*(2.2f+1.6f*rnd(i*2+h,208));
      }
      const float ca=cosine(ha),sa=sine(ha);
      const float s0=halves==1?-1.0f:(h?0.0f:-1.0f),s1=halves==1?1.0f:(h?1.0f:0.0f);
      const int steps=halves==1?10:5;
      for(int j=0;j<=steps;++j) {
        const float s=s0+(s1-s0)*float(j)/steps;
        const float ax=hx+ca*s*len*.5f,ay=hy+sa*s*len*.5f;
        const float hw=wid*.5f*(1-s*s)*flip;
        if(clear(ax-sa*hw,ay+ca*hw))plot(ax-sa*hw,ay+ca*hw,leaf,leafK*1.15f);
        if(hw>.6f && clear(ax+sa*hw,ay-ca*hw))plot(ax+sa*hw,ay-ca*hw,leaf,leafK*1.15f);
        if(hw>1.0f) { // a soft fill between the edges and the vein
          if(clear(ax-sa*hw*.5f,ay+ca*hw*.5f))plot(ax-sa*hw*.5f,ay+ca*hw*.5f,leaf,.55f*leafK);
          if(clear(ax+sa*hw*.5f,ay-ca*hw*.5f))plot(ax+sa*hw*.5f,ay-ca*hw*.5f,leaf,.55f*leafK);
        }
        if(clear(ax,ay))plot(ax,ay,vein,.85f*leafK);
      }
    }
  }
}

// ---- Bloom: buds below the readout unfurl into solid-petalled flowers,
// hold at full bloom, then burst into a shower of petals and pollen ---------
inline void Renderer::bloom(float t) {
  constexpr float BURST=1.65f;
  constexpr int FLOWERS=5,PETALS=5;
  glint(t,1.28f,.35f);
  const float cx=number_.cx(),cy=number_.cy();
  const Rgb gold=rgb8(255,210,95);
  const float travelK=t>BURST?(1-std::exp(-2.0f*(t-BURST)))/2.0f:0; // shared drag term
  const float sinkK=t>BURST?1-std::exp(-.55f*(t-BURST)):0; // shared slow-sink term
  // A small solid teardrop petal from (ox,oy) along angle a: a filled row of
  // plots across its width at each step, pointed at both ends.
  auto petal=[&](float ox,float oy,float a,float len,float wid,const Rgb &c,const Rgb &tip,float k){
    const float ca=cosine(a),sa=sine(a);
    const int steps=std::max(3,int(len*.85f));
    for(int j=1;j<=steps;++j) {
      const float s=float(j)/steps,px=ox+ca*s*len,py=oy+sa*s*len;
      const float hw=wid*(s<.5f?.25f+1.5f*s:1-.95f*(s-.5f));
      const Rgb &col=s>.78f?tip:c;
      const int wo=int(hw); // every integer offset across, so the fill is solid
      for(int o=-wo;o<=wo;++o)plot(px-sa*o,py+ca*o,col,k);
    }
  };
  for(int f=0;f<FLOWERS;++f) {
    float fx,fy;
    if(f==0){fx=19;fy=number_.y1+21;}
    else if(f==1){fx=w_-20.0f;fy=number_.y1+21;}
    else if(f==2){fx=cx-27;fy=number_.y1+37;}
    else if(f==3){fx=cx+27;fy=number_.y1+37;}
    else{fx=cx;fy=number_.y1+24;}
    fx+=(rnd(f,221)-.5f)*4;fy+=(rnd(f,222)-.5f)*3;
    if(t>BURST-.4f && t<BURST)fx+=.6f*sine(t*2+f*1.3f); // held blooms sway a little
    static constexpr int PETAL_RGB[5][3]={{255,135,185},{255,95,135},{255,170,150},{235,150,255},{255,200,220}};
    const auto &pr=PETAL_RGB[f];
    const Rgb petalC=mixed(rgb8(pr[0],pr[1],pr[2]),accent_,f==4?.3f:.1f),tipC=mixed(petalC,WHITE,.45f);
    const float maxLen=(f==4?13.5f:11.5f)+2*rnd(f,223),maxWid=maxLen*.24f,rot=6.28f*rnd(f,224);
    const float budT=smooth((t-.04f*f)/.4f),open=easeOut((t-.45f-.05f*f)/.8f);
    if(t<BURST) {
      if(open<=0) { // a closed bud: a solid green base swelling to a coloured tip
        if(budT<=0)continue;
        const Rgb green=rgb8(95,195,95);
        plot(fx,fy+2,green,.95f);plot(fx-1.4f,fy+1,green,.85f);plot(fx+1.4f,fy+1,green,.85f);
        plot(fx,fy+.5f,petalC,budT);plot(fx-1,fy-.5f-1.5f*budT,petalC,budT);plot(fx+1,fy-.5f-1.5f*budT,petalC,budT);
        plot(fx,fy-1-3.5f*budT,tipC,budT);
        continue;
      }
      for(int p=0;p<PETALS;++p) {
        // Petals start furled upward and spread to a radial rosette.
        const float radial=rot+p*(2*HALF_TURN/PETALS);
        const float furled=-HALF_TURN/2+(p-PETALS/2.0f)*.15f;
        float d=radial-furled;
        while(d>HALF_TURN)d-=2*HALF_TURN;
        while(d<-HALF_TURN)d+=2*HALF_TURN;
        petal(fx,fy,furled+d*open,maxLen*(.3f+.7f*open),maxWid*open,petalC,tipC,1.0f);
      }
      if(open>.35f) { // a solid gold heart once the flower is half open
        const float k=(open-.35f)*1.5f;
        plot(fx,fy,WHITE,k);plot(fx-1.4f,fy,gold,k);plot(fx+1.4f,fy,gold,k);plot(fx,fy-1.4f,gold,k);plot(fx,fy+1.4f,gold,k);
        plot(fx-2.4f,fy,gold,k*.6f);plot(fx+2.4f,fy,gold,k*.6f);plot(fx,fy-2.4f,gold,k*.6f);plot(fx,fy+2.4f,gold,k*.6f);
      }
      continue;
    }
    // Burst: every petal lets go and flutters outward, then sinks slowly
    // through the sustain on the shared drag and sink terms, still
    // tumbling but ever more lazily, and fades in the settle.
    const float age=t-BURST,fade=1-smooth((age-2.3f)/.95f);
    if(fade<=0)continue;
    if(age<.25f)twinkle(fx,fy,gold,(1-age/.25f)*.9f,2);
    // The gold seed head stays behind after the petals leave, swaying on
    // its stem and cooling through the sustain.
    {
      const float hk=fade*(1-smooth((age-2.2f)/.9f)),sx=fx+.8f*sine(age*1.8f+f*1.3f);
      if(hk>0){plot(sx,fy,WHITE,hk*.8f);plot(sx-1.4f,fy,gold,hk*.8f);plot(sx+1.4f,fy,gold,hk*.8f);plot(sx,fy+2.2f,rgb8(95,195,95),hk*.7f);plot(sx,fy+3.6f,rgb8(95,195,95),hk*.5f);}
    }
    for(int p=0;p<PETALS;++p) {
      const unsigned seed=f*PETALS+p;
      const float radial=rot+p*(2*HALF_TURN/PETALS);
      const float bx=fx+cosine(radial)*maxLen*.35f,by=fy+sine(radial)*maxLen*.35f;
      const float sp=30+38*rnd(seed,231),vx=sp*cosine(radial),vy=sp*sine(radial)-16;
      const float x=bx+vx*travelK+2.6f*sine(age*7+seed)*std::min(age,1.2f),y=by+vy*travelK+52*sinkK+3*sine(age*3.1f+seed)*std::min(age,1.0f);
      const float a=radial+(age<1?age:1+.3f*(age-1))*(2.5f+2*rnd(seed,232))*(p%2?1:-1);
      petal(x,y,a,maxLen*.72f,maxWid*.7f,petalC,tipC,fade);
    }
  }
  // Pollen motes are shed from the burst onward, in ones and twos through
  // the whole sustain, each drifting out and settling.
  if(t>BURST)for(unsigned i=0;i<12;++i) {
    const float birth=BURST+2.2f*rnd(i,246),life=.9f+.5f*rnd(i,241),age=t-birth;
    if(age<0 || age>=life)continue;
    const float a=2*HALF_TURN*rnd(i,242),v=(42+62*rnd(i,243))*(age*(1-.25f*age));
    const float ox=rnd(i,244)<.5f?-28:28;
    plot(cx+ox*rnd(i,245)+v*cosine(a)*.5f,cy+16+v*sine(a)*.5f+20*sinkK*(age/life),gold,(1-age/life)*1.1f);
  }
}
}
