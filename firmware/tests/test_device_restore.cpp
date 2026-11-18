// DeviceRestore (device_restore.h) on the real stores and history log: a
// restore is all or nothing as the device shows it, and what it shows after
// any outcome is what the next boot loads.
#include "../LaunchLabMini/device_restore.h"
#include "nor_flash.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>
using namespace DeviceRestore;
using Appearance::Effect;using Appearance::Theme;

struct Fx {
  bool ready=true,fail=false;unsigned prepared=0,replaced=0;NorFlash *dies=nullptr;
  bool prepare(const DeviceBackup::Contents &){++prepared;return ready;}
  // `dies`: every write from here on fails, NVS and flash alike.
  bool replace(const DeviceBackup::Contents &c){if(c.loadedCount)++replaced;if(dies){dies->dead=true;Preferences::failWrite=true;}return !fail;}
};
template<class T> static void fresh(T &t){t.~T();new(&t) T();}
static Palette::Custom custom(const char *name,uint16_t accent) {
  Palette::Custom t;std::strncpy(t.name,name,Palette::Custom::MAX_NAME);
  const uint16_t roles[Palette::ROLES]={0x0841,0xffff,0x8c71,accent,accent,0xfda0,0x07e0,0xf800};
  std::memcpy(t.role,roles,sizeof(roles));assert(t.valid());return t;
}
static LaunchMotion::Trace trace(uint32_t number,float rpm) {
  LaunchMotion::Trace t;t.fused=true;t.quality=LaunchMotion::Quality::Valid;t.count=48;t.number=number;t.rpm=rpm;t.durationMs=400;t.gravity[2]=1;
  for(unsigned i=0;i<t.count;++i)t.points[i].ms=-500+int(i*1150/47);
  t.startLevel={StickS3Level::State::Valid,6,.2f,0};t.endLevel={StickS3Level::State::Valid,15,0,-.5f};
  return t;
}
static void pull(PracticeHistory &h,Appearance::Bests &b,float rpm){h.accept(h.size()+1,true,rpm,1000*(h.size()+1));b.observe(0,0,true,rpm);}

// One unit: its storage, its stores and what it shows.
struct Unit {
  NorFlash flash;HistoryLog::Store<NorFlash> log;Preferences prefs;bool logOpen=true;
  PracticeStore practiceStore;AppearanceStore<Appearance::Config> lookStore;AppearanceStore<Appearance::Bests> bestStore;
  MotionStore motionStore;ThemeStore themeStore;MilestoneStore milestoneStore;AppearanceStore<EffectSet::Config> effectSetStore;Fx fx;
  PracticeHistory practice;Appearance::Config look;Appearance::Bests bests;LaunchMotion::Trace motion;Themes::Library themes;
  Milestones::Config milestones;EffectSet::Config effectSet;DeviceBackup::UserSettings settings;uint32_t loggedHistory=0,loggedBests=0;
  // setup()'s loads.
  void boot() {
    Preferences::failWritePrefixes.clear();
    fresh(log);if(logOpen)assert(log.open(flash)!=HistoryLog::Store<NorFlash>::Log::Open::Failed);
    prefs.begin("launchlab-mini",false);settings={};
    settings.sleepMinutes=prefs.getUChar("sleep_min",InactivityTimer::DEFAULT_MINUTES);Brightness::load(prefs,settings.brightness);
    RpmEstimatorSetting::load(prefs,settings.rpmMethod);SensorProfileSetting::load(prefs,settings.sensorProfile);
    DisplayFlipSetting::load(prefs,settings.flipped);MountOrientationSetting::load(prefs,settings.mount);
    ShakeWakeSetting::load(prefs,settings.shakeWake);BatteryIconSetting::load(prefs,settings.batteryIcon);
    MinRpmSetting::load(prefs,settings.minRpmSteps);
    look={};fresh(lookStore);lookStore.begin(look,"ll-appearance","cfg0","cfg1");
    bests={};fresh(bestStore);const bool bestsInNvs=bestStore.begin(bests,"ll-bests","best0","best1") && bestStore.present();
    {Appearance::Bests logged;uint32_t mirrored=0;const bool inLog=log.isOpen() && log.loadBests(logged,mirrored);
     const auto nvs=bestsInNvs?HistoryLog::Nvs::Found:bestStore.ready() && !bestStore.present()?HistoryLog::Nvs::Absent:HistoryLog::Nvs::Unreadable;
     const auto from=HistoryLog::pick(inLog,mirrored,nvs,bests.generation());
     if(from==HistoryLog::Source::Log)bests=logged;loggedBests=from==HistoryLog::Source::Log?bests.generation():~bests.generation();}
    themes={};fresh(themeStore);themeStore.begin(themes);
    milestones={};fresh(milestoneStore);milestoneStore.begin(milestones);
    effectSet={};fresh(effectSetStore);effectSetStore.begin(effectSet,"ll-appearance","eset0","eset1",true);
    practice={};fresh(practiceStore);assert(practiceStore.begin(practice));
    if(log.isOpen()) {
      PracticeHistory logged;uint32_t mirrored=0;const bool inLog=log.loadHistory(logged,mirrored);
      const auto nvs=practiceStore.found()?HistoryLog::Nvs::Found:practiceStore.ready()?HistoryLog::Nvs::Absent:HistoryLog::Nvs::Unreadable;
      const auto from=HistoryLog::pick(inLog,mirrored,nvs,practiceStore.savedGeneration());
      if(from==HistoryLog::Source::Log)practice=logged;loggedHistory=from==HistoryLog::Source::Log?practice.generation():~practice.generation();
    }
    motion={};fresh(motionStore);assert(motionStore.begin(motion));
  }
  Device<NorFlash,Fx> device() {
    return {practiceStore,lookStore,bestStore,motionStore,themeStore,milestoneStore,effectSetStore,prefs,log,fx,
            practice,look,bests,motion,themes,milestones,effectSet,settings,loggedHistory,loggedBests};
  }
};

