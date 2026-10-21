#pragma once
// Jujutsu Kaisen-inspired celebrations (Rupture..Ravage): Renderer members
// declared in best_effect_ui.h, which includes this file after the class.
// Same rules as the others: additive light on the background outside the UI
// halo, a pure function of elapsed time with fixed seeds, and the readout
// exactly as drawn well before the end. Darkness on a black background is
// drawn with lit edges (Rupture's black bolts, Umbra's pool) or taken from
// the readout itself (Rupture's inverted frame, Umbra's sinking digits).
// Each runs about five seconds: an early impact, a sustained middle that
// keeps moving off the readout's glyphs at less cost than the impact, and a
// settle. Touching the readout: Rupture ripples its rows sideways, flashes
// it deep crimson then white and glints it, and gives it back by 1.75 s;
// Umbra darkens its lower rows below a rising shadow line (a lit waterline
// on the glyphs) and gives it back by 2.1 s; Boundless, Precision and
// Ravage only brighten glyph pixels (every channel up) and stop by 2.05,
// 1.7 and 1.85 s. Nothing takes a glyph pixel below 3:1 on the background:
// the dims stop at that floor (shadeAnchor, sorcShade) and the brightening
// turns toward black on a light custom background, as the glint does.
// Cost: strokes are fixed-point and clipped to the screen before the light
// probe, sparks are dots, and each effect keeps the rows it reaches near the
// readout, so its mask scans stay a few hundred words per frame at most.
namespace BestEffectUI {

// A fixed-point line with its weight ramping k0..k1 (Q8); points off the
// screen are skipped before the probe.
inline void Renderer::sorcLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1) {
  const float dx=x1-x0,dy=y1-y0;const int n=std::max(1,int(std::max(std::fabs(dx),std::fabs(dy))));
  int x8=q8(x0)+128,y8=q8(y0)+128,wq=k0<<8;const int sx=q8(dx/n),sy=q8(dy/n),dw=(k1-k0)*256/n;
  for(int i=0;i<=n;++i,x8+=sx,y8+=sy,wq+=dw) {
    const int x=x8>>8,y=y8>>8;
    if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_))addw(x,y,c,wq>>8);
  }
}
// Black lightning: an angular zigzag (midpoint displacement whose finest
// level alternates sides) drawn only as two lit rims, so its core stays as
// dark as the background between them; one branch forks off its middle.
inline void Renderer::sorcRimBolt(float x0,float y0,float x1,float y1,uint32_t seed,const Ink &rim,const Ink &outer,int k,int depth) {
  float xs[9],ys[9];const int n=1<<std::min(depth,3);xs[0]=x0;ys[0]=y0;xs[n]=x1;ys[n]=y1;
  for(int step=n;step>1;step/=2)for(int i=0;i+step<=n;i+=step) {
    const int m=i+step/2;const float dx=xs[i+step]-xs[i],dy=ys[i+step]-ys[i];
    const float side=step==2?((i/2+seed)&1?1.0f:-1.0f):(rnd(seed*32+m,302)<.5f?-1.0f:1.0f);
    const float off=side*(.18f+.24f*rnd(seed*32+m,301));
    xs[m]=(xs[i]+xs[i+step])*.5f-dy*off;ys[m]=(ys[i]+ys[i+step])*.5f+dx*off;
  }
  for(int i=0;i<n;++i) {
    const float dx=xs[i+1]-xs[i],dy=ys[i+1]-ys[i],inv=1/std::sqrt(dx*dx+dy*dy+.01f);
    // Rims 2 px either side of the core near the root, 1.5 px toward the tip.
    const float gap=i<n/2?2.0f:1.5f,nx=-dy*inv*gap,ny=dx*inv*gap;
    sorcLine(xs[i]+nx,ys[i]+ny,xs[i+1]+nx,ys[i+1]+ny,rim,k,k);
    sorcLine(xs[i]-nx,ys[i]-ny,xs[i+1]-nx,ys[i+1]-ny,rim,k,k);
  }
  // The fork: a short two-segment zigzag off the middle joint, in the outer colour.
  if(depth>=2) {
    const int m=n/2;const float dx=x1-x0,dy=y1-y0,turn=rnd(seed,303)<.5f?-.55f:.55f;
    const float c=cosine(turn),s=sine(turn),bx=(dx*c-dy*s)*.38f,by=(dx*s+dy*c)*.38f;
    const float mx=xs[m]+bx*.5f-by*.2f,my=ys[m]+by*.5f+bx*.2f;
    sorcLine(xs[m],ys[m],mx,my,outer,k,k);sorcLine(mx,my,xs[m]+bx,ys[m]+by,outer,k,k>>1);
  }
}
// Moves one readout row sideways by d px in place; vacated pixels take the background.
inline void Renderer::sorcShiftRow(int y,int d) {
  const Box &b=number_;d=std::max(-b.x0,std::min(w_-1-b.x1,d));
  if(!d || y<b.y0 || y>b.y1)return;
  uint16_t *row=px_+y*w_;const int n=b.x1-b.x0+1;
  std::memmove(row+b.x0+d,row+b.x0,size_t(n)*2);
  if(d>0)std::fill(row+b.x0,row+b.x0+d,bg_);else std::fill(row+b.x1+d+1,row+b.x1+1,bg_);
  plots_+=unsigned(n);
}
// Builds the mask words of rows y0..y1 before a horizontal run of light is
// drawn on them: a word's scan reads two pixels of each neighbour, so a
// word built after light was added next to it would take that light for UI
// ink and leave a two-pixel gap at the boundary.
inline void Renderer::sorcPrime(int y0,int y1) {
  for(int y=std::max(0,y0);y<=std::min(bottom_,y1);++y)for(int x=0;x<w_;x+=32)blocked(x,y);
}
// Pulls one glyph pixel away from the background by k8 (Q8), as the glint
// does: toward `target` with each channel only upward when the ink is
// lighter than the background (every built-in theme), toward black on a
// light custom background, so the heat never washes the readout out.
inline void Renderer::sorcHeat(int x,int y,const Ink &target,int k8) {
  if(k8<=0 || unsigned(x)>=unsigned(w_) || unsigned(y)>=unsigned(h_))return;
  uint16_t &p=px_[y*w_+x];if(p==bg_)return;
  const unsigned v=load(p),bv=load(bg_);int r=v>>11,g=(v>>5)&63,bl=v&31;
  if(2*r+g+2*bl>=2*int(bv>>11)+int((bv>>5)&63)+2*int(bv&31)) {
    r+=((std::max(r,target.r>>8)-r)*k8)>>8;g+=((std::max(g,target.g>>8)-g)*k8)>>8;bl+=((std::max(bl,target.b>>8)-bl)*k8)>>8;
  } else {r-=(r*k8)>>8;g-=(g*k8)>>8;bl-=(bl*k8)>>8;}
  const uint16_t out=store(uint16_t(r<<11|g<<5|bl));
  p=out==bg_?uint16_t(out^store(1)):out;++plots_;
}
namespace detail {
// Palette::luminance's weighted channel terms with each linear value split
// at 10000 (hi*10000+lo): the weighted his are whole luminance units and the
// weighted los of three channels stay under 10^8, so the luminance comes out
// exactly in 32-bit arithmetic with one division by a constant.
struct SorcLuma {uint32_t hi,lo;};
template<size_t N> constexpr std::array<SorcLuma,N> sorcLumaTable(const uint32_t (&linear)[N],uint32_t weight) {
  std::array<SorcLuma,N> t{};
  for(size_t i=0;i<N;++i)t[i]={weight*(linear[i]/10000),weight*(linear[i]%10000)};
  return t;
}
constexpr auto SORC_LUMA_R=sorcLumaTable(Palette::LINEAR5,2126),SORC_LUMA_B=sorcLumaTable(Palette::LINEAR5,722);
constexpr auto SORC_LUMA_G=sorcLumaTable(Palette::LINEAR6,7152);
// Palette::luminance(c)+50000, the term contrast100 compares.
inline uint32_t sorcLuma(uint16_t c) {
  const SorcLuma &r=SORC_LUMA_R[c>>11],&g=SORC_LUMA_G[(c>>5)&63],&b=SORC_LUMA_B[c&31];
  return r.hi+g.hi+b.hi+(r.lo+g.lo+b.lo+5000)/10000+50000;
}
// One glyph colour v (RGB565) pulled toward a target colour (RGB565
// channels) by k (Q8), each channel either way, and the floor it must keep
// against the background: 3:1, or its own contrast if it starts lower.
// The verdicts are Palette::contrast100's exactly, without its 64-bit
// divisions (a library call each on the device, three per test):
// floor(la*100/lb)>=f is la*100>=f*lb, and every product fits 32 bits
// (luminance+50000 is at most 1050000 and f at most 300).
struct SorcShader {
  int r=0,g=0,b=0,tr=0,tg=0,tb=0;uint32_t lb=1,floor=0;
  SorcShader()=default;
  SorcShader(unsigned v,uint16_t background,int targetR,int targetG,int targetB,uint32_t minContrast100)
    :r(int(v>>11)),g(int((v>>5)&63)),b(int(v&31)),tr(targetR),tg(targetG),tb(targetB),lb(sorcLuma(background)) {
    const uint32_t lv=sorcLuma(uint16_t(v));floor=std::min(lv>=lb?lv*100/lb:lb*100/lv,minContrast100);
  }
  uint16_t at(int k)const{return uint16_t((r+(((tr-r)*k)>>8))<<11|(g+(((tg-g)*k)>>8))<<5|(b+(((tb-b)*k)>>8)));}
  bool readable(uint16_t c)const{const uint32_t l=sorcLuma(c);return l>=lb?l*100>=floor*lb:lb*100>=floor*l;}
  // The pull by k8, or where it stops at the floor (at(0), the colour
  // itself, always keeps it): searched between the two.
  uint16_t pull(int k8)const {
    uint16_t out=at(k8);
    if(!readable(out)) {
      int lo=0,hi=k8;
      while(hi-lo>1){const int mid=(lo+hi)/2;(readable(at(mid))?lo:hi)=mid;}
      out=at(lo);
    }
    return out;
  }
  // The largest pull up to kMax that keeps the floor when the contrast
  // falls steadily with k (a pull toward the background); callers still
  // check each result, since that need not hold on every custom theme.
  int limit(int kMax)const {
    if(readable(at(kMax)))return kMax;
    int lo=0,hi=kMax;
    while(hi-lo>1){const int mid=(lo+hi)/2;(readable(at(mid))?lo:hi)=mid;}
    return lo;
  }
};
}
// Glyph value v (RGB565) pulled toward `target` by k8, each channel either
// way, but never below 3:1 against the background (below its own contrast
// if it started lower), as shadeAnchor does: the pull stops at that point.
inline uint16_t Renderer::sorcShade(unsigned v,const Ink &target,int k8)const {
  return detail::SorcShader(v,load(bg_),target.r>>8,target.g>>8,target.b>>8,READOUT_CONTRAST100).pull(k8);
}
// Heats the glyph pixels a line crosses, inside the readout's box only.
inline void Renderer::sorcHeatLine(float x0,float y0,float x1,float y1,const Ink &target,int k8) {
  if(k8<=0)return;
  const Box &b=number_;const float dx=x1-x0,dy=y1-y0;const int n=std::max(1,int(std::max(std::fabs(dx),std::fabs(dy))));
  int x8=q8(x0)+128,y8=q8(y0)+128;const int sx=q8(dx/n),sy=q8(dy/n);
  for(int i=0;i<=n;++i,x8+=sx,y8+=sy) {
    const int x=x8>>8,y=y8>>8;
    if(x>=b.x0 && x<=b.x1 && y>=b.y0 && y<=b.y1)sorcHeat(x,y,target,k8);
  }
}

