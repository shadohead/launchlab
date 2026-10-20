#include "appearance_store.h"
#include "best_effect_ui.h"
#include <cassert>
#include <iostream>
#include <limits>

static std::array<uint16_t,135*240> base_{};
static uint16_t swap(uint16_t v){return uint16_t(v<<8|v>>8);}
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
  // Stored effect values never change: Slash was added after Random.
  static_assert(unsigned(Effect::Orbit)==0 && unsigned(Effect::Lightning)==4 && unsigned(Effect::Off)==5 &&
                unsigned(Effect::Random)==6 && unsigned(Effect::Slash)==7 && EFFECTS==8 && DRAWN==6);
  for(unsigned i=0;i<DRAWN;++i)assert(drawn(unsigned(drawnEffect(i))) && drawnIndex(drawnEffect(i))==i);
  assert(!drawn(5) && !drawn(6) && drawn(7) && !drawn(8) && !std::strcmp(label(Effect::Slash),"Slash") && duration(Effect::Slash)==1700);
  // Settings order Random, Orbit..Lightning, Slash, Off; Random never repeats its previous pick.
  {Effect e=Effect::Random;unsigned seen=0;for(unsigned i=0;i<EFFECTS;++i){seen|=1u<<unsigned(e);e=nextChoice(e);}
   assert(e==Effect::Random && seen==(1u<<EFFECTS)-1 && nextChoice(Effect::Random)==Effect::Orbit && nextChoice(Effect::Lightning)==Effect::Slash &&
          nextChoice(Effect::Slash)==Effect::Off && nextChoice(Effect::Off)==Effect::Random);
   unsigned picked=0;
   for(uint32_t bits=0;bits<200;++bits)for(unsigned prev=0;prev<EFFECTS;++prev){const Effect p=pick(bits*2654435761u,Effect(prev));assert(drawn(unsigned(p)) && p!=Effect(prev));picked|=1u<<unsigned(p);}
   assert(picked==((1u<<5)-1 | 1u<<7));}
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
  std::array<uint16_t,135*240> &base=base_;
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
    const bool slash=Effect(mode)==Effect::Slash;
    for(unsigned ms=0;ms<=2000;ms+=10) {
      auto out=base;renderer.draw({out.data(),135,240},Effect(mode),ms,accent(Theme(theme)));
      auto again=base;renderer.draw({again.data(),135,240},Effect(mode),ms,accent(Theme(theme)));
      assert(out==again);maxPlots=std::max(maxPlots,renderer.plots());
      for(int i=0;i<135*240;++i)if(out[i]!=base[i]) {
        const int y=i/135;drew=true;lower|=y>80;
        // Slash moves the readout's own pixels: only in its rows and the
        // few rows its halves slide into.
        if(slash && y>=13-6 && y<=46+6)continue;
        if(base[i]) {
          // Only the glint touches UI pixels: header glyphs, brighter in every channel.
          const uint16_t was=swap(base[i]),now=swap(out[i]);
          assert(!slash && y<=46 && (now>>11)>=(was>>11) && ((now>>5)&63)>=((was>>5)&63) && (now&31)>=(was&31));glinted=true;
        } else assert(!halo[i]);
      }
      if(ms>=duration(Effect(mode)))assert(out==base);
      ++frames;
    }
    assert(drew==drawn(mode) && glinted==(drew && !slash));
    // Full-screen effects reach behind the replay; the pixel-write bound
    // tracks the per-frame cost measured on the device.
    if(Effect(mode)==Effect::Sparkles || Effect(mode)==Effect::Shockwave || Effect(mode)==Effect::Lightning || slash)assert(lower);
    assert(maxPlots<=12000);
  }
  // A custom theme's background: the same pixels change as on black, since
  // ink is anything not of the background color; untouched pixels keep it.
  {
    const uint16_t bg=0x0841;std::array<uint16_t,135*240> themed=base;
    for(auto &p:themed)if(!p)p=swap(bg);
    for(unsigned mode=0;mode<DRAWN;++mode)for(unsigned ms:{40u,300u,700u,1200u}) {
      auto black=base;renderer.draw({black.data(),135,240},drawnEffect(mode),ms,accent(Theme::Mint));
      auto out=themed;renderer.draw({out.data(),135,240},drawnEffect(mode),ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
      for(int i=0;i<135*240;++i)assert((out[i]!=themed[i])==(black[i]!=base[i]));
    }
  }
  // Slash: the blade cuts the readout along a line rising 0.3 to the right
  // through its centre, the halves slide apart and back, and the readout is
  // exactly as drawn again well before the effect ends.
  {
    // Readout ink bounds in the fixture: x 10..124, y 17..43. An aperiodic
    // ink pattern, so a displaced half matches the original in one place only.
    auto base=::base_;
    for(int y=17;y<44;++y)for(int x=10;x<125;++x){uint32_t h=uint32_t(x*73856093u^y*19349663u);h^=h>>13;h*=0x5bd1e995u;h^=h>>15;base[y*135+x]=h%4?swap(0x07ff):0;}
    base[17*135+10]=base[43*135+124]=swap(0x07ff);
    const float cx=67,cy=30;auto cutY=[&](int x){return int(std::lround(cy-(x-cx)*.3f));};
    auto ink=[&](const std::array<uint16_t,135*240> &f,int x,int y){return f[y*135+x]!=0;};
    for(unsigned theme:{0u,3u,5u,8u}) {
      const uint16_t color=accent(Theme(theme));
      // Open: along the cut the readout is parted; elsewhere in the box the
      // halves carry their glyphs.
      auto open=base;renderer.draw({open.data(),135,240},Effect::Slash,600,color);
      unsigned onCut=0,parted=0;
      for(int x=20;x<=114;++x){const int y=cutY(x);if(ink(base,x,y)){++onCut;parted+=!ink(open,x,y);}}
      assert(onCut>40 && parted*10>=onCut*9);
      unsigned changed=0;for(int y=13;y<=46;++y)for(int x=0;x<135;++x)changed+=open[y*135+x]!=base[y*135+x];
      assert(changed>800);
      // The upper half is displaced up and to the right: its glyph rows reappear shifted.
      auto at=[&](int dx,int dy){unsigned same=0;for(int y=19;y<=24;++y)for(int x=30;x<=80;++x)same+=open[(y+dy)*135+x+dx]==base[y*135+x];return same;};
      unsigned best=0;int bx=0,by=0;
      for(int dy=-6;dy<=0;++dy)for(int dx=0;dx<=14;++dx){const unsigned v=at(dx,dy);if(v>best){best=v;bx=dx;by=dy;}}
      assert(bx>=3 && bx<=5 && by<=-4 && by>=-6 && best>=280);
      // Before the split the cut glows on the glyphs left of the blade.
      auto cut=base;renderer.draw({cut.data(),135,240},Effect::Slash,80,color);
      unsigned hot=0;for(int x=10;x<=40;++x){const int y=cutY(x);if(ink(base,x,y) && cut[y*135+x]!=base[y*135+x])++hot;}
      assert(hot>5);
      // Settled: the readout rows are exactly the original from 1.5 s on.
      for(unsigned ms=1500;ms<1700;ms+=10){auto out=base;renderer.draw({out.data(),135,240},Effect::Slash,ms,color);assert(out==base);}
      // Closed again at 1.35 s: no part of the readout is displaced.
      auto closed=base;renderer.draw({closed.data(),135,240},Effect::Slash,1360,color);
      for(int y=13;y<=46;++y)for(int x=0;x<135;++x)if(base[y*135+x])assert(closed[y*135+x]!=0);
    }
    // On a custom background, parted pixels take the theme's background.
    const uint16_t bg=0x0841;std::array<uint16_t,135*240> themed=base;for(auto &p:themed)if(!p)p=swap(bg);
    auto out=themed;renderer.draw({out.data(),135,240},Effect::Slash,600,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);
    unsigned gap=0;for(int x=20;x<=114;++x){const int y=cutY(x);if(themed[y*135+x]!=swap(bg) && out[y*135+x]==swap(bg))++gap;}
    assert(gap>40);
    for(unsigned ms=1500;ms<1700;ms+=50){auto o=themed;renderer.draw({o.data(),135,240},Effect::Slash,ms,accent(Theme::Mint),BestEffectUI::Renderer::Anchor(),bg);assert(o==themed);}
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
