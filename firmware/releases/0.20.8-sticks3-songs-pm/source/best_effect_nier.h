#pragma once
// NieR:Automata-inspired celebrations (Breach..Glitch): Renderer members declared in
// best_effect_ui.h, which includes this file after the class. Their shared look is
// the game's: parchment-beige light on black, clean geometric lines and small
// squares, orange for the shots, and horizontal glitch tears.
// Breach..Barrage only add light outside the UI halo (plus the glint). Glitch also
// tears the readout: it rotates bands of the readout's rows sideways inside the
// readout's own box, keeping it readable on every frame (thin bands moved 1-3 px,
// glyph edges tinted), and gives every pixel back exactly by 1.56 s of its 4.8 s.
// Cost: strokes step one pixel at a time with integer or incremental
// coordinates (no table trigonometry per pixel), particles are single pixels,
// small squares or dots, and every effect stays in the rows near its subject.
namespace BestEffectUI {

inline void Renderer::nier(unsigned which,float t) {
  switch(which) {
    case 0:nierBreach(t);break;
    case 1:nierFlurry(t);break;
    case 2:nierMoon(t);break;
    case 3:nierBarrage(t);break;
    case 4:nierGlitch(t);break;
    default:break;
  }
}

// addw for strokes that cross mask words. scanWord reads HALO px past each
// edge of its word, so a word first built after this frame's decoration has
// landed within HALO px of it would take that light for UI ink; addw itself
// builds the neighbouring word of the row first when a point lands that
// close to an edge, so this no longer primes the neighbour's whole halo
// (four more rows of scans, the same pixels).
BEST_EFFECT_INLINE void Renderer::nierAdd(int x,int y,const Ink &c,int w){addw(x,y,c,w);}
// An axis-aligned ellipse at about one point per pixel, by incremental rotation.
inline void Renderer::nierRing(float cx,float cy,float rx,float ry,const Ink &c,int kq) {
  if(kq<5 || rx<.5f)return;
  const int n=std::max(12,int((rx+ry)*3.2f));const float da=2*HALF_TURN/n,cd=cosine(da),sd=sine(da);
  float co=1,si=0;
  for(int i=0;i<n;++i){nierAdd(roundi(cx+rx*co),roundi(cy+ry*si),c,kq);const float nc=co*cd-si*sd;si=si*cd+co*sd;co=nc;}
}
// A small glowing sphere: a disc of radius ~2.2 with a hot centre.
inline void Renderer::nierOrb(float x,float y,const Ink &c,int kq) {
  const int ix=roundi(x),iy=roundi(y);const Ink hot=mixedInk(c,WHITE_INK,150);
  for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
    const int d=dx*dx+dy*dy;if(d>5)continue;
    nierAdd(ix+dx,iy+dy,d<=1?hot:c,d<=1?kq:d<=4?kq*13/16:kq/2);
  }
}
// A square of side 2*half+1 on whole pixels: filled, or only its outline.
inline void Renderer::nierSquare(int x,int y,int half,const Ink &c,int kq,bool filled) {
  for(int dy=-half;dy<=half;++dy)for(int dx=-half;dx<=half;++dx)
    if(filled || dx==-half || dx==half || dy==-half || dy==half)nierAdd(x+dx,y+dy,c,kq);
}
// A one-pixel line in Q8 steps, drawn only on rows yMin..yMax.
inline void Renderer::nierLine(int x0,int y0,int x1,int y1,const Ink &c,int kq,int yMin,int yMax) {
  const int n=std::max(1,std::max(std::abs(x1-x0),std::abs(y1-y0)));
  const int sx=(x1-x0)*256/n,sy=(y1-y0)*256/n;
  int x=x0*256+128,y=y0*256+128;
  for(int i=0;i<=n;++i,x+=sx,y+=sy){const int py=y>>8;if(py>=yMin && py<=yMax)nierAdd(x>>8,py,c,kq);}
}

