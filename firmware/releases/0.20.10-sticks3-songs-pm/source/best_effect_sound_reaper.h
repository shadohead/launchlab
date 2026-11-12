#pragma once
// The reaper set's celebration sounds (Crescent..Blink), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to the
// animation; a hit with a short tail, silent after its soundDuration().
namespace BestEffectSound {
namespace detail {
// The tail's fade: full until 0.3 s before the end, then smoothly to silence.
BEST_SOUND_INLINE float reaperTail(float t,float length){return 1-smooth((t-length+.3f)*(1/.3f));}
// A metallic ring: four inharmonic partials, each with its own decay.
struct ReaperRing {
  Osc osc[4];Decay env[4];uint32_t step[4];
  void set(float hz,float rate) {
    static constexpr float ratio[]={1,1.51f,2.13f,2.87f};
    for(int k=0;k<4;++k){step[k]=Osc::step(hz*ratio[k]);env[k].set(rate*(1+.6f*k));}
  }
  void strike(float amount){static constexpr float level[]={.42f,.28f,.18f,.1f};for(int k=0;k<4;++k)env[k].trigger(amount*level[k]);}
  BEST_SOUND_INLINE float next(){float v=0;for(int k=0;k<4;++k)if(env[k].level>.0005f)v+=env[k].next()*osc[k].advance(step[k]);return v;}
};

// Crescent: a dark hum swelling under crackling flame, then the swing: a
// fast rising swish, a heavy falling whoomph and a metallic ring, and the
// air tearing behind it, crackle dying away.
struct Crescent {
  static constexpr float LAUNCH=.6f,LENGTH=2.2f;
  Noise noise{0x2545f491u};Smooth low,wide,rip,tick,lo2;Osc hum,body;Decay thump,spark;ReaperRing ring;bool swung=false;
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Crescent(){thump.set(7);spark.set(900);ring.set(1460,5);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t<LAUNCH+.05f) {
      const float s=t*(1/LAUNCH),y=hum.next(260+240*s*s);
      v+=.2f*smooth(t*(1/.25f))*(1-smooth((t-LAUNCH)*(1/.05f)))*(y+.35f*y*y*y)*(.5f+.5f*s);
    }
    if(t>=LAUNCH) {
      if(!swung){thump.trigger(1);ring.strike(.9f);swung=true;}
      const float a=t-LAUNCH;
      // The swish: a band of noise sweeping up over 0.15 s.
      if(a<.25f){const float u=a*(1/.25f),env=smooth(a*(1/.02f))*(1-u)*(1-u);v+=.7f*env*(wide.next(x,.15f+.55f*u)-low.next(x,.05f+.2f*u));}
      const float hz=480-260*clamp01(a*(1/.2f)),y=body.next(hz);
      v+=.55f*thump.next()*smooth(a*(1/.003f))*(1.4f*y-.4f*y*y*y);
      v+=ring.next();
      // The tear: a ragged band of noise, its level jittering, fading.
      if(a>.05f && a<.9f){const float u=(a-.05f)*(1/.85f),jitter=.5f+.5f*SINE[uint32_t(a*37*1024)&1023];
        v+=.32f*(1-u)*smooth((a-.05f)*(1/.05f))*jitter*(rip.next(x,.5f)-lo2.next(x,.12f));}
    }
    // Crackle: flame ticks through the charge and after the swing.
    const float rate=40,density=t<LAUNCH?.3f+.5f*t*(1/LAUNCH):.7f*(1-smooth((t-LAUNCH-.4f)*(1/1.0f)));
    const uint32_t now=uint32_t(t*rate);
    if(now!=slot){slot=now;at=(slot+rnd(slot,71))/rate;live=rnd(slot,72)<density;fired=false;}
    if(live && !fired && t>=at){spark.trigger(.25f+.25f*rnd(slot,73));fired=true;}
    v+=spark.next()*(x-tick.next(x,.35f));
    return v*reaperTail(t,LENGTH);
  }
};
// Release: wind whirling faster and faster with a rising tone, then the
// eruption: a deep boom under a bright roar and a held open chord, the
// tornado's wind beating round, dying away.
struct Release {
  static constexpr float BURST=.75f,LENGTH=2.6f;
  Noise noise{0x9e3779b1u};Smooth low,wide,roarLo,roarHi;Osc rise,boom,chord[3],whirl;Decay hit,crack;bool burst=false;
  uint32_t chordStep[3];
  Release(){hit.set(4);crack.set(30);static constexpr float hz[]={659.3f,987.8f,1318.5f};for(int k=0;k<3;++k)chordStep[k]=Osc::step(hz[k]);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t<BURST) {
      // The whirl: a band whose level beats with the spin, faster and faster.
      const float s=t*(1/BURST),spin=whirl.next(3+15*s*s);
      v+=(.12f+.3f*s*s)*(.6f+.4f*spin)*(wide.next(x,.1f+.3f*s)-low.next(x,.05f+.1f*s));
      v+=.1f*s*s*rise.next(320+900*s*s);
    }
    if(t>=BURST) {
      if(!burst){hit.trigger(1);crack.trigger(1);burst=true;}
      const float a=t-BURST,hz=420-210*clamp01(a*(1/.3f)),y=boom.next(hz);
      v+=.6f*hit.next()*smooth(a*(1/.003f))*(1.3f*y-.3f*y*y*y)+.5f*crack.next()*x;
      // The roar and the tornado's beating wind.
      const float env=smooth(a*(1/.05f))*(1-smooth((t-1.6f)*(1/.8f))),beat=.65f+.35f*SINE[uint32_t(a*7*1024)&1023];
      v+=.38f*env*beat*(roarHi.next(x,.45f)-roarLo.next(x,.08f));
      float c=0;for(int k=0;k<3;++k)c+=chord[k].advance(chordStep[k]);
      v+=.07f*smooth(a*(1/.1f))*(1-smooth((t-1.4f)*(1/1.0f)))*c;
    }
    return v*reaperTail(t,LENGTH);
  }
};
// Petalfall: a blade's ring, then the scatter: a rustle of high noise and
// a cascade of tiny glassy pings that swells with the stream, a bright
// chime as the ring closes, and sparse twinkles after.
struct Petalfall {
  static constexpr float SCATTER=.4f,CLOSE=1.4f,LENGTH=2.4f;
  Noise noise{0x7f4a7c15u};Smooth low,wide;Osc ping,chime[3];ReaperRing blade;Decay pip,bell;bool rang=false,closed=false;
  uint32_t slot=~0u,pingStep=0,chimeStep[3];float at=0;bool live=false,fired=false;
  Petalfall(){blade.set(2093,4);pip.set(45);bell.set(3.2f);static constexpr float hz[]={2637.0f,3136.0f,3951.1f};for(int k=0;k<3;++k)chimeStep[k]=Osc::step(hz[k]);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t>=.1f && !rang){blade.strike(.45f);rang=true;}
    v+=blade.next();
    // The rustle: a soft high band rising with the stream.
    const float flow=smooth((t-SCATTER)*(1/.3f))*(1-smooth((t-1.6f)*(1/.6f)));
    v+=.2f*flow*(wide.next(x,.6f)-low.next(x,.25f));
    // Pings: a cascade, densest as the stream pours out.
    const float density=smooth((t-SCATTER)*(1/.1f))*(.85f-.6f*smooth((t-CLOSE)*(1/.8f)));
    const uint32_t now=uint32_t(t*45);
    if(now!=slot){slot=now;at=(slot+rnd(slot,81))*(1/45.0f);live=rnd(slot,82)<density;fired=false;}
    if(live && !fired && t>=at){static constexpr float scale[]={3136.0f,3520.0f,4186.0f,4698.6f,5274.0f,6271.9f};
      pip.trigger(.09f+.06f*rnd(slot,83));pingStep=Osc::step(scale[hash(slot*11+3)%6]);fired=true;}
    if(pip.level>.001f)v+=pip.next()*ping.advance(pingStep);
    if(t>=CLOSE && !closed){bell.trigger(.22f);closed=true;}
    if(closed){float c=0;for(int k=0;k<3;++k)c+=chime[k].advance(chimeStep[k]);v+=bell.next()*c*smooth((t-CLOSE)*(1/.004f));}
    return v*reaperTail(t,LENGTH);
  }
};
// Mask: a hiss drawn down with the sweep and bony clicks as the pieces
// land, then the awakening: a throaty growl with a fast tremble and a
// dissonant bell, ringing out.
struct Mask {
  static constexpr float SWEEP=.1f,MARK=.85f,LENGTH=2.4f;
  Noise noise{0x3c6ef372u};Smooth low,wide,hp;Osc growl,sub;ReaperRing bell;Decay click,roar;bool woke=false;
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Mask(){click.set(260);roar.set(2.6f);bell.set(880,3.5f);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t<.65f) {
      const float u=clamp01((t-SWEEP)*(1/.45f)),env=smooth((t-.05f)*(1/.08f))*(1-smooth((t-.55f)*(1/.1f)));
      v+=.28f*env*(wide.next(x,.5f-.3f*u)-low.next(x,.12f));
      // Clicks as pieces slide into place.
      const uint32_t now=uint32_t(t*24);
      if(now!=slot){slot=now;at=(slot+rnd(slot,91))*(1/24.0f);live=t>SWEEP && rnd(slot,92)<.75f;fired=false;}
      if(live && !fired && t>=at){click.trigger(.35f+.2f*rnd(slot,93));fired=true;}
    }
    v+=click.next()*(x-hp.next(x,.3f));
    if(t>=MARK) {
      if(!woke){roar.trigger(1);bell.strike(.7f);woke=true;}
      const float a=t-MARK,trem=.7f+.3f*SINE[uint32_t(a*23*1024)&1023];
      const float g=growl.next(233+18*SINE[uint32_t(a*5*1024)&1023]),saw=g+.5f*growl.octave()+.3f*sub.next(349.2f);
      v+=.3f*roar.next()*smooth(a*(1/.01f))*trem*(saw+.4f*x);
      v+=bell.next();
    }
    return v*reaperTail(t,LENGTH);
  }
};
// Blink: a short whoosh at each of the six steps, closer and closer, a
// tick at each landing, then the cut: a sharp crack and a bright ring.
struct Blink {
  static constexpr float CUT=1.25f,LENGTH=2.0f;static constexpr int STEPS=6;
  Noise noise{0x6a09e667u};Smooth low,wide,hp;Osc tick;ReaperRing ring;Decay crack,tap;int stepped=-1;bool cut=false;
  Blink(){crack.set(28);tap.set(180);ring.set(2793.8f,4.5f);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    static constexpr float AT[STEPS]={.25f,.5f,.7f,.85f,.97f,1.07f};
    const float x=noise.next();float v=0;
    // The whoosh into each step: 70 ms of noise sweeping up.
    for(int i=0;i<STEPS;++i) {
      const float a=t-AT[i]+.07f;if(a<0 || a>=.09f)continue;
      const float u=a*(1/.09f),env=SINE[uint32_t(u*512)&1023];
      v+=.45f*env*(wide.next(x,.2f+.5f*u)-low.next(x,.06f+.2f*u));
    }
    int now=-1;for(int i=0;i<STEPS;++i)if(t>=AT[i])now=i;
    if(now>stepped){stepped=now;tap.trigger(.4f);}
    if(tap.level>.001f)v+=tap.next()*(.6f*tick.next(1760)+.4f*(x-hp.next(x,.4f)));
    if(t>=CUT-.05f && t<CUT) {
      const float u=(t-CUT+.05f)*(1/.05f);v+=.5f*u*(wide.next(x,.3f+.5f*u)-low.next(x,.1f));
    }
    if(t>=CUT) {
      if(!cut){crack.trigger(1);ring.strike(.85f);cut=true;}
      v+=.55f*crack.next()*x+ring.next();
    }
    return v*reaperTail(t,LENGTH);
  }
};
}
}