// The device: every record saved (history and bests to NVS and the log, one
// pull more only in RAM), a custom theme, its own table, set and reference.
static std::unique_ptr<Unit> device(bool withLog) {
  Preferences::data.clear();Preferences::failWritePrefixes.clear();
  auto u=std::unique_ptr<Unit>(new Unit);u->logOpen=withLog;u->boot();
  u->settings.sleepMinutes=3;u->settings.brightness=50;u->settings.flipped=true;u->settings.minRpmSteps=12;assert(saveSettings(u->prefs,u->settings));
  assert(u->look.select(Theme::Mint,Effect(2)) && u->lookStore.saveViewed(u->look));
  assert(u->themes.put(0,custom("Own",0x5c7f)) && u->themeStore.save(u->themes));
  const Milestones::Row rows[]={{9000,Effect::Orbit}};assert(u->milestones.set(Milestones::Mode::Both,rows,1)==Milestones::Problem::None && u->milestoneStore.save(u->milestones));
  u->effectSet.set(EffectSet::DEFAULT_MASK&~EffectSet::flag(Effect::Orbit));assert(u->effectSetStore.saveViewed(u->effectSet));
  u->motion=trace(7,8090);assert(u->motionStore.save(u->motion));
  for(unsigned i=0;i<10;++i)pull(u->practice,u->bests,8000+10*i);
  assert(u->practiceStore.save(u->practice) && u->bestStore.save(u->bests));
  if(withLog){bool m;assert(saveHistory(*u,u->practice,m) && saveBests(*u,u->bests,m));}
  pull(u->practice,u->bests,8100);
  return u;
}
static DeviceBackup::Contents backup(bool everything=true) {
  DeviceBackup::Contents b;
  for(unsigned i=0;i<4;++i)pull(b.practice,b.bests,12000+10*i);
  b.look.select(Theme::Classic,Effect(101));  // a loaded slot: saved with its older view
  b.settings.sleepMinutes=7;b.settings.brightness=80;b.settings.flipped=false;b.settings.minRpmSteps=6;
  if(everything) {
    b.hasMotion=true;b.motion=trace(3,12030);
    b.hasThemes=true;assert(b.themes.put(2,custom("Backup",0xfb20)));
    const Milestones::Row rows[]={{11000,Effect::Slash},{13000,Effect::Orbit}};
    b.hasMilestones=true;assert(b.milestones.set(Milestones::Mode::Milestones,rows,2)==Milestones::Problem::None);
    b.hasEffectSet=true;b.effectSet.set(EffectSet::DEFAULT_MASK);
  }
  return b;
}
// The device's own records, or the backup's, shown in RAM or loaded at boot.
static bool own(const Unit &u) {
  return u.practice.size()==11 && u.practice.recent()->rpm==8100 && u.bests.rpm[0]==8100 && u.look.theme==Theme::Mint && u.look.effect==Effect(2) &&
    u.settings.sleepMinutes==3 && u.settings.brightness==50 && u.settings.flipped && u.settings.minRpmSteps==12 && u.themes.validMask()==1u && u.milestones.count()==1 &&
    !u.effectSet.mask().has(unsigned(Effect::Orbit)) && u.motion.valid() && u.motion.number==7;
}
static bool restored(const Unit &u) {
  return u.practice.size()==4 && u.practice.recent()->rpm==12030 && u.bests.rpm[0]==12030 && u.look.theme==Theme::Classic && u.look.effect==Effect(101) &&
    u.settings.sleepMinutes==7 && u.settings.brightness==80 && !u.settings.flipped && u.settings.minRpmSteps==6 && u.themes.validMask()==4u && u.milestones.count()==2 &&
    u.effectSet.mask().has(unsigned(Effect::Orbit)) && u.motion.valid() && u.motion.number==3;
}

