// Palettes and custom themes: built-ins identical to the pre-palette colors,
// contrast and name validation, slot/library/request records, the stored
// record's fallback and budget, Settings cycling, and the vectors the web
// theme editor is tested against.
#include "../LaunchLabMini/theme_store.h"
#include "../LaunchLabMini/controls.h"
#include "theme_vectors.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

using Appearance::Theme;
static Palette::Custom valid(const char *name="Dragoon Storm",uint16_t accent=0x5c7f) {
  return ThemeVectors::theme(name,{0x0000,0xffff,0x7bef,accent,accent,0xfda0,0x07e0,0xf800});
}

int main() {
  // The integer sRGB tables are the WCAG transfer, rounded to millionths.
  auto linear=[](double s){return s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);};
  for(unsigned i=0;i<32;++i)assert(std::llround(linear(i/31.0)*1e6)==Palette::LINEAR5[i]);
  for(unsigned i=0;i<64;++i)assert(std::llround(linear(i/63.0)*1e6)==Palette::LINEAR6[i]);
  assert(Palette::luminance(0xffff)==1000000 && Palette::luminance(0)==0 && Palette::contrast100(0xffff,0)==2100);
  assert(Palette::contrast100(0x07ff,0x0841)==Palette::contrast100(0x0841,0x07ff));

  // Every built-in is exactly the colors the screens drew before palettes:
  // black, white, LGFX dark grey/orange/green/red, the fixed greys, its
  // accent, and a white RPM readout only for Pegasus Cyan.
  for(unsigned t=0;t<Appearance::THEMES;++t) {
    const auto c=Themes::builtIn(Theme(t));const uint16_t accent=Appearance::accent(Theme(t));
    Palette::Colors expected;
    expected.background=0x0000;expected.text=0xffff;expected.muted=0x7bef;expected.faint=0x39e7;expected.accent=accent;
    expected.number=t==0?0xffff:accent;expected.warning=0xfda0;expected.ready=0x07e0;expected.alert=0xf800;
    expected.sceneRear=0x18c3;expected.scenePlane=0x2965;
    assert(c==expected);
    // Each built-in is also a valid custom theme, so the editor can start from it.
    Palette::Custom copy=ThemeVectors::theme(Appearance::label(Theme(t)),{c.background,c.text,c.muted,c.accent,c.number,c.warning,c.ready,c.alert});
    assert(copy.valid() && copy.colors()==c);
  }
  // Derived shades sit between the background and muted, channel by channel,
  // on any background.
  for(uint16_t bg:{0x0000,0x0841,0x2104,0x0019,0x3800})for(uint16_t muted:{0x7bef,0xffff,0x8c71,0x07e0}) {
    const auto c=Palette::derive(bg,0xffff,muted,0x07ff,0x07ff,0xfda0,0x07e0,0xf800);
    for(uint16_t shade:{c.faint,c.sceneRear,c.scenePlane})for(unsigned shift:{11u,5u,0u}) {
      const unsigned mask=shift==5?63:31,v=(shade>>shift)&mask,a=(bg>>shift)&mask,b=(muted>>shift)&mask;
      assert(v>=std::min(a,b) && v<=std::max(a,b));
    }
  }

  // Validation: names, the dark background and each role's contrast.
  const unsigned none=Palette::ROLES;
  struct Verdict {Palette::Problem problem;unsigned role;};
  const Verdict verdicts[]={{Palette::Problem::None,none},{Palette::Problem::None,none},{Palette::Problem::Contrast,Palette::Muted},
    {Palette::Problem::None,none},{Palette::Problem::BackgroundTooLight,none},{Palette::Problem::Contrast,Palette::Text},
    {Palette::Problem::Contrast,Palette::Alert},{Palette::Problem::Name,none},{Palette::Problem::Name,none},
    {Palette::Problem::None,none},{Palette::Problem::Name,none}};
  const auto vectors=ThemeVectors::themes();
  assert(vectors.size()==sizeof(verdicts)/sizeof(verdicts[0]));
  for(size_t i=0;i<vectors.size();++i){unsigned role=none;assert(vectors[i].check(&role)==verdicts[i].problem && role==verdicts[i].role);}
  {
    char name[Palette::Custom::NAME_BYTES]={};
    std::memcpy(name,"Ember ",6);assert(!Palette::Custom::validName(name,sizeof(name)));
    std::memcpy(name,"Ember\0x",7);assert(!Palette::Custom::validName(name,sizeof(name)));
    std::memset(name,'a',sizeof(name));assert(!Palette::Custom::validName(name,sizeof(name)));
    name[14]=0;assert(Palette::Custom::validName(name,sizeof(name)));
    name[3]=char(0x7f);assert(!Palette::Custom::validName(name,sizeof(name)));
  }
  // Slot records round trip; only flag 1 is a theme.
  {
    uint8_t slot[Themes::SLOT_BYTES];const auto t=vectors[1];t.encode(slot);
    Palette::Custom back;assert(back.decode(slot) && !std::memcmp(&back,&t,sizeof(t)));
    for(uint8_t flag:{0,2,255}){slot[0]=flag;assert(!back.decode(slot));}
  }

  // The library: an empty or invalid slot shows the default, by name and colors.
  Themes::Library library;
  assert(library.empty() && !library.validMask() && library.revision==0);
  for(unsigned t=Appearance::THEMES;t<Appearance::ALL_THEMES;++t) {
    assert(!library.available(Theme(t)) && library.effective(Theme(t))==Themes::FALLBACK);
    assert(library.colors(Theme(t))==Themes::builtIn(Themes::FALLBACK) && !std::strcmp(library.label(Theme(t)),"Pegasus Cyan"));
  }
  assert(!library.put(4,valid()) && !library.put(0,vectors[2]) && library.revision==0);
  assert(library.put(1,valid()) && library.revision==1 && library.put(1,valid()) && library.revision==1);
  assert(library.put(3,valid("Ember",0xfb20)) && library.validMask()==0b1010 && library.revision==2);
  assert(library.colors(Themes::slotTheme(1))==valid().colors() && !std::strcmp(library.label(Themes::slotTheme(3)),"Ember"));
  assert(library.colors(Theme::Amber)==Themes::builtIn(Theme::Amber) && !std::strcmp(library.label(Theme::Amber),"Wizard Gold"));
  // Settings order: the built-ins, then each valid custom theme.
  {
    Theme t=Theme::Classic;std::vector<unsigned> order;
    for(unsigned i=0;i<12;++i){order.push_back(unsigned(t));t=library.next(t);}
    assert((order==std::vector<unsigned>{0,1,2,3,4,5,6,7,8,9,11,13}) && t==Theme::Classic);
    assert(library.next(Themes::slotTheme(0))==Theme::Mint); // a missing selection cycles from the default
    Controls::State state;state.page=Controls::Page::Edit;state.setting=unsigned(Controls::Setting::Theme);
    state.customThemes=library.validMask();state.draft=0;
    for(unsigned i=0;i<12;++i){assert(state.draft==order[i]);assert(state.tapB()==Controls::Effect::Preview);}
    assert(state.draft==0);
    state.customThemes=0;state.draft=9;state.tapB();assert(state.draft==0);
  }
  // The image round trips; any corruption loses the whole image.
  Themes::Library::Image image;library.encode(image);
  {
    Themes::Library back;assert(back.decode(image.data(),image.size()) && back==library && back.validMask()==0b1010);
    for(size_t i=0;i<image.size();++i){auto bad=image;bad[i]^=1;Themes::Library rejected;assert(!rejected.decode(bad.data(),bad.size()) && rejected.empty());}
    Themes::Library rejected;assert(!rejected.decode(image.data(),image.size()-1));
  }
  // A slot this firmware cannot accept (another flag, rules it fails) shows
  // the default but keeps its bytes for the firmware that wrote it.
  {
    auto odd=image;odd[8+2*Themes::SLOT_BYTES]=2;odd[8+2*Themes::SLOT_BYTES+5]=0x41;
    uint8_t failing[Themes::SLOT_BYTES];vectors[5].encode(failing);std::memcpy(odd.data()+8,failing,sizeof(failing));
    Appearance::put32(odd.data()+odd.size()-4,Appearance::hash(odd.data(),odd.size()-4));
    Themes::Library kept;assert(kept.decode(odd.data(),odd.size()));
    assert(kept.state(0)==Themes::SlotState::Invalid && kept.state(2)==Themes::SlotState::Invalid && kept.validMask()==0b1010);
    assert(kept.colors(Themes::slotTheme(0))==Themes::builtIn(Themes::FALLBACK) && !kept.available(Themes::slotTheme(2)));
    Themes::Library::Image again;kept.encode(again);assert(again==odd);
    assert(kept.remove(2) && kept.state(2)==Themes::SlotState::Empty);
  }
  assert(library.remove(1) && library.validMask()==0b1000 && library.revision==3 && library.remove(0) && library.revision==3);

  // Requests: each op, and every malformed shape is refused.
  {
    using Op=Themes::Request::Op;
    for(Op op:{Op::Put,Op::Delete,Op::Select,Op::Clear}) {
      Themes::Request q;q.op=op;q.argument=op==Op::Clear?0:op==Op::Select?11:2;if(op==Op::Put)q.theme=valid();
      uint8_t b[Themes::Request::BYTES];q.encode(b);Themes::Request back;
      auto copy=[&]{std::array<uint8_t,Themes::Request::BYTES> a;std::memcpy(a.data(),b,sizeof(b));return a;};
      assert(back.decode(b,sizeof(b)) && back.op==op && back.argument==q.argument);
      if(op==Op::Put)assert(!std::memcmp(&back.theme,&q.theme,sizeof(q.theme)));
      assert(!back.decode(b,sizeof(b)-1));
      for(size_t i=0;i<sizeof(b);++i){auto bad=copy();bad[i]^=4;assert(!back.decode(bad.data(),bad.size()));}
      auto reseal=[&](uint8_t *p){Appearance::put32(p+Themes::Request::BYTES-4,Appearance::hash(p,Themes::Request::BYTES-4));};
      {auto bad=copy();bad[6]=1;reseal(bad.data());assert(!back.decode(bad.data(),bad.size()));}
      {auto bad=copy();bad[4]=0;reseal(bad.data());assert(!back.decode(bad.data(),bad.size()));}
      {auto bad=copy();bad[4]=5;reseal(bad.data());assert(!back.decode(bad.data(),bad.size()));}
      if(op!=Op::Put){auto bad=copy();bad[20]=1;reseal(bad.data());assert(!back.decode(bad.data(),bad.size()));}
      else {auto bad=copy();bad[8]=0;reseal(bad.data());assert(!back.decode(bad.data(),bad.size()));}
      if(op==Op::Clear){auto bad=copy();bad[5]=1;reseal(bad.data());assert(!back.decode(bad.data(),bad.size()));}
    }
  }

  // The stored record: missing is empty, saved records reload, an unreadable
  // record shows the default and is kept until cleared on purpose.
  {
    Preferences::data.clear();
    Themes::Library themes;ThemeStore store;assert(store.begin(themes)==ThemeStore::Status::Empty && themes.empty());
    assert(store.save(themes) && Preferences::data.empty()); // nothing to write
    assert(themes.put(2,valid()) && store.pending(themes) && store.save(themes) && !store.pending(themes));
    Themes::Library reloaded;ThemeStore reader;assert(reader.begin(reloaded)==ThemeStore::Status::Loaded && reloaded==themes);
    // A failed or short write leaves the record and the RAM copy's pending state.
    auto next=themes;assert(next.put(0,valid("Ember",0xfb20)));
    Preferences::failWrite=true;assert(!store.save(next) && store.pending(next));Preferences::failWrite=false;
    Preferences::shortRead=true;assert(!store.save(next));Preferences::shortRead=false;
    assert(store.save(next));
    // Corrupt record: the default shows, uploads are refused, the bytes stay.
    Preferences::data["ll-appearancethemes"][20]^=0x10;const auto retained=Preferences::data;
    Themes::Library corrupt;ThemeStore damaged;assert(damaged.begin(corrupt)==ThemeStore::Status::Unreadable && corrupt.empty());
    assert(corrupt.colors(Themes::slotTheme(2))==Themes::builtIn(Themes::FALLBACK));
    auto attempt=corrupt;assert(attempt.put(1,valid()) && !damaged.save(attempt) && Preferences::data==retained);
    // A wrong-length record is unreadable too.
    Preferences::data["ll-appearancethemes"].resize(16);
    {Themes::Library t;ThemeStore s;assert(s.begin(t)==ThemeStore::Status::Unreadable);}
    assert(damaged.clear() && !Preferences::data.count("ll-appearancethemes") && damaged.status()==ThemeStore::Status::Empty);
    assert(damaged.save(attempt));
    Themes::Library fresh;ThemeStore again;assert(again.begin(fresh)==ThemeStore::Status::Loaded && fresh.validMask()==0b10);
  }
  // An upload keeps room for a history rewrite: the record (8 entries) plus
  // a history image (87). The device had 158 entries free beside 120 launches.
  static_assert(NvsBudget::blobEntries(Themes::Library::IMAGE_BYTES)==8 && ThemeStore::NEEDED_ENTRIES==95);
  assert(!ThemeStore::fits(94) && ThemeStore::fits(95) && ThemeStore::fits(158));

  // The web editor's vectors are the firmware's own results.
  {
    std::ifstream file("firmware/tests/fixtures/theme_vectors.json");std::stringstream text;text<<file.rdbuf();
    if(text.str()!=ThemeVectors::json()) {
      std::cerr<<"firmware/tests/fixtures/theme_vectors.json is stale: regenerate it with theme_fixture.cpp\n";return 1;
    }
  }
  std::cout<<"PASS: built-in palettes identical to the pre-palette colors, integer WCAG contrast, name/background/role validation, "
             "slot/library/request records, default fallback for empty, invalid and unreadable themes, budget, Settings order, web vectors\n";
}