inline void Renderer::sorcery(unsigned which,float t) {
  switch(which) {
    case 0:sorcRupture(t);break;
    case 1:sorcBoundless(t);break;
    case 2:sorcPrecision(t);break;
    case 3:sorcUmbra(t);break;
    case 4:sorcRavage(t);break;
    default:break;
  }
}

// ---- Rupture, after Black Flash): cursed energy (the accent)
// crackles round the readout as space ripples through its rows, a ring
// snaps onto it, and the hit lands as an inverted frame (near-black digits
// with a red cast in a red blaze), then a white flash; black lightning with
// red rims bursts out of it, re-struck every frame, while the rows shudder.
// Sustain: the crackle dies down to a stray bolt now and then, red energy
// smoulders along the readout's foot and embers keep drifting off it; then
// the last embers cool.
inline void Renderer::sorcRupture(float t) {
  constexpr float HIT=.7f,CRACKLE=.8f,LINGER=3.0f,END=4.8f;
  const Box &b=number_;const float cx=b.cx(),cy=b.cy(),hw=(b.x1-b.x0)*.5f,hh=(b.y1-b.y0)*.5f;
  const Rgb red=rgb8(255,34,26),hot=rgb8(255,205,190),blood=rgb8(190,8,16);
  const Ink redInk=inkOf(red),bloodInk=inkOf(blood),hotInk=inkOf(hot);
  const int frame=floori(t*20+.001f);
  const float a=t-HIT;
  // Distortion: a ripple band runs down the readout as the energy builds;
  // after the flash two stronger bands run out from its middle row, and
  // a last soft one follows.
  if(t<HIT) {
    const float amp=1.8f*smooth(t/HIT),yc=b.y0-3+(b.y1-b.y0+6)*(t*(1/.3f)-floori(t*(1/.3f)));
    for(int y=std::max(b.y0,int(yc)-3);y<=std::min(b.y1,int(yc)+3);++y)sorcShiftRow(y,roundi(amp*sine((y-yc)*.9f)));
  } else if(a>=.1f && a<.6f) {
    const float v=a-.1f,u=v<.25f?v/.25f:(v-.25f)/.25f,amp=(v<.25f?2.6f:1.5f)*(1-u),reach=(hh+3)*u;
    for(int side=-1;side<=1;side+=2) {
      const float yc=cy+side*reach;
      for(int y=std::max(b.y0,int(yc)-1);y<=std::min(b.y1,int(yc)+1);++y)sorcShiftRow(y,roundi(amp*(y==int(yc)?1:.55f)*side));
    }
  } else if(a>=.75f && a<1.05f) {
    const float u=(a-.75f)/.3f,yc=b.y0-2+(b.y1-b.y0+4)*u;
    for(int y=std::max(b.y0,int(yc)-1);y<=std::min(b.y1,int(yc)+1);++y)sorcShiftRow(y,y==int(yc)?1:0);
  }
  // The hit: an inverted frame, then a white flash, then a glint sweeps the
  // header as its colour comes back. The inverted digits are a deep crimson
  // that keeps 3:1 on black by itself, so shadeAnchor's floor (which stops
  // any pull at 3:1) does not cut the pull short and leave them a dim
  // version of their own colour.
  if(a>=0 && a<.05f)shadeAnchor(inkOf(rgb8(205,8,22)),q8(.93f),false);
  else if(a>=.05f && a<.1f)shadeAnchor(hotInk,q8(.85f),true);
  glint(t,HIT+.12f,.4f);
  // Charge: an aura flickers along the readout's rim and arcs crackle off
  // it, blue (the accent) turning red as the hit nears.
  if(t<HIT) {
    const float s=t/HIT;const Ink c=inkOf(mixed(accent_,red,s*s));
    sorcPrime(b.y0-4,b.y0-3);sorcPrime(b.y1+3,b.y1+4);
    const int kq=q8(.25f+.55f*s);
    for(int x=std::max(0,b.x0-2);x<=std::min(w_-1,b.x1+2);++x) {
      const int n1=noise(x*40,frame*90,331),n2=noise(x*40+9000,frame*90,332);
      if(n1>150)addw(x,b.y0-3,c,(kq*(n1-120))>>7);
      if(n2>150)addw(x,b.y1+3,c,(kq*(n2-120))>>7);
    }
    const int n=1+int(9*s*s);
    for(int j=0;j<n;++j) {
      const uint32_t seed=uint32_t(frame)*16+j;if(rnd(seed,311)<.2f)continue;
      const bool top=rnd(seed,312)<.3f;const float x=b.x0+4+(b.x1-b.x0-8)*rnd(seed,313),y=top?b.y0-3.5f:b.y1+3.5f;
      const float len=6+10*rnd(seed,314)*(.5f+s),dir=rnd(seed,315)<.5f?-1:1,out=top?-1:1;
      const float mx=x+dir*len*.45f,my=y+out*(1.5f+(top?1.5f:4)*rnd(seed,316));
      sorcLine(x,y,mx,my,c,240,220);sorcLine(mx,my,x+dir*len,y+out*.5f,c,220,60);
    }
    // The split second before contact: a ring snaps in onto the readout.
    if(t>HIT-.16f) {
      const float u=(t-HIT+.16f)/.16f,sc=1+1.5f*(1-u)*(1-u),rx=(hw+6)*sc,ry=(hh+6)*sc;
      const Ink ring=inkOf(mixed(red,WHITE,u*.6f));const int k=q8(.35f+.65f*u);
      for(int i=0;i<160;++i) {
        const float an=i*(2*HALF_TURN/160);const int x=roundi(cx+rx*cosine(an)),y=roundi(cy+ry*sine(an));
        if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_))addw(x,y,ring,k);
      }
    }
  }
  // The blaze round the inverted digits: red bands hugging the readout.
  if(a>=0 && a<.1f) {
    const float k=a<.05f?1:.45f;
    sorcPrime(b.y0-4,b.y0-3);sorcPrime(b.y1+3,b.y1+4);
    for(int o=3;o<=4;++o) {
      const int kq=q8(k*(o==3?1.1f:.8f));const Ink c=o==3?inkOf(mixed(red,hot,.3f)):redInk;
      for(int x=std::max(0,b.x0-o);x<=std::min(w_-1,b.x1+o);++x){addw(x,b.y0-o,c,kq);addw(x,b.y1+o,c,kq);}
    }
  }
  // Black lightning bursts out of the readout, re-struck every frame: most
  // bolts fan downward into the open screen, a few short ones go up; then
  // only a stray, weaker bolt now and then.
  if(a>=.1f && a<LINGER) {
    const float u=(a-.1f)/CRACKLE,late=(a-.1f-CRACKLE)/(LINGER-.1f-CRACKLE);const int n=u<1?int(7-4*u):1;
    const float power=u<1?1-.4f*u:.6f-.25f*late;
    for(int j=0;j<n;++j) {
      const uint32_t seed=uint32_t(frame)*8+j;
      if(u>=1 && rnd(seed,327)>.62f-.3f*late)continue;
      const bool up=j==n-1 && n>2;
      const float an=up?-HALF_TURN*(.25f+.5f*rnd(seed,323)):HALF_TURN*(.12f+.76f*(j+rnd(seed,323))/std::max(1,n-1));
      const float ca=cosine(an),sa=sine(an),ex=ca/(hw+3),ey=sa/(hh+3),r0=1/std::sqrt(ex*ex+ey*ey);
      const float x=cx+r0*ca,y=cy+r0*sa;
      float len=(up?9+6*rnd(seed,324):24+34*rnd(seed,324))*power;
      if(up)len=std::min(len,(y-1)/std::max(.3f,-sa));
      if(len<5)continue;
      sorcRimBolt(x,y,x+len*ca,y+len*sa,seed,redInk,bloodInk,q8(std::min(1.0f,power*1.2f)),3);
      dot(x,y+(up?-1:1),hot,power);
    }
  }
  // Smoulder: red energy crawls along the readout's foot through the sustain.
  const float smoulder=smooth((a-.3f)/.4f)*(1-smooth((a-2.6f)/.8f));
  if(smoulder>0) {
    sorcPrime(b.y0-3,b.y0-3);sorcPrime(b.y1+3,b.y1+4);
    const int kq=q8(.8f*smoulder),drift=int(t*700);const Ink warm=inkOf(mixed(red,hot,.25f));
    for(int x=std::max(0,b.x0-2);x<=std::min(w_-1,b.x1+2);++x) {
      const int n=noise(x*45+drift,int(t*260),333),m=noise(x*45-drift+5000,int(t*230),334);
      if(n>120){const int k=(kq*(n-100))>>7;addw(x,b.y1+3,n>200?warm:redInk,k);addw(x,b.y1+4,bloodInk,k>>1);}
      if(m>150)addw(x,b.y0-3,redInk,(kq*(m-130))>>8);
    }
  }
  // Embers drift off the rim and cool, from the hit to well into the sustain.
  for(unsigned i=0;i<44;++i) {
    const float age=a-.1f-2.8f*rnd(i,331),life=.8f+.9f*rnd(i,332);if(age<0 || age>=life)continue;
    const bool top=i%4==0;const float x0=b.x0+(b.x1-b.x0)*rnd(i,333),y0=top?b.y0-3.0f:b.y1+3.0f;
    const float vx=(rnd(i,334)-.5f)*50,vy=top?-10-12*rnd(i,335):10+38*rnd(i,335);
    const float x=x0+vx*age+2.5f*sine(age*6+i),y=y0+vy*age-12*age*age,v=age/life;
    const float k=(1-v)*(1-smooth((t-END+.7f)/.5f))*(.75f+.25f*sine(age*25+i));
    const Rgb c=mixed(mixed(hot,red,std::min(1.0f,v*2.0f)),blood,smooth((v-.5f)/.5f));
    dot(x,y,c,k*1.3f);plot(x-vx*.03f,y-(vy-24*age)*.03f,c,k*.5f);
  }
}

