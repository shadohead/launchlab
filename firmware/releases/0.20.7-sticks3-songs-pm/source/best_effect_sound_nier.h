#pragma once
// NieR:Automata-inspired celebration sounds (Breach..Glitch), in the style of
// best_effect_sound.h, which includes this file before the Synth. Timings
// match best_effect_nier.h. Fixed seeds, no libm per sample, content above
// ~200 Hz. Each is a hit and a short tail (LENGTH s, faded over its last
// 0.3 s) and silent after, while the animation's sustain runs on quietly.
namespace BestEffectSound {
namespace detail {
// A soft-clipped square from a sine: the digital timbre of the game's UI.
BEST_SOUND_INLINE float nierSquare(float o){return o>.33f?1.0f:o<-.33f?-1.0f:o*3;}
// 1 at the start of a window of `len` s, falling as (1-age/len)^2, else 0.
BEST_SOUND_INLINE float nierBlip(float age,float len){if(age<0 || age>=len)return 0;const float u=1-age/len;return u*u;}
// The tail fade: 1 until 0.3 s before `length`, then down to 0.
BEST_SOUND_INLINE float nierTail(float t,float length){return 1-smooth((t-(length-.3f))*(1/.3f));}

// A falling digital sweep as the arena scans in, a pew per orange shot, a
// crunch per broken shield block, bloops for the return fire, a noise burst
// with a falling tone as the core breaks and a two-note "complete" chime.
struct Breach {
  static constexpr float FIRE=.5f,CEASE=1.3f,BREAK=1.38f,SHOT=1/18.0f,LENGTH=2.6f;
  Noise noise{0x4e1e0001u};Smooth low,wide,crunch;Osc dive,pew,knock,bloop,fall,chime[2];
  Decay burst,chimeEnv[2];bool broke=false,rung[2]={};uint32_t chimeStep[2];
  Breach(){burst.set(8);for(int k=0;k<2;++k)chimeEnv[k].set(4.5f);chimeStep[0]=Osc::step(1318.5f);chimeStep[1]=Osc::step(1975.5f);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t<.4f) {
      const float u=t*(1/.36f),a=.3f*smooth(t*(1/.02f))*(1-smooth((t-.28f)*(1/.1f)));
      v+=a*(.35f*nierSquare(dive.next(2400-1800*clamp01(u)))+.5f*(wide.next(x,.35f)-low.next(x,.05f)));
    }
    if(t>=FIRE && t<CEASE+.04f) {
      const float ph=(t-FIRE)*18;const int j=int(ph);const float age=(ph-j)*SHOT;
      if(FIRE+j*SHOT<CEASE){const float e=nierBlip(age,.035f);v+=.3f*e*nierSquare(pew.next(1900-900*age*(1/.035f)));}
    }
    for(int i=0;i<6;++i) {
      const float age=t-(.62f+.105f*i);if(age<0 || age>=.06f)continue;
      const float e=nierBlip(age,.06f);v+=.5f*e*crunch.next(x,.5f)+.25f*e*knock.next(420);
    }
    for(int r=0;r<2;++r){const float age=t-(r?1.05f:.78f);if(age>=0 && age<.12f)v+=.2f*(1-age*(1/.12f))*bloop.next(600+500*age*(1/.12f));}
    if(t>=BREAK) {
      if(!broke){burst.trigger();broke=true;}
      const float e=burst.next()*smooth((t-BREAK)*(1/.003f));
      v+=.6f*e*(.7f*x+.5f*fall.next(900-650*clamp01((t-BREAK)*(1/.25f))));
    }
    for(int k=0;k<2;++k) {
      const float at=k?1.58f:1.46f;if(t<at)continue;
      if(!rung[k]){chimeEnv[k].trigger(.34f);rung[k]=true;}
      const float e=chimeEnv[k].next()*smooth((t-at)*(1/.004f));
      v+=e*(chime[k].advance(chimeStep[k])+.25f*chime[k].octave());
    }
    return v*nierTail(t,LENGTH);
  }
};

// A thin shimmer along the blade, a whoosh and a metallic shing per slash of
// the chain, then the finisher: two bigger whooshes, a knock and a long
// ringing chord through the hold, and a few debris ticks.
struct Bladework {
  static constexpr float START[7]={.24f,.37f,.5f,.63f,.76f,.95f,1.02f},LENGTH=2.4f;
  Noise noise{0x2b2b0002u};Smooth bright,tick;Osc shimmer,shingA,shingB,thudO,ring[3];
  Decay shing,chord,thud,spark;int lastSlash=-1;bool finished=false,thudded=false;
  uint32_t ringStep[3],slot=~0u;float at=0;bool live=false,fired=false;
  Bladework(){shing.set(14);chord.set(3.2f);thud.set(20);spark.set(900);
    static constexpr float hz[]={1975.5f,2637.0f,3951.0f};for(int k=0;k<3;++k)ringStep[k]=Osc::step(hz[k]);}
  float next(float t) {
    static constexpr float PITCH[7]={2637.0f,3136.0f,2793.8f,3520.0f,2960.0f,2349.3f,2637.0f};
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t<.28f)v+=.14f*smooth(t*(1/.05f))*(1-smooth((t-.18f)*(1/.1f)))*(.7f+.3f*SINE[uint32_t(t*30*1024)&1023])*shimmer.next(4186);
    int now=-1;
    for(int i=0;i<7;++i) {
      const bool fin=i>=5;const float age=t-START[i]+.03f,len=fin?.18f:.12f;
      if(age>=0 && age<len) {
        const float s2=smooth(age*(1/.04f))*(1-smooth((age-.05f)*(1/(len-.05f))));
        v+=(fin?.7f:.55f)*s2*(x-bright.next(x,.2f+.5f*clamp01(age*(1/.08f))));
      }
      if(t>=START[i]+.04f)now=i;
    }
    if(now!=lastSlash && now>=0 && now<5){shing.trigger(.26f);lastSlash=now;}
    if(lastSlash>=0){const float p=PITCH[lastSlash],e=shing.next();v+=e*(shingA.next(p)+.6f*shingB.next(p*1.5f));}
    if(t>=.97f) {
      if(!thudded){thud.trigger(.5f);thudded=true;}
      v+=thud.next()*thudO.next(330-130*clamp01((t-.97f)*(1/.08f)));
    }
    if(t>=.99f) {
      static constexpr float level[]={.3f,.22f,.15f};
      if(!finished){chord.trigger();finished=true;}
      const float e=chord.next()*smooth((t-.99f)*(1/.004f));
      for(int k=0;k<3;++k)v+=level[k]*e*ring[k].advance(ringStep[k]);
    }
    const uint32_t s=uint32_t(t*24);
    if(s!=slot){slot=s;at=(slot+rnd(slot,701))*(1/24.0f);live=t>1.0f && t<2.0f && rnd(slot,702)<.55f;fired=false;}
    if(live && !fired && t>=at){spark.trigger(.22f);fired=true;}
    return (v+spark.next()*tick.next(x,.8f))*nierTail(t,LENGTH);
  }
};

