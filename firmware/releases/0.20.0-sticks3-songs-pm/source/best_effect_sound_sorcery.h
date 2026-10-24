#pragma once
// Jujutsu Kaisen-inspired celebration sounds (Rupture..Ravage), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to the
// animation. Each is a hit with a short tail and is silent after LENGTH
// seconds, inside its effect's five-second animation.
namespace BestEffectSound {
namespace detail {
// The tail's fade: full until 0.3 s before the end, then smoothly to silence.
BEST_SOUND_INLINE float sorceryTail(float t,float length){return 1-smooth((t-length+.3f)*(1/.3f));}

// Rupture (Black Flash): crackling charge with a rising whine, then the hit:
// a noise crack, a crunching body falling in pitch, and an electric buzz
// with dense crackle that dies away.
struct Rupture {
  static constexpr float HIT=.7f,LENGTH=1.9f;
  Noise noise{0x6a09e667u};Smooth low,wide,tick,crack;Osc whine,body,buzz;Decay hit,thump,zap,spark;bool struck=false;
  uint32_t slot=~0u,buzzStep=Osc::step(233);float at=0;bool live=false,fired=false;
  Rupture(){hit.set(32);thump.set(8);zap.set(3.2f);spark.set(1100);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t<HIT) {
      const float s=t*(1/HIT),band=wide.next(x,.12f+.3f*s)-low.next(x,.08f);
      v+=(.18f+.32f*s*s)*band+.09f*s*smooth(t*(1/.2f))*whine.next(300+700*s*s);
    }
    if(t>=HIT) {
      if(!struck){hit.trigger(1);thump.trigger(.85f);zap.trigger(.5f);struck=true;}
      const float a=t-HIT,hz=520-300*clamp01(a*(1/.14f)),y=body.next(hz),crunch=1.6f*y-.6f*y*y*y;
      const float sq=buzz.advance(buzzStep)>0?1.0f:-1.0f,z=zap.next();
      v+=.75f*hit.next()*crack.next(x,.55f)+.6f*thump.next()*smooth(a*(1/.002f))*crunch+.22f*z*sq*(.5f+.5f*x);
    }
    // Crackle: ticks growing denser up to the hit, then dying away.
    const float rate=t<HIT?18+50*t*(1/HIT):70,density=t<HIT?.7f:.85f*(1-smooth((t-HIT-.2f)*(1/.8f)));
    const uint32_t now=uint32_t(t*rate);
    if(now!=slot){slot=now;at=(slot+rnd(slot,51))/rate;live=rnd(slot,52)<density;fired=false;}
    if(live && !fired && t>=at){spark.trigger(.3f+.25f*rnd(slot,53));fired=true;}
    v+=spark.next()*(x-tick.next(x,.35f));
    return v*sorceryTail(t,LENGTH);
  }
};
// Boundless (the first Unlimited Void): a breath drawn in and a rising whine
// as the eyes open and glow; a hard white flash at the burst (a noise crack and a
// glassy ping); a bright wash as the white floods the screen; soft thuds of
// ink as the blots swallow it; then the void opens on a deep, slowly beating
// chord with a high shimmer of pings that thins out (the close is silent:
// sounds stop by 2.8 s).
struct Boundless {
  static constexpr float BURST=.45f,WHITE=.55f,HOLES=.74f,VOID=1.08f,LENGTH=2.8f;static constexpr int PAD=4,BLOTS=8;
  Noise noise{0xbb67ae85u};Smooth low,wide,thud,air,wash,washLow;Osc whine,ping,pad[PAD],shimmer,drop;
  Decay crack,glass,flood,blot,sparkle;
  uint32_t padStep[PAD],pingStep=Osc::step(3951.1f),slot=~0u,sparkStep=0;int blotted=0;
  bool burst=false,flooded=false;float at=0;bool live=false,fired=false;
  Boundless() {
    // A, E, A, C#: a wide A major, all above 200 Hz.
    static constexpr float chord[]={220.0f,329.6f,440.0f,554.4f};
    for(int k=0;k<PAD;++k)padStep[k]=Osc::step(chord[k]);
    crack.set(40);glass.set(5);flood.set(9);blot.set(22);sparkle.set(40);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The eyes: a breath drawn in, the whine rising under it.
    if(t<BURST) {
      const float s=t*(1/BURST);
      v+=.22f*s*s*(wide.next(x,.18f+.3f*s)-low.next(x,.06f))+.1f*smooth(s*3)*whine.next(420+680*s*s);
    }
    // The flash: a crack of noise and a bright glass ping.
    if(t>=BURST) {
      if(!burst){crack.trigger(1);glass.trigger(.35f);burst=true;}
      v+=.7f*crack.next()*(x-air.next(x,.2f))+glass.next()*ping.advance(pingStep);
    }
    // The white floods the screen: a bright wash of noise.
    if(t>=WHITE && !flooded){flood.trigger(.5f);flooded=true;}
    if(flood.level>.002f)v+=flood.next()*(wash.next(x,.6f)-washLow.next(x,.15f));
    // Ink: a soft, low thud as each blot blooms.
    const int due=t<HOLES?0:std::min(BLOTS,1+int((t-HOLES)*(1/.03f)));
    if(due>blotted){blot.trigger(.4f-.025f*blotted);blotted=due;}
    if(blot.level>.002f)v+=blot.next()*(1.4f*thud.next(x,.05f)+.25f*drop.next(240+70*blotted));
    // The void: a deep chord beating slowly, and a high shimmer of pings.
    if(t>=VOID-.05f) {
      const float a=t-VOID,env=smooth((a+.05f)*(1/.25f))*(1-smooth((t-2.0f)*(1/.8f)));
      const float beat=.75f+.25f*SINE[uint32_t(std::max(0.0f,a)*1.7f*1024)&1023];
      float chord=0;for(int k=0;k<PAD;++k)chord+=pad[k].advance(padStep[k]);
      v+=.13f*env*beat*chord;
      const float density=.8f*smooth((a-.1f)*(1/.2f))*(1-smooth((t-1.9f)*(1/.6f)));
      const uint32_t now=uint32_t(t*18);
      if(now!=slot){slot=now;at=(slot+rnd(slot,71))*(1/18.0f);live=rnd(slot,72)<density;fired=false;}
      if(live && !fired && t>=at){static constexpr float scale[]={1760.0f,2217.5f,2637.0f,3520.0f,4434.9f};
        sparkle.trigger(.08f+.05f*rnd(slot,73));sparkStep=Osc::step(scale[hash(slot*5+3)%5]);fired=true;}
    }
    if(sparkle.level>.001f)v+=sparkle.next()*shimmer.advance(sparkStep);
    return v*sorceryTail(t,LENGTH);
  }
};
// Precision (the 7:3 cut): ten ticks of the ruler, a two-note lock, then the
// strike: a short swish, a bright inharmonic ring and a knock.
struct Precision {
  static constexpr float TICK0=.3f,TICKSTEP=.045f,MARK=.85f,STRIKE=1.2f,LENGTH=2.0f;static constexpr int PARTIALS=4;
  Noise noise{0x3c6ef372u};Smooth low,wide,hp;Osc click,lock,ring[PARTIALS],knock;Decay tick,beep,shing[PARTIALS],thud;
  uint32_t ringStep[PARTIALS];int ticked=-1,locked=0;bool cut=false;float clickHz=2600,lockHz=1318.5f;
  Precision() {
    static constexpr float hz[]={2093.0f,3170.0f,4430.0f,5920.0f},rate[]={5,7,10,14};
    for(int k=0;k<PARTIALS;++k){ringStep[k]=Osc::step(hz[k]);shing[k].set(rate[k]);}
    tick.set(140);beep.set(16);thud.set(20);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // Ticks: one per tenth, the seventh higher.
    const int i=t<TICK0?-1:std::min(10,int((t-TICK0)*(1/TICKSTEP)));
    if(i>ticked && i>=0){ticked=i;tick.trigger(i==7?.55f:.38f);clickHz=i==7?3520:2600;}
    v+=tick.level>.001f?tick.next()*(.7f*click.next(clickHz)+.3f*(x-hp.next(x,.4f))):0;
    // The lock: two short notes.
    if(t>=MARK && locked==0){beep.trigger(.3f);lockHz=1318.5f;locked=1;}
    if(t>=MARK+.1f && locked==1){beep.trigger(.3f);lockHz=1760;locked=2;}
    if(beep.level>.001f)v+=beep.next()*lock.next(lockHz);
    // The strike: a swish into the cut, the ring and a knock.
    if(t>=STRIKE-.07f && t<STRIKE+.04f) {
      const float u=(t-STRIKE+.07f)*(1/.11f);
      v+=.55f*SINE[uint32_t(u*512)&1023]*(wide.next(x,.2f+.5f*u)-low.next(x,.08f));
    }
    if(t>=STRIKE) {
      static constexpr float level[]={.34f,.25f,.17f,.11f};
      if(!cut){for(int k=0;k<PARTIALS;++k)shing[k].trigger(level[k]);thud.trigger(.6f);cut=true;}
      const float attack=smooth((t-STRIKE)*(1/.002f));
      for(int k=0;k<PARTIALS;++k)v+=attack*shing[k].next()*ring[k].advance(ringStep[k]);
      v+=attack*thud.next()*knock.next(300-100*clamp01((t-STRIKE)*(1/.1f)));
    }
    return v*sorceryTail(t,LENGTH);
  }
};
// Umbra (Chimera Shadow Garden): a murky gurgle swelling as the shadow
// floods, bubbles plopping in it, a rough growl as the creatures rise, and
// three soft glints as their eyes open.
struct Umbra {
  static constexpr float RISE=.9f,LENGTH=2.2f;static constexpr int EYES=3;
  Noise noise{0xa54ff53au};Smooth low,wide;Osc bubble,growlA,growlB,glint[EYES];Decay plop,shine[EYES];
  uint32_t glintStep[EYES],slot=~0u;float at=0,pitch=400;bool live=false,fired=false,opened[EYES]={};
  Umbra() {
    static constexpr float hz[]={2637.0f,3136.0f,3520.0f};
    for(int k=0;k<EYES;++k){glintStep[k]=Osc::step(hz[k]);shine[k].set(9);}
    plop.set(28);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The gurgle: noise in a low-middle band, wobbling.
    const float flood=smooth(t*(1/.5f))*(1-smooth((t-1.2f)*(1/.8f)));
    const float wobble=.65f+.35f*SINE[uint32_t(t*3.3f*1024)&1023];
    v+=.6f*flood*wobble*(wide.next(x,.16f)-low.next(x,.08f));
    // Bubbles: short rising sines.
    const uint32_t now=uint32_t(t*13);
    if(now!=slot){slot=now;at=(slot+rnd(slot,71))*(1/13.0f);live=t<1.6f && rnd(slot,72)<.75f;fired=false;}
    if(live && !fired && t>=at){plop.trigger(.32f);pitch=320+260*rnd(slot,73);fired=true;}
    const float d=plop.level;
    if(d>.001f)v+=plop.next()*bubble.next(pitch*(1.9f-d*2.8f));
    // The growl: two detuned voices, waveshaped and roughened at 19 Hz.
    if(t>=RISE-.1f) {
      const float g=smooth((t-RISE+.1f)*(1/.3f))*(1-smooth((t-1.35f)*(1/.5f)));
      const float rough=.6f+.4f*SINE[uint32_t(t*19*1024)&1023],a=growlA.next(222),b=growlB.next(224.5f);
      const float y=a+b;v+=.24f*g*rough*(y+.35f*y*y-.15f*y*y*y);
    }
    // Eyes opening: three soft glints.
    for(int k=0;k<EYES;++k) {
      const float when=1.3f+.1f*k;if(t<when)continue;
      if(!opened[k]){shine[k].trigger(.16f);opened[k]=true;}
      v+=smooth((t-when)*(1/.004f))*shine[k].next()*glint[k].advance(glintStep[k]);
    }
    return v*sorceryTail(t,LENGTH);
  }
};
// Ravage (Malevolent Shrine): a lub-dub heartbeat, then a storm of slashes
// (bright noise bursts, each with a short ring), and the final crossed cut:
// a long inharmonic ring over a crash and a falling boom.
struct Ravage {
  static constexpr float BARRAGE=.5f,FINAL=1.55f,LENGTH=2.6f;static constexpr int PARTIALS=4;
  Noise noise{0x510e527fu};Smooth low,hp,crashLp;Osc beat,slashRing,ring[PARTIALS],boom;Decay heart,cutEnv,ringEnv,shing[PARTIALS],crash,body;
  uint32_t ringStep[PARTIALS],slot=~0u,slashStep=0;float at=0,beatHz=300;bool live=false,fired=false,cut=false,second=false;int beats=0;
  Ravage() {
    static constexpr float hz[]={1709.0f,2467.0f,3613.0f,5129.0f},rate[]={2.6f,3.6f,5,7.5f};
    for(int k=0;k<PARTIALS;++k){ringStep[k]=Osc::step(hz[k]);shing[k].set(rate[k]);}
    heart.set(16);cutEnv.set(70);ringEnv.set(45);crash.set(9);body.set(6);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // Heartbeat: lub-dub at 0 and 0.8 s.
    static constexpr float BEATS[]={0,.17f,.8f,.97f};
    if(beats<4 && t>=BEATS[beats]){heart.trigger(beats%2?.38f:.6f);beatHz=beats%2?260:300;++beats;}
    if(heart.level>.001f)v+=heart.next()*beat.next(beatHz*(.75f+.25f*heart.level));
    // The storm of slashes.
    const float storm=smooth((t-BARRAGE)*(1/.3f))*(1-smooth((t-FINAL+.15f)*(1/.15f)));
    const uint32_t now=uint32_t(t*22);
    if(now!=slot){slot=now;at=(slot+rnd(slot,81))*(1/22.0f);live=rnd(slot,82)<.85f*storm;fired=false;}
    if(live && !fired && t>=at){cutEnv.trigger(.5f+.3f*rnd(slot,83));ringEnv.trigger(.18f);slashStep=Osc::step(2400+2600*rnd(slot,84));fired=true;}
    if(cutEnv.level>.001f)v+=cutEnv.next()*(x-hp.next(x,.3f))+ringEnv.next()*slashRing.advance(slashStep);
    // The final cut, and the second a beat later.
    if(t>=FINAL) {
      static constexpr float level[]={.3f,.22f,.15f,.1f};
      if(!cut){for(int k=0;k<PARTIALS;++k)shing[k].trigger(level[k]);crash.trigger(.6f);body.trigger(.55f);cut=true;}
      if(t>=FINAL+.05f && !second){for(int k=0;k<PARTIALS;++k)shing[k].trigger(shing[k].level+level[k]*.6f);crash.trigger(crash.level+.35f);second=true;}
      const float attack=smooth((t-FINAL)*(1/.002f));
      for(int k=0;k<PARTIALS;++k)v+=attack*shing[k].next()*ring[k].advance(ringStep[k]);
      v+=crash.next()*crashLp.next(x,.5f)+attack*body.next()*boom.next(340-120*clamp01((t-FINAL)*(1/.3f)));
    }
    return v*sorceryTail(t,LENGTH);
  }
};
}
}