// The lower arc of the tunnel ellipse s px out from the readout (rx
// hw+5+0.7s, ry hh+5+1.4s round its centre): the readout spans nearly the screen's
// width, so only the part below it is on the screen. Stepped per column
// with a two-pixel vertical split for smooth motion; steep runs near the
// screen's sides are joined by a line. `split` draws each column as a
// two-pixel vertical split (smooth slow motion, twice the probes); without
// it each column is one pixel, for fast rings.
inline void Renderer::sorcRing(float s,const Ink &c,int kq,bool split) {
  if(kq<5)return;
  const Box &b=number_;const float cx=b.cx(),cy=b.cy(),rx=(b.x1-b.x0)*.5f+5+.7f*s,ry=(b.y1-b.y0)*.5f+5+1.4f*s;
  const int x0=std::max(0,int(std::ceil(cx-rx))),x1=std::min(w_-1,int(cx+rx));
  if(x0>x1 || cy+ry*.2f>bottom_+1)return;
  const float inv=1/rx;float py=-1;
  for(int x=x0;x<=x1;++x) {
    const float u=(x-cx)*inv,y=cy+ry*std::sqrt(std::max(0.0f,1-u*u));
    if(y>bottom_+1){py=-1;continue;}
    if(py>=0 && std::fabs(y-py)>1.5f)sorcLine(float(x-1),py,float(x),y,c,kq,kq);
    else if(split)vdot(x,y,c,kq);else addw(x,roundi(y),c,kq);
    py=y;
  }
}

