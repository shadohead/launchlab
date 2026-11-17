// What firmware without loaded effects does with this firmware's records,
// using the frozen 0.18.0 source itself (firmware/releases/0.18.0-*/source)
// compiled beside this firmware in its own namespace
// (docs/FIRMWARE_LOADABLE_EFFECTS.md, "IDs and compatibility"):
//  - an effect set that offers loaded effects is saved with the view 0.18
//    can read one generation below it, so 0.18 offers the set's drawn
//    effects instead of falling back to its default;
//  - a change made on 0.18 lands over the record and wins when the unit is
//    upgraded again;
//  - a set without loaded effects is saved exactly as 0.18 saves it;
//  - a Best effect that is a loaded slot shows on 0.18 as its stock
//    fallback, with the theme and sound as they were;
//  - a milestone table naming loaded slots shows on 0.18 with each slot's
//    stock fallback in its rows, and 0.18's own change wins on re-upgrade;
//  - a backup naming or carrying loaded effects (format 7) is refused whole
//    by 0.18, before anything is written; any other backup it reads as before.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <Preferences.h>
// Needs clang: GCC's #pragma once also skips a frozen header that is
// byte-identical to one already included, which would compile the frozen
// release against today's types instead.
#if defined(__GNUC__) && !defined(__clang__)
#error "test_loaded_compat needs clang (GCC merges byte-identical #pragma once headers)"
#endif
#include "../LaunchLabMini/appearance_store.h"
#include "../LaunchLabMini/effect_set.h"
#include "../LaunchLabMini/milestone_store.h"
#include "../LaunchLabMini/device_backup.h"
namespace Released018 {
#include "../releases/0.18.0-sticks3-anime-pm/source/appearance_store.h"
#include "../releases/0.18.0-sticks3-anime-pm/source/effect_set.h"
#include "../releases/0.18.0-sticks3-anime-pm/source/milestone_store.h"
#include "../releases/0.18.0-sticks3-anime-pm/source/device_backup.h"
}

