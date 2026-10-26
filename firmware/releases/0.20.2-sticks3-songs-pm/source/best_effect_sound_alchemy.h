#pragma once
// The alchemy set's celebration sounds (Transmute..Fracture), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to the
// animation; a hit with a short tail, silent after its soundDuration().
namespace BestEffectSound {
namespace detail {
// The tail's fade: full until 0.3 s before the end, then smoothly to silence.
BEST_SOUND_INLINE float alchemyTail(float t,float length){return 1-smooth((t-length+.3f)*(1/.3f));}

// Transmute: the pen scratches the circle in (band noise in quick jittery
// strokes) over a glassy hum that rises as it closes, a tick per rune, then
// the activation: a bright chord and a falling whump under a burst of
// electric crackle that thins out.
struct Transmute {
  static constexpr float ACT=1.1f,LENGTH=2.4f;static constexpr int CHORD=4;
  Noise noise{0x3c6ef372u};Smooth low,wide,tick,bite;Osc hum,chord[CHORD],whump,pip;Decay ring[CHORD],thump,crack,spark,rune;
  uint32_t chordStep[CHORD],pipStep=Osc::step(3520),slot=~0u;float at=0;bool live=false,fired=false,rang=false;int runes=0;
  Transmute() {
    static constexpr float hz[]={880.0f,1108.7f,1318.5f,1760.0f},rate[]={2.6f,3.2f,3.8f,5.0f};
    for(int k=0;k<CHORD;++k){chordStep[k]=Osc::step(hz[k]);ring[k].set(rate[k]);}
    thump.set(9);crack.set(5);spark.set(900);rune.set(60);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t<ACT) {
      // The pen: scratchy strokes, each about 70 ms, louder as the strokes overlap.
      const float stroke=SINE[uint32_t(t*14.3f*1024)&1023],grit=.5f+.5f*stroke*stroke;
      const float pen=smooth((t-.06f)*(1/.05f))*(1-smooth((t-.95f)*(1/.1f)))*(t>.6f?1.25f:1);
      v+=.34f*pen*grit*(wide.next(x,.55f)-low.next(x,.18f));
      const float s=clamp01((t-.3f)*(1/.8f));
      v+=.1f*s*s*smooth((t-.3f)*(1/.2f))*hum.next(440+440*s);
      // A tick per rune as it is written.
      const int due=std::min(12,std::max(0,int((t-.72f)*(1/.026f))+1));
      if(due>runes){runes=due;rune.trigger(.22f);}
    }
    if(rune.level>.001f)v+=rune.next()*pip.advance(pipStep);
    if(t>=ACT) {
      static constexpr float level[]={.24f,.18f,.15f,.1f};
      if(!rang){for(int k=0;k<CHORD;++k)ring[k].trigger(level[k]);thump.trigger(.7f);crack.trigger(1);rang=true;}
      const float a=t-ACT,attack=smooth(a*(1/.004f));
      for(int k=0;k<CHORD;++k)v+=attack*ring[k].next()*chord[k].advance(chordStep[k]);
      v+=thump.next()*attack*whump.next(620-360*clamp01(a*(1/.12f)));
      v+=.35f*crack.next()*(x-bite.next(x,.3f));
    }
    // Crackle: sparse sparks as the circle closes, a dense burst at the
    // activation, then a spark now and then.
    const float density=t<.85f?0:t<ACT?.3f:t<ACT+.7f?.9f:.35f*(1-smooth((t-1.9f)*(1/.4f)));
    const uint32_t now=uint32_t(t*45);
    if(now!=slot){slot=now;at=(slot+rnd(slot,71))*(1/45.0f);live=rnd(slot,72)<density;fired=false;}
    if(live && !fired && t>=at){spark.trigger(.25f+.25f*rnd(slot,73));fired=true;}
    v+=spark.next()*(x-tick.next(x,.4f));
    return v*alchemyTail(t,LENGTH);
  }
};
// Ignition: a dry finger snap, a fizzing spark racing off it (bright noise
// and ticks, rising), then the ignition: a whoomph of air (a noise burst
// with a falling body) and a fire roar that crackles and dies down.
struct Ignition {
  static constexpr float SNAP=.12f,RUN=.16f,IGN=.55f,LENGTH=2.2f;
  Noise noise{0xa54ff53au};Smooth low,wide,fizzLow,roarLow,roarWide,tick,click;Osc body,ping;Decay snap,pop,whoomph,thud,spark;
  uint32_t slot=~0u;float at=0;bool live=false,fired=false,snapped=false,lit=false;
  Ignition(){snap.set(140);pop.set(45);whoomph.set(4.5f);thud.set(11);spark.set(1000);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t>=SNAP) {
      if(!snapped){snap.trigger(1);pop.trigger(.4f);snapped=true;}
      v+=.75f*snap.next()*(x-click.next(x,.25f))+pop.next()*ping.next(2350);
    }
    // The run: a thin fizz rising in brightness until the ignition.
    if(t>=RUN && t<IGN+.05f) {
      const float s=clamp01((t-RUN)*(1/(IGN-RUN)));
      v+=(.12f+.16f*s)*(1-smooth((t-IGN)*(1/.05f)))*(x-fizzLow.next(x,.35f+.3f*s));
    }
    if(t>=IGN) {
      if(!lit){whoomph.trigger(1);thud.trigger(.8f);lit=true;}
      const float a=t-IGN,band=roarWide.next(x,.18f)-roarLow.next(x,.07f);
      v+=.85f*whoomph.next()*smooth(a*(1/.03f))*band;
      v+=thud.next()*smooth(a*(1/.004f))*body.next(400-180*clamp01(a*(1/.2f)));
      // The fire burns on, lower.
      v+=.32f*smooth((a-.15f)*(1/.2f))*(1-smooth((t-1.3f)*(1/.7f)))*(wide.next(x,.22f)-low.next(x,.07f));
    }
    // Crackle: through the run, then in the fire.
    const float density=t<RUN?0:t<IGN?.8f:.7f*(1-smooth((t-1.4f)*(1/.6f)));
    const uint32_t now=uint32_t(t*(t<IGN?60:35));
    if(now!=slot){slot=now;at=(slot+rnd(slot,81))*(t<IGN?1/60.0f:1/35.0f);live=rnd(slot,82)<density;fired=false;}
    if(live && !fired && t>=at){spark.trigger(.22f+.25f*rnd(slot,83));fired=true;}
    v+=spark.next()*(x-tick.next(x,.4f));
    return v*alchemyTail(t,LENGTH);
  }
};
// Spires: a sharp clap, a thump as the palms press the floor, an electric
// zip racing out along it, and a stone crunch for each spike as it bursts
// up (the middle first, then pairs outward), under a rumble that settles.
struct Spires {
  static constexpr float CLAP=.3f,PRESS=.45f,LENGTH=2.2f;static constexpr int N=9;
  Noise noise{0x510e527fu};Smooth low,wide,crunchLow,crunchWide,rumbleLow,rumbleWide,tick,zipLow;Osc body,stone[N];
  Decay clap,thump,crunch[N],spark;float at[N];bool clapped=false,pressed=false,rose[N]={};
  uint32_t slot=~0u;float sparkAt=0;bool live=false,fired=false;
  Spires() {
    clap.set(60);thump.set(14);spark.set(1000);
    for(int i=0;i<N;++i){const float x=7+i*(121.0f/(N-1));at[i]=PRESS+(x>67.5f?x-67.5f:67.5f-x)*(1/240.0f);crunch[i].set(16);}
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The palms rush in.
    if(t<CLAP)v+=.16f*smooth(t*(1/.2f))*(wide.next(x,.4f)-low.next(x,.15f));
    if(t>=CLAP){if(!clapped){clap.trigger(1);clapped=true;}v+=.8f*clap.next()*(x-tick.next(x,.2f));}
    if(t>=PRESS) {
      if(!pressed){thump.trigger(.9f);pressed=true;}
      const float a=t-PRESS;
      v+=thump.next()*smooth(a*(1/.003f))*body.next(330-120*clamp01(a*(1/.15f)));
      // The zip along the floor: bright noise for a quarter second.
      if(a<.3f)v+=.3f*(1-a*(1/.3f))*(x-zipLow.next(x,.5f+.3f*a));
      // The rumble of stone settling.
      v+=.3f*smooth(a*(1/.1f))*(1-smooth((t-1.0f)*(1/.9f)))*(rumbleWide.next(x,.14f)-rumbleLow.next(x,.06f));
    }
    for(int i=0;i<N;++i) {
      if(t<at[i])continue;
      if(!rose[i]){crunch[i].trigger(.5f+.1f*rnd(i,91));rose[i]=true;}
      if(crunch[i].level<.002f)continue;
      const float c=crunch[i].next();v+=c*(.7f*(crunchWide.next(x,.3f)-crunchLow.next(x,.06f))+.35f*stone[i].next(260+40*i));
    }
    const float density=t<PRESS?0:t<PRESS+.4f?.9f:.3f*(1-smooth((t-1.3f)*(1/.5f)));
    const uint32_t now=uint32_t(t*40);
    if(now!=slot){slot=now;sparkAt=(slot+rnd(slot,92))*(1/40.0f);live=rnd(slot,93)<density;fired=false;}
    if(live && !fired && t>=sparkAt){spark.trigger(.2f+.2f*rnd(slot,94));fired=true;}
    v+=spark.next()*(x-tick.next(x,.4f));
    return v*alchemyTail(t,LENGTH);
  }
};
// Gate: stone grinds as the door rises, the leaves groan open on a slow
// wavering creak, then the eye opens on a low dissonant chord that swells
// with a deep thud, and a rush of whispering air as the arms burst out,
// all fading into a hollow hum.
struct Gate {
  static constexpr float OPEN=.45f,EYE=.95f,REACH=1.1f,LENGTH=2.6f;static constexpr int CHORD=4;
  Noise noise{0x9b05688cu};Smooth low,wide,grainLow,grainWide,airLow,airWide;Osc creak,chord[CHORD],thudOsc;Decay thud;
  uint32_t chordStep[CHORD];bool opened=false;
  Gate() {
    static constexpr float hz[]={311.1f,440.0f,622.3f,932.3f};
    for(int k=0;k<CHORD;++k)chordStep[k]=Osc::step(hz[k]);
    thud.set(7);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // Grinding stone: band noise chopped by a fast irregular grain.
    if(t<OPEN+.1f) {
      const float grain=.55f+.45f*SINE[uint32_t(t*37*1024+400*SINE[uint32_t(t*11*1024)&1023])&1023];
      v+=.42f*smooth(t*(1/.08f))*(1-smooth((t-OPEN)*(1/.1f)))*grain*(grainWide.next(x,.22f)-grainLow.next(x,.08f));
    }
    // The creak: a wavering low tone, rough with noise.
    if(t>=OPEN-.05f && t<EYE+.1f) {
      const float a=t-OPEN+.05f,env=smooth(a*(1/.1f))*(1-smooth((t-EYE+.05f)*(1/.15f)));
      const float hz=230+40*SINE[uint32_t(a*6*1024)&1023]+30*a,y=creak.next(hz);
      v+=.3f*env*(y+.5f*y*y*y)*(.7f+.3f*x);
    }
    if(t>=EYE) {
      if(!opened){thud.trigger(.85f);opened=true;}
      const float a=t-EYE,env=smooth(a*(1/.25f))*(1-smooth((t-1.9f)*(1/.6f)));
      float c=0;for(int k=0;k<CHORD;++k)c+=chord[k].advance(chordStep[k])*(k==3?.5f:1);
      v+=.13f*env*(.8f+.2f*SINE[uint32_t(a*5*1024)&1023])*c;
      v+=thud.next()*smooth(a*(1/.004f))*thudOsc.next(300-90*clamp01(a*(1/.2f)));
    }
    // The arms burst out on a rush of whispering air.
    if(t>=REACH) {
      const float a=t-REACH,env=smooth(a*(1/.08f))*(1-smooth((a-.15f)*(1/.6f)));
      v+=.38f*env*(airWide.next(x,.5f)-airLow.next(x,.2f));
    }
    v+=.06f*smooth((t-.2f)*(1/.3f))*(wide.next(x,.2f)-low.next(x,.08f));
    return v*alchemyTail(t,LENGTH);
  }
};
// Fracture: a buzzing charge with red crackle as the palm presses, a sharp
// split, the cracks running out as a dense stony crackle of resonant
// clicks, a burst with a falling body when the web flashes, then a
// smouldering fizz that thins out.
struct Fracture {
  static constexpr float PRESS=.35f,FULL=.9f,LENGTH=2.4f;
  Noise noise{0x1f83d9abu};Smooth low,wide,tick,bite,fizzLow;Osc buzz,body,click;Decay split,burst,thump,spark,knock;
  uint32_t slot=~0u,clickStep=0;float at=0;bool live=false,fired=false,pressed=false,flashed=false;
  Fracture(){split.set(45);burst.set(9);thump.set(10);spark.set(1100);knock.set(160);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t<PRESS+.05f) {
      const float s=t*(1/PRESS),sq=buzz.next(150+90*s)>0?1.0f:-1.0f;
      v+=.12f*smooth(t*(1/.1f))*(1-smooth((t-PRESS)*(1/.05f)))*sq*(.6f+.4f*x);
    }
    if(t>=PRESS){if(!pressed){split.trigger(1);pressed=true;}v+=.7f*split.next()*(x-bite.next(x,.3f));}
    if(t>=FULL) {
      if(!flashed){burst.trigger(.8f);thump.trigger(.8f);flashed=true;}
      const float a=t-FULL;
      v+=.5f*burst.next()*(wide.next(x,.25f)-low.next(x,.07f));
      v+=thump.next()*smooth(a*(1/.004f))*body.next(420-200*clamp01(a*(1/.18f)));
      v+=.1f*smooth(a*(1/.2f))*(1-smooth((t-1.8f)*(1/.5f)))*(x-fizzLow.next(x,.6f));
    }
    // Stony crackle: resonant clicks, densest while the cracks run.
    const float rate=t<PRESS?25:t<FULL+.2f?70:30;
    const float density=t<PRESS?.5f:t<FULL+.2f?.95f:.6f*(1-smooth((t-1.5f)*(1/.6f)));
    const uint32_t now=uint32_t(t*rate);
    if(now!=slot){slot=now;at=(slot+rnd(slot,101))/rate;live=rnd(slot,102)<density;fired=false;}
    if(live && !fired && t>=at) {
      const float amount=.18f+.25f*rnd(slot,103);
      if(t>=PRESS && rnd(slot,104)<.6f){knock.trigger(amount);clickStep=Osc::step(900+1800*rnd(slot,105));}
      else spark.trigger(amount);
      fired=true;
    }
    v+=spark.next()*(x-tick.next(x,.4f));
    if(knock.level>.001f)v+=knock.next()*click.advance(clickStep);
    return v*alchemyTail(t,LENGTH);
  }
};
}
}