// ---- Boundless, after Unlimited Void): a crossed-finger sign
// snaps shut under the readout, and the boundary bursts out from it: a
// bright rim sweeps down past the screen's edge and opens an endless void
// behind it, a funnel of depth whose vanishing point is the readout. Rings
// of light rise from beyond the screen's foot and recede into the readout,
// slowing and brightening as they go, starlight streams in along the
// funnel, stars glitter round the header and bands of overload race through
// the digits. Sustain: the void keeps receding, slower and calmer, a ring
// now and then. Then the rim sweeps back up into the readout and the last
// stars twinkle out.
inline void Renderer::sorcBoundless(float t) {
  constexpr float OPEN=.5f,SPREAD=.45f,CALM=2.2f,CLOSE=4.2f,SHUT=.5f,S0=150,RIM=178;
  const Box &b=number_;const float cx=b.cx(),cy=b.cy(),hw=(b.x1-b.x0)*.5f,hh=(b.y1-b.y0)*.5f;
  const float sx=cx,sy=b.y1+12.0f;
  const Rgb ice=rgb8(205,235,255),blue=rgb8(80,165,255),violet=rgb8(180,125,255),pink=rgb8(255,140,220);
  const Rgb tints[]={ice,blue,violet,pink};
  // The sign: two strokes cross and close to a narrow X, then flash.
  if(t<OPEN+.12f) {
    const float th=.95f-.75f*easeOut(t/OPEN),k=smooth(t/.1f)*(t<OPEN?1:1-(t-OPEN)/.12f);
    for(int s=-1;s<=1;s+=2) {
      const float ux=sine(th)*s,uy=-cosine(th);
      segment(sx-ux*7,sy-uy*7,sx+ux*14,sy+uy*14,mixed(blue,accent_,.3f),k*1.2f);
    }
    if(t>OPEN-.06f)spark(sx,sy-3,ice,1.4f*(1-std::fabs(t-OPEN)/.12f),2.6f);
  }
  if(t<OPEN)return;
  glint(t,OPEN+.02f,.45f);
  // The boundary: how far out the void reaches (tunnel units, see sorcRing).
  // It closes from the screen's foot (152), swallowing the void as it rises.
  const float rim=t<CLOSE?RIM*easeOut((t-OPEN)/SPREAD):152*(1-smooth((t-CLOSE)/SHUT));
  // The rim itself while it moves: a white front with blue and violet
  // afterglow trailing toward the readout.
  const bool opening=t<OPEN+SPREAD,closing=t>=CLOSE && t<CLOSE+SHUT;
  if(opening || closing) {
    const float k=opening?1-.35f*(t-OPEN)/SPREAD:.85f,trail=opening?1:-1;
    sorcRing(rim,WHITE_INK,q8(k),false);
    sorcRing(rim-5*trail,inkOf(mixed(blue,violet,.4f)),q8(k*.5f),false);
  }
  // Rings recede into the readout: one every 0.4 s, crossing in 1.2 s; in
  // the calm one every 0.8 s, crossing in 2 s and fainter. Each ring's
  // distance falls as about (1-u)^2.2: fast at the screen's foot, slowing as it
  // nears the vanishing point, as depth does.
  const float span=(CALM-OPEN)/.4f,ph=(std::min(t,CALM)-OPEN)*(1/.4f)+std::max(0.0f,t-CALM)*(1/.8f);
  for(int n=floori(ph);n>=0 && n>floori(ph)-4;--n) {
    const bool calm=n>=span;const float born=calm?CALM+(n-span)*.8f:OPEN+n*.4f,life=calm?2.0f:1.2f;
    const float u=(t-born)/life;if(u<0 || u>=1)continue;
    const float q=1-u,s=S0*q*q*(.8f+.2f*q);if(s>rim-2)continue;
    const float k=smooth(u/.12f)*(1-smooth((u-.8f)/.2f))*(.45f+.75f*u)*(calm?.6f:1);
    const Rgb c=mixed(tints[n&3],WHITE,.25f+.3f*u);
    sorcRing(s,inkOf(c),q8(k),calm);
  }
  // Starlight streams in along the funnel: each star rides one ray from
  // beyond the screen's foot into the readout, with a streak behind it as
  // long as it is fast, then a new ray. Slower in the calm.
  const float travel=(std::min(t,CALM)-OPEN)+std::max(0.0f,t-CALM)*.4f;
  for(unsigned i=0;i<16;++i) {
    const float life=.9f+.6f*rnd(i,341),phase=rnd(i,342)+travel/life;
    const int cycle=floori(phase);const float u=phase-cycle;const uint32_t seed=i*67+uint32_t(cycle);
    const float q=1-u,s=S0*q*q*(.8f+.2f*q);if(s>rim-3)continue;
    const float a=HALF_TURN*(.1f+.8f*rnd(seed,343)),ca=cosine(a),sa=sine(a);
    const float k=smooth(u/.15f)*(1-smooth((u-.82f)/.18f))*(.6f+.6f*u);if(k<.03f)continue;
    const float x=cx+(hw+5+.7f*s)*ca,y=cy+(hh+5+1.4f*s)*sa;
    const Rgb c=mixed(tints[(i+cycle)&3],WHITE,.3f);
    // The streak: back along the ray to where the star was 40 ms before
    // (scaled by its pace), at most 7 px.
    const float ds=S0*(1.6f+.6f*q)*q*(.04f/life)*(t<CALM?1:.4f),ex=.7f*ca*ds,ey=1.4f*sa*ds,len=std::sqrt(ex*ex+ey*ey);
    if(len>1.2f){const float f=std::min(1.0f,7/len);sorcLine(x+ex*f,y+ey*f,x,y,inkOf(c),0,q8(k*.8f));}
    dot(x,y,mixed(c,WHITE,.4f),k*1.2f);
  }
  // Stars glitter round the header and the readout's flanks, each on its
  // own slow beat, as long as the void is open.
  static constexpr float STARS[][2]={{8,5},{27,3},{110,4},{126,8},{3,49},{131,51}};
  for(unsigned i=0;i<sizeof(STARS)/sizeof(STARS[0]);++i) {
    const float x=STARS[i][0]*w_/135.0f,y=STARS[i][1]<12?STARS[i][1]:b.y1+STARS[i][1]-43;
    const float k=smooth((rim-14)/10)*smooth((t-OPEN-.05f-.04f*i)/.15f)*(.45f+.55f*sine(t*(2.2f+1.7f*rnd(i,344))+i*2.3f));
    if(k>.05f)twinkle(x,y,mixed(tints[i&3],WHITE,.4f),k,1);
  }
  // Overload: two bands of light race down through the digits once the
  // rim is out, gone by 2.05 s.
  const float over=smooth((t-OPEN-.2f)/.15f)*(1-smooth((t-1.75f)/.3f));
  if(over>0)for(int band=0;band<2;++band) {
    const float p=t*(1/.3f)+band*.5f;const int yc=b.y0-2+int((b.y1-b.y0+5)*(p-floori(p)));
    const Ink c=band?inkOf(rgb8(255,200,245)):WHITE_INK;
    for(int y=std::max(b.y0,yc-1);y<=std::min(b.y1,yc+1);++y){const int k=q8(over*(y==yc?1:.6f));for(int x=b.x0;x<=b.x1;++x)sorcHeat(x,y,c,k);}
  }
  // Closed: a last glimmer at the sign, and a few stars twinkle out.
  const float z=t-CLOSE-SHUT;
  if(z>-.05f) {
    const float k=z<0?(z+.05f)/.05f:1-smooth(z/.3f);
    if(k>0)twinkle(sx,sy,ice,k,3);
    for(unsigned i=0;i<8;++i) {
      const float age=z-.03f*i,life=.32f;if(age<0 || age>=life)continue;
      const float x=6+(w_-12)*rnd(i,361),y=b.y1+8+90*rnd(i,362);
      twinkle(x,y,i%2?ice:violet,sine(HALF_TURN*age/life)*.8f,1);
    }
  }
}

