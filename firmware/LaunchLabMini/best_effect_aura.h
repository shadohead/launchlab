#pragma once
// The aura set of 0.18 (Haymaker..Waterleaf): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Same rules as
// the other sets: additive light on the background outside the UI halo, a
// pure function of elapsed time with fixed seeds, the readout exactly as
// drawn from three quarters of the duration, every frame within the cost
// model's 8 ms. Touching the readout: Elastic stretches its lower half
// taller and jiggles it (only changed pixels written), exact from 2.0 s;
// Haymaker brightens the glyphs in its lower rows round the contact and
// stops by 1.4 s; every effect glints the header at its impact. The
// subjects are pixel-art shapes and filled strokes, centred under the
// readout or in the screen's side strips, so they read across the
// replay's frame.
namespace BestEffectUI {

inline void Renderer::aura(unsigned which,float t) {
  switch(which) {
    case 0:auraHaymaker(t);break;
    case 1:auraPalms(t);break;
    case 2:auraElastic(t);break;
    case 3:auraChains(t);break;
    case 4:auraWaterleaf(t);break;
    default:break;
  }
}

// An open hand, palm toward the viewer, thumb on the left (Palms, Waterleaf).
constexpr int AURA_HAND_W=14,AURA_HAND_H=14;
constexpr const char *AURA_HAND[AURA_HAND_H]={
    "......##......",
    "......##.##...",
    "...##.##.##...",
    "...##.##.##.##",
    "...##.##.##.##",
    "#..##.##.##.##",
    "##.##.##.##.##",
    ".##.##########",
    "..############",
    "..############",
    "..############",
    "...##########.",
    "....#########.",
    ".....#######.."};
// A sprite as row bit masks, built at compile time for scales 1 and 2 and
// both mirrors: its set pixels and its outline (set pixels with a clear
// 4-neighbour, judged on the scaled grid), so drawing only tests bits.
struct AuraMask {uint32_t on[2][2][32],rim[2][2][32];};
constexpr AuraMask auraMask(const char *const *rows,int w,int h) {
  AuraMask m{};
  for(int s=1;s<=2;++s)for(int mi=0;mi<2;++mi) {
    const int W=w*s,H=h*s;
    auto set=[&](int x,int y){if(x<0 || y<0 || x>=W || y>=H)return false;const int c=x/s;return rows[y/s][mi?w-1-c:c]=='#';};
    for(int y=0;y<H;++y)for(int x=0;x<W;++x) {
      if(!set(x,y))continue;
      m.on[s-1][mi][y]|=1u<<x;
      if(!set(x-1,y) || !set(x+1,y) || !set(x,y-1) || !set(x,y+1))m.rim[s-1][mi][y]|=1u<<x;
    }
  }
  return m;
}
constexpr AuraMask AURA_HAND_MASK=auraMask(AURA_HAND,AURA_HAND_W,AURA_HAND_H);
// sqrt(1-i/256) for i in 0..256, built at compile time by Newton steps:
// the half-ellipse profile of the glass's base and the water's dome
// without a square root per column on the device.
constexpr float auraSqrtC(float v){if(v<=0)return 0;float r=v>1?v:1;for(int i=0;i<24;++i)r=.5f*(r+v/r);return r;}
constexpr std::array<float,257> auraArcTable(){std::array<float,257> a{};for(int i=0;i<=256;++i)a[i]=auraSqrtC(1-i/256.0f);return a;}
constexpr std::array<float,257> AURA_ARC=auraArcTable();
// sqrt(1-v*v) for |v|<=1 (0 outside), from the table.
inline float auraArc(float v){const float q=v*v*256;if(q>=256)return 0;const int i=int(q);return AURA_ARC[i]+(AURA_ARC[i+1]-AURA_ARC[i])*(q-i);}
// 1/sqrt(v) for v>0 by the bit trick and two Newton steps: multiplies only.
inline float auraRsqrt(float v){uint32_t i;std::memcpy(&i,&v,4);i=0x5f3759dfu-(i>>1);float y;std::memcpy(&y,&i,4);y*=1.5f-.5f*v*y*y;y*=1.5f-.5f*v*y*y;return y;}
// x modulo a period whose reciprocal is given, without fmodf.
inline float auraWrap(float x,float period,float inv){const float q=x*inv;const int n=int(q)-(q<int(q));return x-n*period;}
// Pen-up marker in the outline tables below.
constexpr float AURA_PEN=1e3f;
// A table of outline points (x,y pairs, AURA_PEN AURA_PEN between strokes),
// scaled by sx,sy and placed at ox,oy, stroked with fixed-point lines.
inline void Renderer::auraPath(const float *xy,int n,float ox,float oy,float sx,float sy,const Ink &c,int kq) {
  if(kq<5)return;
  bool pen=false;float px=0,py=0;
  for(int i=0;i+1<n;i+=2) {
    if(xy[i]>=AURA_PEN){pen=false;continue;}
    const float x=ox+xy[i]*sx,y=oy+xy[i+1]*sy;
    if(pen)sorcLine(px,py,x,y,c,kq,kq);
    px=x;py=y;pen=true;
  }
}
// A flickering aura outline round an ellipse: its radius wanders with value
// noise sampled on a circle (so the outline closes without a seam), and the
// upper half reaches up in tongues that scroll upward with `scroll`.
inline void Renderer::auraFlame(float cx,float cy,float rx,float ry,float rise,int scroll,uint32_t salt,const Ink &c,int kq) {
  if(kq<5)return;
  constexpr int N=60;float px=0,py=0;
  for(int i=0;i<=N;++i) {
    const float a=(i%N)*(2*HALF_TURN/N),ca=cosine(a),sa=sine(a),up=sa<0?-sa:0;
    const int n=noise(int((3+2.2f*ca)*256),int((3+2.2f*sa)*256)+scroll,salt);
    const float wobble=1+(n-128)*(.22f/128),tongue=up*up*rise*n*(1/255.0f);
    const float x=cx+rx*ca*wobble,y=cy+ry*sa*wobble-ry*tongue;
    if(i)sorcLine(px,py,x,y,c,kq,kq);
    px=x;py=y;
  }
}
// Pulls the readout's glyph pixels inside an ellipse away from the
// background (sorcHeat: brighter on a dark theme, darker on a light one),
// strongest at its centre.
inline void Renderer::auraHeatSpot(float cx,float cy,float rx,float ry,const Ink &target,int k8) {
  if(k8<=0)return;
  const Box &b=number_;
  const int y0=std::max(b.y0,int(cy-ry)),y1=std::min(b.y1,int(cy+ry)+1);
  for(int y=y0;y<=y1;++y) {
    const float dy=(y-cy)/ry,rest=1-dy*dy;if(rest<=0)continue;
    const float span=rx*std::sqrt(rest);const int kr=int(k8*rest);
    const int x0=std::max(b.x0,int(cx-span)),x1=std::min(b.x1,int(cx+span)+1);
    // Weight falls off across the row as (1-(dx/span)^2), in fixed point.
    const int c8=q8(cx),inv=int(65536/(span*span+1));
    for(int x=x0;x<=x1;++x) {
      if(px_[y*w_+x]==bg_)continue;
      const int dx=(x*256-c8)>>4,f=65536-((dx*dx*inv)>>8);
      if(f>0)sorcHeat(x,y,target,(kr*(f>>8))>>8);
    }
  }
}

// A sprite (see AuraMask) centred at cx,cy, at scale 1 or 2, optionally
// mirrored left to right. Pixels on its outline take `rim`, the rest `body`
// (kBody under 5 draws the outline alone). Rows and columns are walked as
// bits, so a pixel costs a bit test, not a lookup of its neighbours.
inline void Renderer::auraSprite(const AuraMask &m,int w,int h,float cx,float cy,int scale,bool mirror,const Ink &rim,const Ink &body,int kRim,int kBody) {
  if(kRim<5 && kBody<5)return;
  const int s=scale>1?1:0,W=w<<s,H=h<<s,x0=roundi(cx-W*.5f),y0=roundi(cy-H*.5f);
  const uint32_t *on=m.on[s][mirror],*edge=m.rim[s][mirror];
  // Columns left of the screen or right of it are masked off once.
  uint32_t clip=~0u;
  if(x0<0)clip&=~0u<<std::min(31,-x0);
  if(x0+W>w_)clip&=(w_-x0)>=32?~0u:(1u<<std::max(0,w_-x0))-1;
  for(int r=0;r<H;++r) {
    const int y=y0+r;if(unsigned(y)>unsigned(bottom_))continue;
    const uint32_t e=edge[r]&clip;uint32_t bits=(kBody>=5?on[r]:e)&clip;
    while(bits) {
      const int c=__builtin_ctz(bits);const uint32_t bit=bits&(0u-bits);bits^=bit;
      if(e&bit)addw(x0+c,y,rim,kRim);else addw(x0+c,y,body,kBody);
    }
  }
}

// Stretches the lower half of the readout down by d px like pulled gum
// (squashes it when d is negative), row by row in place: each row below
// the middle takes the row its stretched position maps back to, and rows
// left below a squashed readout take the background. The halo is built
// from the result, so call it before any light is drawn. Only pixels that
// change are written: glyphs are mostly vertical strokes in one colour.
inline void Renderer::auraStretchReadout(float d) {
  const Box &b=number_;
  const int mid=(b.y0+b.y1)/2,h=b.y1-mid,dd=roundi(d);
  if(!dd || h<2)return;
  const int last=std::min(h_-1,b.y1+std::max(0,dd));
  auto source=[&](int y){const int s=mid+roundi((y-mid)*float(h)/(h+d));return s<=b.y1?s:-1;};
  auto copy=[&](int y){
    const int s=source(y);uint16_t *row=px_+y*w_;const uint16_t *src=px_+s*w_;
    for(int x=b.x0;x<=b.x1;++x){const uint16_t v=s>=0?src[x]:bg_;if(row[x]!=v){row[x]=v;++plots_;}}
  };
  // Stretching reads rows above the one written, so it runs bottom up;
  // squashing reads below, so it runs top down.
  if(dd>0)for(int y=last;y>mid;--y)copy(y);
  else for(int y=mid+1;y<=b.y1;++y)copy(y);
}
// A solid strand of gum hanging from ax,ay (above) to ex,ey (below), filled
// row by row: `bow` bends it sideways in the middle, `hum` is a sideways
// standing wave, and its width runs w0 at the top, wMid in the middle and
// w1 at the bottom (pulled gum thins in the middle). Each row's span is
// shaded across: a pale highlight on its upper-facing side, a deep pink
// underside, so it reads as a round, glossy strand.
inline void Renderer::auraGum(float ax,float ay,float ex,float ey,float bow,float hum,float w0,float wMid,float w1,int kq) {
  static constexpr Ink core=inkOf(rgb8(255,92,180)),hi=inkOf(rgb8(255,206,236)),shade=inkOf(rgb8(176,22,112));
  if(kq<5 || ey<ay+1)return;
  const float dx=ex-ax,dy=ey-ay,len=std::sqrt(dx*dx+dy*dy),stretchX=len/dy;
  // The side facing up: for a strand leaning left going down, its right.
  const bool upRight=dx<0;
  const int y0=std::max(0,roundi(ay)),y1=std::min(bottom_,roundi(ey));
  for(int y=y0;y<=y1;++y) {
    const float u=(y-ay)/dy,m=1-std::fabs(2*u-1);
    const float w=(u<.5f?w0+(wMid-w0)*m:w1+(wMid-w1)*m)*std::min(stretchX,2.5f);
    const float x=ax+dx*u+(bow+hum)*4*u*(1-u),l=x-w*.5f;
    const int xa=roundi(l),xb=roundi(l+w)-1;
    if(xb<xa){addw(roundi(x),y,core,kq);continue;}
    for(int xx=std::max(0,xa);xx<=std::min(w_-1,xb);++xx) {
      const int from=upRight?xb-xx:xx-xa;  // 0 at the upper-facing edge
      const Ink &c=xb-xa<2?core:from==0?hi:xx==(upRight?xa:xb)?shade:core;
      addw(xx,y,c,kq);
    }
  }
}
// A glossy blob of gum: a filled ellipse, its upper left lit.
inline void Renderer::auraBlob(float cx,float cy,float rx,float ry,int kq) {
  static constexpr Ink core=inkOf(rgb8(255,92,180)),hi=inkOf(rgb8(255,206,236)),shade=inkOf(rgb8(176,22,112));
  if(kq<5)return;
  for(int y=floori(cy-ry);y<=roundi(cy+ry);++y) {
    const float v=(y-cy)/ry;if(v*v>=1)continue;
    const float span=rx*std::sqrt(1-v*v);
    for(int x=roundi(cx-span);x<=roundi(cx+span);++x) {
      if(unsigned(x)>=unsigned(w_) || unsigned(y)>unsigned(bottom_))continue;
      const float h=(x-cx)/rx-v;  // the lit side is up and to the left
      addw(x,y,h<-.7f?hi:h>.75f?shade:core,kq);
    }
  }
}

// One chain link centred at x,y: an open oval (7x5 along a horizontal run,
// 3x7 along a vertical one) or one seen edge-on (a bar through its
// neighbours), a highlight on its upper or left edge.
inline void Renderer::auraLink(float x,float y,bool vertical,bool open,const Ink &c,const Ink &hi,int kq) {
  if(kq<5)return;
  const int cx=roundi(x),cy=roundi(y);
  auto put=[&](int px,int py,const Ink &ink,int k){if(unsigned(px)<unsigned(w_) && unsigned(py)<=unsigned(bottom_))addw(px,py,ink,k);};
  if(!vertical) {
    if(open) {
      for(int d=-2;d<=2;++d){put(cx+d,cy-2,hi,kq);put(cx+d,cy+2,c,(kq*3)>>2);}
      for(int d=-1;d<=1;++d){put(cx-3,cy+d,c,kq);put(cx+3,cy+d,c,kq);}
    } else {
      for(int d=-3;d<=3;++d)put(cx+d,cy,d<0?hi:c,kq);
    }
  } else {
    if(open) {
      put(cx,cy-3,hi,kq);put(cx,cy+3,c,kq);
      for(int d=-2;d<=2;++d){put(cx-1,cy+d,hi,kq);put(cx+1,cy+d,c,(kq*3)>>2);}
    } else {
      for(int d=-3;d<=3;++d)put(cx,cy+d,d<0?hi:c,kq);
    }
  }
}

// ---- Haymaker: aura gathers into a fist below the readout over three
// counted beats (each flares its aura, blooms its outline, pulls a ring of
// light in onto it and lights a pip of the power gauge either side); it
// cocks, then drives straight up into the readout's foot and bursts there:
// a flash, rays fanning along the foot, two shock rings, sparks, and the
// digits round the contact flashing white-hot. Sustain: the fist sinks back
// and its aura burns on lower, tongues licking up and embers rising off it,
// while the struck spot under the readout cools. Settle: it burns away.
inline void Renderer::auraHaymaker(float t) {
  static constexpr float BEAT[3]={.14f,.44f,.74f};
  constexpr float COCK=.84f,PUNCH=1.0f,HIT=1.07f,HOLD=1.2f,BACK=1.85f,FADE=3.75f,END=4.85f;
  // The fist from the front, knuckles up: four fingers with their creases,
  // the thumb folded across them, the heel of the hand and the wrist.
  // 'o' rim, '#' body, '+' a highlight, '-' a crease (left dark).
  static constexpr int FW=31,FH=25;
  static const char *const FIST[FH]={
    "..........oooo...oooo..........",
    ".........o#++#o.o#++#o.........",
    "...oooo..o####o.o####o..oooo...",
    "..o#++#o.o####o.o####o.o#++#o..",
    "..o####o.o####o.o####o.o####o..",
    "..o#--#o.o#--#o.o#--#o.o#--#o..",
    "..o####o.o####o.o####o.o####o..",
    "..o####o.o####o.o####o.o####o..",
    "..o#----------------#o.o####o..",
    "..o#++++++++++++++--#o.o####o..",
    "..o#################-#######o..",
    "..o#################-#######o..",
    "..o################-########o..",
    "..o#---------------#########o..",
    "..o#########################o..",
    "..o#########################o..",
    "...o#######################o...",
    "....oo###################oo....",
    "......o#################o......",
    "......o#################o......",
    "......o#################o......",
    "......o#################o......",
    "......o#################o......",
    "......o#################o......",
    "......o#################o......"};
  const Box &b=number_;const float cx=b.cx();
  const Rgb hot=rgb8(255,246,214),gold=rgb8(255,204,64),orange=mixed(rgb8(255,128,24),accent_,.2f),red=rgb8(236,84,18);
  int beats=0;float flare=0;
  for(int i=0;i<3;++i)if(t>=BEAT[i]){beats=i+1;flare=std::max(flare,1-smooth((t-BEAT[i])/.28f));}
  // The fist's top row over time: rest, cock down, drive up, hold, sink back.
  const float restTop=b.y1+21.0f,hitTop=b.y1+3.0f;
  auto fistTop=[&](float at){
    if(at<COCK)return restTop;
    if(at<PUNCH)return restTop+4*smooth((at-COCK)/(PUNCH-COCK));
    if(at<HIT){const float u=(at-PUNCH)/(HIT-PUNCH);return restTop+4+(hitTop-restTop-4)*u*u;}
    if(at<HOLD)return hitTop+1.5f*sine((at-HIT)*(HALF_TURN/(HOLD-HIT)));
    return hitTop+(restTop-hitTop)*smooth((at-HOLD)/(BACK-HOLD));
  };
  const float top=fistTop(t),fcy=top+11;
  const float presence=smooth(t/.12f)*(1-smooth((t-FADE)/(END-FADE)));
  // Aura power: a step per beat, a surge as it cocks, a blaze at the hit,
  // then a lower burn that breathes.
  float power=.35f+.2f*beats+.45f*flare;
  if(t>=COCK && t<HIT)power+=.35f*smooth((t-COCK)/(PUNCH-COCK));
  if(t>=HIT)power=.95f+.35f*(1-smooth((t-HIT)/.7f))+.12f*sine(t*5.3f)+.06f*sine(t*13.1f);
  power*=presence;
  // The glint sweeps the header as the struck digits cool.
  glint(t,HIT+.33f,.42f);
  // The aura: flame tongues licking out of an ellipse round the fist and
  // bending upward, gold at the root, orange then red at the tip.
  // In the blast (0.36 s from the hit) the aura is spent in the burst.
  const bool blast=t>=HIT && t<HIT+.36f;
  if(power>.02f && !blast) {
    const float rx=17.5f+1.5f*power,ry=15+1.5f*power;
    const int scroll=int(t*1100);
    ellipseAt(cx,fcy,rx,ry,gold,.45f*std::min(1.0f,power),1.4f);
    for(int j=0;j<40;++j) {
      const float a=j*(2*HALF_TURN/40)+.05f*sine(t*3+j),ca=cosine(a),sa=sine(a),up=(1-sa)*.5f;
      const int n=noise(j*170,scroll,503),m=noise(j*170+40000,scroll,504);
      const float len=(2.5f+9*n*(1/255.0f))*(.45f+.75f*up)*power*(.8f+.4f*flare);if(len<1.2f)continue;
      float dx=ca,dy=sa-1.3f;const float inv=1/std::sqrt(dx*dx+dy*dy);dx*=inv;dy*=inv;
      const float bend=(m-128)*(2.5f/128),x0=cx+rx*ca,y0=fcy+ry*sa;
      const float mx=x0+dx*len*.5f-dy*bend*.5f,my=y0+dy*len*.5f+dx*bend*.5f,tx=x0+dx*len-dy*bend,ty=y0+dy*len+dx*bend;
      const int k=q8(std::min(1.2f,.9f*power));
      sorcLine(x0,y0,mx,my,inkOf(mixed(gold,orange,.3f)),k,(k*3)>>2);
      sorcLine(mx,my,tx,ty,inkOf(n>170?orange:red),(k*3)>>2,k>>3);
    }
  }
  // The fist, with two after-images trailing it on the drive, and a bloom
  // of its outline on each beat and at the hit.
  if(presence>.01f) {
    const float bloom=std::max(flare,t>=HIT && t<HIT+.3f?1-(t-HIT)/.3f:0.0f);
    auto stamp=[&](float y0,float k,bool full){
      const Ink rim=inkOf(mixed(hot,gold,.1f)),shine=inkOf(hot);
      const int left=roundi(cx-FW*.5f),ty=roundi(y0);
      for(int r=0;r<FH;++r) {
        const float rowK=k*(r<17?1:1-(r-16)*(1/9.0f));
        const Ink body=inkOf(mixed(mixed(gold,hot,.15f),orange,.1f+.55f*r/FH));
        const int kRim=q8(rowK),kBody=q8(rowK*.78f),kShine=q8(rowK*.95f);
        for(int c=0;c<FW;++c) {
          const char ch=FIST[r][c];if(ch=='.' || ch=='-')continue;
          if(!full && ch!='o')continue;
          const int x=left+c,y=ty+r;
          if(unsigned(x)>=unsigned(w_) || unsigned(y)>unsigned(bottom_))continue;
          addw(x,y,ch=='o'?rim:ch=='+'?shine:body,ch=='o'?kRim:ch=='+'?kShine:kBody);
        }
      }
    };
    if(t>=PUNCH && t<HIT+.06f)for(int g=2;g>=1;--g)stamp(fistTop(t-.025f*g),presence*(g==1?.45f:.22f),false);
    if(bloom>.05f) {
      // The outline one pixel out on each side, fading.
      const Ink c=inkOf(mixed(gold,hot,.5f));const int k=q8(.55f*bloom*presence),left=roundi(cx-FW*.5f),ty=roundi(top);
      for(int r=0;r<FH-6;++r)for(int col=0;col<FW;++col) {
        if(FIST[r][col]!='o')continue;
        if(col==0 || FIST[r][col-1]=='.')addw(left+col-1,ty+r,c,k);
        if(col+1==FW || FIST[r][col+1]=='.')addw(left+col+1,ty+r,c,k);
        if(r==0 || FIST[r-1][col]=='.')addw(left+col,ty+r-1,c,k);
      }
    }
    stamp(top,presence,true);
  }
  // The gauge: a pip either side lights on each beat and pops; all go out
  // as the fist cocks.
  if(t<COCK+.08f)for(int i=0;i<beats;++i) {
    const float age=t-BEAT[i],k=(1+.9f*(1-smooth(age/.18f)))*(1-smooth((t-COCK)/.08f))*presence;
    for(int side=-1;side<=1;side+=2)twinkle(cx+side*31.0f,restTop+19-7.0f*i,mixed(gold,hot,.4f),k*.85f,1+(age<.12f));
  }
  // Before each beat a ring of light closes in onto the fist.
  for(int i=0;i<3;++i) {
    const float age=t-BEAT[i]+.2f,life=.2f;if(age<0 || age>=life)continue;
    const float e=easeOut(age/life),r=50-30*e;
    ellipseAt(cx,restTop+11,r,r*.8f,mixed(orange,gold,e),(.3f+.7f*e)*presence,1.6f);
  }
  // The drive: speed lines streaming below the fist.
  if(t>=PUNCH && t<HIT+.12f) {
    const float k=t<HIT?1:1-(t-HIT)/.12f;
    for(int i=0;i<7;++i) {
      const float x=cx+(i-3)*4.2f,len=(16+14*rnd(i,511))*(t<HIT?1:1-4*(t-HIT)),y0=top+FH+2*rnd(i,512)-(i==0 || i==6?6:0);
      sorcLine(x,y0,x,y0+len,inkOf(mixed(hot,orange,.4f)),q8(k),0);
    }
  }
  const float u=t-HIT;
  if(u>=0) {
    const float px=cx,py=b.y1+3.0f;
    // The flash at the contact.
    if(u<.12f){const float k=1-u/.12f;for(int r=0;r<3;++r)ellipseAt(px,py+1,10+8.0f*r,3+3.0f*r,mixed(hot,gold,r*.2f),k*(1-r*.2f),1.8f);}
    // Rays fan out along the readout's foot and down.
    if(u<.34f) {
      const float e=easeOut(u/.28f),k=(1-u/.34f)*(1-u/.34f)*1.4f;
      for(int i=0;i<13;++i) {
        const float a=HALF_TURN*(-.05f+1.1f*(i+.5f)/13+.03f*(rnd(i,513)-.5f)),ca=cosine(a),sa=sine(a)*.5f;
        const float r1=18+(30+34*rnd(i,514))*e,r0=std::max(17.0f*std::fabs(ca)+12*sa,r1*.55f);
        sorcLine(px+ca*r0,py+sa*r0,px+ca*r1,py+sa*r1,inkOf(mixed(hot,gold,e*.8f)),q8(k*.5f),q8(k));
      }
    }
    // Two shock rings spread from it along the foot and down; their upper
    // halves would lie behind the readout.
    for(int r=0;r<2;++r) {
      const float v=u-.07f*r,life=r?.5f:.42f;if(v<0 || v>=life)continue;
      const float e=easeOut(v/life),rx=14+(r?56:76)*e,ry=5+(r?14:19)*e,k=(1-e)*(r?.9f:1.2f);
      const Ink c=inkOf(mixed(hot,gold,e)),o=inkOf(orange);const int kq=q8(k),ko=q8(k*.55f);
      const int n=int((rx+ry)*(r?1.1f:1.4f));const float step=1.2f*HALF_TURN/n;
      for(int i=0;i<=n;++i) {
        const float a=-.1f*HALF_TURN+i*step,ca=cosine(a),sa=sine(a);
        const int x=roundi(px+rx*ca),y=roundi(py+ry*sa);
        if(unsigned(x)<unsigned(w_) && unsigned(y)<=unsigned(bottom_)){addw(x,y,c,kq);if(r==0 && (i&1) && unsigned(y-1)<=unsigned(bottom_))addw(x,y-1,o,ko);}
      }
    }
    // The digits just above the contact flash white-hot and cool by 1.4 s.
    if(u<.32f)auraHeatSpot(px,b.y1+2.0f,32,9,inkOf(hot),q8(.95f*(1-smooth((u-.03f)/.29f))));
    // Sparks thrown out with gravity.
    for(unsigned i=0;i<24;++i) {
      const float age=u-.03f*rnd(i,515),life=.4f+.3f*rnd(i,516);if(age<0 || age>=life)continue;
      const float a=HALF_TURN*(-.12f+1.24f*rnd(i,517)),v=70+120*rnd(i,518);
      const float x=px+cosine(a)*v*age,y=py+sine(a)*v*.45f*age+70*age*age,k=1-age/life;
      dot(x,y,i%3?gold:hot,k*1.3f);
    }
    // The struck spot cools under the readout's foot.
    const float scar=smooth((u-.25f)/.15f)*(1-smooth((u-.4f)/1.4f))*presence;
    if(scar>.02f) {
      const Ink c=inkOf(mixed(gold,orange,smooth(u/1.2f)));
      for(int x=roundi(px-26);x<=roundi(px+26);++x) {
        const float d=(x-px)*(1/26.0f),k=scar*(1-d*d)*(.7f+.3f*sine(x*.7f+t*9));
        vdot(x,py+.5f+2*d*d,c,q8(k));
      }
    }
  }
  // Embers rise off the aura through the sustain.
  for(unsigned i=0;i<34;++i) {
    const float age=t-HIT-.15f-2.4f*rnd(i,519),life=.8f+.6f*rnd(i,520);if(age<0 || age>=life)continue;
    const float y0=fistTop(t-age);
    const float x=cx+(rnd(i,521)-.5f)*30+3*sine(age*5+i),y=y0-3-6*rnd(i,522)-(22+18*rnd(i,523))*age,v=age/life;
    dot(x,y,mixed(gold,red,v),(1-v)*1.35f*(1-smooth((t-END+.25f)/.25f))*(.7f+.3f*sine(age*21+i)));
  }
}

// ---- Palms: a halo of raised open golden hands opens round the readout,
// down both sides of the screen. A wave of light
// runs down the hands while two small palms glide together under the
// readout in prayer and meet with a flash. Then the hands strike one after
// another, left and right and lower each time: each drops with speed lines,
// lands with a flash and a square shock, leaves a glowing print and springs
// back. Last, every hand strikes at once toward the readout and a great
// palm drives up under it, flashing white. Sustain: the prints fade, the hands sway and a
// slow wave of light keeps running down them. Settle: they fade top down.
inline void Renderer::auraPalms(float t) {
  constexpr int HW=AURA_HAND_W,HH=AURA_HAND_H;const AuraMask &HAND=AURA_HAND_MASK;
  // The halo's hands down each side (x from the readout's centre, y below
  // its foot), placed in the screen's open margins clear of the replay's
  // frame; the upper pair each inside one 32-pixel mask word: left, right.
  static constexpr int RING=3;
  static constexpr float HALO[RING][3]={{-44,39,11},{-57,57,40},{-58,58,70}};
  constexpr float WAVE=.5f,PRAY=.32f,CLAP=.74f,FIRST=.86f,STEP=.075f,ALL=1.55f,FADE=3.85f,END=4.9f;
  const Box &b=number_;const float cx=b.cx(),y1=float(b.y1);
  const Rgb gold=rgb8(255,196,64),ivory=rgb8(255,238,186),halo=mixed(gold,accent_,.35f);
  const float presence=1-smooth((t-FADE)*(1/(END-FADE)));
  const float all=t-ALL;
  for(int i=0;i<2*RING;++i) {
    const int j=i%RING,side=i<RING?-1:1;
    const float grow=smooth((t-.05f-.09f*j)*(1/.22f))*(1-smooth((t-FADE-.15f*j)*(1/.55f)));if(grow<=0)continue;
    const float sway=smooth((all-.3f)*(1/.6f)),bob=sway*sine(t*2.2f+i*1.3f);
    const float sx=cx+(side<0?HALO[j][0]:HALO[j][1]),sy=y1+HALO[j][2];
    // Its strike: hands go in turn, left then right, top to bottom.
    const float strike=t-(FIRST+STEP*(2*j+(side>0)));
    float drop=0,hit=0;
    if(strike>=-.05f && strike<.45f) {
      drop=strike<0?9*(1+strike*(1/.05f)):strike<.1f?9:9*(1-smooth((strike-.1f)*(1/.35f)));
      hit=strike<0?0:1-smooth(strike*(1/.25f));
    }
    // The final strike: every hand lunges a little toward the readout.
    float lunge=0;
    float ring=0;
    if(all>=-.05f && all<.5f){lunge=all<0?1+all*(1/.05f):1-smooth(all*(1/.5f));ring=all<0?0:1-smooth(all*(1/.3f));}
    const float hx=sx-side*5*lunge,hy=sy+drop+bob-3*lunge;
    // A wave of light runs down the halo before the strikes, and a slow one
    // keeps running down it through the sustain.
    const float wave=std::max(0.0f,1-std::fabs((t-WAVE)*8-j*.9f))*(t<FIRST?1:0);
    const float slow=all>.5f?std::max(0.0f,sine((t*1.3f-j*.3f)*HALF_TURN)):0;
    const float lit=std::max(wave,hit),k=presence*grow*(.55f+.65f*std::max(lit,ring)+.3f*slow);
    if(strike<0 && strike>=-.05f)for(int l=-1;l<=1;++l)sorcLine(hx+l*4,hy-8,hx+l*4,hy-19,inkOf(gold),0,q8(.7f*presence));
    auraSprite(HAND,HW,HH,hx,hy,1,side>0,inkOf(mixed(mixed(gold,ivory,.3f+.5f*lit),WHITE,hit*.6f)),inkOf(mixed(gold,ivory,hit)),q8(k),lit>.25f?q8(k*(.4f*wave+.7f*hit)):0);
    if(strike>=0 && strike<1.6f) {
      // The print it leaves where it landed, cooling.
      if(strike>.1f){const float pk=presence*(1-smooth((strike-.1f)*(1/.55f)))*.6f;auraSprite(HAND,HW,HH,sx,sy+9,1,side>0,inkOf(mixed(gold,halo,.4f)),inkOf(gold),q8(pk),0);}
      // The square shock round the landing.
      if(strike<.2f) {
        const float e=easeOut(strike*(1/.2f)),r=7+9*e;const int kq=q8((1-e)*.85f*presence),n=roundi(r);const float py=sy+9;
        const Ink c=inkOf(mixed(ivory,gold,e));
        for(int d=-n;d<=n;++d){addw(roundi(sx+d),roundi(py-r),c,kq);addw(roundi(sx+d),roundi(py+r),c,kq);addw(roundi(sx-r),roundi(py+d),c,kq);addw(roundi(sx+r),roundi(py+d),c,kq);}
      }
      // Motes rise off the print.
      for(unsigned m=0;m<3;++m) {
        const float ma=strike-.25f-.6f*rnd(i*8+m,532),ml=.7f+.4f*rnd(i*8+m,533);if(ma<0 || ma>=ml)continue;
        dot(sx+(rnd(i*8+m,534)-.5f)*12+2*sine(ma*4+m),sy+5-16*ma,mixed(gold,ivory,.5f),(1-ma/ml)*presence*.9f);
      }
    }
  }
  // The prayer: two small palms glide together under the readout and meet
  // with a flash before the first strike.
  {
    const float k=presence*smooth((t-PRAY)*(1/.15f))*(1-smooth((t-CLAP-.1f)*(1/.15f)));
    if(k>.02f) {
      const float close=smooth((t-PRAY)*(1/(CLAP-PRAY))),gap=10*(1-close),y=y1+12.0f;
      const float meet=t>=CLAP?1-smooth((t-CLAP)*(1/.15f)):0;
      for(int side=-1;side<=1;side+=2)
        auraSprite(HAND,HW,HH,cx+side*(gap+HW*.5f-2),y,1,side>0,inkOf(mixed(gold,ivory,.4f+.6f*meet)),inkOf(gold),q8(k*(.85f+.4f*meet)),q8(k*(.25f+.5f*meet)));
      if(meet>0)for(int r=0;r<2;++r)ellipseAt(cx,y,10+7*r+8*(1-meet),6+3*r+4*(1-meet),mixed(ivory,gold,r*.4f),meet*(1-r*.3f),1.6f);
    }
  }
  // Motes of gold drift up off the hands from the final strike on.
  for(unsigned m=0;m<18;++m) {
    const float age=all-.1f-2.6f*rnd(m,537),life=.9f+.6f*rnd(m,538);if(age<0 || age>=life)continue;
    const int j=int(m%(2*RING)),side=j<RING?-1:1,r=j%RING;
    const float x=cx+(side<0?HALO[r][0]:HALO[r][1])+(rnd(m,539)-.5f)*14+2.5f*sine(age*3.5f+m),y=y1+HALO[r][2]-4-14*age;
    dot(x,y,mixed(gold,ivory,rnd(m,540)),presence*sine(HALF_TURN*age/life)*.95f);
  }
  // The great palm: twice the size, it drives up into the band under the
  // readout at the final strike, flashing white, and fades.
  if(all>=-.08f && all<.7f) {
    const float rise=all<0?(all+.08f)*(1/.08f):1,flash=all<0?0:1-smooth(all*(1/.25f)),k=presence*(all<.2f?1:1-smooth((all-.2f)*(1/.5f)));
    const float y=y1+12+12*(1-rise);
    // Its body only in the flash; then it cools to a glowing outline.
    auraSprite(HAND,HW,HH,cx,y,2,false,inkOf(mixed(ivory,WHITE,flash)),inkOf(mixed(gold,ivory,flash)),q8(k*(1+.3f*flash)),flash>.3f?q8(k*.9f*flash):0);
  }
  glint(t,ALL+.3f,.45f);
}

// ---- Elastic: two blobs of sticky pink gum fly in from the screen's sides
// trailing thick strands and stick to the readout's foot with a splat. The
// sides pull: the far ends slide down the screen's edges, the strands
// stretch thin in the middle like pulled gum, humming faster and faster,
// with a stringy thread beside each, and pull the readout's lower half
// taller. Then they let go: the strands snap back like rubber into the
// readout, which jiggles, squashing and stretching, with a pink flash. Sustain: the gum hangs
// from the readout's foot as two blobs on stretchy strings, bouncing like
// bungee cords and dropped again now and then. Settle: the strings draw
// back up and the blobs shrink away.
inline void Renderer::auraElastic(float t) {
  constexpr float SHOOT=.06f,FLY=.24f,STRETCH=.42f,SNAP=1.12f,DONE=2.0f,RETRACT=3.9f,END=4.8f;
  const Box &b=number_;const float cx=b.cx(),y1=float(b.y1);
  const Rgb pink=rgb8(255,92,180),pale=rgb8(255,206,236);
  // The readout: its lower half is pulled taller by the stretch, then it
  // jiggles, squashing and stretching, when the gum lets go, and is exact
  // again by DONE.
  const float tension=t<SNAP?smooth((t-STRETCH)/(SNAP-STRETCH)):0;
  float pull=2.6f*tension;
  if(t>=SNAP){const float a=t-SNAP;pull=3.0f*std::exp(-4.5f*a)*cosine(16*a)*(1-smooth((a-.5f)/(DONE-SNAP-.55f)));}
  auraStretchReadout(pull);
  glint(t,SNAP+.04f,.42f);
  const float a=t-SNAP,fade=1-smooth((t-END+.3f)/.3f);
  for(int side=-1;side<=1;side+=2) {
    const float launch=SHOOT+(side>0?.07f:0),u=(t-launch)/FLY;if(u<0)continue;
    const float ax=cx+side*38.0f,ay=y1+3.0f;
    if(t<SNAP) {
      // The far end on the screen's edge, sliding down as it pulls; while
      // flying, the head runs from the edge up to the readout.
      const float ex=side<0?-1.0f:w_+0.0f,ey=y1+28+46*tension,reach=easeOut(u);
      const float hx=ex+(ax-ex)*reach,hy=ey+(ay-ey)*reach;
      const float hum=tension*(1+1.6f*tension)*sine(2*HALF_TURN*(t-STRETCH)*(5+11*tension));
      const float sag=(1-tension)*side*-3.0f*smooth((t-launch-.1f)/.3f);
      auraGum(hx,hy,ex,ey,sag,hum,4.2f,4.2f-3.0f*tension,4.6f,q8(fade));
      // A stringy thread of gum beside the strand, as pulled gum has.
      if(tension>.15f) {
        const float off=side*(3+3*tension);
        sorcLine(ax+off*.3f,ay+3,(ax+ex)*.5f+off,(ay+ey)*.5f+hum,inkOf(pale),q8(.55f*tension),q8(.35f*tension));
        sorcLine((ax+ex)*.5f+off,(ay+ey)*.5f+hum,ex-side*2,ey-4,inkOf(pink),q8(.35f*tension),q8(.6f*tension));
      }
      if(u<1)auraBlob(hx,hy,3,2.5f,q8(1));
      // The splat as it sticks: gum spreading along the readout's foot.
      const float splat=u>=1?1-smooth((t-launch-FLY)/.3f):0;
      auraBlob(ax,ay+1,4+4*splat,1.6f+.6f*splat,q8(fade));
      if(splat>0)for(int s=0;s<5;++s){const float sa=HALF_TURN*(.1f+.8f*s/4),r=4+11*(1-splat);dot(ax+r*cosine(sa)*1.4f,ay+2+r*sine(sa)*.7f,pale,splat);}
      // The gum stuck to the edge, squashing as it pulls.
      auraBlob(ex,ey,3.5f-tension,3+tension,q8(fade));
    } else {
      // Snapped: the free end springs back, overshoots and wobbles, then
      // hangs as a blob on a string under the attachment, bouncing.
      const float spring=std::exp(-5.5f*a)*cosine(12*a),f=std::max(0.0f,spring);
      float drop=16;
      for(int k=0;k<4;++k){const float at=DONE-.55f+.5f*k+.2f*rnd(k*2+(side>0),551)+(side>0?.25f:0),v=t-at;if(v>0)drop+=10*std::exp(-2.4f*v)*sine(9*v);}
      drop*=1-smooth((t-RETRACT-(side>0?.15f:0))/.55f);
      const float ex0=side<0?-1.0f:w_+0.0f,ey0=y1+74;
      const float sway=side*3.5f*sine(t*2.1f+(side>0?1.3f:0))*smooth((a-.5f)/.5f);
      const float ex=ax+(ex0-ax)*f+sway,ey=std::max(ay+3,ay+drop+(ey0-ay-drop)*f+std::min(0.0f,spring)*5);
      const float stretch=smooth((ey-ay-14)/10);
      const float k=fade*(t<RETRACT+.4f?1:1-smooth((t-RETRACT-.4f)/.4f));
      auraGum(ax,ay,ex,ey,f*side*4*sine(a*40),-sway*.3f,3.8f,3.2f-1.6f*stretch-1.4f*f,3.8f,q8(k));
      // The blob at the free end, squashing as it bounces.
      const float squash=1+.3f*sine(t*10)*std::exp(-2.5f*std::max(0.0f,a-.3f))+.25f*stretch;
      const float size=1-smooth((t-RETRACT-.2f)/.5f)*.5f;
      auraBlob(ex,ey+2,4.4f*squash*size,4.0f/squash*size,q8(k));
    }
  }
  // The snap: a pink flash ring round each attachment.
  if(a>=0 && a<.35f) {
    const float e=easeOut(a/.35f);
    for(int side=-1;side<=1;side+=2)ellipseAt(cx+side*38.0f,y1+5,4+15*e,2+6*e,mixed(pale,accent_,e*.5f),(1-e)*1.1f,2.0f);
  }
  // Pink sparkles shaken loose by the snap, drifting down.
  for(unsigned i=0;i<20;++i) {
    const float age=a-.02f-2.0f*rnd(i,543)*rnd(i,544),life=.8f+.6f*rnd(i,545);if(age<0 || age>=life)continue;
    const float x=cx+(i%2?38:-38)+(rnd(i,546)-.5f)*30+3*sine(age*4+i),y=y1+6+10*rnd(i,547)+16*age;
    dot(x,y,i%3?pale:mixed(pink,accent_,.4f),sine(HALF_TURN*age/life)*fade);
  }
}

// ---- Chains: two steel chains shoot in from the screen's sides, one along
// the readout's foot, one along its top, and each turns the corner up or
// down the far side, so together they wrap the readout in a loop of
// links; their hooked heads catch each other's chain and the loop snaps
// taut with a flash that runs scarlet through the links, sparks at the
// two locks, and a rattle. Sustain: the loop holds, glints running round
// it and a ripple shaking along it now and then. Settle: the chains
// slacken, sag, and slide back out the way they came.
inline void Renderer::auraChains(float t) {
  constexpr float PITCH=4.6f,SHOOT0=.12f,SHOOT=.42f,LOCK=.62f,SLACK=3.45f,PULL=3.95f,GONE=4.7f;
  const Box &b=number_;
  const float topY=b.y0-5.0f,botY=b.y1+6.0f,lx=1.0f,rx=w_-2.0f,run=rx-lx+12,side=botY-topY;
  const Rgb steel=mixed(rgb8(196,208,226),accent_,.15f),shine=rgb8(240,246,255),scarlet=rgb8(240,24,44);
  // Each chain's path: in from off screen along one run, then round the
  // corner up or down the far side. s is the distance from its start.
  auto pos=[&](int chain,float s,float &x,float &y,bool &vertical){
    if(s<run){vertical=false;y=chain?topY:botY;x=chain?rx+12-s:lx-12+s;return;}
    vertical=true;x=chain?lx:rx;const float v=std::min(s-run,side);y=chain?topY+v:botY-v;
  };
  const float total=run+side;
  // The heads: shot in fast, slowing into the lock; pulled back out at the end.
  float head[2];
  for(int c=0;c<2;++c) {
    const float start=SHOOT0+.06f*c;
    head[c]=total*easeOut((t-start)/SHOOT)*(t<PULL?1:1-smooth((t-PULL-.05f*c)/(GONE-PULL-.1f)))+(t<PULL?0:0);
  }
  const float lock=t-LOCK;
  // The lock: the links flash white, then scarlet, then cool to steel.
  const float flash=lock>=0?1-smooth(lock/.12f):0,red=lock>=0?smooth(lock/.08f)*(1-smooth((lock-.15f)/.5f)):0;
  // Rattle: a jitter dying away after the lock, then a ripple now and then.
  const float rattle=lock>=0?1.2f*(1-smooth(lock/.9f)):0;
  const float slack=smooth((t-SLACK)/.45f);
  glint(t,LOCK+.02f,.42f);
  for(int c=0;c<2;++c) {
    const float h=head[c];if(h<=0)continue;
    const int n=int(h/PITCH)+1;
    for(int i=0;i<n;++i) {
      const float s=h-i*PITCH;if(s<0)break;
      float x,y;bool vertical;pos(c,s,x,y,vertical);
      if(x<-4 || x>w_+3)continue;
      // The glint running round the loop, and the ripple shaking along it.
      const float lap=(t-1.4f)*90,d=std::fmod(std::fabs(s+c*run-lap),total*2),glow=t>1.4f && t<SLACK?std::max(0.0f,1-std::min(d,total*2-d)/14):0;
      float jig=0;
      // The rattle: a quick wave shaking along the chain, every third link
      // kicked a little out of step.
      if(rattle>0)jig=rattle*(sine(s*.42f-t*48+c*1.7f)+(i%3==0?.5f:0)*sine(t*61+i));
      for(int r=0;r<3;++r){const float at=1.9f+.75f*r+.2f*c,v=t-at;if(v>0 && v<.6f){const float wave=s-v*260;jig+=.9f*std::exp(-3*v)*sine(wave*.25f)*(std::fabs(wave)<40?1-std::fabs(wave)/40:0);}}
      // Slack: the foot's run sags in the middle, the far side bows.
      if(!vertical && c==0)y+=slack*3.5f*sine(HALF_TURN*clamp01((x-lx)/(rx-lx)));
      if(vertical)x+=slack*(c?1:-1)*.4f;
      if(vertical)x+=jig*.6f;else y+=jig;
      const Rgb base=mixed(mixed(steel,scarlet,red),WHITE,flash);
      const int k=q8(.95f+.5f*glow);
      auraLink(x,y,vertical,i%2==0,inkOf(mixed(base,shine,glow)),inkOf(mixed(mixed(shine,scarlet,red*.6f),WHITE,flash)),k);
    }
    // The hooked head.
    if(h>2 && t<GONE) {
      float x,y;bool vertical;pos(c,h+3,x,y,vertical);
      const Ink hk=inkOf(mixed(shine,scarlet,red));const int kq=q8(1.1f);
      if(!vertical){const float dir=c?-1:1;sorcLine(x-dir*3,y,x+dir*2,y,hk,kq,kq);sorcLine(x+dir*2,y,x+dir*3,y-2,hk,kq,kq);sorcLine(x+dir*3,y-2,x+dir*1,y-3,hk,kq,kq);}
      else{const float dir=c?1:-1;sorcLine(x,y-dir*3,x,y+dir*2,hk,kq,kq);sorcLine(x,y+dir*2,x+(c?1:-1),y+dir*3,hk,kq,kq);}
    }
  }
  // The lock's pulse: a scarlet line thrown down off the loop's foot.
  if(lock>=0 && lock<.45f) {
    const float e=easeOut(lock/.45f),o=2+58*e;const int kq=q8((1-e)*(1-e)*1.2f);
    const Ink c=inkOf(mixed(scarlet,shine,.25f*(1-e)));
    for(int x=0;x<w_;++x)addw(x,roundi(botY+2+o),c,kq);
  }
  // Sparks off the two locks, at the far corners.
  if(lock>=0 && lock<.6f)for(unsigned i=0;i<24;++i) {
    const int corner=i%2;const float age=lock-.03f*rnd(i,562),life=.3f+.3f*rnd(i,563);if(age<0 || age>=life)continue;
    const float x0=corner?lx+1:rx-1,y0=corner?botY:topY,a=HALF_TURN*(corner?(-.4f+.8f*rnd(i,564)):(.6f+.8f*rnd(i,564))),v=50+70*rnd(i,565);
    const float x=x0+cosine(a)*v*age,y=y0+sine(a)*v*.6f*age+60*age*age;
    dot(x,y,i%3?shine:scarlet,(1-age/life)*1.2f);
  }
  // A faint aura shimmering round the bound readout through the sustain.
  const float hold=smooth((lock-.4f)/.4f)*(1-slack);
  if(hold>0)for(unsigned i=0;i<14;++i) {
    const float age=t-1.0f-2.4f*rnd(i,566),life=.6f+.4f*rnd(i,567);if(age<0 || age>=life)continue;
    const bool top=i%2;const float x=6+(w_-12)*rnd(i,568),y=(top?topY-1:botY+3)+(top?-2:4)*age/life;
    dot(x,y,mixed(steel,accent_,.5f),hold*sine(HALF_TURN*age/life)*.8f);
  }
}

// ---- Waterleaf: a glass filled to the brim stands under the readout with
// a leaf floating on the water. Two open hands of aura rise either side of
// it and glow, light streaming from them into the glass; the water
// shivers in rings round the leaf, the leaf turns, and the water swells
// over the brim. Then it surges: a dome of water heaves up, drops burst out
// sparkling, a crack flashes across the glass and the header glints.
// Sustain: the water keeps welling over the brim and running down the
// glass in drops that fall and splash, the leaf bobbing and turning on the
// dome, the hands breathing. Settle: the hands fade, then the glass.
inline void Renderer::auraWaterleaf(float t) {
  constexpr float GLASS=.3f,HANDS=.25f,SWELL=.75f,SURGE=1.25f,FADE=3.8f,END=4.9f;
  const Box &b=number_;const float cx=b.cx(),y1=float(b.y1);
  const Rgb rim=rgb8(214,240,255),water=rgb8(96,196,255),deep=rgb8(54,140,245),foam=rgb8(225,248,255);
  const Rgb leafLight=rgb8(150,230,90),leafDark=rgb8(40,150,60),aura=mixed(rgb8(255,236,170),accent_,.4f);
  // The glass: 34 px tall, 32 wide at the brim and 26 at the base.
  const float gt=y1+12,gb=y1+45,topW=16,botW=13;
  const float slope=(botW-topW)*(1/(gb-gt));
  auto wallX=[&](float y,int side){return cx+side*(topW+slope*(y-gt));};
  const float glassK=smooth(t/GLASS)*(1-smooth((t-FADE-.3f)*(1/(END-FADE-.3f))));
  const float handK=smooth((t-HANDS)*(1/.3f))*(1-smooth((t-FADE)*(1/.6f)));
  // The water: brim full, swelling over the brim before the surge, heaving
  // into a dome at it and settling to a low dome that keeps welling over.
  const float u=t-SURGE;
  float dome=1.6f*smooth((t-SWELL)*(1/(SURGE-SWELL)));
  if(u>=0)dome=1.6f+4.4f*std::exp(-4*u)*cosine(9*u)*(u<.12f?u*(1/.12f):1)+(2.4f+.4f*sine(t*3.1f))*smooth(u*(1/.4f));
  dome*=1-smooth((t-FADE)*(1/.7f));
  const float ys=gt+1.5f;
  if(glassK>.01f) {
    const int kq=q8(glassK);
    // Water inside: clear in the middle, the light gathering toward the
    // walls in a smooth gradient (as a full glass looks), deeper toward the
    // base, with a lit band under the surface and a slow shimmer.
    // Per pixel only integers: e (the distance from the middle over the
    // half width) steps in Q8 from a per-row reciprocal.
    const float invDepth=1/(gb-ys);
    for(int y=roundi(ys+1);y<=roundi(gb-1);++y) {
      const float d=(y-ys)*invDepth;const int l=roundi(wallX(float(y),-1))+1,r=roundi(wallX(float(y),1))-1;
      const int band8=std::max(0,q8(1-(y-ys)*(1/3.5f))),inv16=65536/(r-l+1),depth8=q8(1-.35f*d);
      const Ink c=inkOf(mixed(water,deep,d)),edge=inkOf(mixed(water,deep,.4f+.6f*d));
      const int sh=noise(y*90,int(t*300),581),kRow=q8(glassK*(.85f+.15f*(sh-128)*(1/128.0f)));
      for(int x=l;x<=r;++x) {
        const int e8=(std::abs(2*x-l-r)*inv16)>>8,g8=std::max(band8,e8<128?0:(e8-128)*2);
        if(g8<=5)continue;
        addw(x,y,e8>179?edge:c,(kRow*((((g8*g8)>>8)*depth8)>>8))>>8);
      }
    }
    // The walls and base, with a highlight down the left wall.
    sorcLine(cx-topW,gt,cx-botW,gb,inkOf(rim),kq,kq);sorcLine(cx+topW,gt,cx+botW,gb,inkOf(rim),(kq*3)>>2,(kq*3)>>2);
    sorcLine(cx-topW+2.5f,gt+3,cx-botW+2.2f,gb-4,inkOf(foam),kq>>1,kq>>3);
    for(int x=roundi(cx-botW);x<=roundi(cx+botW);++x){const float v=(x-cx)*(1/botW);addw(x,roundi(gb+.8f*auraArc(v)),inkOf(rim),kq);addw(x,roundi(gb+2),inkOf(rim),(kq*3)>>2);}
    // The brim: the back of the rim dim, the front bright.
    for(int i=0;i<=40;++i){const float a=i*(2*HALF_TURN/40),x=cx+topW*cosine(a),y=gt+1.8f*sine(a);addw(roundi(x),roundi(y),inkOf(rim),sine(a)>0?kq:(kq*2)/5);}
    // The surface: an ellipse at the brim, or a dome of water over it.
    const Ink surf=inkOf(mixed(water,foam,.5f));
    for(int x=roundi(cx-topW+1);x<=roundi(cx+topW-1);++x) {
      const float v=(x-cx)*(1/(topW-1)),top=ys-dome*auraArc(v);
      vdot(x,top,surf,kq);
      if(dome>1.2f)for(int y=roundi(top)+1;y<roundi(ys);++y)addw(x,y,inkOf(water),(kq*2)/5);
    }
  }
  // The rings round the leaf as the aura stirs the water.
  float lx=cx+3*sine(t*1.3f),ly=ys-dome+.5f;
  if(glassK>.01f && t>.55f && t<FADE+.4f) {
    for(int r=0;r<3;++r) {
      const float age=auraWrap(t-.55f+r*.23f,.7f,1*(1/.7f)),e=age*(1/.7f),rr=3+11*e;
      const float k=glassK*(1-e)*(t<SURGE?.9f:.5f);
      for(int i=0;i<=24;++i){const float a=i*(2*HALF_TURN/24),x=lx+rr*cosine(a);if(std::fabs(x-cx)>topW-1.5f)continue;addw(roundi(x),roundi(ly+rr*.22f*sine(a)),inkOf(foam),q8(k*.7f));}
    }
  }
  // The leaf: a small green spindle on the surface, turning; it spins fast
  // as the water surges and then keeps turning slowly.
  if(glassK>.01f) {
    float turn=.8f*t+2.5f*smooth((t-.6f)*(1/.6f));
    if(u>=0)turn+=4*(1-std::exp(-3*u))+.4f*u;
    // At the surge it is tossed up off the dome and lands back.
    const float toss=u>=0 && u<.45f?12*(u*(1/.45f))*(1-u*(1/.45f))*4*.5f:0;
    const float ca=cosine(turn),sa=sine(turn)*.35f,bob=(u>=0?1.2f*sine(t*4.3f)*smooth((u-.45f)*(1/.3f)):0)-toss;
    const float fx=lx,fy=ly-1+bob;
    for(int i=-7;i<=7;++i) {
      const float v=i*(1/7.0f),w=2.4f*(1-v*v),x=fx+ca*i,y=fy+sa*i;
      vdot(roundi(x),y,inkOf(leafDark),q8(glassK));
      if(w>.6f){vdot(roundi(x-sa*w*2.5f),y-ca*w*.6f,inkOf(leafLight),q8(glassK));vdot(roundi(x+sa*w*2.5f),y+ca*w*.6f,inkOf(leafLight),q8(glassK*.8f));}
    }
    addw(roundi(fx-ca*8),roundi(fy-sa*8),inkOf(leafDark),q8(glassK));addw(roundi(fx-ca*9),roundi(fy-sa*9-.5f),inkOf(leafDark),q8(glassK*.7f));
  }
  // The hands of aura either side, thumbs toward the glass, breathing; the
  // light streams from them into the water.
  if(handK>.01f) {
    const float breath=.75f+.25f*sine(t*4.5f),surge=u>=0 && u<.4f?1-u*(1/.4f):0,build=smooth((t-HANDS)*(1/(SURGE-HANDS)));
    for(int side=-1;side<=1;side+=2) {
      const float hx=cx+side*(topW+9),hy=gt+6+1.5f*sine(t*2+side);
      auraSprite(AURA_HAND_MASK,AURA_HAND_W,AURA_HAND_H,hx,hy,1,side>0,inkOf(mixed(aura,WHITE,surge)),inkOf(aura),q8(handK*(.75f+.35f*breath+.4f*surge)),u>.6f?0:q8(handK*(.3f+.3f*build*breath+.5f*surge)*(u>0?1-u*(1/.6f):1)));
      // Wisps rising off the hand.
      for(unsigned i=0;i<4;++i) {
        const float a1=t*1.1f+rnd(i,571)+side,age=a1-floori(a1);
        dot(hx+(rnd(i+side*9,572)-.5f)*12+1.5f*sine(age*6+i),hy-6-12*age,aura,handK*(1-age)*.8f);
      }
      // Light streaming into the glass along a short arc.
      for(unsigned i=0;i<5;++i) {
        const float p1=t*1.6f+i*.2f,ph=p1-floori(p1),x=hx-side*(4+(topW+4)*ph),y=hy-2-6*sine(ph*HALF_TURN);
        dot(x,y,mixed(aura,foam,ph),handK*build*sine(HALF_TURN*ph)*.9f);
      }
    }
  }
  // The hands burn: aura licks up round each in short tongues, taller as it
  // builds and flaring white at the surge.
  if(handK>.01f) {
    const float build=smooth((t-.4f)*(1/.7f)),flare=u>=0 && u<.4f?1-u*(1/.4f):0,scroll=t*900;
    for(int side=-1;side<=1;side+=2) {
      const float hx=cx+side*(topW+9),hy=gt+6+1.5f*sine(t*2+side);
      for(int j=0;j<14;++j) {
        const float a=j*(2*HALF_TURN/14),ca=cosine(a),sa=sine(a),up=(1-sa)*.5f;
        const int n=noise(j*190+side*5000,int(scroll),582);
        const float len=(1.5f+5*n*(1/255.0f))*(.4f+.8f*up)*(.4f+.6f*build+.4f*flare);if(len<1)continue;
        float dx=ca,dy=sa-1.2f;const float inv=auraRsqrt(dx*dx+dy*dy);dx*=inv;dy*=inv;
        const float x0=hx+9*ca,y0=hy+9*sa;
        sorcLine(x0,y0,x0+dx*len,y0+dy*len,inkOf(mixed(aura,WHITE,flare*.5f)),q8(handK*(.7f+.3f*build)),q8(handK*.15f));
      }
    }
  }
  if(u<0)return;
  glint(t,SURGE+.22f,.42f);
  const float endK=1-smooth((t-FADE)*(1/(END-FADE-.2f)));
  // The crack: a zigzag across the front of the glass, white at the surge,
  // fading to a faint line that glints now and then.
  {
    static constexpr float CRACK[][2]={{-9,3},{-4,8},{-7,12},{-1,17},{-3,21},{3,27}};
    const float k=glassK*(u<.15f?1.3f:.45f+.25f*std::max(0.0f,sine(t*2.7f)));
    for(int i=0;i+1<6;++i)sorcLine(cx+CRACK[i][0],gt+CRACK[i][1],cx+CRACK[i+1][0],gt+CRACK[i+1][1],u<.15f?WHITE_INK:inkOf(foam),q8(k),q8(k));
    sorcLine(cx-4,gt+8,cx-11,gt+11,inkOf(foam),q8(k*.7f),0);sorcLine(cx-1,gt+17,cx+5,gt+15,inkOf(foam),q8(k*.6f),0);
  }
  // The surge: water sprays out of the dome to both sides, arcing down
  // the screen, sparkling.
  for(unsigned i=0;i<24;++i) {
    const float age=u-.05f*rnd(i,573),life=.55f+.3f*rnd(i,574);if(age<0 || age>=life)continue;
    const float side=i%2?1:-1,a=HALF_TURN*(1.5f+side*(.2f+.28f*rnd(i,575))),v=60+70*rnd(i,576);
    const float x=cx+side*(topW-3)*rnd(i,577)+cosine(a)*v*age,y=ys-dome+sine(a)*v*age+190*age*age;
    const float k=(1-age/life)*1.4f,vx=cosine(a)*v,vy=sine(a)*v+380*age;
    if(i%3==0)twinkle(x,y,foam,k,1);else dot(x,y,i%2?mixed(water,foam,.4f):foam,k*1.2f);
    if(i&1)sorcLine(x-vx*.025f,y-vy*.025f,x,y,inkOf(water),0,q8(k*.6f));
  }
  // Sustain: water wells over the brim and runs down both sides of the
  // glass, then drips off its base and falls away down the screen.
  const float slide=std::sqrt((gb-gt+1)*(1/60.0f));
  for(unsigned i=0;i<10;++i) {
    if(u<.2f)continue;
    const float period=1.1f+.6f*rnd(i,578),r=auraRsqrt(period),age=auraWrap(u-.2f+rnd(i,579)*period,period,r*r);
    const int side=i%2?1:-1;
    const float k=endK*smooth(age*(1/.08f))*(1-smooth((age-period+.15f)*(1/.15f)));
    if(age<slide) {
      const float y=gt+60*age*age,x=wallX(y,side)+side*1.6f;
      dot(x,y,water,k);dot(x,y-1.5f,mixed(water,foam,.5f),k*.6f);
    } else {
      const float fall=age-slide;if(fall>.28f)continue;
      const float x=wallX(gb,side)+side*(1.6f+5*fall),y=gb+2+fall*(40+160*fall);
      dot(x,y,i%3?water:foam,k*(1-smooth((fall-.15f)*(1/.12f))));plot(x,y-2.5f,water,k*.4f);
    }
  }
  // Sparkles on the dome.
  for(unsigned i=0;i<6;++i) {
    const float a1=t*.8f+rnd(i,580),age=a1-floori(a1),x=cx+(rnd(i+uint32_t(t*.8f+rnd(i,580))*7,581)-.5f)*24;
    twinkle(x,ys-dome*.7f-.5f,foam,endK*sine(HALF_TURN*age)*.9f,1);
  }
}
}