// Music-box notes: three falling as the moonlight gathers, four rising as the
// petals unfurl, a slow D-minor-add chord at full bloom with a breath of air,
// and sparse twinkles as light rises off the flower.
struct Moonpetal {
  static constexpr int NOTES=15;static constexpr float LENGTH=2.8f;
  Noise noise{0x3a3a0003u};Smooth low,wide;Osc osc[NOTES];Decay env[NOTES];
  float start[NOTES],level[NOTES];uint32_t inc[NOTES];bool rung[NOTES]={};
  Moonpetal() {
    static constexpr float at[NOTES]={.08f,.26f,.44f,.62f,.8f,.98f,1.16f,1.38f,1.38f,1.38f,1.4f,1.6f,1.85f,2.1f,2.3f};
    static constexpr float hz[NOTES]={1760.0f,1396.9f,1174.7f,1174.7f,1396.9f,1760.0f,2093.0f,1174.7f,1396.9f,1760.0f,2349.3f,2637.0f,2349.3f,3136.0f,2637.0f};
    static constexpr float lv[NOTES]={.22f,.22f,.22f,.26f,.26f,.26f,.26f,.2f,.2f,.2f,.18f,.14f,.14f,.13f,.12f};
    static constexpr float rate[NOTES]={5,5,5,5,5,5,4,1.8f,1.8f,1.8f,2,7,7,7,8};
    for(int p=0;p<NOTES;++p){start[p]=at[p];inc[p]=Osc::step(hz[p]);level[p]=lv[p];env[p].set(rate[p]);}
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    float v=0;
    for(int p=0;p<NOTES;++p) {
      const float age=t-start[p];if(age<0)continue;
      if(!rung[p]){env[p].trigger(level[p]);rung[p]=true;}
      const float o=osc[p].advance(inc[p]);
      v+=smooth(age*(1/.004f))*env[p].next()*(o+.22f*osc[p].octave());
    }
    if(t>1.3f && t<2.5f) {
      const float x=noise.next(),u=t-1.3f;
      v+=.1f*smooth(u*(1/.3f))*(1-smooth((u-.6f)*(1/.6f)))*(wide.next(x,.2f)-low.next(x,.03f));
    }
    return v*nierTail(t,LENGTH);
  }
};