// ---- Precision, after the 7:3 ratio cut): rulers draw out above
// and below the readout and tick off its tenths, the seventh tick locks a
// guide on the 7:3 point (seven parts gold, three the accent), and the
// strike falls exactly there: a vertical cut that flashes the cut-off three
// tenths white, heats the digits it crosses, breaks both rulers and throws
// sparks off the point. Sustain: the broken rulers tip and fall away while
// a faint seam breathes on the line and gold dust drifts off the two points.
// Settle: the last dust fades.
inline void Renderer::sorcPrecision(float t) {
  constexpr float DRAW=.3f,TICK0=.3f,TICKSTEP=.045f,MARK=.85f,STRIKE=1.2f;
  const Box &b=number_;const float x0=b.x0,span=float(b.x1-b.x0);
  const int X7=roundi(x0+.7f*span),yt=b.y0-5,yb=b.y1+5,yEnd=std::min(bottom_,b.y1+80);
  const Rgb silver=mixed(rgb8(210,218,232),accent_,.15f),gold=rgb8(255,196,70),pale=rgb8(255,242,205);
  const float u=t-STRIKE;
  // Rulers: drawn, ticked, recoloured at the mark, broken by the strike;
  // the halves recoil, tip and fall away.
  const float grow=easeOut(t/DRAW),mark=smooth((t-MARK)/.15f);
  const float gone=u<0?1:1-smooth(u/1.8f);
  if(gone>0)for(int r=0;r<2;++r) {
    const int y=r?yb:yt,out=r?1:-1;
    {const int d0=u<0?0:int(11.2f*u*u),d1=u<0?0:int(14*u*u)+1,e=(u<0?0:int(5*easeOut(u/.4f)))+1;sorcPrime(y+d0-e-(r?0:5),y+d1+e+(r?5:0));}
    for(int half=0;half<2;++half) {
      const float shift=u<0?0:(half?1:-1)*7*easeOut(u/.3f),drop=u<0?0:14*u*u*(half?1:.8f);
      const float tilt=u<0?0:(half?1:-1)*.06f*easeOut(u/.4f);
      const Ink line=inkOf(mixed(silver,half?accent_:gold,mark));
      const int from=half?X7+(u<0?1:2):b.x0,to=half?b.x1:X7-(u<0?1:2);
      const float end=x0+span*grow;
      const int kq=q8(.8f*gone);
      for(int x=from;x<=std::min(to,int(end));++x) {
        const float yy=y+drop+(x-X7)*tilt;
        const int xx=roundi(x+shift);if(unsigned(xx)>=unsigned(w_))continue;
        vdot(xx,yy,line,kq);
      }
      for(int i=half?7:0;i<=(half?10:7);++i) {
        const float born=t-(TICK0+i*TICKSTEP);if(born<0)continue;
        if(u>=0 && i==7)continue;
        const int tx=roundi(x0+span*i/10),len=i==7?4:i%5==0?5:3;
        const float pop=1+.8f*(1-smooth(born/.1f)),yy=y+drop+(tx-X7)*tilt;
        const int xx=roundi(tx+shift);if(unsigned(xx)>=unsigned(w_))continue;
        const Ink c=i==7?inkOf(mixed(pale,gold,mark)):line;
        for(int d=1;d<=len;++d)addw(xx,roundi(yy+out*d),c,std::min(256,int(kq*pop*(1-.12f*d))));
      }
    }
  }
  // The mark: a marching guide through the readout at 7:3, a diamond
  // reticle locking onto each ruler, and the digits on the line warming.
  if(t>=MARK && t<STRIKE) {
    const float m=t-MARK,k=smooth(m/.1f)*(.6f+.4f*sine(t*30));
    const Ink g=inkOf(mixed(gold,pale,.3f));const int phase=floori(t*40);
    for(int y=1;y<=yb+16;++y)if(((y+phase)&3)<2 && unsigned(y)<=unsigned(bottom_))addw(X7,y,g,q8(k*(y>yb+6?1.2f-(y-yb-6)*.07f:1.0f)));
    const float rr=3.5f+8*(1-easeOut(m/.22f));
    for(int r=0;r<2;++r) {
      const float ry=r?yb:yt;
      sorcLine(X7-rr,ry,X7,ry-rr,g,q8(k),q8(k));sorcLine(X7,ry-rr,X7+rr,ry,g,q8(k),q8(k));
      sorcLine(X7+rr,ry,X7,ry+rr,g,q8(k),q8(k));sorcLine(X7,ry+rr,X7-rr,ry,g,q8(k),q8(k));
    }
    for(int y=b.y0;y<=b.y1;++y)sorcHeat(X7,y,inkOf(gold),q8(.35f+.25f*sine(t*30)));
  }
  if(u<0)return;
  // The strike: a blade falls down the 7:3 line, then its afterglow fades.
  const float head=-8+(yEnd+8)*easeOut(u/.07f);
  if(u<.07f) {
    for(int y=std::max(0,int(head)-48);y<=std::min(yEnd,int(head));++y) {
      const int k=q8(1-(head-y)/50);
      addw(X7,y,WHITE_INK,k);addw(X7-1,y,inkOf(accent_),(k*150)>>8);addw(X7+1,y,inkOf(accent_),(k*150)>>8);
    }
  } else if(u>=.1f && u<.75f) {
    const float k=.85f*(1-smooth((u-.1f)/.65f));const Ink core=inkOf(mixed(WHITE,gold,smooth((u-.1f)/.3f)));
    const int kq=q8(k);
    for(int y=0;y<=yEnd;++y){const int f=kq*(yEnd+40-y)/(yEnd+40);addw(X7,y,core,f);if(u<.3f)addw(X7+((y&1)?1:-1),y,inkOf(accent_),f>>2);}
  }
  // The cut-off three tenths of the readout flash white.
  if(u<.1f){const int k=q8(.85f*(1-smooth(u/.12f)));for(int y=b.y0;y<=b.y1;++y)for(int x=X7+2;x<=b.x1;++x)sorcHeat(x,y,WHITE_INK,k);}
  // The digits on the cut flash white and cool through gold.
  const float heat=1-smooth((u-.05f)/.5f);
  if(heat>0) {
    const Ink hot=inkOf(mixed(WHITE,gold,smooth(u/.35f)));
    for(int y=b.y0;y<=std::min(b.y1,int(head));++y){sorcHeat(X7,y,hot,q8(heat));sorcHeat(X7-1,y,hot,q8(heat*.5f));sorcHeat(X7+1,y,hot,q8(heat*.5f));}
  }
  // Impact flares along both rulers, and sparks off the point.
  if(u>=.02f && u<.4f) {
    const float v=(u-.02f)/.38f,len=4+18*easeOut(v/.3f),k=(1-v)*(1-v);
    for(int r=0;r<2;++r) {
      const float y=r?yb:yt;
      sorcLine(X7,y,X7-len,y,inkOf(pale),q8(k),0);sorcLine(X7,y,X7+len,y,inkOf(pale),q8(k),0);
      twinkle(X7,y,WHITE,k*1.3f,3);
    }
  }
  for(unsigned i=0;i<16;++i) {
    const float age=u-.02f-.04f*rnd(i,371),life=.45f+.35f*rnd(i,372);if(age<0 || age>=life)continue;
    const bool top=i%2==0;const float side=rnd(i,373)<.5f?-1:1;
    const float vx=side*(35+85*rnd(i,374)),vy=(top?-1:1)*(8+40*rnd(i,375));
    const float x=X7+vx*age,y=(top?yt:yb)+vy*age+150*age*age,k=1-age/life;
    const Rgb c=i%3==0?WHITE:i%3==1?gold:pale;
    plot(x-vx*.02f,y-(vy+300*age)*.02f,c,k*.4f);dot(x,y,c,k);
  }
  // The seam: a faint line on the cut that breathes, then fades.
  const float seam=.42f*smooth((u-.3f)/.4f)*(1-smooth((u-2.0f)/.7f));
  if(seam>0) {
    const int kq=q8(seam*(.7f+.3f*sine(u*4)));const Ink c=inkOf(mixed(gold,pale,.4f));
    for(int y=std::max(0,yt-8);y<=std::min(bottom_,yb+22);++y)addw(X7,y,c,y>yb+10?kq*(yb+23-y)/13:kq);
  }
  // The result: hairlines under the readout, seven tenths gold and three
  // the accent, split at the cut, with a glint running in along each to it.
  const float result=smooth((u-.7f)/.4f)*(1-smooth((u-2.4f)/.8f));
  if(result>0) {
    const int y=yb+2;sorcPrime(y,y);
    const float run=u*.7f-floori(u*.7f);
    const float gl=b.x0+(X7-3-b.x0)*run,gr=b.x1-(b.x1-X7-3)*run;
    for(int x=b.x0;x<=b.x1;++x) {
      if(std::abs(x-X7)<3)continue;
      const bool left=x<X7;const float g=std::max(0.0f,1-std::fabs(x-(left?gl:gr))*(1/7.0f));
      addw(x,y,left?inkOf(mixed(gold,pale,g)):inkOf(mixed(accent_,WHITE,g*.6f)),q8(result*(.4f+.6f*g)));
    }
  }
  // Gold dust drifts off the two struck points.
  for(unsigned i=0;i<40;++i) {
    const float age=u-.15f-1.8f*rnd(i,376),life=.9f+.7f*rnd(i,377);if(age<0 || age>=life)continue;
    const bool top=i%3==0;const float x=X7+(rnd(i,378)-.5f)*6+(rnd(i,379)-.5f)*18*age+1.5f*sine(age*3+i);
    const float y=(top?yt:yb)+(top?-1:1)*(1+(3+7*rnd(i,380))*age),v=age/life;
    dot(x,y,i%2?gold:pale,1.2f*smooth(age/.12f)*(1-v)*(.6f+.4f*sine(age*9+i*1.3f)));
  }
}

