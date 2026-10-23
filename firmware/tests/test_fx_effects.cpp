// Loaded celebration effects (fx_effect.h, docs/FIRMWARE_LOADABLE_EFFECTS.md):
// six built-ins ported to LLFX files (firmware/tools/fx/effects) draw the
// native frames, keep the renderer's rules (light only outside the UI halo,
// the readout touched only early and given back exactly, nothing after the
// end) and fit the device cost model; the loader refuses malformed headers
// and programs; the guard stops a frame between draws; and the compiler
// reports source errors by line. Same fixture as test_classic_effects.
// Frames are compared bit for bit, so nothing here is contracted into fused
// multiply-adds (clang's default on arm64 hosts, not on the Linux runner).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
#include "../tools/fx/fx_compiler.h"
#include "../tools/fx/fx_validate.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <limits>
#include <iostream>
#include <sstream>

using namespace LoadableFx;
static std::array<uint16_t,135*240> base_{};
static uint16_t swap16(uint16_t v){return uint16_t(v<<8|v>>8);}

static std::vector<uint8_t> compileFile(const std::string &path) {
  std::ifstream in(path);std::stringstream text;text<<in.rdbuf();
  assert(in.good() || !text.str().empty());
  const auto r=FxCompiler::compile(text.str());
  if(!r.ok)std::cerr<<path<<":"<<r.line<<": "<<r.error<<"\n";
  assert(r.ok);
  return r.bytes;
}
static std::vector<uint8_t> compileText(const std::string &text){const auto r=FxCompiler::compile(text);if(!r.ok)std::cerr<<r.line<<": "<<r.error<<"\n";assert(r.ok);return r.bytes;}
// Rewrites the size and CRC fields after an edit, so the edit reaches the
// structural checks.
static void reseal(std::vector<uint8_t> &f) {
  put32(f.data()+24,uint32_t(f.size()));put32(f.data()+28,0);put32(f.data()+28,crc32(f.data(),f.size()));
}

