#include "appearance_store.h"
#include "best_effect_ui.h"
#include <cassert>
#include <iostream>
#include <limits>

int main(){
  using namespace Appearance;
  static_assert(THEMES==10 && sizeof(Config::Image)==16);
  static_assert(unsigned(Theme::Classic)==0 && unsigned(Theme::Mint)==1 && unsigned(Theme::Amber)==2 && unsigned(Theme::Violet)==3);
  const char *names[]={"Pegasus Cyan","Wolf Mint","Wizard Gold","Meteor Purple","Golem Green","Phoenix Red","Cobalt Blue","Shark Pink","Valkyrie White","Tyranno Orange"};
  const uint16_t originalColors[]={0x07ff,0x8ff9,0xff10,0xc5bf};
  auto linear=[](double s){return s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);};
  for(unsigned theme=0;theme<THEMES;++theme) {
    assert(!std::strcmp(label(Theme(theme)),names[theme]));
    const uint16_t color=accent(Theme(theme));
    if(theme<4)assert(color==originalColors[theme]);
    for(unsigned prior=0;prior<theme;++prior)assert(color!=accent(Theme(prior)));
    const double luminance=.2126*linear((color>>11)/31.0)+.7152*linear(((color>>5)&63)/63.0)+.0722*linear((color&31)/31.0);
    assert((luminance+.05)/.05>=4.5); // Readable accent text on the black UI.
  }
  assert(accent(Theme(255))==originalColors[0] && !std::strcmp(label(Theme(255)),names[0]));
  // Golden records from the original four-ID, 16-byte format retain their bytes.
  const uint8_t legacy[][16]={
    {0x4c,0x4c,0x41,0x01,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0xc6,0xd1,0xbb},
    {0x4c,0x4c,0x41,0x01,0x07,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x21,0x05,0xfe,0x0b},
    {0x4c,0x4c,0x41,0x01,0x07,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x32,0x44,0x2a,0x5c},
    {0x4c,0x4c,0x41,0x01,0x07,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x43,0x83,0x56,0xac}
  };
  for(unsigned theme=0;theme<4;++theme) {
    Config old;assert(old.decode(legacy[theme],16) && unsigned(old.theme)==theme && old.revision==7 && old.sound==Sound::Off);
    Config::Image image;old.encode(image);assert(!std::memcmp(image.data(),legacy[theme],16));
  }
  Preferences::data.clear();Preferences::zeroLengthKeys.clear();
  Config config;AppearanceStore<Config> settings;assert(settings.begin(config,"a","cfg0","cfg1"));
  assert(config.theme==Theme::Classic && config.effect==Effect::Random && config.sound==Sound::Off);
  // Settings order Random, Orbit..Lightning, Off; Random never repeats its previous pick.
  {Effect e=Effect::Random;for(unsigned i=0;i<EFFECTS;++i)e=nextChoice(e);assert(e==Effect::Random && nextChoice(Effect::Random)==Effect::Orbit && nextChoice(Effect::Off)==Effect::Random);
   for(uint32_t bits=0;bits<200;++bits)for(unsigned prev=0;prev<EFFECTS;++prev){const Effect p=pick(bits*2654435761u,Effect(prev));assert(drawn(unsigned(p)) && p!=Effect(prev));}}
  for(unsigned theme=0;theme<THEMES;++theme)for(unsigned effect=0;effect<EFFECTS;++effect){
    assert(config.select(Theme(theme),Effect(effect)) && settings.save(config));
    Config restored;AppearanceStore<Config> reader;assert(reader.begin(restored,"a","cfg0","cfg1"));
    assert(restored.theme==config.theme && restored.effect==config.effect);
  }
  auto draft=config;draft.select(Theme::Mint,Effect::Orbit);Preferences::failWrite=true;
  assert(!settings.save(draft) && config.theme==Theme::TyrannoOrange);
  Preferences::failWrite=false;assert(settings.save(draft));
  assert(!draft.select(Theme(255),Effect::Off));
  // Checksummed invalid IDs are rejected without changing the current config.
  {Config kept;assert(kept.select(Theme::TyrannoOrange,Effect::Lightning,Sound::High));
   Config::Image good;kept.encode(good);
   for(unsigned invalid:{ALL_THEMES,255u}) {
     assert(!kept.select(Theme(invalid),Effect::Off));
     auto bad=good;bad[8]=invalid;put32(bad.data()+12,hash(bad.data(),12));
     assert(!kept.decode(bad.data(),bad.size()));Config::Image after;kept.encode(after);assert(after==good);
   }}
  Bests b;AppearanceStore<Bests> store;assert(store.begin(b,"b","best0","best1"));
  assert(!b.observe(0,0,false,9000) && !b.observe(0,0,true,0) && !b.observe(0,0,true,std::numeric_limits<float>::quiet_NaN()));
  for(unsigned sensor=0;sensor<2;++sensor)for(unsigned method=0;method<2;++method){
    assert(!b.observe(sensor,method,true,6000));assert(!b.observe(sensor,method,true,6000));
    assert(!b.observe(sensor,method,true,5999));assert(b.observe(sensor,method,true,6000.5f));
  }
  assert(b.revision==8 && store.pending(b));assert(store.save(b));
  Bests reboot;AppearanceStore<Bests> afterBoot;assert(afterBoot.begin(reboot,"b","best0","best1"));
  assert(!reboot.observe(0,0,true,6000.5f) && reboot.observe(0,0,true,6100));
  Preferences::shortRead=true;assert(!afterBoot.save(reboot) && afterBoot.pending(reboot));Preferences::shortRead=false;
  assert(afterBoot.save(reboot));
  // Corrupt newest slot recovers verified older generation; all-corrupt/typed
  // keys are retained and disable saves/celebration rather than reseed a PB.
  Preferences::data["bbest0"][0]^=1;
  Bests recovered;AppearanceStore<Bests> older;assert(older.begin(recovered,"b","best0","best1") && recovered.rpm[0]==6000.5f);
  Preferences::zeroLengthKeys={"bbest0","bbest1"};const auto retained=Preferences::data;
  Bests unknown;AppearanceStore<Bests> invalid;assert(!invalid.begin(unknown,"b","best0","best1") && !invalid.ready());
  assert(!invalid.save(unknown) && Preferences::data==retained);Preferences::zeroLengthKeys.clear();
  Celebration c;assert(!c.badge());c.queue(true,7,Effect::Orbit);c.captured(6);assert(!c.badge());
  c.captured(7);assert(c.badge() && c.effect()==Effect::Orbit && !c.moving(10));c.presented(0xfffffff0u);
  assert(c.moving(40) && c.elapsed(40)==56);c.presented(40);assert(c.elapsed(40)==56);
  assert(c.moving(1580) && !c.moving(1600) && c.badge());
  Celebration off;off.queue(true,9,Effect::Off);off.captured(9);off.presented(0);assert(off.badge() && !off.moving(0));
  c.clear();c.captured(7);c.presented(20);assert(!c.badge());c.queue(false,8,Effect::Orbit);c.captured(8);assert(!c.badge());
  // Canvas pixels are stored as LGFX sprites keep them: RGB565 big-endian.
  auto swap=[](uint16_t v){return uint16_t(v<<8|v>>8);};
  std::array<uint16_t,135*240> base{};
  auto ui=[&](int x,int y,uint16_t c){base[y*135+x]=swap(c);};
  // Label, RPM glyphs with holes, replay wireframe and faces, lower labels.
  for(int y=2;y<10;++y)for(int x=44;x<91;++x)if((x+y)%3)ui(x,y,0x07ff);
  for(int y=17;y<44;++y)for(int x=10;x<125;++x)if((x+y)%5)ui(x,y,0x07ff);
  for(int i=0;i<90;++i){ui(20+i,60+i/3,0x18c3);ui(110-i,60+i/3,0x18c3);}
  for(int y=110;y<150;++y)for(int x=50;x<85;++x)ui(x,y,0x1947);
  for(int y=164;y<172;++y)for(int x=40;x<95;++x)if((x*y)%4)ui(x,y,0x7bef);
  for(int x=26;x<109;++x)ui(x,178,0x39e7);
  std::array<bool,135*240> halo{};
  for(int y=0;y<240;++y)for(int x=0;x<135;++x)if(base[y*135+x])
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)if(x+dx>=0 && x+dx<135 && y+dy>=0 && y+dy<240)halo[(y+dy)*135+x+dx]=true;
  // Sound level: byte 10, reserved as zero before, so older records read as Off.
  {Config c;assert(c.sound==Sound::Off && c.select(Theme::Amber,Effect::Sparkles,Sound::High) && c.revision==1);
   assert(c.select(Theme::Amber,Effect::Sparkles,Sound::High) && c.revision==1 && !c.select(Theme::Amber,Effect::Sparkles,Sound(3)));
   Config::Image image;c.encode(image);assert(image[10]==2);Config back;assert(back.decode(image.data(),16) && back.sound==Sound::High && back.effect==Effect::Sparkles);
   image[10]=0;Appearance::put32(image.data()+12,Appearance::hash(image.data(),12));assert(back.decode(image.data(),16) && back.sound==Sound::Off);
   image[10]=3;Appearance::put32(image.data()+12,Appearance::hash(image.data(),12));assert(!back.decode(image.data(),16));
   // The sound cue fires once, when the effect starts.
   Celebration cue;cue.queue(true,3,Effect::Orbit);assert(!cue.takeCue());cue.captured(3);assert(!cue.takeCue());
   cue.presented(100);assert(cue.takeCue() && !cue.takeCue());cue.presented(200);assert(!cue.takeCue());}
  BestEffectUI::Renderer renderer;unsigned frames=0;
  for(unsigned theme=0;theme<THEMES;++theme)for(unsigned mode=0;mode<EFFECTS;++mode) {
    bool drew=false,lower=false,glinted=false;uint32_t maxPlots=0;
    for(unsigned ms=0;ms<=2000;ms+=10) {
      auto out=base;renderer.draw({out.data(),135,240},Effect(mode),ms,accent(Theme(theme)));
      auto again=base;renderer.draw({again.data(),135,240},Effect(mode),ms,accent(Theme(theme)));
      assert(out==again);maxPlots=std::max(maxPlots,renderer.plots());
      for(int i=0;i<135*240;++i)if(out[i]!=base[i]) {
        const int y=i/135;drew=true;lower|=y>80;
        if(base[i]) {
          // Only the glint touches UI pixels: header glyphs, brighter in every channel.
          const uint16_t was=swap(base[i]),now=swap(out[i]);
          assert(y<=46 && (now>>11)>=(was>>11) && ((now>>5)&63)>=((was>>5)&63) && (now&31)>=(was&31));glinted=true;
        } else assert(!halo[i]);
      }
      if(ms>=duration(Effect(mode)))assert(out==base);
      ++frames;
    }
    assert(drew==drawn(mode) && glinted==drew);
    // Full-screen effects reach behind the replay; the pixel-write bound
    // tracks the per-frame cost measured on the device.
    if(Effect(mode)==Effect::Sparkles || Effect(mode)==Effect::Shockwave || Effect(mode)==Effect::Lightning)assert(lower);
    assert(maxPlots<=12000);
  }
  // A custom theme's background: the same pixels change as on black, since
  // ink is anything not of the background color; untouched pixels keep it.
  {
    const uint16_t bg=0x0841;std::array<uint16_t,135*240> themed=base;
    for(auto &p:themed)if(!p)p=swap(bg);
    for(unsigned mode=0;mode<DRAWN;++mode)for(unsigned ms:{40u,300u,700u,1200u}) {
      auto black=base;renderer.draw({black.data(),135,240},Effect(mode),ms,accent(Theme::Mint));
      auto out=themed;renderer.draw({out.data(),135,240},Effect(mode),ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
  }
  // Custom theme IDs follow the built-ins in the same record, which firmware
  // without custom themes rejects (falling back to its other slot or defaults).
  {Config c;for(unsigned theme=THEMES;theme<ALL_THEMES;++theme) {
     assert(custom(Theme(theme)) && c.select(Theme(theme),Effect::Orbit));
     Config::Image image;c.encode(image);assert(image[8]==theme);Config back;assert(back.decode(image.data(),16) && back.theme==Theme(theme));
   }
   assert(!custom(Theme::TyrannoOrange) && !custom(Theme(ALL_THEMES)));}
  // Unsupported canvases are left alone.
  {auto out=base;renderer.draw({out.data(),240,135},Effect::Orbit,100,accent(Theme::Classic));assert(out==base);
   renderer.draw({nullptr,135,240},Effect::Orbit,100,accent(Theme::Classic));}
  std::cout<<"PASS: appearance defaults/cycles, verified alternating stores/retry/reboot/corruption, per-profile/method strict PB, presentation token/wrap, "<<frames<<" deterministic halo-clipped effect frames\n";
}
