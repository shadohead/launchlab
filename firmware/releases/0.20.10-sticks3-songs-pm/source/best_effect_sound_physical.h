#pragma once
// Physical celebration sounds, in the style of best_effect_sound.h, which
// includes this file before the Synth. Timings match best_effect_physical.h.
namespace BestEffectSound {
namespace detail {

// A tense cord slide, a ripcord zip at the yank, a whir rising and pulsing
// faster as the top spins up, a bright lock chime at 1.35 s, a held grinding
// whir, then a wobbling hum that runs down as it settles.
struct Spin {
  // The effect now holds its spin to 3.45 s, but the sound stays a hit and
  // a short tail: the held whir runs to 2.2 s, then sags into the wobble
  // and is done by the 2.6 s sound length.
  static constexpr float YANK=.3f,LOCK=1.35f,HELD=2.2f;
  Noise noise{0x51ed27u};Smooth low,wide;Osc whir,ring[2];Decay chime,wobble;
  uint32_t ringStep[2];bool locked=false,settled=false;
  Spin(){chime.set(5);wobble.set(3.4f);ringStep[0]=Osc::step(1568.0f);ringStep[1]=Osc::step(2350.0f);}
  float next(float t) {
    const float x=noise.next();float v=0;
    if(t<.58f) { // cord: a quiet tension slide, then the brightening yank zip
      const float slide=t<YANK?.25f*smooth(t*(1/.1f)):0.0f;
      const float zip=t>=YANK?smooth((t-YANK)*(1/.04f))*(1-smooth((t-.48f)*(1/.1f))):0.0f;
      const float bright=.25f+.55f*clamp01((t-YANK)*(1/.25f));
      v+=(slide+.6f*zip)*(wide.next(x,bright)-low.next(x,.05f));
    }
    if(t>=YANK && t<HELD) { // whir: pitch and blade-pass pulse climb as s^2, then hold
      const float s=std::min(t,LOCK)<=YANK?0:clamp01((std::min(t,LOCK)-YANK)/(LOCK-YANK)),passes=9*s*s;
      const float pulse=.6f+.4f*SINE[uint32_t(passes*1024)&1023];
      const float a=.34f*smooth((t-YANK)*(1/.15f))*(1-smooth((t-HELD+.1f)*(1/.1f)))*pulse;
      const float o=whir.next(220+1650*s*s);
      v+=a*(o+.3f*o*o*o);
      // Tip grind under the held whir: a faint gritty band.
      if(t>LOCK)v+=.1f*(wide.next(x,.5f)-low.next(x,.1f));
    }
    if(t>=LOCK) {
      if(!locked){chime.trigger();locked=true;}
      const float env=smooth((t-LOCK)*(1/.004f))*chime.next();
      v+=.4f*env*ring[0].advance(ringStep[0])+.24f*env*ring[1].advance(ringStep[1]);
    }
    if(t>=HELD) {
      // Settling run-down: the whir's pitch sags and a low wobble takes over.
      const float a=t-HELD,sag=clamp01(1-a*.55f);
      if(!settled){wobble.trigger();settled=true;}
      const float wob=.5f+.5f*SINE[uint32_t(a*2118.6f)&1023];
      v+=.16f*wobble.next()*wob*whir.next(196+320*sag*sag);
    }
    return v;
  }
};

// Low wind gathers, a metallic tick sounds as each cut opens, a near-silent
// held beat, then every cut detonates in one glassy crash and shards tinkle
// down for the long settle.
struct Storm {
  static constexpr float DETONATE=1.75f;
  Noise noise{0x27d4eb99u};Smooth low,wide,tick;Osc ring[3];Decay cutRing,crash,shard;
  uint32_t ringStep[3];bool burst=false;
  uint32_t slot=~0u,slot2=~0u;float at=0,at2=0;bool live=false,fired=false,live2=false,fired2=false;
  Storm(){
    static constexpr float hz[]={2093.0f,2960.0f,4186.0f};
    for(int k=0;k<3;++k)ringStep[k]=Osc::step(hz[k]);
    cutRing.set(28);crash.set(7);shard.set(900);
  }
  float next(float t) {
    const float x=noise.next();float v=0;
    // Wind: band noise swelling to the detonation, held low in the beat.
    const float env=t<DETONATE?.12f+.3f*smooth(t*(1/DETONATE)):0.0f;
    const float hush=t>1.35f && t<DETONATE?.18f:1.0f;
    v+=env*hush*(wide.next(x,.16f)-low.next(x,.03f));
    // One tick per cut as they open between 0.45 s and 1.35 s.
    const uint32_t now=uint32_t(t*16);
    if(now!=slot){slot=now;at=(slot+rnd(slot,301))*(1/16.0f);live=t>.45f && t<1.35f && rnd(slot,302)<.7f;fired=false;}
    if(live && !fired && t>=at){cutRing.trigger(.5f);fired=true;}
    v+=cutRing.next()*tick.next(x,.8f);
    if(t>=DETONATE) {
      if(!burst){crash.trigger();burst=true;}
      const float attack=smooth((t-DETONATE)*(1/.003f)),e=crash.next();
      v+=attack*e*(.5f*x+.26f*ring[0].advance(ringStep[0])+.18f*ring[1].advance(ringStep[1])+.12f*ring[2].advance(ringStep[2]));
      // Falling shards: sparse bright ticks, thinning as they settle.
      const uint32_t s2=uint32_t(t*24);
      if(s2!=slot2){slot2=s2;at2=(s2+rnd(s2,303))*(1/24.0f);live2=t<2.5f && rnd(s2,304)<.55f-.25f*clamp01((t-DETONATE)*(1/.75f));fired2=false;}
      if(live2 && !fired2 && t>=at2){shard.trigger(.3f);fired2=true;}
      v+=shard.next()*tick.next(x,.85f);
    }
    return v;
  }
};

// Reticle clicks as the brackets settle, three gunshots (crack, thump and a
// casing tink after each), and a final lock click, with smoke hiss after.
struct Quickdraw {
  static constexpr float SHOTS[]={.75f,1.05f,1.35f},LOCK=1.55f;
  Noise noise{0x85eb1234u};Smooth bright,tickF;Osc thump,tink,clickOsc;
  Decay crack[3],body[3],ping[3],snap;bool firedS[3]={},tinked[3]={},locked=false;
  uint32_t slot=~0u;float at=0;bool live=false,clicked=false;Decay tickD;
  Quickdraw(){for(int s=0;s<3;++s){crack[s].set(55);body[s].set(16);ping[s].set(24);}snap.set(80);tickD.set(700);}
  float next(float t) {
    const float x=noise.next(),hi=x-bright.next(x,.55f);float v=0;
    // Faint mechanical ticks while the reticle slides in, and one last
    // settle tick in the held aim just before the first shot.
    const uint32_t now=uint32_t(t*18);
    if(now!=slot){slot=now;at=(slot+rnd(slot,311))*(1/18.0f);live=(t<.6f || (t>.62f && t<.72f)) && rnd(slot,312)<.55f;clicked=false;}
    if(live && !clicked && t>=at){tickD.trigger(.22f);clicked=true;}
    v+=tickD.next()*tickF.next(x,.75f);
    for(int s=0;s<3;++s) {
      const float age=t-SHOTS[s];if(age<0)continue;
      if(!firedS[s]){crack[s].trigger();body[s].trigger(.85f);firedS[s]=true;}
      if(age<.4f) {
        const float hz=320-160*clamp01(age*(1/.09f));
        v+=.85f*crack[s].next()*hi+.6f*body[s].next()*thump.next(hz);
      }
      if(age>=.16f && !tinked[s]){ping[s].trigger(.2f);tinked[s]=true;}
      if(tinked[s] && age<.5f)v+=ping[s].next()*tink.next(3950+s*320);
    }
    if(t>=LOCK) {
      if(!locked){snap.trigger(.45f);locked=true;}
      v+=snap.next()*(.6f*clickOsc.next(2900)+.35f*x);
    }
    // A soft breath of smoke hiss curls off the holes and muzzles.
    if(t>1.4f && t<2.25f)v+=.08f*(1-(t-1.4f)/.85f)*hi;
    return v;
  }
};

// Gentle leaf rustle while they fall, two blade whooshes at the cuts, then
// softer fluttering rustle as the halves tumble away.
struct Leaves {
  static constexpr float CUT=1.15f,CUT2=1.28f;
  Noise noise{0x9e37c001u};Smooth low,wide,bright;Osc ring;Decay zing;
  bool rung=false;
  Leaves(){zing.set(11);}
  float next(float t) {
    const float x=noise.next();float v=0;
    // Rustle: band noise breathing in slow swells, softer after the cut.
    const float swell=.65f+.35f*SINE[uint32_t(t*423.7f)&1023];
    const float env=smooth(t*(1/.3f))*(t<CUT?.5f:.34f)*(1-smooth((t-2.05f)*(1/.35f)));
    v+=env*swell*(wide.next(x,.24f)-low.next(x,.035f));
    // Each blade pass: a fast brightening whoosh.
    for(int c=0;c<2;++c) {
      const float start=c?CUT2:CUT,age=t-start;
      if(age<0 || age>=.22f)continue;
      const float s2=smooth(age*(1/.09f))*(1-smooth((age-.1f)*(1/.12f)));
      v+=.6f*s2*(x-bright.next(x,.25f+.55f*clamp01(age*(1/.09f))));
    }
    if(t>=CUT+.02f) { // one soft high ring as the first cut lands
      if(!rung){zing.trigger(.3f);rung=true;}
      v+=zing.next()*ring.next(3136);
    }
    return v;
  }
};

// Warm pentatonic pings rise as the buds open, a gentle held shimmer at full
// bloom, a soft pop at the burst, and sparse descending pings as petals fall.
struct Bloom {
  static constexpr float BURST=1.65f;
  static constexpr int NOTES=14;
  Noise noise{0xc2b2b10bu};Smooth puff;Osc osc[NOTES],popOsc;Decay env[NOTES],pop;
  float start[NOTES];uint32_t inc[NOTES];bool rung[NOTES]={},burst=false;
  Bloom() {
    static constexpr float scale[]={1318.5f,1568.0f,1760.0f,2093.0f,2349.3f,2637.0f,3136.0f};
    for(int p=0;p<NOTES;++p){
      if(p<7){start[p]=.5f+.13f*p;inc[p]=Osc::step(scale[p]);} // opening: rising
      else if(p<9){start[p]=1.38f+.14f*(p-7);inc[p]=Osc::step(scale[5+(p-7)]);} // held bloom shimmer
      else{start[p]=BURST+.1f+.55f*rnd(p,321);inc[p]=Osc::step(scale[6-(p-9)%5]);} // falling
      env[p].set(8);
    }
    pop.set(20);
  }
  float next(float t) {
    float v=0;
    for(int p=0;p<NOTES;++p) {
      const float age=t-start[p];if(age<0 || age>.55f)continue;
      if(!rung[p]){env[p].trigger(p<9?.3f:.2f);rung[p]=true;}
      const float o=osc[p].advance(inc[p]);
      v+=smooth(age*(1/.004f))*env[p].next()*(o+.22f*osc[p].octave());
    }
    if(t>=BURST) {
      if(!burst){pop.trigger(.7f);burst=true;}
      const float age=t-BURST,hz=520-260*clamp01(age*(1/.1f));
      v+=pop.next()*(.55f*popOsc.next(hz)+.3f*puff.next(noise.next(),.3f));
    }
    return v;
  }
};
}
}