int main() {
  // Restored: everything, the loaded Best effect with its older view.
  for(bool withLog:{true,false}) {
    auto u=device(withLog);auto d=u->device();auto c=backup();
    const auto r=run(d,c);assert(r.outcome==Outcome::Restored && r.historyMirrored && r.bestsMirrored && u->fx.prepared==1);
    u->boot();assert(restored(*u));
  }
  // A failure at each record puts the device's own records back: RAM shows
  // them, and so does the next boot, with nothing else written in between.
  const struct {Step step;const char *key;bool withLog;} failures[]={
    {Step::Settings,"launchlab-minidisp_flip",true},{Step::Settings,"launchlab-minimin_rpm",true},{Step::Themes,"ll-appearancethemes",true},{Step::Milestones,"ll-appearancempool",true},
    {Step::EffectSet,"ll-appearanceeset",true},{Step::Look,"ll-appearancecfg",true},{Step::Motion,"ll-motionref",true},
    {Step::History,"ll-practicehistory",false},{Step::Bests,"ll-bestsbest",false}};
  for(const auto &f:failures) {
    auto u=device(f.withLog);auto d=u->device();auto c=backup();
    Preferences::failWritePrefixes={f.key};
    const auto r=run(d,c);
    if(r.outcome!=Outcome::RolledBack || r.failed!=f.step || !own(*u)){std::cerr<<"rollback at "<<name(f.step)<<"\n";return 1;}
    // Log and NVS agree after it: the log's records name what NVS holds, so a
    // boot right now (no power-off) loads the device's own records, the pull only in RAM aside.
    if(f.withLog) {
      PracticeHistory logged;Appearance::Bests loggedBests;uint32_t mh=0,mb=0;
      if(!u->log.loadHistory(logged,mh) || !u->log.loadBests(loggedBests,mb) || mh!=u->practiceStore.savedGeneration() || mb!=u->bestStore.savedGeneration()){
        std::cerr<<"log and NVS disagree after rollback at "<<name(f.step)<<"\n";return 1;}
      Preferences::failWritePrefixes.clear();
      const auto nvs=Preferences::data;const auto flash=u->flash.bytes;
      auto again=std::unique_ptr<Unit>(new Unit);again->flash.bytes=flash;again->boot();
      if(again->practice.size()!=10 || again->bests.rpm[0]<8090){std::cerr<<"boot right after rollback at "<<name(f.step)<<"\n";return 1;}
      Preferences::data=nvs;Preferences::failWritePrefixes={f.key};
    }
    // Power-off's mirror (flushForShutdown) saves what RAM shows above
    // whatever the restore wrote; then the next boot loads it.
    Preferences::failWritePrefixes.clear();
    if(!u->practiceStore.save(u->practice) || !u->bestStore.save(u->bests)){std::cerr<<"power-off after rollback at "<<name(f.step)<<"\n";return 1;}
    const auto ram=u->practice.generation();
    u->boot();
    if(!own(*u) || u->practice.generation()!=ram){std::cerr<<"boot after rollback at "<<name(f.step)<<"\n";return 1;}
  }
  // A write that lands and does not read back (a read error after it): the
  // store reports a failed save while storage holds the backup's record. The
  // device's own record goes back over it, so a boot right now loads what
  // RAM shows (the pull only in RAM aside), and so does one after power-off.
  // logWrites: NVS refuses the copy (a full unit) and the log's record, whose
  // commit is the restore's nth flash write (three an append), does not read back.
  const struct {Step step;const char *key;bool withLog;unsigned logWrites;} unread[]={
    {Step::Themes,"ll-appearancethemes",true,0},{Step::Milestones,"ll-appearancempool",true,0},
    {Step::EffectSet,"ll-appearanceeset",true,0},{Step::Look,"ll-appearancecfg",true,0},{Step::Motion,"ll-motionref",true,0},
    {Step::History,"ll-practicehistory",false,0},{Step::Bests,"ll-bestsbest",false,0},
    {Step::History,"ll-practicehistory",true,3},{Step::Bests,"ll-bestsbest",true,6}};
  for(const auto &f:unread) {
    auto u=device(f.withLog);auto d=u->device();auto c=backup();
    if(f.logWrites){Preferences::failWritePrefixes={f.key};u->flash.unreadAfter=f.logWrites;}
    else {Preferences::failReadPrefix=f.key;Preferences::failReads=1;}
    const auto r=run(d,c);
    const bool consumed=f.logWrites?u->flash.unreadAfter==0 && u->flash.failReads==0:Preferences::failReads==0;
    Preferences::failReads=0;Preferences::failReadPrefix.clear();Preferences::failWritePrefixes.clear();u->flash.failReads=u->flash.unreadAfter=0;
    const char *where=f.logWrites?" (log)":"";
    if(!consumed || r.outcome!=Outcome::RolledBack || r.failed!=f.step || !own(*u)){
      std::cerr<<"unread write at "<<name(f.step)<<where<<": outcome "<<unsigned(r.outcome)<<" failed "<<name(r.failed)<<"\n";return 1;}
    const auto nvs=Preferences::data;
    auto again=std::unique_ptr<Unit>(new Unit);again->logOpen=f.withLog;again->flash.bytes=u->flash.bytes;again->boot();
    const bool history=(again->practice.size()==10 && again->practice.recent()->rpm==8090) || (again->practice.size()==11 && again->practice.recent()->rpm==8100);
    if(!history || (again->bests.rpm[0]!=8090 && again->bests.rpm[0]!=8100) || again->look.theme!=Theme::Mint || again->look.effect!=Effect(2) ||
       again->settings.sleepMinutes!=3 || again->themes.validMask()!=1u || again->milestones.count()!=1 ||
       again->effectSet.mask().has(unsigned(Effect::Orbit)) || again->motion.number!=7){
      std::cerr<<"boot right after an unread write at "<<name(f.step)<<where<<" loads the backup's record\n";return 1;}
    Preferences::data=nvs;
    if(!u->practiceStore.save(u->practice) || !u->bestStore.save(u->bests)){std::cerr<<"power-off after an unread write at "<<name(f.step)<<where<<"\n";return 1;}
    u->boot();
    if(!own(*u)){std::cerr<<"boot after an unread write at "<<name(f.step)<<where<<"\n";return 1;}
  }
  // The record cannot be read back at all (every readback fails): the
  // device's own cannot be shown to be back, so the outcome is Partial and
  // the caller restarts on what storage holds.
  {
    auto u=device(true);auto d=u->device();auto c=backup();
    Preferences::failReadPrefix="ll-appearancecfg";Preferences::failReads=1000;
    const auto r=run(d,c);
    Preferences::failReads=0;Preferences::failReadPrefix.clear();
    if(r.outcome!=Outcome::Partial || r.failed!=Step::Look){std::cerr<<"an unverifiable rollback is not Partial: outcome "<<unsigned(r.outcome)<<"\n";return 1;}
  }
  // Rolled back, the device saves normally again: the next pull and the
  // power-off mirror land above everything the restore wrote.
  {
    auto u=device(false);auto d=u->device();auto c=backup();
    Preferences::failWritePrefixes={"ll-bestsbest"};assert(run(d,c).outcome==Outcome::RolledBack);Preferences::failWritePrefixes.clear();
    assert(!u->practiceStore.pending(u->practice));pull(u->practice,u->bests,8200);
    assert(u->practiceStore.save(u->practice) && u->bestStore.save(u->bests));
    u->boot();assert(u->practice.size()==12 && u->practice.recent()->rpm==8200 && u->bests.rpm[0]==8200);
  }
  // A full NVS with the log open: the history and bests mirrors are refused,
  // the log takes them, and boot picks the log.
  {
    auto u=device(true);auto d=u->device();auto c=backup();
    Preferences::failWritePrefixes={"ll-practicehistory","ll-bestsbest"};
    const auto r=run(d,c);assert(r.outcome==Outcome::Restored && !r.historyMirrored && !r.bestsMirrored);
    u->boot();assert(restored(*u) && u->practiceStore.savedGeneration()==10);
  }
  // NVS refusing every history and bests copy from the start (it never held
  // one): the log's records say NOT_MIRRORED, before and after the restore,
  // so boot loads the restored records from the log.
  {
    Preferences::data.clear();Preferences::failWritePrefixes.clear();
    auto u=std::unique_ptr<Unit>(new Unit);u->boot();
    Preferences::failWritePrefixes={"ll-practicehistory","ll-bestsbest"};
    for(unsigned i=0;i<3;++i)pull(u->practice,u->bests,8000+10*i);
    {bool m=true;assert(saveHistory(*u,u->practice,m) && !m && saveBests(*u,u->bests,m) && !m);}
    auto d=u->device();auto c=backup();
    const auto r=run(d,c);assert(r.outcome==Outcome::Restored && !r.historyMirrored && !r.bestsMirrored);
    PracticeHistory logged;uint32_t mh=1;assert(u->log.loadHistory(logged,mh) && mh==HistoryLog::NOT_MIRRORED);
    u->boot();assert(restored(*u) && !u->practiceStore.found());
  }
  // The effects store cannot take the backup's effects: nothing is written.
  {
    auto u=device(true);auto d=u->device();auto c=backup();c.loadedCount=1;
    u->fx.ready=false;const auto nvs=Preferences::data;const auto flash=u->flash.bytes;
    const auto r=run(d,c);assert(r.outcome==Outcome::Refused && Preferences::data==nvs && u->flash.bytes==flash && !u->fx.replaced);
    assert(own(*u));
  }
  // The effects fail after every record: the records go back, the effects
  // cannot (Partial: the caller restarts on what storage holds).
  {
    auto u=device(true);auto d=u->device();auto c=backup();c.loadedCount=1;
    u->fx.fail=true;const auto r=run(d,c);assert(r.outcome==Outcome::Partial && r.failed==Step::Effects && u->fx.replaced==1);
    u->boot();assert(own(*u));
  }
  // A failure the device cannot put back (storage refuses every write once
  // the effects fail): Partial, and the next boot loads what storage holds,
  // the backup's records.
  {
    auto u=device(true);auto d=u->device();auto c=backup();c.loadedCount=1;
    u->fx.fail=true;u->fx.dies=&u->flash;
    const auto r=run(d,c);assert(r.outcome==Outcome::Partial && r.failed==Step::Effects);
    Preferences::failWrite=false;u->flash.restore();u->boot();assert(restored(*u));
  }
  // An older backup (format 1: no themes, table, set or reference) leaves
  // the device's own.
  {
    auto u=device(true);auto d=u->device();auto c=backup(false);
    assert(run(d,c).outcome==Outcome::Restored);u->boot();
    assert(u->practice.size()==4 && u->look.theme==Theme::Classic && u->themes.validMask()==1u && u->milestones.count()==1 &&
           !u->effectSet.mask().has(unsigned(Effect::Orbit)) && u->motion.number==7);
  }
  // Restored records supersede what NVS holds even where RAM is behind it.
  {
    auto u=device(true);
    Appearance::Config ahead=u->look;ahead.revision+=9;assert(u->lookStore.save(ahead));
    auto d=u->device();auto c=backup();assert(run(d,c).outcome==Outcome::Restored);
    u->boot();assert(restored(*u));
  }
  std::cout<<"PASS: restore writes every record or puts the device's own back (at each of 8 records, and over a write that landed unread), effects last and refused before any write, "
             "full NVS restores through the log, older backups keep what they lack, restored records outrank NVS\n";
}
