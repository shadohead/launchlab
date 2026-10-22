#include "../LaunchLabMini/device_backup.h"
#include "../LaunchLabMini/appearance_store.h"
#include "../LaunchLabMini/motion_store.h"
#include "../LaunchLabMini/practice_store.h"
#include "../LaunchLabMini/theme_store.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

static LaunchMotion::Trace trace(uint32_t number,float rpm) {
  LaunchMotion::Trace t;t.fused=true;t.quality=LaunchMotion::Quality::Valid;t.count=48;t.number=number;t.rpm=rpm;t.durationMs=400;t.gravity[2]=1;
  for(unsigned i=0;i<t.count;++i){t.points[i].ms=-500+int(i*1150/47);t.points[i].q[0]=16384;}
  t.startLevel={StickS3Level::State::Valid,6,.2f,0};t.endLevel={StickS3Level::State::Valid,15,0,-.5f};
  return t;
}
static DeviceBackup::Contents sample() {
  DeviceBackup::Contents c;
  for(uint32_t i=1;i<=40;++i)c.practice.accept(i,true,5000+i*37,i*20000);
  c.practice.newSession();c.practice.accept(41,true,9100,900000);
  c.look.select(Appearance::Theme::Amber,Appearance::Effect::Lightning);
  c.bests.observe(0,0,true,8800);c.bests.observe(1,1,true,7700);
  c.hasMotion=true;c.motion=trace(41,9100);
  c.settings={7,60,RpmEstimator::Mode::SingleTurn,StickS3SensorProfile::Mode::Tcrt,true};
  return c;
}
static bool same(const DeviceBackup::Contents &a,const DeviceBackup::Contents &b) {
  PracticeHistory::Image x,y;a.practice.encode(x);b.practice.encode(y);
  if(x!=y || a.look.theme!=b.look.theme || a.look.effect!=b.look.effect || a.look.revision!=b.look.revision)return false;
  for(unsigned i=0;i<4;++i)if(a.bests.rpm[i]!=b.bests.rpm[i])return false;
  if(a.hasMotion!=b.hasMotion || (a.hasMotion && (a.motion.number!=b.motion.number || a.motion.rpm!=b.motion.rpm || a.motion.count!=b.motion.count)))return false;
  const auto &s=a.settings,&t=b.settings;
  return s.sleepMinutes==t.sleepMinutes && s.brightness==t.brightness && s.rpmMethod==t.rpmMethod && s.sensorProfile==t.sensorProfile && s.flipped==t.flipped && s.mount==t.mount;
}
static Palette::Custom custom(const char *name,uint16_t accent) {
  Palette::Custom t;std::strncpy(t.name,name,Palette::Custom::MAX_NAME);
  const uint16_t roles[Palette::ROLES]={0x0841,0xffff,0x8c71,accent,accent,0xfda0,0x07e0,0xf800};
  std::memcpy(t.role,roles,sizeof(roles));assert(t.valid());return t;
}
// Sends text through the receiver the way the web updater does: lines, then ".".
static DeviceBackup::Receiver::Event transfer(DeviceBackup::Receiver &rx,const std::vector<std::string> &lines,uint32_t &now) {
  rx.start(now);auto last=DeviceBackup::Receiver::Event::None;
  for(const auto &line:lines)for(char c:line+"\r\n"){last=rx.feed(c,++now);if(last==DeviceBackup::Receiver::Event::Error)return last;}
  for(char c:std::string(".\n"))last=rx.feed(c,++now);
  return last;
}

