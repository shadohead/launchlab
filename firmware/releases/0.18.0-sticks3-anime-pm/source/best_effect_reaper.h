#pragma once
// The reaper set of 0.18 (Crescent..Blink): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Same rules as
// the other sets: additive light on the background outside the UI halo, a
// pure function of elapsed time with fixed seeds, the readout exactly as
// drawn from three quarters of the duration, every frame within the cost
// model's 8 ms.
namespace BestEffectUI {

inline void Renderer::reaper(unsigned which,float t) {
  switch(which) {
    case 0:reaperCrescent(t);break;
    case 1:reaperRelease(t);break;
    case 2:reaperPetalfall(t);break;
    case 3:reaperMask(t);break;
    case 4:reaperBlink(t);break;
    default:break;
  }
}

// addw, skipping the readout's box and its halo without building its mask
// words: light there is blocked anyway, so a stroke passing behind the
// readout costs nothing for the rows it hides behind.
inline void Renderer::reaperAdd(int x,int y,const Ink &c,int w) {
  const Box &b=number_;
  if(y>=b.y0-HALO && y<=b.y1+HALO && x>=b.x0-HALO && x<=b.x1+HALO)return;
  addw(x,y,c,w);
}
inline void Renderer::reaperVdot(int x,float y,const Ink &c,int kq) {
  const int iy=floori(y),ay=q8(y-iy);
  reaperAdd(x,iy,c,(kq*(256-ay))>>8);reaperAdd(x,iy+1,c,(kq*ay)>>8);
}
// A point split between the two pixels left and right of it: smooth
// sideways motion for near-vertical strokes at half a dot's cost.
inline void Renderer::reaperHdot(float x,int y,const Ink &c,int kq) {
  const int ix=floori(x),ax=q8(x-ix);
  addw(ix,y,c,(kq*(256-ax))>>8);addw(ix+1,y,c,(kq*ax)>>8);
}
// 1/sqrt(v) for v > 0 without a library call: the bit-level first guess and
// two Newton steps (relative error under 1e-5).
inline float Renderer::reaperRsqrt(float v) {
  uint32_t i;std::memcpy(&i,&v,4);i=0x5f3759dfu-(i>>1);float y;std::memcpy(&y,&i,4);
  y*=1.5f-.5f*v*y*y;y*=1.5f-.5f*v*y*y;return y;
}
// A flame tongue drawn only as its two rims, so its core stays as dark as
// the background: from a base `half` px either side of (bx,by) to the tip.
inline void Renderer::reaperTongue(float bx,float by,float tx,float ty,float half,const Ink &c,int kq) {
  const float dx=tx-bx,dy=ty-by,inv=half/std::sqrt(dx*dx+dy*dy+.01f),nx=-dy*inv,ny=dx*inv;
  sorcLine(bx+nx,by+ny,tx,ty,c,kq,kq>>1);sorcLine(bx-nx,by-ny,tx,ty,c,kq,kq>>1);
}

// ---- Crescent: dark energy gathers low on the screen as flickering
// tongues with red rims, then one swing fires a screen-wide crescent: a
// black body between a white-hot leading edge and red rims, its horns
// trailing. It races up the screen past the readout and off the top. The
// air it tore stays open behind it: a long rift, black between jagged red
// rims, opening upward from the launch point to the readout's foot.
// Sustain: the rift's rims flicker and shed licks of flame and embers, and
// it seals slowly from both ends toward its middle. Settle: the last
// embers cool.
inline void Renderer::reaperCrescent(float t) {
  constexpr float LAUNCH=.6f,FLY=.7f,SEAL=2.3f,SEALED=1.5f,END=4.9f;
  const Box &b=number_;const float cx=b.cx();
  const Rgb red=rgb8(255,28,34),crimson=rgb8(140,0,16),hot=rgb8(255,232,222);
  const Ink redInk=inkOf(red),crimsonInk=inkOf(crimson),hotInk=inkOf(hot);
  const int frame=floori(t*20+.001f);
  // The launch point, low on the right; the crescent's track leans left as it rises.
  const float ly=std::min(float(bottom_)-40,b.y1+108.0f),lx=cx+16,top=b.y1+5.0f;
  auto trackX=[&](float y){return lx-.1f*(ly-y);};
  constexpr float H=62,B=28,T=22;
  const float speed=(ly+60)/FLY,tCross=LAUNCH+(ly-b.y1)/speed,TEAR=(ly-top)/speed;
  glint(t,tCross-.02f,.4f);
  // The crescent at centre (ccx,cy), scale sc: per column, a white-hot
  // leading edge with a red glow ahead and a red rim behind it, the dark
  // body, and the back rim; short smears trail off the back rim.
  auto crescent=[&](float ccx,float cy,float sc,float k,bool smear) {
    const float h=H*sc,bb=B*sc,th=T*sc;
    const int x0=std::max(0,int(std::ceil(ccx-h))),x1=std::min(w_-1,int(ccx+h));
    for(int x=x0;x<=x1;++x) {
      const float s=(x-ccx)/h,s2=s*s,q=1-s2,fy=cy+bb*s2-th*.5f*q,by=cy+bb*s2+th*.5f*q;
      if(by<-2 || fy>bottom_+2)continue;
      const int kq=q8(k*std::min(1.0f,.3f+q*1.6f));
      reaperAdd(x,roundi(fy-1.7f),redInk,(kq*120)>>8);
      reaperVdot(x,fy,hotInk,kq);
      reaperAdd(x,roundi(fy+1.6f),redInk,(kq*210)>>8);
      if(by-fy>4)reaperVdot(x,by,redInk,(kq*200)>>8);
      if(by-fy>6)reaperAdd(x,roundi(by-1.6f),crimsonInk,(kq*150)>>8);
      if(smear && ((x+frame)&3)==0)for(int d=1;d<=4;++d)reaperAdd(x,roundi(by+d*2.4f),crimsonInk,(kq*(5-d)*30)>>8);
    }
  };
  // Anticipation: the crescent gathers at the launch point, small and
  // flickering, while tongues of dark flame lick off its front and motes
  // are drawn in.
  if(t<LAUNCH) {
    const float s=smooth(t/LAUNCH),k=std::min(1.0f,t/.2f);
    const float flick=.75f+.25f*noise(frame*60,0,509)*(1/255.0f);
    crescent(lx,ly,.32f+.18f*s,k*(.35f+.45f*s)*flick,false);
    const int n=2+int(4*s);const float sc=.32f+.18f*s;
    for(int j=0;j<n;++j) {
      const uint32_t seed=uint32_t(frame)*16+j;
      const float sx=-.8f+1.6f*(j+.25f+.5f*rnd(seed,501))/n,bx=lx+sx*H*sc,by=ly+(B*sx*sx-T*.5f*(1-sx*sx))*sc;
      const float hgt=(5+10*s)*(.6f+.4f*rnd(seed,502))*(1-.6f*sx*sx),lean=(rnd(seed,503)-.5f)*4+sx*4;
      reaperTongue(bx,by-1,bx+lean,by-1-hgt,1.6f,redInk,q8(k*(.45f+.4f*s)));
    }
    for(unsigned i=0;i<16;++i) {
      const float u=t/LAUNCH-.5f*rnd(i,504);if(u<0 || u>=1)continue;
      const float an=2*HALF_TURN*rnd(i,505)+u*2.2f,r=(1-easeOut(u))*(34+18*rnd(i,506));
      dot(lx+r*cosine(an),ly+r*sine(an)*.6f,mixed(crimson,hot,u),k*sine(HALF_TURN*u)*1.2f);
    }
  }
  // The swing: a white flash at the launch point.
  if(t>=LAUNCH && t<LAUNCH+.12f) {
    const float v=(t-LAUNCH)/.12f;twinkle(lx,ly-4,hot,(1-v)*1.6f,4);
  }
  // The flight: it grows as it leaves the swing and races up off the top.
  if(t>=LAUNCH && t<LAUNCH+FLY) {
    const float u=(t-LAUNCH)/FLY,cy=ly-speed*(t-LAUNCH);
    crescent(trackX(cy),cy,.5f+.5f*easeOut(u/.3f),1,true);
  }
  // The rift: torn open right behind the crescent, from the launch point
  // up to the readout's foot, then sealed from both ends toward its middle.
  // Each rim is jagged like torn paper: a slow wander with sharp teeth.
  const float fade=1-smooth((t-SEAL-SEALED+.25f)/.3f);
  if(t>=LAUNCH+.04f && fade>0) {
    const float c=smooth((t-SEAL)/SEALED),reach=(t-LAUNCH-.04f)/TEAR;
    for(int y=int(top);y<=int(ly);++y) {
      const float v=(ly-y)/(ly-top);if(v>reach)break;
      const float m=std::fabs(v-.5f)*2;if(m>1-c)continue;
      const float lens=sine(HALF_TURN*(.03f+.94f*v)),age=(reach-v)*TEAR;
      const float open=easeOut(age/.25f)*(1-.7f*c),half=.5f+6.5f*lens*open;
      const float wander=(noise(y*22,0,511)-128)*(1/48.0f);
      const int toothL=std::abs((y*3+5)%10-5),toothR=std::abs((y*3+1)%8-4);
      const float xc=trackX(float(y))+wander,xl=xc-half-toothL*.28f*lens,xr=xc+half+toothR*.32f*lens;
      // The rims smoulder: a slow glow travelling along them.
      const float flick=noise(y*26,frame*70,512)*(1/255.0f);
      const int k=q8(fade*(.75f+.4f*flick)*std::min(1.0f,lens*2.5f));
      reaperHdot(xl,y,redInk,k);reaperHdot(xr,y,redInk,k);
      addw(roundi(xl-1.4f),y,crimsonInk,(k*190)>>8);addw(roundi(xr+1.4f),y,crimsonInk,(k*190)>>8);
      // A white-hot inner edge while the tear is fresh.
      if(age<.45f){const int kh=(k*q8(1-age/.45f))>>8;addw(roundi(xl+1.3f),y,hotInk,kh);addw(roundi(xr-1.3f),y,hotInk,kh);}
      else {
        // Inside, a smooth deep red glow off each rim, fading to a dark
        // core: the tear opens onto dark energy.
        const int kg=(k*q8(smooth((age-.45f)/.3f)))>>8,l=roundi(xl)+2,r=roundi(xr)-2;
        for(int x=l;x<=r;++x){const int d=std::min(x-l,r-x);if(d<4)reaperAdd(x,y,d?crimsonInk:redInk,(kg*(d?150-30*d:200))>>8);}
      }
    }
    // Licks of dark flame off the open rims, a few each frame.
    if(t>LAUNCH+.3f)for(int j=0;j<3;++j) {
      const uint32_t seed=uint32_t(frame)*8+j;const float v=.2f+.6f*rnd(seed,513);
      if(v>reach || std::fabs(v-.5f)*2>.8f*(1-c))continue;
      const float y=ly-v*(ly-top),side=rnd(seed,514)<.5f?-1:1,x=trackX(y)+side*(2+6.5f*sine(HALF_TURN*v)*(1-.7f*c));
      reaperTongue(x,y,x+side*(3+5*rnd(seed,515)),y-5-6*rnd(seed,516),1.2f,redInk,q8(.6f*fade));
    }
  }
  // Embers leave the rift's rims and drift up and outward, cooling.
  for(unsigned i=0;i<34;++i) {
    const float v=.1f+.8f*rnd(i,517),born=LAUNCH+.1f+v*TEAR+2.6f*rnd(i,518),age=t-born,life=.7f+.8f*rnd(i,519);
    if(age<0 || age>=life)continue;
    const float side=i&1?1:-1,y0=ly-v*(ly-top),u=age/life;
    const float x=trackX(y0)+side*(3+5*sine(HALF_TURN*v))+side*(6+10*rnd(i,520))*age+1.5f*sine(age*7+i),y=y0-(10+16*rnd(i,521))*age;
    const float k=(1-u)*(1-smooth((t-END+.6f)/.5f))*(.7f+.3f*sine(age*23+i));
    dot(x,y,mixed(mixed(hot,red,std::min(1.0f,u*2.5f)),crimson,smooth((u-.5f)/.5f)),k*1.2f);
  }
}

// ---- Release: pressure gathers into a spinning column under the readout:
// strands of wind twist up from the floor, climbing and turning faster
// while dust is drawn in. Then a broad column of blue-white light erupts
// from the floor under the replay up to the readout, the digits flare as
// it hits them, a pressure ring runs out across the floor and dust whirls
// round its foot, flung outward as it clears. Sustain: the column stands
// narrower and flickering, light running up it, its edges crackling and
// motes rising beside it. Settle: it thins away.
inline void Renderer::reaperRelease(float t) {
  constexpr float BURST=.75f,CLEAR=1.9f,FADE=3.9f,END=5.0f;
  const Box &b=number_;const float cx=b.cx();
  const Rgb core=rgb8(240,250,255),blue=mixed(rgb8(110,175,255),accent_,.25f),dust=mixed(rgb8(200,184,152),blue,.12f);
  const Ink coreInk=inkOf(core),blueInk=inkOf(blue),midInk=inkOf(mixed(core,blue,.5f)),dustInk=inkOf(dust);
  const int frame=floori(t*20+.001f);
  const float base=std::min(float(bottom_)-50,b.y1+78.0f),colTop=b.y1+3.0f,a=t-BURST;
  glint(t,BURST+.3f,.45f);
  // The twisting strands of the gathering: three helices round the column,
  // one turn per 52 rows, the near side bright and the far side a faint
  // dotted thread, up to `height` rows above the floor.
  auto strands=[&](float radius,float height,float spin,float k) {
    const int yTop=std::max(0,int(base-height));
    for(int j=0;j<3;++j) {
      float px=-1,py=-1;
      for(int y=int(base);y>=yTop;--y) {
        const float v=(base-y)/std::max(1.0f,height),ph=spin+j*(2*HALF_TURN/3)+(base-y)*(2*HALF_TURN/52);
        const float sp=sine(ph),x=cx+radius*cosine(ph);
        const float kk=k*std::max(0.0f,std::min(1.0f,std::min((1-v)/.15f,v/.06f)));
        const int kq=q8(kk*(sp>0?.45f+.65f*sp:.2f));
        if(sp<0){if(!(y&1))reaperAdd(roundi(x),y,blueInk,kq);}
        else if(py>=0 && std::fabs(x-px)>1.5f){sorcLine(px,py,x,float(y),blueInk,kq,kq);sorcLine(px,py+1,x,float(y)+1,blueInk,kq>>1,kq>>1);}
        else{reaperAdd(roundi(x),y,blueInk,kq);if(sp>.4f)reaperAdd(roundi(x)+(cosine(ph)>0?1:-1),y,blueInk,kq>>1);}
        px=x;py=float(y);
      }
    }
  };
  if(t<BURST) {
    const float s=t/BURST,k=smooth(t/.2f);
    strands(15-3*s,(base-colTop)*(.25f+.75f*easeOut(s))+4,-(9*s*s+3*s)*2.2f,k*(.55f+.45f*s));
    for(int i=0;i<48;++i){const float an=i*(2*HALF_TURN/48);reaperAdd(roundi(cx+20*cosine(an)),roundi(base+3.5f*sine(an)),blueInk,q8(k*s*(.55f+.35f*sine(an))));}
    for(unsigned i=0;i<16;++i) {
      const float u=s*1.3f-.3f*rnd(i,531);if(u<0 || u>=1)continue;
      const float an=2*HALF_TURN*rnd(i,532)+u*3,r=(1-easeOut(u))*(40+20*rnd(i,533));
      dot(cx+r*cosine(an),base-3+r*sine(an)*.3f-6*u,dust,k*sine(HALF_TURN*u)*1.2f);
    }
    return;
  }
  // The column's half width and strength: broad at the eruption, then a
  // narrower flickering column, then thinning away.
  const float settle=1-smooth((t-FADE)/(END-FADE-.15f));
  const float flick=(noise(frame*70,0,543)-128)*(1/128.0f);
  const float half=(a<.08f?4+8*easeOut(a/.08f):12-5*smooth((a-.4f)/1.2f)+.9f*flick*smooth((a-1.2f)/.4f))*settle;
  const float pk=(a<.3f?1.0f:1-.2f*smooth((a-.3f)/1.4f))*(1-smooth((t-FADE-.2f)/(END-FADE-.3f)))*(.92f+.08f*flick);
  const float top=base-(base-colTop)*easeOut(a/.1f);
  if(pk>.02f && half>.6f) {
    // A smooth gradient across the column, white-blue in the middle fading
    // to blue at its edges, brightness running up it; the outer half is
    // drawn on alternate pixels (a checker at low weight), which keeps the
    // fill inside the frame budget.
    // The profile across the column is the same on every row, so its
    // weights (Q8) and colours are worked out once per frame and each row
    // only scales them: no float maths per pixel.
    const int run=int(t*2600),ih=std::min(15,int(half)+1),cxi=roundi(cx);const float inv=1/(half+.8f);
    int prof[33];const Ink *ink[33];
    for(int dx=-ih;dx<=ih;++dx) {
      const float d=std::fabs(dx+cx-cxi)*inv,f=1-d*d;
      prof[dx+16]=d>=1?-1:q8(d>.5f?f*1.6f:f)|(d>.5f?1<<16:0);
      ink[dx+16]=d<.3f?&coreInk:d<.6f?&midInk:&blueInk;
    }
    const float iSpan=1/(base-colTop);
    for(int y=roundi(top);y<=int(base);++y) {
      const float v=(base-y)*iSpan;
      const int sh=noise(y*26+run,0,542);
      const int kk=q8(pk*(v<.12f?v*(1/.12f):1)*(.72f+.4f*sh*(1/255.0f)));
      for(int dx=-ih;dx<=ih;++dx) {
        const int pr=prof[dx+16];if(pr<0)continue;
        if((pr>>16) && ((dx+y)&1))continue;
        reaperAdd(cxi+dx,y,*ink[dx+16],(kk*(pr&0xffff))>>8);
      }
      // Crackling edges: sparks flicker along both sides.
      for(int side=-1;side<=1;side+=2) {
        const int n=noise(y*90,frame*140+side*700,544);
        if(n>150)reaperAdd(cxi+side*(ih+1+(n>210)),y,n>200?coreInk:blueInk,q8(pk*(n-120)*(1/135.0f)));
      }
    }
    // The digits flare as the column hits them, away from the background.
    const float heat=1-smooth(a*(1/.16f));
    if(heat>0 && a>.06f) {
      const float ih7=1/(half*.7f+1);int hq[64];const int x0=roundi(cx-half*.7f),x1=roundi(cx+half*.7f);
      for(int x=x0;x<=x1 && x-x0<64;++x)hq[x-x0]=q8(heat*(1-std::fabs(x-cx)*ih7));
      for(int y=b.y0;y<=b.y1;++y)for(int x=x0;x<=x1 && x-x0<64;++x)sorcHeat(x,y,coreInk,hq[x-x0]);
    }
    // Arcs crackle off the standing column's edges.
    if(a>1.0f)for(int f=frame-1;f<=frame;++f) {
      const uint32_t seed=uint32_t(f)*4+2;if(rnd(seed,552)>.5f)continue;
      const float side=rnd(seed,553)<.5f?-1:1,y=colTop+4+(base-colTop-10)*rnd(seed,554),x=cx+side*(half+1);
      const float mx=x+side*(4+3*rnd(seed,555)),my=y-3+6*rnd(seed,556),ex=mx+side*(3+4*rnd(seed,557)),ey=my-4+8*rnd(seed,558);
      const int kq=q8(pk*(f==frame?1.1f:.35f));
      sorcLine(x,y,mx,my,coreInk,kq,kq);sorcLine(mx,my,ex,ey,blueInk,kq,kq>>2);
    }
    // The footing: a glowing ellipse where the column meets the floor.
    if(a>.25f) {
      const float rx=half+6,k=pk*smooth((a-.25f)/.3f)*(.75f+.25f*sine(t*9));
      for(int i=0;i<40;++i){const float an=i*(2*HALF_TURN/40),sa=sine(an);reaperAdd(roundi(cx+rx*cosine(an)),roundi(base+1+3*sa),sa>0?coreInk:blueInk,q8(k*(.35f+.5f*std::max(0.0f,sa))));}
    }
  }
  // A pressure ring runs out across the floor.
  if(a<.22f) {
    const float u=a/.22f,k=(1-u)*(1-u)*1.3f,r=22+85*easeOut(u);
    for(int i=0;i<60;++i){const float an=i*(2*HALF_TURN/60);reaperAdd(roundi(cx+r*cosine(an)),roundi(base+r*.035f*sine(an)),i&1?blueInk:coreInk,q8(k*(.5f+.5f*std::max(0.0f,sine(an)))));}
  }
  // Dust whirls round the column's foot, near side bright, rising a little;
  // it is flung outward as it clears, and a thinner swirl stays after.
  const float clear=smooth((t-CLEAR)/.4f);
  for(unsigned i=0;i<34;++i) {
    const bool stays=i<10;const float h=rnd(i,546);
    const float k0=smooth((a-.15f-.2f*h)/.2f)*(stays?.6f+.4f*(1-clear):1-clear)*settle;if(k0<.03f)continue;
    const float ang=2*HALF_TURN*rnd(i,547)+a*(8-2*smooth(a/1.5f)),sa=sine(ang),ca=cosine(ang);
    const float fling=stays?0:60*clear*clear*(.6f+.4f*rnd(i,548));
    const float r=half+5+14*rnd(i,549)+fling,y=base-2-16*h*(stays?.5f:1)+r*.2f*sa;
    const float k=k0*(.45f+.55f*std::max(0.0f,sa))*1.5f;
    reaperAdd(roundi(cx+r*ca),roundi(y),dustInk,q8(std::min(1.0f,k)));reaperAdd(roundi(cx+r*ca-sa*1.6f),roundi(y),dustInk,q8(k*.45f));
  }
  // Motes drift up beside the standing column.
  for(unsigned i=0;i<14;++i) {
    const float life=1.0f+.6f*rnd(i,559),clock=t+rnd(i,560)*life;const int cycle=floori(clock/life);
    const float u=clock/life-cycle;const uint32_t seed=i*13+uint32_t(cycle);
    const float k=smooth((a-1.3f)/.4f)*pk*sine(HALF_TURN*u);if(k<.03f)continue;
    const float side=rnd(seed,561)<.5f?-1:1,x=cx+side*(half+3+7*rnd(seed,562))+1.5f*sine(u*6+i),y=base-6-(base-colTop-6)*u*(.6f+.4f*rnd(seed,563));
    dot(x,y,mixed(core,blue,u),k);
  }
}

// ---- Petalfall: a blade stands point-down under the readout, a glint
// runs down it, and from the point up it pours out as a river of pink
// petals, each a small teardrop with a lighter edge. The river streams
// round the readout, faster and faster, its path breathing in and out, so
// it reads as one flowing ribbon; as the last petals leave the blade the
// whole river flashes. Sustain: the ribbon keeps swirling round, slower, petals
// tumbling and catching the light. Settle: its head spirals out and the
// whole ribbon follows it away, scattering.
inline void Renderer::reaperPetalfall(float t) {
  constexpr float SCATTER=.4f,CLOSE=1.4f,LEAVE=3.5f,END=5.1f,LAG=.012f,THETA0=.5f;constexpr int N=72;
  const Box &b=number_;const float cx=b.cx(),yc=b.y1+20.0f;
  const float rx=std::min((b.x1-b.x0)*.5f-2,w_*.5f-12),ry=38;
  const Rgb pink=rgb8(255,120,175),pale=rgb8(255,214,230),deep=rgb8(220,55,130),silver=rgb8(225,232,245);
  const Ink pinkInk=inkOf(pink),paleInk=inkOf(pale),deepInk=inkOf(deep),silverInk=inkOf(silver),whiteInk=WHITE_INK;
  // The blade: point down under the readout, beside the replay's centre line.
  const float bx=cx+21,bTop=b.y1+9.0f,bTip=b.y1+58.0f,len=bTip-bTop;
  glint(t,CLOSE-.02f,.45f);
  // How much of the blade has poured out, from the point up.
  const float gone=clamp01((t-SCATTER)/(N*LAG*1.15f));
  if(gone<1) {
    const float k=smooth(t/.2f);
    const float shine=t<SCATTER?bTop+(len+10)*smooth((t-.1f)/.3f):-100;
    for(int y=roundi(bTop);y<=roundi(bTip-len*gone);++y) {
      const float w=y>bTip-6?(bTip-y)/6:1,sh=std::max(0.0f,1-std::fabs(y-shine)/5);
      const int kq=q8(k*(.7f+.6f*sh));
      reaperAdd(roundi(bx),y,sh>.3f?whiteInk:silverInk,kq);
      if(w>.4f){reaperAdd(roundi(bx)-1,y,silverInk,(kq*200)>>8);reaperAdd(roundi(bx)+1,y,silverInk,(kq*150)>>8);}
      if(w>.8f){reaperAdd(roundi(bx)-2,y,silverInk,(kq*70)>>8);reaperAdd(roundi(bx)+2,y,silverInk,(kq*50)>>8);}
    }
    for(int x=-5;x<=5;++x){reaperAdd(roundi(bx)+x,roundi(bTop)-1,silverInk,q8(k*(1-gone)*.9f));reaperAdd(roundi(bx)+x,roundi(bTop)-2,silverInk,q8(k*(1-gone)*.5f));}
  }
  // As the last petals leave the blade, glints burst off the river.
  if(t>=CLOSE && t<CLOSE+.5f)for(unsigned i=0;i<13;++i) {
    const float age=t-CLOSE-.12f*rnd(i,608),life=.3f+.15f*rnd(i,609);if(age<0 || age>=life)continue;
    const float an=2*HALF_TURN*rnd(i,610),r=rx*(.7f+.4f*rnd(i,611)),u=age/life;
    twinkle(cx+r*sine(an),yc+ry*cosine(an)*(.8f+.3f*rnd(i,612)),i%3?pale:WHITE,sine(HALF_TURN*u)*1.2f,1+int(2*rnd(i,613)));
  }
  // The river's path: an orbit round the readout whose radii breathe, the
  // angle running faster until CLOSE and easing to a steady swirl after;
  // after LEAVE it spirals outward off the screen. Angle 0 is under the
  // readout's centre, increasing to the right and up.
  // Past CLOSE the angle eases by .9 (1-e^(-1.5v)); each petal is LAG*1.15
  // behind the last, so its exponential is the head's times a fixed ratio:
  // one library call per frame instead of one per petal.
  constexpr float STEP=LAG*1.15f,RATIO=1.0209157f; // e^(1.5*STEP)
  auto path=[&](float T,float e,float &x,float &y,float &dx,float &dy) {
    float th;
    if(T<CLOSE){const float u=(T-SCATTER)*(1/(CLOSE-SCATTER));th=THETA0+(T-SCATTER)*(1.5f+1.4f*u);}
    else th=THETA0+(CLOSE-SCATTER)*2.9f+2.0f*(T-CLOSE)+.9f*(1-e);
    const float out=T>LEAVE?(T-LEAVE)*(T-LEAVE):0;
    const float Rx=rx+8*sine(2.3f*th)+150*out,Ry=ry+7*sine(1.7f*th+1)+110*out,s=sine(th),c=cosine(th);
    x=cx+Rx*s;y=yc+Ry*c;dx=Rx*c;dy=-Ry*s;
  };
  const int slot=floori(t*20);
  const float flash=t>=CLOSE?1-smooth((t-CLOSE)*(1/.35f)):0,fade=1-smooth((t-END+.55f)*2),iry=1/ry;
  float e=std::exp(-1.5f*(t-CLOSE));
  for(int i=0;i<N;++i,e*=RATIO) {
    // Each place in the river follows its head along the same path, STEP
    // behind the one before; until its turn it is still part of the blade.
    const float T=t-i*STEP,spotY=bTip-len*(i+.5f)*(1.0f/N);
    if(T<SCATTER)continue;
    float px,py,dx,dy;path(T,e,px,py,dx,dy);
    const float n=reaperRsqrt(dx*dx+dy*dy+.01f),nx=-dy*n,ny=dx*n,join=smooth((T-SCATTER)*4);
    float k0=fade;
    if(T>LEAVE)k0*=1-smooth((T-LEAVE-.4f)*(1/.6f));
    if(k0<.03f)continue;
    for(int j=2*i;j<=2*i+1;++j) {
      // Two petals per place in the river, one either side of its middle;
      // once the river has poured out, a third of its second row thins away.
      if((j&1) && i%3==0 && t>CLOSE-.15f)continue;
      const float across=((j&1)?1:-1)*(1+3*rnd(j,602))*(1+.35f*sine(t*1.3f+i*.7f))+.8f*sine(t*2.1f+i*.37f);
      // Joining the river: blend from the blade over the petal's first 0.25 s.
      float x=px+nx*across,y=py+ny*across;
      x=bx+(x-bx)*join;y=spotY+(y-spotY)*join;
      // Scattering as it sweeps away.
      if(T>LEAVE){const float o=T-LEAVE;x+=(rnd(j,605)-.5f)*40*o*o;y+=(rnd(j,606)-.5f)*30*o*o+20*o*o;}
      // Nearer petals (the lower side of the orbit) are brighter.
      const float k=k0*(.8f+.25f*std::min(1.0f,std::max(-1.0f,(y-yc)*iry)));
      // A small teardrop tumbling as it flows: a pale rounded front, a pink
      // body, a deep pink tail and a lighter edge on one side.
      const float spin=.8f*sine(t*6+j*1.3f),cs=cosine(spin),sn=sine(spin);
      const float ux=(dx*cs-dy*sn)*n,uy=(dx*sn+dy*cs)*n;
      const bool glitter=rnd(uint32_t(j)*64+uint32_t(slot),607)<.05f;
      const int kq=q8(std::min(1.3f,k*1.15f+flash*.3f));
      const Ink &body=glitter || flash>.5f?whiteInk:pinkInk,&edge=flash>.2f?whiteInk:paleInk;
      const float vx=-uy,vy=ux; // across the petal
      reaperAdd(roundi(x+1.2f*ux),roundi(y+1.2f*uy),edge,(kq*160)>>8);
      reaperAdd(roundi(x),roundi(y),body,kq);reaperAdd(roundi(x+vx),roundi(y+vy),pinkInk,(kq*215)>>8);
      reaperAdd(roundi(x-ux),roundi(y-uy),pinkInk,(kq*220)>>8);reaperAdd(roundi(x-ux+vx),roundi(y-uy+vy),deepInk,(kq*170)>>8);
      reaperAdd(roundi(x-2*ux+.5f*vx),roundi(y-2*uy+.5f*vy),deepInk,(kq*130)>>8);
      reaperAdd(roundi(x-vx),roundi(y-vy),edge,(kq*140)>>8);
    }
  }
}

// ---- Mask: a bright line is drawn down across the space under the
// readout, and behind it a white mask assembles from pieces that slide
// into place: an abstract shell lit at its rim, with two slanted eye slits.
// Red stripes flare across it, the slits ignite gold and a pulse rings off
// it. Sustain: it breathes, the stripes shimmer, the eyes flicker and dark
// red embers drift off its sides. Then cracks run white-hot along the seams
// between its pieces and it breaks apart, the fragments falling away.
inline void Renderer::reaperMask(float t) {
  constexpr float SWEEP=.1f,FORMED=.62f,MARK=.85f,CRACK=3.05f,BREAK=3.55f,END=5.0f;
  constexpr int BAND=10;
  const Box &b=number_;const float cx=b.cx();
  // A=25.5 keeps the shell inside two mask word columns on the centred
  // readout (x 40..93), so its rows scan two words instead of three.
  const float my=b.y1+34.0f,A=25.5f,B=27;
  const Rgb bone=rgb8(236,232,222),shade=rgb8(150,150,165),red=rgb8(255,30,40),crimson=rgb8(150,0,20),gold=rgb8(255,205,60);
  const Ink boneInk=inkOf(bone),shadeInk=inkOf(shade),redInk=inkOf(red),crimsonInk=inkOf(crimson),goldInk=inkOf(gold);
  const int frame=floori(t*20+.001f);
  // The glint follows the awakening's flash and pulse rather than landing
  // on them, so the busiest frames do not coincide.
  glint(t,MARK+.42f,.45f);
  const int top=roundi(my-B),bottom=roundi(my+B);
  // The hand's sweep: a bright line drawn down across the mask's place.
  const float sweep=top-4+(bottom-top+8)*smooth((t-SWEEP)/(FORMED-SWEEP-.08f));
  if(t>SWEEP-.05f && t<FORMED+.05f) {
    const float k=smooth((t-SWEEP+.05f)/.08f)*(1-smooth((t-FORMED+.05f)/.1f));
    for(int x=roundi(cx-A-12);x<=roundi(cx+A+12);++x) {
      const float e=1-std::fabs(x-cx)*(1/(A+13));
      reaperVdot(x,sweep,WHITE_INK,q8(k*e*1.2f));reaperVdot(x,sweep-2,shadeInk,q8(k*e*.5f));
    }
  }
  // Each piece is a band of rows on one side of the mask, its seams zigzag;
  // a piece slides in from the side after the sweep passes it and flies
  // off when the mask breaks.
  // Piece lookups without a division per pixel: each column's seam step
  // (-1, 0 or 1 rows) and each row's band for the three steps are tables
  // built once per frame.
  const int cxr=roundi(cx),rowsFrom=top-4;
  int8_t zhOf[MAX_W];for(int x=0;x<w_;++x)zhOf[x]=int8_t((((x*7)%6+6)%6-3)/2);
  constexpr int ROWS=2*32+12;uint8_t bandOf[ROWS][3];
  for(int r=0;r<ROWS;++r)for(int z=0;z<3;++z)bandOf[r][z]=uint8_t(std::max(0,r+rowsFrom-top+z-1+BAND)/BAND);
  // Per row and seam step: whether the band changes from the row above
  // (bit 0: the seam itself) or toward the rows next to it (bit 1: a lip).
  uint8_t seamOf[ROWS][3];
  for(int r=0;r<ROWS;++r)for(int z=0;z<3;++z){const int bnd=bandOf[r][z];
    seamOf[r][z]=uint8_t((r>0 && bandOf[r-1][z]!=bnd)|((r+1<ROWS && bandOf[r+1][z]!=bnd) || (r>1 && bandOf[r-2][z]!=bnd))<<1);}
  auto pieceOf=[&](int x,int y){
    const int r=std::max(0,std::min(ROWS-1,y-rowsFrom)),z=unsigned(x)<unsigned(w_)?zhOf[x]+1:1;
    return bandOf[r][z]*2+(x>=cxr);
  };
  constexpr int PIECES=2*(64/BAND+3);
  // Per piece: its float offset, the same rounded (x+ox rounds to x plus
  // ox rounded, for whole x), and the last row the sweep has revealed.
  float pox[PIECES],poy[PIECES];int pxi[PIECES],pyi[PIECES],pyLim[PIECES];
  for(int p=0;p<PIECES;++p) {
    float ox=0,oy=0;const int row=p>>1;const float side=p&1?1:-1;
    const float in=t-(SWEEP+(FORMED-SWEEP-.08f)*(row+.5f)*(1/(2.0f*B/BAND+1)))-.03f*rnd(p,701);
    if(in<.25f){const float u=smooth(in*4);ox=side*(1-u)*9;oy=-(1-u)*3;}
    const float out=t-BREAK-.05f*rnd(p,702);
    if(out>0){const float vx=side*(18+30*rnd(p,703)),vy=-25-25*rnd(p,704)+row*4;ox+=vx*out;oy+=vy*out+95*out*out;}
    pox[p]=ox;poy[p]=oy;pxi[p]=roundi(ox);pyi[p]=roundi(oy);pyLim[p]=floori(sweep+1-oy);
  }
  auto clampPiece=[&](int p){return std::max(0,std::min(PIECES-1,p));};
  auto offset=[&](int p,float &ox,float &oy){p=clampPiece(p);ox=pox[p];oy=poy[p];};
  const float formed=smooth((t-SWEEP)*10),gone=1-smooth((t-BREAK-.6f)*(1/.7f));
  const float breathe=t>MARK?.9f+.1f*sine((t-MARK)*3.4f):1;
  const float flash=t>=MARK?1-smooth((t-MARK)*(1/.3f)):0;
  // The shell: per row, the band near the rim lit as a smooth gradient
  // that peaks just inside the edge and fades inward (fixed point: r^2 in
  // Q16 from a per-row reciprocal). Only the two spans of each row that
  // can reach the band are visited.
  const float crack=t>=CRACK && t<BREAK+.05f?smooth((t-CRACK)*(1/(BREAK-CRACK-.1f))):0;
  const Ink shade2=inkOf(mixed(shade,bone,.45f));
  if(formed>0 && gone>0) {
    const int kBase=q8(formed*gone*breathe*(1+.5f*flash)),kLip=(q8(crack)*230)>>8;
    for(int y=top-1;y<=bottom+1;++y) {
      const float v=(y-my)*(1/B);if(v*v>=1.09f)continue;
      // Square roots as x/sqrt(x) from the inline reciprocal root: three per row.
      auto root=[](float x){return x>0?x*reaperRsqrt(x):0.0f;};
      const float vp=std::max(0.0f,v),aw=A*(1-.32f*vp*vp),half=aw*root(1.09f-v*v);
      const int v2=int(v*v*65536),inv=int(65536/(aw*aw));
      const float invf=1.0f/inv;
      const int dxOut=int(root(std::max(0,71300-v2)*invf))+1,dxIn=std::max(0,int(root(std::max(0,46200-v2)*invf))-1);
      const int xLo=roundi(cx-half),xHi=roundi(cx+half),crackW=crack>0?floori(crack*half):-1;
      for(int span=0;span<2;++span) {
        const int x0=std::max(xLo,span?std::max(cxr-dxIn+1,cxr+dxIn):cxr-dxOut),x1=std::min(xHi,span?cxr+dxOut:cxr-dxIn);
        for(int x=x0;x<=x1;++x) {
          const int dx=x-cxr,r2=dx*dx*inv+v2;
          if(r2<46200 || r2>71300)continue; // between r .84 and 1.04
          // Weight: rising from r .84 to a peak at .95, a short fall after 1.
          const int w=r2<59000?((r2-46200)*1311)>>16:r2<65536?256:256-(((r2-65536)*2893)>>16);
          const int p=clampPiece(pieceOf(x,y));
          if(y>pyLim[p])continue;
          int kq=(kBase*w)>>8;
          // Cracks: the seams between pieces open dark, spreading out from
          // the middle, their lips lit, before the pieces fly apart.
          bool lip=false;
          if(std::abs(dx)<=crackW) {
            const int f=seamOf[std::max(0,std::min(ROWS-1,y-rowsFrom))][zhOf[x]+1];
            if((f&1) || dx==0 || dx==-1)continue;
            lip=(f&2) || dx==1 || dx==-2;
            if(lip)kq=std::max(kq,kLip);
          }
          // The faint parts of the gradient (its inner rise below about two
          // thirds of the peak and its outer fall below a half) are drawn on
          // alternate pixels at twice the weight: the same light on average
          // at about two fifths fewer pixels for the shell.
          if(!lip && (r2<54500 || r2>68400)){if((x+y)&1)continue;kq=std::min(256,kq*2);}
          reaperAdd(x+pxi[p],y+pyi[p],lip || r2>62000?boneInk:shade2,kq);
        }
      }
    }
  }
  // Eye slits: slanted almonds, a faint rim while forming, then lit gold
  // from MARK, flickering; they go out as the mask breaks.
  const float eyes=smooth((t-MARK)/.08f)*(1-smooth((t-CRACK-.3f)/.25f));
  for(int side=-1;side<=1;side+=2) {
    const float ex=cx+side*A*.4f,ey=my-B*.18f;
    float ox,oy;offset(pieceOf(roundi(ex),roundi(ey)),ox,oy);
    if(ey+oy>sweep)continue;
    const float flick=.85f+.15f*noise(frame*80,side*500,705)*(1/255.0f);
    const Ink eyeCore=inkOf(mixed(gold,WHITE,.35f+flash*.5f));
    for(int i=-8;i<=8;++i) {
      const float u=i*(1/8.0f),h=2.4f*(1-u*u),slant=-side*i*.3f;
      const int x=roundi(ex+i+ox);
      if(eyes>0)for(int d=-int(h+.5f);d<=int(h+.5f);++d)reaperAdd(x,roundi(ey+slant+d+oy),d==0?eyeCore:goldInk,q8(eyes*flick*(1.25f-.3f*std::abs(d))));
      else if(formed>0){reaperAdd(x,roundi(ey+slant-h+oy),shadeInk,q8(formed*.6f));reaperAdd(x,roundi(ey+slant+h+oy),shadeInk,q8(formed*.6f));}
    }
  }
  // Red stripes: they flare across from MARK, hot white first, then red,
  // a highlight running along them; they fade as the cracks spread.
  const float stripes=t<MARK?0:(1-smooth((t-CRACK)/.4f));
  if(stripes>0) {
    // Two per side, curving down the cheeks from beside the eyes, and one
    // down the brow, in mask units (x by A, y by B).
    static constexpr float S[5][6]={{.18f,-.05f,.42f,.35f,.5f,.72f},{.5f,-.32f,.72f,.05f,.7f,.42f},
                                    {-.18f,-.05f,-.42f,.35f,-.5f,.72f},{-.5f,-.32f,-.72f,.05f,-.7f,.42f},{0,-.95f,0,-.7f,0,-.42f}};
    const float drawn=easeOut((t-MARK)*(1/.12f)),hot=1-smooth((t-MARK)*4),shimmer=t*1.3f,run=(shimmer-floori(shimmer))*1.4f-.2f;
    const Ink base0=inkOf(mixed(red,WHITE,hot)),light=inkOf(rgb8(255,170,160));
    for(int j=0;j<5;++j) {
      for(int i=0;i<=22;++i) {
        const float u=i*(1/22.0f);if(u>drawn)break;
        const float a0=(1-u)*(1-u),a1=2*u*(1-u),a2=u*u;
        const float px=cx+A*(S[j][0]*a0+S[j][2]*a1+S[j][4]*a2),py=my+B*(S[j][1]*a0+S[j][3]*a1+S[j][5]*a2);
        float ox,oy;offset(pieceOf(roundi(px),roundi(py)),ox,oy);
        const float hl=std::max(0.0f,1-std::fabs(u-run)*5);
        const Ink c=mixedInk(base0,light,q8(hl*.6f));
        const int kq=q8(stripes*(1+.3f*flash)*(j==4?.9f:1));
        reaperAdd(roundi(px+ox),roundi(py+oy),c,kq);
        // The crimson edges on alternate samples (the samples are under a
        // pixel apart, so the edges stay unbroken).
        if(i&1){reaperAdd(roundi(px+ox)-1,roundi(py+oy),crimsonInk,(kq*170)>>8);reaperAdd(roundi(px+ox)+1,roundi(py+oy),crimsonInk,(kq*170)>>8);}
      }
    }
  }
  // The pulse: a dark red ring rolls off the mask as it awakens.
  if(t>=MARK && t<MARK+.45f) {
    const float u=(t-MARK)/.45f,s=1.08f+.4f*easeOut(u),k=(1-u)*(1-u)*1.1f;
    for(int i=0;i<64;++i) {
      const float an=i*(2*HALF_TURN/64),v=sine(an),aw=A*(1-.32f*std::max(0.0f,v)*std::max(0.0f,v));
      reaperAdd(roundi(cx+aw*s*cosine(an)),roundi(my+B*s*v),i&1?redInk:crimsonInk,q8(k));
    }
  }
  // Embers of dark red energy drift off the mask's sides through the sustain.
  for(unsigned i=0;i<18;++i) {
    const float born=MARK+.1f+2.1f*rnd(i,706),age=t-born,life=.7f+.5f*rnd(i,707);
    if(age<0 || age>=life || born>CRACK)continue;
    const float side=i&1?1:-1,v=-.3f+.9f*rnd(i,708),u=age/life;
    const float aw=A*(1-.32f*std::max(0.0f,v)*std::max(0.0f,v))*std::sqrt(1-v*v);
    const float x=cx+side*(aw+2+(8+10*rnd(i,709))*age),y=my+B*v-(6+10*rnd(i,710))*age+1.5f*sine(age*8+i);
    dot(x,y,mixed(red,crimson,u),sine(HALF_TURN*u)*1.2f);
  }
  // Motes condense onto the pieces as they form, and dust falls off the
  // fragments as they break.
  for(unsigned i=0;i<24;++i) {
    const bool late=i>=12;const float born=late?BREAK+.4f*rnd(i,709):SWEEP+.4f*rnd(i,709),age=t-born,life=.5f+.3f*rnd(i,710);
    if(age<0 || age>=life)continue;
    const float an=2*HALF_TURN*rnd(i,711),u=age/life;
    const float r=late?1+.6f*u:1.6f-.6f*easeOut(u);
    dot(cx+A*r*cosine(an),my+B*r*sine(an)+(late?30*u*u:0),late?bone:mixed(shade,bone,u),sine(HALF_TURN*u)*1.1f);
  }
}

// ---- Blink: something too fast to see steps round the readout. Each
// arrival is a tall soft afterimage that hangs where it stopped, joined to
// the last one by a sharp whoosh line, and each afterimage then smears
// sideways and dissolves away from where it went. Six steps, faster and
// faster, then one sharp cut of light right across the band under the
// readout, and every afterimage left behind flashes out at once. Sustain:
// fainter afterimages keep appearing at new places, slower, each
// dissolving in turn, with dust kicked up where they land. Settle: the
// last one dissolves.
inline void Renderer::reaperBlink(float t) {
  constexpr float CUT=1.25f,END=4.9f;constexpr int STEPS=6,LATE=6;
  const Box &b=number_;const float cx=b.cx();
  const Rgb pale=mixed(rgb8(205,220,240),accent_,.2f),edge=mixed(accent_,rgb8(160,190,255),.4f);
  const Ink paleInk=inkOf(pale),edgeInk=inkOf(edge);
  // Where it stops: round the band under the readout, then the landing
  // after the cut; later, fainter stops for the sustain.
  const float X[STEPS+1]={cx-40,b.x1-10.0f,cx-24,cx+30,b.x0+18.0f,cx+4,b.x1-8.0f};
  const float Y[STEPS+1]={b.y1+22.0f,b.y1+28.0f,b.y1+64.0f,b.y1+58.0f,b.y1+44.0f,b.y1+24.0f,b.y1+34.0f};
  const float AT[STEPS+1]={.25f,.5f,.7f,.85f,.97f,1.07f,CUT};
  const float LX[LATE]={cx-30,cx+26,cx-42,cx+2,b.x1-12.0f,cx-14};
  const float LY[LATE]={b.y1+60.0f,b.y1+24.0f,b.y1+40.0f,b.y1+70.0f,b.y1+52.0f,b.y1+26.0f};
  const float LAT[LATE]={1.75f,2.15f,2.55f,2.9f,3.25f,3.6f};
  constexpr float H=14;
  glint(t,CUT+.02f,.4f);
  // An afterimage: a tall soft column of light, brightest in its middle,
  // that smears row by row toward `dir` and thins out as it dissolves (u).
  // Its half width per row and that width's reciprocal are the same for
  // every afterimage, so they are worked out once per frame.
  constexpr int HI=int(H);float wRow[2*HI+1],wInv[2*HI+1];
  for(int dy=-HI;dy<=HI;++dy){const float v=dy*(1/H),w=1+3.6f*std::sqrt(std::max(0.0f,1-v*v));wRow[dy+HI]=w;wInv[dy+HI]=1/(w+.8f);}
  auto ghost=[&](float gx,float gy,float k,float u,float dir,uint32_t seed) {
    if(k<.02f || u>=1)return;
    for(int dy=-HI;dy<=HI;++dy) {
      const float w=wRow[dy+HI],iw=wInv[dy+HI];
      const float r=rnd(seed*64+uint32_t(dy+32),801);if(r<u*.95f)continue;
      const float shift=dir*u*(5+16*r),fade=k*(1-u)*(1-u)*(.75f+.25f*r);
      const int y=roundi(gy+dy),xs=roundi(gx+shift); // x+gx+shift rounds to x plus this
      for(int x=-int(w);x<=int(w);++x) {
        const float f=1-std::abs(x)*iw;
        reaperAdd(xs+x,y,f>.6f?WHITE_INK:paleInk,q8(fade*f*1.2f));
      }
      // The smear: a streak trailing from each dissolving row.
      if(u>.05f && (dy&1)) {
        const float len=10*u*r,is=1/(len+2);
        for(int d=1;d<=int(len)+1;++d)reaperAdd(roundi(gx+shift-dir*(w+d)),y,edgeInk,q8(fade*.6f*(1-d*is)));
      }
    }
  };
  // A whoosh: a sharp line from one stop to the next, a white core with an
  // accent glow either side, fading fast.
  auto whoosh=[&](float x0,float y0,float x1,float y1,float k) {
    if(k<.02f)return;
    const float dx=x1-x0,dy=y1-y0,inv=1/std::sqrt(dx*dx+dy*dy+.01f),nx=-dy*inv,ny=dx*inv;
    sorcLine(x0,y0,x1,y1,WHITE_INK,q8(k*.4f),q8(k));
    sorcLine(x0+nx,y0+ny,x1+nx,y1+ny,edgeInk,q8(k*.2f),q8(k*.7f));sorcLine(x0-nx,y0-ny,x1-nx,y1-ny,edgeInk,q8(k*.2f),q8(k*.7f));
  };
  // The steps.
  for(int i=0;i<=STEPS;++i) {
    const float age=t-AT[i];if(age<-.12f)continue;
    // Just before the first step, a glint where it will appear.
    if(i==0 && age<0){twinkle(X[0],Y[0]-H,pale,(1+age/.12f)*.9f,2);continue;}
    if(age<0)continue;
    const float next=i<STEPS?AT[i+1]:CUT+.08f;
    const float dir=i<STEPS?(X[i+1]>X[i]?1.0f:-1.0f):1.0f;
    // Each afterimage holds until the next step, then dissolves; all of
    // them flash out at the cut.
    const float u=t<next?0:smooth((t-next)/.45f);
    const float out=t>=CUT?1-smooth((t-CUT)/.12f):1;
    const float k=(age<.05f?1.3f:1.0f)*out;
    ghost(X[i],Y[i],k,i==STEPS?smooth((t-CUT-.35f)/.6f):u,dir,uint32_t(i));
    if(i>0 && age<.2f)whoosh(X[i-1],Y[i-1],X[i],Y[i],1-age/.2f);
    // Dust kicked up at its foot.
    if(age<.4f)for(int j=0;j<5;++j){const float a=age/.4f,sx=(j-2)*(2+9*a);dot(X[i]+sx,Y[i]+H+1-6*a*(1-a)*(2-std::abs(j-2)),pale,(1-a)*.8f);}
  }
  // The cut: one sharp line of light across the band under the readout,
  // three pixels deep, white then cooling to the accent, gone by 2 s.
  {
    const float u=t-CUT;
    if(u>=0 && u<.75f) {
      const float x0=0,y0=b.y1+66.0f,x1=float(w_-1),y1=b.y1+14.0f,head=easeOut(u/.06f);
      const float hx=x0+(x1-x0)*head,hy=y0+(y1-y0)*head,k=u<.08f?1.2f:1-smooth((u-.08f)/.65f);
      const Ink core=inkOf(mixed(WHITE,accent_,smooth((u-.1f)/.4f)));
      const float dx=x1-x0,dy=y1-y0,inv=1/std::sqrt(dx*dx+dy*dy),nx=-dy*inv,ny=dx*inv;
      sorcLine(x0,y0,hx,hy,core,q8(k),q8(k));
      sorcLine(x0+nx,y0+ny,hx+nx,hy+ny,edgeInk,q8(k*.7f),q8(k*.7f));sorcLine(x0-nx,y0-ny,hx-nx,hy-ny,edgeInk,q8(k*.7f),q8(k*.7f));
      if(u<.3f){const float v=u/.3f;twinkle(hx-4,hy+1,WHITE,(1-v)*1.6f,4);}
    }
  }
  // The sustain: fainter afterimages at new places, one after another.
  for(int i=0;i<LATE;++i) {
    const float age=t-LAT[i];if(age<0)continue;
    const float hold=.25f,u=smooth((age-hold)/.6f);if(u>=1)continue;
    const float px=i?LX[i-1]:X[STEPS],py=i?LY[i-1]:Y[STEPS];
    const float k=.75f*(1-.08f*i)*(1-smooth((t-END+.5f)/.4f));
    ghost(LX[i],LY[i],k,u,LX[i]>px?1.0f:-1.0f,uint32_t(20+i));
    if(age<.15f)whoosh(px,py,LX[i],LY[i],.7f*(1-age/.15f));
    if(age<.35f)for(int j=0;j<4;++j){const float a=age/.35f,sx=(j-1.5f)*(2+8*a);dot(LX[i]+sx,LY[i]+H+1-4*a*(1-a),pale,(1-a)*.6f);}
  }
  // Motes hang in the air where it passed.
  for(unsigned i=0;i<16;++i) {
    const float born=.3f+3.3f*rnd(i,802),age=t-born,life=.8f+.6f*rnd(i,803);if(age<0 || age>=life)continue;
    const float x=6+(w_-12)*rnd(i,804)+3*sine(age*2+i),y=b.y1+10+70*rnd(i,805)-5*age;
    dot(x,y,pale,sine(HALF_TURN*age/life)*.6f*(1-smooth((t-END+.5f)/.4f)));
  }
}
}