// ---- Breach: a hacking dive. A wireframe arena scans in under the readout, a
// small white ship strafes and pours orange square shots into a core and its
// ring of shield blocks; the blocks break one by one, the core collapses and a
// flattened diamond of light bursts out of it ("hack complete"). The hacked
// space then lives on: the ship idles, data squares rise off the pulsing
// floor and a slow scan passes, until the arena scans away.
inline void Renderer::nierBreach(float t) {
  constexpr float FIRE=.5f,CEASE=1.3f,BREAK=1.38f,SHOT=1/18.0f,SPEED=240,HUSH=3.9f,ERASE=4.2f;
  constexpr int BLOCKS=6;
  const Rgb sepia=rgb8(226,212,176),orange=rgb8(255,128,32);
  const Ink sepiaInk=inkOf(sepia),orangeInk=inkOf(orange),hotInk=inkOf(rgb8(255,214,160)),paleInk=inkOf(mixed(sepia,WHITE,.5f));
  const float cx=number_.cx();
  const int top=number_.y1+6,floorY=std::min(bottom_-2,number_.y1+58);
  const float coreY=number_.y1+13.0f,shipY=floorY-8.0f;
  glint(t,BREAK-.02f,.45f);
  // The arena: a floor in perspective, revealed by a scanline sweeping down
  // and removed by one sweeping back up; it flashes as the core breaks.
  {
    const float in=top-2+(floorY+3-top)*easeOut(t/.3f),out=floorY+2-(floorY+5-top)*smooth((t-ERASE)/.4f);
    const int yMax=int(std::min(in,out));
    const float flash=t>=BREAK?.6f*(1-smooth((t-BREAK)/.35f)):0;
    // Through the sustain the floor dims a little and pulses slowly.
    const float idle=smooth((t-1.8f)/.4f),level=.36f+idle*(.3f+.07f*sine((t-1.8f)*2.1f)-.36f);
    const int kq=q8(level+flash);
    const int half0=40,half1=int(w_*.5f)-4,icx=roundi(cx);
    if(yMax>=top) {
      nierLine(icx-half0,top,icx+half0,top,sepiaInk,kq,top,yMax);
      nierLine(icx-half1,floorY,icx+half1,floorY,sepiaInk,kq,top,yMax);
      nierLine(icx-half0,top,icx-half1,floorY,sepiaInk,kq,top,yMax);
      nierLine(icx+half0,top,icx+half1,floorY,sepiaInk,kq,top,yMax);
      for(float f:{.36f,.7f}) {
        const int y=top+int((floorY-top)*f),hw=half0+int((half1-half0)*f);
        nierLine(icx-hw,y,icx+hw,y,sepiaInk,kq*3/5,top,yMax);
      }
      // Corner marks: the arena's four corners as small squares.
      const int corners[4][2]={{icx-half0,top},{icx+half0,top},{icx-half1,floorY},{icx+half1,floorY}};
      for(const auto &c:corners)if(c[1]<=yMax)nierSquare(c[0],c[1],1,sepiaInk,std::min(256,kq*2),false);
    }
    // The scanlines themselves: a bright full-width row with a faint wake.
    if(t<.36f){const int y=roundi(in),k=q8(.85f*(1-smooth((t-.24f)/.12f)));for(int x=0;x<w_;++x){nierAdd(x,y,paleInk,k);nierAdd(x,y-2,sepiaInk,k>>2);}}
    // One slow, faint scan crosses the floor in the sustain.
    if(t>2.4f && t<3.4f){const float u=(t-2.4f)/1.0f;const int y=top+roundi((floorY-top)*u),k=q8(.3f*sine(HALF_TURN*u));for(int x=0;x<w_;++x)nierAdd(x,y,sepiaInk,k);}
    if(t>ERASE && t<ERASE+.42f){const int y=roundi(out),k=q8(.7f*(1-smooth((t-ERASE-.3f)/.12f)));for(int x=0;x<w_;++x){nierAdd(x,y,paleInk,k);nierAdd(x,y+2,sepiaInk,k>>2);}}
  }
  // The ship: strafing wide while it fires, then weaving close under the
  // core (just off the screen's centre line) for the last shots.
  auto shipX=[&](float s){const float close=smooth((std::min(s,1.3f)-1.0f)/.25f);return cx+(38-30*close)*sine(2*HALF_TURN*.8f*(std::min(s,1.3f)-.3f));};
  if(t>=.28f && t<HUSH+.4f) {
    // After the break it eases into an idle drift (two unrelated periods).
    const float b=smooth((t-1.4f)/.5f),u=std::max(0.0f,t-1.4f);
    const float x=shipX(t)*(1-b)+(cx+24*sine(.8f*u)+6*sine(2.1f*u))*b;
    const float y=floorY+8-(floorY+8-shipY)*easeOut((t-.28f)/.22f)-12*smooth(u/.6f)+2.5f*sine(1.6f*u)*b;
    const float k=std::min(1.0f,(t-.28f)*12)*(1-smooth((t-HUSH)/.35f));
    const int ix=roundi(x),iy=roundi(y),kq=q8(k);
    // A solid white arrowhead with a notched tail.
    for(int r=0;r<9;++r) {
      const int half=(r*5+4)/8;
      for(int d=-half;d<=half;++d)if(r<6 || std::abs(d)>=r-4)nierAdd(ix+d,iy-5+r,WHITE_INK,kq);
    }
    if(t<CEASE+.1f)nierAdd(ix,iy+4,orangeInk,(kq*(150+100*(int(t*40)&1)))>>8);
  }
  // Orange square shots fly up from the ship's nose and burst on the ring or
  // the core; shots beside the ring fly on to the arena's far edge.
  for(int j=0;FIRE+j*SHOT<CEASE;++j) {
    const float tf=FIRE+j*SHOT,age=t-tf;if(age<0)break;
    const float x0=shipX(tf),dx=std::fabs(x0-cx);
    const float hitY=dx<34?coreY+8*std::sqrt(1-dx*dx/1156)+3:float(top);
    const float y=shipY-6-SPEED*age,hitAge=(shipY-6-hitY)/SPEED;
    const int ix=roundi(x0);
    if(age<hitAge){const int iy=roundi(y);nierSquare(ix,iy,1,orangeInk,240,true);nierAdd(ix,iy,hotInk,200);nierAdd(ix,iy+3,orangeInk,90);}
    else if(age<hitAge+.06f && dx<34)nierSquare(ix,roundi(hitY),2,hotInk,q8(1-(age-hitAge)/.06f),false);
  }
  // The core: a wireframe sphere (rim, equator and a turning meridian) that
  // flickers white once its shields are down, and collapses at BREAK.
  if(t>=.2f && t<BREAK+.08f) {
    const float appear=smooth((t-.2f)/.2f),collapse=t<BREAK?1:1-(t-BREAK)/.08f;
    const bool exposed=t>.62f+.105f*BLOCKS;
    const float R=8*collapse*(.4f+.6f*appear),k=appear*(exposed?(int(t*40)&1?1.0f:.6f):.9f);
    nierRing(cx,coreY,R,R,exposed?WHITE_INK:paleInk,q8(k));
    nierRing(cx,coreY,R,R*.35f,sepiaInk,q8(k*.55f));
    nierRing(cx,coreY,R*std::fabs(cosine(t*3)),R,sepiaInk,q8(k*.5f));
    nierAdd(roundi(cx),roundi(coreY),WHITE_INK,q8(k*.8f));
  }
  // Shield blocks circle the core and break one by one into fragments.
  for(int i=0;i<BLOCKS;++i) {
    const float dies=.62f+.105f*i,a=i*(2*HALF_TURN/BLOCKS)+1.8f*std::min(t,dies),ca=cosine(a),sa=sine(a);
    const float x=cx+32*ca,y=coreY+7*sa;
    if(t<dies) {
      const float k=smooth((t-.25f-.03f*i)/.15f)*(sa<0?.55f:1.0f);
      if(k<=0)continue;
      nierSquare(roundi(x),roundi(y),2,sepiaInk,q8(k*.9f),false);nierAdd(roundi(x),roundi(y),WHITE_INK,q8(k));
      continue;
    }
    const float age=t-dies;
    if(age<.05f){nierSquare(roundi(x),roundi(y),2,WHITE_INK,256,true);continue;}
    if(age>=.45f)continue;
    for(int f=0;f<4;++f) {
      const float fa=a+f*(HALF_TURN/2)+.4f,v=34+20*rnd(i*4+f,402),k=1-age/.45f;
      dot(x+cosine(fa)*v*age,y+sine(fa)*v*age*.6f+30*age*age,f%2?sepia:orange,k);
    }
  }
  // Bullet-hell return fire: two rings of dark orbs with pale rims.
  for(int ringNo=0;ringNo<2;++ringNo) {
    const float age=t-(ringNo?1.05f:.78f);if(age<0 || age>=.55f)continue;
    const int kq=q8(.85f*(1-age/.55f));
    for(int b=0;b<10;++b) {
      const float a=b*(2*HALF_TURN/10)+ringNo*.31f,d=6+60*age;
      const int x=roundi(cx+cosine(a)*d),y=roundi(coreY+sine(a)*d*.55f);
      if(y<=top+1 || y>=floorY-1)continue;
      nierAdd(x-1,y,paleInk,kq);nierAdd(x+1,y,paleInk,kq);nierAdd(x,y-1,paleInk,kq);nierAdd(x,y+1,paleInk,kq);
    }
  }
  if(t<BREAK)return;
  // Hack complete: two flattened diamonds of light expand from the core and
  // white and orange square shards scatter.
  const float age=t-BREAK;
  for(int d=0;d<2;++d) {
    const float u=(age-.08f*d)/.45f;if(u<=0 || u>=1)continue;
    const int sx=int(6+76*easeOut(u)),sy=sx/2,icx=roundi(cx),icy=roundi(coreY);
    const Ink c=mixedInk(WHITE_INK,sepiaInk,int(u*256));const int kq=q8((1-u)*(1-u)*(d?.55f:1.0f));
    // Inside the arena only: a shock across its floor.
    nierLine(icx-sx,icy,icx,icy-sy,c,kq,top,floorY);nierLine(icx,icy-sy,icx+sx,icy,c,kq,top,floorY);
    nierLine(icx+sx,icy,icx,icy+sy,c,kq,top,floorY);nierLine(icx,icy+sy,icx-sx,icy,c,kq,top,floorY);
  }
  if(age<.08f)spark(cx,coreY,sepia,1.4f*(1-age/.08f),2.5f);
  for(int i=0;i<16;++i) {
    const float life=.5f+.35f*rnd(i,411);if(age>=life)continue;
    const float a=2*HALF_TURN*(i+.5f*rnd(i,412))/16,v=45+75*rnd(i,413),d=v*(1-std::exp(-3*age))/3;
    const int x=roundi(cx+cosine(a)*d),y=roundi(coreY+sine(a)*d*.6f+12*age*age);
    const int kq=q8(1-age/life);
    if(i%3==0)nierSquare(x,y,1,i%2?orangeInk:WHITE_INK,kq,false);
    else nierAdd(x,y,i%2?orangeInk:WHITE_INK,kq);
  }
  // Sustain: data rises off the hacked floor, flickering, inside the arena.
  const float hush=1-smooth((t-HUSH)/.35f);
  for(int i=0;i<16;++i) {
    const float a=t-(1.55f+2.0f*rnd(i,415)),life=.9f+.5f*rnd(i,416);
    if(a<0 || a>=life || hush<=0)continue;
    const float y=floorY-4-18*rnd(i,417)-(14+12*rnd(i,418))*a;if(y<top+2)continue;
    const float depth=(y-top)/(floorY-top),hw=40+(w_*.5f-44)*depth;
    const int x=roundi(cx+(rnd(i,419)*2-1)*hw*.85f);
    const int kq=q8(hush*sine(HALF_TURN*a/life)*(.75f+.25f*sine(a*13+i)));
    if(i%3==0)nierSquare(x,roundi(y),1,i%2?WHITE_INK:sepiaInk,kq,false);else nierAdd(x,roundi(y),i%4==1?orangeInk:paleInk,kq);
  }
}

