#pragma once
// HIL only (included by hil.h): the loaded-effects spike
// (docs/FIRMWARE_LOADABLE_EFFECTS.md). The compiled files are built in
// (fx_spike_effects.h); the base firmware would read them from flash.
//   !fxlist           the files and their static bounds
//   !fxplay <n>       preview file n on the recap like E<id>, through the real
//                     UI loop, with the 9 ms guard; PERF and !effectframes time it
//   !fxsound <n> [slice]  render file n's sound (in slices of `slice`
//                     samples, 250 by default, as the idle passes do) and the
//                     native one, timed, with the work the cost model counts
//   !fxbench [n] [g]  offscreen: every 50 ms of file n (or all) and of its
//                     native counterpart on the demo recap, effect pass only;
//                     g=0 runs without the guard. Also hashes three frames.
// The spike times each primitive on the device in a separate bench pass
// (LoadableFx::Engine::profiling; fx_effect.h turns it on for HIL builds).
#include "fx_effect.h"
#include "fx_spike_effects.h"
#include <new>

namespace HilFx {
// The built-in effects' renderer and mask cache, shared as the base
// firmware would share them.
inline BestEffectUI::Renderer &renderer=bestEffectRenderer;
// The spike's own state is allocated at the first fx command, not at boot:
// static internal RAM is what the ADC's DMA needs at boot (two renderers and
// two 16 KB buffers left it short, spike run 3; main's HIL image has 8.9 KB
// to spare). The file goes to PSRAM and only the parts read every frame
// (code, arguments, constants; at most 4 KB) to internal RAM, as the base
// firmware will play them (LoadableFx::place).
constexpr size_t BUFFER=8192;
struct State {
  LoadableFx::Engine engine{bestEffectRenderer};
  LoadableFx::Program program;
  char perFrame[130*7];  // the bench's per-frame times, for fitting the cost model
};
inline State *state=nullptr;
// The sound renders as the firmware's speaker path does: its own program
// over the file (unplaced: the placed one only draws) and the renderer in
// PSRAM, allocated at the first !fxsound.
struct SoundState {LoadableFx::Program program;LoadableFx::Sound sound;};
inline SoundState *soundState=nullptr;
inline uint8_t *file=nullptr,*hot=nullptr;
inline int playing=-1;
inline int64_t frameStart=0;
inline uint32_t budgetUs=9000,stops=0;
inline bool overBudget(void *){return esp_timer_get_time()-frameStart>int64_t(budgetUs);}
inline bool ready() {
  if(!state) {
    void *memory=heap_caps_malloc(sizeof(State),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    file=static_cast<uint8_t *>(heap_caps_malloc(BUFFER,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    hot=static_cast<uint8_t *>(heap_caps_malloc(LoadableFx::HOT_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT));
    if(memory){state=new(memory) State();state->engine.profiling=false;}
  }
  return state && file && hot;
}
// Loads file n into the program: copied to PSRAM, checked, its hot parts placed in internal RAM.
inline bool load(unsigned n) {
  if(!ready() || n>=FxSpike::COUNT || FxSpike::FILES[n].size>BUFFER)return false;
  playing=-1;
  memcpy(file,FxSpike::FILES[n].bytes,FxSpike::FILES[n].size);
  LoadableFx::Program &p=state->program;
  return LoadableFx::load(file,FxSpike::FILES[n].size,p)==LoadableFx::Error::None && LoadableFx::place(p,hot,LoadableFx::HOT_BYTES);
}
// The built-in effect a port was made from, by name (Orbit for the others).
inline Appearance::Effect counterpart(const LoadableFx::Program &p) {
  for(unsigned e=0;e<Appearance::EFFECTS;++e)if(Appearance::drawn(e) && !strcmp(Appearance::label(Appearance::Effect(e)),p.name))return Appearance::Effect(e);
  return Appearance::Effect::Off;
}
inline uint64_t fnv64(const uint8_t *p,size_t n){uint64_t h=0xcbf29ce484222325ull;while(n--){h^=*p++;h*=1099511628211ull;}return h;}
}

// The recap's effect pass when a loaded file is previewing: true when it drew.
LOOP_HELPER static bool hilFxDraw(uint16_t *pixels,int width,int height,uint32_t elapsed,uint16_t accent,uint16_t background) {
  using namespace HilFx;
  if(playing<0 || !state)return false;
  const LoadableFx::Program &program=state->program;LoadableFx::Engine &engine=state->engine;
  if(elapsed>=program.duration){playing=-1;return false;}
  LoadableFx::Guard guard;guard.expired=overBudget;frameStart=esp_timer_get_time();
  engine.draw({pixels,width,height},program,elapsed,accent,BestEffectUI::Renderer::Anchor(),background,guard);
  stops+=engine.stats().stopped;
  return true;
}

LOOP_HELPER static void hilFxCommand(const char *cmd,const char *a1,const char *a2) {
  using namespace HilFx;
  if(!strcmp(cmd,"fxlist")) {
    for(unsigned i=0;i<FxSpike::COUNT;++i) {
      LoadableFx::Program p;const auto e=LoadableFx::load(FxSpike::FILES[i].bytes,FxSpike::FILES[i].size,p);
      Hil::reply("HIL_FX_FILE index=%u name=%s bytes=%u load=%s duration_ms=%u max_ops=%lu max_draws=%lu\n",i,FxSpike::FILES[i].name,
        unsigned(FxSpike::FILES[i].size),LoadableFx::label(e),unsigned(p.duration),(unsigned long)p.maxOps,(unsigned long)p.maxDraws);
    }
    Hil::replyLine("HIL_FX_LIST_END");return;
  }
  if(!strcmp(cmd,"fxplay")) {
    const unsigned n=a1?unsigned(atoi(a1)):~0u;
    if(!load(n)){Hil::replyLine("HIL_ERR fxplay index");return;}
    const LoadableFx::Program &program=state->program;
    const auto native=counterpart(program);
    // The preview's clock and hold follow the named effect; a file without
    // one borrows Orbit's 5 s.
    previewBestEffect(native==Appearance::Effect::Off?Appearance::Effect::Orbit:native);
    playing=int(n);stops=0;
    Hil::reply("HIL_FX_PLAY index=%u name=%s duration_ms=%u budget_us=%lu\n",n,program.name,unsigned(program.duration),(unsigned long)budgetUs);
    return;
  }
  if(!strcmp(cmd,"fxsound")) {
    // Renders file n's sound and the native one into PSRAM, as the speaker's prepare() would.
    const unsigned n=a1?unsigned(atoi(a1)):0;const size_t step=a2 && atoi(a2)>0?size_t(atoi(a2)):250;
    if(!soundState){void *m=heap_caps_malloc(sizeof(SoundState),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(m)soundState=new(m) SoundState();}
    if(!soundState){Hil::replyLine("HIL_ERR fxsound memory");return;}
    LoadableFx::Program &p=soundState->program;LoadableFx::Sound &sound=soundState->sound;
    if(!load(n) || LoadableFx::load(file,FxSpike::FILES[n].size,p)!=LoadableFx::Error::None || !p.hasSound){Hil::replyLine("HIL_ERR fxsound index");return;}
    const size_t count=p.soundSamples();
    int16_t *buffer=static_cast<int16_t *>(heap_caps_malloc(count*sizeof(int16_t),MALLOC_CAP_SPIRAM));
    if(!buffer){Hil::replyLine("HIL_ERR fxsound memory");return;}
    sound.begin(p);
    int64_t started=esp_timer_get_time();uint32_t slice=0,spent=0;
    for(size_t at=0;at<count;){const int64_t s0=esp_timer_get_time();at+=sound.run(buffer+at,step);const uint32_t us=uint32_t(esp_timer_get_time()-s0);spent+=us;slice=std::max(slice,us);vTaskDelay(1);}
    (void)started;const uint32_t loadedUs=spent;
    const uint64_t loadedHash=fnv64(reinterpret_cast<const uint8_t *>(buffer),count*2);
    const auto native=counterpart(p);uint32_t nativeUs=0;uint64_t nativeHash=0;
    if(native!=Appearance::Effect::Off) {
      started=esp_timer_get_time();BestEffectSound::render(native,buffer,count);nativeUs=uint32_t(esp_timer_get_time()-started);
      nativeHash=fnv64(reinterpret_cast<const uint8_t *>(buffer),count*2);
    }
    heap_caps_free(buffer);
    const auto &w=sound.work();
    Hil::reply("HIL_FX_SOUND name=%s samples=%u ops=%lu loaded_us=%lu max_slice_us=%lu native_us=%lu loaded_hash=%016llx native_hash=%016llx "
      "steps=%lu events=%lu voice_samples=%lu noise_samples=%lu filter_samples=%lu partial_samples=%lu attack_samples=%lu idle_noise=%lu\n",p.name,unsigned(count),
      (unsigned long)sound.ops(),(unsigned long)loadedUs,(unsigned long)slice,(unsigned long)nativeUs,(unsigned long long)loadedHash,(unsigned long long)nativeHash,
      (unsigned long)w.steps,(unsigned long)w.events,(unsigned long)w.voiceSamples,(unsigned long)w.noiseSamples,(unsigned long)w.filterSamples,
      (unsigned long)w.partialSamples,(unsigned long)w.attackSamples,(unsigned long)w.idleNoise);
    return;
  }
  if(!strcmp(cmd,"fxstops")){Hil::reply("HIL_FX_STOPS stops=%lu\n",(unsigned long)stops);return;}
  if(!strcmp(cmd,"fxbench")) {
    const int only=a1?atoi(a1):-1;const bool guarded=!a2 || atoi(a2)!=0;
    if(!screenReady){Hil::replyLine("HIL_ERR fxbench no_canvas");return;}
    const Palette::Colors c=Themes::builtIn(Appearance::Theme::Classic);
    LaunchMotion::Trace trace;MotionUI::makeDemo(trace,false);trace.rpm=15500;
    uint16_t *px=static_cast<uint16_t *>(screen.getBuffer());const int w=screen.width(),h=screen.height();
    // The demo recap as ui_render draws it (tools/ui_render/ui_catalog.h).
    auto scene=[&](uint32_t elapsed) {
      ControlsUI::colors=c;screen.fillScreen(c.background);screen.setTextDatum(top_center);screen.setTextColor(c.text,c.background);
      MotionUI::feedback(screen,trace,false,false,MotionReplay::playbackProgress(trace,elapsed),false,c,"NEW BEST",MotionUI::demoCurve(trace.rpm));
      ControlsUI::footer(screen,"A Main  B Menu");
    };
    for(unsigned n=0;n<FxSpike::COUNT;++n) {
      if(only>=0 && int(n)!=only)continue;
      if(!load(n))continue;
      const LoadableFx::Program &p=state->program;LoadableFx::Engine &engine=state->engine;
      const auto native=counterpart(p);
      for(int pass=0;pass<2;++pass) {
        if(pass==1 && native==Appearance::Effect::Off)break;
        uint32_t worst=0,worstAt=0,frames=0,maxOps=0,stopped=0,maxDraws=0,maxTouches=0;uint64_t total=0;
        // Every frame's time, for fitting the cost model against the host's counts.
        char *const perFrame=state->perFrame;constexpr size_t PER_FRAME=sizeof(State::perFrame);size_t perUsed=0;perFrame[0]=0;
        for(uint32_t ms=0;ms<p.duration;ms+=50) {
          scene(ms);
          LoadableFx::Guard guard;if(guarded)guard.expired=overBudget;
          frameStart=esp_timer_get_time();
          if(pass==0)engine.draw({px,w,h},p,ms,c.accent,BestEffectUI::Renderer::Anchor(),c.background,guard);
          else renderer.draw({px,w,h},native,ms,c.accent,BestEffectUI::Renderer::Anchor(),c.background);
          const uint32_t us=uint32_t(esp_timer_get_time()-frameStart);
          if(us>worst){worst=us;worstAt=ms;}
          total+=us;++frames;
          if(perUsed<PER_FRAME-8)perUsed+=snprintf(perFrame+perUsed,PER_FRAME-perUsed,"%s%lu",perUsed?",":"",(unsigned long)us);
          if(pass==0){maxOps=std::max(maxOps,engine.stats().ops);stopped+=engine.stats().stopped;
            maxDraws=std::max(maxDraws,engine.stats().draws);maxTouches=std::max(maxTouches,engine.stats().touches);}
          vTaskDelay(1);  // let the idle task feed the watchdog
        }
        // Frames at fixed times, for the host renderer to reproduce.
        char hashes[96];size_t used=0;
        for(uint32_t ms:{300u,1000u,2500u}) {
          scene(ms);
          if(pass==0)engine.draw({px,w,h},p,ms,c.accent,BestEffectUI::Renderer::Anchor(),c.background);
          else renderer.draw({px,w,h},native,ms,c.accent,BestEffectUI::Renderer::Anchor(),c.background);
          used+=snprintf(hashes+used,sizeof(hashes)-used,"%s%016llx",used?",":"",(unsigned long long)fnv64(reinterpret_cast<const uint8_t *>(px),size_t(w)*h*2));
        }
        Hil::reply("HIL_FX_BENCH file=%s name=%s kind=%s guard=%d frames=%lu max_us=%lu at_ms=%lu mean_us=%lu max_ops=%lu max_draws=%lu max_touches=%lu stopped=%lu hashes=%s\n",
          FxSpike::FILES[n].name,p.name,pass?"native":"loaded",guarded,(unsigned long)frames,(unsigned long)worst,(unsigned long)worstAt,(unsigned long)(frames?total/frames:0),
          (unsigned long)maxOps,(unsigned long)maxDraws,(unsigned long)maxTouches,(unsigned long)stopped,hashes);
        Hil::reply("HIL_FX_FRAMES file=%s kind=%s guard=%d us=%s\n",FxSpike::FILES[n].name,pass?"native":"loaded",guarded,perFrame);
      }
      // Unguarded, a third pass times each primitive (the timing adds a few
      // us a call, so the passes above run without it).
      if(!guarded) {
        engine.profiling=true;for(unsigned k=0;k<LoadableFx::PRIMS;++k){engine.profile[k]=0;engine.profileCalls[k]=0;}
        for(uint32_t ms=0;ms<p.duration;ms+=50){scene(ms);engine.draw({px,w,h},p,ms,c.accent,BestEffectUI::Renderer::Anchor(),c.background);vTaskDelay(1);}
        engine.profiling=false;
        char *const line=state->perFrame;size_t used=0;line[0]=0;
        for(unsigned k=0;k<LoadableFx::PRIMS;++k)if(engine.profileCalls[k] && used<sizeof(State::perFrame)-24)
          used+=snprintf(line+used,sizeof(State::perFrame)-used,"%s%u:%lu:%lu",used?",":"",k,(unsigned long)engine.profileCalls[k],(unsigned long)engine.profile[k]);
        Hil::reply("HIL_FX_PRIMS file=%s prims=%s\n",FxSpike::FILES[n].name,line);
      }
    }
    dirty=true;Hil::replyLine("HIL_FX_BENCH_END");return;
  }
  Hil::replyLine("HIL_ERR fx command");
}
