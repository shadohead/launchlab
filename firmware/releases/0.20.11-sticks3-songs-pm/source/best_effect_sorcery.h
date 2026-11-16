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
// on the glyphs) and gives it back by 2.1 s; Precision and Ravage only
// brighten glyph pixels (every channel up) and stop by 1.7 and 1.85 s;
// Boundless only glints them, while it covers everything below them and
// around their halo from the start and gives it back by 3.85 s. Nothing takes
// a glyph pixel below 3:1 on the background:
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

// ---- Rupture, after Black Flash: cursed energy (the accent)
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

namespace sorcery {
// Whether a theme background is light (Paper and the like), as the glint judges it.
BEST_EFFECT_INLINE bool lightBackground(unsigned bv){return 2*(bv>>11)+((bv>>5)&63)+2*(bv&31)>=94;}
}
// Pixels x0..x1 of a row set to the stored value sv outright, two per
// 32-bit store and no per-pixel test: on the device a per-pixel loop over
// the covered rows alone costs 7-12 ms (HIL effect_times, 2026-10-21).
inline void Renderer::sorcSpan(uint16_t *row,int x0,int x1,uint16_t sv) {
  x0=std::max(0,x0);x1=std::min(w_-1,x1);if(x0>x1)return;
  fills_+=uint32_t(x1-x0+1);++spans_;
  uint16_t *p=row+x0,*const end=row+x1+1;
  if(reinterpret_cast<uintptr_t>(p)&2)*p++=sv;
  typedef uint32_t __attribute__((may_alias)) Pair;
  const uint32_t two=uint32_t(sv)*0x10001u;
  // Eight pixels per pass: on the device the loop's own steps cost more
  // than the stores.
  Pair *q=reinterpret_cast<Pair *>(p);const int pairs=int(end-p)>>1;
  for(int i=pairs>>2;i>0;--i,q+=4){q[0]=two;q[1]=two;q[2]=two;q[3]=two;}
  for(int i=pairs&3;i>0;--i)*q++=two;
  if((end-p)&1)end[-1]=sv;
}
// A solid splotch: every pixel of rows y0..y1 inside an ink disc and outside
// every hole disc is set to v, stored outright (fills_), not blended. Rows
// from `raw` on are covered rows (covers()): every pixel there is set, the
// uncovered ones to `under` (a stored value) unless it is negative. Rows
// above keep the UI and its halo: only background pixels clear of the mask
// change, and only words within HALO of a row's ink pay for a mask. Each
// row's covered pixels are a bit set of disc spans (one table root
// per disc and row, stepped from the row before, only for the discs a row
// crosses: they are sorted by top row and kept in an active list); covered
// rows are then filled run by run (sorcSpan). At most 64
// discs, radii clamped to 400 px. Every mask word is built before the
// first pixel is set, so it never masks itself; draw it before other light
// in those rows.
inline __attribute__((noinline)) void Renderer::sorcSplotch(const SorcDisc *ink,int inks,const SorcDisc *holes,int nholes,int y0,int y1,uint16_t v,int raw,int under) {
  y0=std::max(0,y0);y1=std::min(h_-1,y1);
  const int words=(w_+31)/32;
  uint32_t bits[WORDS];
  // Each disc on whole pixels (inks first, then holes), crossing rows
  // top..bottom; sorted by top row and walked with an active list. Its
  // half-width h on a row is the largest with (2h)^2 < 4r^2-(2dy)^2, found
  // by stepping it from the row before (no root: on the device a float root
  // costs 2.3 us and a flash table look-up more); each step is a touch.
  struct Span{float r4,h2;int16_t x,y,top,bottom;};
  constexpr int MAX=64;
  const int n=std::min(MAX,inks+nholes);
  Span d[MAX];uint8_t order[MAX],active[MAX];int live=0,next=0;
  for(int k=0;k<n;++k) {
    const SorcDisc &s=k<inks?ink[k]:holes[k-inks];
    const float r=std::min(std::max(s.r,0.0f),400.0f);const int ry=int(r);
    Span &e=d[k];e.r4=4*r*r;e.x=int16_t(std::max(-512,std::min(512,roundi(s.x))));
    e.y=int16_t(std::max(-512,std::min(512,roundi(s.y))));e.top=int16_t(e.y-ry);e.bottom=int16_t(r>0?e.y+ry:e.y-1);
    int j=k;while(j>0 && d[order[j-1]].top>e.top){order[j]=order[j-1];--j;}
    order[j]=uint8_t(k);
  }
  // Rows are covered in increasing order within a pass; restart() begins one.
  auto restart=[&]{live=next=0;for(int k=0;k<n;++k)d[k].h2=-2;};
  // One disc past every corner (the negative) covers every row whole: no
  // per-row edges (1.7 ms a frame on the device).
  bool whole=inks==1 && nholes==0;
  if(whole)for(int corner=0;corner<4;++corner) {
    const float dx=(corner&1?w_-1:0)-ink[0].x,dy=(corner&2?h_-1:0)-ink[0].y;
    whole&=dx*dx+dy*dy<ink[0].r*ink[0].r;
  }
  auto cover=[&](int y) {
    if(whole){for(int i=0;i<words;++i)bits[i]=i<words-1 || w_%32==0?~0u:(1u<<(w_%32))-1;return;}
    for(int i=0;i<words;++i)bits[i]=0;
    while(next<n && d[order[next]].top<=y)active[live++]=order[next++];
    for(int a=0;a<live;)if(d[active[a]].bottom<y)active[a]=active[--live];else ++a;
    auto span=[&](Span &e,bool set) {
      // h2 is twice the half-width: grow it while the next fits, shrink it while it does not.
      const float dy=float(2*(y-e.y)),t=e.r4-dy*dy;
      ++touches_;while((e.h2+2)*(e.h2+2)<t){e.h2+=2;++touches_;}
      while(e.h2>=0 && e.h2*e.h2>=t){e.h2-=2;++touches_;}
      if(e.h2<0)return;
      const int h=int(e.h2)>>1,first=std::max(0,e.x-h),last=std::min(w_-1,e.x+h);
      for(int x=first;x<=last;) {
        const int i=x>>5,lo=x&31,hi=std::min(31,lo+(last-x));
        const uint32_t m=(hi==31?~0u:(2u<<hi)-1)&~((1u<<lo)-1);
        if(set)bits[i]|=m;else bits[i]&=~m;
        x+=hi-lo+1;
      }
    };
    for(int a=0;a<live;++a)if(active[a]<inks)span(d[active[a]],true);
    for(int a=0;a<live;++a)if(active[a]>=inks)span(d[active[a]],false);
  };
  // The UI rows: the words each row's ink (widened by HALO) reaches.
  const int masked=std::min(y1,raw-1);uint8_t reach[MAX_H];
  if(y0<=masked)for(int r=std::max(0,y0-HALO);r<=std::min(h_-1,masked+HALO);++r) {
    const uint16_t *p=px_+r*w_;int left=0,right=w_-1;
    while(left<=right && p[left]==bg_)++left;
    touches_+=uint32_t(left+1);
    if(left>right){reach[r]=0;continue;}
    while(p[right]==bg_)--right;
    touches_+=uint32_t(w_-right);
    const int a=std::max(0,left-HALO)>>5,b=std::min(w_-1,right+HALO)>>5;
    reach[r]=uint8_t(((2u<<b)-1)&~((1u<<a)-1));
  }
  auto near=[&](int y){unsigned m=0;for(int r=std::max(0,y-HALO);r<=std::min(h_-1,y+HALO);++r)m|=reach[r];return m;};
  restart();
  for(int y=y0;y<=masked;++y) {
    const unsigned m=near(y);if(!m)continue;
    cover(y);
    for(int i=0;i<words;++i)if(bits[i] && (m>>i&1) && blockedStamp_[y][i]!=stamp_)buildBlocked(y,i);
  }
  sorcMark(4);
  const uint16_t sv=store(v);
  // Fills a row's runs of set bits with sv, and of clear bits with `fill`
  // unless it is negative, a word at a time.
  auto runs=[&](uint16_t *row,int fill) {
    for(int x=0;x<w_;) {
      const bool on=(bits[x>>5]>>(x&31))&1u;
      int i=x>>5;uint32_t m=(on?~bits[i]:bits[i])&(~0u<<(x&31));
      while(!m && ++i<words)m=on?~bits[i]:bits[i];
      const int end=i<words?std::min(w_,i*32+__builtin_ctz(m)):w_;
      if(on)sorcSpan(row,x,end-1,sv);else if(fill>=0)sorcSpan(row,x,end-1,uint16_t(fill));
      x=end;
    }
  };
  restart();
  for(int y=y0;y<=y1;++y) {
    cover(y);uint16_t *row=px_+y*w_;
    sorcMark(5);
    if(y>=raw) {
      runs(row,under);
      sorcMark(7);
      continue;
    }
    // UI rows: only pixels clear of the mask, still filled run by run.
    const unsigned reached=near(y);
    for(int i=0;i<words;++i)if(reached>>i&1)bits[i]&=~blocked_[y][i];
    runs(row,-1);
    sorcMark(6);
  }
}
// The black-and-white negative of a colour (value, not stored): the inverse
// of its own grey.
inline uint16_t Renderer::sorcNegate(uint16_t stored)const {
  const unsigned v=load(stored);const int n=255-int(((v>>11)*630+((v>>5)&63)*608+(v&31)*240)>>8);
  return uint16_t((n>>3)<<11|(n>>2)<<5|(n>>3));
}
// Light on a dark background, exactly as addw; on a light one the same
// weight darkens instead (by the ink's brightest channel), so the void
// reads on any theme.
inline void Renderer::sorcLight(int x,int y,const Ink &c,int w) {
  if(!sorcery::lightBackground(load(bg_))){addw(x,y,c,w);return;}
  if(w<5)return;
  ++probes_;if(blocked(x,y))return;
  const int edge=x&31;
  if(edge<HALO){if(x>=32)ink(y,(x>>5)-1);}
  else if(edge>=32-HALO && (x|31)+1<w_)ink(y,(x>>5)+1);
  uint16_t &p=px_[y*w_+x];const unsigned v=load(p);
  const int d=(std::max(c.r,std::max(c.g>>1,c.b))*w+32768)>>16;
  p=store(uint16_t(std::max(0,int(v>>11)-d)<<11|std::max(0,int((v>>5)&63)-2*d)<<5|std::max(0,int(v&31)-d)));++plots_;
}
// The void's colours: light added to the background, or on a light theme
// taken away from it as a dark blue-black (the negative of the light).
inline uint16_t Renderer::sorcVoidColor(const Rgb &l,float f)const {
  const unsigned v=load(bg_);const int r=v>>11,g=(v>>5)&63,b=v&31;
  if(!sorcery::lightBackground(v))
    return store(uint16_t(std::min(31,r+int(l.r*f+.5f))<<11|std::min(63,g+int(l.g*f+.5f))<<5|std::min(31,b+int(l.b*f+.5f))));
  const float d=std::max(l.r,std::max(l.g*.5f,l.b))*f;
  return store(uint16_t(std::max(0,r-int(d+.5f))<<11|std::max(0,g-int(2*d+.5f))<<5|std::max(0,b-int(.55f*d+.5f))));
}
// Light on the void: in a covered row (from `raw`) it lands outright, with
// no mask, as the void owns those rows; above them it is sorcLight, or
// nothing unless `above`.
inline void Renderer::sorcVoidLight(int x,int y,const Ink &c,int w,int raw,bool above) {
  if(y<raw){if(above && unsigned(x)<unsigned(w_) && y>=0)sorcLight(x,y,c,w);return;}
  if(w<5 || unsigned(x)>=unsigned(w_) || y>=h_)return;
  uint16_t &p=px_[y*w_+x];const unsigned v=load(p);int r=v>>11,g=(v>>5)&63,b=v&31;
  if(sorcery::lightBackground(load(bg_))) {
    const int d=(std::max(c.r,std::max(c.g>>1,c.b))*w+32768)>>16;
    r=std::max(0,r-d);g=std::max(0,g-2*d);b=std::max(0,b-((d*9)>>4));
  } else {
    r=std::min(31,r+((c.r*w+32768)>>16));g=std::min(63,g+((c.g*w+32768)>>16));b=std::min(31,b+((c.b*w+32768)>>16));
  }
  p=store(uint16_t(r<<11|g<<5|b));++plots_;
}
inline void Renderer::sorcVoidLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1,int raw,bool above) {
  const float dx=x1-x0,dy=y1-y0;const int n=std::max(1,int(std::max(std::fabs(dx),std::fabs(dy))));
  int x8=q8(x0)+128,y8=q8(y0)+128,wq=k0<<8;const int sx=q8(dx/n),sy=q8(dy/n),dw=(k1-k0)*256/n;
  for(int i=0;i<=n;++i,x8+=sx,y8+=sy,wq+=dw) {
    const int x=x8>>8,y=y8>>8;
    if(unsigned(x)<unsigned(w_) && unsigned(y)<unsigned(h_))sorcVoidLight(x,y,c,wq>>8,raw,above);
  }
}
// The void's field over the covered rows: nested ellipses round (cx, cy),
// band k covering normalized radius rho[k] down to rho[k+1] in colour
// col[k] (stored). Every row is filled as spans, a root per band, and the
// first rows fade from the background so the header has no edge. At most
// 32 bands.
inline void Renderer::sorcVoidField(int raw,float cx,float cy,float rx,float ry,const float *rho,const Rgb *light,int n,float lum) {
  constexpr int FADE=10;
  // In doubled coordinates (X=2(x-cx), Y=2(y-cy), whole numbers since the
  // centre may fall between pixels), band k holds X^2 < A[k]-Y^2*q[k], with
  // A=4a^2 and q=a^2/b^2 for its radii a, b (at most 400 px). Its edge X[k]
  // steps from the row before (each step a touch): no root.
  uint16_t col[32];float A[32],q[32],X[32];
  const int twiceX=roundi(2*cx),twice=roundi(2*cy),top=std::max(0,raw),parity=twiceX&1;
  for(int k=0;k<n;++k) {
    col[k]=sorcVoidColor(light[k],lum);
    const float a=std::min(400.0f,rx*rho[k]),b=std::max(.5f,std::min(400.0f,ry*rho[k]));
    A[k]=4*a*a;q[k]=a*a/(b*b);X[k]=float(parity-2);
  }
  const unsigned bv=load(bg_);
  int left[32],bands=0;
  // Left edges, outermost first, of the row Y (doubled offset from the
  // centre): band k spans columns left[k]..mirror(left[k]).
  auto edges=[&](int Y) {
    const float y2=float(Y)*float(Y);
    for(bands=0;bands<n;++bands) {
      const float t=A[bands]-y2*q[bands];float &e=X[bands];
      ++touches_;while((e+2)*(e+2)<t){e+=2;++touches_;}
      while(e>=parity && e*e>=t){e-=2;++touches_;}
      if(e<parity)break;
      left[bands]=std::max(0,(twiceX-int(e))>>1);
    }
  };
  auto fill=[&](int y) {
    uint16_t faded[32];const uint16_t *c=col;
    if(y<raw+FADE) {
      // The first rows blend from the background toward the bands.
      const int f8=(y-raw+1)*256/(FADE+1);
      for(int k=0;k<n;++k) {
        const unsigned v=load(col[k]);
        const int r=int(bv>>11)+(((int(v>>11)-int(bv>>11))*f8)>>8),g=int((bv>>5)&63)+(((int((v>>5)&63)-int((bv>>5)&63))*f8)>>8),b=int(bv&31)+(((int(v&31)-int(bv&31))*f8)>>8);
        faded[k]=store(uint16_t(r<<11|g<<5|b));
      }
      c=faded;
    }
    uint16_t *row=px_+y*w_;
    for(int k=0;k<bands;++k) {
      const int a=left[k],mirror=w_-1-a;if(a>mirror)break;
      if(k+1<bands && left[k+1]<=w_-1-left[k+1]){sorcSpan(row,a,left[k+1]-1,c[k]);sorcSpan(row,w_-left[k+1],mirror,c[k]);}
      else sorcSpan(row,a,mirror,c[k]);
    }
  };
  // Rows y and twice-y mirror each other about the centre and share their
  // edges: each pair is computed once.
  for(int y=top;y<h_;++y) {
    const int mirror=twice-y;
    if(mirror<y && mirror>=top)continue;  // done with its pair
    edges(2*y-twice);
    sorcMark(8);
    fill(y);
    if(mirror>y && mirror<h_)fill(mirror);
    sorcMark(9);
  }
}
// Two stylised blue eyes either side of (cx, ey), in covered rows, drawn
// outright: an almond opening (`open` parts the lids), a faint sclera, an
// iris from a dark pupil through white-ice to deep blue at the limbus, a
// highlight, and glowing lid lines. `glow` turns the iris toward white;
// `negative` draws each colour as its black-and-white inverse.
inline void Renderer::sorcEyes(int raw,float cx,float ey,float open,float glow,bool negative) {
  constexpr int W=17,H=8;
  const Rgb sclera=rgb8(26,44,80),limbus=rgb8(30,90,255),mid=rgb8(80,190,255),inner=rgb8(200,240,255),pupil=rgb8(6,14,50),white=rgb8(255,255,255);
  const Rgb lid=rgb8(190,230,255);const float k=.5f+.5f*glow;
  uint16_t c[8]={sorcVoidColor(pupil,1),sorcVoidColor(mixed(inner,white,glow),1),sorcVoidColor(mixed(mid,white,.55f*glow),1),
    sorcVoidColor(mixed(limbus,mid,.5f*glow),1),sorcVoidColor(sclera,.5f+.5f*glow),sorcVoidColor(lid,k),sorcVoidColor(lid,.75f*k),
    sorcVoidColor(white,open)};
  if(negative)for(auto &v:c)v=store(sorcNegate(v));
  auto put=[&](int x,int y,uint16_t v){if(y<raw || y>=h_ || unsigned(x)>=unsigned(w_))return;px_[y*w_+x]=v;++touches_;};
  const int iy=roundi(ey);
  for(int e=-1;e<=1;e+=2) {
    const int ex=roundi(cx+e*24.0f);
    for(int dx=-W;dx<=W;++dx) {
      const float half=open*H*(1-float(dx*dx)/(W*W));
      const int top=-roundi(half*1.1f),bottom=roundi(half*.9f);
      for(int dy=top;dy<=bottom;++dy) {
        // Bands by squared distance from the pupil's centre.
        const int d2=dx*dx+dy*dy;
        put(ex+dx,iy+dy,c[d2<=2?0:d2<=12?1:d2<=30?2:d2<=52?3:4]);
      }
      // The lid lines, the upper one heavier.
      put(ex+dx,iy+top-1,c[5]);put(ex+dx,iy+bottom+1,c[6]);
      if(open>.4f && dx>-W+3 && dx<W-3)put(ex+dx,iy+top-2,c[6]);
    }
    // The highlight, up and to one side of the pupil.
    if(open>.5f)for(int j=0;j<4;++j)put(ex-3+(j&1),iy-3+(j>>1),c[7]);
  }
}
// ---- Boundless, after the first Unlimited Void, over the whole screen:
// the screen below the readout goes dark and a pair of blue eyes opens in
// it, glowing brighter with rays and a long flare; then everything flips to
// a black-and-white negative (the eyes dark in it); a white splotch bursts
// out from between the eyes and floods the screen, black ink blots bloom
// through it and swallow it, and the void opens: a bright point at the
// centre of a dark field, ringed by a blinding white-blue rim, light
// streaming out of it toward the viewer, ripples running out to the rim and
// streams of information falling in. Sustain: the rim breathes, knots of
// light run round it, the ripples and the streams keep coming. Then the void
// draws back into its centre, the recap reappearing from the edges in, and
// the last stars fade.
// From the start to the close the effect covers every row below the
// readout's halo (covers()), writing those rows outright; the readout keeps
// its own pixels and outline. On a light theme every tone is reversed: the
// eyes and the void are dark on the background, the splotch ink-black and
// the ink white.
inline void Renderer::sorcBoundless(float t) {
  constexpr float BURST=.45f,SPLAT=.55f,FLOOD=.17f,HOLES=.74f,GONE=1.08f,VOID=1.08f,SHUT=.5f,END=BOUNDLESS_COVER*.001f,CLOSE=END-SHUT,TAIL=.85f;
  const Box &b=number_;
  const int raw=std::min(h_,b.y1+HALO+1);
  const float cx=(w_-1)*.5f,cy=(raw+h_-1)*.5f;
  const bool light=sorcery::lightBackground(load(bg_));
  const Rgb ice=rgb8(190,230,255),blue=rgb8(70,140,255),white=rgb8(245,250,255);
  // The eyes: the screen below the readout goes dark (every other row for
  // the first two frames, like a shutter), and a pair of blue eyes opens in
  // the middle of it, the irises glowing brighter, rays and a long flare
  // growing out of them until the flash.
  const float ey=cy-8;
  if(t<BURST) {
    for(int y=raw;y<h_;++y)if((y&1) || t>=.1f)sorcSpan(px_+y*w_,0,w_-1,bg_);
    sorcMark(2);
    const float open=easeOut((t-.08f)/.22f),glow=smooth((t-.15f)/.3f);
    if(open>0)sorcEyes(raw,cx,ey,open,glow,false);
    const float k=smooth((t-.22f)/.2f);
    if(k>0) {
      // The flare: a level line of light through both eyes toward the edges.
      const Ink flare=inkOf(mixed(ice,white,.5f));
      for(int side=-1;side<=1;side+=2) {
        const float ex=cx+side*24.0f;
        sorcVoidLine(ex+side*18,ey,ex+side*(18+40*k),ey,flare,q8(.9f*k),0,raw);
        // Rays round each eye, longest up and down.
        for(int r=0;r<6;++r) {
          const float ang=r*(HALF_TURN/3)+HALF_TURN/6,ca=cosine(ang),sa=sine(ang),len=(10+8*std::fabs(sa))*k;
          sorcVoidLine(ex+12*ca,ey+9*sa,ex+(12+len)*ca,ey+(9+len)*sa,inkOf(ice),q8(.7f*k),0,raw);
        }
      }
      sorcVoidLight(roundi(cx),roundi(ey),inkOf(white),q8(.6f*k),raw);
      sorcVoidLine(cx-6,ey,cx+6,ey,flare,q8(.6f*k),q8(.6f*k),raw);
    }
    sorcMark(3);
    return;
  }
  // The negative: the whole screen inverted to black and white for a beat,
  // the eyes dark in it (around the readout only the background flips, so
  // it keeps its outline).
  if(t<SPLAT) {
    const SorcDisc all{cx,cy,1000};
    sorcSplotch(&all,1,nullptr,0,0,h_-1,sorcNegate(bg_),raw,-1);
    sorcEyes(raw,cx,ey,1,1,true);
    sorcMark(3);
    return;
  }
  glint(t,VOID,.45f);
  sorcMark(1);
  // The splotch floods out from the sign over the dark, then holes of ink
  // bloom through it until nothing is left.
  if(t<GONE) {
    const float g=.3f+.7f*easeOut((t-SPLAT)/FLOOD);
    SorcDisc inks[30],holes[34];int ni=0,nh=0;
    inks[ni++]={cx,cy,40*g};inks[ni++]={cx-22*g,cy-36*g,26*g};inks[ni++]={cx+20*g,cy+38*g,28*g};
    // A ragged edge: arms of lobes thrown out from the middle, each
    // thinning to a drop, and spatter flung past them.
    for(int k=0;k<9;++k) {
      const float ang=k*(2*HALF_TURN/9)+.5f*rnd(k,381),ca=cosine(ang),sa=sine(ang),len=1+.5f*rnd(k,382);
      for(int j=0;j<2;++j) {
        const float d=(38+24*j)*len*g;
        inks[ni++]={cx+d*ca,cy+1.45f*d*sa,(14-5.5f*j)*(.7f+.6f*rnd(k*3+j,384))*g};
      }
    }
    const float thrown=smooth((t-SPLAT)/.05f);
    for(int k=0;k<7;++k) {
      const float ang=2*HALF_TURN*rnd(k,385),dist=(80+40*rnd(k,386))*g;
      inks[ni++]={cx+dist*cosine(ang),cy+dist*1.5f*sine(ang),(1.5f+3*rnd(k,387))*thrown};
    }
    // The white takes the rest of the screen (kept last).
    inks[ni++]={cx,cy,300*smooth((t-SPLAT-.09f)/.1f)};
    // Ink blooms through it, blot by blot across the screen, each with drops.
    for(int k=0;k<12;++k) {
      const float a=(t-HOLES-.015f*k)/.28f;if(a<=0)continue;
      const float r=(18+18*rnd(k,391))*easeOut(a),x=(k%4+rnd(k,392))*(w_/4.0f),y=(k/4+rnd(k,393))*(h_/3.0f);
      holes[nh++]={x,y,r};
      const float ang=2*HALF_TURN*rnd(k*3,394),out=1.15f+.5f*rnd(k*3,395);
      holes[nh++]={x+out*r*cosine(ang),y+out*r*sine(ang),(.2f+.22f*rnd(k*3,396))*r};
    }
    // The last of the white goes as the void opens.
    const float last=smooth((t-HOLES-.18f)/(GONE-HOLES-.18f));
    if(last>0)holes[nh++]={cx,cy,300*last};
    // Discs past the screen's reach (170 px from the centre covers it all)
    // hide every other: the flood leaves only itself, the last hole nothing.
    if(inks[ni-1].r>=170){inks[0]=inks[ni-1];ni=1;}
    if(300*last>=170)ni=nh=0;
    const uint16_t splotch=light?uint16_t(0x10a2):uint16_t(0xf7bf);
    sorcMark(5);
    // Once the ink starts, the header is the readout on its own background
    // again (the first blots land there), so only covered rows are drawn:
    // the header's mask costs about 2 ms a frame on the device.
    sorcSplotch(inks,ni,holes,nh,t<HOLES?0:raw,h_-1,splotch,raw,bg_);
    return;
  }
  // The void, from when the ink has eaten the white: the field opens out of
  // the centre, breathes through the sustain, then draws back into it.
  const float a=t-VOID,open=easeOut(a/.35f),closing=smooth((t-CLOSE)/SHUT);
  const float travel=std::min(t,END)-VOID+std::max(0.0f,t-END)*.4f,fade=1-smooth((t-END)/TAIL);
  const float grow=open*(1+.025f*sine(t*1.7f))*(1-.96f*closing),rx=52*grow,ry=std::min(80.0f,(h_-raw)*.42f)*grow;
  if(t<END) {
    const float lum=(1+.45f*(1-smooth(a/.3f)))*(1.05f+.1f*sine(t*2.4f));
    // Bands outermost first: the dark that covers the recap (it shrinks to
    // the centre as the void closes), the outer glow, the rim's white core,
    // the inner glow, the dark heart, and the bright point at its centre.
    // Few, broad bands: on the device every band edge costs two span fills a
    // row (1.2 us each).
    static constexpr int BANDS=9;
    static constexpr float RHO[BANDS]={1,1.15f,1.035f,1,.955f,.88f,.75f,.13f,.045f};
    static constexpr Rgb LIGHT[BANDS]={{0,0,0},rgb8(6,16,50),rgb8(110,180,255),rgb8(245,250,255),rgb8(45,95,215),
      rgb8(14,32,105),rgb8(3,8,34),rgb8(30,70,175),rgb8(240,248,255)};
    float rho[BANDS+1];Rgb shade[BANDS+1];int n=0;
    // The cover reaches past every corner, and shrinks ahead of the rim as
    // the void closes, giving the recap back from the edges in.
    float cover=(1.5f*rx+110*(1-closing))/std::max(rx,.5f);
    // As it closes, the cover's edge is a ring of ice light: an iris.
    if(closing>0){const float s=sine(HALF_TURN*closing);rho[n]=cover;shade[n]={ice.r*s,ice.g*s,ice.b*s};++n;cover-=3/std::max(rx,.5f);}
    for(int k=0;k<BANDS;++k){rho[n]=k?RHO[k]:cover;shade[n]=LIGHT[k];++n;}
    sorcMark(10);
    sorcVoidField(raw,cx,cy,std::max(rx,.5f),std::max(ry,.5f),rho,shade,n,lum*(1-.6f*closing));
    // Ripples run out of the centre to the rim, brightening as they near:
    // rings of points, since a band would cost two span fills on every row
    // it crosses.
    for(int i=0;i<2;++i) {
      const float u=t*.5f+i*.5f,phase=u-floori(u),r=.12f+.7f*phase,k=sine(HALF_TURN*phase)*(.35f+.65f*phase)*(1-closing);
      if(k<.05f)continue;
      const int points=int(20+44*r);const Ink c=inkOf(rgb8(60,120,255));
      for(int j=0;j<points;++j) {
        const float ang=j*(2*HALF_TURN)/points+i;
        sorcVoidLight(roundi(cx+rx*r*cosine(ang)),roundi(cy+ry*r*sine(ang)),c,q8(k),raw,false);
      }
    }
    // Knots of light run round the rim.
    for(int k=0;k<5;++k) {
      const float base=k*(2*HALF_TURN/5)+t*(k&1?1.3f:-1.1f);
      for(int j=-4;j<=4;++j) {
        const float ang=base+j*.045f,w=(1-std::fabs(float(j))/5)*(1-closing);
        sorcVoidLight(roundi(cx+rx*cosine(ang)),roundi(cy+ry*sine(ang)),WHITE_INK,q8(.9f*w),raw);
      }
    }
    // The centre flares, a small cross of light that pulses.
    const float flare=(.6f+.4f*sine(t*5.1f))*open;const int ix=roundi(cx),iy=roundi(cy);
    for(int d=2;d<=6;++d){const int k=q8(flare*(1-d/7.0f));
      sorcVoidLight(ix-d,iy,WHITE_INK,k,raw);sorcVoidLight(ix+d,iy,WHITE_INK,k,raw);sorcVoidLight(ix,iy-d,WHITE_INK,k,raw);sorcVoidLight(ix,iy+d,WHITE_INK,k,raw);}
    // Streams of information fall in from beyond the rim, a row of bits
    // each, shrinking into the distance as they near the centre.
    for(unsigned j=0;j<6;++j) {
      const float life=.85f+.3f*rnd(j,401),phase=rnd(j,402)+travel/life;
      const int cycle=floori(phase);const float u=phase-cycle;const uint32_t seed=j*31+uint32_t(cycle);
      if(cycle<0 || (a<.3f && u>a/.3f))continue;
      const float ang=2*HALF_TURN*rnd(seed,403),ca=cosine(ang),sa=sine(ang);
      const float head=.1f+1.5f*(1-u)*(1-u),tail=head*1.3f+.06f;
      const float k=smooth(u/.12f)*(1-smooth((u-.8f)/.2f))*(1-closing);if(k<.05f)continue;
      const float hx=cx+rx*ca*head,hy=cy+ry*sa*head,tx=cx+rx*ca*tail,ty=cy+ry*sa*tail;
      const int steps=std::max(1,int(std::max(std::fabs(tx-hx),std::fabs(ty-hy))));
      const Ink c=inkOf(mixed(ice,accent_,.35f));
      for(int s=0;s<=steps;++s) {
        if(lattice(s/2,int(seed),404)<100)continue;
        const float f=float(s)/steps;
        sorcVoidLight(roundi(hx+(tx-hx)*f),roundi(hy+(ty-hy)*f),c,q8(k*(1-.6f*f)),raw,false);
      }
    }
  }
  sorcMark(10);
  // Light streams out of the bright centre toward the viewer: each star
  // leaves at its own angle and speeds up as it comes, a streak behind it;
  // past the close the last of them drift out over the recap and fade.
  // While the void is open they stop at its top, where it fades into the
  // header (the header's mask would cost more than they show).
  const int cut=t<END?raw:h_;const bool above=t>=END;
  const float sr=std::max(rx,8.0f),sry=std::max(ry,12.0f);
  // Far stars twinkle in the dark, deep in the void.
  if(t<END)for(unsigned i=0;i<16;++i) {
    const float ang=2*HALF_TURN*rnd(i,405),d=.25f+.6f*rnd(i,406);
    const float k=open*(1-closing)*(.25f+.3f*sine(t*(2+2.5f*rnd(i,407))+7*rnd(i,408)));
    sorcVoidLight(roundi(cx+rx*d*cosine(ang)),roundi(cy+ry*d*sine(ang)),inkOf(i%3?ice:white),q8(k),raw);
  }
  for(unsigned i=0;i<22;++i) {
    const float life=.7f+.5f*rnd(i,371),phase=rnd(i,372)+travel/life;
    const int cycle=floori(phase);const float u=phase-cycle;const uint32_t seed=i*53+uint32_t(cycle);
    if(cycle<0 || (a<.3f && u>a/.3f))continue;
    const float ang=2*HALF_TURN*rnd(seed,373),ca=cosine(ang),sa=sine(ang);
    const float d0=.06f+1.9f*u*u*u,du=std::max(0.0f,u-.11f),d1=.06f+1.9f*du*du*du;
    const float k=smooth(u/.2f)*(.35f+.65f*u)*fade;if(k<.04f)continue;
    const float x=cx+sr*ca*d0,y=cy+sry*sa*d0,px=cx+sr*ca*d1,py=cy+sry*sa*d1;
    const Rgb c=(seed%3)?mixed(ice,white,.4f):blue;
    sorcVoidLine(px,py,x,y,inkOf(c),q8(.25f*k),q8(k),cut,above);
    sorcVoidLight(roundi(x),roundi(y),WHITE_INK,q8(k),cut,above);
    // Near ones come close enough to show a body.
    if(u>.55f){sorcVoidLight(roundi(x)+1,roundi(y),inkOf(c),q8(k*.8f),cut,above);sorcVoidLight(roundi(x),roundi(y)+1,inkOf(c),q8(k*.8f),cut,above);}
  }
  // The void shuts on a last point of light where its centre was.
  if(t>=END-.05f && t<END+.2f)spark(cx,cy,white,1.3f*(1-std::fabs(t-END-.03f)/.2f),2.5f);
  sorcMark(11);
}

// ---- Precision, after the 7:3 ratio cut: rulers draw out above
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

// ---- Umbra, after Chimera Shadow Garden: liquid shadow pools
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

// ---- Ravage, after Malevolent Shrine: a shrine's roof looms in
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
