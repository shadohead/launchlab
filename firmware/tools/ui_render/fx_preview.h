#pragma once
#include <cstring>
#include <string>
#include <vector>
#include "ui_catalog.h"
#include "fx_codes.h"
#include "fx_validate.h"

// Loaded effects in the web preview (ui_wasm.cpp) and its native twin
// (ui_render --fx): one effect compiled from source or opened from a code,
// checked by the validator on the real recaps a celebration plays over,
// and drawn on the recap exactly as the device draws it
// (docs/FIRMWARE_LOADABLE_EFFECTS.md, "Transport and the web").
namespace FxPreview {

// The recaps the validator checks an effect over, drawn by the firmware's
// own code once the replay is complete (all of its ink on screen): NEW BEST,
// a milestone and an unlabelled pull, both flips, the default theme, the
// lightest built-in accent and a light custom theme.
struct Recap {const char *name;int theme;bool light;bool flip;float rpm;const char *badge;};
constexpr Recap RECAPS[]={
  {"best",0,false,false,15500,"NEW BEST"},
  {"best-flip",0,false,true,15500,"NEW BEST"},
  {"milestone",0,false,false,12340,"MILESTONE 12K"},
  {"plain-flip",0,false,true,8200,nullptr},
  {"white-best",int(Appearance::Theme::ValkyrieWhite),false,false,23400,"NEW BEST"},
  {"light-best",-1,true,false,15500,"NEW BEST"},
};
// Paper (tests/fixtures/theme_vectors.json): white background, every role readable.
inline Palette::Colors lightTheme() {
  Palette::Custom c;const uint16_t roles[Palette::ROLES]={0xffff,0x0000,0x738e,0x001f,0x0000,0x9a00,0x0400,0xc000};
  for(unsigned r=0;r<Palette::ROLES;++r)c.role[r]=roles[r];
  return c.colors();
}
inline Palette::Colors colors(const Recap &r){return r.light?lightTheme():Themes::builtIn(Appearance::Theme(r.theme));}
// The replay is complete well before this: the recap with all of its ink.
constexpr uint32_t SETTLED_MS=60000;
inline std::vector<FxValidate::Scene> scenes(lgfx::LGFX_Sprite &g) {
  std::vector<FxValidate::Scene> out;
  for(const auto &r:RECAPS) {
    const auto c=colors(r);
    UiCatalog::celebration(g,c,r.flip,r.rpm,r.badge,Appearance::Effect::Off,SETTLED_MS);
    FxValidate::Scene s;s.name=r.name;s.background=c.background;
    const auto *p=static_cast<const uint16_t *>(g.getBuffer());
    s.pixels.assign(p,p+size_t(UiCatalog::WIDTH)*UiCatalog::HEIGHT);
    out.push_back(std::move(s));
  }
  return out;
}

// One effect: its source, packed source and share code, its file, and the
// program the preview draws.
class Effect {
public:
  Effect()=default;
  Effect(const Effect &)=delete;Effect &operator=(const Effect &)=delete;  // the program points into file_
  // Compiles source text; false with error() and line() when it does not compile.
  bool compile(const std::string &text) {
    clear();
    const auto made=FxCodes::make(text);
    if(!made.ok){error_=made.error;line_=made.line;return false;}
    const auto c=FxCompiler::compile(text);
    const auto e=LoadableFx::load(c.bytes.data(),c.bytes.size(),program_);
    if(e!=LoadableFx::Error::None){error_=std::string("refused: ")+LoadableFx::label(e);return false;}
    source_=text;packed_=made.packed;code_=made.code;file_=c.bytes;
    // The program points into file_: reload it from its own copy.
    LoadableFx::load(file_.data(),file_.size(),program_);
    ready_=true;return true;
  }
  // Opens a pasted code, an .llfx file's text or a link holding one.
  bool open(const std::string &pasted){return opened(FxCodes::open(pasted));}
  // Opens a slot's packed source, read back from a device (X source).
  bool openPacked(const std::string &packed){return opened(FxCodes::unpacked(packed));}
  // The validator over every recap; false with problem().
  bool check(lgfx::LGFX_Sprite &g) {
    if(!ready_){report_=FxValidate::Report();report_.problem="no effect";return false;}
    report_=FxValidate::check(file_,scenes(g));
    return report_.ok;
  }
  // A frame of the effect on the recap, as the device draws a loaded effect.
  bool celebration(lgfx::LGFX_Sprite &g,const Palette::Colors &c,bool flip,float rpm,const char *badge,uint32_t elapsed) {
    if(!ready_)return false;
    UiCatalog::celebration(g,c,flip,rpm,badge,Appearance::Effect::Off,elapsed);
    static BestEffectUI::Renderer renderer;static LoadableFx::Engine engine(renderer);
    engine.draw({static_cast<uint16_t *>(g.getBuffer()),UiCatalog::WIDTH,UiCatalog::HEIGHT},program_,elapsed,c.accent,BestEffectUI::Renderer::Anchor(),c.background);
    return true;
  }
  bool ready()const{return ready_;}
  const std::string &error()const{return error_;}
  int line()const{return line_;}
  const std::string &source()const{return source_;}
  const std::string &packed()const{return packed_;}
  const std::string &code()const{return code_;}
  const std::vector<uint8_t> &file()const{return file_;}
  const LoadableFx::Program &program()const{return program_;}
  const FxValidate::Report &report()const{return report_;}

private:
  bool opened(const FxCodes::Opened &o) {
    if(!o.ok){clear();error_=o.error;return false;}
    return compile(o.source);
  }
  void clear(){ready_=false;error_.clear();line_=0;source_.clear();packed_.clear();code_.clear();file_.clear();program_=LoadableFx::Program();report_=FxValidate::Report();}
  bool ready_=false;std::string error_;int line_=0;
  std::string source_,packed_,code_;std::vector<uint8_t> file_;
  LoadableFx::Program program_;FxValidate::Report report_;
};

// The effect's own sound as the device renders it (LoadableFx::Sound, from
// the file): 16 kHz mono 16-bit samples; empty when it has none (the
// device then plays its fallback's).
inline std::vector<int16_t> sound(const Effect &e) {
  if(!e.ready() || !e.program().hasSound)return {};
  static LoadableFx::Sound renderer;
  std::vector<int16_t> samples(e.program().soundSamples());
  renderer.begin(e.program());samples.resize(renderer.run(samples.data(),samples.size()));
  return samples;
}

}  // namespace FxPreview