// One sword crescent along an elliptical arc {ox,oy,rx,ry,rot,a0,a1} around
// the readout's centre: the part between `tail` and `h` (fractions of the
// sweep), widest two thirds of the way to the head and pointed at both ends,
// with a bright outer edge and a body fading toward the inside of the curve.
// Incremental rotation: no table lookup per sample.
inline void Renderer::nierCrescent(const float *arc,float h,float tail,float width,float k,const Ink &edge,const Ink &body,float dx,float dy) {
  if(h<=tail || k<=.02f)return;
  const float cx=number_.cx()+arc[0]+dx,cy=number_.cy()+arc[1]+dy,rx=arc[2],ry=arc[3];
  const float cr=cosine(arc[4]),sr=sine(arc[4]);
  const float a0=arc[5]+(arc[6]-arc[5])*tail,span=(arc[6]-arc[5])*(h-tail);
  const int n=std::max(2,int(std::fabs(span)*(rx+ry)*.5f));
  const float da=span/n,cd=cosine(da),sd=sine(da),inv=2/(rx+ry),dv=1.0f/n,wk=width*6.75f;
  // The ellipse's axes turned by its rotation, so a sample is two
  // multiply-adds per coordinate.
  const float axx=rx*cr,axy=-ry*sr,ayx=rx*sr,ayy=ry*cr;
  float c=cosine(a0),s=sine(a0),v=0;
  const int kq=q8(k);
  // Samples over the readout's box would only probe its blocked halo.
  const float bx0=number_.x0-HALO-1,bx1=number_.x1+HALO+1,by0=number_.y0-HALO-1,by1=number_.y1+HALO+1;
  for(int i=0;i<=n;++i,v+=dv) {
    const float ex=axx*c+axy*s,ey=ayx*c+ayy*s,x=cx+ex,y=cy+ey;
    {const float nc=c*cd-s*sd;s=s*cd+c*sd;c=nc;}
    if(x>bx0 && x<bx1 && y>by0 && y<by1)continue;
    const int kv=(kq*(112+int(144*v)))>>8,layers=int(wk*v*v*(1-v));
    // Q8 positions (+128 rounds): the edge, then the body stepping inward
    // by inv of the radius per layer, its weight falling linearly.
    int x8=q8(x)+128,y8=q8(y)+128;
    nierAdd(x8>>8,y8>>8,edge,kv);
    if(layers<=0)continue;
    const int sx=q8(ex*inv),sy=q8(ey*inv);
    for(int d=1;d<=layers;++d){x8-=sx;y8-=sy;nierAdd(x8>>8,y8>>8,body,kv*(layers+1-d)/(layers+1));}
  }
}

