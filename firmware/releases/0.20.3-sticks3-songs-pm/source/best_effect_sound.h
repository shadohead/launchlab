#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <variant>
#include "appearance.h"

// Personal-best sounds, one per effect and timed to it: mono 16-bit PCM at
// RATE, synthesized into a buffer ahead of time and played by the speaker
// task, so UI frame timing never affects playback. Fixed seeds, no heap, no
// libm in the per-sample path. Built for a small speaker: content sits above
// ~200 Hz.
namespace BestEffectSound {
constexpr uint32_t RATE=16000;
inline size_t samples(Appearance::Effect e){return size_t(Appearance::soundDuration(e))*RATE/1000;}

namespace detail {
constexpr double PI_D=3.14159265358979323846;
constexpr double taylorSine(double x){double term=x,sum=x;for(int n=1;n<12;++n){term*=-x*x/((2*n)*(2*n+1));sum+=term;}return sum;}
constexpr std::array<float,1025> sineTable() {
  std::array<float,1025> t{};
  for(int i=0;i<=1024;++i){double a=i*2*PI_D/1024;if(a>PI_D)a-=2*PI_D;t[i]=float(taylorSine(a));}
  return t;
}
constexpr std::array<float,1025> SINE=sineTable();

// The ESP32-S3 passes floats in integer registers and divides in software,
// so the per-sample helpers are forced inline and divide only by constants.
#define BEST_SOUND_INLINE inline __attribute__((always_inline))
// Phase accumulator oscillator: phase in turns, Q32.
struct Osc {
  uint32_t phase=0;
  static uint32_t step(float hz){return uint32_t(hz*(4294967296.0f/RATE));}
  BEST_SOUND_INLINE float next(float hz){return advance(step(hz));}
  BEST_SOUND_INLINE float advance(uint32_t inc){const float s=SINE[phase>>22];phase+=inc;return s;}
  // The exact octave above, from the same phase.
  BEST_SOUND_INLINE float octave()const{return SINE[(phase<<1)>>22];}
};
struct Noise {
  uint32_t state;
  explicit Noise(uint32_t seed=1):state(seed){}
  BEST_SOUND_INLINE float next(){state^=state<<13;state^=state>>17;state^=state<<5;return int32_t(state)*(1.0f/2147483648.0f);}
};
// One-pole low-pass; a difference of two makes a cheap band-pass.
struct Smooth {float y=0;BEST_SOUND_INLINE float next(float x,float k){y+=(x-y)*k;return y;}};
// Exponential decay as a per-sample multiply: no libm call per sample, which
// costs about 3 us at 80 MHz.
struct Decay {
  float level=0,k=1;
  void set(float rate){k=std::exp(-rate/RATE);}
  void trigger(float amount=1){level=amount;}
  BEST_SOUND_INLINE float next(){const float v=level;level*=k;return v;}
};
BEST_SOUND_INLINE float clamp01(float v){return v<0?0:v>1?1:v;}
BEST_SOUND_INLINE float smooth(float v){v=clamp01(v);return v*v*(3-2*v);}
inline uint32_t hash(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
inline float rnd(uint32_t i,uint32_t salt){return hash(i*0x9e3779b9u^salt)*(1.0f/4294967296.0f);}

// Two comets spin up (a rising tone pulsing at each pass), then the ring
// locks in with a bright E-major chime.
struct Orbit {
  static constexpr float SPIN=1.05f,LAPS=2.3f;
  Osc tone,ring[3];Decay chime;bool rang=false;
  uint32_t chimeStep[3];
  Orbit(){chime.set(5.5f);static constexpr float hz[]={1318.5f,1975.5f,2637.0f};for(int k=0;k<3;++k)chimeStep[k]=Osc::step(hz[k]);}
  float next(float t) {
    const float s=std::min(t,SPIN)/SPIN;float v=0;
    if(t<SPIN+.06f) {
      const float passes=2*LAPS*s*s,pulse=.55f+.45f*SINE[uint32_t(passes*1024)&1023];
      const float hz=240+1500*s*s,a=.32f*smooth(t*(1/.12f))*(1-smooth((t-SPIN)*(1/.06f)))*pulse;
      const float x=tone.next(hz);v+=a*(x+.3f*x*x*x);
    }
    if(t>=SPIN) {
      static constexpr float level[]={.42f,.3f,.2f};
      if(!rang){chime.trigger();rang=true;}
      const float env=smooth((t-SPIN)*(1/.004f))*chime.next();
      for(int k=0;k<3;++k)v+=level[k]*env*ring[k].advance(chimeStep[k]);
    }
    return v;
  }
};
// A band-passed noise roar that rises, holds and burns down, with about 30
// crackles a second, each a 3 ms burst.
struct Flames {
  Noise noise{0x9e3779b9u};Smooth low,wide,crack;Decay pop;
  uint32_t slot=~0u;float at=0,amount=0;bool live=false,fired=false;
  Flames(){pop.set(900);}
  float next(float t) {
    const float x=noise.next(),roar=wide.next(x,.28f)-low.next(x,.035f);
    const float env=smooth(t*(1/.3f))*(1-smooth((t-1.05f)*(1/.63f)));
    const uint32_t now=uint32_t(t*30);
    if(now!=slot){slot=now;at=(slot+rnd(slot,7))*(1/30.0f);live=t>.15f && t<1.55f && rnd(slot,8)<.8f;amount=.5f+.5f*rnd(slot,9);fired=false;}
    if(live && !fired && t>=at){pop.trigger(amount);fired=true;}
    const float c=crack.next(x,.6f);
    return .75f*env*roar+.55f*c*pop.next();
  }
};
// A burst of glassy pings in a C major pentatonic, then sparse twinkles.
struct Sparkles {
  static constexpr int PINGS=26;
  Osc osc[PINGS];Decay env[PINGS];float start[PINGS];uint32_t inc[PINGS];bool rung[PINGS]={};
  Sparkles() {
    static constexpr float scale[]={2093.0f,2349.3f,2637.0f,3136.0f,3520.0f,4186.0f};
    for(int p=0;p<PINGS;++p){start[p]=p<8?.012f*p:.1f+1.1f*rnd(p,1);inc[p]=Osc::step(scale[hash(p*31+7)%6]);env[p].set(16);}
  }
  float next(float t) {
    float v=0;
    for(int p=0;p<PINGS;++p) {
      const float age=t-start[p];if(age<0 || age>.32f)continue;
      if(!rung[p]){env[p].trigger(p<8?.2f:.16f);rung[p]=true;}
      const float x=osc[p].advance(inc[p]);
      v+=smooth(age*(1/.002f))*env[p].next()*(x+.25f*osc[p].octave());
    }
    return v;
  }
};
// A falling-pitch impact with a noise transient, then the ring's whoosh.
struct Shockwave {
  Osc boom;Noise noise{0x85ebca6bu};Smooth low,wide;Decay body,transient,pitch;
  Shockwave(){body.set(4.5f);transient.set(40);pitch.set(7);body.trigger();transient.trigger();pitch.trigger();}
  float next(float t) {
    const float x=noise.next(),hz=180+620*pitch.next();
    const float impact=.75f*body.next()*smooth(t*(1/.003f))*boom.next(hz)+.55f*transient.next()*x;
    const float band=wide.next(x,.22f)-low.next(x,.02f);
    // sin^2 swell over 0.08-0.56 s, peaking with the ring's sweep.
    const float u=(t-.08f)*(1/.48f),swell=u>0 && u<1?SINE[uint32_t(u*512)]*SINE[uint32_t(u*512)]:0;
    return impact+.45f*swell*band;
  }
};
// Four strikes, each a sharp crack with a re-flash and a fading buzz, and
// crackle between them.
struct Lightning {
  static constexpr float strikes[]={0,.24f,.5f,.86f},power[]={1,.8f,.95f,.6f};
  Noise noise{0xc2b2ae35u};Osc buzz;Smooth bright,tick;uint32_t buzzStep=Osc::step(220);
  Decay crack[4],flash[4],hum[4],spark;bool struck[4]={},flashed[4]={};
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Lightning(){for(int s=0;s<4;++s){crack[s].set(55);flash[s].set(70);hum[s].set(10);}spark.set(1200);}
  float next(float t) {
    const float x=noise.next(),hi=x-bright.next(x,.5f),square=buzz.advance(buzzStep)>0?1.0f:-1.0f;
    float v=0;
    for(int s=0;s<4;++s) {
      const float age=t-strikes[s];if(age<0 || age>.3f)continue;
      if(!struck[s]){crack[s].trigger(power[s]);hum[s].trigger(power[s]);struck[s]=true;}
      if(age>=.08f && !flashed[s]){flash[s].trigger(power[s]);flashed[s]=true;}
      v+=(.7f*crack[s].next()+.45f*flash[s].next())*(.6f*hi+.4f*x)+.1f*hum[s].next()*square;
    }
    const uint32_t now=uint32_t(t*36);
    if(now!=slot){slot=now;at=(slot+rnd(slot,3))*(1/36.0f);live=t<1.25f && rnd(slot,4)<.6f;fired=false;}
    if(live && !fired && t>=at){spark.trigger(.3f);fired=true;}
    return v+spark.next()*tick.next(x,.7f);
  }
};
// A whoosh as the blade crosses, a bright inharmonic ring as it cuts, a low
// knock as the halves part with spark ticks, and a short click when they
// close again.
struct Slash {
  static constexpr int PARTIALS=4;
  Noise noise{0x27d4eb2fu};Smooth low,wide,tick;Osc ring[PARTIALS],knock,click;Decay shing[PARTIALS],thud,spark,snap;
  uint32_t ringStep[PARTIALS];bool cut=false,parted=false,closed=false;
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Slash() {
    static constexpr float hz[]={1873.0f,2741.0f,3919.0f,5233.0f},rate[]={7,9,12,16};
    for(int k=0;k<PARTIALS;++k){ringStep[k]=Osc::step(hz[k]);shing[k].set(rate[k]);}
    thud.set(22);spark.set(1100);snap.set(90);
  }
  float next(float t) {
    const float x=noise.next();float v=0;
    // Whoosh: band-passed noise swelling over the swing, brighter as it ends.
    if(t<.2f) {
      const float swell=smooth(t*(1/.1f))*(1-smooth((t-.1f)*(1/.1f)));
      v+=.6f*swell*(wide.next(x,.18f+.5f*clamp01(t*(1/.12f)))-low.next(x,.04f));
    }
    if(t>=.1f) {
      static constexpr float level[]={.34f,.26f,.18f,.12f};
      if(!cut){for(int k=0;k<PARTIALS;++k)shing[k].trigger(level[k]);cut=true;}
      const float attack=smooth((t-.1f)*(1/.003f));
      for(int k=0;k<PARTIALS;++k)v+=attack*shing[k].next()*ring[k].advance(ringStep[k]);
    }
    if(t>=.16f) {
      if(!parted){thud.trigger(.55f);parted=true;}
      const float hz=260-90*clamp01((t-.16f)*(1/.08f));
      v+=thud.next()*knock.next(hz);
    }
    const uint32_t now=uint32_t(t*40);
    if(now!=slot){slot=now;at=(slot+rnd(slot,5))*(1/40.0f);live=t>.16f && t<.7f && rnd(slot,6)<.65f;fired=false;}
    if(live && !fired && t>=at){spark.trigger(.32f);fired=true;}
    v+=spark.next()*tick.next(x,.75f);
    if(t>=1.24f) {
      if(!closed){snap.trigger(.4f);closed=true;}
      v+=snap.next()*(.6f*click.next(3150)+.4f*x);
    }
    return v;
  }
};
}
}
#include "best_effect_sound_elemental.h"
#include "best_effect_sound_physical.h"
#include "best_effect_sound_eva.h"
#include "best_effect_sound_slayer.h"
#include "best_effect_sound_ninja.h"
#include "best_effect_sound_nier.h"
#include "best_effect_sound_sorcery.h"
#include "best_effect_sound_aura.h"
#include "best_effect_sound_alchemy.h"
#include "best_effect_sound_phantom.h"
#include "best_effect_sound_armory.h"
#include "best_effect_sound_grimoire.h"
#include "best_effect_sound_ripsaw.h"
#include "best_effect_sound_corsair.h"
#include "best_effect_sound_reaper.h"
namespace BestEffectSound {

// One effect's generators at a time, in Settings order (Appearance::drawnIndex
// + 1; 0 is no effect), so the Synth's size is the largest generator's, not
// the sum of all of them.
using Generator=std::variant<std::monostate,
  detail::Orbit,detail::Flames,detail::Sparkles,detail::Shockwave,detail::Lightning,detail::Slash,
  detail::Tidal,detail::Frost,detail::Gale,detail::Eclipse,detail::Radiance,
  detail::Spin,detail::Storm,detail::Quickdraw,detail::Leaves,detail::Bloom,
  detail::Crimson,detail::Bulwark,detail::Crux,detail::Klaxon,detail::Prism,
  detail::Sunwheel,detail::Torrent,detail::Blitz,detail::Tigerclaw,detail::Butterfly,
  detail::Birdsong,detail::Spiral,detail::Pinwheel,detail::Overdrive,detail::Summon,
  detail::Breach,detail::Bladework,detail::Moonpetal,detail::Barrage,detail::Glitch,
  detail::Rupture,detail::Boundless,detail::Precision,detail::Umbra,detail::Ravage,
  detail::Haymaker,detail::Palms,detail::Elastic,detail::Chains,detail::Waterleaf,
  detail::Transmute,detail::Ignition,detail::Spires,detail::Gate,detail::Fracture,
  detail::Timestop,detail::Zipper,detail::Rush,detail::Menace,detail::Sequel,
  detail::Swordland,detail::Treasury,detail::Sunblade,detail::Aegis,detail::Trace,
  detail::Volley,detail::Starfall,detail::Mimic,detail::Scales,detail::Meadow,
  detail::Rev,detail::Hammer,detail::Foxbite,detail::Detonate,detail::Sharkfin,
  detail::Slingshot,detail::Presence,detail::Threefold,detail::Banner,detail::Vapor,
  detail::Crescent,detail::Release,detail::Petalfall,detail::Mask,detail::Blink>;
static_assert(std::variant_size_v<Generator> == Appearance::DRAWN+1,"one generator per drawn effect");

// Synthesizes one effect's sound, resumable: run() continues where the last
// call stopped, so the sketch can render a slice per loop pass. A pure
// function of the effect and length: any slicing gives the same samples.
class Synth {
public:
  Synth(Appearance::Effect e,size_t n):n_(n) {
    if(Appearance::drawn(unsigned(e)))select<1>(Appearance::drawnIndex(e)+1);
  }
  // Starts over for another effect, built in place: no temporary Synth on
  // the loop task's stack.
  void restart(Appearance::Effect e,size_t n){this->~Synth();new(this) Synth(e,n);}
  size_t run(int16_t *out,size_t count) {
    size_t written=0;
    for(;written<count && index_<n_;++written,++index_)out[written]=limit(next_(generator_,index_*(1.0f/RATE)),index_);
    return written;
  }
  bool done()const{return index_>=n_;}
  size_t index()const{return index_;}
private:
  // Builds generator I in place and points next_ at it; no std::visit, so
  // no exception path and one indirect call per sample.
  template<size_t I> void select(unsigned want) {
    if constexpr(I<std::variant_size_v<Generator>) {
      if(I!=want){select<I+1>(want);return;}
      generator_.template emplace<I>();
      next_=[](Generator &g,float t){return std::get_if<I>(&g)->next(t);};
    }
  }
  int16_t limit(float v,size_t i)const {
    // 2 ms in and 25 ms out, so the speaker never clicks.
    constexpr size_t ATTACK=RATE/500,FADE=RATE/40;
    if(i<ATTACK)v*=float(i)/ATTACK;
    if(n_-i<FADE)v*=float(n_-i)/FADE;
    // Linear to 0.6 of full scale, then compressing toward 0.95.
    const float a=std::fabs(v),d=a-.6f,limited=d<=0?a:.6f+.35f*3*d/(1+3*d);
    return int16_t((v<0?-limited:limited)*32767);
  }
  size_t n_,index_=0;
  Generator generator_;
  float (*next_)(Generator &,float)=[](Generator &,float){return 0.0f;};
};

// Renders the effect's sound into out[0..n), normally n=samples(e).
inline void render(Appearance::Effect e,int16_t *out,size_t n) {
  Synth synth(e,n);const size_t written=synth.run(out,n);
  for(size_t i=written;i<n;++i)out[i]=0;
}
}
