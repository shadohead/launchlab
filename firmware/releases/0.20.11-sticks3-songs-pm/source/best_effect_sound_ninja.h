#pragma once
// Naruto-inspired celebration sounds (Birdsong..Summon), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above ~200 Hz, timed to
// best_effect_ninja.h. Each is a build-up, a hit and a short tail, silent
// after its LENGTH (seconds) whatever the buffer length.
namespace BestEffectSound {
namespace detail {

// Birdsong: a flock of bird chirps (short upward sweeps) over an electric
// buzz, growing denser as the lightning gathers; a rising rush as it
// thrusts, a bright crack at the impact, then crackle dying away.
struct Birdsong {
  static constexpr float THRUST=1.05f,HIT=1.2f,LENGTH=2.4f;static constexpr int VOICES=4;
  Noise noise{0x6a09e667u};Smooth low,wide,hiss,tick;Osc chirp[VOICES],buzz,ring[3];
  Decay chirpEnv[VOICES],crack,spark,zap[3];float base[VOICES]={},age[VOICES]={},sweep[VOICES]={};
  uint32_t ringStep[3];int next_=0;bool struck=false;
  uint32_t slot=~0u,cslot=~0u;float at=0,cat=0;bool live=false,fired=false,clive=false,cfired=false;
  Birdsong() {
    for(int v=0;v<VOICES;++v)chirpEnv[v].set(38);
    crack.set(30);spark.set(1100);
    static constexpr float hz[]={2213.0f,3307.0f,4561.0f},rate[]={9,13,18};
    for(int k=0;k<3;++k){ringStep[k]=Osc::step(hz[k]);zap[k].set(rate[k]);}
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next(),hi=x-low.next(x,.3f);float v=0;
    const float gather=smooth(t*(1/.9f)),charging=t<HIT?1:0;
    // Chirps: one starts per slot, at a rate climbing from 14 to 46 a second.
    const float rate=t<HIT?14+32*gather:10;const uint32_t now=uint32_t(t*rate);
    if(now!=cslot){cslot=now;cat=(cslot+.8f*rnd(cslot,101))/rate;clive=(t<HIT+.05f || (t<1.9f && rnd(cslot,102)<.25f)) && rnd(cslot,103)<.9f;cfired=false;}
    if(clive && !cfired && t>=cat) {
      const int k=next_;next_=(next_+1)%VOICES;
      chirpEnv[k].trigger((t<HIT?.16f+.14f*gather:.12f)*(.7f+.3f*rnd(cslot,104)));
      base[k]=2300+1700*rnd(cslot,105);sweep[k]=.35f+.45f*rnd(cslot,106);age[k]=0;cfired=true;
    }
    for(int k=0;k<VOICES;++k) {
      const float e=chirpEnv[k].level;if(e<.002f)continue;
      // An upward tweet that bends back down at its end.
      const float a=age[k]*(1/.035f),bend=a<1?a*(2-a):1;
      v+=chirpEnv[k].next()*chirp[k].next(base[k]*(1+sweep[k]*bend));age[k]+=1.0f/RATE;
    }
    // Buzz: a raspy 240 Hz square under the chirps, gated by the crackle.
    const float sq=buzz.next(240)>0?1.0f:-1.0f;
    v+=(.05f+.07f*gather)*charging*smooth(t*(1/.1f))*sq*(.5f+.5f*x);
    v+=.22f*gather*charging*wide.next(hi,.5f);
    // The thrust: a rush of noise brightening into the hit.
    if(t>=THRUST-.05f && t<HIT+.05f){const float u=clamp01((t-THRUST+.05f)*(1/.2f));v+=.5f*u*(hiss.next(x,.2f+.5f*u));}
    if(t>=HIT) {
      static constexpr float level[]={.32f,.22f,.14f};
      if(!struck){crack.trigger(.85f);for(int k=0;k<3;++k)zap[k].trigger(level[k]);struck=true;}
      v+=crack.next()*(.6f*hi+.4f*x);
      for(int k=0;k<3;++k)v+=zap[k].next()*ring[k].advance(ringStep[k]);
    }
    // Crackle ticks while it gathers and after the hit.
    const uint32_t s=uint32_t(t*40);
    if(s!=slot){slot=s;at=(slot+rnd(slot,107))*(1/40.0f);live=(t<HIT?rnd(slot,108)<.3f+.5f*gather:t<2.2f && rnd(slot,108)<.55f*(1-smooth((t-HIT)*(1/1.0f))));fired=false;}
    if(live && !fired && t>=at){spark.trigger(.35f);fired=true;}
    return (v+spark.next()*tick.next(hi,.8f))*(1-smooth((t-LENGTH+.3f)*(1/.3f)));
  }
};

// Spiral: a whirring wind whose pulse and whine rise as the sphere spins
// up, a thump as it hits, a grinding buzz while it drills, then a whoosh and
// a falling whine as the spiral blast releases.
struct Spiral {
  static constexpr float FORM=.85f,HIT=1.0f,RELEASE=1.65f,LENGTH=2.8f;
  Noise noise{0xbb67ae85u};Smooth low,wide,grit,air;Osc whine,thump,whir;Decay hit,body,blast;
  bool struck=false,released=false;float spin=0;
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;Decay spark;Smooth tick;
  Spiral(){hit.set(16);body.set(11);blast.set(2.6f);spark.set(1000);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next(),band=wide.next(x,.16f)-low.next(x,.025f);float v=0;
    // The spin rate drives the wind's pulse (Hz) and the whine's pitch.
    const float rate=t<RELEASE?6+22*smooth(t*(1/1.2f)):28;spin+=rate*(1.0f/RATE);
    const float pulse=.55f+.45f*SINE[uint32_t(spin*1024)&1023];
    if(t<RELEASE) {
      const float env=smooth(t*(1/.25f))*(t<HIT?1:.8f);
      v+=.55f*env*pulse*band;
      const float w=whine.next(420+520*smooth(t*(1/1.1f)));v+=.12f*env*(w+.35f*w*w*w);
    }
    if(t>=HIT && t<RELEASE+.05f) {
      if(!struck){hit.trigger(.7f);body.trigger(.6f);struck=true;}
      v+=hit.next()*air.next(x,.5f)+body.next()*thump.next(340-110*clamp01((t-HIT)*(1/.12f)));
      // Grinding: noise chopped at 45 Hz with a little jitter.
      const float chop=SINE[int((t-HIT)*45*1024+300*x+1024)&1023]>0?1.0f:.25f;
      v+=.5f*smooth((t-HIT)*(1/.05f))*(1-smooth((t-RELEASE)*(1/.05f)))*chop*grit.next(x,.35f);
    }
    if(t>=RELEASE) {
      if(!released){blast.trigger(.8f);released=true;}
      const float u=t-RELEASE,b=blast.next();
      v+=.75f*b*(wide.y-low.y+.3f*x);
      const float w=whir.next(900-560*clamp01(u*(1/.7f)));v+=.18f*b*w;
    }
    const uint32_t s=uint32_t(t*36);
    if(s!=slot){slot=s;at=(slot+rnd(slot,111))*(1/36.0f);live=t>HIT && t<RELEASE && rnd(slot,112)<.7f;fired=false;}
    if(live && !fired && t>=at){spark.trigger(.3f);fired=true;}
    return (v+spark.next()*tick.next(x,.8f))*(1-smooth((t-LENGTH+.4f)*(1/.4f)));
  }
};

// Pinwheel: a dark swell and a metallic shing as the eyes open, swishes at
// every comma pass, quickening with the spin, a sucked-in rush into a deep
// hit, and a dissonant bell ringing out after the change.
struct Pinwheel {
  static constexpr float MORPH=1.3f,LENGTH=2.8f;static constexpr int BELL=4;
  Noise noise{0x3c6ef372u};Smooth low,wide,swish;Osc swell[2],shing[2],boom,bell[BELL];
  Decay open,hit,ring[BELL];uint32_t bellStep[BELL],shingStep[2];bool opened=false,struck=false;
  Pinwheel() {
    static constexpr float hz[]={1244.5f,1760.0f,2489.0f,3520.0f},rate[]={2.2f,3,4.2f,6};
    for(int k=0;k<BELL;++k){bellStep[k]=Osc::step(hz[k]);ring[k].set(rate[k]);}
    shingStep[0]=Osc::step(3951);shingStep[1]=Osc::step(5274);open.set(6);hit.set(6);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next(),band=wide.next(x,.2f)-low.next(x,.03f);float v=0;
    // The opening: a short, dark swell (D#4 against E4) and a shing.
    if(t<.9f) {
      const float env=smooth(t*(1/.15f))*(1-smooth((t-.3f)*(1/.6f)));
      const float a=swell[0].next(311),b=swell[1].next(331);v+=.18f*env*(a+b+.3f*a*a*a);
    }
    if(!opened){open.trigger(.3f);opened=true;}
    v+=open.next()*smooth(t*(1/.01f))*(shing[0].advance(shingStep[0])+.6f*shing[1].advance(shingStep[1]));
    // Swishes: one per comma passing the top, as the spin accelerates.
    if(t>.3f && t<MORPH) {
      const float u=(t-.3f)*(1/(MORPH-.3f)),passes=3*(.3f*u+1.9f*u*u*u);
      const float p=passes-uint32_t(passes),sw=p<.5f?SINE[uint32_t(p*1024)]:0;
      v+=.7f*sw*sw*swish.next(band,.6f);
    }
    // Sucked in: a reversed swell into the hit.
    if(t>MORPH-.3f && t<MORPH){const float u=(t-MORPH+.3f)*(1/.3f);v+=.7f*u*u*band;}
    if(t>=MORPH) {
      static constexpr float level[]={.32f,.24f,.17f,.11f};
      if(!struck){hit.trigger(.8f);for(int k=0;k<BELL;++k)ring[k].trigger(level[k]);struck=true;}
      const float h=hit.next();v+=h*boom.next(330-110*clamp01((t-MORPH)*(1/.25f)))+.4f*h*h*x;
      const float trem=.8f+.2f*SINE[uint32_t((t-MORPH)*5*1024)&1023];
      for(int k=0;k<BELL;++k)v+=trem*ring[k].next()*bell[k].advance(bellStep[k]);
    }
    return v*(1-smooth((t-LENGTH+.5f)*(1/.5f)));
  }
};

// Overdrive: a heartbeat thump on each of the eight beats over a roar that
// swells with the aura, a blast at the last, steam hissing away and pebbles
// clattering down.
struct Overdrive {
  static constexpr float BURST=1.03f,LENGTH=2.6f;
  Noise noise{0xa54ff53au};Smooth low,wide,steam,tick;Osc heart,boom,shimmer;Decay beat,blast,hissEnv,clack;
  int beats=0;bool burst=false;uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Overdrive(){beat.set(26);blast.set(4);hissEnv.set(1.8f);clack.set(700);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    static constexpr float BEATS[8]={.06f,.32f,.53f,.69f,.81f,.9f,.97f,1.03f};
    const float x=noise.next();float v=0;
    float power=0;for(float b:BEATS)power+=smooth((t-b)*(1/.06f))*.125f;
    if(beats<8 && t>=BEATS[beats]){beat.trigger(beats==7?.9f:.55f+.04f*beats);++beats;}
    const float since=beats?t-BEATS[beats-1]:0;
    // A thump: 300 Hz falling to 210 with its octave, and a click.
    const float hz=300-90*clamp01(since*(1/.06f)),b=beat.next(),h=heart.next(hz);
    v+=b*(h+.45f*heart.octave())+.25f*b*b*x;
    // The roar: band noise opening up as the gates open.
    const float roar=power*(1-smooth((t-1.5f)*(1/.7f)));
    v+=(.12f+.5f*roar)*roar*(wide.next(x,.08f+.3f*power)-low.next(x,.02f));
    if(t>=BURST) {
      if(!burst){blast.trigger(.9f);hissEnv.trigger(.4f);burst=true;}
      const float e=blast.next(),u=t-BURST;
      v+=e*(.6f*x+.5f*boom.next(320-120*clamp01(u*(1/.3f))));
      v+=.15f*e*shimmer.next(1500+400*e);
      v+=hissEnv.next()*(x-steam.next(x,.4f));
    }
    // Pebbles landing after the blast.
    const uint32_t s=uint32_t(t*22);
    if(s!=slot){slot=s;at=(slot+rnd(slot,121))*(1/22.0f);live=t>1.25f && t<2.1f && rnd(slot,122)<.55f;fired=false;}
    if(live && !fired && t>=at){clack.trigger(.35f);fired=true;}
    return (v+clack.next()*tick.next(x,.7f))*(1-smooth((t-LENGTH+.4f)*(1/.4f)));
  }
};

// Summon: a slap and thud as the palm hits, a scratchy brush and a rising
// shimmer as the script runs out, a chime as the ring closes, then a soft
// poof with a whump and a sparkle as the cloud bursts, thinning to a hiss.
struct Summon {
  static constexpr float SLAM=.04f,POOF=.62f,LENGTH=2.3f;static constexpr int CHIME=3;
  Noise noise{0x510e527fu};Smooth low,wide,poof,tick,brush;Osc thud,rise,whump,chime[CHIME],sparkle[2];
  Decay slap,body,scratch,cloud,drift,bloom,ring[CHIME],glit[2];Smooth haze,hazeLow;uint32_t chimeStep[CHIME],glitStep[2];
  bool slammed=false,closed=false,popped=false;uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Summon() {
    static constexpr float hz[]={1568.0f,2093.0f,3136.0f};
    for(int k=0;k<CHIME;++k){chimeStep[k]=Osc::step(hz[k]);ring[k].set(4.5f+2*k);}
    glitStep[0]=Osc::step(4186);glitStep[1]=Osc::step(5274);
    slap.set(60);body.set(18);scratch.set(500);cloud.set(2.2f);drift.set(1.1f);bloom.set(9);glit[0].set(8);glit[1].set(10);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t>=SLAM) {
      if(!slammed){slap.trigger(.8f);body.trigger(.7f);slammed=true;}
      v+=slap.next()*(x-low.next(x,.1f))+body.next()*thud.next(290-80*clamp01((t-SLAM)*(1/.08f)));
    }
    // The script: brush scratches at 50 a second and a rising shimmer.
    if(t>SLAM && t<POOF+.1f) {
      const uint32_t s=uint32_t(t*50);
      if(s!=slot){slot=s;at=(slot+rnd(slot,131))*(1/50.0f);live=t<.5f && rnd(slot,132)<.8f;fired=false;}
      if(live && !fired && t>=at){scratch.trigger(.3f+.15f*rnd(slot,133));fired=true;}
      v+=scratch.next()*brush.next(x-wide.next(x,.08f),.5f);
      const float u=clamp01((t-SLAM)*(1/(POOF-SLAM))),r=rise.next(600+900*u*u);
      v+=.14f*smooth(u*4)*(1-smooth((t-POOF)*(1/.08f)))*(r+.3f*r*r*r);
    }
    if(t>=.5f) {
      static constexpr float level[]={.16f,.12f,.08f};
      if(!closed){for(int k=0;k<CHIME;++k)ring[k].trigger(level[k]);closed=true;}
      for(int k=0;k<CHIME;++k)v+=ring[k].next()*chime[k].advance(chimeStep[k]);
    }
    if(t>=POOF) {
      if(!popped){cloud.trigger(.75f);drift.trigger(.22f);bloom.trigger(.7f);glit[0].trigger(.14f);glit[1].trigger(.1f);popped=true;}
      const float u=t-POOF,c=cloud.next();
      // The poof: noise through a low-pass closing down, and a whump.
      v+=c*poof.next(x,.5f*std::max(.08f,1-u*1.6f));
      // The cloud thinning: a breathy hiss that drifts away.
      v+=drift.next()*(haze.next(x,.35f)-hazeLow.next(x,.06f))*(.7f+.3f*SINE[uint32_t(u*3*1024)&1023]);
      v+=bloom.next()*whump.next(360-140*clamp01(u*(1/.15f)));
      for(int k=0;k<2;++k)v+=glit[k].next()*sparkle[k].advance(glitStep[k]);
    }
    return v*(1-smooth((t-LENGTH+.4f)*(1/.4f)));
  }
};
}
}