// ---- Bladework: a sword combo. After a glint along an unseen blade, five fast
// crescent slashes chain around and under the readout, each followed by its
// own pale afterimage; a two-stroke finisher crosses in an X beneath it and
// holds while the whole chain's afterimages echo once. Then the battle's
// aftermath lingers: the X stays as a breathing afterimage with a slow glint
// along each stroke in turn, fainter echoes of the combo sweep now and then,
// square motes drift up and glints catch the light, until it fades by 4.6 s.
inline void Renderer::nierFlurry(float t) {
  constexpr float D=HALF_TURN/180,SWING=.075f,FINISH=.95f;
  // {ox, oy, rx, ry, rot, a0, a1}, angles in radians; the start times below.
  static constexpr float ARCS[7][7]={
    {-8,46,58,34,-25*D,160*D,330*D},
    {12,40,54,28,20*D,25*D,-150*D},
    {0,60,62,24,0,200*D,340*D},
    {-34,36,30,60,14*D,-80*D,80*D},
    {32,36,30,60,-14*D,260*D,100*D},
    {-60,-10,110,70,0,-12*D,82*D},
    {60,-10,110,70,0,192*D,98*D}};
  static constexpr float START[7]={.24f,.37f,.5f,.63f,.76f,FINISH,FINISH+.07f};
  glint(t,FINISH+.06f,.45f);
  const Rgb steel=mixed(rgb8(214,228,255),accent_,.18f),sepia=rgb8(222,196,140);
  const Ink edgeInk=WHITE_INK,bodyInk=inkOf(steel),ghostInk=inkOf(sepia);
  const float endFade=1-smooth((t-1.75f)/.3f);
  // Anticipation: a hairline glint runs along the blade's edge under the readout.
  if(t<.3f) {
    const float u=easeOut(t/.2f),k=1-smooth((t-.18f)/.1f),y=number_.y1+5.0f,cx=number_.cx(),half=(w_*.5f-8)*u;
    const int kq=q8(.75f*k);
    for(int x=int(cx-half);x<=int(cx+half);++x) {
      const int f=(kq*(256-int(std::fabs(x-cx)*256/(half+1))))>>8,iy=floori(y),ay=q8(y-iy);
      nierAdd(x,iy,bodyInk,(f*(256-ay))>>8);nierAdd(x,iy+1,bodyInk,(f*ay)>>8);
    }
    twinkle(cx+half,y,WHITE,k,3);
  }
  for(int i=0;i<7;++i) {
    const float age=t-START[i];if(age<0)continue;
    const bool fin=i>=5;
    // The head sweeps the arc; after the swing the tail draws in behind it
    // as the crescent fades and thins (the finisher holds a while first).
    const float hold=fin?.32f:0,h=easeOut(age/SWING);
    const float after=std::max(0.0f,age-SWING-hold);
    const float tail=age<SWING?std::max(0.0f,h-.75f):.25f+.75f*smooth(after/.28f);
    const float k=(age<SWING+hold?1:1-smooth(after/.3f))*endFade;
    const float width=(fin?3.3f:3.2f)*(age<SWING?1:1-.5f*smooth((age-SWING)/.25f));
    if(k>0)nierCrescent(ARCS[i],h,tail,width,k,edgeInk,bodyInk,0,0);
    // Afterimage: the same stroke 60 ms behind, offset, in sepia.
    const float ea=age-.06f;
    if(ea>0 && ea<SWING+hold+.3f) {
      const float eh=easeOut(ea/SWING),eAfter=std::max(0.0f,ea-SWING-hold);
      const float et=ea<SWING?std::max(0.0f,eh-.75f):.25f+.75f*smooth(eAfter/.28f);
      nierCrescent(ARCS[i],eh,et,fin?0:1.6f,.5f*(1-smooth(eAfter/.25f))*endFade,ghostInk,ghostInk,fin?0:-2.0f,fin?3.0f:2.0f);
    }
  }
  // The finisher lands: the earlier strokes echo as faint whole afterimages.
  const float fa=t-FINISH-.07f;
  if(fa>0 && fa<.4f) {
    const float k=.34f*(1-fa/.4f)*smooth(fa/.05f);
    for(int i=0;i<5;++i)nierCrescent(ARCS[i],.92f,.38f,0,k,ghostInk,ghostInk,0,0);
  }
  // Sustain (about 1.5-3.95 s), then a slow settle to 4.6 s. The finisher's X
  // stays as a cooling afterimage, breathing, and a glint runs slowly along
  // one of its strokes, then the other; fainter echoes of the combo sweep by
  // now and then; square motes drift up through the air, flickering between
  // hollow and solid; glints catch the light.
  const float linger=1-smooth((t-3.95f)/.65f);
  {
    const float k=.5f*smooth((t-1.45f)/.35f)*linger*(.8f+.2f*sine((t-1.45f)*2.2f));
    if(k>.02f)for(int i=5;i<7;++i)nierCrescent(ARCS[i],.92f,.38f,1.4f,k,ghostInk,ghostInk,0,0);
    static constexpr float GLINT[3]={1.75f,2.6f,3.4f};
    for(int g=0;g<3;++g) {
      const float u=(t-GLINT[g])/.75f;if(u<=0 || u>=1)continue;
      const float head=.4f+.5f*smooth(u),gk=.95f*sine(HALF_TURN*u)*linger;
      nierCrescent(ARCS[5+g%2],head+.06f,head-.06f,1.1f,gk,edgeInk,bodyInk,0,0);
    }
  }
  static constexpr float ECHO[5]={1.85f,2.35f,2.95f,3.3f,3.75f};static constexpr int ECHO_ARC[5]={2,0,4,1,3};
  for(int e=0;e<5;++e) {
    const float age=t-ECHO[e];if(age<0 || age>=.6f)continue;
    const float h=easeOut(age/.14f),tail=age<.14f?std::max(0.0f,h-.7f):.3f+.7f*smooth((age-.14f)/.4f);
    nierCrescent(ARCS[ECHO_ARC[e]],h,tail,1.6f,.55f*linger*(1-smooth((age-.14f)/.42f)),bodyInk,ghostInk,(e%2?4.0f:-5.0f),(e%3)*3.0f);
  }
  for(int g=0;g<7;++g) {
    const float age=t-(1.6f+2.2f*rnd(g,427));if(age<0 || age>=.3f)continue;
    twinkle(number_.cx()+(rnd(g,428)-.5f)*110,number_.y1+8+60*rnd(g,429),WHITE,linger*sine(HALF_TURN*age/.3f),2);
  }
  // Square motes: each rises slowly and sways on its own life.
  for(int i=0;i<20;++i) {
    const float birth=1.3f+2.6f*(i+.6f*rnd(i,481))/20,life=1.3f+.7f*rnd(i,482),age=t-birth;
    if(age<0 || age>=life)continue;
    const float u=age/life,x=number_.cx()+(rnd(i,483)-.5f)*124+3*sine(age*1.3f+i);
    const float y=number_.y1+14+58*rnd(i,484)-(6+8*rnd(i,485))*age;
    const int kq=q8(.95f*linger*sine(HALF_TURN*u)*(.72f+.28f*sine(age*6.5f+i)));
    const Ink &c=i%5==0?bodyInk:i%7==3?edgeInk:ghostInk;
    if(i%3==0)nierSquare(roundi(x),roundi(y),2,c,kq*7/8,false);
    else if(i%3==1)nierSquare(roundi(x),roundi(y),1,c,kq,(int((age+rnd(i,486))*4)&1)!=0);
    else nierAdd(roundi(x),roundi(y),c,kq);
  }
  // Machine debris: small squares and specks drift out of the finisher and fall.
  for(int i=0;i<16;++i) {
    const float age=t-(FINISH+.08f+.12f*rnd(i,421)),life=.8f+.4f*rnd(i,422);
    if(age<0 || age>=life)continue;
    const float x0=number_.cx()+(rnd(i,423)-.5f)*90,y0=number_.y1+30+(rnd(i,424)-.5f)*30;
    const float vx=(rnd(i,425)-.5f)*50,vy=-20-30*rnd(i,426);
    const int x=roundi(x0+vx*age),y=roundi(y0+vy*age+60*age*age),kq=q8((1-age/life)*endFade);
    if(i%3==0)nierSquare(x,y,1,i%2?bodyInk:ghostInk,kq,false);else nierAdd(x,y,i%2?WHITE_INK:ghostInk,kq);
  }
}

// One petal of a flower seen from slightly above: it leaves the centre at
// azimuth phi, `rise` radians above the flower's plane, bending down by `curl`
// toward its tip; a quadratic curve through three projected points, so no
// trigonometry per step. Ovate: broad near the base, pointed at the tip, with
// a bright rim, a glowing midrib and a soft fill.
inline void Renderer::nierPetal(float fx,float fy,float phi,float length,float width,float rise,float curl,const Ink &edge,const Ink &body,int kq) {
  constexpr float TILT=.62f;
  const float cp=cosine(phi),sp=sine(phi),half=length*.5f,e1=rise-curl;
  const float r1=1+half*cosine(rise),z1=half*sine(rise),r2=r1+half*cosine(e1),z2=z1+half*sine(e1);
  const float x0=fx+cp,y0=fy+sp*TILT,x1=fx+r1*cp,y1=fy+r1*sp*TILT-z1,x2=fx+r2*cp,y2=fy+r2*sp*TILT-z2;
  const float wx=-sp,wy=cp*TILT;
  const int n=std::max(6,int(length*.72f));
  for(int j=1;j<=n;++j) {
    const float s=float(j)/n,a=(1-s)*(1-s),b=2*s*(1-s),c=s*s;
    const float x=a*x0+b*x1+c*x2,y=a*y0+b*y1+c*y2,w=width*6.75f*s*a;
    if(j<n)nierAdd(roundi(x),roundi(y),body,kq*12/16);
    nierAdd(roundi(x+wx*w),roundi(y+wy*w),edge,kq);nierAdd(roundi(x-wx*w),roundi(y-wy*w),edge,kq);
    if(w>1.5f){nierAdd(roundi(x+wx*w*.55f),roundi(y+wy*w*.55f),body,kq*10/16);nierAdd(roundi(x-wx*w*.55f),roundi(y-wy*w*.55f),body,kq*10/16);}
    if(j==n)nierAdd(roundi(x),roundi(y),edge,kq);
  }
}

