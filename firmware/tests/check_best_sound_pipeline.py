#!/usr/bin/env python3
"""Exercise production Random selection, recap cues and speaker preparation,
and hold every effect's sound within 20 ms of its frames (SoundSync)."""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"(?:LOOP_HELPER )?static [^\n{;]+\b" + name + r"\([^;]*?\)\s*\{", source)
    if not match:
        raise RuntimeError(f"Production function missing: {name}")
    opening = source.index("{", match.start())
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[match.start():cursor]


parser = argparse.ArgumentParser()
parser.add_argument("--sketch", type=Path, help="Optional pre-edit sketch for a regression check")
options = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (options.sketch or root / "firmware/LaunchLabMini/LaunchLabMini.ino").read_text()
speaker = (root / "firmware/LaunchLabMini/best_effect_speaker.h").read_text()
speaker = speaker[speaker.index("class BestEffectSpeaker {"):]
cue_start = source.index("  if(!tournamentMode && !diagnostics && !historyPage && launchFeedback.shown() && !motionDemo && !motionPending)")
cue_end = source.index("  launchFeedback.replayPresented(", cue_start)
cue = source[cue_start:cue_end]
deadline = re.search(r"static constexpr uint32_t SOUND_LATE_MS=\d+;", source)[0]
# Reproduce normal capture/sound/draw order. The locked wipe branch renders
# only its confirmation/recovery screen and returns before this path.
loop = source[source.index("void loop() {"):]
capture_at=loop.index("finishMotion();")
assert capture_at < loop.index("tickSound(s);") < loop.index("draw(s);",capture_at)

