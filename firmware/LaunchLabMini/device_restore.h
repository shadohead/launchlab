#pragma once
#include <Preferences.h>
#include "appearance_store.h"
#include "battery_icon_setting.h"
#include "brightness.h"
#include "device_backup.h"
#include "display_flip_setting.h"
#include "history_log.h"
#include "milestone_store.h"
#include "min_rpm_setting.h"
#include "motion_store.h"
#include "mount_orientation_setting.h"
#include "practice_store.h"
#include "rpm_estimator_setting.h"
#include "sensor_profile_setting.h"
#include "shake_wake_setting.h"
#include "theme_store.h"

// Writing a validated USB backup (`R`), all or nothing as far as the device
// shows: after any outcome the device shows what it loads at the next boot.
//
// - Nothing is written until the loaded-effects store can take the backup's
//   effects (Effects::prepare: it may format a region nothing uses yet).
// - Each restored record supersedes what RAM and NVS hold. The NVS records
//   go first, history and bests last of them; the loaded effects after all
//   records, so a restore NVS refuses never rewrites them.
// - With the history log open, history and bests are restored once a copy
//   the boot pick (history_log.h) takes holds them: the log record carries
//   the generation NVS holds after its own write, so NVS refusing the mirror
//   (a full unit) does not fail a restore, as at power-off.
// - On a failure every record that changed is written back from the device's
//   own values, still in RAM, superseding what the restore wrote (RolledBack:
//   RAM is what storage holds). If a write back fails too, or the effects
//   were replaced in part, the outcome is Partial and the caller restarts so
//   the device shows what storage holds.
// - A write storage took that did not read back (a store's unverified()) may
//   have left the backup's record where the next boot loads it, though the
//   store still names the device's as saved. It counts as changed: the
//   device's own value is written back over it, and RolledBack is reported
//   only once no store holds such a write.
// - Stores refuse a save older than the record they hold (it would lose at
//   boot), so a device left behind a restored record cannot drop pulls.
// Every write is one record's own (an NVS blob, a log record), so a power cut
// at any point leaves each record loadable, the device's or the backup's.
namespace DeviceRestore {
// The records in the order they are written.
enum class Step:uint8_t {Settings,Themes,Milestones,EffectSet,Look,Motion,History,Bests,Effects,None};
constexpr unsigned STEPS=unsigned(Step::None);
inline const char *name(Step s) {
  static const char *const names[]={"settings","themes","milestones","effect_set","look","motion","history","bests","effects","none"};
  return names[unsigned(s)<STEPS?unsigned(s):STEPS];
}
enum class Outcome:uint8_t {Restored,Refused,RolledBack,Partial};
struct Result {
  Outcome outcome=Outcome::Restored;Step failed=Step::None;
  bool historyMirrored=true,bestsMirrored=true;  // NVS took the copy (the log may hold it alone)
};

// The stores, the history log and the effects a restore writes, and what the
// device shows now (put back after a failure). Effects: prepare(contents)
// before anything is written, replace(contents) to install them.
template<class Flash,class Fx> struct Device {
  PracticeStore &practiceStore;AppearanceStore<Appearance::Config> &lookStore;AppearanceStore<Appearance::Bests> &bestStore;
  MotionStore &motionStore;ThemeStore &themeStore;MilestoneStore &milestoneStore;AppearanceStore<EffectSet::Config> &effectSetStore;
  Preferences &prefs;HistoryLog::Store<Flash> &log;Fx &effects;
  PracticeHistory &practice;Appearance::Config &look;Appearance::Bests &bests;LaunchMotion::Trace &motion;
  Themes::Library &themes;Milestones::Config &milestones;EffectSet::Config &effectSet;DeviceBackup::UserSettings settings;
  uint32_t &loggedHistory,&loggedBests;
};

// The newer of two generations (they wrap, as the stores compare them).
inline uint32_t newer(uint32_t a,uint32_t b){return int32_t(a-b)>=0?a:b;}

// The settings as boot loads them.
inline DeviceBackup::UserSettings heldSettings(Preferences &prefs) {
  DeviceBackup::UserSettings s;s.sleepMinutes=prefs.getUChar("sleep_min",InactivityTimer::DEFAULT_MINUTES);
  Brightness::load(prefs,s.brightness);RpmEstimatorSetting::load(prefs,s.rpmMethod);SensorProfileSetting::load(prefs,s.sensorProfile);
  DisplayFlipSetting::load(prefs,s.flipped);MountOrientationSetting::load(prefs,s.mount);
  ShakeWakeSetting::load(prefs,s.shakeWake);BatteryIconSetting::load(prefs,s.batteryIcon);MinRpmSetting::load(prefs,s.minRpmSteps);
  return s;
}
// Every setting, or (given what NVS holds) only those that differ.
inline bool saveSettings(Preferences &prefs,const DeviceBackup::UserSettings &s,const DeviceBackup::UserSettings *held=nullptr) {
  auto differs=[&](auto field){return !held || held->*field!=s.*field;};
  using U=DeviceBackup::UserSettings;
  return (!differs(&U::sleepMinutes) || prefs.putUChar("sleep_min",s.sleepMinutes)==1) &&
    (!differs(&U::brightness) || prefs.putUChar(Brightness::KEY,s.brightness)==1) &&
    (!differs(&U::rpmMethod) || RpmEstimatorSetting::save(prefs,s.rpmMethod)) &&
    (!differs(&U::sensorProfile) || SensorProfileSetting::save(prefs,s.sensorProfile)) &&
    (!differs(&U::flipped) || DisplayFlipSetting::save(prefs,s.flipped)) && (!differs(&U::mount) || MountOrientationSetting::save(prefs,s.mount)) &&
    (!differs(&U::shakeWake) || ShakeWakeSetting::save(prefs,s.shakeWake)) && (!differs(&U::batteryIcon) || BatteryIconSetting::save(prefs,s.batteryIcon)) &&
    (!differs(&U::minRpmSteps) || MinRpmSetting::save(prefs,s.minRpmSteps));
}
// History to NVS, then to the log naming the generation NVS now holds: saved
// once either copy the boot pick takes holds it.
template<class D> bool saveHistory(D &d,const PracticeHistory &h,bool &mirrored) {
  mirrored=d.practiceStore.save(h);
  if(!d.log.isOpen() || !d.log.saveHistory(h,d.practiceStore.savedGeneration()))return mirrored;
  d.loggedHistory=h.generation();return true;
}
template<class D> bool saveBests(D &d,const Appearance::Bests &b,bool &mirrored) {
  mirrored=d.bestStore.save(b);
  if(!d.log.isOpen() || !d.log.saveBests(b,d.bestStore.savedGeneration()))return mirrored;
  d.loggedBests=b.generation();return true;
}

template<class D> bool apply(D &d,DeviceBackup::Contents &c,Step s,Result &r) {
  switch(s) {
    case Step::Settings:return saveSettings(d.prefs,c.settings);
    case Step::Themes:return !c.hasThemes || d.themeStore.save(c.themes,true);
    case Step::Milestones:return !c.hasMilestones || d.milestoneStore.save(c.milestones);
    case Step::EffectSet:return !c.hasEffectSet || d.effectSetStore.saveViewed(c.effectSet);
    case Step::Look:return d.lookStore.saveViewed(c.look);
    case Step::Motion:return !c.hasMotion || d.motionStore.save(c.motion);
    case Step::History:return saveHistory(d,c.practice,r.historyMirrored);
    case Step::Bests:return saveBests(d,c.bests,r.bestsMirrored);
    case Step::Effects:return d.effects.replace(c);
    default:return false;
  }
}
// What each store held before the restore wrote: a store whose generation
// moved took a restored record (or part of one) that must be written back.
struct Before {uint32_t history,bests,look,milestones,effectSet,motion,loggedHistory,loggedBests;};
template<class D> Before before(const D &d) {
  return {d.practiceStore.savedGeneration(),d.bestStore.savedGeneration(),d.lookStore.savedGeneration(),d.milestoneStore.savedGeneration(),
    d.effectSetStore.savedGeneration(),d.motionStore.generation(),d.loggedHistory,d.loggedBests};
}
// The device's own value written back over a restored record, above every
// generation the restore wrote (`restored`: the backup's record as written).
// True when storage holds the device's value again (or never lost it).
template<class Value,class Store> bool putBack(Value &v,Store &store,uint32_t held,uint32_t restored) {
  if(store.savedGeneration()==held && !store.unverified())return true;
  v.revision=newer(newer(v.generation(),store.savedGeneration()),restored)+1;return store.saveViewed(v) && !store.unverified();
}
// Each case: nothing to do when the store holds what it held before and no
// write of the restore's is unverified; else the device's value goes back,
// and counts as back only when that leaves no unverified write either.
template<class D> bool undo(D &d,const DeviceBackup::Contents &c,Step s,const Before &b) {
  bool mirrored=false;
  switch(s) {
    case Step::Settings:{const auto held=heldSettings(d.prefs);return saveSettings(d.prefs,d.settings,&held);}
    case Step::Themes:
      if(!d.themeStore.pending(d.themes) && !d.themeStore.unverified())return true;
      return (d.themes.empty()?d.themeStore.clear():d.themeStore.save(d.themes,true)) && !d.themeStore.unverified();
    case Step::Milestones:
      if(d.milestoneStore.savedGeneration()==b.milestones && !d.milestoneStore.unverified())return true;
      d.milestones.revision=newer(newer(d.milestones.generation(),d.milestoneStore.savedGeneration()),c.milestones.generation())+1;
      return d.milestoneStore.save(d.milestones) && !d.milestoneStore.unverified();
    case Step::EffectSet:return putBack(d.effectSet,d.effectSetStore,b.effectSet,c.effectSet.generation());
    case Step::Look:return putBack(d.look,d.lookStore,b.look,c.look.generation());
    case Step::Motion:
      if(d.motionStore.generation()==b.motion && !d.motionStore.unverified())return true;
      return (d.motion.valid()?d.motionStore.save(d.motion):d.motionStore.clear()) && !d.motionStore.unverified();
    case Step::History:
      // Pulls not yet saved stay pending as before when neither copy changed.
      if(d.practiceStore.savedGeneration()==b.history && d.loggedHistory==b.loggedHistory &&
         !d.practiceStore.unverified() && !d.log.unverified(HistoryLog::History))return true;
      d.practice.supersede(newer(d.practice.generation(),c.practice.generation()));
      return saveHistory(d,d.practice,mirrored) && !d.practiceStore.unverified() && !d.log.unverified(HistoryLog::History);
    case Step::Bests:
      if(d.bestStore.savedGeneration()==b.bests && d.loggedBests==b.loggedBests &&
         !d.bestStore.unverified() && !d.log.unverified(HistoryLog::Bests))return true;
      d.bests.revision=newer(d.bests.generation(),c.bests.generation())+1;
      return saveBests(d,d.bests,mirrored) && !d.bestStore.unverified() && !d.log.unverified(HistoryLog::Bests);
    default:return false;  // replaced effects cannot come back
  }
}

// Writes `c` (already validated) inside the caller's ADC checkpoint.
template<class D> Result run(D &d,DeviceBackup::Contents &c) {
  Result r;
  if(!d.effects.prepare(c)){r.outcome=Outcome::Refused;r.failed=Step::Effects;return r;}
  c.practice.supersede(newer(d.practice.generation(),d.practiceStore.savedGeneration()));
  c.look.revision=newer(d.look.generation(),d.lookStore.savedGeneration())+1;
  c.bests.revision=newer(d.bests.generation(),d.bestStore.savedGeneration())+1;
  c.themes.revision=d.themes.revision+1;
  c.milestones.revision=newer(d.milestones.generation(),d.milestoneStore.savedGeneration())+1;
  c.effectSet.revision=newer(d.effectSet.generation(),d.effectSetStore.savedGeneration())+1;
  const Before held=before(d);
  unsigned s=0;
  while(s<STEPS && apply(d,c,Step(s),r))++s;
  if(s==STEPS)return r;
  r.failed=Step(s);
  // The failed step too: it may have written part of itself.
  bool clean=true;
  for(unsigned i=s+1;i-->0;)clean=undo(d,c,Step(i),held) && clean;
  r.outcome=clean?Outcome::RolledBack:Outcome::Partial;
  return r;
}
}