// ---- Moonpetal: a moonflower opens. Pale motes of moonlight drift down onto a
// stem growing under the readout; a bud forms and slowly unfurls six slender
// blue-white petals and three inner ones; at full bloom the petals flare and a
// halo of moonlight spreads. It then stays open through a long sustain,
// breathing and swaying, light motes rising off it like tears and moonlight
// drifting down onto it; finally its petals draw
// in a little as it fades back into the dark.
inline void Renderer::nierMoon(float t) {
  constexpr float OPEN=.62f,BLOOM=1.38f;
  const Rgb petal=rgb8(150,200,255),edgeC=rgb8(226,242,255),stemC=rgb8(110,200,170),pale=mixed(rgb8(190,222,255),accent_,.15f);
  const float fx=number_.cx(),fy=number_.y1+16.0f;
  const float fade=1-smooth((t-4.1f)/.85f);
  glint(t,BLOOM-.02f,.45f);
  if(fade<=0)return;
  // Moonlight gathers: motes drift down and inward onto the flower.
  if(t<.75f)for(int i=0;i<9;++i) {
    const float u=(t-.06f*i)/.6f;if(u<=0 || u>=1)continue;
    const float sx=fx+(rnd(i,431)-.5f)*120,sy=number_.y1+4+6*rnd(i,432);
    const float e=u*u,x=sx+(fx-sx)*e+3*sine(u*6+i),y=sy+(fy+4-sy)*e;
    dot(x,y,pale,.85f*sine(HALF_TURN*u));
  }
  // The stem grows up to the flower in a gentle curve, with two leaves.
  {
    const float grown=easeOut(t/.45f);const int n=int(34*grown);
    const Ink c=inkOf(stemC);const int kq=q8(.75f*fade);
    auto stemX=[&](float s){return fx-9*sine(s*HALF_TURN*.9f);};
    for(int j=0;j<n;++j){const float s=1-j/34.0f;nierAdd(roundi(stemX(s)),roundi(fy+2+34*s),c,kq);}
    for(int leaf=0;leaf<2;++leaf) {
      const float at=leaf?.42f:.68f,g=clamp01((grown-(1-at))/.3f);if(g<=0)continue;
      const float bx=stemX(at),by=fy+2+34*at,side=leaf?1:-1;
      for(int j=1;j<=int(10*g);++j){const int x=roundi(bx+side*j),y=roundi(by-j*.55f-.06f*j*j);nierAdd(x,y,c,kq*(13-j)/13);if(j>2 && j<8)nierAdd(x,y+1,c,kq*(13-j)/26);}
    }
  }
  // Bud, then the opening petals: back petals first and dimmer.
  const float bud=smooth((t-.3f)/.32f);
  if(bud<=0)return;
  const float breathe=t>BLOOM?.86f+.09f*sine((t-BLOOM)*2.3f)+.05f*sine((t-BLOOM)*3.7f):1;
  const float flare=t>=BLOOM?.7f*(1-smooth((t-BLOOM)/.35f)):0;
  const Ink edge=mixedInk(inkOf(edgeC),WHITE_INK,q8(flare)),body=inkOf(mixed(petal,edgeC,flare*.6f));
  const int kq=q8(std::min(1.0f,(.8f+.3f*flare)*breathe*fade*bud));
  // Azimuths in the flower's plane, uneven on purpose: seen from above the
  // front petals open into the wedges either side of the screen's centre
  // line, the side ones level with the heart, the back ones up behind the
  // readout. Far side first.
  const float close=1-.3f*smooth((t-4.0f)/.9f);  // the petals draw in as it fades
  static constexpr float PHI[6]={0,1.22f,1.92f,HALF_TURN,4.36f,5.06f},LEN[6]={30,40,40,30,26,26};
  for(int pass=0;pass<2;++pass)for(int i=0;i<6;++i) {
    if((PHI[i]>HALF_TURN+.01f)!=(pass==0))continue;
    const float phi=PHI[i]+.05f*sine(t*1.3f+i),o=easeOut((t-OPEN-.04f*i)/.72f)*(1-.06f*(.5f+.5f*sine(t*1.1f+i))*smooth((t-BLOOM)/.5f))*close;
    const float len=LEN[i]*(.5f+.5f*bud)*(.62f+.38f*o),rise=1.4f-1.2f*o,curl=.05f+.3f*o;
    nierPetal(fx,fy,phi,len,8*(.45f+.55f*o),rise,curl,edge,body,pass?kq:kq*14/16);
  }
  for(int i=0;i<3;++i) {
    const float phi=i*(2*HALF_TURN/3)+HALF_TURN/6,o=easeOut((t-OPEN-.1f)/.8f)*close;
    nierPetal(fx,fy,phi,18*(.6f+.4f*bud),5,1.48f-.68f*o,.1f+.2f*o,edge,body,kq*13/16);
  }
  // The heart glows.
  dot(fx,fy-1,edgeC,fade*bud*(.6f+.6f*flare));
  if(t>=BLOOM-.1f)spark(fx,fy-1,pale,fade*(.5f+.9f*flare)*smooth((t-BLOOM+.1f)/.1f),1.8f);
  if(t<BLOOM)return;
  // A halo of moonlight spreads from the bloom in the flower's plane.
  {
    const float u=(t-BLOOM)/.6f;
    if(u>0 && u<1){const float rad=34+18*easeOut(u);nierRing(fx,fy,rad,rad*.62f,inkOf(mixed(pale,WHITE,.3f*(1-u))),q8(.9f*(1-u)*(1-u)));}
  }
  // Light motes rise off the petals and twinkle out, through the sustain.
  for(int i=0;i<30;++i) {
    const float age=t-(BLOOM+.05f+2.5f*rnd(i,441)),life=.9f+.4f*rnd(i,442);
    if(age<0 || age>=life)continue;
    const float u=age/life,x=fx+(rnd(i,443)-.5f)*80+3*sine(age*4+i),y=fy+2+16*rnd(i,445)-(8+8*rnd(i,444))*age;
    const float k=sine(HALF_TURN*u)*fade;
    if(i%4==0)twinkle(x,y,edgeC,k,1);else dot(x,y,pale,k);
  }
  // Moonlight keeps drifting down onto the flower now and then.
  for(int i=0;i<6;++i) {
    const float u=(t-(1.8f+1.8f*rnd(i,446)))/1.1f;if(u<=0 || u>=1)continue;
    const float sx=fx+(rnd(i,447)-.5f)*110,x=sx+(fx-sx)*u*u+2*sine(u*7+i),y=number_.y1+4+(fy-number_.y1)*u;
    dot(x,y,pale,.7f*sine(HALF_TURN*u)*fade);
  }
}