prefix = r'''
#include "appearance_store.h"
#include "milestones.h"
#include "analog_tachometer.h"
#include "best_effect_sound.h"
#include "sound_sync.h"
#include "controls.h"
#include "accel_range.h"
#include "launch_feedback.h"
#include "launch_motion.h"
#include "practice_history.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
#define LOOP_HELPER
static uint64_t hostUs=5000000;
static uint32_t millis(){return uint32_t(hostUs/1000);}
static int64_t esp_timer_get_time(){return hostUs;}
// A fixed peripheral RNG makes the previous regression deterministic:
// Orbit -> Flames -> Orbit if a non-PB consumes the middle pick.
static unsigned randomCalls=0;
static uint32_t esp_random(){++randomCalls;return 0;}
static constexpr unsigned MALLOC_CAP_SPIRAM=1;
static std::vector<void *> allocations;
static void *heap_caps_malloc(size_t bytes,unsigned){auto p=std::malloc(bytes);allocations.push_back(p);return p;}
static void heap_caps_free(void *p){for(auto &a:allocations)if(a==p)a=nullptr;std::free(p);}
static unsigned liveAllocations(){unsigned n=0;for(auto *a:allocations)n+=a!=nullptr;return n;}
struct StickS3MotionAcquisition {
  bool lock(){return true;}void unlock(){}
};
// The speaker's timing: starting the codec takes beginUs of the loop; a clip
// is heard from startedUs plus the output latency its DMA ring imposes.
struct SpeakerPeripheral {
  struct Config {int task_pinned_core=0;bool stereo=true;} settings;
  bool powered=false,playing=false;
  unsigned plays=0,stops=0;
  std::vector<int16_t> clip;
  uint64_t beginUs=28000,startedUs=0;uint32_t latencyUs=0;size_t remaining=0;
  Config config(){return settings;}void config(Config c){settings=c;}
  bool isEnabled(){return true;}
  bool begin(){hostUs+=beginUs;powered=true;return true;}
  void end(){powered=false;}
  void setVolume(unsigned){}
  bool playRaw(const int16_t *data,size_t count,unsigned,bool,unsigned,unsigned,bool){
    latencyUs=SoundSync::outputUs(playing);startedUs=hostUs;remaining=count;
    clip.assign(data,data+count);playing=true;++plays;return true;}
  bool isPlaying(){return playing;}
  bool isPlaying(uint8_t){return playing;}
  void stop(){playing=false;++stops;}
  void stop(uint8_t){stop();}
  void setChannelVolume(uint8_t,uint8_t){}
};
static struct {SpeakerPeripheral Speaker;} M5;
// The music streamer's stand-ins (best_effect_speaker.h defines them for
// the device above the class this harness takes): a host lock, no task.
#include "music_stream.h"
#include <mutex>
struct HostLock {std::mutex m;struct Hold {std::mutex &m;explicit Hold(HostLock &l):m(l.m){m.lock();}~Hold(){m.unlock();}};};
using Music=MusicStream<HostLock>;
using TaskHandle_t=void *;
static void xTaskNotifyGive(TaskHandle_t){}
// The speaker's music channel: holds up to two blocks until the test plays them.
struct MusicChannel:MusicOutput {
  static inline unsigned held=0,queuedBlocks=0,stops=0;static inline int lastVolume=-1;
  unsigned queued() override{return held;}
  bool queue(const int16_t *,size_t) override{if(held>=2)return false;++held;++queuedBlocks;return true;}
  void volume(uint8_t v) override{lastVolume=v;}
  void stop() override{held=0;++stops;}
};
#include "fx_compiler.h"

struct IdleFake {bool ensureAwake(){return true;}};
static IdleFake &pm1Idle(){static IdleFake idle;return idle;}
static struct {template<class... T>void printf(const char*,T...){}void println(const char*){}} Serial;
'''
state = r'''
static StickS3MotionAcquisition imuAcquisition;
static BestEffectSpeaker soundPlayer(imuAcquisition);
static Appearance::Config appearance;
static Appearance::Bests personalBests;
static AppearanceStore<Appearance::Bests> bestStore;
static Appearance::Celebration bestEffect,editorEffect;
static Milestones::Config milestones;
static Appearance::Effect milestoneNext[Milestones::MAX_ROWS];static Milestones::Config milestonePicked;static bool milestonePicksReady=false;
static EffectSet::Config effectSet;
static Appearance::Effect upcomingEffect=Appearance::Effect::Off;
static Controls::State controls;
static PracticeHistory practice;
static LaunchMotion::Trace latestMotion;
static LaunchFeedback launchFeedback;
static bool dirty=false,motionPending=false,celebrationSound=false,soundPreview=false;
static constexpr uint64_t MOTION_CAPTURE_RETRY_US=3000000;static uint32_t motionCaptureRetries=0;
static bool tournamentMode=false,historyPage=false,diagnostics=false,motionDemo=false,demo=false;
static uint32_t lastAcceptedAt=0,historyPendingSince=0,motionDrops=0,maxEffectUs=0,maxDrawUs=0;
static uint64_t motionStart=0,motionEnd=0;
static struct {bool active=false,celebration=false;Appearance::Effect effect=Appearance::Effect::Off;Appearance::Sound level=Appearance::Sound::Off;} soundRequest;
static int64_t effectReadUs=0;static uint32_t presentLagUs=15000;
static int32_t soundShiftMs=0;static uint32_t soundSkipped=0;
struct Snapshot {
  AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;
  unsigned sensorProfile=0,estimator=0;
  bool valid=true;
  float resultRpm=6100;
  uint64_t burstStartHostUs=0,burstEndHostUs=0;
};
static void cancelSettingEdit(){controls.page=Controls::Page::Settings;}
static void syncNavigation(){}
static Appearance::Effect celebrationEffect();
static void startSound(bool late);
'''
tests = r'''
static void reset(Appearance::Effect effect=Appearance::Effect::Random) {
  M5.Speaker={};soundPlayer.~BestEffectSpeaker();new(&soundPlayer) BestEffectSpeaker(imuAcquisition);soundPlayer.configure();
  appearance={};appearance.effect=effect;appearance.sound=Appearance::Sound::Low;
  personalBests={};bestStore={};bestEffect={};editorEffect={};upcomingEffect=Appearance::Effect::Off;milestones={};effectSet={};
  practice={};latestMotion={};launchFeedback={};controls={};soundRequest={};
  dirty=motionPending=celebrationSound=soundPreview=tournamentMode=historyPage=diagnostics=motionDemo=demo=false;
  hostUs=5000000;randomCalls=0;lastAcceptedAt=historyPendingSince=motionDrops=0;
  Preferences::data.clear();Preferences::zeroLengthKeys.clear();
  assert(bestStore.begin(personalBests,"sound-test","best0","best1"));
  assert(!personalBests.observe(0,0,true,6000));
}
static void accepted(float rpm) {
  Snapshot event;event.resultRpm=rpm;event.burstEndHostUs=hostUs;event.burstStartHostUs=hostUs-75000;
  assert(practice.accept(practice.size()+1,true,rpm,millis()));
  prepareMotion(event);
}
static void captured(){motionPending=false;bestEffect.captured(latestMotion.number);}
static void prepared(){M5.Speaker.playing=false;tickSound(Snapshot{});assert(soundPlayer.prepare(soundTarget()));}
// The owner, 2026-10-22: "the sound seems to play a little late". The sound's
// sample heard as each frame of the effect reaches the glass, against that
// frame's own elapsed time: within 20 ms, all the way through. A frame whose
// clock reads e is seen present lag later; the clip's first sample (the
// effect's sample `skipped`) is heard its output latency after playRaw.
static double worstOffsetMs=0;
static void checkSync(const Appearance::Celebration &shown,Appearance::Effect effect) {
  const size_t skipped=BestEffectSound::samples(effect)-M5.Speaker.remaining;
  const double heardMs=(M5.Speaker.startedUs+M5.Speaker.latencyUs)/1000.0;
  for(uint32_t e=0;e<Appearance::duration(effect);e+=50) {
    const double seenMs=double(shown.startedAt())+e+presentLagUs/1000.0;
    const double sampleMs=skipped*1000.0/BestEffectSound::RATE+(seenMs-heardMs);
    if(sampleMs<0 || sampleMs*BestEffectSound::RATE/1000>=BestEffectSound::samples(effect))continue;  // silent then
    worstOffsetMs=std::max(worstOffsetMs,std::fabs(sampleMs-e));
    assert(std::fabs(sampleMs-e)<=20);
  }
}
static void checkClip(Appearance::Effect effect) {
  std::vector<int16_t> expected(BestEffectSound::samples(effect));
  BestEffectSound::render(effect,expected.data(),expected.size());
  assert(M5.Speaker.clip==expected);
}
int main() {
  // The speaker task runs on the UI core and mixes mono: stereo mixed two
  // identical channels during effect frames (0.18.1).
  reset();assert(M5.Speaker.settings.task_pinned_core==1 && !M5.Speaker.settings.stereo);
  prepared();assert(soundTarget()==Appearance::Effect::Orbit);
  accepted(6100);captured();tickSound(Snapshot{});presentFrame();
  assert(M5.Speaker.plays==1);checkClip(Appearance::Effect::Orbit);
  const auto next=upcomingEffect;const unsigned calls=randomCalls;
  accepted(5900);captured();assert(!bestEffect.badge() && upcomingEffect==next && randomCalls==calls);
  prepared();accepted(6200);captured();tickSound(Snapshot{});presentFrame();
  assert(bestEffect.effect()!=Appearance::Effect::Orbit && M5.Speaker.plays==2);checkClip(bestEffect.effect());
  // A hidden PB updates the baseline but cannot consume the upcoming effect.
  for(unsigned hidden=0;hidden<3;++hidden) {
    reset();prepared();const auto kept=upcomingEffect;const auto rng=randomCalls;
    tournamentMode=hidden==0;historyPage=hidden==1;diagnostics=hidden==2;
    accepted(6100);captured();assert(!bestEffect.badge() && upcomingEffect==kept && randomCalls==rng);
  }
  // Cancelling before presentation also preserves the next pick and clip.
  reset();prepared();accepted(6100);captured();const auto kept=upcomingEffect;
  bestEffect.clear();launchFeedback.clear();tickSound(Snapshot{});
  assert(upcomingEffect==kept && M5.Speaker.plays==0);
  accepted(6200);captured();tickSound(Snapshot{});presentFrame();
  assert(bestEffect.effect()==kept && M5.Speaker.plays==1);checkClip(kept);
  // Every explicit USB preview must retain its rendered clip through the
  // idle pass before the first frame, even when the saved choice is Orbit.
  for(unsigned e=0;e<Appearance::DRAWN;++e) {
    reset(Appearance::Effect::Orbit);latestMotion.number=1;
    const auto effect=Appearance::drawnEffect(e);previewBestEffect(effect);
    assert(soundPlayer.prepared(effect));tickSound(Snapshot{});
    assert(soundPlayer.prepared(effect));presentFrame();
    assert(M5.Speaker.plays==1);checkClip(effect);
    tickSound(Snapshot{AnalogTachometer::Phase::Launch});
    assert(!soundPlayer.playing() && soundPlayer.powered());
    tickSound(Snapshot{});assert(!soundPlayer.powered());
  }
  // A queued Random recap keeps its prepared clip while capture is pending,
  // including wrap of the effect's presentation clock.
  reset();prepared();hostUs=uint64_t(0xfffffff0u)*1000;accepted(6100);
  tickSound(Snapshot{});assert(soundPlayer.prepared(Appearance::Effect::Orbit));
  captured();tickSound(Snapshot{});presentFrame();checkClip(Appearance::Effect::Orbit);
  // Measured from the clock's start, which moves to where the sound is heard.
  const uint32_t from=millis()>bestEffect.startedAt()?millis():bestEffect.startedAt();
  hostUs=uint64_t(from)*1000+56000;assert(bestEffect.elapsed(millis())==uint32_t(millis()-bestEffect.startedAt()) && M5.Speaker.plays==1);
  // The existing Best effect editor previews every concrete draft through
  // its actual helper and first-frame cue, including an unprepared clip.
  for(unsigned e=0;e<Appearance::DRAWN;++e) {
    reset(Appearance::Effect::Orbit);controls.page=Controls::Page::Edit;controls.setting=unsigned(Controls::Setting::BestEffect);controls.draft=unsigned(Appearance::drawnEffect(e));
    previewEffect();assert(editorEffect.badge() && editorEffect.effect()==Appearance::drawnEffect(e));
    // The preview holds its first frame until its sound is rendered, then both start.
    tickSound(Snapshot{});presentFrame();
    for(unsigned i=0;!editorEffect.started() && i<200;++i){hostUs+=10000;tickSound(Snapshot{});presentFrame();}
    assert(M5.Speaker.plays==1 && !soundRequest.active);checkClip(Appearance::drawnEffect(e));
    controls.page=Controls::Page::Settings;tickSound(Snapshot{});
    assert(!editorEffect.badge() && !soundPlayer.playing() && !soundPlayer.powered());
  }
  reset();controls.page=Controls::Page::Edit;controls.setting=unsigned(Controls::Setting::BestEffect);controls.draft=unsigned(Appearance::Effect::Random);
  previewEffect();const auto editorPick=editorEffect.effect();previewEffect();
  assert(editorEffect.effect()!=editorPick);
  controls.draft=unsigned(Appearance::Effect::Off);previewEffect();presentFrame();
  assert(!editorEffect.badge() && !soundRequest.active && M5.Speaker.plays==0);
  // Deferred editor requests render in idle slices, survive the celebration
  // deadline, and stop when the editor closes.
  reset(Appearance::Effect::Orbit);controls.page=Controls::Page::Edit;controls.setting=unsigned(Controls::Setting::Sound);
  requestSound(Appearance::Effect::Sparkles,Appearance::Sound::Low,false);
  const auto target=upcomingEffect;tickSound(Snapshot{AnalogTachometer::Phase::Launch});
  assert(!soundRequest.active && !soundPlayer.prepared(Appearance::Effect::Sparkles) && upcomingEffect==target);
  requestSound(Appearance::Effect::Sparkles,Appearance::Sound::Low,false);
  for(unsigned i=0;soundRequest.active && i<200;++i){hostUs+=10000;tickSound(Snapshot{});}
  assert(M5.Speaker.plays==1 && !soundRequest.active);checkClip(Appearance::Effect::Sparkles);
  controls.page=Controls::Page::Settings;tickSound(Snapshot{});
  assert(!soundPlayer.playing() && !soundPlayer.powered());
  // The actual Sound editor helper uses the draft level and saved effect.
  reset(Appearance::Effect::Lightning);controls.page=Controls::Page::Edit;controls.setting=unsigned(Controls::Setting::Sound);
  controls.draft=unsigned(Appearance::Sound::High);previewSound();
  assert(soundRequest.active && soundRequest.level==Appearance::Sound::High);
  for(unsigned i=0;soundRequest.active && i<200;++i){hostUs+=10000;tickSound(Snapshot{});}
  assert(M5.Speaker.plays==1);checkClip(Appearance::Effect::Lightning);
  controls.draft=unsigned(Appearance::Sound::Off);previewSound();
  assert(!soundPlayer.playing() && !soundRequest.active);
  tickSound(Snapshot{});assert(!soundPlayer.powered());
  // Late celebration requests are discarded, rather than playing out of sync.
  reset();bestEffect.queue(true,1,Appearance::Effect::Sparkles);bestEffect.captured(1);bestEffect.presented(millis());
  requestSound(Appearance::Effect::Sparkles,Appearance::Sound::Low,true);
  hostUs+=uint64_t(SOUND_LATE_MS+1)*1000;tickSound(Snapshot{});
  assert(!soundRequest.active && M5.Speaker.plays==0);
  reset();appearance.sound=Appearance::Sound::Off;accepted(6100);captured();presentFrame();
  assert(upcomingEffect!=bestEffect.effect() && M5.Speaker.plays==0);
  // Milestones: a pull's effect is known only when it ends, so idle passes
  // render every effect the table can play; the milestone's sound then starts
  // with its first frame, and Random's next pick is left for the next best.
  {
    using Appearance::Effect;
    reset(Appearance::Effect::Random);assert(milestones.select(Milestones::Mode::Both));
    unsigned passes=0;
    auto ready=[&]{for(unsigned i=0;i<milestones.count();++i)if(!soundPlayer.prepared(milestonePicks()[i]))return false;return soundPlayer.prepared(soundTarget());};
    while(!ready() && passes<2000){tickSound(Snapshot{});++passes;}
    assert(ready() && passes<2000);
    const auto pick=upcomingEffect;const unsigned rng=randomCalls;
    accepted(15500);captured();tickSound(Snapshot{});presentFrame();
    assert(bestEffect.effect()==Effect::Slash && bestEffect.milestone()==15000 && M5.Speaker.plays==1);checkClip(Effect::Slash);
    // The milestone's row picks its next effect itself; Random's pick is untouched.
    assert(upcomingEffect==pick);(void)rng;
    // Every table effect plays at once from its own buffer, one after another.
    const Effect expected[]={Effect::Sparkles,Effect::Orbit,Effect::Flames,Effect::Shockwave,Effect::Lightning};
    const float rpms[]={9100,11200,12100,13300,14800};
    for(unsigned i=0;i<5;++i) {
      M5.Speaker.playing=false;tickSound(Snapshot{});
      accepted(rpms[i]);captured();tickSound(Snapshot{});presentFrame();
      assert(bestEffect.effect()==expected[i] && M5.Speaker.plays==2+i);checkClip(expected[i]);
    }
    // With New best only, idle passes leave the milestone effects unrendered.
    reset(Appearance::Effect::Orbit);assert(milestones.select(Milestones::Mode::Best));
    for(unsigned i=0;i<2000;++i)tickSound(Snapshot{});
    assert(soundPlayer.prepared(Effect::Orbit) && !soundPlayer.prepared(Effect::Slash) && !soundPlayer.prepared(Effect::Sparkles));
  }
  // The effect set: Random's picks, the first one, each next pick after a
  // presented best and the editor's Random preview stay inside it, never
  // repeating while another is offered; one offered effect always plays.
  {
    using Appearance::Effect;using EffectSet::flag;
    const EffectSet::Mask mine=flag(Effect::Slash)|flag(Effect::Crimson)|flag(Effect::Ravage);
    reset();assert(effectSet.set(mine)==EffectSet::Problem::None);
    prepared();assert(soundTarget()==Effect::Slash);
    Effect previous=Effect::Off;
    for(unsigned i=0;i<6;++i) {
      prepared();accepted(6100+100*i);captured();tickSound(Snapshot{});presentFrame();
      assert(bestEffect.badge() && effectSet.offers(bestEffect.effect()) && bestEffect.effect()!=previous && M5.Speaker.plays==1+i);
      checkClip(bestEffect.effect());previous=bestEffect.effect();
      assert(effectSet.offers(upcomingEffect) && upcomingEffect!=previous);
      M5.Speaker.playing=false;
    }
    controls.page=Controls::Page::Edit;controls.setting=unsigned(Controls::Setting::BestEffect);controls.draft=unsigned(Effect::Random);
    for(unsigned i=0;i<4;++i){previewEffect();assert(effectSet.offers(editorEffect.effect()));}
    reset();assert(effectSet.set(flag(Effect::Bladework))==EffectSet::Problem::None);
    for(unsigned i=0;i<3;++i){prepared();accepted(6100+100*i);captured();tickSound(Snapshot{});presentFrame();
      assert(bestEffect.effect()==Effect::Bladework && upcomingEffect==Effect::Bladework);M5.Speaker.playing=false;}
    // A saved effect outside the set still plays.
    reset(Effect::Bloom);assert(effectSet.set(mine)==EffectSet::Problem::None);
    prepared();accepted(6100);captured();tickSound(Snapshot{});presentFrame();assert(bestEffect.effect()==Effect::Bloom);
  }
  // At most MAX_BUFFERS sounds are kept: preparing another frees the least
  // recently used, never the one playing, and a freed sound renders again.
  {
    using Appearance::drawnEffect;
    reset(Appearance::Effect::Orbit);const unsigned before=liveAllocations();
    assert(soundPlayer.prepare(drawnEffect(0)) && soundPlayer.play(drawnEffect(0),Appearance::Sound::Low));
    for(unsigned e=1;e<Appearance::DRAWN;++e)assert(soundPlayer.prepare(drawnEffect(e)));
    static_assert(Appearance::DRAWN>BestEffectSpeaker::MAX_BUFFERS,"the cache is smaller than the effects");
    assert(liveAllocations()-before==BestEffectSpeaker::MAX_BUFFERS);
    assert(soundPlayer.prepared(drawnEffect(0)) && !soundPlayer.prepared(drawnEffect(1)));
    for(unsigned e=Appearance::DRAWN-BestEffectSpeaker::MAX_BUFFERS+1;e<Appearance::DRAWN;++e)assert(soundPlayer.prepared(drawnEffect(e)));
    M5.Speaker.playing=false;assert(soundPlayer.prepare(drawnEffect(1)) && soundPlayer.play(drawnEffect(1),Appearance::Sound::Low));
    checkClip(drawnEffect(1));assert(liveAllocations()-before==BestEffectSpeaker::MAX_BUFFERS && !soundPlayer.prepared(drawnEffect(0)));
  }
  // An effect pool: idle passes render the row's next pick only; a pull
  // reaching the row plays that pick, and the row's next pick is another.
  {
    using Appearance::Effect;
    reset(Effect::Orbit);
    const Milestones::Row rows[]={{9000,Effect::Sparkles},{12000,uint64_t(1)<<unsigned(Effect::Tidal)|uint64_t(1)<<unsigned(Effect::Frost)|uint64_t(1)<<unsigned(Effect::Bloom)}};
    assert(milestones.set(Milestones::Mode::Milestones,rows,2)==Milestones::Problem::None);milestonePicksReady=false;
    Effect previous=Effect::Off;
    for(unsigned pull=0;pull<4;++pull) {
      for(unsigned i=0;i<4000 && !soundPlayer.prepared(milestonePicks()[1]);++i){M5.Speaker.playing=false;tickSound(Snapshot{});}
      const Effect next=milestonePicks()[1];
      assert(rows[1].has(next) && next!=previous && soundPlayer.prepared(next));
      M5.Speaker.playing=false;accepted(12500+pull);captured();tickSound(Snapshot{});presentFrame();
      assert(bestEffect.effect()==next && bestEffect.milestone()==12000);checkClip(next);
      previous=next;
    }
  }
  // Sync: every effect's sound meets its frames, for a celebration (the codec
  // off, so it starts first), a USB preview, the Best effect editor's
  // preview after another, a celebration cutting into a playing sound (the
  // whole DMA ring ahead of it), and a sound ready only after its effect
  // started.
  for(unsigned e=0;e<Appearance::DRAWN;++e) {
    using Appearance::drawnEffect;const auto effect=drawnEffect(e);
    reset(effect);prepared();accepted(6100);captured();tickSound(Snapshot{});presentFrame();
    assert(bestEffect.effect()==effect && M5.Speaker.plays==1 && soundPlayer.outputUs()==SoundSync::outputUs(false));
    checkSync(bestEffect,effect);
    // The clock starts where the sound is heard: after the codec's start,
    // not at the first frame before it.
    assert(bestEffect.startedAt()>=millis()-5 && millis()-bestEffect.startedAt()<=5);
    reset(Appearance::Effect::Orbit);latestMotion.number=1;previewBestEffect(effect);presentFrame();checkSync(bestEffect,effect);
    // The Best effect editor: a new preview silences the one still playing at
    // once, holds its first frame without sound while its own renders (in one
    // pass), then starts with it from a fresh codec.
    reset(Appearance::Effect::Orbit);controls.page=Controls::Page::Edit;controls.setting=unsigned(Controls::Setting::BestEffect);
    controls.draft=unsigned(drawnEffect((e+1)%Appearance::DRAWN));previewEffect();
    for(unsigned i=0;i<4 && !editorEffect.started();++i){tickSound(Snapshot{});presentFrame();}
    assert(editorEffect.started() && M5.Speaker.playing);hostUs+=400000;
    controls.draft=unsigned(effect);previewEffect();assert(!M5.Speaker.playing);
    presentFrame();assert(!editorEffect.started() && M5.Speaker.plays==1);
    tickSound(Snapshot{});assert(soundPlayer.prepared(effect));presentFrame();
    assert(editorEffect.started() && M5.Speaker.plays==2 && soundPlayer.outputUs()==SoundSync::outputUs(false));
    checkSync(editorEffect,effect);
    // A cue cutting into a sound still playing (a pull or a new preview
    // stops sound first, so only a direct start leaves one playing): the
    // whole DMA ring is ahead of it, and the clock allows for that.
    reset(effect);prepared();const auto other=drawnEffect((e+1)%Appearance::DRAWN);
    assert(soundPlayer.prepare(other) && soundPlayer.play(other,Appearance::Sound::Low) && M5.Speaker.playing);
    accepted(6100);captured();presentFrame();
    assert(bestEffect.effect()==effect && M5.Speaker.plays==2 && soundPlayer.outputUs()==SoundSync::outputUs(true));
    checkSync(bestEffect,effect);
    // Late: the celebration starts before its sound is rendered, which then
    // joins partway in, at the sample of the frame on the glass.
    reset(effect);accepted(6100);captured();presentFrame();assert(bestEffect.started() && M5.Speaker.plays==0);
    for(unsigned i=0;i<4000 && soundRequest.active;++i){hostUs+=1000;tickSound(Snapshot{});}
    if(M5.Speaker.plays) {
      assert(M5.Speaker.remaining<BestEffectSound::samples(effect));checkSync(bestEffect,effect);
    }
  }
  // A loaded effect with a sound of its own renders it into its slot's
  // buffer, a slice at a time, and plays it like a built-in's, skip
  // included; a replaced file (another key) renders again; one without a
  // sound of its own plays its stock fallback's.
  {
    using Appearance::Effect;
    struct FakeLoaded:BestEffectSpeaker::LoadedSounds {
      uint32_t key=1;size_t samples=4000,at=0;bool has=true;unsigned begins=0;
      bool sound(unsigned e,uint32_t &k,size_t &n) override{if(e!=96 || !has)return false;k=key;n=samples;return true;}
      bool begin(unsigned) override{++begins;at=0;return true;}
      size_t run(int16_t *out,size_t n) override{for(size_t i=0;i<n;++i)out[i]=int16_t((at+i)*7+key);at+=n;return n;}
      bool song(unsigned) override{return false;}
      bool readSong(unsigned,const uint8_t *&,size_t &) override{return false;}
      std::vector<int16_t> clip(size_t skip)const{std::vector<int16_t> c;for(size_t i=skip;i<samples;++i)c.push_back(int16_t(i*7+key));return c;}
    } fake;
    Appearance::loadedHooks().fallback=[](unsigned){return unsigned(Effect::Sparkles);};
    const Effect mine=Effect(96);
    reset(Effect::Orbit);soundPlayer.attach(&fake);
    unsigned slices=0;while(!soundPlayer.prepare(mine,1000))++slices;
    assert(slices==3 && fake.begins==1 && soundPlayer.prepared(mine) && !soundPlayer.prepared(Effect::Sparkles));
    assert(soundPlayer.play(mine,Appearance::Sound::Low,100) && M5.Speaker.clip==fake.clip(100));
    assert(!soundPlayer.play(mine,Appearance::Sound::Low,fake.samples));
    // Replaced while playing: it renders again once the old sound ends.
    fake.key=2;assert(!soundPlayer.prepared(mine) && !soundPlayer.prepare(mine) && fake.begins==1);
    M5.Speaker.playing=false;assert(soundPlayer.prepare(mine) && fake.begins==2);
    assert(soundPlayer.play(mine,Appearance::Sound::Low) && M5.Speaker.clip==fake.clip(0));
    // No sound of its own (or no store): its fallback's.
    fake.has=false;M5.Speaker.playing=false;assert(!soundPlayer.prepared(mine));
    while(!soundPlayer.prepare(mine,4000)){}
    assert(soundPlayer.prepared(Effect::Sparkles) && soundPlayer.play(mine,Appearance::Sound::Low));checkClip(Effect::Sparkles);
    soundPlayer.attach(nullptr);fake.has=true;assert(soundPlayer.prepared(mine));
    Appearance::loadedHooks()=Appearance::LoadedHooks();
  }
  // A song (engine level 2) is streamed: always ready, it plays on past
  // its celebration's frames, and a pull fades it to a stop within 20 ms
  // through the production tickSound.
  {
    using Appearance::Effect;
    const auto compiled=FxCompiler::compile("effect \"Song\"\nuid 0x4c4c4d5500000099\nduration 3000\nreadout 900\nframe { x = W }\n"
      "voice lead = pulse(duty: .25, decay: 3, gain: .3)\nsong(tempo: 120, rows: 4) {\n pattern a {\n  lead: \"c5 . e5 . g5 . e5 .\"\n }\n order a a a a\n}\n");
    assert(compiled.ok);
    struct SongLoaded:BestEffectSpeaker::LoadedSounds {
      std::vector<uint8_t> file;
      bool sound(unsigned,uint32_t &,size_t &) override{return false;}
      bool begin(unsigned) override{return false;}
      size_t run(int16_t *,size_t) override{return 0;}
      bool song(unsigned e) override{return e==96;}
      bool readSong(unsigned e,const uint8_t *&f,size_t &n) override{if(e!=96)return false;f=file.data();n=file.size();return true;}
    } songs;
    songs.file=compiled.bytes;
    static auto space=std::make_unique<Music::Space>();static Music stream(space.get());
    const Effect mine=Effect(96);
    reset(Effect::Orbit);soundPlayer.attach(&songs);soundPlayer.attachMusic(&stream,nullptr);
    assert(soundPlayer.prepared(mine) && soundPlayer.prepare(mine));
    assert(soundPlayer.play(mine,Appearance::Sound::Low) && soundPlayer.streaming() && soundPlayer.playing());
    // Play it a while: the music task renders, the loop feeds and the channel plays.
    auto run=[&](unsigned passes,Snapshot s){for(unsigned i=0;i<passes;++i){stream.step([]{return 0u;});hostUs+=4000;if(MusicChannel::held && i%8==7)--MusicChannel::held;tickSound(s);}};
    run(50,Snapshot{});
    assert(soundPlayer.streaming() && MusicChannel::queuedBlocks>0);
    // Its celebration's frames have ended: a song keeps playing.
    celebrationSound=true;run(10,Snapshot{});assert(soundPlayer.streaming());
    // A pull starts: faded within 20 ms (five 4 ms passes and the stop).
    const unsigned stops=MusicChannel::stops;
    run(1,Snapshot{AnalogTachometer::Phase::Launch});assert(stream.fading() && soundPlayer.streaming());
    run(6,Snapshot{AnalogTachometer::Phase::Launch});
    assert(!soundPlayer.streaming() && MusicChannel::stops==stops+1 && MusicChannel::lastVolume<255);
    // Without the streamer a song effect sounds as its stock fallback.
    soundPlayer.attachMusic(nullptr,nullptr);M5.Speaker.playing=false;
    Appearance::loadedHooks().fallback=[](unsigned){return unsigned(Effect::Sparkles);};
    assert(!soundPlayer.prepared(mine));while(!soundPlayer.prepare(mine,4000)){}
    assert(soundPlayer.play(mine,Appearance::Sound::Low));checkClip(Effect::Sparkles);
    soundPlayer.attach(nullptr);Appearance::loadedHooks()=Appearance::LoadedHooks();
  }
  // These buffers live for the firmware lifetime; release fake allocations
  // here so the host harness can also use leak detection.
  for(auto p:allocations)if(p)std::free(p);
  std::cout<<"PASS: every effect's sound within "<<int(std::ceil(worstOffsetMs))<<" ms of its frames (celebration, USB preview, editor preview after another, celebration into a playing sound, late start); production Random/PB/hidden/cancel selection, first-frame cues, six USB preview sounds, milestone sounds rendered ahead, a bounded least-recently-used sound cache, actual Best effect/Sound editor helpers, real sliced synth/speaker buffer, pull/close cancellation, late deadline and timer wrap, a streamed song outliving its frames and fading on a pull\n";
}
'''
names = ["milestonePicks", "nextMilestonePick", "upcoming", "celebrationEffect", "soundTarget", "requestSound", "startSound", "tickSound", "prepareMotion", "previewBestEffect", "previewEffect", "previewSound"]
code = prefix + speaker + state + deadline + "\n".join(function(source, name) for name in names)
code += "\nstatic void presentFrame(){\n" + cue + "}\n" + tests
with tempfile.TemporaryDirectory(prefix="launchlab-best-sound-") as temporary:
    build = Path(temporary)
    cpp = build / "test.cpp"
    executable = build / "test"
    cpp.write_text(code)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-I" + str(root / "firmware/tests/fakes"),
                    "-I" + str(root / "firmware/LaunchLabMini"), "-I" + str(root / "firmware/LaunchLabRpm"),
                    "-I" + str(root / "firmware/tools/fx"), str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