// ---- Umbra, after Chimera Shadow Garden): liquid shadow pools
// out round the readout's foot across the floor, its rim catching a cold
// sheen; the digits sink into it below a lit waterline; shadow creatures
// rise out of the pool and open their eyes. Sustain: the readout surfaces
// while the wolves stay, bobbing in the pool, blinking now and then and
// sending out ripples, as glints slide over it. Then they sink back and the
// pool drains away.
inline void Renderer::sorcUmbra(float t) {
  constexpr float FLOOD=.8f,SURFACE=1.65f,SINK=3.35f,DRAIN=3.95f,END=4.75f;
  const Box &b=number_;const float cx=b.cx(),h=float(b.y1-b.y0);
  const float py=b.y1+2.0f;
  const Rgb sheen=mixed(rgb8(120,145,215),accent_,.2f),bright=rgb8(205,220,255),eye=rgb8(255,236,175);
  // The pool's size: spreading fast and slowing, held, then draining.
  const float s=t<FLOOD?easeOut(t/FLOOD):1-smooth((t-DRAIN)/(END-DRAIN));
  // The digits sink: a wavering shadow line rises up the readout, darkens
  // everything below it and rim-lights the glyphs it crosses.
  const float level=t<1.05f?smooth((t-.35f)/.7f):1-smooth((t-SURFACE)/.45f);
  if(level>0) {
    const int L8=q8(b.y1+1.5f-(h*.48f+2.5f)*level);
    // The waver: two sines whose phases advance by a fixed angle per
    // column, stepped by rotation (no table lookup per column).
    int off[MAX_W],offMin=1<<30,offMax=-(1<<30);
    {
      float c1=cosine(b.x0*.21f+t*4.5f),s1=sine(b.x0*.21f+t*4.5f),c2=cosine(b.x0*.47f-t*3),s2=sine(b.x0*.47f-t*3);
      const float cd1=cosine(.21f),sd1=sine(.21f),cd2=cosine(.47f),sd2=sine(.47f);
      for(int x=b.x0;x<=b.x1;++x) {
        const int o=q8(1.2f*s1+.6f*s2);off[x]=o;offMin=std::min(offMin,o);offMax=std::max(offMax,o);
        const float n1=c1*cd1-s1*sd1,n2=c2*cd2-s2*sd2;s1=s1*cd1+c1*sd1;c1=n1;s2=s2*cd2+c2*sd2;c2=n2;
      }
    }
    // Both pulls keep sorcShade's floor: no glyph pixel goes below 3:1 on
    // the background (the digits stay readable on any theme, light ones
    // included). The readout is one colour, so each depth's result is
    // remembered for the last input colour (bg_ marks an empty slot: a
    // result equal to the background is nudged off it). The shadow's pull
    // stops where the contrast reaches the floor, searched once per colour;
    // a depth past that stop takes it, and every result is checked against
    // the floor, with sorcShade's own search when one misses it.
    const Ink shadow=inkOf(rgb(load(bg_))),rim=inkOf(bright);
    uint16_t lastIn=bg_,memo[64];detail::SorcShader dark;int stop=0;
    auto shaded=[&](uint16_t p,int slot) {
      if(p!=lastIn){lastIn=p;std::fill(memo,memo+64,bg_);dark=detail::SorcShader(load(p),load(bg_),shadow.r>>8,shadow.g>>8,shadow.b>>8,READOUT_CONTRAST100);stop=dark.limit(232);}
      uint16_t &m=memo[slot];
      if(m==bg_) {
        uint16_t out;
        if(slot<63){const int k=170+slot;out=dark.at(std::min(k,stop));if(!dark.readable(out))out=dark.pull(k);}
        else out=sorcShade(load(p),rim,180);
        out=store(out);m=out==bg_?uint16_t(out^store(1)):out;
      }
      return m;
    };
    for(int y=std::max(b.y0,(L8>>8)-3);y<=b.y1;++y) {
      // Depth d8 of pixel x is base-off[x]: a row the line misses is
      // skipped, and a row wholly 4 px or more under it is one slot.
      const int base=(y<<8)-L8;if(base-offMin<=-160)continue;
      uint16_t *row=px_+y*w_;
      if(base-offMax>=62*16) {
        for(int x=b.x0;x<=b.x1;++x) {
          const uint16_t p=row[x];if(p==bg_)continue;
          row[x]=shaded(p,62);++plots_;
        }
        continue;
      }
      for(int x=b.x0;x<=b.x1;++x) {
        const uint16_t p=row[x];if(p==bg_)continue;
        const int d8=base-off[x];if(d8<=-160)continue;
        // Under the shadow: toward the background, deeper is darker (slots
        // 0..62); the waterline catches the light (slot 63).
        row[x]=shaded(p,d8>96?std::min(62,d8>>4):63);++plots_;
      }
    }
  }
  // The pool's near rim: a wavering perspective half-ellipse, brightest at
  // the front (its far side is behind the readout).
  if(s>.01f) {
    const float rx=90*s,ry=40*s,fade=t<DRAIN?1:std::max(.3f,s);
    const Ink c=inkOf(sheen),hi=inkOf(mixed(sheen,bright,.5f));
    auto rimY=[&](float x){const float u=(x-cx)/rx;if(u<=-1 || u>=1)return py;
      return py+ry*std::sqrt(1-u*u)*(1+.05f*sine(u*7+t*3)+.03f*sine(u*13-t*5));};
    // Prime the words where the rim crosses a word boundary, so the rim
    // does not take its own light there for UI ink.
    for(int bx=32;bx<w_;bx+=32){const int y=roundi(rimY(float(bx)));for(int r=y-1;r<=y+2;++r){if(unsigned(r)>unsigned(bottom_))continue;blocked(bx-1,r);blocked(bx,r);}}
    // Column by column, as rimY: u steps by 1/rx, one square root per
    // column serves both the depth and the brightness, and the two wobble
    // sines advance by rotation.
    {
      const int x0=std::max(0,int(cx-rx)),x1=std::min(w_-1,int(cx+rx));
      const float du=1/rx,u0=(x0-cx)*du,cd1=cosine(7*du),sd1=sine(7*du),cd2=cosine(13*du),sd2=sine(13*du);
      float u=u0,c1=cosine(u0*7+t*3),s1=sine(u0*7+t*3),c2=cosine(u0*13-t*5),s2=sine(u0*13-t*5);
      const int k0=q8(fade*.45f);const float kf=fade*.55f*256;
      for(int x=x0;x<=x1;++x,u+=du) {
        const float front=u>-1 && u<1?std::sqrt(1-u*u):0,y=py+ry*front*(1+.05f*s1+.03f*s2);
        const int k=k0+int(kf*front);vdot(x,y,hi,k);if(front>.55f)vdot(x,y-1.2f,c,k>>1);
        const float n1=c1*cd1-s1*sd1,n2=c2*cd2-s2*sd2;s1=s1*cd1+c1*sd1;c1=n1;s2=s2*cd2+c2*sd2;c2=n2;
      }
    }
    // Oily glints slide over the pool.
    for(unsigned i=0;i<6;++i) {
      const float u=-.75f+1.5f*rnd(i,381)+.08f*sine(t*1.3f+i),v=.35f+.5f*rnd(i,382);
      const float y=py+ry*v,x=cx+rx*u*std::sqrt(std::max(0.0f,1-v*v)),len=(4+7*rnd(i,383))*s;
      const int k=q8(fade*.45f*(.6f+.4f*sine(t*2.5f+i*1.7f)));
      for(int d=-int(len);d<=int(len);++d){const int xx=roundi(x+d);if(unsigned(xx)<unsigned(w_))vdot(xx,y,c,k*(int(len)+1-std::abs(d))/(int(len)+1));}
    }
  }
  // Creatures: three wolves' heads in profile rise from the pool, the
  // outer two looking away and the middle one back, open their eyes,
  // bob and blink through the sustain, and sink back.
  static constexpr float HEAD[][2]={{-.95f,0},{-.9f,-.8f},{-.62f,-1.15f},{-.48f,-1.95f},{-.15f,-1.2f},{.3f,-1.1f},{.75f,-.95f},
                                    {1.25f,-.72f},{1.42f,-.55f},{1.22f,-.4f},{.75f,-.3f},{.4f,-.1f},{.3f,0}};
  constexpr int POINTS=sizeof(HEAD)/sizeof(HEAD[0]);
  for(int i=0;i<3;++i) {
    const float hx=i==0?cx-38:i==1?cx+33:cx-2,size=i==2?11.5f:9.5f,face=i==1?1.0f:-1.0f;
    const float base=py+40*(i==2?.88f:.62f),start=.85f+.1f*i,sinkAt=SINK+.15f*i;
    const float e=t<sinkAt?easeOut((t-start)/.5f):1-smooth((t-sinkAt)/.45f);
    // Ripples spread from each head as it bobs.
    for(int j=0;j<2;++j) {
      const float age=t-(1.55f+.85f*j+.4f*rnd(i*7+j,391)+.25f*i),life=.95f;if(age<0 || age>=life || t>sinkAt)continue;
      const float rr=size*(1.3f+2.2f*age/life);const int kk=q8(.5f*(1-age/life));
      for(int q=0;q<=20;++q){const float an=q*(HALF_TURN/20);addw(roundi(hx+rr*cosine(an)),roundi(base+rr*.28f*sine(an)),inkOf(sheen),kk);}
    }
    if(e<=0)continue;
    const float lift=(1-e)*size*2.1f+.9f*sine(t*2.1f+i*2.3f)*smooth((t-start-.5f)/.3f);
    const Ink outline=inkOf(mixed(sheen,bright,.4f));const int k=q8(.95f*std::min(1.0f,e*2));
    for(int p=0;p+1<POINTS;++p) {
      float ax=hx+face*HEAD[p][0]*size,ay=base+lift+HEAD[p][1]*size,bx=hx+face*HEAD[p+1][0]*size,by=base+lift+HEAD[p+1][1]*size;
      if(ay>=base && by>=base)continue;
      if(ay>base){const float f=(ay-base)/(ay-by);ax+=(bx-ax)*f;ay=base;}
      if(by>base){const float f=(by-base)/(by-ay);bx+=(ax-bx)*f;by=base;}
      sorcLine(ax,ay,bx,by,outline,k,k);
    }
    // Where the head breaks the surface, a small rim-lit ripple.
    if(e<1) {
      const float rr=size*(1.4f+.6f*(1-e));const int kk=q8(.6f*std::min(1.0f,e*3)*(1-e*.6f));
      for(int j=0;j<=16;++j){const float an=j*(HALF_TURN/16);addw(roundi(hx+rr*cosine(an)),roundi(base+rr*.28f*sine(an)),inkOf(sheen),kk);}
    }
    const float open=smooth((e-.75f)/.25f);
    bool blink=t>1.45f+.09f*i && t<1.51f+.09f*i;
    for(int j=0;j<3;++j){const float at=1.95f+.5f*j+.35f*rnd(i*5+j,392);blink|=t>at && t<at+.06f;}
    if(open>0 && !blink) {
      const float ex=hx+face*.28f*size,ey=base+lift-.8f*size;
      dot(ex,ey,eye,open*1.4f);dot(ex+face*1.2f,ey+.4f,eye,open*.8f);
    }
  }
}