// ---- Barrage: a support unit's barrage. A boxy drone glides in beside the
// readout and pours a stream of tracer shots into a swarm of orange enemy
// orbs, popping them one by one; it charges and fires one wide beam into a
// big orb, which bursts into orange squares. Through the sustain it patrols,
// drifting, and picks off a few stragglers with short bursts, then glides
// away. The drone sits on the right, clear of the replay's tilt arrow.
inline void Renderer::nierBarrage(float t) {
  constexpr float FIRE=.5f,RATE=30,SPEED=300,CHARGE=1.38f,BEAM=1.58f,BEAM_END=1.9f,LEAVE=4.1f;
  constexpr int ORBS=9,SHOTS=28;
  const Rgb sepia=rgb8(226,212,176),orange=rgb8(255,128,32),tracer=rgb8(255,226,140);
  const Ink sepiaInk=inkOf(sepia),orangeInk=inkOf(orange),tracerInk=inkOf(tracer),whiteInk=WHITE_INK;
  const float bandTop=number_.y1+8.0f;
  glint(t,BEAM+.02f,.45f);
  // The drone's path: in from the right, bobbing, out to the upper right.
  auto podX=[&](float s){const float u=std::max(0.0f,s-1.95f);return w_+10-27*easeOut(s/.4f)-6*sine(.9f*u)*smooth(u/.4f)+60*smooth((s-LEAVE)/.45f);};
  auto podY=[&](float s){const float u=std::max(0.0f,s-1.95f);return number_.y1+16+1.3f*sine(s*5.5f)+4*sine(.7f*u)*smooth(u/.4f)-10*smooth((s-LEAVE)/.45f);};
  // Orb k is at x(k) at time .62+.1k, drifting right; its lane is fixed.
  auto orb=[&](int k,float s,float &x,float &y){
    const float v=26+18*rnd(k,451),at=.62f+.1f*k,xT=16+46*rnd(k,452);
    x=xT+v*(s-at);y=bandTop+3+38*rnd(k,453)+2*sine(s*3+k);
  };
  // Each orb dies when the third shot aimed at it arrives; later shots aim at
  // the next. Arrival found by fixed-point iteration.
  float dies[ORBS];
  for(int k=0;k<ORBS;++k) {
    const float tf=FIRE+(3*k+2)/RATE,mx=podX(tf)-8,my=podY(tf);float ta=tf+.2f;
    for(int it=0;it<3;++it){float x,y;orb(k,ta,x,y);ta=tf+std::sqrt((x-mx)*(x-mx)+(y-my)*(y-my))/SPEED;}
    dies[k]=ta;
  }
  const float px=podX(t),py=podY(t),podK=1-smooth((t-LEAVE-.15f)/.3f);
  // Tracers: a hot head and a tail fading through yellow to orange.
  for(int j=0;j<SHOTS;++j) {
    const float tf=FIRE+j/RATE,age=t-tf;if(age<0)break;
    const int k=std::min(ORBS-1,j/3);
    const float mx=podX(tf)-8,my=podY(tf);float ta=tf+.2f,tx=0,ty=0;
    for(int it=0;it<3;++it){orb(k,ta,tx,ty);ta=tf+std::sqrt((tx-mx)*(tx-mx)+(ty-my)*(ty-my))/SPEED;}
    const float dist=std::sqrt((tx-mx)*(tx-mx)+(ty-my)*(ty-my)),ux=(tx-mx)/dist,uy=(ty-my)/dist;
    const bool hits=ta<=dies[k]+.001f;const float flight=hits?ta-tf:.5f;
    if(age<flight) {
      const float d=SPEED*age;
      for(int s=0;s<7;++s) {
        if(d-s<0)break;
        nierAdd(roundi(mx+ux*(d-s)),roundi(my+uy*(d-s)),s<2?whiteInk:mixedInk(tracerInk,orangeInk,s*40),(250*(7-s))/7);
      }
    } else if(hits && age<flight+.05f)twinkle(tx,ty,tracer,1-(age-flight)/.05f,1);
  }
  // The orange orbs: glowing spheres; each pops into a ring and four specks.
  for(int k=0;k<ORBS;++k) {
    float x,y;
    if(t<dies[k]) {
      orb(k,t,x,y);if(x<-3)continue;
      nierOrb(x,y,orangeInk,q8(.95f*smooth((t-.05f)/.15f)));
      continue;
    }
    const float age=t-dies[k];if(age>=.3f)continue;
    orb(k,dies[k],x,y);const float u=age/.3f,r=2+8*easeOut(u);
    nierRing(x,y,r,r,mixedInk(whiteInk,orangeInk,int(u*256)),q8(1-u));
    for(int f=0;f<4;++f){const float a=f*(HALF_TURN/2)+.8f+k;dot(x+cosine(a)*54*age,y+sine(a)*54*age,orange,(1-u)*.9f);}
  }
  // The big orb rises in, takes the beam and bursts.
  const float bx=number_.cx()-28,by=number_.y1+30.0f;
  if(t>=.85f && t<BEAM+.04f) {
    const float g=smooth((t-.85f)/.3f),r=5*g*(1+.08f*sine(t*20));
    nierRing(bx,by,r,r,orangeInk,q8(.95f*g));nierRing(bx,by,r+2,r+2,orangeInk,q8(.35f*g));
    nierOrb(bx,by,inkOf(mixed(orange,WHITE,.3f)),q8(.85f*g));
  }
  if(t>=BEAM) {
    const float age=t-BEAM;
    if(age<.1f)spark(bx,by,orange,1.6f*(1-age/.1f),3.4f);
    if(age<.6f){const float u=age/.6f;nierRing(bx,by,6+34*easeOut(u),6+34*easeOut(u),mixedInk(whiteInk,orangeInk,int(u*256)),q8((1-u)*(1-u)));}
    for(int i=0;i<16;++i) {
      const float life=.55f+.3f*rnd(i,461);if(age>=life)continue;
      const float a=2*HALF_TURN*(i+.5f*rnd(i,462))/16,v=40+60*rnd(i,463),d=v*(1-std::exp(-3*age))/3;
      const int x=roundi(bx+cosine(a)*d),y=roundi(by+sine(a)*d+30*age*age),kq=q8(1-age/life);
      if(i%2)nierSquare(x,y,1,orangeInk,kq,false);else nierAdd(x,y,i%4?whiteInk:orangeInk,kq);
    }
  }
  // Sustain: stragglers drift in from the left and a four-shot burst picks
  // each off. Each dies when its fourth shot arrives.
  static constexpr float STRAY[4]={2.0f,2.55f,3.05f,3.5f};
  for(int k=0;k<4;++k) {
    auto at=[&](float s,float &x,float &y){x=-4+(26+8*rnd(k,471))*(s-STRAY[k]);y=bandTop+6+30*rnd(k,472)+2*sine(s*2.6f+k);};
    float dead=1e9f;
    for(int j=0;j<4;++j) {
      const float tf=STRAY[k]+.75f+j/20.0f,mx0=podX(tf)-8,my0=podY(tf);float ta=tf+.25f,tx=0,ty=0;
      for(int it=0;it<3;++it){at(ta,tx,ty);ta=tf+std::sqrt((tx-mx0)*(tx-mx0)+(ty-my0)*(ty-my0))/SPEED;}
      if(j==3)dead=ta;
      const float age=t-tf;if(age<0)continue;
      const float dist=std::sqrt((tx-mx0)*(tx-mx0)+(ty-my0)*(ty-my0)),ux=(tx-mx0)/dist,uy=(ty-my0)/dist;
      if(age<ta-tf) {
        const float d=SPEED*age;
        for(int s=0;s<6 && d-s>=0;++s)nierAdd(roundi(mx0+ux*(d-s)),roundi(my0+uy*(d-s)),s<2?whiteInk:mixedInk(tracerInk,orangeInk,s*45),(230*(6-s))/6);
      } else if(age<ta-tf+.05f)twinkle(tx,ty,tracer,1-(age-(ta-tf))/.05f,1);
      if(age>=0 && age<1/30.0f){const int ix=roundi(mx0),iy=roundi(my0);for(int s=1;s<=4;++s)nierAdd(ix-s,iy,tracerInk,200*(5-s)/5);}
    }
    float x,y;
    if(t>=STRAY[k] && t<dead){at(t,x,y);if(x>-3)nierOrb(x,y,orangeInk,q8(.8f*smooth((t-STRAY[k])/.2f)));}
    else if(t>=dead && t<dead+.25f){at(dead,x,y);const float u=(t-dead)/.25f;nierRing(x,y,2+5*easeOut(u),2+5*easeOut(u),mixedInk(whiteInk,orangeInk,int(u*256)),q8(.9f*(1-u)));}
  }
  // Charge: specks converge on the muzzle while its glow swells.
  const float mx=px-8,my=py;
  if(t>=CHARGE && t<BEAM) {
    const float u=(t-CHARGE)/(BEAM-CHARGE);
    for(int i=0;i<8;++i){const float a=i*(HALF_TURN/4)+u*2,d=14*(1-u);dot(mx+cosine(a)*d,my+sine(a)*d,tracer,.4f+.6f*u);}
    spark(mx,my,tracer,.5f+.8f*u,1.2f+1.2f*u);
  }
  // The beam: a white core with warm edges, jittering, thinning as it ends.
  if(t>=BEAM && t<BEAM_END+.2f) {
    const float w=t<BEAM_END?2.4f+.6f*(int(t*40)&1):2.4f*(1-(t-BEAM_END)/.2f);
    const float dx=bx-mx,dy=by-my;const int n=int(std::max(std::fabs(dx),std::fabs(dy)));
    const int layers=int(w);
    for(int i=0;i<=n;++i) {
      const int x=roundi(mx+dx*i/n),y=roundi(my+dy*i/n);
      nierAdd(x,y,whiteInk,256);
      for(int l=1;l<=layers;++l){const int kq=200*(layers+1-l)/(layers+1);nierAdd(x,y-l,l==1?tracerInk:orangeInk,kq);nierAdd(x,y+l,l==1?tracerInk:orangeInk,kq);}
    }
  }
  // Muzzle flashes while the stream pours out.
  if(t>=FIRE && t<FIRE+SHOTS/RATE) {
    const float ph=(t-FIRE)*RATE,k=1-(ph-floori(ph));
    const int kq=q8(.95f*k),ix=roundi(mx),iy=roundi(my);
    for(int s=1;s<=3+int(3*k);++s)nierAdd(ix-s,iy,tracerInk,kq*(8-s)/8);
    nierAdd(ix-1,iy-1,tracerInk,kq>>1);nierAdd(ix-1,iy+1,tracerInk,kq>>1);nierAdd(ix,iy,whiteInk,kq);
  }
  // The drone, facing left: a box with a lit top, two arms and a barrel.
  if(podK>0 && px<w_+12) {
    const int ix=roundi(px),iy=roundi(py),kq=q8(podK);
    for(int x=-4;x<=5;++x){nierAdd(ix+x,iy-3,whiteInk,kq);nierAdd(ix+x,iy+3,sepiaInk,kq*3/4);}
    for(int y=-2;y<=2;++y){nierAdd(ix+5,iy+y,sepiaInk,kq*3/4);nierAdd(ix-4,iy+y,sepiaInk,kq);}
    for(int x=-3;x<=4;++x)nierAdd(ix+x,iy-2,sepiaInk,kq*3/8);
    nierLine(ix+3,iy+4,ix+4,iy+6,sepiaInk,kq*3/4,0,h_);nierLine(ix-2,iy+4,ix-3,iy+6,sepiaInk,kq*3/4,0,h_);
    nierLine(ix-5,iy,ix-7,iy,whiteInk,kq,0,h_);
    // Thrusters while it glides.
    if(t<.45f || t>LEAVE)for(int s=1;s<=3;++s)nierAdd(ix+5+2*s,iy+1,tracerInk,kq*(4-s)/5);
  }
}

