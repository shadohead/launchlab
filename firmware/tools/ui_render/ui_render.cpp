// Renders every catalog screen with the firmware's drawing code on a real
// M5GFX canvas, for each built-in theme and both Flip choices (or for one
// custom theme), and prints one FNV-1a 64 hash per frame. With an output
// directory it also writes each frame as a PPM.
//
//   ui_render [--out DIR] [--custom HEX16x8 NAME] [--celebrations] [--legibility] [--settled] [--costs FILE]
//             [--bench-hashes STEP_MS]
//   ui_render --fx FILE.fx...
//   ui_render --fx-sound FILE.fx...
//
// --celebrations instead renders every drawn effect frame by frame (50 ms,
// the device's cadence) on the recap of a 15,500 RPM pull labelled NEW BEST,
// in both Flip choices: the frames the web milestone editor previews.
// --legibility compares each of those frames with the same recap without the
// effect and prints, per frame, the UI ink changed below the header (not
// counted while an effect covers those rows, Renderer::covers) and the
// header pixels readable at 3:1 on the background (with the effect / without).
// --settled renders, for every drawn effect, the recap from the end of its
// duration through the following second and prints whether each frame equals
// the same recap without an effect: what the device and the web previews show
// once a celebration is over.
// --costs FILE reads recorded device frame times (fixtures/effect_device_frames.txt:
// an effect, the hash of its frames when recorded, then ms:us pairs; a
// `# sound: low` line means they were recorded with Sound on) and prints,
// per effect, the hash of its frames now at those times, the recorded hash,
// the device's slowest frame, the model's (Renderer::modelledUs(), with the
// speaker's share while a sound plays) and the median of device minus model
// over the frames, so check_ui_render can hold the model to the device.
// --bench-hashes STEP_MS prints, per drawn effect, the hash HIL `!effectbench
// STEP_MS... fixed` reports: its frames every STEP_MS from STEP_MS/2 drawn on
// Renderer::benchCanvas(), so a device build's frames can be held to the host's.
//
// --fx compiles each effect source as the web effects library does
// (fx_preview.h) and prints, per effect, the hashes of its compiled file,
// its share code and the file recompiled from that code, the validator's
// verdict on the real recaps, and every 50 ms frame on the default theme's
// recap and, flipped, on a light theme's: what check_wasm.mjs holds the
// browser build to.
//
// --mic renders the Mic RPM page's screens.
//
// tests/check_ui_render.py compares the hashes with fixtures/ui_render.txt,
// so any change to how a built-in theme draws is caught.
#include <M5GFX.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "fx_preview.h"

static uint64_t fnv64(const uint8_t *p,size_t n){uint64_t h=0xcbf29ce484222325ull;while(n--){h^=*p++;h*=1099511628211ull;}return h;}

