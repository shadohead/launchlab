#pragma once
// Evangelion-inspired celebration sounds (Crimson..Prism), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to
// best_effect_eva.h.
namespace BestEffectSound {
namespace detail {

// A sine with a soft cubic edge: brighter than a pure tone on a small speaker.
BEST_SOUND_INLINE float evaEdge(float x){return x+.3f*x*x*x;}
// Slow vibrato or tremolo: a table sine of t*hz, 0.5 +/- 0.5.
BEST_SOUND_INLINE float evaWobble(float t,float hz){return .5f+.5f*SINE[uint32_t(t*hz*1024)&1023];}
// Each sound is a hit and a short tail, silent from `end` on (the
// animations run about 5 s; the sound stops well before), with a 0.15 s fade.
BEST_SOUND_INLINE float evaGate(float t,float end){return 1-smooth((t-end+.15f)*(1/.15f));}

// The sphere: a band of noise swells as the crimson wave rises with a
// rising minor chord under it; a deep falling boom and a bright bell as the
// sphere breaks the sea; a slow, trembling A-minor choir as it rises,
// fading out by 2.7 s.
struct Crimson {
  static constexpr float RISE=.8f,IGNITE=.82f,HOLD=1.85f;
  Noise noise{0x5eed0e1au};Smooth low,wide;Osc rise[3],choir[3],boom,bell[2];Decay hit,ring;
  uint32_t bellStep[2];bool lit=false;
  static constexpr float END=2.7f;
  Crimson(){hit.set(5);ring.set(3.2f);bellStep[0]=Osc::step(1046.5f);bellStep[1]=Osc::step(1568.0f);}
  float next(float t) {
    if(t>=END)return 0;
    const float x=noise.next();float v=0;
    if(t<IGNITE+.05f) {
      // The wave: band noise opening upward, and a chord gliding up a fifth.
      const float s=clamp01(t*(1/RISE)),env=smooth(t*(1/.25f))*(1-smooth((t-IGNITE)*(1/.05f)));
      v+=.34f*env*(wide.next(x,.08f+.22f*s)-low.next(x,.03f));
      static constexpr float base[]={220.0f,261.6f,329.6f};
      for(int k=0;k<3;++k)v+=.11f*env*s*evaEdge(rise[k].next(base[k]*(1+.5f*s*s)));
    }
    if(t>=IGNITE) {
      if(!lit){hit.trigger();ring.trigger();lit=true;}
      const float a=t-IGNITE,attack=smooth(a*(1/.004f)),h=hit.next(),r=ring.next();
      v+=.62f*attack*h*evaEdge(boom.next(200+260*h));
      v+=attack*r*(.2f*bell[0].advance(bellStep[0])+.12f*bell[1].advance(bellStep[1]));
      // The choir: A4, C5, E5 with a slow tremble, rising in and fading out.
      static constexpr float hz[]={440.0f,523.3f,659.3f};
      const float env=smooth((a-.05f)*(1/.4f))*(1-smooth((t-HOLD)*(1/.7f)));
      for(int k=0;k<3;++k) {
        const float vib=1+.006f*(evaWobble(t,5.1f+k*.7f)-.5f);
        const float o=choir[k].next(hz[k]*vib);
        v+=env*(.13f+.05f*evaWobble(t,3.3f+k))*(o+.35f*choir[k].octave());
      }
    }
    return v*evaGate(t,END);
  }
};

// The barrier: a falling zap as each strike comes in, a hard inharmonic
// clang and a short shimmer as the octagons ring out; the third hit is
// lower, louder and longer, with a thump under it; silent by 2.4 s.
struct Bulwark {
  static constexpr int HITS=3,PARTIALS=4;
  static constexpr float AT[HITS]={.32f,.64f,1.0f};
  Noise noise{0xa7f1e1d5u};Smooth bright,band;Osc zap,clang[HITS][PARTIALS],thump;Decay ring[HITS][PARTIALS],burst[HITS],body;
  uint32_t clangStep[HITS][PARTIALS];bool struck[HITS]={};
  Bulwark() {
    static constexpr float hz[PARTIALS]={1210.0f,1847.0f,2633.0f,3709.0f},rate[PARTIALS]={6,8,11,15};
    for(int h=0;h<HITS;++h)for(int p=0;p<PARTIALS;++p) {
      const float scale=h==2?.72f:h==1?1.06f:1.0f;
      clangStep[h][p]=Osc::step(hz[p]*scale);ring[h][p].set(rate[p]*(h==2?.55f:1.0f));
    }
    for(int h=0;h<HITS;++h)burst[h].set(45);
    body.set(12);
  }
  static constexpr float END=2.4f;
  float next(float t) {
    if(t>=END)return 0;
    const float x=noise.next(),hi=x-bright.next(x,.5f);float v=0;
    for(int h=0;h<HITS;++h) {
      const float age=t-AT[h];
      if(age>=-.14f && age<0) { // the strike: a chirp falling as it closes in
        const float u=(age+.14f)*(1/.14f);
        v+=.22f*smooth(u*4)*zap.next(3200-2100*u)+.08f*u*hi;
      }
      if(age<0)continue;
      if(!struck[h]) {
        static constexpr float level[PARTIALS]={.3f,.22f,.16f,.1f};
        const float power=h==2?1.25f:.85f;
        for(int p=0;p<PARTIALS;++p)ring[h][p].trigger(level[p]*power);
        burst[h].trigger(power*.7f);if(h==2)body.trigger(.6f);
        struck[h]=true;
      }
      const float attack=smooth(age*(1/.002f));float c=0;
      for(int p=0;p<PARTIALS;++p)c+=ring[h][p].next()*clang[h][p].advance(clangStep[h][p]);
      // The ripple: a short tremolo shimmer on the ring.
      v+=attack*c*(age<.6f?.85f+.15f*evaWobble(age,18):1.0f)+burst[h].next()*band.next(hi,.6f);
    }
    if(t>=AT[2]) {
      const float a=t-AT[2];
      v+=body.next()*evaEdge(thump.next(330-110*clamp01(a*(1/.12f))));
    }
    return v*evaGate(t,END);
  }
};

// The cross: a core heartbeat quickening under a rising whine, then the
// detonation: a crash, a falling boom and a roar that holds while the cross
// blazes, crackling as it cools and dying away by 2.5 s.
struct Crux {
  static constexpr float DET=.36f,HOLD=1.3f;
  Noise noise{0xc205505eu};Smooth low,wide,crackF,roarLow;Osc beat,whine,boom;Decay pulse,crash,fall,crack;
  uint32_t slot=~0u,cslot=~0u;float cat=0,beatAt=0;bool clive=false,cfired=false,blown=false;
  static constexpr float END=2.5f;
  Crux(){pulse.set(30);crash.set(9);fall.set(4);crack.set(900);}
  float next(float t) {
    if(t>=END)return 0;
    const float x=noise.next();float v=0;
    if(t<DET+.02f) {
      // Heartbeats 6 to 20 a second, each a short 330 Hz thump.
      const float rate=6+40*t;const uint32_t now=uint32_t(t*rate);
      if(now!=slot){slot=now;beatAt=t;pulse.trigger(.35f+.4f*clamp01(t*(1/DET)));}
      v+=pulse.next()*smooth((t-beatAt)*(1/.003f))*evaEdge(beat.next(330));
      v+=.12f*smooth(t*(1/DET))*whine.next(700+1500*t*t*(1/(DET*DET)));
    }
    if(t>=DET) {
      if(!blown){crash.trigger();fall.trigger();blown=true;}
      const float a=t-DET,attack=smooth(a*(1/.003f)),f=fall.next();
      v+=attack*(.55f*crash.next()*x+.55f*f*evaEdge(boom.next(200+300*f)));
      // The roar: low band noise held while the cross burns.
      const float roar=smooth(a*(1/.05f))*(1-smooth((t-HOLD)*(1/.9f)));
      v+=.3f*roar*(wide.next(x,.18f)-roarLow.next(x,.025f));
      // Crackle, sparser as it cools.
      const float density=t<HOLD?30:30-20*clamp01((t-HOLD)*(1/.9f));const uint32_t now=uint32_t(t*density);
      if(now!=cslot){cslot=now;cat=(cslot+rnd(cslot,741))/density;clive=t<END-.2f && rnd(cslot,742)<.7f;cfired=false;}
      if(clive && !cfired && t>=cat){crack.trigger(.3f+.2f*rnd(cslot,743));cfired=true;}
      v+=crack.next()*crackF.next(x,.7f);
    }
    return v*evaGate(t,END);
  }
};

// The alarm: a soft rising sonar sweep while the scan runs, then a two-tone
// klaxon in step with the blinking frame for about a second, a ticking
// meter under it, and a falling tone; silent by 2.2 s.
struct Klaxon {
  static constexpr float ALERT=.45f,STAND=1.5f,END=2.2f;
  Noise noise{0xb10eb10eu};Smooth tickF;Osc sweep,horn,horn2,down;Decay tick,stab;
  uint32_t slot=~0u;bool alarmed=false;
  Klaxon(){tick.set(600);stab.set(14);}
  float next(float t) {
    if(t>=END)return 0;
    const float x=noise.next();float v=0;
    if(t<ALERT+.03f) {
      const float u=clamp01(t*(1/ALERT)),env=smooth(t*(1/.06f))*(1-smooth((t-ALERT)*(1/.03f)));
      v+=.26f*env*(.6f+.4f*evaWobble(t,24))*sweep.next(700+900*u*u);
    }
    if(t>=ALERT) {
      if(!alarmed){stab.trigger(.5f);alarmed=true;}
      const float a=t-ALERT,on=1-smooth((t-STAND)*(1/.25f));
      // High and low on the frame's 0.28 s blink; edges smoothed over 8 ms.
      const float phase=a-.28f*float(int(a*(1/.28f))),hiPart=smooth(phase*(1/.008f))*(1-smooth((phase-.19f)*(1/.008f)));
      const float hz=660+220*hiPart,o=horn.next(hz);
      v+=on*(.26f+.1f*hiPart)*(evaEdge(o)+.25f*horn.octave())+stab.next()*horn2.next(1760);
      // The meter ticks: about 14 a second.
      const uint32_t now=uint32_t(a*14);
      if(now!=slot){slot=now;if(t<STAND)tick.trigger(.12f);}
      v+=tick.next()*tickF.next(x,.8f);
    }
    if(t>=STAND && t<STAND+.55f) {
      const float a=t-STAND;
      v+=.26f*smooth(a*(1/.03f))*(1-smooth((a-.25f)*(1/.3f)))*evaEdge(down.next(880-500*clamp01(a*(1/.55f))));
    }
    return v*evaGate(t,END);
  }
};

// The beam: the gem's glassy hum and a whine climbing faster and faster as
// the particles draw in, with crystalline ticks; then the beam itself, a
// loud buzzing tone over hiss with a fast vibrato, cut off with a falling
// tone as a ring rolls away, and the hum fading out by 2.5 s.
struct Prism {
  static constexpr float CHARGE=.25f,FIRE=1.05f,CEASE=1.6f;
  Noise noise{0x0c7a4ed1u};Smooth hiss,hissLow,tickF;Osc hum[2],whine,beam,beam2,fall;Decay tick,blast;
  uint32_t humStep[2],slot=~0u;float at=0;bool live=false,fired=false,shot=false;
  static constexpr float END=2.5f;
  Prism(){humStep[0]=Osc::step(587.3f);humStep[1]=Osc::step(880.0f);tick.set(500);blast.set(10);}
  float next(float t) {
    if(t>=END)return 0;
    const float x=noise.next();float v=0;
    // The gem's hum, in from the start and out as it fades.
    const float gem=smooth(t*(1/.25f))*(1-smooth((t-1.85f)*(1/.5f)));
    v+=gem*(.09f*hum[0].advance(humStep[0])+.06f*hum[1].advance(humStep[1]))*(.75f+.25f*evaWobble(t,4));
    if(t>=CHARGE*.6f && t<FIRE) {
      const float u=clamp01((t-CHARGE*.6f)*(1/(FIRE-CHARGE*.6f))),pull=u*u*u;
      v+=.2f*smooth(u*5)*evaEdge(whine.next(400+2600*pull));
      // Crystalline ticks as the particles arrive, denser toward the shot.
      const float rate=10+50*u;const uint32_t now=uint32_t(t*rate);
      if(now!=slot){slot=now;at=(slot+rnd(slot,751))/rate;live=rnd(slot,752)<.7f;fired=false;}
      if(live && !fired && t>=at){tick.trigger(.22f);fired=true;}
    }
    v+=tick.next()*tickF.next(x,.85f);
    if(t>=FIRE && t<CEASE+.25f) {
      if(!shot){blast.trigger(.6f);shot=true;}
      const float a=t-FIRE,env=smooth(a*(1/.004f))*(t<CEASE?1:1-smooth((t-CEASE)*(1/.25f)));
      const float vib=1+.02f*(evaWobble(t,31)-.5f);
      const float o=beam.next(523.3f*vib),o2=beam2.next(784.0f*vib);
      v+=env*(.26f*evaEdge(o)+.14f*beam.octave()+.12f*o2+.16f*(hiss.next(x,.5f)-hissLow.next(x,.05f)));
      v+=blast.next()*x;
    }
    if(t>=CEASE && t<CEASE+.6f) {
      const float a=t-CEASE;
      v+=.22f*smooth(a*(1/.02f))*(1-smooth((a-.15f)*(1/.45f)))*fall.next(1200-800*clamp01(a*(1/.6f)));
    }
    return v*evaGate(t,END);
  }
};
}
}