int main() {
  using namespace Appearance;
  std::array<uint16_t,135*240> &base=base_;
  auto ui=[&](int x,int y,uint16_t c){base[y*135+x]=swap16(c);};
  for(int y=2;y<10;++y)for(int x=44;x<91;++x)if((x+y)%3)ui(x,y,0x07ff);
  for(int y=17;y<44;++y)for(int x=10;x<125;++x)if((x+y)%5)ui(x,y,0x07ff);
  for(int i=0;i<90;++i){ui(20+i,60+i/3,0x18c3);ui(110-i,60+i/3,0x18c3);}
  for(int y=110;y<150;++y)for(int x=50;x<85;++x)ui(x,y,0x1947);
  for(int y=164;y<172;++y)for(int x=40;x<95;++x)if((x*y)%4)ui(x,y,0x7bef);
  for(int x=26;x<109;++x)ui(x,178,0x39e7);
  std::array<bool,135*240> halo{};
  for(int y=0;y<240;++y)for(int x=0;x<135;++x)if(base[y*135+x])
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)if(x+dx>=0 && x+dx<135 && y+dy>=0 && y+dy<240)halo[(y+dy)*135+x+dx]=true;

  BestEffectUI::Renderer native,shared;Engine engine(shared);
  double worstAll=0;
  // Each port's native counterpart, and how closely it must match it: the
  // share of frames identical to the native ones and the share of changed
  // pixels that differ. Ports differ only where the native code mixes in
  // fixed point or calls std::exp, which the engine does in its own exact way.
  struct Port {const char *name;Effect effect;double minSame,maxDiff;bool movesReadout;};
  const Port ports[]={{"orbit",Effect::Orbit,.8,.01,false},{"flames",Effect::Flames,1,0,false},{"sparkles",Effect::Sparkles,.95,.002,false},
                      {"shockwave",Effect::Shockwave,.75,.006,false},{"slash",Effect::Slash,.95,.006,true},{"rupture",Effect::Rupture,.99,.0002,true}};
  for(const Port &port:ports) {
    const auto file=compileFile(std::string("firmware/tools/fx/effects/")+port.name+".fx");
    Program p;assert(load(file.data(),file.size(),p)==Error::None);
    assert(!std::strcmp(p.name,label(port.effect)) && p.duration==duration(port.effect));
    unsigned frames=0,same=0;double differing=0,changed=0,worstUs=0,worstNativeUs=0,worstPixelUs=0;uint32_t worstOps=0;unsigned worstMs=0;
    for(unsigned theme:{0u,3u,5u,8u}) {
      const uint16_t color=accent(Theme(theme));
      for(unsigned ms=0;ms<=5300;ms+=10) {
        auto want=base,got=base,again=base;
        native.draw({want.data(),135,240},port.effect,ms,color);
        engine.draw({got.data(),135,240},p,ms,color);
        const Engine::Stats s=engine.stats();const double us=FxValidate::Cost().us(engine);
        engine.draw({again.data(),135,240},p,ms,color);
        assert(got==again);
        if(us>worstUs){worstUs=us;worstMs=ms;}
        // Pixel work: the cost model's pixel, probe and mask-scan terms, without loop steps and roots.
        worstPixelUs=std::max(worstPixelUs,FxValidate::pixelUs(s.plots,s.probes,s.scans,s.fills,s.spans));
        worstNativeUs=std::max(worstNativeUs,FxValidate::pixelUs(native.plots(),native.probes(),native.scans(),native.fills(),native.spans()));
        worstOps=std::max(worstOps,s.ops);
        assert(s.ops<=p.maxOps && s.draws<=p.maxDraws && !s.stopped);
        unsigned d=0,c=0;
        for(int i=0;i<135*240;++i) {
          d+=want[i]!=got[i];c+=want[i]!=base[i] || got[i]!=base[i];
          if(got[i]==base[i])continue;
          const int row=i/135;
          // The rules: UI ink changes only through the readout primitives,
          // near the readout and before the readout end; light lands only
          // outside the halo, except where the readout itself moved.
          if(base[i]) {
            assert(ms<p.readoutEnd && row<=46+6);
            if(!port.movesReadout) {
              const uint16_t was=swap16(base[i]),now=swap16(got[i]);
              assert((now>>11)>=(was>>11) && ((now>>5)&63)>=((was>>5)&63) && (now&31)>=(was&31));
            }
          } else if(!port.movesReadout || ms>=p.readoutEnd || row<13-6 || row>46+6)assert(!halo[i]);
        }
        if(ms>=p.duration)assert(got==base);
        ++frames;same+=d==0;differing+=d;changed+=c;
      }
    }
    std::printf("%s: %u bytes, %u/%u frames identical to native, %.3f%% of changed pixels differ, worst frame %.0f us modelled at %u ms (native %.0f us), %u instructions at most (bound %u)\n",
                port.name,unsigned(file.size()),same,frames,100*differing/std::max(1.0,changed),worstUs,worstMs,worstNativeUs,worstOps,p.maxOps);
    assert(same>=port.minSame*frames && differing<=port.maxDiff*changed+.5);
    // The primitives are the built-ins' own, so the pixel work matches the
    // native effect's; the bytecode is the only added cost. (As written,
    // looping over points and columns, every port is over the 8 ms budget:
    // the design note's "Instruction budget".)
    assert(worstPixelUs<=worstNativeUs*1.03+50);
    worstAll=std::max(worstAll,worstUs);
    // A custom theme's background: the same pixels change as on black.
    const uint16_t bg=0x0841;std::array<uint16_t,135*240> themed=base;
    for(auto &px:themed)if(!px)px=swap16(bg);
    for(unsigned ms:{40u,300u,700u,1200u,2600u,3800u,4600u}) {
      auto black=base;engine.draw({black.data(),135,240},p,ms,accent(Theme::Mint));
      auto out=themed;engine.draw({out.data(),135,240},p,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
  }

  // ---- the engine's own idiom ----
  // Effects written with the loop primitives (emitters, strokes, arc
  // trails, clouds, rays...) instead of one run per point keep every rule
  // and run at most about two thousand instructions a frame, most a few
  // hundred, where one run per point would take tens of thousands.
  for(const char *name:{"orbit","sparkles","slash","rupture","tidal","crimson","bladework","overdrive","summon","boundless017","sunblade","waterleaf"}) {
    const auto file=compileFile(std::string("firmware/tools/fx/effects/idiom/")+name+".fx");
    const auto r=FxValidate::check(file,{FxValidate::fixture(),FxValidate::fixture(0x0841)},FxValidate::Cost(),1e9);
    if(!r.ok)std::fprintf(stderr,"idiom %s: %s\n",name,r.problem.c_str());
    std::printf("idiom %s: %u bytes, worst frame %.0f us modelled at %u ms, %u instructions at most\n",name,unsigned(file.size()),r.worstUs,r.worstMs,r.worstOps);
    assert(r.ok && r.worstOps<2500);
  }
  // place: the parts read every frame copied to internal RAM play the same
  // frames as the file itself; a file whose parts exceed the copy, or an
  // unaligned copy, is refused and the program left as it was.
  for(const char *name:{"waterleaf","summon","bladework"}) {
    const auto file=compileFile(std::string("firmware/tools/fx/effects/idiom/")+name+".fx");
    Program direct,placed;assert(load(file.data(),file.size(),direct)==Error::None && load(file.data(),file.size(),placed)==Error::None);
    alignas(4) static uint8_t hot[HOT_BYTES+4];
    assert(!place(placed,hot+1,HOT_BYTES) && placed.code==direct.code);
    assert(!place(placed,hot,hotBytes(placed)-1) && placed.code==direct.code);
    assert(place(placed,hot,HOT_BYTES) && placed.code==hot && placed.args>=hot && placed.args+placed.argsLength<=hot+HOT_BYTES);
    for(uint32_t ms=0;ms<placed.duration;ms+=250) {
      auto a=base,b=base;
      engine.draw({a.data(),135,240},direct,ms,0x07ff);engine.draw({b.data(),135,240},placed,ms,0x07ff);
      assert(a==b);
    }
  }
  // A sprite and a table: each sprite pixel is light where its class is set,
  // and a table entry is clamped to the table.
  {
    const auto f=compileText("effect \"Stamp\"\nduration 2000\nsprite dot (\"1.2\" \"...\" \"3.1\")\n"
      "frame {\n x = table(t*10, 20, 60, 100)\n stamp(dot, x, 200, 2, 0, white, rgb(255, 0, 0), rgb(0, 0, 255), 1)\n}\n");
    Program q;assert(load(f.data(),f.size(),q)==Error::None && q.spriteCount==1 && q.tableCount==1);
    auto out=base;engine.draw({out.data(),135,240},q,0,0x07ff);
    // t = 0: entry 0, x = 20; a 3x3 sprite at scale 2 is 6x6 pixels from (20, 200).
    auto px=[&](int x,int y){return swap16(out[size_t(y*135+x)]);};
    assert(px(20,200)==0xffff && px(21,201)==0xffff && px(22,200)==0 && px(24,200)==0xf800 && px(20,204)==0x001f && px(24,204)==0xffff);
    unsigned lit=0;for(int i=0;i<135*240;++i)lit+=out[size_t(i)]!=base[size_t(i)];
    assert(lit==4*4);
    out=base;engine.draw({out.data(),135,240},q,1900,0x07ff);  // t*10 = 19: clamped to the last entry
    assert(px(100,200)==0xffff);
  }

  // ---- the round-3 primitives and colour selects ----
  auto drawText=[&](const std::string &body,uint32_t ms) {
    const auto f=compileText("effect \"P\"\nduration 2000\nreadout 1000\n"+body);
    Program q;assert(load(f.data(),f.size(),q)==Error::None);
    auto out=base;engine.draw({out.data(),135,240},q,ms,0x07ff);return out;
  };
  auto at=[](const std::array<uint16_t,135*240> &out,int x,int y){return swap16(out[size_t(y*135+x)]);};
  auto lit=[&](const std::array<uint16_t,135*240> &out){std::vector<std::pair<int,int>> v;for(int i=0;i<135*240;++i)if(out[size_t(i)]!=base[size_t(i)])v.push_back({i%135,i/135});return v;};
  // The cost table's per-primitive steps are the engine's own loop steps:
  // an emitter's are its particles, however many pixels the renderer's
  // spark counts as touches inside it (it counts its area since #46).
  {
    drawText("frame {\n emit(24, salt: 7, life: 2, x: 67, y: 180, spreadX: 30, spreadY: 20, speed: 10, color: white, k: 1, style: 3)\n}\n",300);
    const auto &st=engine.stats();
    assert(st.particles>0 && engine.primTouches[EMIT]==st.particles && st.touches>st.particles);
  }
  // A colour ?: is one CSEL; a table of colours, one lookup per channel.
  {
    const std::string body="frame {\n c = t > 1 ? rgb(255, 0, 0) : rgb(0, 0, 255)\n plot(60, 200, c, 1)\n plot(70, 200, table(t*2, rgb(0, 255, 0), white), 1)\n}\n";
    const auto f=compileText("effect \"P\"\nduration 2000\n"+body);Program q;assert(load(f.data(),f.size(),q)==Error::None);
    unsigned csel=0,ldx=0;for(unsigned i=0;i<q.codeLength;++i){csel+=q.code[4*i]==CSEL;ldx+=q.code[4*i]==LDX;}
    assert(csel==1 && ldx==3);
    auto early=drawText(body,100),late=drawText(body,1500);
    assert(at(early,60,200)==0x001f && at(early,70,200)==0x07e0 && at(late,60,200)==0xf800 && at(late,70,200)==0xffff);
  }
  // disc: the ordered dither lights every pixel at 16, none at 0, about half at 8.
  {
    const auto all=lit(drawText("frame {\n disc(67, 200, 9, 6, white, 1, dither: 16)\n}\n",100));
    const auto half=lit(drawText("frame {\n disc(67, 200, 9, 6, white, 1, dither: 8)\n}\n",100));
    assert(all.size()>100 && lit(drawText("frame {\n disc(67, 200, 9, 6, white, 1, dither: 0)\n}\n",100)).empty());
    assert(half.size()*2>=all.size()-8 && half.size()*2<=all.size()+8);
    for(auto [x,y]:all)assert(std::abs(x-67)<=9 && std::abs(y-200)<=7);
  }
  // A cloud: a puff wholly inside an earlier one adds nothing (its body is
  // claimed, its rim hidden); puffs alone draw nothing until the cloud.
  {
    const auto one=drawText("frame {\n puff(67, 200, 12, 1)\n cloud(rgb(150, 160, 185), white)\n}\n",100);
    const auto two=drawText("frame {\n puff(67, 200, 12, 1)\n puff(66, 201, 5, 1)\n cloud(rgb(150, 160, 185), white)\n}\n",100);
    assert(!lit(one).empty() && one==two && lit(drawText("frame {\n puff(67, 200, 12, 1)\n}\n",100)).empty());
  }
  // arc: the lower half of the ellipse, every column lit; the upper half with a negative height.
  {
    const auto low=lit(drawText("frame {\n arc(67, 190, 30, 20, white, 1)\n}\n",100));
    const auto high=lit(drawText("frame {\n arc(67, 190, 30, -20, white, 1)\n}\n",100));
    std::array<bool,135> column{};for(auto [x,y]:low){assert(y>=190 && y<=211);column[size_t(x)]=true;}
    for(int x=37;x<=97;++x)assert(column[size_t(x)]);
    assert(!high.empty());for(auto [x,y]:high)assert(y<=190);
  }
  // heatring: only glyph pixels between the radii change, and none after the readout end.
  {
    const std::string body="frame {\n heatring(20, 100, 60, 64, rgb(255, 240, 186), 1)\n}\n";
    const auto v=lit(drawText(body,100));assert(!v.empty());
    for(auto [x,y]:v){const int d=(x-20)*(x-20)+(y-100)*(y-100);assert(base[size_t(y*135+x)]!=0 && d>=60*60 && d<=64*64);}
    assert(lit(drawText(body,1500)).empty());
  }
  // vessel: light only inside the glass, below the surface, mirror symmetric about its axis.
  {
    const auto out=drawText("frame {\n vessel(67, 190, 223, 16, 13, 191.5, rgb(96, 196, 255), rgb(54, 140, 245), 1, 3.5, 0)\n}\n",100);
    const auto v=lit(out);assert(v.size()>100);
    for(auto [x,y]:v){assert(y>=193 && y<=222 && std::abs(x-67)<=15);assert(out[size_t(y*135+x)]==out[size_t(y*135+134-x)]);}
  }
  // rays: none of zero length; otherwise all between the circle and its reach.
  {
    assert(lit(drawText("frame {\n rays(67, 200, 5, 12, 0, color: white)\n}\n",100)).empty());
    const auto v=lit(drawText("frame {\n rays(67, 200, 5, 12, 4, color: white)\n}\n",100));assert(v.size()>=12);
    for(auto [x,y]:v){const float d=std::sqrt(float((x-67)*(x-67)+(y-200)*(y-200)));assert(d>=3.5f && d<=10.5f);}
  }
  // runes: n glyphs of one to four pixels each along the row.
  {
    assert(lit(drawText("frame {\n runes(30, 200, 4, 0, 0, white)\n}\n",100)).empty());
    const auto v=lit(drawText("frame {\n runes(30, 200, 4, 0, 10, white)\n}\n",100));
    assert(v.size()>=10 && v.size()<=40);for(auto [x,y]:v)assert(x>=29 && x<=67 && y>=198 && y<=200);
  }
  // aura: shells round the readout's outline, never on its halo, within five rows of it.
  {
    assert(lit(drawText("frame {\n aura(0, white)\n}\n",100)).empty());
    const auto v=lit(drawText("frame {\n aura(3, white, accent, 1, 0, 1, 1)\n}\n",100));assert(v.size()>100);
    for(auto [x,y]:v)assert(!halo[size_t(y*135+x)] && y>=11 && y<=49);
  }
  // stamp: each class has its own weight; a class at weight 0 lights nothing.
  {
    const std::string sprite="sprite two (\"12\" \"21\")\n";
    const auto both=lit(drawText(sprite+"frame {\n stamp(two, 60, 200, 1, 0, white, accent, k: 1)\n}\n",100));
    const auto first=drawText(sprite+"frame {\n stamp(two, 60, 200, 1, 0, white, accent, k: 1, k2: 0)\n}\n",100);
    assert(both.size()==4 && lit(first).size()==2 && at(first,60,200)==0xffff && at(first,61,201)==0xffff);
  }

  // The sound (control-rate voices) has its own suite: test_fx_sound.

  // ---- the loader ----
  const auto orbit=compileFile("firmware/tools/fx/effects/orbit.fx");
  Program p;
  assert(load(orbit.data(),orbit.size(),p)==Error::None);
  auto refused=[&](std::vector<uint8_t> f,Error want,bool seal=true){if(seal)reseal(f);Program q;const Error e=load(f.data(),f.size(),q);if(e!=want)std::cerr<<"got "<<label(e)<<" want "<<label(want)<<"\n";assert(e==want);};
  {auto f=orbit;f[0]='X';refused(f,Error::Magic);}
  {auto f=orbit;f[4]=2;refused(f,Error::Format);}
  {auto f=orbit;f[5]=ENGINE+1;refused(f,Error::Engine);}
  {auto f=orbit;f[5]=0;refused(f,Error::Engine);}
  {auto f=orbit;f[40]^=1;refused(f,Error::Checksum,false);}
  {auto f=orbit;f.push_back(0);refused(f,Error::Size,false);}
  {auto f=orbit;f.resize(20);refused(f,Error::Size,false);}
  {std::vector<uint8_t> f(MAX_FILE+1,0);refused(f,Error::Size,false);}
  {auto f=orbit;put16(f.data()+18,999);refused(f,Error::Header);}               // duration under 1 s
  {auto f=orbit;put16(f.data()+18,6001);refused(f,Error::Header);}              // over 6 s
  {auto f=orbit;put16(f.data()+20,2201);refused(f,Error::Header);}              // readout end past 2.2 s
  {auto f=orbit;put16(f.data()+18,2000);put16(f.data()+20,1600);refused(f,Error::Header);} // past 3/4 of the duration
  {auto f=orbit;put16(f.data()+22,2801);refused(f,Error::Header);}              // sound over 2.8 s
  {auto f=orbit;put16(f.data()+6,1);refused(f,Error::Header);}                  // undefined flags
  {auto f=orbit;f.insert(f.end(),{200,1,0,0});refused(f,Error::Section);}       // unknown required section
  {auto f=orbit;f.insert(f.end(),{200,0,2,0,7,7});Program q;reseal(f);assert(load(f.data(),f.size(),q)==Error::None);} // unknown optional: skipped
  {auto f=orbit;f.insert(f.end(),{NAME,0,1,0,'x'});refused(f,Error::Section);}  // a section twice
  {auto f=orbit;f.insert(f.end(),{7,0,9,0});refused(f,Error::Section);}         // runs past the end
  // Edits inside the sections, found by walking them.
  auto sectionAt=[&](const std::vector<uint8_t> &f,uint8_t type){for(size_t at=HEADER_BYTES;at+4<=f.size();at+=4+get16(f.data()+at+2))if(f[at]==type)return at;return size_t(0);};
  {auto f=orbit;f[sectionAt(f,NAME)+4]=0x7f;refused(f,Error::Name);}
  {auto f=orbit;f[sectionAt(f,NAME)+4]=' ';refused(f,Error::Name);}
  const size_t code=sectionAt(orbit,CODE)+4,blocks=sectionAt(orbit,BLOCKS)+4,consts=sectionAt(orbit,CONSTS)+4;
  {auto f=orbit;f[code]=OPS;refused(f,Error::Opcode);}
  {auto f=orbit;f[code]=MOV;f[code+1]=T;refused(f,Error::Register);}             // writes a built-in
  {auto f=orbit;f[code]=MOV;f[code+1]=FIRST_CONST;refused(f,Error::Register);}   // writes a constant
  {auto f=orbit;f[code]=MOV;f[code+1]=40;f[code+2]=BUILTINS;refused(f,Error::Register);} // reads an undefined register
  {auto f=orbit;f[code]=ADD;f[code+1]=40;f[code+2]=T;f[code+3]=255;refused(f,Error::Register);} // reads past the constants
  {auto f=orbit;f[code]=DRAW;f[code+1]=PRIMS;refused(f,Error::Primitive);}
  {auto f=orbit;f[code]=DRAW;f[code+1]=PLOT;f[code+2]=0xff;f[code+3]=0xff;refused(f,Error::Args);}
  {auto f=orbit;f[code]=LDK;f[code+1]=40;f[code+2]=0xff;f[code+3]=3;refused(f,Error::Consts);}
  {auto f=orbit;f[code]=CMIX;f[code+1]=FIRST_CONST-2;f[code+2]=T;f[code+3]=T;refused(f,Error::Register);} // colour past the temps
  {auto f=orbit;put16(f.data()+blocks+4,0);refused(f,Error::Blocks);}            // count 0
  {auto f=orbit;put16(f.data()+blocks+4,MAX_COUNT+1);refused(f,Error::Blocks);}
  {auto f=orbit;put16(f.data()+blocks+2,0xffff);refused(f,Error::Blocks);}       // runs past the code
  {auto f=orbit;put16(f.data()+blocks+6,0x4000);refused(f,Error::Blocks);}       // unknown block flag
  {auto f=orbit;put16(f.data()+blocks+6,GUARDED|BUILTINS);refused(f,Error::Blocks);} // guard on an undefined register
  {auto f=orbit;put16(f.data()+blocks+4,MAX_COUNT);put16(f.data()+blocks+8+4,MAX_COUNT);refused(f,Error::Budget);} // over the frame's instruction cap
  {auto f=orbit;const uint32_t nan=0x7fc00000;LoadableFx::put32(f.data()+consts,nan);refused(f,Error::Consts);}

  // ---- the engine ----
  // The interpreter's inline cases compute what Math::apply (the compiler's
  // constant folding) computes, bit for bit, on edge and random values.
  {
    const float edges[]={0,-0.0f,.5f,-.5f,1,-1,2.5f,-2.5f,1e-30f,3.4e38f,-3.4e38f,8388608.5f,-8388607.5f,16777217.0f,1e6f,-1e6f,
                         3.14159265f,123.456f,-987.25f,std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::quiet_NaN()};
    uint32_t seed=12345;auto next=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    auto pick=[&]{const uint32_t r=next();if(r%3==0)return edges[r/3%(sizeof(edges)/sizeof(*edges))];float v;const uint32_t bits=next();std::memcpy(&v,&bits,4);return r%3==1?v:(float(int32_t(next()))/65536.0f);};
    for(uint8_t op=MOV;op<OPS;++op) {
      if(op==CMIX || op==CSEL || op==EXIT || op==DRAW || op==LDK || op==LDX)continue;
      for(int k=0;k<4000;++k) {
        Program q;alignas(4) uint8_t in[4]={op,40,41,42};q.code=in;q.codeLength=1;
        float R[REGS]={};R[41]=pick();R[42]=pick();R[40]=pick();
        const float want=Math::apply(op,R[41],R[42],R[40]);uint32_t ops=0;
        execute(q,R,0,1,ops,[](uint8_t,const uint8_t *){return true;});
        const bool same=std::memcmp(&want,&R[40],4)==0 || (want!=want && R[40]!=R[40]);
        if(!same)std::fprintf(stderr,"op %u: %g %g -> %g, apply %g\n",op,R[41],R[42],R[40],want);
        assert(same);
      }
    }
  }
  // Readout primitives do nothing from the readout end on: a file whose
  // readout end is 0 never moves a glyph.
  {
    const auto f=compileText("effect \"Shifty\"\nduration 2000\nreadout 0\nrepeat 40 {\n shiftrow(ay0 + i, 5)\n shade(white, 1, 1)\n glint(0, 1)\n}\n");
    Program q;assert(load(f.data(),f.size(),q)==Error::None);
    for(unsigned ms=0;ms<2000;ms+=50){auto out=base;engine.draw({out.data(),135,240},q,ms,0x07ff);assert(out==base && engine.stats().draws==120);}
  }
  // Wild numbers are clamped: no NaN, infinity or huge coordinate reaches a primitive.
  {
    const auto f=compileText("effect \"Wild\"\nduration 2000\nrepeat 16 {\n big = 1e30*i\n nan = big*big - big*big\n"
      " line(big, -big, nan, big, color(big, -big, nan), big)\n spark(nan, 1e9, white, 1e9, big)\n ellipse(acx, acy, big, big, accent, 1, big, big, big)\n"
      " glow(acx, acy, big, big, white, 1)\n bolt(0, 0, big, big, big, 1, big)\n rimbolt(0, 0, -big, big, nan, white, accent, 1, 99)\n fire(big, big, big)\n"
      " prime(-big, big)\n star(big, 3, big, white)\n twinkle(-1e20, 1e20, white, 1, 99)\n fline(0, 0, 1e7, 1e7, white, 1, 1)\n clip(-big)\n}\n");
    Program q;assert(load(f.data(),f.size(),q)==Error::None);
    for(unsigned ms=0;ms<2000;ms+=100){auto out=base;engine.draw({out.data(),135,240},q,ms,0x07ff);
      for(int i=0;i<135*240;++i)if(out[i]!=base[i])assert(!base[i] && !halo[i]);}
  }
  // The guard: an expired clock stops the frame before its first draw; a
  // clock that runs out or an instruction cap stops it later, and what was
  // drawn stays.
  {
    Guard stop;stop.expired=[](void *)->bool{return true;};
    auto out=base;engine.draw({out.data(),135,240},p,1100,0x07ff,BestEffectUI::Renderer::Anchor(),0,stop);
    assert(out==base && engine.stats().stopped && engine.stats().draws==0);
    struct Count{unsigned left;} count{40};
    Guard some;some.context=&count;some.expired=[](void *c)->bool{auto *k=static_cast<Count *>(c);return k->left--==0;};
    out=base;engine.draw({out.data(),135,240},p,1100,0x07ff,BestEffectUI::Renderer::Anchor(),0,some);
    // The clock is read between draws, every eighth run and inside long primitives.
    assert(engine.stats().stopped && engine.stats().draws>0 && engine.stats().draws<=40 && out!=base);
    Guard few;few.maxOps=500;
    out=base;engine.draw({out.data(),135,240},p,1100,0x07ff,BestEffectUI::Renderer::Anchor(),0,few);
    assert(engine.stats().stopped && engine.stats().ops<=500+64);
  }
  // Nothing at or after the duration, and nothing without a canvas.
  {
    auto out=base;assert(!engine.draw({out.data(),135,240},p,p.duration,0x07ff) && out==base);
    assert(!engine.draw({nullptr,135,240},p,100,0x07ff));
  }
  // Constants past the 96 registers are loaded with LDK, outside the
  // busiest loops when they can be.
  {
    std::string text="effect \"Pool\"\nduration 2000\nframe {\n";
    for(int k=0;k<120;++k)text+=" v"+std::to_string(k)+" = t*"+std::to_string(k+1000)+".5\n";
    text+=" plot(v0, v119, white, 1)\n}\nrepeat 30 {\n plot(i + 300.25, 50, white, 1)\n}\n";
    const auto f=compileText(text);Program q;assert(load(f.data(),f.size(),q)==Error::None && q.constCount>MAX_CONSTS);
    auto out=base;engine.draw({out.data(),135,240},q,100,0x07ff);
  }

  // ---- the compiler ----
  auto fails=[](const char *text,int line,const char *what) {
    const auto r=FxCompiler::compile(text);
    if(r.ok || r.line!=line || r.error.find(what)==std::string::npos)std::cerr<<"compile: line "<<r.line<<" '"<<r.error<<"' want "<<line<<" '"<<what<<"'\n";
    assert(!r.ok && r.line==line && r.error.find(what)!=std::string::npos);
  };
  // Names come from strangers' codes: what the device can show, nothing else.
  for(const char *bad:{"","Fifteen chars!!"," Lead","Trail "})
    fails((std::string("effect \"")+bad+"\"\nduration 2000\nframe {\n dot(acx, acy, accent, 1)\n}\n").c_str(),1,"the name");
  for(const char *bad:{"A\xe2\x80\xae" "B","Bell\x07","Tab\tbed","Caf\xc3\xa9"})
    fails((std::string("effect \"")+bad+"\"\nduration 2000\nframe {\n dot(acx, acy, accent, 1)\n}\n").c_str(),1,"plain letters");
  fails("effect \"A\"\nduration 2000\nframe {\n x = nope\n}\n",4,"unknown name");
  fails("effect \"A\"\nduration 2000\nframe {\n t = 1\n}\n",4,"cannot assign");
  fails("effect \"A\"\nduration 2000\nframe {\n plot(1, 2, 3, 4)\n}\n",4,"must be a colour");
  fails("effect \"A\"\nduration 2000\nframe {\n x = white\n x = 1\n}\n",5,"changes between");
  fails("effect \"A\"\nduration 2000\nrepeat 600 {\n plot(1, 2, white, 4)\n}\n",3,"1-512");
  fails("effect \"A\"\nduration 2000\nframe {\n splat(1)\n}\n",4,"unknown primitive");
  fails("effect \"A\"\nduration 2000\nframe {\n x = (1 + \n}\n",5,"expected a value");
  fails("duration 2000\nframe {\n plot(1, 2, white, 4)\n}\n",1,"missing effect name");
  fails("effect \"A\"\nduration 900\nframe {\n plot(1, 2, white, 4)\n}\n",0,"refused: header");
  // The fallback: a built-in named in the source, carried in META; Orbit when none.
  {
    const auto f=compileText("effect \"A\"\nduration 2000\nfallback \"Sparkles\"\nframe {\n plot(1, 2, white, 1)\n}\n");
    Program q;assert(load(f.data(),f.size(),q)==Error::None && q.fallback==uint8_t(Effect::Sparkles));
    const auto g=compileText("effect \"A\"\nduration 2000\nframe {\n plot(1, 2, white, 1)\n}\n");
    assert(load(g.data(),g.size(),q)==Error::None && q.fallback==Program::NO_FALLBACK);
  }
  fails("effect \"A\"\nduration 2000\nfallback \"Nope\"\nframe {\n plot(1, 2, white, 4)\n}\n",3,"unknown fallback effect");
  // Numbers out of range are refused, not converted (undefined behaviour the share-code fuzzing found).
  fails("effect \"A\"\nduration 501400\nframe {\n plot(1, 2, white, 4)\n}\n",2,"whole number 0-65535");
  fails("effect \"A\"\nduration 2000\nreadout 2.5\nframe {\n plot(1, 2, white, 4)\n}\n",3,"whole number 0-65535");
  fails("effect \"A\"\nduration 2000\nframe {\n plot(1, 2, rgb(300, 0, 0), 4)\n}\n",4,"rgb takes three constant numbers 0-255");
  fails("effect \"A\"\nduration 2000\nrepeat 1e38*10 - 1e38*10 {\n plot(1, 2, white, 4)\n}\n",3,"repeat needs a whole number");
  fails("effect \"A\"\nduration 2000\nframe {\n plot(1e300, 2, white, 4)\n}\n",4,"number out of range");
  // A constant folded past float range (1e38*10) is refused by the loader's check of the pool, with a message.
  {const auto r=FxCompiler::compile("effect \"A\"\nduration 2000\nframe {\n plot(1e38*10, 2, white, 4)\n}\n");assert(!r.ok);}
  std::printf("slowest modelled frame of any port: %.0f us (budget %.0f)\n",worstAll,FxValidate::BUDGET_US);
  // The validator: the worst-case file is refused as over budget, and a light
  // file passes every rule.
  {
    const auto worst=compileFile("firmware/tools/fx/effects/worst.fx");
    const auto r=FxValidate::check(worst,{FxValidate::fixture()});
    assert(!r.ok && r.problem.find("over the frame budget")!=std::string::npos);
    const auto light=compileText("effect \"Light\"\nduration 2000\nreadout 500\nframe {\n glint(.1, .3)\n}\nrepeat 12 {\n a = i*.52 + t\n"
      " plot(acx + 50*cos(a), acy + 20*sin(a) + 30, accent, 1 - t/2)\n}\n");
    const auto ok=FxValidate::check(light,{FxValidate::fixture(),FxValidate::fixture(0x0841)});
    if(!ok.ok)std::fprintf(stderr,"light: %s\n",ok.problem.c_str());
    assert(ok.ok && ok.worstUs<8000);
    // A file whose playing part outgrows the 4 KB internal-RAM copy is refused.
    std::string big="effect \"Big\"\nduration 2000\nframe {\n";
    for(int k=0;k<200;++k)big+=" v"+std::to_string(k%40)+" = t*"+std::to_string(k)+".25 + "+std::to_string(k)+".5\n";
    big+=" plot(v0, v1, white, 1)\n}\n";
    const auto r2=FxValidate::check(compileText(big),{FxValidate::fixture()});
    assert(!r2.ok && r2.problem.find("too large for internal RAM")!=std::string::npos);
  }
  std::puts("test_fx_effects: ok");
}
