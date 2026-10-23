// Renders loaded effects on the recap as the device composes it (the real
// M5GFX canvas and fonts, tools/ui_render/ui_catalog.h) next to the built-in
// effect of the same name: one PPM per 50 ms frame of each, and a CSV of
// per-frame differences and engine work (and prims.csv, per primitive).
// Host timing is not device timing.
//   render_fx OUT_DIR FILE.fx...
#include <M5GFX.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include "ui_catalog.h"
#include "fx_compiler.h"

static void writePpm(lgfx::LGFX_Sprite &g,const std::string &path) {
  FILE *f=std::fopen(path.c_str(),"wb");if(!f){std::perror(path.c_str());std::exit(1);}
  std::fprintf(f,"P6\n%d %d\n255\n",g.width(),g.height());
  const auto *p=static_cast<const uint8_t *>(g.getBuffer());
  for(int i=0;i<g.width()*g.height();++i) {
    const unsigned v=p[2*i]<<8|p[2*i+1];
    const uint8_t rgb[3]={uint8_t(((v>>11)&31)*255/31),uint8_t(((v>>5)&63)*255/63),uint8_t((v&31)*255/31)};
    std::fwrite(rgb,1,3,f);
  }
  std::fclose(f);
}

int main(int argc,char **argv) {
  if(argc<3){std::fprintf(stderr,"usage: render_fx OUT_DIR FILE.fx...\n");return 2;}
  const std::string out=argv[1];
  const Palette::Colors colors=Themes::builtIn(Appearance::Theme::Classic);
  lgfx::LGFX_Sprite canvas;canvas.setColorDepth(16);
  if(!canvas.createSprite(UiCatalog::WIDTH,UiCatalog::HEIGHT)){std::fprintf(stderr,"no canvas\n");return 1;}
  const size_t n=size_t(UiCatalog::WIDTH)*UiCatalog::HEIGHT;
  BestEffectUI::Renderer renderer;LoadableFx::Engine engine(renderer);
  FILE *csv=std::fopen((out+"/frames.csv").c_str(),"w");
  // Each frame's calls and loop steps per primitive (by its number in fx_effect.h's Prim), for fitting their device costs.
  FILE *prims=std::fopen((out+"/prims.csv").c_str(),"w");
  std::fprintf(prims,"file,elapsed_ms,prim,calls,touches\n");
  std::fprintf(csv,"file,effect,elapsed_ms,differing_px,ops,draws,plots,probes,scans,touches,roots,particles,columns,points,pixels,render_us\n");
  for(int a=2;a<argc;++a) {
    std::ifstream in(argv[a]);std::stringstream text;text<<in.rdbuf();
    const auto r=FxCompiler::compile(text.str());
    if(!r.ok){std::fprintf(stderr,"%s:%d: %s\n",argv[a],r.line,r.error.c_str());return 1;}
    LoadableFx::Program p;LoadableFx::load(r.bytes.data(),r.bytes.size(),p);
    Appearance::Effect native=Appearance::Effect::Off;
    for(unsigned e=0;e<Appearance::EFFECTS;++e)if(Appearance::drawn(e) && !std::strcmp(Appearance::label(Appearance::Effect(e)),p.name))native=Appearance::Effect(e);
    std::vector<uint16_t> want(n);
    for(uint32_t ms=0;ms<p.duration+250;ms+=50) {
      char name[96];
      if(native!=Appearance::Effect::Off) {
        UiCatalog::celebration(canvas,colors,false,15500,"NEW BEST",native,ms);
        std::memcpy(want.data(),canvas.getBuffer(),n*2);
        std::snprintf(name,sizeof(name),"/%s_native_%03u.ppm",p.name,unsigned(ms/50));writePpm(canvas,out+name);
      }
      UiCatalog::celebration(canvas,colors,false,15500,"NEW BEST",Appearance::Effect::Off,ms);
      engine.draw({static_cast<uint16_t *>(canvas.getBuffer()),UiCatalog::WIDTH,UiCatalog::HEIGHT},p,ms,colors.accent,BestEffectUI::Renderer::Anchor(),colors.background);
      std::snprintf(name,sizeof(name),"/%s_loaded_%03u.ppm",p.name,unsigned(ms/50));writePpm(canvas,out+name);
      const auto *got=static_cast<const uint16_t *>(canvas.getBuffer());unsigned differ=0;
      if(native!=Appearance::Effect::Off)for(size_t i=0;i<n;++i)differ+=got[i]!=want[i];
      const auto &s=engine.stats();
      std::fprintf(csv,"%s,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.0f\n",argv[a],p.name,unsigned(ms),differ,unsigned(s.ops),unsigned(s.draws),unsigned(s.plots),unsigned(s.probes),unsigned(s.scans),
                   unsigned(s.touches),unsigned(s.roots),unsigned(s.particles),unsigned(s.columns),unsigned(s.points),unsigned(s.pixels),s.renderUs);
      for(unsigned k=0;k<LoadableFx::PRIMS;++k)if(engine.primCalls[k])
        std::fprintf(prims,"%s,%u,%u,%u,%u\n",argv[a],unsigned(ms),k,unsigned(engine.primCalls[k]),unsigned(engine.primTouches[k]));
    }
  }
  std::fclose(csv);std::fclose(prims);
  return 0;
}
