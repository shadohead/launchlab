#pragma once
// The phantom set of 0.18 (Timestop..Sequel): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Same rules as
// the other sets: additive light on the background outside the UI halo, a
// pure function of elapsed time with fixed seeds, the readout exactly as
// drawn from three quarters of the duration, every frame within the cost
// model's 8 ms.
// The set's look is a printed comic page come alive: flat bold colour, hard
// outlines, heavy lettering and freeze frames.
// On a light custom background added light would vanish, so the set's
// strokes are painted there as ink instead (phantomAdd): the pixel is pulled
// toward a darker shade of the stroke's colour, still only outside the halo.
namespace BestEffectUI {

// A light background (a Paper-like custom theme), where light is painted as ink.
inline bool Renderer::phantomPaper()const {
  const unsigned v=load(bg_);return 2*(v>>11)+((v>>5)&63)+2*(v&31)>=96;
}
// addw for this set: points off the screen are skipped before the probe,
// and on a light background the stroke is painted as ink (toward 47% of
// its colour) rather than added as light.
inline void Renderer::phantomAdd(int x,int y,const Ink &c,int w) {
  if(w<5 || unsigned(x)>=unsigned(w_) || unsigned(y)>unsigned(bottom_))return;
  ++probes_;if(blocked(x,y))return;
  const int edge=x&31;
  if(edge<HALO){if(x>=32)ink(y,(x>>5)-1);}
  else if(edge>=32-HALO && (x|31)+1<w_)ink(y,(x>>5)+1);
  uint16_t &p=px_[y*w_+x];const unsigned v=load(p);
  int r=v>>11,g=(v>>5)&63,b=v&31;
  if(phantomInk_) {
    const int k=std::min(256,w),tr=(c.r*120)>>16,tg=(c.g*120)>>16,tb=(c.b*120)>>16;
    r+=((tr-r)*k)>>8;g+=((tg-g)*k)>>8;b+=((tb-b)*k)>>8;
  } else {
    r=std::min(31,r+((c.r*w+32768)>>16));g=std::min(63,g+((c.g*w+32768)>>16));b=std::min(31,b+((c.b*w+32768)>>16));
  }
  p=store(uint16_t(r<<11|g<<5|b));++plots_;
}
// A sub-pixel point over four pixels.
inline void Renderer::phantomDot(float x,float y,const Ink &c,int kq) {
  const int ix=floori(x),iy=floori(y),ax=q8(x-ix),ay=q8(y-iy);
  phantomAdd(ix,iy,c,(kq*(256-ax)*(256-ay))>>16);phantomAdd(ix+1,iy,c,(kq*ax*(256-ay))>>16);
  phantomAdd(ix,iy+1,c,(kq*(256-ax)*ay)>>16);phantomAdd(ix+1,iy+1,c,(kq*ax*ay)>>16);
}
// A fixed-point line, one pixel per step, its weight ramping k0..k1 (Q8).
inline void Renderer::phantomLine(float x0,float y0,float x1,float y1,const Ink &c,int k0,int k1) {
  // Steps in Q8 by integer division (a float division is a library call
  // on the device).
  const int dx8=q8(x1-x0),dy8=q8(y1-y0),n=std::max(1,std::max(std::abs(dx8),std::abs(dy8))>>8);
  int x8=q8(x0)+128,y8=q8(y0)+128,wq=k0<<8;const int sx=dx8/n,sy=dy8/n,dw=(k1-k0)*256/n;
  for(int i=0;i<=n;++i,x8+=sx,y8+=sy,wq+=dw)phantomAdd(x8>>8,y8>>8,c,wq>>8);
}
// A small four-pointed glint: a dot and short arms.
inline void Renderer::phantomTwinkle(float x,float y,const Ink &c,int kq,int arm) {
  phantomDot(x,y,c,kq);const int ix=roundi(x),iy=roundi(y);
  for(int d=1;d<=arm;++d){const int f=kq*(arm+1-d)/(arm+2);
    phantomAdd(ix+d+1,iy,c,f);phantomAdd(ix-d-1,iy,c,f);phantomAdd(ix,iy+d+1,c,f);phantomAdd(ix,iy-d-1,c,f);}
}
// An axis-aligned elliptical arc, about one point per pixel of its length.
inline void Renderer::phantomEllipse(float cx,float cy,float rx,float ry,const Ink &c,int kq,float from,float sweep) {
  if(kq<5 || rx<.5f || ry<.5f)return;
  const int n=std::max(8,int(std::fabs(sweep)*(rx+ry)*.75f));const float step=sweep/n;
  // Each point is the last one rotated by the step: two table lookups per
  // arc, not per point.
  float ca=cosine(from),sa=sine(from);const float rc=cosine(step),rs=sine(step);
  for(int i=0;i<=n;++i) {
    phantomAdd(roundi(cx+rx*ca),roundi(cy+ry*sa),c,kq);
    const float c2=ca*rc-sa*rs;sa=sa*rc+ca*rs;ca=c2;
  }
}
// A throwing knife pointing along the unit vector (dx,dy) from its guard at
// (gx,gy): a three-pixel blade tapering to its tip, a crosswise guard, a
// grip and a pommel, about 26 px long. `shine` (0..1 along the blade,
// negative for none) places a travelling highlight.
inline void Renderer::phantomKnife(float gx,float gy,float dx,float dy,const Ink &blade,const Ink &hilt,int kq,float shine) {
  if(kq<5)return;
  constexpr float BLADE=15;
  const float px=-dy,py=dx;  // across the blade
  // A three-pixel blade: the bright spine to the tip, a softer row each
  // side tapering in short of it.
  phantomLine(gx+dx,gy+dy,gx+dx*BLADE,gy+dy*BLADE,blade,kq,kq);
  phantomLine(gx+dx+px,gy+dy+py,gx+dx*(BLADE-4)+px,gy+dy*(BLADE-4)+py,blade,(kq*170)>>8,(kq*120)>>8);
  phantomLine(gx+dx-px,gy+dy-py,gx+dx*(BLADE-4)-px,gy+dy*(BLADE-4)-py,blade,(kq*120)>>8,(kq*80)>>8);
  // A two-row guard across it, a two-pixel grip and a pommel.
  phantomLine(gx-px*3.5f,gy-py*3.5f,gx+px*3.5f,gy+py*3.5f,hilt,kq,kq);
  phantomLine(gx-px*3-dx,gy-py*3-dy,gx+px*3-dx,gy+py*3-dy,hilt,(kq*150)>>8,(kq*150)>>8);
  phantomLine(gx-dx*2,gy-dy*2,gx-dx*7,gy-dy*7,hilt,(kq*210)>>8,(kq*210)>>8);
  phantomLine(gx-dx*2+px,gy-dy*2+py,gx-dx*7+px,gy-dy*7+py,hilt,(kq*110)>>8,(kq*110)>>8);
  phantomDot(gx-dx*8.5f,gy-dy*8.5f,hilt,(kq*200)>>8);
  if(shine>=0 && shine<=1) {
    const float s=1+(BLADE-1)*shine;
    phantomDot(gx+dx*s,gy+dy*s,WHITE_INK,std::min(256,kq*3/2));
  }
}

// Writes one pixel directly (RGB565), for the strokes Zipper draws on the
// readout's own rows while it holds them; only a changed pixel is counted.
inline void Renderer::phantomPut(int x,int y,uint16_t value) {
  if(unsigned(x)>=unsigned(w_) || unsigned(y)>=unsigned(h_))return;
  uint16_t &p=px_[y*w_+x];const uint16_t v=store(value);if(p!=v){p=v;++plots_;}
}
// Zipper teeth over columns x0..x1: the upper tape's teeth on rows upper and
// upper+1 at every other pair of columns, the lower tape's on rows lower and
// lower+1 between them, so when the rows meet (upper = lower) they
// interlock into one three-row band. Each tooth is lit on its left and top.
// `overlay` writes them over whatever is there (the readout's rows, while
// Zipper holds them), blended in by k8; otherwise they are light (or ink on
// a light background) outside the halo.
inline void Renderer::phantomTeeth(int x0,int x1,int upper,int lower,int k8,bool overlay) {
  if(k8<=0)return;
  const bool paper=phantomPaper();
  const Ink lit=paper?inkOf(rgb8(150,104,20)):inkOf(rgb8(255,226,120)),body=paper?inkOf(rgb8(104,70,10)):inkOf(rgb8(224,168,52)),dark=paper?inkOf(rgb8(70,46,6)):inkOf(rgb8(150,100,24));
  for(int x=std::max(0,x0);x<=std::min(w_-1,x1);++x) {
    const int phase=x&3,row=phase<2?upper:lower;
    for(int r=0;r<2;++r) {
      const Ink &c=phase&1?(r?dark:body):(r?body:lit);
      if(!overlay){phantomAdd(x,row+r,c,k8);continue;}
      if(unsigned(row+r)>=unsigned(h_))continue;
      const unsigned v=load(px_[(row+r)*w_+x]);
      const int cr=c.r>>8,cg=c.g>>8,cb=c.b>>8,pr=v>>11,pg=(v>>5)&63,pb=v&31;
      phantomPut(x,row+r,uint16_t((pr+(((cr-pr)*k8)>>8))<<11|(pg+(((cg-pg)*k8)>>8))<<5|(pb+(((cb-pb)*k8)>>8))));
    }
  }
}
// The zipper's slider at (x,y), a small rounded body over the teeth, and its
// pull tab hanging below it, swung by `swing` radians.
inline void Renderer::phantomPull(float x,float y,float swing,int k8,bool overlay) {
  if(k8<=0)return;
  const bool paper=phantomPaper();
  const Ink lit=paper?inkOf(rgb8(120,110,90)):inkOf(rgb8(255,250,230)),body=paper?inkOf(rgb8(110,76,14)):inkOf(rgb8(246,200,90)),edge=paper?inkOf(rgb8(60,40,6)):inkOf(rgb8(176,120,30));
  auto put=[&](int px,int py,const Ink &c) {
    if(!overlay){phantomAdd(px,py,c,k8);return;}
    if(unsigned(px)>=unsigned(w_) || unsigned(py)>=unsigned(h_))return;
    const unsigned v=load(px_[py*w_+px]);const int pr=v>>11,pg=(v>>5)&63,pb=v&31;
    phantomPut(px,py,uint16_t((pr+((((c.r>>8)-pr)*k8)>>8))<<11|(pg+((((c.g>>8)-pg)*k8)>>8))<<5|(pb+((((c.b>>8)-pb)*k8)>>8))));
  };
  const int ix=roundi(x),iy=roundi(y);
  // The body: a 7x5 rounded block, lit along its top.
  static constexpr int HALF[]={2,3,3,3,2};
  for(int r=0;r<5;++r)for(int dx=-HALF[r];dx<=HALF[r];++dx)put(ix+dx,iy-2+r,r==0?lit:std::abs(dx)==HALF[r] || r==4?edge:body);
  // The tab: three pixels wide and nine long, with a hole near its end.
  const float sx=sine(swing),cy=cosine(swing);
  for(int j=0;j<9;++j)for(int w=-1;w<=1;++w) {
    if(j==6 && w==0)continue;
    const float px=x+sx*(j+3)+cy*w,py=y+cy*(j+3)-sx*w;
    put(roundi(px),roundi(py),w<0?lit:w>0 || j==8?edge:body);
  }
}

// A clenched fist seen from the side, its knuckles at (x,y) and punching
// along (dx,dy) (right, left or up): an outline with finger creases and a
// thumb, a dithered fill and, with `arm`, a forearm with a cuff trailing
// behind it. `scale` 2 doubles every pixel (the fill stays dithered).
inline void Renderer::phantomFist(float x,float y,int dx,int dy,int scale,const Ink &edge,const Ink &fill,int k8,bool arm) {
  if(k8<5)return;
  // Rows across the punch (v), columns along it (u, the knuckles at u=10).
  // Rows across the punch (v), columns along it (u, the knuckles at the
  // last column): the thumb folded over the top, two grooves between the
  // curled fingers and the knuckles' bumps along the front.
  static constexpr int U=14,V=11;
  static constexpr const char *SPRITE[V]={
    "....XXXXXXX...",
    "..XXoooooooXX.",
    ".XoooXXXXXoooX",
    "XoooXoooooXooX",
    "XooXooooooooXX",
    "XooooXXXXXXooX",
    "XooooooooooooX",
    "XooooXXXXXXoXX",
    "XoooooooooooX.",
    ".XXooooooooX..",
    "...XXXXXXXX...",
  };
  const int px=dy!=0?1:0,py=dy!=0?0:1;  // across the punch
  // A ghost (fill drawn in the edge colour) is an outline only.
  const bool ghostly=fill.r==edge.r && fill.g==edge.g && fill.b==edge.b;
  const int ox=roundi(x),oy=roundi(y),kf=(k8*80)>>8;
  for(int v=0;v<V;++v)for(int u=0;u<U;++u) {
    const char c=SPRITE[v][u];if(c=='.')continue;
    const int bu=(u-U+1)*scale,bv=(v-V/2)*scale;
    for(int a=0;a<scale;++a)for(int b2=0;b2<scale;++b2) {
      const int sx=ox+(bu+a)*dx+(bv+b2)*px,sy=oy+(bu+a)*dy+(bv+b2)*py;
      if(c=='X')phantomAdd(sx,sy,edge,k8);
      else if(!ghostly)phantomAdd(sx,sy,fill,kf);
    }
  }
  if(!arm)return;
  // The forearm: two edges fading back from the wrist, and a cuff at it.
  const int len=14*scale;
  for(int side=0;side<2;++side) {
    const float v=(side?3.0f:-3.0f)*scale,bx=x+(.5f-U)*scale*dx+v*px,by=y+(.5f-U)*scale*dy+v*py;
    phantomLine(bx,by,bx-len*dx,by-len*dy,edge,(k8*200)>>8,0);
  }
  const float cx=x-(U+.5f)*scale*dx,cyy=y-(U+.5f)*scale*dy;
  phantomLine(cx-3*scale*px,cyy-3*scale*py,cx+3*scale*px,cyy+3*scale*py,inkOf(rgb8(255,206,90)),k8,k8);
}
// One of three abstract jagged marks (a bolt, a sawtooth column and a
// hooked fang; never a letter), or its mirror image (shape + 3), 16 px
// square (32 at scale 2) with its
// top-left at (x,y), drawn like heavy comic lettering: strokes about three
// pixels thick with a bright outline round them, a fill shading down to the
// darker colour and an extrusion down and to the right in it.
inline void Renderer::phantomGlyph(int x,int y,int shape,int scale,const Ink &edge,const Ink &fill,const Ink &shade,int k8) {
  if(k8<5)return;
  static constexpr int N=16;
  static constexpr const char *SHAPES[3][N]={
    {"......####......",
     ".....####.......",
     "....####...##...",
     "...####...###...",
     "..##########....",
     "..#########.....",
     ".......####.....",
     "......####......",
     ".....##########.",
     "....##########..",
     "....###...####..",
     "..........###...",
     ".........###....",
     "........###.....",
     "........##......",
     "................"},
    {"....######......",
     "....######......",
     "......####......",
     "....######...##.",
     "..#######...###.",
     "..####.....###..",
     "..########.##...",
     "....########....",
     "........####....",
     ".......####.....",
     "......########..",
     ".....########...",
     "..........##....",
     ".........##.....",
     "................",
     "................"},
    {"..##............",
     "..###......###..",
     "..####....####..",
     "...####..####...",
     "....########....",
     ".....######.....",
     "......####......",
     ".....####.......",
     "....#####.......",
     "...######.......",
     "..###.###.......",
     "..##...####.....",
     "........#####...",
     ".........####...",
     "...........##...",
     "................"}};
  // Shapes 3..5 are 0..2 mirrored left to right.
  const char *const *g=SHAPES[shape%3];const bool mirror=shape>=3;
  auto in=[&](int u,int v){return u>=0 && v>=0 && u<N && v<N && g[v][mirror?N-1-u:u]=='#';};
  const int ks=(k8*150)>>8,kf=(k8*150)>>8;
  for(int v=0;v<N;++v) {
    // The fill shades from its colour at the top to the shade at the foot.
    const Ink body=mixedInk(fill,shade,v*256/N);
    for(int u=0;u<N;++u) {
      const bool on=in(u,v);
      // The extrusion: just below and right of the mark, where it does not cover.
      if(!on){if(in(u-1,v-1))phantomAdd(x+u*scale+scale-1,y+v*scale+scale-1,shade,ks);continue;}
      const bool rim=!in(u-1,v) || !in(u+1,v) || !in(u,v-1) || !in(u,v+1);
      for(int a=0;a<scale;++a)for(int c=0;c<scale;++c)phantomAdd(x+u*scale+a,y+v*scale+c,rim?edge:body,rim?k8:kf);
    }
  }
}

// A left-pointing banner arrow with its tip at (x,y), `length` px long: a
// head 21 px tall and 14 long, a body 9 px tall with a notched tail. A
// bright rim and a solid fill shading from `top` to `bottom`, a lit row
// along its upper edge; `shine` (0..1 along it, negative for none) places
// a sweeping highlight.
inline void Renderer::phantomArrow(float x,float y,int length,const Ink &edge,const Ink &top,const Ink &bottom,int k8,float shine) {
  if(k8<5)return;
  constexpr int HEAD=14,HH=10,BH=4;
  const int ox=roundi(x),oy=roundi(y),kf=(k8*190)>>8;
  Ink rows[2*HH+1];
  for(int v=-HH;v<=HH;++v)rows[v+HH]=mixedInk(top,bottom,(v+BH)*256/(2*BH+1));
  for(int u=0;u<=length;++u) {
    const int h=u<HEAD?(u*HH+HEAD/2)/HEAD:BH,notch=u>length-5?u-(length-5):0;
    const float sh=shine>=0?1-std::fabs(u-shine*length)/8:0;
    const int boost=sh>0?int(sh*110):0;
    for(int v=-h;v<=h;++v) {
      if(notch && std::abs(v)<notch-1)continue;
      const bool rim=std::abs(v)==h || u==length || (u<HEAD && std::abs(v)>=h-1) || (u==HEAD && std::abs(v)>BH) || (notch && std::abs(v)<notch);
      if(rim)phantomAdd(ox+u,oy+v,edge,std::min(256,k8+boost));
      else phantomAdd(ox+u,oy+v,v==-h+1?edge:rows[std::max(-BH,std::min(BH,v))+HH],std::min(256,kf+boost));
    }
  }
}
// Tints every other row of the readout (rows of the given parity) toward
// `target` by k8, never below 3:1 on the background (sorcShade); the last
// colour is remembered, since the readout is drawn in one colour.
inline void Renderer::phantomTint(const Ink &target,int k8,int parity) {
  if(k8<=0)return;
  const Box &b=number_;uint16_t lastIn=bg_,lastOut=bg_;
  for(int y=b.y0+((b.y0^parity)&1);y<=b.y1;y+=2) {
    uint16_t *row=px_+y*w_;
    for(int x=b.x0;x<=b.x1;++x) {
      const uint16_t p=row[x];if(p==bg_)continue;
      if(p!=lastIn){const uint16_t out=store(sorcShade(load(p),target,k8));lastIn=p;lastOut=out==bg_?uint16_t(out^store(1)):out;}
      if(row[x]!=lastOut){row[x]=lastOut;++plots_;}
    }
  }
}

// The front-view fist's cells, listed at compile time: rim (X) and knuckle
// (K) cells first, so an after-image draws only those.
namespace phantomFistShape {
constexpr int W=24,H=21,KINDS=7;
enum Kind {RIMK=0,KNUCK=1,CREASE=2,THUMBRIM=3,THUMBK=4,WRISTK=5,FILL=6};
constexpr const char *FIST[H]={
  "...XXX...XXX...XXX..XXX.",
  "..XKKKX.XKKKX.XKKKXXKKKX",
  ".XooKoocooKoocooKoocoKoX",
  ".XooooocooooocooooocoooX",
  ".XooooocooooocooooocoooX",
  ".XcccccccccccccccccccccX",
  ".XooooocooooocooooocoooX",
  ".XooooocooooocooooocoooX",
  ".XooooocooooocooooocoooX",
  ".XcccccccccccccccccccccX",
  ".XTTTTTTTTTTTTTTTToooooX",
  ".XttttttttttttttTToooooX",
  ".XtttttttttttttttToooooX",
  ".XcccccccccccccccToooooX",
  ".XoooooooooooooooooooooX",
  "..XoooooooooooooooooooX.",
  "...XXXXXXXXXXXXXXXXXXX..",
  "......XwwwwwwwwwwwX.....",
  "......XwwwwwwwwwwwX.....",
  "......XwwwwwwwwwwwX.....",
  "......XwwwwwwwwwwwX....."};
constexpr int kindOf(char c){return c=='X'?RIMK:c=='K'?KNUCK:c=='c'?CREASE:c=='T'?THUMBRIM:c=='t'?THUMBK:c=='w'?WRISTK:FILL;}
struct Cells {int n=0,rims=0;uint8_t u[W*H]={},v[W*H]={},kind[W*H]={};};
constexpr Cells build() {
  Cells c{};
  for(int pass=0;pass<2;++pass)for(int v=0;v<H;++v)for(int u=0;u<W;++u) {
    const char ch=FIST[v][u];if(ch=='.')continue;
    const int k=kindOf(ch);const bool rim=k==RIMK || k==KNUCK;
    if(rim!=(pass==0))continue;
    c.u[c.n]=uint8_t(u);c.v[c.n]=uint8_t(v);c.kind[c.n]=uint8_t(k);++c.n;if(rim)++c.rims;
  }
  return c;
}
constexpr Cells CELLS=build();
}

// A clenched fist seen from the front, punching at the viewer, centred on
// (x,y): four knuckle bumps on top lit along their crowns, the folded
// fingers as two bands split by creases, the thumb folded across the front
// and a wrist below, about 24 px across, its fill shaded from a light
// violet at the knuckles to a deep one at the wrist. `mirror` puts the
// thumb on the other side; `scale` 2 doubles it. A ghost (an after-image)
// draws only its rim and knuckles, in `tint`.
inline void Renderer::phantomPunch(float x,float y,bool mirror,int scale,bool ghost,const Ink &tint,int k8) {
  if(k8<5)return;
  using namespace phantomFistShape;
  static constexpr Ink RIM=inkOf(rgb8(222,210,255)),KNUCKLE=inkOf(rgb8(255,250,255)),TOP=inkOf(rgb8(196,172,255));
  static constexpr Ink FOOT=inkOf(rgb8(118,86,226)),THUMB=inkOf(rgb8(226,212,255)),WRIST=inkOf(rgb8(108,78,206));
  const int ox=roundi(x)-W*scale/2,oy=roundi(y)-8*scale;
  // The cells are listed once at compile time (rim and knuckles first), so
  // a ghost visits only its rim and a fist only its own cells; colours and
  // weights are looked up per kind, the fill's shade per row.
  Ink body[H];for(int v=0;v<H;++v)body[v]=mixedInk(TOP,FOOT,v*256/16);
  const Ink inks[KINDS]={RIM,KNUCKLE,FOOT,KNUCKLE,THUMB,WRIST,TOP};
  const int ks[KINDS]={k8,k8,(k8*60)>>8,(k8*200)>>8,(k8*215)>>8,(k8*200)>>8,(k8*230)>>8};
  const int count=ghost?CELLS.rims:CELLS.n,kRim=(k8*170)>>8;
  for(int i=0;i<count;++i) {
    const int u=mirror?W-1-CELLS.u[i]:CELLS.u[i],v=CELLS.v[i],kind=CELLS.kind[i];
    const Ink &ink=ghost?tint:kind==FILL?body[v]:inks[kind];
    const int k=ghost?(kind==KNUCK?k8:kRim):ks[kind];
    if(scale==1)phantomAdd(ox+u,oy+v,ink,k);
    else for(int a=0;a<scale;++a)for(int d=0;d<scale;++d)phantomAdd(ox+u*scale+a,oy+v*scale+d,ink,k);
  }
}

// A jagged comic burst round (cx,cy): spikes out to about r1 between
// valleys at about r0, each a little uneven, stroked as a bold rim between
// angles fromA and toA only, with a soft hot centre.
inline void Renderer::phantomBurst(float cx,float cy,float r0,float r1,int spikes,float turn,const Ink &c,int k8,float fromA,float toA) {
  const float step=HALF_TURN/spikes;
  // Three nested outlines a pixel apart, dimmer inward: a bold rim with a
  // glow inside it.
  for(int row=0;row<3;++row) {
    const float in=row*1.1f;const int k=row==0?k8:row==1?(k8*150)>>8:(k8*80)>>8;
    float lx=0,ly=0;bool have=false;
    for(int i=0;i<=2*spikes;++i) {
      const float a=turn+i*step;const int j=i%(2*spikes);
      const float r=((j&1)?r0*(.85f+.3f*rnd(j,561)):r1*(.75f+.4f*rnd(j,562)))-in*((j&1)?1:2.2f);
      const float x=cx+r*cosine(a),y=cy+r*.8f*sine(a);
      float rel=a-fromA;while(rel<0)rel+=2*HALF_TURN;while(rel>=2*HALF_TURN)rel-=2*HALF_TURN;
      const bool on=rel<=toA-fromA;
      if(have && on)phantomLine(lx,ly,x,y,c,k,k);
      lx=x;ly=y;have=on;
    }
  }
  // A soft hot centre.
  const int rr=int(r0*.6f);
  for(int dy=-rr;dy<=rr;++dy)for(int dx=-rr;dx<=rr;++dx) {
    const int d2=dx*dx+dy*dy;if(d2>rr*rr)continue;
    phantomAdd(roundi(cx)+dx,roundi(cy)+dy,c,(k8*(rr*rr-d2)/(rr*rr+1))>>1);
  }
}

inline void Renderer::phantom(unsigned which,float t) {
  phantomInk_=phantomPaper();
  switch(which) {
    case 0:phantomTimestop(t);break;
    case 1:phantomZipper(t);break;
    case 2:phantomRush(t);break;
    case 3:phantomMenace(t);break;
    case 4:phantomSequel(t);break;
    default:break;
  }
}

// ---- Timestop: a violet ring closes on the readout, then a sphere of
// inverted light bursts out of it (a bright rim with pale light glowing
// inside it; on a light background the sphere is dark), holds and collapses
// back into the readout. Time has stopped: knives hang in an arc under the
// readout, each popping into place, and a clock face below ticks once every
// half second while frozen dust hangs in the air and a shine slides along
// each blade. Time resumes: colour floods back into the knives and they fly
// up at the readout, ringing off its edge in sparks, while the clock spins
// and fades.
inline void Renderer::phantomTimestop(float t) {
  constexpr float BURST=.4f,FULL=.78f,TURN=.9f,STOP=1.25f,RESUME=3.3f;
  constexpr int KNIVES=8,MOTES=7;
  const Box &b=number_;const float cx=b.cx(),cy=b.cy(),hw=(b.x1-b.x0)*.5f,hh=(b.y1-b.y0)*.5f;
  const Ink field=inkOf(rgb8(226,255,176)),violet=inkOf(rgb8(170,110,255));
  const Ink frozen=inkOf(rgb8(186,186,194)),steel=inkOf(rgb8(230,240,255)),gold=inkOf(rgb8(255,196,64));
  const Ink dial=inkOf(mixed(rgb8(236,214,150),accent_,.25f));
  // Anticipation: a violet ring closes in on the readout, accelerating,
  // with motes pulled in along with it.
  if(t<BURST) {
    const float u=t/BURST,s=1-u*u;
    phantomEllipse(cx,cy,hw+6+62*s,hh+6+46*s,violet,q8(.3f+.6f*u));
    for(unsigned i=0;i<10;++i) {
      const float a=2*HALF_TURN*(i+.4f*rnd(i,531))/10,d=1.15f+.5f*rnd(i,532)-.9f*u*u*(1+rnd(i,533)*.3f);
      if(d<.3f)continue;
      phantomDot(cx+(hw+6)*d*cosine(a),cy+(hh+6)*d*1.9f*sine(a),field,q8(.8f*smooth(u*3)));
    }
  }
  // The sphere: out fast past the foot of the screen, a held beat, then it
  // collapses back in, faster and faster. Its limb is a bright rim with a
  // band of pale glow inside it, drawn only where the rim is on the
  // screen, so a frame touches the rows round the visible arc and no more.
  if(t>=BURST && t<STOP) {
    const float s=t<FULL?easeOut((t-BURST)/(FULL-BURST)):t<TURN?1:1-std::pow((t-TURN)/(STOP-TURN),2.0f);
    const float breathe=t>=FULL && t<TURN?1.2f*sine((t-FULL)*25):0;
    const float rx=6+250*s+breathe,ry=4+216*s+breathe;
    const float kRim=t<FULL?1:t<TURN?.95f:.95f-.35f*(t-TURN)/(STOP-TURN);
    // A small sphere covers every row round the readout, so its glow band
    // fades in only once the visible arc is a band low on the screen.
    const float glow=kRim*smooth((ry-60)/40);
    const int n=int((rx+ry)*3.3f),depth=glow>.05f?9:0;const float step=2*HALF_TURN/n;
    const Ink rimCore=inkOf(rgb8(255,255,236));const int kc=q8(kRim),kf=q8(kRim*.75f),kv=q8(kRim*.5f);
    int glowK[9];for(int d=0;d<9;++d)glowK[d]=q8(glow*.62f*(1-d*(1.0f/9))*(1-d*(1.0f/12)));
    // Only the angles whose points can be on the screen are visited: once
    // the sphere is wider than the screen that is the arc round its foot
    // (the top of it is far above the screen). One table lookup and asin
    // per frame, then each point is a rotation of the last, and its samples
    // across the rim step in fixed point.
    int i0=0,i1=n-1;
    const float reach=std::max(cx,w_-1-cx)+5;
    if(rx>reach){const float h=std::asin(reach/rx);i0=int((HALF_TURN/2-h)/step);i1=int((HALF_TURN/2+h)/step)+1;}
    float ca=cosine(i0*step),sa=sine(i0*step);const float rc=cosine(step),rs=sine(step);
    for(int i=i0;i<=i1;++i) {
      const float x=cx+rx*ca,y=cy+ry*sa;
      if(y>=-4 && y<=bottom_+4 && x>=-4 && x<=w_+4) {
        const int x8=q8(x)+128,y8=q8(y)+128,ux=q8(ca),uy=q8(sa);
        phantomAdd(x8>>8,y8>>8,rimCore,kc);
        phantomAdd((x8-ux*8/5)>>8,(y8-uy*8/5)>>8,field,kf);
        phantomAdd((x8+ux*8/5)>>8,(y8+uy*8/5)>>8,violet,kv);
        // The glow inside the limb: the field's pale light fading smoothly
        // inward, one sample per pixel of depth.
        for(int d=0;d<depth;++d)phantomAdd((x8-ux*(d+3))>>8,(y8-uy*(d+3))>>8,field,glowK[d]);
      }
      const float c2=ca*rc-sa*rs;sa=sa*rc+ca*rs;ca=c2;
    }
    // Where the rim crosses the readout its glyphs flare toward white.
    if(ry<70)for(int y=b.y0;y<=b.y1;++y) {
      const float dy=(y-cy)/ry,q=1-dy*dy;if(q<=0)continue;
      const float half=rx*std::sqrt(q);
      for(int side=-1;side<=1;side+=2) {
        const int xc=roundi(cx+side*half);
        for(int x=xc-3;x<=xc+3;++x)if(x>=b.x0 && x<=b.x1)sorcHeat(x,y,WHITE_INK,q8(kRim*(1-std::abs(x-xc)*.22f)));
      }
    }
  }
  glint(t,FULL-.06f,.45f);
  // Knife guards sit on the lower half of an ellipse round the readout.
  const float krx=hw+4,kry=hh+44;
  // Time stopped: a few specks of dust hang among the knives, twinkling in
  // place; after the resume they fall and fade.
  if(t>=STOP-.05f && t<4.95f) {
    const float on=smooth((t-STOP+.05f)/.15f),fall=t>RESUME?t-RESUME:0,off=1-smooth((t-RESUME-.5f)/1.1f);
    for(unsigned i=0;i<MOTES;++i) {
      const float a=HALF_TURN*(.12f+.76f*(i+.5f)/MOTES),d=.6f+.5f*rnd(i,542);
      const float x=cx+krx*d*cosine(a),y=cy+kry*d*sine(a)+16*fall*fall*(.6f+rnd(i,543));
      const float k=on*off*(.4f+.35f*sine(t*(5+3*rnd(i,544))+i*1.7f));
      phantomDot(x,y,i%3?frozen:field,q8(k));
    }
  }
  // The clock: a bezel under the readout, the readout its face, with a
  // tick every 30 degrees; a pointer on the bezel steps one tick every
  // 0.45 s with a small overshoot, and spins on as time resumes.
  const float brx=hw+5,bry=hh+13;
  if(t>=STOP && t<4.1f) {
    const float draw=easeOut((t-STOP)/.3f),fade=1-smooth((t-RESUME-.15f)/.6f);
    phantomEllipse(cx,cy,brx,bry,dial,q8(.75f*fade),HALF_TURN/2*(1-draw),HALF_TURN*draw);
    phantomEllipse(cx,cy,brx+1,bry+1,dial,q8(.4f*fade),HALF_TURN/2*(1-draw),HALF_TURN*draw);
    for(int h=0;h<6;++h) {
      const float a=(h+.5f)*(HALF_TURN/6);if(std::fabs(a-HALF_TURN/2)>HALF_TURN/2*draw+.01f)continue;
      const float len=h==0 || h==5?5.0f:3.5f,ca=cosine(a),sa=sine(a);
      phantomLine(cx+(brx+2)*ca,cy+(bry+2)*sa,cx+(brx+1+len)*ca,cy+(bry+1+len)*sa,dial,q8(fade),q8(.7f*fade));
      phantomLine(cx+(brx+2)*ca-sa,cy+(bry+2)*sa+ca,cx+(brx+1+len)*ca-sa,cy+(bry+1+len)*sa+ca,dial,q8(.6f*fade),q8(.4f*fade));
    }
    float hand=0;
    if(t>=1.55f) {
      const float clock=std::min(t,RESUME)-1.55f;const int ticks=int(clock/.45f)+1;const float w=(clock-(ticks-1)*.45f)/.12f;
      // A quick step with a slight overshoot.
      const float v=std::min(w,1.0f)-1;hand=ticks-1+1+1.6f*v*v*v+.6f*v*v;
      if(t>RESUME){const float r=t-RESUME;hand+=14*r*r;}
    }
    const float a=HALF_TURN/6*(1.5f+hand),ca=cosine(a),sa=sine(a);
    if(sa>.2f) {
      const float px=cx+(brx+3)*ca,py=cy+(bry+3)*sa;
      // A bold wedge, widest at the bezel: colourless while time is stopped.
      const int k=q8(fade*draw);
      phantomLine(px,py,px+8*ca,py+8*sa,WHITE_INK,k,k);
      phantomLine(px-sa,py+ca,px+5*ca-.4f*sa,py+5*sa+.4f*ca,frozen,k,k>>1);
      phantomLine(px+sa,py-ca,px+5*ca+.4f*sa,py+5*sa-.4f*ca,frozen,k,k>>1);
      phantomDot(px+9*ca,py+9*sa,steel,k);
    }
  }
  // The knives, each aimed at the readout's centre.
  if(t>=STOP) {
    for(int i=0;i<KNIVES;++i) {
      const float an=HALF_TURN*(.17f+.66f*i/(KNIVES-1)),gx=cx+krx*cosine(an),gy=cy+kry*sine(an);
      float dx=cx-gx,dy=cy-gy;const float inv=1/std::sqrt(dx*dx+dy*dy);dx*=inv;dy*=inv;
      const int order=(i*4)%KNIVES;
      const float pop=t-STOP-.06f*order;if(pop<0)continue;
      if(t<RESUME) {
        // Pops into place with a flash, then hangs, a shine sliding along it now and then.
        const float flash=pop<.12f?1-pop/.12f:0;
        if(flash>0)phantomTwinkle(gx+dx*6,gy+dy*6,steel,q8(flash),3);
        const float cyc=(t-1.6f-.37f*order)/1.4f,shine=cyc-floori(cyc);
        phantomKnife(gx,gy,dx,dy,mixedInk(frozen,steel,q8(flash)),mixedInk(frozen,gold,q8(flash*.7f)),q8(.85f+.15f*flash),t>1.6f?shine*1.6f-.3f:-1);
      } else {
        // Flight: full colour floods back, the knife accelerates along its
        // aim and rings off the readout's edge.
        const float a=t-RESUME-.035f*order;
        const float tipY=gy+dy*15,edgeY=b.y1+4,stopAt=tipY>edgeY?(tipY-edgeY)/(-dy):0;
        const float d=a<0?0:300*a*a;
        if(d<stopAt) {
          const float col=smooth((t-RESUME)/.12f);
          phantomKnife(gx+dx*d,gy+dy*d,dx,dy,mixedInk(frozen,steel,q8(col)),mixedInk(frozen,gold,q8(col)),256,-1);
          if(d>2){const float tail=std::min(d,24.0f);
            phantomLine(gx+dx*(d-8),gy+dy*(d-8),gx+dx*(d-8-tail),gy+dy*(d-8-tail),steel,q8(.55f),0);}
        } else {
          const float hitT=std::sqrt(stopAt/300),age=a-hitT;
          if(age<.4f) {
            const float hx=gx+dx*(stopAt+15),hy=gy+dy*(stopAt+15)+1,k=1-age/.4f;
            phantomTwinkle(hx,hy,mixedInk(steel,inkOf(accent_),q8(age/.4f)),q8(k*1.1f),1+int(3*k));
            for(int j=0;j<3;++j){const float sa=(j-1)*.9f+.5f*rnd(i*3+j,545),v=30+25*rnd(i*3+j,546);
              phantomDot(hx+sa*v*age,hy+(14+v*.4f)*age+70*age*age,gold,q8(k*.8f));}
          }
        }
      }
    }
    // Time resumes: one faint ring rolls out of the readout.
    if(t>=RESUME && t<RESUME+.3f) {
      const float u=(t-RESUME)/.3f;
      phantomEllipse(cx,cy,hw+6+40*u,hh+8+40*u,field,q8(.5f*(1-u)),0,HALF_TURN);
    }
  }
}

// ---- Zipper: a pull runs across the readout's middle row stitching a
// closed zipper behind it, tugs, and runs back unzipping it: the readout's
// halves gape open like a mouth, the upper lip lifting a little and the lower
// one dropping further, onto a violet gap full of drifting stars. The lips
// breathe, then the pull zips it shut and the closed zipper slides down out
// of the readout to rest under it (exact from 2.05 s). There the pull's tab
// keeps swinging, a shine runs along the teeth and stray stars sift down;
// at the end the pull runs off to the left, taking the teeth with it.
inline void Renderer::phantomZipper(float t) {
  constexpr float STITCH0=.08f,STITCH1=.36f,OPEN0=.5f,OPEN1=.98f,SHUT0=1.42f,SHUT1=1.8f,DROP=2.02f,LEAVE=3.95f,GONE=4.65f;
  constexpr int A=9,STARS=18;
  const Box &b=number_;const float cy=b.cy();
  const int ym=roundi(cy),xl=b.x0-3,xr=b.x1+3,rest=b.y1+4;
  const bool paper=phantomPaper();
  // Where the pull is, and how far it has stitched or unzipped.
  float xs=xl;
  if(t<STITCH1)xs=xl+(xr-xl)*easeOut(clamp01((t-STITCH0)/(STITCH1-STITCH0)));
  else if(t<OPEN0)xs=xr+1.2f*sine((t-STITCH1)*50)*(1-(t-STITCH1)/(OPEN0-STITCH1));
  else if(t<OPEN1)xs=xr-(xr-xl)*smooth((t-OPEN0)/(OPEN1-OPEN0));
  else if(t<SHUT0)xs=xl;
  else if(t<SHUT1)xs=xl+(xr-xl)*smooth((t-SHUT0)/(SHUT1-SHUT0));
  else xs=xr;
  // The mouth: open between the pull and the right-hand stop.
  const bool open=t>=OPEN0 && t<SHUT1;
  const float span=xr-xs,amp=open?A*std::min(1.0f,span/60)*(t>=OPEN1 && t<SHUT0?1+.12f*sine((t-OPEN1)*14):1):0;
  if(t<DROP) {
    const uint16_t bgv=load(bg_);
    const int vr=paper?3:std::min(31,int(bgv>>11)+2),vg=paper?2:std::min(63,int((bgv>>5)&63)+1),vb=paper?8:std::min(31,int(bgv&31)+5);
    const uint16_t voidc=uint16_t(vr<<11|vg<<5|vb);
    for(int x=std::max(0,int(xs)+1);x<=std::min(w_-1,xr);++x) {
      if(amp<=0)break;
      const float sh=sine(HALF_TURN*(x-xs)/span),o=amp*sh*(1.5f-.5f*sh);
      const int u=roundi(o*.35f),d=roundi(o*.65f);
      if(u>0){for(int y=b.y0-u;y<=ym-1-u;++y)phantomPut(x,y,load(px_[(y+u)*w_+x]));for(int y=ym-u;y<=ym-1;++y)phantomPut(x,y,bgv);}
      if(d>0){for(int y=b.y1+d;y>=ym+d;--y)phantomPut(x,y,load(px_[(y-d)*w_+x]));for(int y=ym;y<ym+d;++y)phantomPut(x,y,bgv);}
      // The gap between the lips' teeth: the void, and stars drifting through it.
      for(int y=ym-u+1;y<=ym+d-1;++y)phantomPut(x,y,voidc);
    }
    if(amp>0)for(unsigned i=0;i<STARS;++i) {
      float x=b.x0+(b.x1-b.x0)*rnd(i,551)-t*(6+6*rnd(i,552));
      while(x<b.x0)x+=b.x1-b.x0;
      if(x<=xs+1 || x>=xr)continue;
      const float sh=sine(HALF_TURN*(x-xs)/span),o=amp*sh*(1.5f-.5f*sh);
      const int u=roundi(o*.35f),d=roundi(o*.65f),rows=u+d-1;if(rows<1)continue;
      const int ix=int(x),iy=ym-u+1+int(rnd(i,553)*rows);
      const float tw=.55f+.45f*sine(t*(9+7*rnd(i,554))+i*2.1f);
      const Ink c=i%4==0?inkOf(accent_):i%3==0?inkOf(rgb8(255,236,160)):WHITE_INK;
      const int k=q8(tw*(.85f+.3f*rnd(i,555)));
      auto star=[&](int px,int py,int kk){const unsigned v=load(px_[py*w_+px]);
        phantomPut(px,py,uint16_t(std::min(31,int(v>>11)+((c.r*kk)>>16))<<11|std::min(63,int((v>>5)&63)+((c.g*kk)>>16))<<5|std::min(31,int(v&31)+((c.b*kk)>>16))));};
      star(ix,iy,k);
      if(rows>=3 && i%3==0 && iy>ym-u+1 && iy<ym+d-1){star(ix-1,iy,k/3);star(ix+1,iy,k/3);star(ix,iy-1,k/3);star(ix,iy+1,k/3);}
    }
    // Teeth: closed behind the pull, parted along the lips of the mouth.
    const float drop=smooth((t-SHUT1)/(DROP-SHUT1));
    const int zy=ym+roundi((rest+1-ym)*drop);
    const int fadeIn=q8(smooth((t-STITCH0)/.08f));
    if(t<OPEN0)phantomTeeth(xl,int(xs)-4,zy-1,zy,fadeIn,true);
    else if(t<SHUT1) {
      phantomTeeth(xl,int(xs)-4,ym-1,ym,256,true);
      for(int x=std::max(xl,int(xs)-3);x<=xr;++x) {
        const float sh=x<=xs?0:sine(HALF_TURN*(x-xs)/span),o=amp*sh*(1.5f-.5f*sh);
        const int u=roundi(o*.35f),d=roundi(o*.65f);
        phantomTeeth(x,x,ym-1-u,ym+d,256,true);
      }
    } else phantomTeeth(xl,xr,zy-1,zy,256,true);
    // The pull rides on the teeth; its tab trails the way it moves.
    const float vel=t<STITCH1?1:t<OPEN0?0:t<OPEN1?-1:t<SHUT0?0:t<SHUT1?1:0;
    const float swing=-.5f*vel*(t<SHUT1?1:0)+(t>=STITCH1 && t<OPEN0?.25f*sine((t-STITCH1)*40):0)+(t>=OPEN1 && t<SHUT0?.3f*sine((t-OPEN1)*9)*(1-(t-OPEN1)/(SHUT0-OPEN1)):0);
    if(t>=STITCH0)phantomPull(t<SHUT1?xs:xr-2,t<SHUT1?ym:zy,swing,fadeIn,true);
  }
  glint(t,SHUT1-.05f,.3f);
  // Under the readout: the closed zipper rests, its tab swinging slowly, a
  // shine running along the teeth; at the end the pull runs off to the left
  // and the teeth go with it.
  if(t>=DROP && t<GONE) {
    const float leave=smooth((t-LEAVE)/(GONE-LEAVE)),px=xr-2-(xr-2-xl+12)*leave;
    const int k=q8(1-smooth((t-GONE+.2f)/.2f));
    phantomTeeth(xl,std::min(xr,int(px)-4),rest,rest+1,k,false);
    const float a=t-DROP,swing=.45f*std::exp(-1.4f*a)*sine(a*7.5f)+.12f*sine(a*2.6f)+(t>LEAVE?.5f*leave:0);
    phantomPull(px,rest+1,swing,k,false);
    // The shine: a bright head sweeping along the teeth every 0.9 s.
    const float cyc=(a+.2f)/.9f,head=xl+(xr-xl)*(cyc-floori(cyc))*1.3f;
    for(int x=int(head)-6;x<=int(head);++x)if(x<px-4)phantomAdd(x,rest+((x&3)<2?0:1),WHITE_INK,k*(7-(int(head)-x))/10);
  }
  // Stars that slipped out of the gap sift down and wink out.
  if(t>=1.0f && t<4.6f)for(unsigned i=0;i<10;++i) {
    const float birth=1.0f+2.6f*rnd(i,556),life=1.0f+.6f*rnd(i,557),age=t-birth;
    if(age<0 || age>=life)continue;
    const float u=age/life,x=b.x0+8+(b.x1-b.x0-16)*rnd(i,558)+4*sine(age*3+i),y=rest+5+30*age+8*age*age;
    phantomDot(x,y,i%3?WHITE_INK:inkOf(accent_),q8(sine(HALF_TURN*u)*(1.0f+.4f*sine(t*13+i*1.9f))));
  }
}

// ---- Rush: two fists draw back at the screen's edges, trembling, then a
// barrage: a jab every 50 ms from alternating sides, each a solid fist
// seen from the front on its forearm, shooting in under the readout with
// three fading after-images behind it, speed lines and a spark where it
// lands. A beat of stillness, then one big fist drives straight up from the
// foot of the screen into the readout's underside: a jagged burst, speed
// lines fanning out, the readout's lower rows flashing and a glint. The
// fist shudders and drops away; the burst's star hangs under the readout,
// pulsing and turning, slower echoes of the barrage jab in now and then,
// and debris falls.
inline void Renderer::phantomRush(float t) {
  constexpr float RUSH0=.32f,RUSH1=1.32f,JAB=.05f,LIFE=.12f,UPPER=1.48f,HIT=1.58f,DROPT=1.86f,END=4.8f;
  const Box &b=number_;const float cx=b.cx();
  const Ink edge=inkOf(rgb8(206,190,255)),fill=inkOf(rgb8(120,88,226)),ghost=inkOf(mixed(rgb8(150,120,255),accent_,.35f));
  const Ink speed=inkOf(mixed(WHITE,accent_,.2f)),hot=inkOf(rgb8(255,240,170));
  // The open triangles beside the replay's top corner, under the readout:
  // the fists stay there, clear of the frame's edges.
  const float band0=b.y1+13,band1=b.y1+20;
  // A jab's forearm: from the fist's lower half back to the screen's
  // edge, a lit rim along its top and bottom and a softer core, fading
  // out into speed lines.
  auto forearm=[&](float x,float y,int dir,int k) {
    const float x0=x-dir*10,x1=x0-dir*30;
    phantomLine(x0,y-1,x1,y-1,edge,k,0);phantomLine(x0,y+7,x1,y+7,edge,k,0);
    phantomLine(x0,y+2,x1,y+2,fill,(k*150)>>8,0);phantomLine(x0,y+4,x1,y+4,fill,(k*150)>>8,0);
    for(int l=0;l<3;++l){const float ly=y-6+7*l;phantomLine(x1,ly,x1-dir*(10+5*l),ly,speed,(k*170)>>8,0);}
  };
  // The stance: two fists at the edges, drawn back and trembling.
  if(t<RUSH0+.05f) {
    const float in=smooth(t/.15f)*(1-smooth((t-RUSH0)/.05f)),shake=sine(t*90);
    // The left one low, clear of the tilt arrow.
    phantomPunch(8+shake,band1,false,1,false,ghost,q8(.9f*in));
    phantomPunch(w_-9-shake,band0,true,1,false,ghost,q8(.9f*in));
  }
  // The barrage. Jab j starts at RUSH0+j*JAB from the left when j is even:
  // a fist on its forearm with three fading after-images trailing it.
  if(t>=RUSH0 && t<RUSH1+LIFE) {
    const int last=std::min(int((t-RUSH0)/JAB),int((RUSH1-RUSH0)/JAB));
    for(int j=std::max(0,last-int(LIFE/JAB));j<=last;++j) {
      const float age=t-RUSH0-j*JAB;if(age<0 || age>=LIFE)continue;
      const int dir=j&1?-1:1;
      const float y=band0+(band1-band0)*rnd(j,571),reach=30+10*rnd(j,572);
      // Out fast, a held instant, back.
      const float out=age<.05f?easeOut(age/.05f):age<.07f?1:1-smooth((age-.07f)/.05f);
      const float x=dir>0?reach*out+2:w_-3-reach*out,k=1-.5f*smooth((age-.07f)/.05f);
      if(age<.08f)for(int g=3;g>=1;--g)phantomPunch(x-dir*8*g,y,dir<0,1,true,ghost,q8(k*(.55f-.15f*g)));
      forearm(x,y,dir,q8(k*.9f));
      phantomPunch(x,y,dir<0,1,false,ghost,q8(k));
      if(age>=.045f && age<.09f)phantomTwinkle(x+dir*12,y-4,hot,q8(1-(age-.045f)/.045f),3);
    }
  }
  // The uppercut: up from below the screen in 0.1 s, a shudder, then down.
  if(t>=UPPER && t<DROPT+.3f) {
    const float top=b.y1+4;
    float y;
    if(t<HIT)y=h_+30-(h_+30-top)*smooth((t-UPPER)/(HIT-UPPER));
    else if(t<DROPT)y=top+1.2f*std::fabs(sine((t-HIT)*60))*(1-(t-HIT)/(DROPT-HIT));
    else y=top+260*(t-DROPT)*(t-DROPT);
    const float k=t<DROPT?1:1-smooth((t-DROPT)/.3f);
    phantomFist(cx+22,y,0,-1,2,edge,fill,q8(k),true);
    if(t<HIT)for(int l=-2;l<=2;++l)if(l)phantomLine(cx+22+l*7,y+26,cx+22+l*7,y+26+40,speed,q8(.8f),0);
  }
  // The impact: a jagged burst under the readout, fanning speed lines, the
  // readout's lower rows flashing near the blow, a glint.
  // Off the centre line, where the replay's frame has a vertical edge.
  const float impact=t-HIT,ix=cx+22,iy=b.y1+4;
  if(impact>=0 && impact<.3f) {
    const float u=impact/.3f,k=1-u*u;
    phantomBurst(ix,iy,10+10*u,24+16*u,9,.2f,hot,q8(k),0,HALF_TURN);
    for(int l=0;l<7;++l){const float a=HALF_TURN*(.04f+.92f*(l+.5f)/7),r=26+60*easeOut(u)+10*rnd(l,574);
      phantomLine(ix+r*cosine(a),iy+r*.8f*sine(a),ix+(r+12+10*rnd(l,575))*cosine(a),iy+(r+12)*.8f*sine(a),speed,q8(k),q8(k*.3f));}
    if(impact<.22f) {
      const int heat=q8(1-impact/.22f);
      for(int y=b.y1-4;y<=b.y1;++y)for(int x=std::max(b.x0,int(ix)-28);x<=std::min(b.x1,int(ix)+28);++x)
        sorcHeat(x,y,hot,(heat*(256-std::abs(x-int(ix))*9))>>8);
    }
  }
  glint(t,HIT+.1f,.45f);
  // Sustain: the burst lingers, smaller, turning and pulsing; debris falls.
  const float fade=1-smooth((t-3.9f)/(END-3.9f-.1f));
  if(impact>=.3f && fade>0) {
    const float a=impact-.3f,pulse=.75f+.25f*sine(a*7),grow=smooth(a/.15f);
    // Its star hangs under the blow, turning and pulsing.
    phantomBurst(ix,iy+9,7+2*pulse,15+4*pulse,8,.4f+a*.6f,mixedInk(hot,inkOf(accent_),76),q8(.85f*fade*grow),0,2*HALF_TURN);
    for(unsigned i=0;i<12;++i) {
      const float birth=.25f+2.6f*rnd(i,576),age=impact-birth,life=1.3f+.5f*rnd(i,577);
      if(age<0 || age>=life)continue;
      const float an=HALF_TURN*(.1f+.8f*rnd(i,578)),v=24+30*rnd(i,579);
      const float x=ix+cosine(an)*v*age,y=iy+4+sine(an)*v*.6f*age+30*age*age;
      phantomDot(x,y,i%2?hot:speed,q8(fade*(1-age/life)*(.8f+.3f*sine(t*20+i))));
    }
  }
  // Echoes of the barrage: a slower, fainter jab now and then from
  // alternating sides, each with its after-images.
  static constexpr float ECHO[]={2.15f,2.6f,3.0f,3.45f};
  for(int e=0;e<4;++e) {
    const float age=t-ECHO[e],life=.4f;if(age<0 || age>=life)continue;
    const int dir=e&1?-1:1;const float y=band0+(band1-band0)*rnd(e,581),reach=28+8*rnd(e,582);
    const float out=age<.12f?easeOut(age/.12f):1-smooth((age-.12f)/(life-.12f));
    const float x=dir>0?2+reach*out:w_-3-reach*out,k=.65f*(1-.6f*smooth((age-.12f)/(life-.12f)))*fade;
    if(age<.15f)for(int g=2;g>=1;--g)phantomPunch(x-dir*8*g,y,dir<0,1,true,ghost,q8(k*(.5f-.15f*g)));
    forearm(x,y,dir,q8(k*.9f));
    phantomPunch(x,y,dir<0,1,false,ghost,q8(k));
  }
}

// ---- Menace: heavy jagged violet marks (abstract, never letters) fade in
// one by one in two stacks flanking the replay under the readout,
// tremble, and climb slowly toward the readout, throbbing on a beat that
// quickens as the tension builds. Then the hard flash: the stacks flare
// white and vanishes as two big marks slam down into the band under the
// readout white-hot, focus lines streak in from the edges and a glint
// crosses the readout. After it the big pair holds
// there, trembling on a slow throb, while one small mark a side climbs
// again, until they all fade.
inline void Renderer::phantomMenace(float t) {
  constexpr float FLASH=1.55f,END=4.8f;constexpr int MARKS=6;
  static constexpr float BEATS[]={.45f,.8f,1.06f,1.25f,1.38f,1.47f};
  const Box &b=number_;
  const Ink edge=inkOf(rgb8(222,96,255)),edgeAlt=inkOf(mixed(rgb8(222,96,255),accent_,.35f)),fill=inkOf(rgb8(150,50,220)),shade=inkOf(rgb8(70,8,110));
  // The beat: a throb after each beat, then a slow one every 0.6 s.
  float throb=0;
  for(float at:BEATS)if(t>=at && t<at+.2f)throb=std::max(throb,1-(t-at)/.2f);
  if(t>FLASH+.3f){const float c=(t-FLASH-.3f)/.6f,f=c-floori(c);throb=.6f*std::max(0.0f,1-f/.35f);}
  const float flash=t>=FLASH && t<FLASH+.25f?1-(t-FLASH)/.25f:0;
  const float fade=1-smooth((t-(END-.8f))/.7f);
  const uint32_t frame=uint32_t(t*20);
  // Climb: slow at first, faster as the tension builds, slower again after.
  const float climb=t<FLASH?6*t+5*t*t:6*FLASH+5*FLASH*FLASH+11*(t-FLASH);
  const float top=b.y1+6,bottom=std::min(float(bottom_-60),b.y1+104.0f);
  for(int i=0;i<MARKS;++i) {
    const int side=i&1;
    const float slot=(i>>1)/3.0f,span=bottom-top,yy=bottom-std::fmod(slot*span+climb,span);
    const float rise=(bottom-yy)/span;  // 0 at the foot of the climb, 1 at the top
    // Hugging the screen's sides beside the replay's frame, stepping in
    // above its top corners (and clear of the tilt arrow).
    const float in=17*smooth((rise-.62f)/.3f);
    const float xx=side?w_-17-in:1+in;
    const float appear=.1f+.12f*i;
    if(t<appear)continue;
    // Each mark fades in where it spawns, out as it nears the big pair. At
    // the flash the stacks are swept away; one mark a side comes back.
    const float swept=t<FLASH?1:i<2?smooth((t-FLASH-.35f)/.3f):0;
    const float life=smooth((t-appear)/.15f)*smooth((1-rise)/.3f)*smooth(rise/.12f+(t<FLASH?1:0))*fade*swept;
    if(life<=.02f)continue;
    const int jx=rnd(frame*16+i,591)<.3f?(rnd(frame*16+i,592)<.5f?-1:1):0,jy=rnd(frame*16+i,593)<.2f?1:0;
    const float k=life*(.6f+.4f*throb)*(1+.3f*flash);
    const Ink e=flash>0?mixedInk(edge,WHITE_INK,q8(flash)):i%3==0?edgeAlt:edge;
    phantomGlyph(roundi(xx)+jx,roundi(yy)+jy,i*7%3,1,e,flash>0?mixedInk(fill,WHITE_INK,q8(flash*.7f)):fill,shade,q8(std::min(1.0f,k)));
  }
  // The peak: two big marks slam down into the band under the readout,
  // white-hot, then hold there trembling and throbbing until the end.
  if(t>=FLASH) {
    const float a=t-FLASH,drop=a<.08f?1-a/.08f:0,k=fade*std::min(1.0f,(.75f+.25f*throb)*(1+.4f*flash));
    for(int m=0;m<2;++m) {
      const int jx=rnd(frame*4+m,596)<.35f?(rnd(frame*4+m,597)<.5f?-1:1):0;
      const int x=roundi(b.cx())+(m?2:-34)+jx,y=roundi(b.y1+5-10*drop);
      const Ink e=mixedInk(m?edgeAlt:edge,WHITE_INK,q8(flash)),f=mixedInk(fill,WHITE_INK,q8(flash*.8f));
      phantomGlyph(x,y,m?3:0,2,e,f,shade,q8(k));
    }
  }
  // Focus lines streak in from both edges along the band under the
  // readout as the pair lands, converging on its centre.
  if(flash>0) {
    const float fx=b.cx(),fy=b.y1+2;
    for(int l=0;l<10;++l) {
      const int side=l&1?-1:1;const float y0=b.y1+6+30*rnd(l,594),x0=side>0?0:w_-1;
      const float len=(22+16*rnd(l,595))*(.5f+.5f*flash),dx=fx-x0,dy=fy-y0,inv=len/std::sqrt(dx*dx+dy*dy);
      phantomLine(x0,y0,x0+dx*inv,y0+dy*inv,WHITE_INK,q8(flash),0);
      phantomLine(x0,y0+1,x0+dx*inv*.7f,y0+1+dy*inv*.7f,WHITE_INK,q8(flash*.45f),0);
    }
  }
  glint(t,FLASH,.4f);
}

// ---- Sequel: a burst of streaks and sparks erupts over the replay, and
// is caught mid-flight in a freeze frame: a white flash and a jolt, then
// every frozen streak and spark takes a warm sepia and the readout is tinted
// sepia too (never below 3:1), its colour coming back every other row
// first, like a paused frame's interlace. A big arrow banner pointing left
// slides in from the right into the band under the readout as the colour
// returns, and the frozen frame holds, grain and scratches flickering round
// the burst and a shine sweeping along the banner, until it slides off to
// the left and the scene fades.
inline void Renderer::phantomSequel(float t) {
  constexpr float FREEZE=.55f,INTER=.95f,TINT1=1.3f,SLIDE0=1.3f,SLIDE1=1.7f,LEAVE=4.0f,END=4.8f;
  constexpr int RAYS=9,SPARKS=8;
  const Box &b=number_;const float cx=b.cx(),ox=cx+6,oy=b.y1+50;
  const bool paper=phantomPaper();
  const Ink sepia=inkOf(rgb8(236,190,120)),sepiaDeep=inkOf(rgb8(176,110,52)),cream=inkOf(rgb8(255,236,190));
  const Ink live=inkOf(mixed(WHITE,accent_,.3f)),hot=inkOf(rgb8(255,226,140));
  const float ft=std::min(t,FREEZE);  // time stops at the freeze
  const float toSepia=smooth((t-FREEZE)/.12f),fade=1-smooth((t-LEAVE)/(END-LEAVE));
  const int jolt=t>=FREEZE && t<FREEZE+.1f?2:0;
  // The burst: rays shooting out of a point under the readout, sparks flung.
  if(fade>0) {
    const float u=ft/FREEZE,flash=t>=FREEZE && t<FREEZE+.08f?1-(t-FREEZE)/.08f:0;
    for(int r=0;r<RAYS;++r) {
      const float a=2*HALF_TURN*(r+.5f*rnd(r,601))/RAYS,ca=cosine(a),sa=sine(a)*.5f;
      const float len=(10+16*rnd(r,602))*easeOut(u),r0=5+22*u*u*(.5f+rnd(r,603));
      const Ink c=mixedInk(mixedInk(live,sepia,q8(toSepia)),WHITE_INK,q8(flash));
      // A bright core with a softer row beside it.
      const int k0=q8(fade*(.95f-.2f*toSepia)),k1=q8(fade*.2f);
      phantomLine(ox+ca*r0+jolt,oy+sa*r0,ox+ca*(r0+len)+jolt,oy+sa*(r0+len),c,k0,k1);
      phantomLine(ox+ca*r0+jolt-sa,oy+sa*r0+ca*.6f,ox+ca*(r0+len)+jolt-sa,oy+sa*(r0+len)+ca*.6f,c,k0>>1,k1>>1);
    }
    for(int i=0;i<SPARKS;++i) {
      const float a=2*HALF_TURN*rnd(i,604),v=50+40*rnd(i,605),d=v*ft*(1-.3f*ft);
      const float x=ox+cosine(a)*d,y=oy+sine(a)*d*.5f+20*ft*ft;
      const Ink c=mixedInk(mixedInk(i%3?hot:live,i%2?sepia:sepiaDeep,q8(toSepia)),WHITE_INK,q8(flash));
      phantomDot(x+jolt,y,c,q8(fade*(t<FREEZE?.9f:.75f+.2f*sine(t*3+i))));
    }
  }
  // The freeze: the readout's tint and a glint.
  if(t>=FREEZE && t<TINT1) {
    const float k=smooth((t-FREEZE)/.1f)*(1-smooth((t-TINT1+.25f)/.25f));
    // The whole readout at first; then, as the banner arrives, every other
    // row lets its colour back, like a paused frame's interlace, and the
    // rest follow.
    const Ink target=paper?inkOf(rgb8(96,58,20)):inkOf(rgb8(240,184,104));
    if(t<INTER)shadeAnchor(target,q8(.9f*k),false);
    else phantomTint(target,q8(.9f*k),0);
  }
  glint(t,INTER,.4f);
  if(t<FREEZE)return;
  // Grain and scratches flicker round the frozen burst.
  const float frame=smooth((t-INTER)/.2f)*fade;
  if(frame>0) {
    const uint32_t f=uint32_t(t*20);
    for(int g=0;g<6;++g){const uint32_t sd=f*32+g;
      phantomAdd(int(ox-40+80*rnd(sd,606)),int(oy-20+44*rnd(sd,607)),g&1?sepia:cream,q8(frame*(.35f+.4f*rnd(sd,608))));}
    if(rnd(f,609)<.6f){const float sx=ox-40+80*rnd(f,610),sy=oy-16+20*rnd(f,611);
      phantomLine(sx,sy,sx+1,sy+14+12*rnd(f,612),sepia,q8(frame*.45f),q8(frame*.15f));}
  }
  // The banner: slides in from the right into the band under the readout,
  // stops with a small overshoot, holds with a shine sweeping along it,
  // and slides off to the left at the end.
  if(t>=SLIDE0 && t<END) {
    const int length=w_-30;const float home=16,y=b.y1+14;  // the tip clear of the tilt arrow
    float x;
    if(t<SLIDE1){const float u=(t-SLIDE0)/(SLIDE1-SLIDE0);x=home+(w_+4-home)*(1-easeOut(u))-4*sine(HALF_TURN*u)*u;}
    else if(t<LEAVE)x=home;
    else x=home-(length+12)*smooth((t-LEAVE)/(END-LEAVE));
    const float cyc=(t-SLIDE1-.2f)/1.2f,shine=t>SLIDE1+.2f && t<LEAVE?(cyc-floori(cyc))*1.4f-.2f:-1;
    phantomArrow(x,y,length,cream,sepia,sepiaDeep,q8(fade),shine);
  }
}

}