int main(int argc,char **argv) {
  std::string out,costs;Palette::Custom custom;bool useCustom=false,celebrations=false,legibility=false,settled=false,mic=false;unsigned benchStep=0;
  std::vector<std::string> effects,sounds;
  for(int i=1;i<argc;++i) {
    const std::string arg=argv[i];
    if(arg=="--out" && i+1<argc)out=argv[++i];
    else if(arg=="--custom" && i+2<argc) {
      const std::string roles=argv[++i];useCustom=true;
      for(unsigned r=0;r<Palette::ROLES;++r)custom.role[r]=uint16_t(std::strtoul(roles.substr(r*4,4).c_str(),nullptr,16));
      std::strncpy(custom.name,argv[++i],Palette::Custom::MAX_NAME);
    } else if(arg=="--celebrations")celebrations=true;
    else if(arg=="--mic")mic=true;
    else if(arg=="--legibility")legibility=true;
    else if(arg=="--settled")settled=true;
    else if(arg=="--costs" && i+1<argc)costs=argv[++i];
    else if(arg=="--bench-hashes" && i+1<argc)benchStep=unsigned(std::max(20,std::atoi(argv[++i])));
    else if(arg=="--fx" && i+1<argc)effects.assign(argv+i+1,argv+argc),i=argc;
    else if(arg=="--fx-sound" && i+1<argc)sounds.assign(argv+i+1,argv+argc),i=argc;
    else {std::fprintf(stderr,"usage: ui_render [--out DIR] [--custom ROLES_HEX NAME] [--celebrations] [--legibility] [--settled] [--costs FILE] [--bench-hashes STEP_MS] | --fx FILE.fx... | --fx-sound FILE.fx...\n");return 2;}
  }
  // --fx-sound: each effect's own sound as the device renders it
  // (FxPreview::sound): its sample count and the hash of its samples, or none.
  for(const auto &path:sounds) {
    std::ifstream in(path);std::stringstream text;text<<in.rdbuf();
    std::string base=path.substr(path.find_last_of('/')+1);base="fx-"+base.substr(0,base.find('.'));
    FxPreview::Effect fx;
    if(!in || !fx.compile(text.str())){std::fprintf(stderr,"%s:%d: %s\n",path.c_str(),fx.line(),fx.error().c_str());return 1;}
    const auto samples=FxPreview::sound(fx);
    if(samples.empty())std::printf("%s-sound none\n",base.c_str());
    else std::printf("%s-sound %zu %016llx\n",base.c_str(),samples.size(),(unsigned long long)fnv64(reinterpret_cast<const uint8_t *>(samples.data()),samples.size()*2));
  }
  if(!sounds.empty())return 0;
  lgfx::LGFX_Sprite canvas;canvas.setColorDepth(16);
  if(!canvas.createSprite(UiCatalog::WIDTH,UiCatalog::HEIGHT)){std::fprintf(stderr,"no canvas\n");return 1;}
  auto write=[&](const std::string &name) {
    const auto *pixels=static_cast<const uint8_t *>(canvas.getBuffer());
    const size_t bytes=size_t(UiCatalog::WIDTH)*UiCatalog::HEIGHT*2;
    std::printf("%s %016llx\n",name.c_str(),(unsigned long long)fnv64(pixels,bytes));
    if(out.empty())return true;
    FILE *f=std::fopen((out+"/"+name+".ppm").c_str(),"wb");if(!f){std::perror("ppm");return false;}
    std::fprintf(f,"P6\n%d %d\n255\n",UiCatalog::WIDTH,UiCatalog::HEIGHT);
    // Canvas pixels are RGB565 big-endian, as the device's `v` export sends them.
    for(size_t i=0;i<bytes;i+=2) {
      const unsigned v=pixels[i]<<8|pixels[i+1];
      const uint8_t rgb[3]={uint8_t(((v>>11)&31)*255/31),uint8_t(((v>>5)&63)*255/63),uint8_t((v&31)*255/31)};
      std::fwrite(rgb,1,3,f);
    }
    std::fclose(f);return true;
  };
  if(!effects.empty()) {
    auto hash=[](const void *p,size_t n){char h[17];std::snprintf(h,sizeof(h),"%016llx",(unsigned long long)fnv64(static_cast<const uint8_t *>(p),n));return std::string(h);};
    for(const auto &path:effects) {
      std::ifstream in(path);std::stringstream text;text<<in.rdbuf();
      if(!in){std::fprintf(stderr,"cannot read %s\n",path.c_str());return 1;}
      std::string base=path.substr(path.find_last_of('/')+1);base="fx-"+base.substr(0,base.find('.'));
      FxPreview::Effect fx,again;
      if(!fx.compile(text.str())){std::fprintf(stderr,"%s:%d: %s\n",path.c_str(),fx.line(),fx.error().c_str());return 1;}
      std::printf("%s-file %s\n",base.c_str(),hash(fx.file().data(),fx.file().size()).c_str());
      std::printf("%s-code %s\n",base.c_str(),hash(fx.code().data(),fx.code().size()).c_str());
      again.open(fx.code());
      std::printf("%s-reopened %s\n",base.c_str(),hash(again.file().data(),again.file().size()).c_str());
      const bool ok=fx.check(canvas);
      std::printf("%s-check %s\n",base.c_str(),ok?("ok "+std::to_string(int(fx.report().worstUs))+"@"+std::to_string(fx.report().worstMs)).c_str():fx.report().problem.c_str());
      for(bool light:{false,true})for(uint32_t ms=0;ms<fx.program().duration;ms+=50) {
        const Palette::Colors colors=light?FxPreview::lightTheme():Themes::builtIn(Appearance::Theme::Classic);
        fx.celebration(canvas,colors,light,15500,"NEW BEST",ms);
        char name[96];std::snprintf(name,sizeof(name),"%s-%s-%04u",base.c_str(),light?"light-flip":"theme0",unsigned(ms));
        if(!write(name))return 1;
      }
    }
    return 0;
  }
  if(legibility) {
    const Palette::Colors colors=useCustom?custom.colors():Themes::builtIn(Appearance::Theme::Classic);
    constexpr int HEADER=BestEffectUI::Renderer::NUMBER_BOTTOM+1; // the label, the readout and the glint's reach
    const size_t n=size_t(UiCatalog::WIDTH)*UiCatalog::HEIGHT;std::vector<uint16_t> base(n);
    auto pixel=[](const uint16_t *p,size_t i){return uint16_t(p[i]<<8|p[i]>>8);};
    for(bool flip:{false,true})for(unsigned e=0;e<Appearance::DRAWN;++e) {
      const auto effect=Appearance::drawnEffect(e);
      for(uint32_t ms=0;ms<Appearance::duration(effect);ms+=50) {
        UiCatalog::celebration(canvas,colors,flip,15500,"NEW BEST",Appearance::Effect::Off,ms);
        const auto *p=static_cast<const uint16_t *>(canvas.getBuffer());
        for(size_t i=0;i<n;++i)base[i]=pixel(p,i);
        UiCatalog::celebration(canvas,colors,flip,15500,"NEW BEST",effect,ms);
        unsigned changed=0,readable=0,readableBase=0;
        for(size_t i=0;i<n;++i) {
          const uint16_t was=base[i],now=pixel(p,i);
          // Below the header; an effect that covers it (Boundless) owns those
          // rows until it gives them back.
          if(int(i/UiCatalog::WIDTH)>=HEADER){changed+=ms>=BestEffectUI::Renderer::covers(effect) && was!=colors.background && now!=was;continue;}
          readableBase+=Palette::contrast100(was,colors.background)>=300;readable+=Palette::contrast100(now,colors.background)>=300;
        }
        std::printf("%s%s-%s-%04u changed_ink=%u readable=%u/%u\n",useCustom?"custom":"theme0",flip?"-flip":"",Appearance::label(effect),
          unsigned(ms),changed,readable,readableBase);
      }
    }
    return 0;
  }
  if(benchStep) {
    std::vector<uint16_t> pixels(size_t(UiCatalog::WIDTH)*UiCatalog::HEIGHT);BestEffectUI::Renderer renderer;
    const BestEffectUI::Surface surface{pixels.data(),UiCatalog::WIDTH,UiCatalog::HEIGHT};
    for(unsigned e=0;e<Appearance::DRAWN;++e) {
      const auto effect=Appearance::drawnEffect(e);uint32_t hash=2166136261u;
      for(uint32_t ms=benchStep/2;ms<Appearance::duration(effect);ms+=benchStep) {
        if(!BestEffectUI::Renderer::benchCanvas(surface)){std::fprintf(stderr,"benchCanvas needs 135x240\n");return 1;}
        renderer.draw(surface,effect,ms,BestEffectUI::Renderer::BENCH_ACCENT,BestEffectUI::Renderer::Anchor(),BestEffectUI::Renderer::BENCH_BACKGROUND);
        hash=BestEffectUI::Renderer::benchHash(surface,hash);
      }
      std::printf("%s %08lx\n",Appearance::label(effect),(unsigned long)hash);
    }
    return 0;
  }
  if(!costs.empty()) {
    const Palette::Colors colors=Themes::builtIn(Appearance::Theme::Classic);
    std::ifstream in(costs);if(!in){std::fprintf(stderr,"cannot read %s\n",costs.c_str());return 1;}
    BestEffectUI::Renderer renderer;std::string line;bool sound=false;
    while(std::getline(in,line)) {
      // Recorded with the speaker playing each effect's sound (Sound Low).
      if(line.rfind("# sound: ",0)==0){sound=line.find("off")==std::string::npos;continue;}
      if(line.empty() || line[0]=='#')continue;
      std::istringstream fields(line);std::string name,recorded,pair;fields>>name>>recorded;
      Appearance::Effect effect=Appearance::Effect::Off;
      for(unsigned e=0;e<Appearance::EFFECTS;++e)if(name==Appearance::label(Appearance::Effect(e)))effect=Appearance::Effect(e);
      if(effect==Appearance::Effect::Off){std::fprintf(stderr,"unknown effect %s\n",name.c_str());return 1;}
      uint64_t hash=0xcbf29ce484222325ull;unsigned device=0,deviceMs=0,modelMs=0;double model=0;std::vector<double> gaps;
      while(fields>>pair) {
        const unsigned ms=unsigned(std::stoul(pair)),us=unsigned(std::stoul(pair.substr(pair.find(':')+1)));
        UiCatalog::celebration(canvas,colors,false,15500,"NEW BEST",Appearance::Effect::Off,ms);
        auto *pixels=static_cast<uint16_t *>(canvas.getBuffer());
        renderer.draw({pixels,UiCatalog::WIDTH,UiCatalog::HEIGHT},effect,ms,colors.accent,BestEffectUI::Renderer::Anchor(),colors.background);
        hash=(hash^fnv64(reinterpret_cast<const uint8_t *>(pixels),size_t(UiCatalog::WIDTH)*UiCatalog::HEIGHT*2))*1099511628211ull;
        const double us_=renderer.modelledUs(sound && ms<Appearance::soundDuration(effect));
        if(us>device){device=us;deviceMs=ms;}
        if(us_>model){model=us_;modelMs=ms;}
        gaps.push_back(us-us_);
      }
      std::sort(gaps.begin(),gaps.end());
      std::printf("%s %016llx %s device=%u@%u model=%.0f@%u median_gap=%.0f\n",name.c_str(),(unsigned long long)hash,recorded.c_str(),device,deviceMs,model,modelMs,
        gaps.empty()?0.0:gaps[gaps.size()/2]);
    }
    return 0;
  }
  if(mic) {
    UiCatalog::Look look;
    if(useCustom){look.colors=custom.colors();look.theme=Themes::slotTheme(0);look.name=custom.name;}
    else {look.colors=Themes::builtIn(Appearance::Theme::Classic);look.theme=Appearance::Theme::Classic;}
    for(unsigned s=0;s<UiCatalog::MIC_COUNT;++s) {
      UiCatalog::micScreen(canvas,s,look);
      if(!write(std::string(useCustom?"custom-":"theme0-")+UiCatalog::MIC_SCREENS[s].id))return 1;
    }
    return 0;
  }
  if(settled) {
    const Palette::Colors colors=useCustom?custom.colors():Themes::builtIn(Appearance::Theme::Classic);
    const size_t n=size_t(UiCatalog::WIDTH)*UiCatalog::HEIGHT;std::vector<uint16_t> clean(n);
    for(bool flip:{false,true})for(unsigned e=0;e<Appearance::DRAWN;++e) {
      const auto effect=Appearance::drawnEffect(e);const uint32_t end=Appearance::duration(effect);
      for(uint32_t ms=end;ms<=end+1000;ms+=50) {
        UiCatalog::celebration(canvas,colors,flip,15500,"NEW BEST",Appearance::Effect::Off,ms);
        const auto *p=static_cast<const uint16_t *>(canvas.getBuffer());std::copy(p,p+n,clean.begin());
        UiCatalog::celebration(canvas,colors,flip,15500,"NEW BEST",effect,ms);
        std::printf("%s%s-%s-%04u settled=%d\n",useCustom?"custom":"theme0",flip?"-flip":"",Appearance::label(effect),unsigned(ms),
          int(std::equal(clean.begin(),clean.end(),p)));
      }
    }
    return 0;
  }
  if(celebrations) {
    const Palette::Colors colors=useCustom?custom.colors():Themes::builtIn(Appearance::Theme::Classic);
    for(bool flip:{false,true})for(unsigned e=0;e<Appearance::DRAWN;++e) {
      const auto effect=Appearance::drawnEffect(e);
      for(uint32_t ms=0;ms<Appearance::duration(effect);ms+=50) {
        UiCatalog::celebration(canvas,colors,flip,15500,"NEW BEST",effect,ms);
        char name[64];std::snprintf(name,sizeof(name),"%s%s-%s-%04u",useCustom?"custom":"theme0",flip?"-flip":"",Appearance::label(effect),unsigned(ms));
        if(!write(name))return 1;
      }
    }
    return 0;
  }
  const unsigned themes=useCustom?1:Appearance::THEMES;
  for(unsigned t=0;t<themes;++t)for(bool flip:{false,true})for(unsigned s=0;s<UiCatalog::COUNT;++s) {
    UiCatalog::Look look;
    if(useCustom){look.colors=custom.colors();look.theme=Themes::slotTheme(0);look.name=custom.name;}
    else {look.colors=Themes::builtIn(Appearance::Theme(t));look.theme=Appearance::Theme(t);look.name=Appearance::label(look.theme);}
    UiCatalog::draw(canvas,s,look,flip);
    const std::string name=(useCustom?std::string("custom"):"theme"+std::to_string(t))+(flip?"-flip-":"-")+UiCatalog::SCREENS[s].id;
    if(!write(name))return 1;
  }
}
