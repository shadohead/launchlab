#pragma once
// Elemental celebration sounds, in the style of best_effect_sound.h, which
// includes this file before the Synth: fixed seeds, no libm per sample,
// timed to the animation.
namespace BestEffectSound {
namespace detail {
// A band-passed wash swelling as the tide rises, a crash and a falling
// thump as it hits, bubbly plinks as the splash falls back and droplets hop
// off the rolling sea, and a soft rolling wash that dies away. The sea's
// drain at 3.55 s is silent: the sound is the hit and a short tail.
struct Tidal {
  static constexpr float RISE=.6f,ROLL=1.15f,END=2.6f;
  Noise noise{0x3c6ef372u};Smooth low,wide,crash,plinkLow;Osc thump,bubble;Decay hit,body,drop;
  uint32_t slot=~0u;float at=0,pitch=0;bool live=false,fired=false,struck=false;
  Tidal(){hit.set(14);body.set(9);drop.set(45);}
  float next(float t) {
    const float x=noise.next(),band=wide.next(x,.12f+.3f*clamp01(t*(1/RISE)))-low.next(x,.03f);
    const float rise=smooth(t*(1/RISE))*(t<RISE?1:.35f),roll=t>ROLL?.4f*smooth((t-ROLL)*(1/.3f))*(1-smooth((t-ROLL-.35f)*(1/(END-ROLL-.4f)))):0;
    float v=.55f*(rise*(t<RISE?1:1-smooth((t-RISE)*(1/.5f)))+roll*(.8f+.2f*SINE[uint32_t(t*1.7f*1024)&1023]))*band;
    if(t>=RISE) {
      if(!struck){hit.trigger(.7f);body.trigger(.6f);struck=true;}
      const float hz=330-130*clamp01((t-RISE)*(1/.15f));
      v+=hit.next()*crash.next(x,.45f)+body.next()*thump.next(hz);
    }
    // Plinks: each a short sine whose pitch glides up, like a drop landing.
    const uint32_t now=uint32_t(t*26);
    if(now!=slot){slot=now;at=(slot+rnd(slot,11))*(1/26.0f);live=t>RISE+.1f && t<2.3f && rnd(slot,12)<(t<1.7f?.7f:.3f);fired=false;}
    if(live && !fired && t>=at){drop.trigger(t<1.7f?.32f:.2f);pitch=700+700*rnd(slot,13);fired=true;}
    const float d=drop.level;
    if(d>.001f)v+=drop.next()*bubble.next(pitch*(1.9f-d*2.6f));
    return v;
  }
};
// Crackling as the ferns grow, a glassy inharmonic shimmer swelling as the
// readout freezes, a shatter of bright pings, then sparse soft twinkles.
struct Frost {
  static constexpr float FREEZE=.85f,SHATTER=1.25f;static constexpr int PINGS=18,GLASS=3;
  Noise noise{0x510e527fu};Smooth bright,tick,burst;Decay crack,shatter,env[PINGS];
  Osc glass[GLASS],ping[PINGS];uint32_t glassStep[GLASS],pingStep[PINGS];float start[PINGS];bool rung[PINGS]={},broke=false;
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Frost() {
    static constexpr float hz[]={2617.0f,3921.0f,5187.0f};
    for(int k=0;k<GLASS;++k)glassStep[k]=Osc::step(hz[k]);
    crack.set(1500);shatter.set(18);
    for(int p=0;p<PINGS;++p){pingStep[p]=Osc::step(2900+3200*rnd(p,21));env[p].set(20);start[p]=p<10?SHATTER+.01f*p:SHATTER+.15f+1.0f*rnd(p,22);}
  }
  float next(float t) {
    const float x=noise.next(),hi=x-bright.next(x,.45f);float v=0;
    // Crackle: ticks growing denser as the ferns grow.
    const float rate=20+60*clamp01(t*(1/FREEZE));const uint32_t now=uint32_t(t*rate);
    if(now!=slot){slot=now;at=(slot+rnd(slot,23))/rate;live=t<SHATTER && rnd(slot,24)<.7f;fired=false;}
    if(live && !fired && t>=at){crack.trigger(.35f+.25f*rnd(slot,25));fired=true;}
    v+=crack.next()*tick.next(hi,.8f);
    // The freeze: a high glassy chord swelling until it breaks.
    if(t>=FREEZE-.1f && t<SHATTER+.05f) {
      static constexpr float level[]={.16f,.11f,.07f};
      const float swell=smooth((t-FREEZE+.1f)*(1/.3f))*(1-smooth((t-SHATTER)*(1/.05f)));
      for(int k=0;k<GLASS;++k)v+=swell*level[k]*glass[k].advance(glassStep[k]);
    }
    if(t>=SHATTER) {
      if(!broke){shatter.trigger(.6f);broke=true;}
      v+=shatter.next()*burst.next(hi,.7f);
      for(int p=0;p<PINGS;++p) {
        const float age=t-start[p];if(age<0 || age>.3f)continue;
        if(!rung[p]){env[p].trigger(p<10?.17f:.1f);rung[p]=true;}
        v+=smooth(age*(1/.002f))*env[p].next()*ping[p].advance(pingStep[p]);
      }
    }
    return v;
  }
};
// Wind: noise through a band whose centre swells with the gust, a thin
// whistle riding on it, and grit ticks while it blows; the breeze after it
// fades out by 2.6 s while the eddies drift on silently.
struct Gale {
  static constexpr float GUST=.55f;
  Noise noise{0x9b05688cu};Smooth low,wide,tick;Osc whistle;Decay grit;
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Gale(){grit.set(1300);}
  float next(float t) {
    const float x=noise.next();
    const float gust=smooth((t-GUST+.14f)*(1/.2f))*(1-smooth((t-1.2f)*(1/.9f)));
    const float whisper=.35f*smooth(t*(1/.15f))*(1-smooth((t-1.85f)*(1/.7f)));
    const float k=.06f+.22f*gust+.04f*SINE[uint32_t(t*7*1024)&1023];
    const float band=wide.next(x,k)-low.next(x,.02f+.03f*gust);
    float v=(whisper+.75f*gust)*band;
    v+=.09f*gust*whistle.next(950+350*gust+60*SINE[uint32_t(t*5*1024)&1023]);
    const uint32_t now=uint32_t(t*30);
    if(now!=slot){slot=now;at=(slot+rnd(slot,31))*(1/30.0f);live=gust>.3f && rnd(slot,32)<.6f;fired=false;}
    if(live && !fired && t>=at){grit.trigger(.25f);fired=true;}
    return v+grit.next()*tick.next(x,.7f);
  }
};
// A low beating drone darkening as the moon covers the sun, a hollow hum in
// totality, and a bright bell as the diamond ring flashes.
struct Eclipse {
  static constexpr float COVER=.75f,DIAMOND=1.6f;static constexpr int BELL=4;
  Osc drone[3],bell[BELL];Decay ring[BELL];uint32_t bellStep[BELL];bool rang=false;
  Eclipse() {
    static constexpr float hz[]={1568.0f,2349.3f,3136.0f,4699.6f},rate[]={3.2f,4.5f,6,9};
    for(int k=0;k<BELL;++k){bellStep[k]=Osc::step(hz[k]);ring[k].set(rate[k]);}
  }
  float next(float t) {
    // Falling from G4 toward E4 as the light goes, two voices beating.
    const float fall=smooth(t*(1/COVER)),hz=392-62*fall;
    const float env=smooth(t*(1/.08f))*(1-smooth((t-DIAMOND)*(1/.25f)))*(.55f+.25f*fall);
    const float a=drone[0].next(hz),b=drone[1].next(hz*1.006f),c=drone[2].next(hz*1.5f);
    const float tremolo=.75f+.25f*SINE[uint32_t(t*6*1024)&1023];
    float v=.32f*env*tremolo*(a+b+.35f*c+.25f*a*a*a);
    if(t>=DIAMOND) {
      static constexpr float level[]={.36f,.26f,.18f,.1f};
      if(!rang){for(int k=0;k<BELL;++k)ring[k].trigger(level[k]);rang=true;}
      const float attack=smooth((t-DIAMOND)*(1/.003f));
      for(int k=0;k<BELL;++k)v+=attack*ring[k].next()*bell[k].advance(bellStep[k]);
    }
    return v;
  }
};
// A shimmer rising as the light gathers, then a bright major chord swelling
// with the burst and fading with sparkles, the last few with the rising motes.
struct Radiance {
  static constexpr float BURST=.65f;static constexpr int VOICES=4,TWINKLES=16;
  Osc rise,riseHi,voice[VOICES],twinkle[TWINKLES];Decay chord,env[TWINKLES];
  uint32_t voiceStep[VOICES],twinkleStep[TWINKLES];float start[TWINKLES];bool rung[TWINKLES]={},burst=false;
  Radiance() {
    static constexpr float hz[]={1046.5f,1318.5f,1568.0f,2093.0f},scale[]={2093.0f,2637.0f,3136.0f,4186.0f};
    for(int k=0;k<VOICES;++k)voiceStep[k]=Osc::step(hz[k]);
    chord.set(1.4f);
    for(int p=0;p<TWINKLES;++p){twinkleStep[p]=Osc::step(scale[hash(p*17+3)%4]);env[p].set(14);start[p]=BURST+.12f+(p<12?1.3f:1.7f)*rnd(p,41);}
  }
  float next(float t) {
    float v=0;
    if(t<BURST+.04f) {
      const float u=clamp01(t*(1/BURST)),hz=500+1300*u*u,a=.26f*smooth(t*(1/.1f))*(1-smooth((t-BURST)*(1/.04f)));
      const float trem=.7f+.3f*SINE[uint32_t(t*(18+30*u)*1024)&1023];
      v+=a*trem*(rise.next(hz)+.4f*riseHi.next(hz*2.01f));
    }
    if(t>=BURST) {
      static constexpr float level[]={.26f,.22f,.2f,.14f};
      if(!burst){chord.trigger();burst=true;}
      const float e=smooth((t-BURST)*(1/.02f))*chord.next(),vib=1+.004f*SINE[uint32_t(t*5.5f*1024)&1023];
      for(int k=0;k<VOICES;++k)v+=level[k]*e*voice[k].advance(uint32_t(voiceStep[k]*vib));
      for(int p=0;p<TWINKLES;++p) {
        const float age=t-start[p];if(age<0 || age>.3f)continue;
        if(!rung[p]){env[p].trigger(.1f);rung[p]=true;}
        v+=smooth(age*(1/.002f))*env[p].next()*twinkle[p].advance(twinkleStep[p]);
      }
    }
    return v;
  }
};
}
}