// The drone's servo hum, a machine-gun rattle of thirty shots a second, a
// bright pop per orb, a rising charge, a buzzing beam and the big orb's crash.
struct Barrage {
  static constexpr float FIRE=.5f,RATE_=30,CHARGE=1.38f,BEAM=1.58f,BEAM_END=1.9f,LENGTH=2.4f;
  static constexpr int SHOTS=28;
  Noise noise{0x4d4d0004u};Smooth bright,low,wide;Osc hum,hum2,gun,pop,charge,beam,boom;Decay crash;bool crashed=false;
  Barrage(){crash.set(7);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    v+=.07f*smooth(t*(1/.2f))*(hum.next(330)+.5f*hum2.next(495));
    if(t>=FIRE) {
      const float ph=(t-FIRE)*RATE_;const int j=int(ph);const float age=(ph-j)*(1/RATE_);
      if(j<SHOTS){const float e=nierBlip(age,.022f);v+=.38f*e*(.6f*(x-bright.next(x,.5f))+.5f*nierSquare(gun.next(1100-500*age*(1/.022f))));}
    }
    // When each orb pops on screen (the third shot aimed at it arrives).
    static constexpr float POPS[9]={.826f,.932f,.936f,1.152f,1.22f,1.253f,1.34f,1.455f,1.581f};
    for(float at:POPS){const float age=t-at;if(age>=0 && age<.08f)v+=.22f*(1-age*(1/.08f))*pop.next(1300+900*age*(1/.08f));}
    if(t>=CHARGE && t<BEAM) {
      const float u=(t-CHARGE)*(1/(BEAM-CHARGE));
      v+=.28f*u*(.7f+.3f*SINE[uint32_t(t*24*1024)&1023])*charge.next(500+1500*u*u);
    }
    if(t>=BEAM && t<BEAM_END+.2f) {
      const float a=.42f*smooth((t-BEAM)*(1/.006f))*(t<BEAM_END?1:1-(t-BEAM_END)*(1/.2f));
      const float o=beam.next(440);
      v+=a*(.5f*o+.3f*beam.octave()+.25f*nierSquare(o)+.3f*(wide.next(x,.3f)-low.next(x,.04f)));
    }
    if(t>=BEAM+.02f) {
      if(!crashed){crash.trigger();crashed=true;}
      const float e=crash.next()*smooth((t-BEAM-.02f)*(1/.003f));
      v+=.55f*e*(.6f*x+.4f*boom.next(600-350*clamp01((t-BEAM-.02f)*(1/.2f))));
    }
    return v*nierTail(t,LENGTH);
  }
};

// A soft two-tone UI hum with menu ticks as the rules draw out, a chopped
// flicker, then the glitch: a new stutter every 50 ms (gated buzz, crushed
// noise or a band of static), a clean high ping at the snap and two short
// stutters for the aftershocks.
struct Glitch {
  static constexpr float G0=.62f,G1=1.15f,LENGTH=2.0f;
  Noise noise{0x5e5e0005u};Smooth low,wide,holdLow;Osc h1,h2,tickO,buzz,ping[2];Decay pingEnv,clickEnv;
  float held=0;uint32_t heldSlot=~0u;bool snapped=false;uint32_t pingStep[2];
  Glitch(){pingEnv.set(6);clickEnv.set(400);pingStep[0]=Osc::step(2637.0f);pingStep[1]=Osc::step(3951.0f);}
  float stutter(uint32_t frame,float t,float x) {
    const uint32_t mode=hash(frame*7+3)%3;const float pitch=300+1200*rnd(frame,801);
    if(mode==0)return (uint32_t(t*160)&1)?.8f*nierSquare(buzz.next(pitch)):0.0f;
    if(mode==1) { // sample-and-hold noise: a bit-crushed hiss
      const uint32_t s=uint32_t(t*(2000+4000*rnd(frame,802)));
      if(s!=heldSlot){heldSlot=s;held=x;}
      return .8f*(held-holdLow.next(held,.06f));
    }
    return .9f*(wide.next(x,.45f)-low.next(x,.08f));
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    const float e=t>=G0 && t<G1?smooth((t-G0)*(1/.08f))*(1-smooth((t-1.0f)*(1/.15f))):0;
    const float flicker=t>=.48f && t<G0?((uint32_t(t*40)&1)?.35f:1.0f):1;
    v+=.1f*smooth(t*(1/.3f))*flicker*(1-.7f*e)*(h1.next(784)+.6f*h2.next(1174.7f));
    for(float at:{.03f,.2f,.38f}){const float age=t-at;if(age>=0 && age<.025f)v+=.3f*nierBlip(age,.025f)*tickO.next(1568);}
    if(e>0)v+=.45f*e*stutter(uint32_t((t-G0)*20),t,x);
    if(t>=1.32f && t<1.4f)v+=.3f*stutter(97,t,x);
    if(t>=1.48f && t<1.56f)v+=.26f*stutter(98,t,x);
    if(t>=G1) {
      if(!snapped){pingEnv.trigger(.36f);clickEnv.trigger(.3f);snapped=true;}
      const float pe=pingEnv.next()*smooth((t-G1)*(1/.003f));
      v+=pe*(ping[0].advance(pingStep[0])+.5f*ping[1].advance(pingStep[1]))+clickEnv.next()*x;
    }
    return v*nierTail(t,LENGTH);
  }
};
}
}