// Rotates rows y0..y1 of the readout's box by `shift` px (positive: right),
// wrapping inside the box, so the glyphs tear sideways and come back exactly.
inline void Renderer::nierTear(int y0,int y1,int shift) {
  const Box &b=number_;const int n=b.x1-b.x0+1;
  shift%=n;if(shift<0)shift+=n;if(!shift)return;
  for(int y=std::max(y0,b.y0);y<=std::min(y1,b.y1);++y) {
    uint16_t *row=px_+y*w_+b.x0;
    std::rotate(row,row+n-shift,row+n);
    plots_+=unsigned(n);
  }
}

// ---- Glitch: a system glitch. Parchment-beige menu rules draw out above and
// below the readout with square end caps and score-like ticks; they flicker,
// then the screen glitches (0.62-1.15 s): thin bands of the readout jog a
// pixel or two with orange and beige fringes on their glyph edges and a streak
// along each band, bars of light flicker below; it snaps clean with a glint
// and two aftershocks twitch, the last ending at 1.56 s. Through the sustain the menu idles: highlights travel along the
// rules, the ticks shimmer, and small glitches flicker past now and then
// (never on the readout); finally the rules retract into dissolving squares.
inline void Renderer::nierGlitch(float t) {
  constexpr float G0=.62f,G1=1.15f;
  const Rgb sepia=rgb8(222,208,170);
  const Ink sepiaInk=inkOf(sepia),orangeInk=inkOf(rgb8(255,128,32)),whiteInk=WHITE_INK;
  const Box &b=number_;const int rows=b.y1-b.y0+1;
  // The tear comes first, so every later stroke is masked by the torn readout.
  // The RPM must read on every frame, so the tear is small against glyphs
  // about 18 rows tall with 3 px strokes: one or two thin bands at a time
  // (one or two rows), each moved 1-2 px, 3 px only on a single row at the
  // two peaks. The glitch reads through colour instead: each torn band's
  // glyph edges split into an orange and a beige fringe (shape unchanged),
  // and a beige streak runs along the band across the screen.
  const float e=t>=G0 && t<G1?smooth((t-G0)/.08f)*(1-smooth((t-1.0f)/.15f)):0;
  const uint32_t frameNo=uint32_t(std::max(0.0f,t-G0)*20);
  struct Tear {int y0,y1,shift;};
  Tear tears[2];int torn=0;
  if(e>0) {
    const bool peak=frameNo==3 || frameNo==7;
    const int bands=peak?1:1+int(rnd(frameNo,500)<.45f*e);
    for(int band=0;band<bands;++band) {
      const uint32_t seed=frameNo*8+band;
      // Bands keep to their own half of the readout, so two never overlap.
      const int half=rows/2,y0=b.y0+band*half+int(rnd(seed,501)*(half-2)),hgt=peak?1:1+int(rnd(seed,502)*2);
      const int mag=peak?3:1+int(rnd(seed,504)*1.99f*e);
      tears[torn++]={y0,y0+hgt-1,(rnd(seed,503)<.5f?-1:1)*mag};
    }
  }
  // Aftershocks: one thin band each, briefly; every pixel is back by 1.56 s.
  if(t>=1.32f && t<1.4f)tears[torn++]={b.y0+rows/3,b.y0+rows/3+1,2};
  if(t>=1.48f && t<1.56f)tears[torn++]={b.y1-5,b.y1-5,-2};
  if(torn) {
    // Fringe tints mix the glyph's own colour toward orange (leading edge)
    // or beige (trailing edge), kept only where the tint stays readable
    // (3:1 on the background, or no worse than the glyph itself).
    const unsigned bgv=load(bg_);
    uint16_t lastIn[2]={bg_,bg_},lastOut[2]={bg_,bg_};
    auto tint=[&](uint16_t stored,int side)->uint16_t {
      if(stored==lastIn[side])return lastOut[side];
      const unsigned v=load(stored);const Ink &to=side?sepiaInk:orangeInk;
      const int r=v>>11,g=(v>>5)&63,bl=v&31;
      const uint16_t mix=uint16_t((r+((((to.r>>8)-r)*210)>>8))<<11|(g+((((to.g>>8)-g)*210)>>8))<<5|(bl+((((to.b>>8)-bl)*210)>>8)));
      const uint32_t was=Palette::contrast100(uint16_t(v),uint16_t(bgv)),now=Palette::contrast100(mix,uint16_t(bgv));
      lastIn[side]=stored;lastOut[side]=now>=std::min<uint32_t>(300,was)?store(mix):stored;
      return lastOut[side];
    };
    for(int i=0;i<torn;++i) {
      const Tear &tr=tears[i];
      nierTear(tr.y0,tr.y1,tr.shift);
      for(int y=std::max(tr.y0,b.y0);y<=std::min(tr.y1,b.y1);++y) {
        uint16_t *row=px_+y*w_;
        // The leading side of the move gets the orange fringe.
        for(int x=b.x0;x<=b.x1;++x) {
          if(row[x]==bg_)continue;
          const bool left=x==b.x0 || row[x-1]==bg_,right=x==b.x1 || row[x+1]==bg_;
          if(left==right)continue;
          row[x]=tint(row[x],left==(tr.shift>0)?1:0);++plots_;
        }
      }
    }
  }
  // Each torn band's streak: beige light along its rows across the screen,
  // brightest toward the side the band moved (the halo keeps it off the glyphs).
  for(int i=0;i<torn;++i) {
    const Tear &tr=tears[i];const int k0=q8(.55f*std::max(e,.6f));
    for(int y=tr.y0;y<=tr.y1;++y)for(int x=0;x<w_;++x) {
      // Fainter between the glyphs, where it would clutter the digits.
      const int toward=tr.shift>0?x:w_-1-x,inside=x>=b.x0 && x<=b.x1?2:5;
      nierAdd(x,y,sepiaInk,(k0*(96+160*toward/w_)*inside/5)>>8);
    }
  }
  glint(t,G1,.42f);
  // Rules: out from the centre, flickering before the glitch, flashing at
  // the snap, retracting at the end.
  const float out=easeOut(t/.45f)*(1-smooth((t-4.0f)/.5f));
  if(out>0) {
    const float flicker=t>=.48f && t<G0?((uint32_t(t*40)&1)?.35f:1.0f):1;
    const float snap=t>=G1?.8f*(1-smooth((t-G1)/.25f)):0;
    // Sustain micro-glitches: brief, irregular, a two-pixel twitch of the rules.
    static constexpr float TWITCH[6]={1.9f,2.35f,2.6f,3.05f,3.4f,3.7f};
    int twitch=-1;for(int i=0;i<6;++i)if(t>=TWITCH[i] && t<TWITCH[i]+.08f)twitch=i;
    const int kq=q8(std::min(1.0f,.85f*flicker*(1-.5f*e)+snap)),jitter=e>0?int((rnd(frameNo,505)-.5f)*8*e):twitch>=0?(twitch%2?2:-2):0;
    const Ink c=snap>0?mixedInk(sepiaInk,whiteInk,q8(snap)):sepiaInk;
    const int cx=roundi(b.cx())+jitter,hl=int((w_*.5f-7)*out),topY=b.y0-4,botY=b.y1+5;
    for(int x=cx-hl;x<=cx+hl;++x){nierAdd(x,topY,c,kq);nierAdd(x,botY,c,kq);}
    for(int x=cx-hl*3/5;x<=cx+hl*3/5;++x)nierAdd(x-jitter*2,botY+2,c,kq*3/8);
    // Score ticks under the bottom rule, every 8 px out from the centre.
    // In the sustain they shimmer, each tick at its own phase.
    for(int d=0;d<=hl-4;d+=8) {
      const float sh=t>1.4f?.75f+.25f*sine(t*2.7f+d*.37f):1;const int k2=int(kq/2*sh),k3=int(kq/3*sh);
      nierAdd(cx+d,botY+4,c,k2);nierAdd(cx-d,botY+4,c,k2);nierAdd(cx+d,botY+5,c,k3);nierAdd(cx-d,botY+5,c,k3);
    }
    // Highlights travel the rules once each: bottom left to right, top right to left.
    for(int r=0;r<2;++r) {
      const float u=(t-(r?2.4f:1.6f))/1.5f;if(u<=0 || u>=1)continue;
      const int head=r?cx+hl-int(2*hl*u):cx-hl+int(2*hl*u),y=r?topY:botY;
      for(int d=0;d<14;++d){const int x=r?head+d:head-d;if(x>=cx-hl && x<=cx+hl)nierAdd(x,y,whiteInk,q8(.7f*sine(HALF_TURN*u))*(14-d)/14);}
    }
    // End caps: hollow squares on the bottom rule, filled ones on the top.
    nierSquare(cx-hl-3,botY,2,c,kq,false);nierSquare(cx+hl+3,botY,2,c,kq,false);
    nierSquare(cx-hl-2,topY,1,c,kq,true);nierSquare(cx+hl+2,topY,1,c,kq,true);
    // A small diamond marks the centre.
    nierAdd(cx,botY+3,whiteInk,kq);nierAdd(cx-1,botY+4,c,kq);nierAdd(cx+1,botY+4,c,kq);nierAdd(cx,botY+5,c,kq);
  }
  // Glitch bars flicker across the screen, a new pattern every frame.
  auto bars=[&](uint32_t seed,int count,float k) {
    for(int i=0;i<count;++i) {
      const uint32_t s=seed*16+i;
      // Below the rules only: a bar beside the label could read as a glyph.
      const int yTop=b.y1+12,y=yTop+int(rnd(s,511)*std::max(1,std::min(bottom_,150)-yTop)),x0=int(rnd(s,512)*(w_-14)),len=6+int(rnd(s,513)*32);
      const Ink &c=i%7==6?orangeInk:i%3==2?whiteInk:sepiaInk;const int kq=q8(k*(.55f+.4f*rnd(s,514)));
      for(int x=x0;x<std::min(w_,x0+len);++x){nierAdd(x,y,c,kq);if(i%4==1)nierAdd(x,y+1,c,kq*3/4);}
    }
  };
  if(e>0)bars(frameNo,8,e);
  if(t>=1.32f && t<1.4f)bars(97,4,.8f);
  if(t>=1.48f && t<1.56f)bars(98,3,.7f);
  static constexpr float FLICKS[6]={1.9f,2.35f,2.6f,3.05f,3.4f,3.7f};
  for(int i=0;i<6;++i)if(t>=FLICKS[i] && t<FLICKS[i]+.08f)bars(200+i,2,.55f);
  // The rules dissolve into squares that drift up and fade.
  for(int i=0;i<14;++i) {
    const float age=t-(4.02f+.3f*rnd(i,521)),life=.45f+.15f*rnd(i,522);
    if(age<0 || age>=life)continue;
    const bool lower=i%3!=0;
    const float x=b.cx()+(rnd(i,523)-.5f)*(w_-20)+(rnd(i,524)-.5f)*16*age,y=(lower?b.y1+5:b.y0-4)-(10+22*rnd(i,525))*age;
    const int kq=q8(.9f*(1-age/life));
    if(i%2)nierSquare(roundi(x),roundi(y),1,sepiaInk,kq,false);else nierAdd(roundi(x),roundi(y),i%4?sepiaInk:whiteInk,kq);
  }
}
}