// ---- Ravage, after Malevolent Shrine): a shrine's roof looms in
// dim red under the readout, pulsing like a heartbeat; then a storm of
// unseen blades: cuts flash everywhere for a single frame, each leaving a
// red scar and flinging debris, some slicing through the digits; it ends on
// one great crossed cut through the readout. Sustain: the shrine keeps
// beating, slower, a stray cut flickers below the readout now and then and
// ash drifts down. Then the shrine fades.
inline void Renderer::sorcRavage(float t) {
  constexpr float BARRAGE=.5f,FINAL=1.55f,LINGER=3.7f,FADE=3.9f;
  const Box &b=number_;const float cx=b.cx(),cy=b.cy(),h=float(b.y1-b.y0);
  const Rgb crimson=rgb8(215,12,28),blood=rgb8(120,0,14),steel=rgb8(255,232,228);
  const Ink crimsonInk=inkOf(crimson);
  const int frame=floori(t*20+.001f);
  // The shrine: fades in, beats lub-dub every 0.8 s, flares at the final
  // cut, then beats on every 1.1 s.
  {
    const float P=t<FINAL?t*(1/.8f):FINAL*(1/.8f)+(t-FINAL)*(1/1.1f),ph=(P-floori(P))*(t<FINAL?.8f:1.1f);
    auto pulse=[](float x){return x<0?0.0f:x<.05f?x/.05f:std::max(0.0f,1-(x-.05f)/.25f);};
    const float beat=pulse(ph)+.6f*pulse(ph-.17f),flare=t>=FINAL?1-smooth((t-FINAL)/.5f):0;
    const float presence=smooth(t/.5f)*(1-smooth((t-FADE)/.8f));
    const float k=presence*(.45f+.45f*beat+.5f*flare);
    if(k>.02f) {
      const Ink c=inkOf(mixed(blood,crimson,.4f*beat+flare));const int kq=q8(k),k2=q8(k*.6f);
      // Two tiers of curved roof with upturned eaves, rafters under them,
      // a horned ridge on top and posts below: a temple's silhouette.
      const float top=b.y1+13.0f;
      auto roofY=[&](float x,float half,float t0,float depth){
        const float v=std::fabs(x-cx)/half;return t0+depth*v*std::sqrt(v)-(v>.84f?(v-.84f)*depth*2.6f:0);};
      static constexpr float TIER[2][3]={{63,0,11},{44,15,7}};
      for(int tier=0;tier<2;++tier) {
        const float half=TIER[tier][0],t0=top+TIER[tier][1],depth=TIER[tier][2];
        for(int bx=32;bx<w_;bx+=32){if(std::fabs(bx-cx)>half)continue;const int y=roundi(roofY(float(bx),half,t0,depth));for(int r=y-1;r<=y+5;++r)if(unsigned(r)<=unsigned(bottom_)){blocked(bx-1,r);blocked(bx,r);}}
        for(int x=roundi(cx-half);x<=roundi(cx+half);++x) {
          const float y=roofY(float(x),half,t0,depth),v=std::fabs(x-cx)/half;
          addw(x,roundi(y),c,tier?k2:kq);
          if(v<.95f && (tier==0 || (x&1)))addw(x,roundi(y+3),c,k2);
          if(v<.9f && (x-roundi(cx))%5==0){addw(x,roundi(y+1.5f),c,k2>>1);addw(x,roundi(y+2.2f),c,k2>>1);}
        }
      }
      for(int x=roundi(cx-10);x<=roundi(cx+10);++x)addw(x,roundi(top-2),c,kq);
      for(int side=-1;side<=1;side+=2) {
        sorcLine(cx+side*10,top-2,cx+side*13,top-6,c,kq,kq>>1);
        for(int post=0;post<2;++post) {
          const float px=cx+side*(post?32:14),y0=roofY(px,44,top+15,7)+4;
          sorcLine(px,y0,px,top+34,c,k2,0);
        }
      }
    }
  }
  // The storm: every frame a new set of cuts flashes for that frame alone.
  const float storm=smooth((t-BARRAGE)/.35f)*(1-smooth((t-FINAL+.2f)/.18f));
  if(storm>0)for(int f=frame-1;f<=frame;++f) {
    const bool now=f==frame;const int n=2+int(5.5f*storm);
    for(int j=0;j<n;++j) {
      const uint32_t seed=uint32_t(f)*16+j;
      const float x=6+(w_-12)*rnd(seed,391),y=rnd(seed,392)<.7f?b.y0-8+(h+18)*rnd(seed,393):b.y1+12+60*rnd(seed,393);
      const float an=HALF_TURN*rnd(seed,394),half=6+11*rnd(seed,395),ux=cosine(an)*half,uy=sine(an)*half;
      if(now) {
        // A white core tapering to both ends, with a red edge on one side.
        sorcLine(x,y,x+ux,y+uy,WHITE_INK,q8(storm),0);sorcLine(x,y,x-ux,y-uy,WHITE_INK,q8(storm),0);
        const float nx=-uy/half,ny=ux/half;
        sorcLine(x+nx-ux*.8f,y+ny-uy*.8f,x+nx+ux*.8f,y+ny+uy*.8f,crimsonInk,q8(.7f*storm),q8(.7f*storm));
        sorcHeatLine(x-ux,y-uy,x+ux,y+uy,WHITE_INK,q8(.8f*storm));
      } else if(j%2==0)sorcLine(x-ux*.9f,y-uy*.9f,x+ux*.9f,y+uy*.9f,crimsonInk,q8(.32f*storm),q8(.32f*storm));
    }
  }
  // The final cut: two long slashes cross through the readout's centre.
  const float u=t-FINAL;
  if(u>=0 && u<.6f) {
    for(int c=0;c<2;++c) {
      const float v=u-.05f*c;if(v<0)continue;
      const float an=c?.3f:-.3f,ca=cosine(an),sa=sine(an),reach=95;
      const float dir=c?-1:1,head=-reach+2*reach*easeOut(v/.06f);
      const float ax=cx-dir*reach*ca,ay=cy-dir*reach*sa,hx=cx+dir*head*ca,hy=cy+dir*head*sa;
      const float k=v<.06f?1:1-smooth((v-.06f)/.5f);const float nx=-sa,ny=ca;
      const Ink core=v<.12f?WHITE_INK:inkOf(mixed(steel,crimson,smooth((v-.12f)/.2f)));
      // The light runs outside the readout's rows; across the digits the
      // cut is drawn on the glyphs themselves (the heat line below).
      const float in=(b.y1-b.y0)*.5f/std::fabs(sa)+2;
      for(int part=0;part<2;++part) {
        const float s0=part?in:-reach,s1=part?head:std::min(head,-in);if(s1<=s0)continue;
        const float px0=cx+dir*s0*ca,py0=cy+dir*s0*sa,px1=cx+dir*s1*ca,py1=cy+dir*s1*sa;
        sorcLine(px0,py0,px1,py1,core,q8(k),q8(k));
        sorcLine(px0+nx,py0+ny,px1+nx,py1+ny,crimsonInk,q8(k*.7f),q8(k*.7f));
        if(v<.15f)sorcLine(px0-nx,py0-ny,px1-nx,py1-ny,crimsonInk,q8(k*.7f),q8(k*.7f));
      }
      sorcHeatLine(ax,ay,hx,hy,WHITE_INK,q8(1-smooth((v-.03f)/.22f)));
    }
  }
  // Sustain: now and then a stray cut flickers below the readout (never on
  // the digits), leaving a scar for a frame.
  if(t>=FINAL+.45f && t<LINGER)for(int f=frame-1;f<=frame;++f)for(int j=14;j<16;++j) {
    const uint32_t seed=uint32_t(f)*16+j;const float ft=f*.05f;
    if(ft<FINAL+.45f || rnd(seed,396)>.45f*(1-smooth((ft-3.0f)/.7f)))continue;
    const float x=10+(w_-20)*rnd(seed,391),y=b.y1+10+70*rnd(seed,393);
    const float an=HALF_TURN*rnd(seed,394),half=9+9*rnd(seed,395),ux=cosine(an)*half,uy=sine(an)*half;
    if(f==frame){sorcLine(x,y,x+ux,y+uy,WHITE_INK,q8(.85f),0);sorcLine(x,y,x-ux,y-uy,WHITE_INK,q8(.85f),0);sorcLine(x-uy/half,y+ux/half,x+ux*.7f-uy/half,y+uy*.7f+ux/half,crimsonInk,q8(.6f),q8(.3f));}
    else sorcLine(x-ux*.9f,y-uy*.9f,x+ux*.9f,y+uy*.9f,crimsonInk,q8(.28f),q8(.28f));
  }
  // Ash drifts down through the sustain.
  for(unsigned i=0;i<38;++i) {
    const float age=t-FINAL-1.75f*rnd(i,408),life=1.0f+.6f*rnd(i,409);if(age<0 || age>=life)continue;
    const float x=8+(w_-16)*rnd(i,410)+3*sine(age*2.3f+i),y=b.y1+4+40*rnd(i,411)+(8+10*rnd(i,412))*age,v=age/life;
    dot(x,y,mixed(rgb8(255,96,64),blood,v*v),1.5f*smooth(age/.15f)*(1-v)*(.7f+.3f*sine(age*7+i)));
  }
  // Debris: shreds fly from the cuts and fall, tumbling.
  for(unsigned i=0;i<30;++i) {
    const bool last=i>=16;const float birth=last?FINAL+.03f+.05f*rnd(i,401):BARRAGE+.15f+1.0f*rnd(i,401);
    const float age=t-birth,life=.45f+.35f*rnd(i,402);if(age<0 || age>=life)continue;
    const float x0=last?cx+(rnd(i,403)-.5f)*120:6+(w_-12)*rnd(i,403),y0=last?cy+(x0-cx)*(i%2?.31f:-.31f):b.y0-6+(h+30)*rnd(i,404);
    const float vx=(rnd(i,405)-.5f)*(last?150:110),vy=-20-70*rnd(i,406);
    const float x=x0+vx*age,y=y0+vy*age+240*age*age,spin=age*(14+10*rnd(i,407))+i,k=1-age/life;
    const float dx=1.6f*cosine(spin),dy=1.6f*sine(spin);
    sorcLine(x-dx,y-dy,x+dx,y+dy,inkOf(mixed(steel,crimson,.3f+.7f*age/life)),q8(k),q8(k*.6f));
  }
}
}
