#pragma once
// The grimoire set's celebration sounds (Volley..Meadow), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to the
// animation; a hit with a short tail, silent after its soundDuration().
// The set's voice is glassy and light: sine bells, soft noise and quick
// falling zaps, never a heavy boom.
namespace BestEffectSound {
namespace detail {
// The tail fade: 1 until 0.3 s before `length`, then down to 0.
BEST_SOUND_INLINE float grimoireTail(float t,float length){return 1-smooth((t-(length-.3f))*(1/.3f));}

// Volley: six glassy pings as the circles open, a quick falling zap for each
// beam of the three crossing pairs, a trembling whine as all six charge, then
// six zaps rising in a ripple into a bright bell chord as the final volley crosses, ringing out with a shimmer.
struct Volley {
  static constexpr float FINAL=1.35f,LENGTH=2.3f;static constexpr int SHOTS=8,BELL=4;
  Noise noise{0x51ab0001u};Smooth hp,hp2,air;Osc ping,zap,bell[BELL],twinkle,charge;Decay pingEnv,bellEnv[BELL],sparkle;
  uint32_t pingStep=0,bellStep[BELL],slot=~0u,twinkleStep=0;int opened=-1;bool rang=false;float at=0;bool live=false,fired=false;
  Volley() {
    static constexpr float hz[]={1760.0f,2637.0f,3520.0f,4434.9f},rate[]={2.4f,3.2f,4.4f,6};
    for(int k=0;k<BELL;++k){bellStep[k]=Osc::step(hz[k]);bellEnv[k].set(rate[k]);}
    pingEnv.set(22);sparkle.set(30);
  }
  // Shot k's start: each circle in turn, then the final volley (the upper
  // two together, the lower two after them).
  static float shotAt(int k){static constexpr float AT[SHOTS]={.55f,.7f,.95f,1.07f,FINAL,FINAL+.05f,FINAL+.2f,FINAL+.32f};return AT[k];}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The circles open: one soft ping each, climbing.
    static constexpr float OPEN[4]={1318.5f,1568.0f,1975.5f,2637.0f};
    const int o=t<.05f?-1:std::min(3,int((t-.05f)*(1/.1f)));
    if(o>opened){opened=o;pingEnv.trigger(.16f);pingStep=Osc::step(OPEN[o]);}
    if(pingEnv.level>.001f)v+=pingEnv.next()*ping.advance(pingStep);
    // Zaps: a sine falling fast from high, with a breath of bright noise.
    // The newest zap only: one oscillator, so overlapping shots restart it.
    for(int k=SHOTS-1;k>=0;--k) {
      const float age=t-shotAt(k);if(age<0)continue;if(age>=.09f)break;
      const float u=age*(1/.09f),e=(1-u)*(1-u)*smooth(age*(1/.002f)),top=k<4?3400.0f-200*k:2800.0f+300*(k-4);
      v+=.38f*e*zap.next(top-(top-700)*u)+.24f*e*(x-hp.next(x,.3f));
      break;
    }
    // All six charge: a rising, trembling whine into the final volley.
    if(t>=1.12f && t<FINAL+.04f) {
      const float u=(t-1.12f)*(1/(FINAL-1.12f)),trem=.6f+.4f*SINE[uint32_t(t*28*1024)&1023];
      v+=.14f*smooth(u*3)*(1-smooth((t-FINAL)*(1/.04f)))*trem*charge.next(1300+1500*u*u);
    }
    // The final volley crosses: a bright bell chord.
    if(t>=FINAL+.04f) {
      static constexpr float level[]={.3f,.2f,.13f,.08f};
      if(!rang){for(int k=0;k<BELL;++k)bellEnv[k].trigger(level[k]);rang=true;}
      const float attack=smooth((t-FINAL-.04f)*(1/.003f));
      for(int k=0;k<BELL;++k)v+=attack*bellEnv[k].next()*bell[k].advance(bellStep[k]);
      v+=.08f*(1-smooth((t-FINAL-.04f)*(1/.5f)))*(air.next(x,.45f)-hp2.next(x,.2f));
    }
    // A sparse shimmer of high twinkles through the tail.
    const uint32_t now=uint32_t(t*12);
    if(now!=slot){slot=now;at=(slot+rnd(slot,901))*(1/12.0f);live=t>1.45f && rnd(slot,902)<.6f;fired=false;}
    if(live && !fired && t>=at){static constexpr float hz[]={3520.0f,3951.1f,4698.6f,5274.0f};sparkle.trigger(.08f);twinkleStep=Osc::step(hz[hash(slot)%4]);fired=true;}
    if(sparkle.level>.001f)v+=sparkle.next()*twinkle.advance(twinkleStep);
    return v*grimoireTail(t,LENGTH);
  }
};
// Starfall: a patter of tiny high tinks as the lights kindle, climbing ring
// by ring, an airy chord swelling as they brighten together, a falling
// whistle as they drop, then a crackling barrage of soft pops as they burst
// on the dome, and a pale bell chord ringing out.
struct Starfall {
  static constexpr float FIRE=1.1f,HIT=1.26f,LENGTH=2.4f;static constexpr int PAD=3,BELL=3;
  Noise noise{0x51ab0002u};Smooth low,wide,popLp,popHp;Osc tink,pad[PAD],whistle,thump,bell[BELL];Decay tinkEnv,popEnv,thumpEnv,bellEnv[BELL];
  uint32_t padStep[PAD],bellStep[BELL],tinkStep=0,slot=~0u,popSlot=~0u;float at=0,popAt=0,thumpHz=300;bool live=false,fired=false,popLive=false,popFired=false,rang=false;
  Starfall() {
    static constexpr float chord[]={1174.7f,1480.0f,1760.0f},bellHz[]={2349.3f,2960.0f,3520.0f},rate[]={2.6f,3.4f,4.4f};
    for(int k=0;k<PAD;++k)padStep[k]=Osc::step(chord[k]);
    for(int k=0;k<BELL;++k){bellStep[k]=Osc::step(bellHz[k]);bellEnv[k].set(rate[k]);}
    tinkEnv.set(40);popEnv.set(60);thumpEnv.set(28);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // Kindling: tinks about 30 a second, each ring a step higher.
    const uint32_t now=uint32_t(t*30);
    if(now!=slot){slot=now;at=(slot+rnd(slot,911))*(1/30.0f);live=t>.05f && t<FIRE && rnd(slot,912)<.75f;fired=false;}
    if(live && !fired && t>=at) {
      static constexpr float scale[]={2637.0f,2960.0f,3520.0f,3951.1f,4698.6f,5274.0f};
      const int ring=std::min(2,int(t*(1/.3f)));tinkEnv.trigger(.07f+.03f*rnd(slot,913));tinkStep=Osc::step(scale[(hash(slot)%4)+ring/2+(ring>1)]);fired=true;
    }
    if(tinkEnv.level>.001f)v+=tinkEnv.next()*tink.advance(tinkStep);
    // The brightening: an airy chord swelling, cut as they fall.
    if(t>=.75f && t<FIRE+.06f) {
      const float env=smooth((t-.75f)*(1/.3f))*(1-smooth((t-FIRE)*(1/.06f))),trem=.75f+.25f*SINE[uint32_t(t*18*1024)&1023];
      float c=0;for(int k=0;k<PAD;++k)c+=pad[k].advance(padStep[k]);
      v+=.1f*env*trem*c+.12f*env*(wide.next(x,.4f)-low.next(x,.12f));
    }
    // The fall: a whistle diving fast.
    if(t>=FIRE && t<HIT+.04f) {
      // u passes 1 in the 40 ms fade after the hit; the pitch holds there.
      const float u=(t-FIRE)*(1/(HIT-FIRE)),p=std::min(u,1.0f);
      v+=.2f*smooth(u*4)*(1-smooth((t-HIT)*(1/.04f)))*whistle.next(3200-2200*p*p);
    }
    // The barrage: soft pops, each a noise burst with a small falling thump.
    const uint32_t ps=uint32_t(t*45);
    if(ps!=popSlot){popSlot=ps;popAt=(ps+rnd(ps,914))*(1/45.0f);const float d=t<HIT?0:1-smooth((t-HIT-.15f)*(1/.25f));popLive=rnd(ps,915)<.9f*d;popFired=false;}
    if(popLive && !popFired && t>=popAt){popEnv.trigger(.3f+.2f*rnd(ps,916));thumpEnv.trigger(.25f);thumpHz=420+260*rnd(ps,917);popFired=true;}
    if(popEnv.level>.001f)v+=popEnv.next()*(popLp.next(x,.5f)-popHp.next(x,.06f));
    if(thumpEnv.level>.001f){const float e=thumpEnv.next();v+=e*thump.next(thumpHz*(.6f+.4f*e*4));}
    // The ring-out.
    if(t>=HIT+.05f) {
      static constexpr float level[]={.18f,.12f,.08f};
      if(!rang){for(int k=0;k<BELL;++k)bellEnv[k].trigger(level[k]);rang=true;}
      const float attack=smooth((t-HIT-.05f)*(1/.01f));
      for(int k=0;k<BELL;++k)v+=attack*bellEnv[k].next()*bell[k].advance(bellStep[k]);
    }
    return v*grimoireTail(t,LENGTH);
  }
};
// Mimic: two wooden knocks as the chest rocks, a creak as the lid opens and
// a rising harp of treasure sparkle, then the slam: a hard woody clack with
// a coin's ting, two chomping clacks, and a springy boing as it hops,
// landing with a clack.
struct Mimic {
  static constexpr float OPEN=.6f,SNAP=1.3f,LENGTH=2.4f;static constexpr int KNOCKS=6,HARP=5;
  Noise noise{0x51ab0003u};Smooth creakLp,clackLp,clackHp;Osc knock,creak,harp,boing,ting[2];Decay knockEnv,harpEnv,tingEnv;
  uint32_t harpStep=0,tingStep[2];int knocked=-1,plucked=-1;bool tinged=false;float knockHz=600,knockLevel=0;
  Mimic(){knockEnv.set(30);harpEnv.set(9);tingEnv.set(7);tingStep[0]=Osc::step(2793.8f);tingStep[1]=Osc::step(4186.0f);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // Knocks and clacks: two rocks, the slam, two chomps, the hop's landing.
    static constexpr float AT[KNOCKS]={.3f,.47f,SNAP,SNAP+.31f,SNAP+.55f,2.36f},LEVEL[KNOCKS]={.3f,.26f,.75f,.45f,.45f,.5f},HZ[KNOCKS]={520,470,700,820,780,640};
    int k=-1;for(int i=0;i<KNOCKS;++i)if(t>=AT[i])k=i;
    if(k>knocked){knocked=k;knockEnv.trigger(1);knockHz=HZ[k];knockLevel=LEVEL[k];}
    if(knockEnv.level>.001f) {
      const float e=knockEnv.next();
      v+=knockLevel*e*(.7f*knock.next(knockHz*(.8f+.2f*e))+.6f*(clackLp.next(x,.6f)-clackHp.next(x,.1f))*e);
    }
    // The creak: a scraping tone in grains as the lid opens.
    if(t>=OPEN && t<OPEN+.38f) {
      const float u=(t-OPEN)*(1/.38f),grain=SINE[uint32_t(t*46*1024)&1023];
      v+=.16f*SINE[uint32_t(u*512)]*(grain>0?grain:0)*(creak.next(360+220*u)+.5f*creakLp.next(x,.2f));
    }
    // Treasure: a rising harp, one note every 70 ms.
    if(t>=OPEN+.12f) {
      static constexpr float NOTE[HARP]={1046.5f,1318.5f,1568.0f,2093.0f,2637.0f};
      const int n=std::min(HARP-1,int((t-OPEN-.12f)*(1/.07f)));
      if(n>plucked && t<SNAP){plucked=n;harpEnv.trigger(.18f);harpStep=Osc::step(NOTE[n]);}
      if(harpEnv.level>.001f){const float e=harpEnv.next();v+=e*(harp.advance(harpStep)+.3f*harp.octave());}
    }
    // A coin's ting as it is spat out.
    if(t>=SNAP+.04f) {
      if(!tinged){tingEnv.trigger(.16f);tinged=true;}
      const float e=tingEnv.next();v+=e*(ting[0].advance(tingStep[0])+.6f*ting[1].advance(tingStep[1]));
    }
    // The hop: a springy boing on take-off.
    if(t>=2.0f && t<2.3f) {
      const float u=(t-2.0f)*(1/.3f),wob=1+.08f*SINE[uint32_t(t*30*1024)&1023];
      v+=.22f*(1-u)*smooth(u*20)*boing.next((380+520*SINE[uint32_t(u*256)])*wob);
    }
    return v*grimoireTail(t,LENGTH);
  }
};
// Scales: a glassy sweep as the balance draws in, a low two-voice hum and a
// metal tick at each end of its swing as it weighs, a held, trembling
// minor tone while it leans the wrong way, then the turn: a rush rising
// into a bright major chord and a hard metallic clang as the pan slams down.
struct Scales {
  static constexpr float DRAW=.35f,HOLD=.95f,TURN=1.25f,SLAM=1.33f,LENGTH=2.3f;static constexpr int CHORD=4,CLANG=4;
  Noise noise{0x51ab0004u};Smooth low,wide;Osc sweep,humA,humB,tick,chord[CHORD],clang[CLANG];Decay tickEnv,chordEnv,clangEnv[CLANG];
  uint32_t chordStep[CHORD],clangStep[CLANG];int ticked=-1;bool turned=false,slammed=false;
  Scales() {
    static constexpr float hz[]={1046.5f,1318.5f,1568.0f,2093.0f},clangHz[]={1187.0f,1913.0f,2766.0f,3851.0f},rate[]={3.5f,5,7,10};
    for(int k=0;k<CHORD;++k)chordStep[k]=Osc::step(hz[k]);
    for(int k=0;k<CLANG;++k){clangStep[k]=Osc::step(clangHz[k]);clangEnv[k].set(rate[k]);}
    tickEnv.set(80);chordEnv.set(2.2f);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // Drawing in: a glassy rising sweep.
    if(t<DRAW+.05f)v+=.12f*smooth(t*(1/.05f))*(1-smooth((t-DRAW)*(1/.05f)))*sweep.next(900+1500*t*(1/DRAW));
    // Weighing: a low hum, and a tick at each end of the beam's swing (every 0.21 s).
    if(t>=DRAW && t<TURN+.05f) {
      const float env=smooth((t-DRAW)*(1/.15f))*(1-smooth((t-TURN)*(1/.05f))),lean=t>HOLD?.6f+.4f*SINE[uint32_t(t*9*1024)&1023]:1;
      v+=.09f*env*lean*(humA.next(440)+humB.next(t>HOLD?466.2f:523.3f));
      const int k=int((t-DRAW)*(1/.21f));
      if(k>ticked && t<HOLD+.1f){ticked=k;tickEnv.trigger(.2f*std::exp(-.5f*k));}
    }
    if(tickEnv.level>.001f)v+=tickEnv.next()*(tick.next(2600)+.5f*x);
    // The turn: a rush sweeping up into a bright chord.
    if(t>=TURN-.12f && t<TURN+.25f) {
      const float u=(t-TURN+.12f)*(1/.37f);
      v+=.3f*SINE[uint32_t(u*512)&1023]*(wide.next(x,.1f+.5f*u)-low.next(x,.05f+.15f*u));
    }
    if(t>=TURN) {
      if(!turned){chordEnv.trigger(.11f);turned=true;}
      const float e=chordEnv.next()*smooth((t-TURN)*(1/.02f));float c=0;
      for(int k=0;k<CHORD;++k)c+=chord[k].advance(chordStep[k]);
      v+=e*c;
    }
    // The slam: a metallic clang.
    if(t>=SLAM) {
      static constexpr float level[]={.3f,.22f,.15f,.1f};
      if(!slammed){for(int k=0;k<CLANG;++k)clangEnv[k].trigger(level[k]);slammed=true;}
      const float attack=smooth((t-SLAM)*(1/.002f));
      for(int k=0;k<CLANG;++k)v+=attack*clangEnv[k].next()*clang[k].advance(clangStep[k]);
    }
    return v*grimoireTail(t,LENGTH);
  }
};
// Meadow: a soft bell as the seed of light appears, then a breath of air
// sweeping out with the wave over a warm major-seventh pad, and a dense
// patter of tiny music-box pips as the flowers pop; a quick rising
// glissando as the field completes, then a breeze with the odd pip.
struct Meadow {
  static constexpr float SEED=.25f,FULL=1.3f,LENGTH=2.4f;static constexpr int PAD=4;
  Noise noise{0x51ab0005u};Smooth low,wide,airLow,airWide;Osc bell,pad[PAD],pip,gliss;Decay bellEnv,pipEnv,glissEnv;
  uint32_t padStep[PAD],pipStep=0,glissStep=0,slot=~0u;int glided=-1;bool rang=false;float at=0;bool live=false,fired=false;
  Meadow(){static constexpr float hz[]={784.0f,987.8f,1174.7f,1480.0f};for(int k=0;k<PAD;++k)padStep[k]=Osc::step(hz[k]);bellEnv.set(4);pipEnv.set(45);glissEnv.set(14);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(!rang){bellEnv.trigger(.16f);rang=true;}
    v+=bellEnv.next()*smooth(t*(1/.01f))*bell.next(2349.3f);
    // The wave: air sweeping brighter as it spreads.
    if(t>=SEED && t<FULL+.2f) {
      const float u=(t-SEED)*(1/(FULL-SEED)),env=smooth(u*4)*(1-smooth((t-FULL)*(1/.2f)));
      v+=.2f*env*(wide.next(x,.08f+.3f*u)-low.next(x,.03f+.1f*u));
    }
    // The pad swells with the field and fades with the breeze.
    if(t>=SEED) {
      const float env=smooth((t-SEED)*(1/.6f))*(1-smooth((t-1.6f)*(1/.7f)));
      float c=0;for(int k=0;k<PAD;++k)c+=pad[k].advance(padStep[k]);
      v+=.045f*env*c;
    }
    // Pips: about 40 a second while the flowers pop, then sparse.
    const float rate=40;const uint32_t now=uint32_t(t*rate);
    if(now!=slot){slot=now;at=(slot+rnd(slot,921))*(1/rate);
      const float density=t<SEED?0:t<FULL?.85f:.18f*(1-smooth((t-1.9f)*(1/.3f)));live=rnd(slot,922)<density;fired=false;}
    if(live && !fired && t>=at){static constexpr float scale[]={1568.0f,1760.0f,1975.5f,2349.3f,2637.0f,3136.0f,3520.0f};
      pipEnv.trigger(.07f+.04f*rnd(slot,923));pipStep=Osc::step(scale[hash(slot*5+2)%7]);fired=true;}
    if(pipEnv.level>.001f)v+=pipEnv.next()*(pip.advance(pipStep)+.3f*pip.octave());
    // The field completes: a quick rising glissando.
    if(t>=FULL-.05f) {
      static constexpr float NOTE[6]={1568.0f,1975.5f,2349.3f,3136.0f,3951.1f,4698.6f};
      const int n=std::min(5,int((t-FULL+.05f)*(1/.045f)));
      if(n>glided){glided=n;glissEnv.trigger(.12f);glissStep=Osc::step(NOTE[n]);}
      if(glissEnv.level>.001f)v+=glissEnv.next()*gliss.advance(glissStep);
    }
    // The breeze.
    if(t>=1.5f){const float env=smooth((t-1.5f)*(1/.4f));v+=.1f*env*(.7f+.3f*SINE[uint32_t(t*2.3f*1024)&1023])*(airWide.next(x,.22f)-airLow.next(x,.06f));}
    return 1.5f*v*grimoireTail(t,LENGTH);
  }
};
}
}