int main() {
  using namespace DeviceBackup;
  const auto original=sample();
  Buffer buffer;const size_t n=encode(original,"0.11.0-sticks3-test",buffer);
  assert(n<=MAX_BYTES && get32(buffer.data()+8)==n);
  Contents decoded;assert(decode(buffer.data(),n,decoded) && same(original,decoded));
  assert(std::string(decoded.runtime)=="0.11.0-sticks3-test");
  // Without custom themes a backup is still format 1, byte for byte what
  // 0.13.9 wrote (and the web updater's fixture), so older firmware and an
  // older updater keep reading it.
  assert(!original.hasThemes && get32(buffer.data()+4)==LEGACY_FORMAT && n==3936 && checksum(buffer.data(),n)==0x3e6edb68u);
  assert(!decoded.hasThemes && decoded.themes.empty());

  // Custom themes make it format 2, which always carries their record: two
  // themes, one selected, round trip exactly.
  {
    auto themed=original;themed.hasThemes=true;
    assert(themed.themes.put(1,custom("Dragoon Storm",0x5c7f)) && themed.themes.put(3,custom("Ember",0xfb20)));
    assert(themed.look.select(Themes::slotTheme(3),Appearance::Effect::Orbit) && themed.format()==THEMES_FORMAT);
    Buffer encoded;const size_t length=encode(themed,"0.14.0-sticks3-test",encoded);
    assert(get32(encoded.data()+4)==THEMES_FORMAT && length==n+8+Themes::Library::IMAGE_BYTES);
    Contents restored;assert(decode(encoded.data(),length,restored) && same(themed,restored));
    assert(restored.hasThemes && restored.themes==themed.themes && restored.themes.state(1)==Themes::SlotState::Valid);
    Palette::Custom back;assert(restored.themes.get(3,back) && !std::strcmp(back.name,"Ember") && restored.look.theme==Themes::slotTheme(3));
    // Every corruption of a format 2 backup is rejected too.
    for(size_t i=0;i<length;i+=5){auto bad=encoded;bad[i]^=0x20;Contents ignored;assert(!decode(bad.data(),length,ignored));}
    // Format 1 cannot carry the themes section, format 3 needs milestones,
    // format 5 the effect set, and no other format is read.
    for(uint32_t format:{LEGACY_FORMAT,MILESTONES_FORMAT,4u,FORMAT,FORMAT+1}) {
      auto bad=encoded;put32(bad.data()+4,format);put32(bad.data()+length-4,checksum(bad.data(),length-4));
      Contents ignored;assert(!decode(bad.data(),length,ignored));
    }
    // A format 2 restore replaces the device's themes, even an unreadable record.
    Preferences::data.clear();Preferences::data["ll-appearancethemes"]={1,2,3};
    Themes::Library device;ThemeStore store;assert(store.begin(device)==ThemeStore::Status::Unreadable && device.empty());
    assert(!store.save(restored.themes) && Preferences::data["ll-appearancethemes"].size()==3);
    assert(store.save(restored.themes,true));
    Themes::Library reloaded;ThemeStore reader;assert(reader.begin(reloaded)==ThemeStore::Status::Loaded && reloaded==restored.themes);
  }
  // A custom selection with no themes left is format 2 with an empty record,
  // never a format 1 backup older firmware would half-read.
  {
    auto orphan=original;assert(orphan.look.select(Themes::slotTheme(0),Appearance::Effect::Off) && !orphan.hasThemes);
    Buffer encoded;const size_t length=encode(orphan,"x",encoded);assert(get32(encoded.data()+4)==THEMES_FORMAT);
    Contents restored;assert(decode(encoded.data(),length,restored) && restored.hasThemes && restored.themes.empty());
  }

  // The default milestones add nothing: format 1 stays byte for byte (above).
  // A changed mode or table 0.15 can hold makes format 3 (0.15's record), with
  // or without custom themes; a table it cannot hold makes format 4.
  static_assert(FORMAT==6 && EFFECT_SET_FORMAT==5 && POOLS_FORMAT==4 && MILESTONES_FORMAT==3,"0.15 reads formats 1-3, 0.16 formats 1-4, 0.17 formats 1-5");
  {
    using Milestones::Row;using Appearance::Effect;
    assert(original.milestones.isDefault() && !original.hasMilestones);
    auto modeOnly=original;assert(modeOnly.milestones.select(Milestones::Mode::Both));modeOnly.hasMilestones=true;
    auto table=original;const Row rows[]={{8000,Effect::Orbit},{12500,Effect::Lightning},{16000,Effect::Slash}};
    assert(table.milestones.set(Milestones::Mode::Milestones,rows,3)==Milestones::Problem::None);table.hasMilestones=true;
    auto both=table;both.hasThemes=true;assert(both.themes.put(0,custom("Dragoon Storm",0x5c7f)));
    for(const auto *c:{&modeOnly,&table,&both}) {
      assert(c->format()==MILESTONES_FORMAT);
      Buffer encoded;const size_t length=encode(*c,"0.15.0-sticks3-test",encoded);
      assert(get32(encoded.data()+4)==MILESTONES_FORMAT && length==n+8+Milestones::Config::V1_IMAGE_BYTES+(c->hasThemes?8+Themes::Library::IMAGE_BYTES:0));
      Contents restored;assert(decode(encoded.data(),length,restored) && same(*c,restored));
      assert(restored.hasMilestones && restored.milestones.same(c->milestones) && restored.milestones.revision==c->milestones.revision);
      assert(restored.hasThemes==c->hasThemes && (!c->hasThemes || restored.themes==c->themes));
      for(size_t i=0;i<length;i+=5){auto bad=encoded;bad[i]^=0x20;Contents ignored;assert(!decode(bad.data(),length,ignored));}
      // Older formats cannot carry the milestones section, formats 4 and 6
      // need the pooled section and format 5 the effect set.
      for(uint32_t format:{LEGACY_FORMAT,THEMES_FORMAT,POOLS_FORMAT,EFFECT_SET_FORMAT,FORMAT,FORMAT+1}) {
        auto bad=encoded;put32(bad.data()+4,format);put32(bad.data()+length-4,checksum(bad.data(),length-4));
        Contents ignored;assert(!decode(bad.data(),length,ignored));
      }
    }
    // A checksummed record with an invalid table is rejected whole.
    {
      Buffer encoded;const size_t length=encode(table,"x",encoded);
      const size_t body=length-4-Milestones::Config::V1_IMAGE_BYTES;
      assert(encoded[body-8]==MilestoneTable && encoded[body+8+1]==3);
      auto bad=encoded;bad[body+8+4+3]=bad[body+8+4];bad[body+8+4+4]=bad[body+8+4+1]; // row 1 rpm = row 0 rpm
      put32(bad.data()+body+Milestones::Config::V1_IMAGE_BYTES-4,Appearance::hash(bad.data()+body,Milestones::Config::V1_IMAGE_BYTES-4));
      put32(bad.data()+length-4,checksum(bad.data(),length-4));
      Contents ignored;assert(!decode(bad.data(),length,ignored));
    }
    // Format 4: effect pools, more than eight rows or an effect 0.15 lacks,
    // in the pooled section; round trip, damage and the wrong section refused.
    {
      auto pooled=original;pooled.hasMilestones=true;
      const Row pools[]={{9000,Effect::Sparkles},{12000,uint64_t(1)<<unsigned(Effect::Tidal)|uint64_t(1)<<unsigned(Effect::Bloom)},{15000,Effect::Slash}};
      assert(pooled.milestones.set(Milestones::Mode::Both,pools,3)==Milestones::Problem::None && !pooled.milestones.fitsV1());
      auto tidal=original;tidal.hasMilestones=true;const Row one[]={{9000,Effect::Tidal}};assert(tidal.milestones.set(Milestones::Mode::Milestones,one,1)==Milestones::Problem::None);
      auto ladder=original;ladder.hasMilestones=true;Row twelve[12];for(unsigned i=0;i<12;++i)twelve[i]={uint16_t(9000+500*i),Effect::Orbit};
      assert(ladder.milestones.set(Milestones::Mode::Milestones,twelve,12)==Milestones::Problem::None);
      for(const auto *c:{&pooled,&tidal,&ladder}) {
        assert(c->format()==POOLS_FORMAT);
        Buffer encoded;const size_t length=encode(*c,"0.16.0-sticks3-test",encoded);
        // 0.16's pooled record, byte for byte.
        assert(get32(encoded.data()+4)==POOLS_FORMAT && length==n+8+Milestones::Config::V2_IMAGE_BYTES);
        assert(encoded[length-4-Milestones::Config::V2_IMAGE_BYTES-8]==MilestonePools);
        {Milestones::Config::V2Image v2;c->milestones.encodeV2(v2);assert(std::equal(v2.begin(),v2.end(),encoded.begin()+length-4-v2.size()));}
        Contents restored;assert(decode(encoded.data(),length,restored) && restored.hasMilestones && restored.milestones.same(c->milestones));
        for(size_t i=0;i<length;i+=7){auto bad=encoded;bad[i]^=0x10;Contents ignored;assert(!decode(bad.data(),length,ignored));}
        for(uint32_t format:{LEGACY_FORMAT,THEMES_FORMAT,MILESTONES_FORMAT,EFFECT_SET_FORMAT,FORMAT,FORMAT+1}) {
          auto bad=encoded;put32(bad.data()+4,format);put32(bad.data()+length-4,checksum(bad.data(),length-4));
          Contents ignored;assert(!decode(bad.data(),length,ignored));
        }
        // The pooled record under the 0.15 section id is refused.
        auto bad=encoded;bad[length-4-Milestones::Config::V2_IMAGE_BYTES-8]=MilestoneTable;put32(bad.data()+length-4,checksum(bad.data(),length-4));
        Contents ignored;assert(!decode(bad.data(),length,ignored));
      }
    }
    // A restore writes the backup's milestones over whatever the device
    // holds, a damaged record included; a backup without them leaves them.
    Preferences::data.clear();Preferences::data["ll-appearancemile0"]={9,9,9};
    Milestones::Config device;AppearanceStore<Milestones::Config> store;
    assert(!store.begin(device,"ll-appearance","mile0","mile1",true) && store.damaged() && device.isDefault());
    auto incoming=table.milestones;incoming.revision=device.revision+1;assert(store.save(incoming));
    Milestones::Config reloaded;AppearanceStore<Milestones::Config> reader;
    assert(reader.begin(reloaded,"ll-appearance","mile0","mile1",true) && !reader.damaged() && reloaded.same(table.milestones));
  }

  // The default effect set adds nothing: formats 1-4 stay byte for byte
  // (above, and test_effect_set_compat against the frozen 0.15 and 0.16
  // sources). A set that differs from the default makes format 5, with or
  // without custom themes and milestones; its milestones are always the
  // pooled section, even a table 0.15's record could hold.
  {
    using EffectSet::flag;using Appearance::Effect;
    assert(original.effectSet.isDefault() && !original.hasEffectSet && !decoded.hasEffectSet && decoded.effectSet.isDefault());
    auto only=original;assert(only.effectSet.set(flag(Effect::Slash)|flag(Effect::Crimson)|flag(Effect::Ravage))==EffectSet::Problem::None);only.hasEffectSet=true;
    only.effectSet.revision=0x0a0b0c0d;
    auto all=only;all.hasThemes=true;assert(all.themes.put(2,custom("Dragoon Storm",0x5c7f)));
    const Milestones::Row rows[]={{8000,Effect::Bulwark},{16000,Effect::Slash}};
    assert(all.milestones.set(Milestones::Mode::Both,rows,2)==Milestones::Problem::None);all.hasMilestones=true;
    auto tabled=only;tabled.milestones=all.milestones;tabled.hasMilestones=true;
    for(const auto *c:{&only,&tabled,&all}) {
      assert(c->format()==EFFECT_SET_FORMAT);
      Buffer encoded;const size_t length=encode(*c,"0.17.0-sticks3-test",encoded);
      assert(get32(encoded.data()+4)==EFFECT_SET_FORMAT && length==n+8+EffectSet::Config::IMAGE_BYTES+(c->hasThemes?8+Themes::Library::IMAGE_BYTES:0)+
             (c->hasMilestones?8+Milestones::Config::V2_IMAGE_BYTES:0));
      // The effect set is the last section.
      assert(encoded[length-4-EffectSet::Config::IMAGE_BYTES-8]==OfferedEffects);
      Contents restored;assert(decode(encoded.data(),length,restored) && same(*c,restored));
      assert(restored.hasEffectSet && restored.effectSet.mask()==c->effectSet.mask() && restored.effectSet.revision==c->effectSet.revision);
      assert(restored.hasThemes==c->hasThemes && restored.hasMilestones==c->hasMilestones && (!c->hasMilestones || restored.milestones.same(c->milestones)));
      for(size_t i=0;i<length;i+=5){auto bad=encoded;bad[i]^=0x20;Contents ignored;assert(!decode(bad.data(),length,ignored));}
      if(c->hasMilestones)assert(encoded[length-4-EffectSet::Config::IMAGE_BYTES-8-Milestones::Config::V2_IMAGE_BYTES-8]==MilestonePools);
      // No other format carries the effect set section with 0.16's pooled
      // record (format 6 carries the 128-bit one).
      for(uint32_t format:{LEGACY_FORMAT,THEMES_FORMAT,MILESTONES_FORMAT,POOLS_FORMAT,FORMAT,FORMAT+1}) {
        auto bad=encoded;put32(bad.data()+4,format);put32(bad.data()+length-4,checksum(bad.data(),length-4));
        Contents ignored;assert(!decode(bad.data(),length,ignored));
      }
      // The set under another section id is refused, and so is the pooled
      // record under the 0.15 section id.
      for(uint8_t id:{uint8_t(MilestoneTable),uint8_t(MilestonePools)}) {
        auto bad=encoded;bad[length-4-EffectSet::Config::IMAGE_BYTES-8]=id;put32(bad.data()+length-4,checksum(bad.data(),length-4));
        Contents ignored;assert(!decode(bad.data(),length,ignored));
      }
      if(c->hasMilestones) {
        auto bad=encoded;bad[length-4-EffectSet::Config::IMAGE_BYTES-8-Milestones::Config::V2_IMAGE_BYTES-8]=MilestoneTable;
        put32(bad.data()+length-4,checksum(bad.data(),length-4));
        Contents ignored;assert(!decode(bad.data(),length,ignored));
      }
    }
    // A checksummed record with an invalid set is rejected whole.
    for(EffectSet::Mask mask:{EffectSet::Mask(),flag(5),flag(Appearance::EFFECTS),flag(127)}) {
      Buffer encoded;const size_t length=encode(only,"x",encoded);const size_t body=length-4-EffectSet::Config::IMAGE_BYTES;
      auto bad=encoded;EffectSet::putMask(bad.data()+body+8,mask);
      put32(bad.data()+body+24,Appearance::hash(bad.data()+body,24));put32(bad.data()+length-4,checksum(bad.data(),length-4));
      Contents ignored;assert(!decode(bad.data(),length,ignored));
    }
    // A restore writes the backup's set over whatever the device holds, a
    // damaged record included; a backup without one leaves the device's.
    Preferences::data.clear();Preferences::data["ll-appearanceeset0"]={9,9,9};
    EffectSet::Config device;AppearanceStore<EffectSet::Config> store;
    assert(!store.begin(device,"ll-appearance","eset0","eset1",true) && store.damaged() && device.isDefault());
    auto incoming=only.effectSet;incoming.revision=device.revision+1;assert(store.save(incoming));
    EffectSet::Config reloaded;AppearanceStore<EffectSet::Config> reader;
    assert(reader.begin(reloaded,"ll-appearance","eset0","eset1",true) && !reader.damaged() && reloaded.mask()==only.effectSet.mask());
    Contents plain;assert(decode(buffer.data(),n,plain) && !plain.hasEffectSet);
  }

  // A table whose pools hold an effect past value 63 (0.18) makes format 6:
  // the 128-bit pooled record, with or without custom themes and an effect
  // set (optional there). A table within effects 0-63 stays format 3-5.
  {
    using EffectSet::flag;using Appearance::Effect;using Milestones::Row;
    const Row wide[]={{9000,Effect::Sparkles},{12000,flag(Effect::Tidal)|flag(Effect::Rev)|flag(Effect::Sharkfin)},{15000,Effect::Volley}};
    auto pools=original;pools.hasMilestones=true;assert(pools.milestones.set(Milestones::Mode::Both,wide,3)==Milestones::Problem::None && !pools.milestones.fitsV2());
    pools.milestones.revision=0x11223344;
    auto withSet=pools;withSet.hasEffectSet=withSet.effectSet.set(flag(Effect::Slash)|flag(Effect::Meadow))==EffectSet::Problem::None;
    auto withAll=withSet;withAll.hasThemes=true;assert(withAll.themes.put(1,custom("Dragoon Storm",0x5c7f)));
    for(const auto *c:{&pools,&withSet,&withAll}) {
      assert(c->format()==FORMAT);
      Buffer encoded;const size_t length=encode(*c,"0.18.0-sticks3-test",encoded);
      const size_t setBytes=c->hasEffectSet?8+EffectSet::Config::IMAGE_BYTES:0;
      assert(get32(encoded.data()+4)==FORMAT && length==n+8+Milestones::Config::IMAGE_BYTES+setBytes+(c->hasThemes?8+Themes::Library::IMAGE_BYTES:0));
      const size_t pooled=length-4-setBytes-Milestones::Config::IMAGE_BYTES;
      assert(encoded[pooled-8]==MilestonePools && get32(encoded.data()+pooled)==Milestones::Config::MAGIC);
      Contents restored;assert(decode(encoded.data(),length,restored) && same(*c,restored));
      assert(restored.hasMilestones && restored.milestones.same(c->milestones) && restored.milestones.revision==c->milestones.revision);
      assert(restored.hasEffectSet==c->hasEffectSet && (!c->hasEffectSet || restored.effectSet.mask()==c->effectSet.mask()));
      for(size_t i=0;i<length;i+=5){auto bad=encoded;bad[i]^=0x20;Contents ignored;assert(!decode(bad.data(),length,ignored));}
      // No other format carries the 128-bit record.
      for(uint32_t format:{LEGACY_FORMAT,THEMES_FORMAT,MILESTONES_FORMAT,POOLS_FORMAT,EFFECT_SET_FORMAT,FORMAT+1}) {
        auto bad=encoded;put32(bad.data()+4,format);put32(bad.data()+length-4,checksum(bad.data(),length-4));
        Contents ignored;assert(!decode(bad.data(),length,ignored));
      }
      // The 128-bit record under the 0.15 section id is refused.
      {auto bad=encoded;bad[pooled-8]=MilestoneTable;put32(bad.data()+length-4,checksum(bad.data(),length-4));Contents ignored;assert(!decode(bad.data(),length,ignored));}
    }
    // Format 6 without its pooled section is not a backup.
    {Buffer encoded;const size_t length=encode(withSet,"x",encoded);(void)length;
     Contents setOnly=original;setOnly.hasEffectSet=true;setOnly.effectSet=withSet.effectSet;Buffer five;const size_t k=encode(setOnly,"x",five);
     assert(get32(five.data()+4)==EFFECT_SET_FORMAT);put32(five.data()+4,FORMAT);put32(five.data()+k-4,checksum(five.data(),k-4));
     Contents ignored;assert(!decode(five.data(),k,ignored));}
    // A checksummed v3 record with an invalid pool (an effect past the last) is rejected whole.
    {Buffer encoded;const size_t length=encode(pools,"x",encoded);const size_t body=length-4-Milestones::Config::IMAGE_BYTES;
     auto bad=encoded;bad[body+8+4+2+15]|=0x40; // row 0's pool, bit 126
     put32(bad.data()+body+Milestones::Config::IMAGE_BYTES-4,Appearance::hash(bad.data()+body,Milestones::Config::IMAGE_BYTES-4));
     put32(bad.data()+length-4,checksum(bad.data(),length-4));Contents ignored;assert(!decode(bad.data(),length,ignored));}
    // The edge: Volley (63) alone still fits 0.16's record, Starfall (64) does not.
    {auto edge=original;edge.hasMilestones=true;const Row v[]={{9000,flag(Effect::Volley)|flag(Effect::Orbit)}},w[]={{9000,flag(Effect::Starfall)|flag(Effect::Orbit)}};
     assert(edge.milestones.set(Milestones::Mode::Both,v,1)==Milestones::Problem::None && edge.format()==POOLS_FORMAT);
     assert(edge.milestones.set(Milestones::Mode::Both,w,1)==Milestones::Problem::None && edge.format()==FORMAT);}
  }

  // Expanded accents travel through the complete USB backup, including the
  // highest new ID and either saved Flip switch value.
  for(unsigned theme=0;theme<Appearance::THEMES;++theme)for(bool flipped:{false,true})for(auto mount:{MountOrientation::Mode::Top,MountOrientation::Mode::Sidecar}) {
    auto trial=original;trial.settings.flipped=flipped;trial.settings.mount=mount;
    assert(trial.look.select(Appearance::Theme(theme),Appearance::Effect::Lightning));
    Buffer encoded;const auto length=encode(trial,"0.12.1-sticks3-test",encoded);
    Contents restored;assert(decode(encoded.data(),length,restored) && same(trial,restored));
  }
  // The original reserved bytes are zero, so every legacy backup loads Top
  // regardless of the destination's previous choice. Sidecar uses only byte5.
  const size_t settingsBody=n-4-SETTINGS_BYTES;
  assert(buffer[settingsBody+5]==0 && buffer[settingsBody+6]==0 && buffer[settingsBody+7]==0);
  decoded.settings.mount=MountOrientation::Mode::Sidecar;
  assert(decode(buffer.data(),n,decoded) && decoded.settings.mount==MountOrientation::Mode::Top);
  for(unsigned offset:{5u,6u,7u}) {
    Buffer bad=buffer;bad[settingsBody+offset]=offset==5?2:1;
    put32(bad.data()+n-4,checksum(bad.data(),n-4));
    auto retained=decoded;assert(!decode(bad.data(),n,decoded) && same(decoded,retained));
  }

  // Base64 lines through the receiver reproduce the exact bytes.
  std::vector<std::string> lines;base64Lines(buffer.data(),n,96,[&](const char *line){lines.push_back(line);});
  for(const auto &line:lines)assert(line.size()<=96);
  uint32_t now=0;Receiver rx;
  assert(transfer(rx,lines,now)==Receiver::Event::Complete && !rx.active());
  std::array<uint8_t,MAX_BYTES> back{};size_t written=0;
  assert(rx.take(back.data(),back.size(),written) && written==n && std::equal(back.begin(),back.begin()+n,buffer.begin()));
  // Any line split works: the device concatenates before decoding.
  std::string all;for(const auto &line:lines)all+=line;
  std::vector<std::string> odd;for(size_t i=0;i<all.size();i+=37)odd.push_back(all.substr(i,37));
  assert(transfer(rx,odd,now)==Receiver::Event::Complete && rx.take(back.data(),back.size(),written) && written==n);

  // No motion yet (a fresh device) is still a complete backup.
  auto fresh=original;fresh.hasMotion=false;
  Buffer small;const size_t m=encode(fresh,"x",small);assert(m==n-8-LaunchMotion::IMAGE_BYTES);
  Contents noMotion;assert(decode(small.data(),m,noMotion) && !noMotion.hasMotion && same(fresh,noMotion));

  // Every corruption is rejected before anything could be written.
  Contents rejected=sample();rejected.look.select(Appearance::Theme::Mint,Appearance::Effect::Off);
  const auto untouched=rejected;
  for(size_t i=0;i<n;i+=7){auto bad=buffer;bad[i]^=0x40;assert(!decode(bad.data(),n,rejected));}
  assert(same(rejected,untouched));
  assert(!decode(buffer.data(),n-1,rejected) && !decode(buffer.data(),HEADER_BYTES,rejected));
  {
    // Format 2 without its themes section is not a backup either, nor format 3 without milestones.
    for(uint32_t format:{THEMES_FORMAT,MILESTONES_FORMAT,POOLS_FORMAT,EFFECT_SET_FORMAT,FORMAT}) {
      auto bad=buffer;put32(bad.data()+4,format);put32(bad.data()+n-4,checksum(bad.data(),n-4));assert(!decode(bad.data(),n,rejected));
    }
  }
  {
    // A valid checksum cannot rescue an invalid setting.
    auto bad=sample();bad.settings.brightness=45;Buffer out;const size_t k=encode(bad,"x",out);assert(!decode(out.data(),k,rejected));
    bad=sample();bad.settings.sleepMinutes=0;encode(bad,"x",out);assert(!decode(out.data(),k,rejected));
  }
  {
    // Missing required section: drop the settings section and re-seal.
    Buffer out=buffer;const size_t cut=n-4-8-SETTINGS_BYTES;put32(out.data()+8,uint32_t(cut+4));
    put32(out.data()+cut,checksum(out.data(),cut));assert(!decode(out.data(),cut+4,rejected));
  }
  {
    // Duplicate section: repeat the settings section.
    Buffer out{};std::copy(buffer.begin(),buffer.begin()+n-4,out.begin());
    const size_t settings=n-4-8-SETTINGS_BYTES;
    if(n+8+SETTINGS_BYTES<=MAX_BYTES) {
      std::copy(buffer.begin()+settings,buffer.begin()+n-4,out.begin()+n-4);
      const size_t k=n+8+SETTINGS_BYTES;put32(out.data()+8,uint32_t(k));put32(out.data()+k-4,checksum(out.data(),k-4));
      assert(!decode(out.data(),k,rejected));
    }
  }

  // Receiver bounds: stray characters, oversize, a bad terminator and silence.
  rx.start(0);for(char c:std::string("QUJD\n"))rx.feed(c,1);
  assert(rx.feed('#',2)==Receiver::Event::Error && std::string(rx.error())=="bad_character" && !rx.active());
  rx.start(0);assert(rx.feed('.',1)==Receiver::Event::None && rx.feed('x',2)==Receiver::Event::Error);
  rx.start(0);auto event=Receiver::Event::None;
  for(unsigned i=0;i<=TEXT_BYTES && event!=Receiver::Event::Error;++i)event=rx.feed('A',1);
  assert(event==Receiver::Event::Error && std::string(rx.error())=="too_large");
  rx.start(100);assert(!rx.timedOut(100+Receiver::IDLE_MS-1) && rx.timedOut(100+Receiver::IDLE_MS));
  rx.start(0);assert(rx.feed('Q',1)==Receiver::Event::None && rx.feed('\n',2)==Receiver::Event::Chunk && rx.received()==1);
  assert(rx.feed('.',3)==Receiver::Event::None && rx.feed('\n',4)==Receiver::Event::Complete && !rx.take(back.data(),back.size(),written));

  // A theme request is one short framed line; the theme receiver is bounded to it.
  {
    DeviceBackup::LineReceiver<(Themes::Request::BYTES+2)/3*4> themeRx;
    Themes::Request request;request.op=Themes::Request::Op::Put;request.argument=2;request.theme=custom("Wolf Night",0x8ff9);
    uint8_t bytes[Themes::Request::BYTES];request.encode(bytes);
    std::vector<std::string> text;base64Lines(bytes,sizeof(bytes),96,[&](const char *line){text.push_back(line);});
    assert(text.size()==1 && text[0].size()==60);
    uint32_t at=0;themeRx.start(at);for(char ch:text[0]+"\n.\n")event=themeRx.feed(ch,++at);
    uint8_t back[Themes::Request::BYTES];size_t got=0;Themes::Request decoded;
    assert(event==Receiver::Event::Complete && themeRx.take(back,sizeof(back),got) && decoded.decode(back,got));
    assert(decoded.op==request.op && decoded.argument==2 && !std::strcmp(decoded.theme.name,"Wolf Night"));
    themeRx.start(0);event=Receiver::Event::None;
    for(unsigned i=0;i<=60 && event!=Receiver::Event::Error;++i)event=themeRx.feed('A',1);
    assert(event==Receiver::Event::Error && std::string(themeRx.error())=="too_large");
  }

  // Restoring an older backup onto a device whose stores hold newer generations:
  // the restored records must be the ones loaded after the restart.
  Preferences::data.clear();
  PracticeHistory live;PracticeStore practiceStore;assert(practiceStore.begin(live));
  for(uint32_t i=1;i<=60;++i){live.accept(i,true,6000,i*1000);assert(practiceStore.save(live));}
  Appearance::Config look;AppearanceStore<Appearance::Config> lookStore;assert(lookStore.begin(look,"ll-appearance","cfg0","cfg1"));
  for(unsigned i=0;i<9;++i){look.select(Appearance::Theme(i%4),Appearance::Effect(i%6));assert(lookStore.save(look));}
  Appearance::Bests bests;AppearanceStore<Appearance::Bests> bestStore;assert(bestStore.begin(bests,"ll-bests","best0","best1"));
  for(unsigned i=0;i<12;++i){bests.observe(0,0,true,9000+i);assert(bestStore.save(bests));}
  LaunchMotion::Trace reference;MotionStore motionStore;assert(motionStore.begin(reference));
  assert(motionStore.save(trace(55,6000)) && motionStore.save(trace(60,6000)));
  auto restore=decoded;
  assert(restore.practice.generation()<live.generation() && restore.look.revision<look.revision && restore.bests.revision<bests.revision);
  restore.practice.supersede(live.generation());restore.look.revision=look.revision+1;restore.bests.revision=bests.revision+1;
  assert(practiceStore.save(restore.practice) && lookStore.save(restore.look) && bestStore.save(restore.bests) && motionStore.save(restore.motion));
  PracticeHistory afterHistory;PracticeStore p2;assert(p2.begin(afterHistory) && afterHistory.size()==41 && afterHistory.recent()->rpm==9100);
  Appearance::Config afterLook;AppearanceStore<Appearance::Config> l2;assert(l2.begin(afterLook,"ll-appearance","cfg0","cfg1"));
  assert(afterLook.theme==Appearance::Theme::Amber && afterLook.effect==Appearance::Effect::Lightning);
  Appearance::Bests afterBests;AppearanceStore<Appearance::Bests> b2;assert(b2.begin(afterBests,"ll-bests","best0","best1"));
  assert(afterBests.rpm[0]==8800 && afterBests.rpm[3]==7700);
  LaunchMotion::Trace afterMotion;MotionStore m2;assert(m2.begin(afterMotion) && afterMotion.number==41);
  // Restoring the same backup twice is harmless: it supersedes again.
  restore.practice.supersede(afterHistory.generation());assert(p2.save(restore.practice));
  PracticeHistory again;PracticeStore p3;assert(p3.begin(again) && again.size()==41);

  std::cout<<"PASS: device backup round trip, base64 line transfer at any split, fresh device without motion, all-or-nothing validation, receiver bounds, and restored records outrank newer saved generations\n";
}