int main() {
  namespace Old=Released018;
  using EffectSet::flag;using Appearance::Effect;
  static_assert(Old::Appearance::EFFECTS==Appearance::EFFECTS,"0.18 has today's built-ins");
  // Two loaded effects installed here.
  auto &hooks=Appearance::loadedHooks();
  hooks.playable=[](unsigned e){return e==96 || e==101;};
  {
    Preferences::data.clear();
    // A set offering Slash, Crimson and two loaded effects, saved as the sketch does.
    EffectSet::Config set;AppearanceStore<EffectSet::Config> store;
    assert(store.begin(set,"ll-appearance","eset0","eset1",true));
    const EffectSet::Mask mine=flag(Effect::Slash)|flag(Effect::Crimson)|flag(96)|flag(101);
    assert(set.set(mine)==EffectSet::Problem::None);
    ++set.revision;  // room for the view one generation below
    assert(set.needsOlderView() && store.saveWithOlderView(set,set.olderView()));
    // This firmware reads its own record back.
    {EffectSet::Config back;AppearanceStore<EffectSet::Config> s;assert(s.begin(back,"ll-appearance","eset0","eset1",true) && back.mask()==mine && back.revision==set.revision);}
    // 0.18 reads the view: the set's drawn effects, not its default.
    Old::EffectSet::Config old;Old::AppearanceStore<Old::EffectSet::Config> oldStore;
    assert(oldStore.begin(old,"ll-appearance","eset0","eset1",true) && !oldStore.damaged());
    assert(old.revision==set.revision-1 && old.mask().lo==(flag(Effect::Slash)|flag(Effect::Crimson)).lo && old.mask().hi==(flag(Effect::Slash)|flag(Effect::Crimson)).hi);
    // A change made on 0.18 (Tidal added) is saved over the record...
    assert(old.set(Old::EffectSet::Mask(old.mask().lo|(uint64_t(1)<<unsigned(Effect::Tidal)),old.mask().hi))==Old::EffectSet::Problem::None);
    assert(oldStore.save(old));
    // ...and wins when the unit is upgraded again.
    EffectSet::Config again;AppearanceStore<EffectSet::Config> upgraded;
    assert(upgraded.begin(again,"ll-appearance","eset0","eset1",true) && again.mask()==(flag(Effect::Slash)|flag(Effect::Crimson)|flag(Effect::Tidal)) &&
           again.revision==old.revision);
  }
  // A set without loaded effects is one ordinary save, byte for byte as 0.18 writes it.
  {
    Preferences::data.clear();
    EffectSet::Config set;AppearanceStore<EffectSet::Config> store;assert(store.begin(set,"ll-appearance","eset0","eset1",true));
    assert(set.set(flag(Effect::Slash)|flag(Effect::Blink))==EffectSet::Problem::None && store.save(set));
    const auto ours=Preferences::data;Preferences::data.clear();
    Old::EffectSet::Config old;Old::AppearanceStore<Old::EffectSet::Config> oldStore;assert(oldStore.begin(old,"ll-appearance","eset0","eset1",true));
    assert(old.set(Old::EffectSet::Mask(set.mask().lo,set.mask().hi))==Old::EffectSet::Problem::None && oldStore.save(old));
    assert(Preferences::data==ours);
  }
  // The Best effect: a loaded slot (fallback Sparkles) under a custom theme and sound.
  {
    Preferences::data.clear();
    hooks.fallback=[](unsigned e)->unsigned{return e==101?unsigned(Effect::Sparkles):~0u;};
    Appearance::Config look;AppearanceStore<Appearance::Config> store;assert(store.begin(look,"ll-appearance","cfg0","cfg1"));
    assert(look.select(Appearance::Theme::Amber,Effect(101),Appearance::Sound::High));
    ++look.revision;
    assert(look.needsOlderView() && store.saveWithOlderView(look,look.olderView()));
    {Appearance::Config back;AppearanceStore<Appearance::Config> s;assert(s.begin(back,"ll-appearance","cfg0","cfg1") && back.effect==Effect(101) && back.revision==look.revision);}
    Old::Appearance::Config old;Old::AppearanceStore<Old::Appearance::Config> oldStore;
    assert(oldStore.begin(old,"ll-appearance","cfg0","cfg1"));
    assert(old.effect==Old::Appearance::Effect::Sparkles && old.theme==Old::Appearance::Theme::Amber && old.sound==Old::Appearance::Sound::High);
    // Chosen on 0.18 (Tidal), kept on re-upgrade.
    assert(old.select(old.theme,Old::Appearance::Effect::Tidal) && oldStore.save(old));
    Appearance::Config again;AppearanceStore<Appearance::Config> upgraded;
    assert(upgraded.begin(again,"ll-appearance","cfg0","cfg1") && again.effect==Effect::Tidal && again.theme==Appearance::Theme::Amber);
    // A deleted loaded Best effect, or one without a fallback, stands in as Orbit.
    assert(Appearance::stockFallback(Effect(100))==Effect::Orbit && Appearance::stockFallback(Effect::Slash)==Effect::Slash);
  }
  // Milestones: slots 96 (fallback Tidal) and 101 (fallback Sparkles) in pools.
  {
    Preferences::data.clear();
    hooks.fallback=[](unsigned e)->unsigned{return e==96?unsigned(Effect::Tidal):e==101?unsigned(Effect::Sparkles):~0u;};
    using Milestones::Row;using Milestones::Mode;
    const Row rows[]={{9000,Effect::Orbit},{12000,flag(Effect::Slash)|flag(96)},{15000,flag(101)}};
    Milestones::Config table;MilestoneStore store;assert(!store.begin(table));
    assert(table.set(Mode::Both,rows,3)==Milestones::Problem::None);++table.revision;
    assert(store.save(table));
    {Milestones::Config back;MilestoneStore s;assert(s.begin(back) && back.same(table));}
    Old::Milestones::Config old;Old::MilestoneStore oldStore;assert(oldStore.begin(old));
    assert(old.revision==table.revision-1 && old.count()==3 && old.row(0).pool.lo==flag(Effect::Orbit).lo);
    assert(old.row(1).pool.lo==(flag(Effect::Slash)|flag(Effect::Tidal)).lo && !old.row(1).pool.hi);
    assert(old.row(2).pool.lo==flag(Effect::Sparkles).lo && !old.row(2).pool.hi);
    // 0.18 changes the mode; its table wins on re-upgrade.
    assert(old.select(Old::Milestones::Mode::Milestones) && oldStore.save(old));
    Milestones::Config again;MilestoneStore upgraded;
    assert(upgraded.begin(again) && again.mode()==Mode::Milestones && again.revision==old.revision && !again.namesLoaded());
  }
  // Backups: format 7 refused whole by 0.18; a backup without loaded effects is one it reads.
  {
    DeviceBackup::Contents named;named.look.select(Appearance::Theme::Classic,Appearance::Effect(101));
    DeviceBackup::Buffer b;const size_t n=DeviceBackup::encode(named,"0.19.0",b);
    assert(n && DeviceBackup::get32(b.data()+4)==DeviceBackup::LOADED_FORMAT);
    auto old=std::unique_ptr<Old::DeviceBackup::Contents>(new Old::DeviceBackup::Contents);
    assert(n<=Old::DeviceBackup::MAX_BYTES && !Old::DeviceBackup::decode(b.data(),n,*old));
    DeviceBackup::Contents plain;plain.look.select(Appearance::Theme::Classic,Effect::Slash);
    const size_t m=DeviceBackup::encode(plain,"0.19.0",b);assert(Old::DeviceBackup::decode(b.data(),m,*old) && old->look.effect==Old::Appearance::Effect::Slash);
  }
  hooks=Appearance::LoadedHooks();
  std::puts("test_loaded_compat: ok");
}
