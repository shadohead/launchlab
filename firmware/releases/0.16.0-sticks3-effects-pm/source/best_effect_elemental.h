#pragma once
// Elemental celebrations: Renderer members declared in best_effect_ui.h, which
// includes this file after the class. Same rules as the others: additive light
// on the background outside the UI halo, a pure function of elapsed time, and
// the readout exactly as drawn well before the end. Three touch the readout's
// own pixels and give them back unchanged before the settle: Frost and
// Radiance brighten its glyphs (never darker in any channel), Eclipse dims
// them toward the background, and Gale leans its rows sideways.
// Cost, measured at 80 MHz: an addw that passes its weight test costs about a
// microsecond, a spark of radius r costs (2*ceil(r)+1)^2 of them, and every
// pass over the readout's pixels a few hundred more; so strokes are thin,
// particles are dots, and each effect keeps under about 1,600 light probes
// and one pass over the readout per frame.
namespace BestEffectUI {

// Pulls the anchor's ink toward `target` by k8 (Q8): toward the background
// when darkening, and in each channel only upward when lightening. A shaded
// glyph pixel never takes the background's exact value, so it still blocks
// decoration as drawn. The RPM stays readable on any theme: a pixel is never
// shaded below 3:1 against the background (below its own contrast if it
// started lower), so the pull stops at that point.
inline void Renderer::shadeAnchor(const Ink &target,int k8,bool lighten) {
  if(k8<=0)return;
  const Box &b=number_;
  const int tr=target.r>>8,tg=target.g>>8,tb=target.b>>8;
  const uint16_t bg=load(bg_);
  auto shaded=[&](unsigned v,int k) {
    int r=v>>11,g=(v>>5)&63,bl=v&31;
    if(lighten){r+=((std::max(r,tr)-r)*k)>>8;g+=((std::max(g,tg)-g)*k)>>8;bl+=((std::max(bl,tb)-bl)*k)>>8;}
    else {r+=((tr-r)*k)>>8;g+=((tg-g)*k)>>8;bl+=((tb-bl)*k)>>8;}
    return uint16_t(r<<11|g<<5|bl);
  };
  // The readout is drawn in one colour, so the last result is remembered:
  // the per-pixel channel arithmetic was most of this pass's cost.
  uint16_t lastIn=bg_,lastOut=bg_;
  for(int y=b.y0;y<=b.y1;++y) {
    uint16_t *row=px_+y*w_;
    for(int x=b.x0;x<=b.x1;++x) {
      const uint16_t p=row[x];if(p==bg_)continue;
      if(p!=lastIn) {
        const unsigned v=load(p);
        const uint32_t floor=std::min(Palette::contrast100(uint16_t(v),bg),READOUT_CONTRAST100);
        uint16_t out=shaded(v,k8);
        if(Palette::contrast100(out,bg)<floor) {
          // The strongest pull that keeps the floor, by bisection (k=0 keeps it).
          int lo=0,hi=k8;
          while(hi-lo>1){const int mid=(lo+hi)/2;(Palette::contrast100(shaded(v,mid),bg)>=floor?lo:hi)=mid;}
          out=shaded(v,lo);
        }
        out=store(out);
        lastIn=p;lastOut=out==bg_?uint16_t(out^store(1)):out;
      }
      row[x]=lastOut;++plots_;
    }
  }
}
// Leans the anchor's rows sideways in place: row y moves by shear*(centre-y)
// px, so the top leans right; vacated pixels take the background. Rows are
// moved whole with memmove. shear 0 leaves the readout as drawn.
inline void Renderer::leanAnchor(float shear) {
  const Box &b=number_;const float cy=b.cy();
  for(int y=b.y0;y<=b.y1;++y) {
    const int d=std::max(-b.x0,std::min(w_-1-b.x1,roundi(shear*(cy-y))));if(!d)continue;
    uint16_t *row=px_+y*w_;const int n=b.x1-b.x0+1;
    std::memmove(row+b.x0+d,row+b.x0,size_t(n)*2);
    if(d>0)std::fill(row+b.x0,row+b.x0+d,bg_);else std::fill(row+b.x1+d+1,row+b.x1+1,bg_);
    plots_+=unsigned(n);
  }
}
// Points of an axis-aligned ellipse, `density` per pixel of radius.
inline void Renderer::ellipseAt(float cx,float cy,float rx,float ry,const Rgb &c,float k,float density) {
  if(k<=.02f)return;
  const int n=std::max(12,int((rx+ry)*density));const float step=2*HALF_TURN/n;const Ink ink=inkOf(c);const int kq=q8(k);
  for(int i=0;i<n;++i){const float a=i*step;addw(roundi(cx+rx*cosine(a)),roundi(cy+ry*sine(a)),ink,kq);}
}
// A point split between the two pixels above and below it: smooth vertical
// motion for horizontal strokes at half a dot's cost.
inline void Renderer::vdot(int x,float y,const Ink &c,int kq) {
  const int iy=floori(y),ay=q8(y-iy);
  addw(x,iy,c,(kq*(256-ay))>>8);addw(x,iy+1,c,(kq*ay)>>8);
}

// ---- Tidal: the tide floods up the screen, crashes against the readout in
// a splash and rings out, rolls at rest below the readout, then drains away.
// The sea is drawn as its surface line, a short gradient below it and a few
// bubbles, never a filled body, so a frame touches only the rows around the
// surface.
inline void Renderer::tidal(float t) {
  constexpr float RISE=.6f,EBB=3.55f,DRAINED=4.75f,G=420;
  const Rgb foam=rgb8(215,248,255),spray=rgb8(120,206,255);
  const Rgb ripple=mixed(spray,accent_,.35f);
  const float cx=number_.cx(),rest=std::min(float(bottom_-8),number_.y1+12.0f);
  // While it rests the sea rolls: a long swell travels across it and the
  // level breathes; both ease in after the crash and out as it drains.
  const float roll=smooth((t-1.45f)/.8f)*(1-smooth((t-EBB)/.5f));
  // The level rises fast and slows as it arrives, then drains away.
  const float level=(t<RISE?h_+6-(h_+6-rest)*easeOut(t/RISE):t<EBB?rest:rest+(h_+10-rest)*smooth((t-EBB)/(DRAINED-EBB)))+roll*1.2f*sine(t*1.7f);
  glint(t,RISE-.04f,.45f);
  // Surface height per column, shared by the crest, the foam glints and the
  // droplets hopping off it.
  float ys[MAX_W];
  const bool sea=level<h_+4;
  if(sea) {
    const float u=t-RISE,rising=t<RISE?1-t/RISE:0;
    const float chop=1.2f+2.6f*rising,ring=u>0?3*std::exp(-1.8f*u):0,surge=u>0?10*(1-smooth(u/.4f)):0,wave=16*u,swell=1.7f*roll;
    const Ink crest=inkOf(mixed(foam,spray,.35f)),cap=inkOf(foam),deep=inkOf(rgb8(20,96,255));
    for(int x=0;x<w_;++x) {
      const float dx=std::fabs(x-cx),e=dx*(1/15.0f);
      const float y=level-chop*sine(x*.11f-t*9)-.6f*chop*sine(x*.23f+t*6)-swell*sine(x*.045f-t*2.3f)-(u>0?ring*sine(dx*.3f-wave)+surge/(1+e*e):0);
      ys[x]=y;vdot(x,y,crest,250);
      const int iy=floori(y);
      if(x&1)addw(x,iy-1,cap,80);
      addw(x,iy+2,deep,170);addw(x,iy+3,deep,120);addw(x,iy+4,deep,75);addw(x,iy+5,deep,35);
    }
    // Bubbles rise through the water below the surface.
    for(unsigned i=0;i<10;++i) {
      const float depth=8+70*rnd(i,107),y=level+depth-fmodf(t*(30+30*rnd(i,108))+depth,depth);
      if(y>level+5)dot(5+(w_-10)*rnd(i,109)+2*sine(t*8+i),y,spray,.55f);
    }
    // Foam glints ride the crests across the screen.
    if(roll>0)for(unsigned i=0;i<4;++i) {
      const float x=fmodf(17+i*37+t*(13+7*rnd(i,175)),float(w_-4))+2,edge=std::min(1.0f,std::min(x,w_-1-x)*.1f);
      twinkle(x,ys[int(x)]-1.5f,foam,.75f*roll*edge*(.55f+.45f*sine(t*(4.3f+1.7f*i)+i*1.3f)),1);
    }
  }
  if(t<RISE)return;
  const float u=t-RISE,calm=1-smooth((t-EBB)/.35f);
  // Ripple rings spread over the surface in perspective, one after another.
  for(int r=0;r<3;++r){const float a=(u-.3f*r)/1.0f;if(a>0 && a<1)ellipseAt(cx,rest+2,8+66*easeOut(a),(8+66*easeOut(a))*.15f,ripple,.8f*(1-a)*calm,1.8f);}
  // The splash: a crown of droplets thrown up from the flanks of the surge
  // falls back to the sea, each landing with a plink.
  for(unsigned i=0;i<48;++i) {
    const float a=u-.06f*rnd(i,101);if(a<=0)continue;
    const float side=i%2?1:-1,x0=cx+side*(8+26*rnd(i,104));
    const float vx=side*(20+100*rnd(i,102)),vy=-(140+190*rnd(i,103));
    const float land=-2*vy/G,x=x0+vx*a;
    if(a<land) {
      const float y=rest-3+vy*a+.5f*G*a*a,k=std::min(1.0f,1.4f-a/land);
      const Rgb c=i%3?spray:foam;
      plot(x-vx*.02f,y-(vy+G*a)*.02f,c,k*.45f);dot(x,y,c,k);
    } else if(a<land+.2f && calm>0) {
      const float p=(a-land)/.2f,k=(1-p)*.7f*calm;plot(x-2-3*p,level+1,ripple,k);plot(x+2+3*p,level+1,ripple,k);
    }
  }
  // While the sea rolls, droplets hop off its crests one after another and
  // ring out where they fall back.
  if(sea)for(unsigned j=0;j<12;++j) {
    const float a=t-(1.75f+.14f*j+.1f*rnd(j,171));if(a<0)continue;
    const float vy=-(75+70*rnd(j,172)),flight=-2*vy/G,vx=(rnd(j,174)-.5f)*36;
    const int x0=6+int((w_-12)*rnd(j,173));const float x=x0+vx*std::min(a,flight);
    if(a<flight) {
      const float y=ys[x0]-1+vy*a+.5f*G*a*a;
      plot(x-vx*.02f,y-(vy+G*a)*.02f,foam,.4f);dot(x,y,j%3?spray:foam,.9f);
    } else {
      const float p=(a-flight)/.75f;if(p>=1)continue;
      const float rx=2+15*easeOut(p);ellipseAt(x,level+1.5f,rx,rx*.22f,ripple,.65f*(1-p)*calm,1.8f);
    }
  }
  // As the sea drains, drips run off the readout's lower edge.
  for(unsigned i=0;i<6;++i) {
    const float a=t-(EBB-.05f+.45f*rnd(i,105));if(a<0)continue;
    const float x=number_.x0+6+(number_.x1-number_.x0-12)*rnd(i,106),y=number_.y1+3+.5f*G*.5f*a*a;
    if(y>=level || y>h_)continue;
    dot(x,y,spray,.9f);plot(x,y-1.5f,foam,.45f);
  }
}

// ---- Frost: ice ferns creep in from the edges, the readout freezes over,
// shatters into shards, and snow drifts down.
// `from` melts the fern from its root: the stem before it and the branches
// rooted there are gone, so a melting fern also costs less.
inline void Renderer::fern(float x,float y,float dx,float dy,float length,float grown,int depth,uint32_t seed,const Rgb &c,float k,float from) {
  // A straight crystal stem, brightening toward its tip, with branches at 60
  // degrees on alternating sides. Fixed point: position in Q8, weight in Q16.
  const float shown=std::min(length,grown);if(shown<1 || from>=shown)return;
  const Ink ink=inkOf(c);const int n=int(shown),x8=q8(x)+128,y8=q8(y)+128,dx8=q8(dx),dy8=q8(dy);
  const int w0=int(k*.55f*65536*256)>>8,dw=int(k*.45f*65536/shown);
  for(int s=std::max(0,int(from));s<n;++s)addw((x8+dx8*s)>>8,(y8+dy8*s)>>8,ink,(w0+dw*s)>>8);
  if(depth==0)return;
  constexpr float C60=.5f,S60=.866f;
  for(int b=0;b*5+4<n;++b) {
    const float at=4+b*5,side=b%2?1:-1,branch=(length-at)*(depth==2?.45f:.4f);
    if(at<from)continue;
    const float bx=x+dx*at,by=y+dy*at,ux=dx*C60-dy*S60*side,uy=dy*C60+dx*S60*side;
    fern(bx,by,ux,uy,branch,(grown-at)*.7f,depth-1,seed*7+b,c,k*.8f);
  }
}
// A small four-pointed glint: a bright dot and short arms.
inline void Renderer::twinkle(float x,float y,const Rgb &c,float k,int arm) {
  dot(x,y,c,k);
  for(int d=1;d<=arm;++d){const float f=k*(1-float(d)/(arm+1));plot(x+d+.5f,y,c,f);plot(x-d-.5f,y,c,f);plot(x,y+d+.5f,c,f);plot(x,y-d-.5f,c,f);}
}
inline void Renderer::frost(float t) {
  constexpr float GROW=.85f,ICE=.93f,SHATTER=1.25f,RIME=1.4f,MELT=3.5f,END=5.0f;
  const Rgb ice=rgb8(225,250,255),cyan=rgb8(110,214,255);
  const float cx=number_.cx(),cy=number_.cy();
  // Ferns creep in from the open screen edges: the top corners and both
  // sides of the band below the readout, each aimed at the readout.
  // They melt into the readout as it ices: from their roots at the screen's
  // edge toward their tips at the readout, fading as they go.
  const float fade=1-smooth((t-GROW-.12f)/.28f),melt=smooth((t-GROW+.05f)/.4f);
  if(fade>0) {
    const float grown=easeOut(t/GROW),bright=t<GROW?.75f:.75f+.25f*(1-smooth((t-GROW)/.4f));
    const Rgb c=t<GROW?cyan:mixed(ice,cyan,smooth((t-GROW)/.5f));
    const float y1=number_.y1;
    const float seeds[][4]={{0,0,cx-30,cy},{float(w_-1),0,cx+30,cy},{0,y1+8,cx-12,y1},{float(w_-1),y1+10,cx+12,y1},
                            {0,y1+52,cx-24,y1+6},{float(w_-1),y1+56,cx+24,y1+6}};
    for(unsigned i=0;i<6;++i) {
      const float x=seeds[i][0],y=seeds[i][1];
      float dx=seeds[i][2]-x,dy=seeds[i][3]-y;const float n=1/std::sqrt(dx*dx+dy*dy);dx*=n;dy*=n;
      const float length=(i<2?30:i<4?44:40)+8*rnd(i,111);
      fern(x,y,dx,dy,length,grown*length*1.4f,2,i*13+5,c,bright*fade,melt*length);
      if(t<GROW)twinkle(x+dx*grown*length,y+dy*grown*length,ice,.9f*(1-t/GROW),2);
    }
  }
  // The readout ices over, rimmed with glints, holds, then cracks clear.
  const int freeze=t<ICE?0:t<SHATTER?q8(.85f*smooth((t-ICE)/.18f)):q8(.85f*(1-smooth((t-SHATTER)/.06f)));
  shadeAnchor(inkOf(rgb8(235,252,255)),freeze,true);
  if(t>=GROW && t<SHATTER+.05f)for(unsigned i=0;i<14;++i) {
    const float a=t-GROW-.25f*rnd(i,125);if(a<0)continue;
    const float x=number_.x0+(number_.x1-number_.x0)*rnd(i,126),y=i%2?number_.y0-3.5f:number_.y1+3.5f;
    twinkle(x,y,ice,std::min(1.0f,a*10)*(.7f+.3f*sine(a*17+i))*(t<SHATTER?1:1-(t-SHATTER)/.05f),1);
  }
  glint(t,SHATTER-.02f,.4f);
  // Shards burst out from the readout's edges and fall, spinning.
  if(t>=SHATTER)for(unsigned i=0;i<30;++i) {
    const float a=t-SHATTER-.05f*rnd(i,112),life=.55f+.4f*rnd(i,113);if(a<0 || a>=life)continue;
    const float sx=number_.x0+(number_.x1-number_.x0)*rnd(i,114),sy=rnd(i,115)<.5f?number_.y0-3:number_.y1+3;
    float ox=sx-cx,oy=sy-cy;const float n=1/std::sqrt(ox*ox+oy*oy*4+1);ox*=n;oy*=n*2;
    const float speed=70+120*rnd(i,116),x=sx+ox*speed*a,y=sy+oy*speed*a-30*a+170*a*a;
    const float spin=rnd(i,117)*2*HALF_TURN+a*(12+10*rnd(i,118)),len=2.5f+2.5f*rnd(i,119),k=1-a/life;
    const float ux=cosine(spin)*len,uy=sine(spin)*len;const Rgb c=mixed(ice,cyan,a/life);
    line(x-ux,y-uy,x+ux,y+uy,c,k);plot(x+uy*.35f,y-ux*.35f,c,k*.5f);
  }
  // Rime: after the shatter, fainter ferns creep back in from the side
  // edges of the open screen below the readout, a glint now and then
  // running out along each stem, until they melt away.
  const float rime=smooth((t-RIME)/.35f)*(1-smooth((t-MELT)/.7f)),thaw=smooth((t-MELT+.1f)/.75f);
  if(rime>0) {
    const float grown=easeOut((t-RIME)/1.5f),y1=number_.y1,right=float(w_-1);
    const float seeds[][4]={{0,y1+62,cx,y1+80},{right,y1+74,cx,y1+94},{0,y1+128,cx,y1+112},{right,y1+138,cx,y1+124}};
    const Rgb c=mixed(cyan,ice,.35f);
    for(unsigned i=0;i<4;++i) {
      const float x=seeds[i][0],y=seeds[i][1];
      float dx=seeds[i][2]-x,dy=seeds[i][3]-y;const float n=1/std::sqrt(dx*dx+dy*dy);dx*=n;dy*=n;
      const float length=26+7*rnd(i,161),shown=grown*length;
      fern(x,y,dx,dy,length,shown*1.4f,2,i*17+3,c,.6f*rime,thaw*length);
      const float run=length*2.2f,s=fmodf(t*(16+9*rnd(i,162))+run*rnd(i,163),run);
      if(s<shown && s>=thaw*length)twinkle(x+dx*s,y+dy*s,ice,rime*(.45f+.55f*s/length),1);
    }
  }
  // Snowflakes: six-armed, drifting down and swaying, twinkling out. The
  // first fall from the shattered readout, the rest keep drifting through
  // the open screen below it.
  const float snow=1-smooth((t-4.15f)/(END-4.2f));
  for(unsigned i=0;i<24;++i) {
    const bool first=i<8;
    const float a=t-(SHATTER-.05f+(first?.6f*rnd(i,121):.5f+2.0f*(i-8)/16+.15f*rnd(i,127))),life=first?2.0f+.6f*rnd(i,128):1.5f+.6f*rnd(i,128);
    if(a<0 || a>=life)continue;
    const float k=std::min(1.0f,a*5)*(1-smooth((a-life+.45f)/.45f))*snow*(.7f+.3f*sine(a*9+i));
    const float x=5+(w_-10)*rnd(i,122)+3*sine(a*2.5f+i*1.7f);
    const float y=(first?number_.y0-4+90*rnd(i,123):number_.y1+6+110*rnd(i,123))+(14+12*rnd(i,124))*a;
    const Rgb c=i%3?ice:mixed(ice,accent_,.4f);
    dot(x,y,c,k);
    for(int arm=0;arm<3;++arm){const float ca=cosine(arm*HALF_TURN/3+i+a*.6f),sa=sine(arm*HALF_TURN/3+i+a*.6f);
      for(int d=2;d<=3;++d){plot(x+ca*d,y+sa*d,c,k*(d==2?.75f:.45f));plot(x-ca*d,y-sa*d,c,k*(d==2?.75f:.45f));}}
  }
}

// ---- Gale: a whisper of wind, then a gust of streaks curling at their
// heads (the wind glyph) blows through; it leans the readout over like a
// sapling, which sways back upright. The breeze lingers: lighter streaks,
// little eddies turning as they drift downwind and tumbling grit, until the
// air goes still.
inline void Renderer::gale(float t) {
  constexpr float GUST=.55f,CALM=3.4f,END=4.8f;
  // The lean: pushed over as the gust arrives, then a damped sway back.
  if(t>=GUST-.08f && t<1.8f) {
    const float u=t-GUST-.1f,s=u<0?.3f*easeOut((u+.18f)/.18f):.3f*std::exp(-3.6f*u)*cosine(10*u);
    leanAnchor(s);
  }
  const Rgb air=mixed(rgb8(210,255,240),accent_,.3f),bright=rgb8(240,255,250);
  const Ink airInk=inkOf(air),brightInk=inkOf(bright);
  glint(t,GUST,.5f);
  const float fade=1-smooth((t-CALM)/(END-CALM-.2f));
  // One streak: a single-pixel line brightening toward its head, doubled
  // near the head in the gust, and optionally curling up at the head.
  auto streak=[&](float lane,float head,float length,float phase,float k,bool thick,bool curl,float rc) {
    const float tail=head-length;if(tail>w_)return;
    const int kq=q8(k);
    for(int x=std::max(0,int(tail));x<=std::min(w_-1,int(head));++x) {
      const int u8=int((x-tail)*256/length),y=roundi(lane+2.2f*sine(x*.06f+phase+t*4));
      addw(x,y,u8>205?brightInk:airInk,(kq*u8)>>8);
      if(thick && u8>205)addw(x,y+1,airInk,(kq*u8)>>9);
    }
    // The curl: the head rolls up and back into a shrinking spiral.
    if(curl && head<w_+14) {
      const float y0=lane+2.2f*sine(head*.06f+phase+t*4),yc=y0-rc;
      for(int s=0;s<14;++s){const float p=s*(1.6f*HALF_TURN/14),r=rc*(1-.6f*s/14);plot(head+r*sine(p),yc+r*cosine(p),s<4?bright:air,k*(1-.5f*s/14));}
    }
  };
  for(unsigned i=0;i<22;++i) {
    // Three waves: a few slow scouts, the gust, and stragglers.
    const unsigned wave=i<4?0:i<17?1:2;
    const float birth=wave==0?.02f+.4f*rnd(i,131):wave==1?GUST-.16f+.55f*rnd(i,131):1.15f+.6f*rnd(i,131);
    const float speed=(wave==1?300:160)+90*rnd(i,132),a=t-birth;if(a<0)continue;
    const float length=24+34*rnd(i,133)*(wave==1?1:.7f);
    // Lanes crowd around the readout and the band below it.
    const float lane=i%3==0?number_.y0-4-7*rnd(i,134):i%3==1?number_.y1+4+12*rnd(i,134):number_.y1+20+30*rnd(i,134);
    // Two in three curl; in the gust only one lane in two of those, so it stays light.
    const bool curl=i%3!=2 && (wave!=1 || i%3==1 || i%2==0);
    streak(lane,-6+speed*a,length,6*rnd(i,135),fade*(wave==1?1.0f:.6f)*std::min(1.0f,a*8),wave==1,curl,4.5f+2.5f*rnd(i,136));
  }
  // The breeze after the gust: lighter streaks one after another, in the
  // header and the open screen below the readout, a few curling.
  for(unsigned i=0;i<14;++i) {
    const float a=t-(1.65f+.15f*i+.1f*rnd(i,181));if(a<0)continue;
    const float lane=i%4==0?number_.y0-5-6*rnd(i,182):number_.y1+16+112*rnd(i,182);
    streak(lane,-6+(150+70*rnd(i,183))*a,22+24*rnd(i,184),6*rnd(i,185),fade*.6f*std::min(1.0f,a*6),false,i%3==0,3.5f+2*rnd(i,186));
  }
  // Eddies the gust left behind: little whirls turning as they drift
  // downwind, rising in and fading out.
  for(unsigned i=0;i<7;++i) {
    const float a=t-(1.1f+.36f*i+.15f*rnd(i,187)),life=1.4f+.6f*rnd(i,188);if(a<0 || a>=life)continue;
    const float k=.75f*fade*smooth(a/.35f)*(1-smooth((a-life+.5f)/.5f));if(k<=0)continue;
    const float ex=8+(w_-60)*rnd(i,189)+(24+18*rnd(i,190))*a,ey=number_.y1+14+120*rnd(i,191)+3*sine(a*2.2f+i);
    const float rc=4.5f+2.5f*rnd(i,192),spin=-(4+2*rnd(i,193))*a+2*HALF_TURN*rnd(i,194);
    for(int s=0;s<22;++s) {
      const float p=spin+s*(1.7f*HALF_TURN/22),r=rc*(1-.65f*s/22);
      plot(ex+r*cosine(p),ey+r*.75f*sine(p),s<4?bright:air,k*(1-.55f*s/22));
    }
  }
  // Grit and seeds tumble along in the gust, and more drift by after it.
  for(unsigned i=0;i<12;++i) {
    const float a=t-(GUST-.05f+.6f*rnd(i,137));if(a<0)continue;
    const float x=-4+(200+80*rnd(i,138))*a,y=number_.y1+6+30*rnd(i,139)+6*sine(a*9+i)-12*a;
    if(x<w_+2)dot(x,y,mixed(bright,accent_,.5f),fade*.9f);
  }
  for(unsigned i=0;i<16;++i) {
    const float a=t-(1.25f+.12f*i+.12f*rnd(i,195));if(a<0)continue;
    const float vx=60+60*rnd(i,196),x=-3+vx*a,y=number_.y1+12+120*rnd(i,197)+5*sine(a*5+i)+2*a;
    if(x>=w_+3)continue;
    const Rgb c=mixed(bright,accent_,.3f+.4f*rnd(i,198));const float k=.8f*fade*std::min(1.0f,a*6);
    dot(x,y,c,k*(.75f+.25f*sine(a*13+i)));plot(x-2.5f,y,c,k*.3f);
  }
}

// ---- Eclipse: a sun behind the readout is eclipsed. The digits darken as
// its crescent thins; in totality a pale corona and rim-lit void tendrils
// reach out; a diamond-ring flash relights the digits. The corona lingers,
// breathing, while the tendrils slowly withdraw and wisps of the dark drift
// off, then it fades.
// The built-in backgrounds are black, which no light can darken, so the
// darkness is taken from the readout itself and drawn as negative space
// between bright rims.
inline void Renderer::eclipse(float t) {
  constexpr float COVER=.75f,DIAMOND=1.6f,WANE=3.7f,END=5.2f;
  const Rgb violet=rgb8(150,70,255),pearl=mixed(rgb8(235,230,255),accent_,.25f);
  const float cx=number_.cx(),cy=number_.cy(),r=std::min(30.0f,(number_.y1-number_.y0)*.5f+13);
  // The digits darken toward the background (never below 3:1, see
  // shadeAnchor), then relight at the flash.
  const float dim=t<DIAMOND?.84f*smooth((t-.05f)/(COVER-.05f)):.84f*(1-smooth((t-DIAMOND)/.25f));
  shadeAnchor(inkOf(rgb(load(bg_))),q8(dim),false);
  // The crescent: the sun's rim, thinning toward the upper right as the moon covers it.
  constexpr float SUN=-.8f;  // the last light, up and to the right
  if(t<COVER+.06f) {
    const float edge=-1+2.1f*smooth(t/COVER),fadeOut=t<COVER?1:1-(t-COVER)/.06f;
    const int n=int(r*6.4f);
    for(int i=0;i<n;++i) {
      const float a=i*(2*HALF_TURN/n),lit=(cosine(a-SUN)-edge)*(1/.35f);if(lit<=0)continue;
      const float k=std::min(1.0f,lit)*fadeOut,ca=cosine(a),sa=sine(a);
      plot(cx+r*ca,cy+r*sa,WHITE,k);plot(cx+(r+1)*ca,cy+(r+1)*sa,pearl,k*.8f);plot(cx+(r+2)*ca,cy+(r+2)*sa,pearl,k*.35f);
    }
  }
  // Totality and after: corona streamers and void tendrils. The corona
  // breathes, its streamers each swelling on their own slow beat.
  const float total=smooth((t-COVER+.1f)/.25f)*(1-smooth((t-WANE)/(END-WANE-.45f)));
  if(total>0) {
    // Once the light is back the corona opens out: longer, brighter streamers.
    const float breath=.78f+.22f*sine(t*2.2f-1.2f),open=smooth((t-DIAMOND)/.6f);
    ellipseAt(cx,cy,r,r,pearl,.35f*total*breath,3.2f);
    const Ink pearlInk=inkOf(pearl),violetInk=inkOf(violet);
    for(unsigned j=0;j<22;++j) {
      const float a=j*(2*HALF_TURN/22)+.08f*rnd(j,141),len=(6+18*rnd(j,142))*(1+.7f*open)*(.62f+.38f*sine(t*(1.3f+.8f*rnd(j,147))+j*2.4f))*total*breath;
      const float ca=cosine(a),sa=sine(a);const int kq=q8((.5f+.35f*open)*total*breath);
      for(float s=1;s<len;s+=1)addw(roundi(cx+(r+s)*ca),roundi(cy+(r+s)*sa),pearlInk,int(kq*(1-s/len)));
    }
    // The tendrils reach out in totality and draw back slowly once the light returns.
    const float reach=easeOut((t-COVER)/.6f)*(t<DIAMOND+.15f?1:1-smooth((t-DIAMOND-.15f)/2.3f));
    if(reach>0)for(unsigned i=0;i<5;++i) {
      const float base=i*(2*HALF_TURN/5)+.5f+.4f*rnd(i,143),len=(34+30*rnd(i,144))*reach;
      float px=cx+r*cosine(base),py=cy+r*sine(base);
      for(float s=0;s<len;s+=1.4f) {
        // A wavering path; rims on both sides with the core left dark.
        const float a=base+.55f*sine(s*.07f+t*2.6f+i*2),ca=cosine(a),sa=sine(a);
        px+=ca*1.4f;py+=sa*1.4f;
        const float width=1+2.8f*(1-s/len);const int kq=q8(total*(.4f+.6f*(1-s/len)));
        addw(roundi(px-sa*width),roundi(py+ca*width),violetInk,kq);addw(roundi(px+sa*width),roundi(py-ca*width),violetInk,kq);
      }
      if(len>2)dot(px,py,violet,.8f*total);
    }
  }
  // The diamond ring: the first light back, a point flare with rays.
  const float f=t-DIAMOND;
  if(f>=0 && f<.6f) {
    const float k=f<.06f?f/.06f:1-smooth((f-.06f)/.54f);
    const float x=cx+r*cosine(SUN),y=cy+r*sine(SUN);
    spark(x,y,pearl,1.5f*k,2.5f);
    for(int ray=0;ray<4;++ray) {
      const float a=ray*HALF_TURN/4+.2f,len=(ray%2?14:30)*(.6f+.4f*k),ca=cosine(a),sa=sine(a);
      line(x-ca*len,y-sa*len,x+ca*len,y+sa*len,mixed(WHITE,pearl,.3f),k*.7f);
    }
  }
  glint(t,DIAMOND,.45f);
  // Wisps of the dark drift away as it lifts, one after another.
  for(unsigned i=0;i<26;++i) {
    const float a=t-(DIAMOND+.1f+(i<12?.45f*rnd(i,145):.45f+2.4f*(i-12)/14+.15f*rnd(i,148))),life=i<12?.6f:.95f;
    if(a<0 || a>=life)continue;
    const float ang=2*HALF_TURN*rnd(i,146),d=r+8+(i<12?40:30)*easeOut(a/life);
    const float k=(1-a/life)*std::min(1.0f,a*8);
    if(i<12)dot(cx+d*cosine(ang),cy+d*sine(ang)*.8f,violet,.8f*k);else twinkle(cx+d*cosine(ang),cy+d*sine(ang)*.8f,violet,k,1);
  }
}

// ---- Radiance: motes of light spiral in and gather, the readout flares
// white-gold and bursts into long rays. The rays settle into a softer
// crown that keeps turning slowly, each ray breathing on its own beat,
// while motes float up and twinkle; then the light fades.
inline void Renderer::radiance(float t) {
  constexpr float BURST=.65f,DIM=3.3f,END=5.0f;
  const Rgb gold=rgb8(255,232,160),warm=mixed(gold,accent_,.4f);
  const float cx=number_.cx(),cy=number_.cy(),hw=(number_.x1-number_.x0)*.5f+3,hh=(number_.y1-number_.y0)*.5f+3;
  const float b=t-BURST;
  // The flare: the digits blaze toward warm white, then cool to their colour.
  shadeAnchor(inkOf(rgb8(255,246,215)),b<0?q8(.5f*smooth((t-BURST+.15f)/.15f)):q8(.85f*(1-smooth((b-.03f)/.19f))),true);
  glint(t,BURST+.12f,.45f);
  // Gathering: motes spiral inward, accelerating, and vanish behind the digits.
  if(b<.02f)for(unsigned i=0;i<22;++i) {
    const float arrive=BURST-.15f*rnd(i,151),u=t/arrive;if(u>=1)continue;
    const float a0=2*HALF_TURN*rnd(i,152),r0=55+60*rnd(i,153);
    for(int trail=3;trail>=0;--trail) {
      const float v=std::max(0.0f,u-trail*.04f),r=r0*(1-v*v*v)+hw*.3f*v,a=a0+1.3f*v;
      const float x=cx+r*cosine(a),y=cy+r*sine(a)*.8f,k=(.35f+.65f*u)*(1-trail*.22f);
      if(trail)plot(x,y,warm,k*.7f);else twinkle(x,y,i%3?gold:WHITE,k*1.2f,1);
    }
  }
  // The burst: rays from the readout's edge, alternately long and short.
  // They ease back from the burst to a crown at about 0.7 of its light and
  // 0.8 of its reach, which keeps turning until it fades.
  if(b<0)return;
  const float hold=.72f+.28f*(1-smooth((b-.06f)/1.0f));
  const float k=b<.06f?b/.06f:hold*(1-smooth((t-DIM)/(END-DIM-.4f))),reach=easeOut(b/.32f)*(1-.2f*smooth((b-.3f)/1.2f));
  if(k>0)for(unsigned j=0;j<14;++j) {
    const float a=j*(2*HALF_TURN/14)+.1f*rnd(j,154)+.25f*b,ca=cosine(a),sa=sine(a);
    // Leave from the ellipse around the readout.
    const float ex=ca/hw,ey=sa/hh,r0=1/std::sqrt(ex*ex+ey*ey);
    const float beat=sine(t*(1.2f+.7f*rnd(j,159))+j*1.9f),settled=smooth((b-.4f)/.8f);
    const bool longRay=j%2==0;const float len=(longRay?75:42)*(.8f+.4f*rnd(j,155))*reach*(1+.12f*settled*beat);
    const Ink c=inkOf(longRay?gold:warm),core=WHITE_INK;const int kq=q8(k*(longRay?1.25f:.9f)*(1+.3f*settled*beat));
    for(float s=0;s<len;s+=1) {
      const float fall=1-s/len,x=cx+(r0+s)*ca,y=cy+(r0+s)*sa;const int kk=int(kq*fall*fall);
      addw(roundi(x),roundi(y),s<6?core:c,kk);
      if(longRay && s<len*.25f)addw(roundi(x-sa),roundi(y+ca),c,kk>>1);
    }
  }
  // Motes rise from below the readout and twinkle, the first with the
  // burst and more one after another while the crown turns.
  const float out=1-smooth((t-4.25f)/(END-4.3f));
  for(unsigned i=0;i<28;++i) {
    const bool first=i<14;
    const float a=b-.1f-(first?.6f*rnd(i,156):.55f+2.3f*(i-14)/14+.15f*rnd(i,160)),life=1.4f+.5f*rnd(i,161);
    if(a<0 || a>=life)continue;
    const float x=number_.x0+(number_.x1-number_.x0)*rnd(i,157)+3*sine(a*4+i),y=number_.y1+(first?8+40*rnd(i,158):14+60*rnd(i,158))-(first?20:16)*a;
    const float k=(.6f+.4f*sine(a*11+i))*std::min(1.0f,a*5)*(1-smooth((a-life+.4f)/.4f))*out;
    if(first)dot(x,y,i%2?gold:warm,k);else twinkle(x,y,i%2?gold:warm,k,1);
  }
}
}
