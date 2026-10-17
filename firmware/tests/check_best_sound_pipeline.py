#!/usr/bin/env python3
"""Exercise production Random selection, recap cues and speaker preparation."""
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
#include "analog_tachometer.h"
#include "best_effect_sound.h"
#include "controls.h"
#include "accel_range.h"
#include "launch_feedback.h"
#include "launch_motion.h"
#include "practice_history.h"
#include <cassert>
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
struct StickS3MotionAcquisition {
  bool lock(){return true;}void unlock(){}
};
struct SpeakerPeripheral {
  struct Config {int task_pinned_core=0;} settings;
  bool powered=false,playing=false;
  unsigned plays=0,stops=0;
  std::vector<int16_t> clip;
  Config config(){return settings;}void config(Config c){settings=c;}
  bool isEnabled(){return true;}
  bool begin(){powered=true;return true;}
  void end(){powered=false;}
  void setVolume(unsigned){}
  bool playRaw(const int16_t *data,size_t count,unsigned,bool,unsigned,unsigned,bool){clip.assign(data,data+count);playing=true;++plays;return true;}
  bool isPlaying(){return playing;}
  void stop(){playing=false;++stops;}
};
static struct {SpeakerPeripheral Speaker;} M5;
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
'''
tests = r'''
static void reset(Appearance::Effect effect=Appearance::Effect::Random) {
  M5.Speaker={};soundPlayer.~BestEffectSpeaker();new(&soundPlayer) BestEffectSpeaker(imuAcquisition);soundPlayer.configure();
  appearance={};appearance.effect=effect;appearance.sound=Appearance::Sound::Low;
  personalBests={};bestStore={};bestEffect={};editorEffect={};upcomingEffect=Appearance::Effect::Off;
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
static void checkClip(Appearance::Effect effect) {
  std::vector<int16_t> expected(BestEffectSound::samples(effect));
  BestEffectSound::render(effect,expected.data(),expected.size());
  assert(M5.Speaker.clip==expected);
}
int main() {
  reset();prepared();assert(soundTarget()==Appearance::Effect::Orbit);
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
    const auto effect=Appearance::Effect(e);previewBestEffect(effect);
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
  hostUs+=56000;assert(bestEffect.elapsed(millis())==56 && M5.Speaker.plays==1);
  // The existing Best effect editor previews every concrete draft through
  // its actual helper and first-frame cue, including an unprepared clip.
  for(unsigned e=0;e<Appearance::DRAWN;++e) {
    reset(Appearance::Effect::Orbit);controls.page=Controls::Page::Edit;controls.setting=6;controls.draft=e;
    previewEffect();assert(editorEffect.badge() && editorEffect.effect()==Appearance::Effect(e));
    tickSound(Snapshot{});presentFrame();
    for(unsigned i=0;soundRequest.active && i<200;++i){hostUs+=10000;tickSound(Snapshot{});}
    assert(M5.Speaker.plays==1 && !soundRequest.active);checkClip(Appearance::Effect(e));
    controls.page=Controls::Page::Settings;tickSound(Snapshot{});
    assert(!editorEffect.badge() && !soundPlayer.playing() && !soundPlayer.powered());
  }
  reset();controls.page=Controls::Page::Edit;controls.setting=6;controls.draft=unsigned(Appearance::Effect::Random);
  previewEffect();const auto editorPick=editorEffect.effect();previewEffect();
  assert(editorEffect.effect()!=editorPick);
  controls.draft=unsigned(Appearance::Effect::Off);previewEffect();presentFrame();
  assert(!editorEffect.badge() && !soundRequest.active && M5.Speaker.plays==0);
  // Deferred editor requests render in idle slices, survive the celebration
  // deadline, and stop when the editor closes.
  reset(Appearance::Effect::Orbit);controls.page=Controls::Page::Edit;controls.setting=7;
  requestSound(Appearance::Effect::Sparkles,Appearance::Sound::Low,false);
  const auto target=upcomingEffect;tickSound(Snapshot{AnalogTachometer::Phase::Launch});
  assert(!soundRequest.active && !soundPlayer.prepared(Appearance::Effect::Sparkles) && upcomingEffect==target);
  requestSound(Appearance::Effect::Sparkles,Appearance::Sound::Low,false);
  for(unsigned i=0;soundRequest.active && i<200;++i){hostUs+=10000;tickSound(Snapshot{});}
  assert(M5.Speaker.plays==1 && !soundRequest.active);checkClip(Appearance::Effect::Sparkles);
  controls.page=Controls::Page::Settings;tickSound(Snapshot{});
  assert(!soundPlayer.playing() && !soundPlayer.powered());
  // The actual Sound editor helper uses the draft level and saved effect.
  reset(Appearance::Effect::Lightning);controls.page=Controls::Page::Edit;controls.setting=7;
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
  // These buffers live for the firmware lifetime; release fake allocations
  // here so the host harness can also use leak detection.
  for(auto p:allocations)std::free(p);
  std::cout<<"PASS: production Random/PB/hidden/cancel selection, first-frame cues, five USB preview sounds, actual Best effect/Sound editor helpers, real sliced synth/speaker buffer, pull/close cancellation, late deadline and timer wrap\n";
}
'''
names = ["upcoming", "celebrationEffect", "soundTarget", "requestSound", "tickSound", "prepareMotion", "previewBestEffect", "previewEffect", "previewSound"]
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
                    str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
