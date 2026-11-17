#pragma once
// The phantom set's celebration sounds (Timestop..Sequel), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to the
// animation; a hit with a short tail, silent after its soundDuration().
namespace BestEffectSound {
namespace detail {
// The tail's fade: full until 0.3 s before the end, then smoothly to silence.
BEST_SOUND_INLINE float phantomTail(float t,float length){return 1-smooth((t-length+.3f)*(1/.3f));}

// Timestop: a sucked-in rising swell, then the sphere bursts with a hollow
// falling boom and a glassy chord that bends down as it spreads; a sharp
// click as time stops, then the clock's tick and tock every 0.45 s.
struct Timestop {
  static constexpr float BURST=.4f,STOP=1.25f,LENGTH=2.6f;static constexpr int BELL=4;
  Noise noise{0x1f83d9abu};Smooth low,wide,hp;Osc swell,boom,bell[BELL],click;Decay body,ring[BELL],snap,tick;
  bool burst=false,stopped=false;int ticks=0;float tickHz=2400;
  Timestop(){body.set(3.6f);static constexpr float rate[]={2.2f,3,4,5.5f};for(int k=0;k<BELL;++k)ring[k].set(rate[k]);snap.set(60);tick.set(120);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The swell: noise in a band climbing toward the burst, over a rising tone.
    if(t<BURST+.02f) {
      const float u=t*(1/BURST),env=u*u*(1-smooth((t-BURST)*(1/.02f)));
      v+=.5f*env*(wide.next(x,.08f+.4f*u)-low.next(x,.05f))+.16f*env*swell.next(400+900*u*u);
    }
    if(t>=BURST) {
      static constexpr float level[]={.26f,.2f,.15f,.1f},hz[]={1046.5f,1244.5f,1568.0f,2093.0f};
      if(!burst){body.trigger(.8f);for(int k=0;k<BELL;++k)ring[k].trigger(level[k]);burst=true;}
      const float a=t-BURST,attack=smooth(a*(1/.004f)),bend=1-.12f*clamp01(a*(1/.8f));
      const float y=boom.next(330-110*clamp01(a*(1/.5f)));
      v+=attack*body.next()*(y+.3f*y*y*y);
      for(int k=0;k<BELL;++k)v+=attack*ring[k].next()*bell[k].next(hz[k]*bend);
      if(a<.5f)v+=.3f*(1-a*2)*(1-a*2)*(x-hp.next(x,.2f));
    }
    // Time stops: a snap, then tick and tock.
    if(t>=STOP && !stopped){snap.trigger(.55f);stopped=true;}
    if(snap.level>.001f)v+=snap.next()*(.5f*click.next(3300)+.5f*x);
    const float when=1.55f+.45f*ticks;
    if(t>=when && when<LENGTH-.1f){tick.trigger(.7f);tickHz=ticks%2?2000:2600;++ticks;}
    if(tick.level>.001f)v+=tick.next()*(.7f*click.next(tickHz)+.3f*(x-hp.next(x,.5f)));
    return v*phantomTail(t,LENGTH);
  }
};
// Zipper: the rasp of teeth as the pull stitches across, two tugs, a
// longer falling rasp as it unzips, an airy shimmer with sparkling pings
// while the mouth gapes, a rising rasp as it zips shut and a snap.
struct Zipper {
  static constexpr float STITCH0=.08f,STITCH1=.36f,OPEN0=.5f,OPEN1=.98f,SHUT0=1.42f,SHUT1=1.8f,LENGTH=2.2f;
  static constexpr int PAD=3;
  Noise noise{0x5be0cd19u};Smooth low,hp,band,padLow;Osc ring,pad[PAD],ping,clickOsc;Decay tooth,tug,snap,pip;
  uint32_t padStep[PAD],slot=~0u,pingStep=0;int passed=-1,tugs=0;float at=0,toothHz=3000;bool live=false,fired=false,snapped=false;
  Zipper() {
    static constexpr float chord[]={1174.7f,1568.0f,2349.3f};
    for(int k=0;k<PAD;++k)padStep[k]=Osc::step(chord[k]);
    tooth.set(420);tug.set(60);snap.set(45);pip.set(30);
  }
  // Teeth passed so far, about one per four pixels of the pull's travel.
  static float teeth(float t) {
    auto ease=[](float v){v=clamp01(v);const float w=1-v;return 1-w*w*w;};
    if(t<STITCH1)return 32*ease((t-STITCH0)*(1/(STITCH1-STITCH0)));
    if(t<OPEN0)return 32;
    if(t<OPEN1)return 32+32*smooth((t-OPEN0)*(1/(OPEN1-OPEN0)));
    if(t<SHUT0)return 64;
    return 64+32*smooth((t-SHUT0)*(1/(SHUT1-SHUT0)));
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // Each tooth is a short bright click; the stitch and the shut rise in
    // pitch, the unzip falls.
    const int n=int(teeth(t));
    if(n!=passed){passed=n;if(n>0 && n<96){tooth.trigger(.55f+.25f*rnd(n,91));
      toothHz=n<32?2600+20*n:n<64?3600-30*(n-32):2400+30*(n-64);}}
    if(tooth.level>.001f){const float e=tooth.next();v+=e*(.6f*(x-hp.next(x,.45f))+.5f*clickOsc.next(toothHz));}
    // Two tugs on the pull.
    static constexpr float TUGS[]={.4f,.46f};
    if(tugs<2 && t>=TUGS[tugs]){tug.trigger(.4f);++tugs;}
    if(tug.level>.001f)v+=tug.next()*(.5f*x+.5f*ring.next(1900));
    // The gape: an airy chord and sparkling pings.
    if(t>=OPEN0+.2f && t<SHUT1+.1f) {
      const float env=smooth((t-OPEN0-.2f)*(1/.3f))*(1-smooth((t-SHUT0)*(1/.45f)));
      // Indexed from the opening, so the phase is never negative.
      const float breathe=.75f+.25f*SINE[uint32_t((t-OPEN0)*2.2f*1024)&1023];
      float chord=0;for(int k=0;k<PAD;++k)chord+=pad[k].advance(padStep[k]);
      v+=env*(.09f*breathe*chord+.12f*(band.next(x,.3f)-padLow.next(x,.1f)));
      const uint32_t now=uint32_t(t*18);
      if(now!=slot){slot=now;at=(slot+rnd(slot,92))*(1/18.0f);live=rnd(slot,93)<env;fired=false;}
      if(live && !fired && t>=at){static constexpr float scale[]={2637.0f,3136.0f,3520.0f,4186.0f,4698.6f};
        pip.trigger(.14f);pingStep=Osc::step(scale[hash(slot*5+3)%5]);fired=true;}
    }
    if(pip.level>.001f)v+=pip.next()*ping.advance(pingStep);
    // The snap as it shuts.
    if(t>=SHUT1 && !snapped){snap.trigger(.6f);snapped=true;}
    if(snap.level>.001f)v+=snap.next()*(.45f*x+.55f*ring.next(2793));
    return v*phantomTail(t,LENGTH);
  }
};
// Rush: a tense swell as the fists draw back, then a punch every 50 ms,
// each a slap of noise on a short thump alternating in pitch over a roar,
// a beat of silence, a rising whoosh and the uppercut's crack and boom.
struct Rush {
  static constexpr float RUSH0=.32f,RUSH1=1.32f,JAB=.05f,UPPER=1.48f,HIT=1.58f,LENGTH=2.4f;
  Noise noise{0x9b05688cu};Smooth low,wide,slapLp,slapHp,crackHp;Osc thump,boom,ring;Decay punch,body,crack,big;
  int jabs=-1;float thumpHz=300;bool hit=false;
  Rush(){punch.set(55);body.set(40);crack.set(45);big.set(5.5f);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    if(t<RUSH0)v+=.35f*smooth(t*(1/RUSH0))*(wide.next(x,.06f+.2f*t)-low.next(x,.04f));
    if(t>=RUSH0 && t<RUSH1) {
      const int j=int((t-RUSH0)*(1/JAB));
      if(j!=jabs){jabs=j;punch.trigger(.5f+.25f*rnd(j,95));body.trigger(.55f);thumpHz=j&1?270:330;}
      // The roar under the barrage.
      v+=.18f*(wide.next(x,.2f)-low.next(x,.05f));
    }
    if(punch.level>.001f) {
      const float slap=slapLp.next(x,.45f)-slapHp.next(x,.08f);
      v+=punch.next()*slap+body.next()*thump.next(thumpHz*(.8f+.4f*body.level));
    }
    if(t>=UPPER && t<HIT) {
      const float u=(t-UPPER)*(1/(HIT-UPPER));
      v+=.55f*u*u*(wide.next(x,.1f+.5f*u)-low.next(x,.06f));
    }
    if(t>=HIT) {
      if(!hit){crack.trigger(1);big.trigger(.9f);hit=true;}
      const float a=t-HIT,y=boom.next(360-140*clamp01(a*(1/.3f)));
      v+=.8f*crack.next()*(x-crackHp.next(x,.3f))+smooth(a*(1/.002f))*big.next()*(.75f*(y+.35f*y*y*y)+.12f*ring.next(1480));
    }
    return v*phantomTail(t,LENGTH);
  }
};
// Menace: a low rumbling tritone drone, rough and trembling, with a thump
// on each beat as the beats quicken and the drone climbs; then the flash:
// a crack and a harsh bright cluster, and the rumble throbs on, fading.
struct Menace {
  static constexpr float FLASH=1.55f,LENGTH=2.6f;static constexpr int STAB=3;
  Noise noise{0x2f8bd8e1u};Smooth low,wide,crackHp;Osc droneA,droneB,beat,stab[STAB];Decay thump,crack,ring[STAB];
  uint32_t stabStep[STAB];int beats=0;bool flashed=false;
  Menace(){static constexpr float hz[]={1661.2f,2349.3f,3322.4f};for(int k=0;k<STAB;++k){stabStep[k]=Osc::step(hz[k]);ring[k].set(6+3*k);}thump.set(18);crack.set(40);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    static constexpr float BEATS[]={.45f,.8f,1.06f,1.25f,1.38f,1.47f};
    const float x=noise.next();float v=0;
    // The drone: two voices a tritone apart, waveshaped, roughened at 23 Hz,
    // climbing a little as the tension builds and throbbing slowly after.
    const float climb=t<FLASH?1+.12f*t*(1/FLASH):1.12f,swell=smooth(t*(1/.4f));
    float throb=1;
    if(t>FLASH+.3f){const float c=(t-FLASH-.3f)*(1/.6f),f=c-int(c);throb=.55f+.45f*std::max(0.0f,1-f*(1/.35f));}
    const float rough=.65f+.35f*SINE[uint32_t(t*23*1024)&1023];
    const float y=droneA.next(233*climb)+droneB.next(329.6f*climb);
    v+=.2f*swell*throb*rough*(y+.4f*y*y-.2f*y*y*y)+.1f*swell*(wide.next(x,.12f)-low.next(x,.05f));
    // A thump on each beat.
    if(beats<6 && t>=BEATS[beats]){thump.trigger(.45f+.06f*beats);++beats;}
    if(thump.level>.001f)v+=thump.next()*beat.next(300-60*(1-thump.level));
    if(t>=FLASH) {
      static constexpr float level[]={.3f,.22f,.14f};
      if(!flashed){crack.trigger(1);for(int k=0;k<STAB;++k)ring[k].trigger(level[k]);flashed=true;}
      const float attack=smooth((t-FLASH)*(1/.002f));
      v+=.8f*crack.next()*(x-crackHp.next(x,.3f));
      for(int k=0;k<STAB;++k){const float r=stab[k].advance(stabStep[k]);v+=attack*ring[k].next()*(r+.3f*r*r*r);}
    }
    return v*phantomTail(t,LENGTH);
  }
};
// Sequel: the burst whooshes and crackles, then the freeze stops it like a
// tape: its pitch and level drop away in a fifth of a second, a shutter
// clicks; the banner slides in on a soft swish and lands on three rising
// plucks and a bright chord.
struct Sequel {
  static constexpr float FREEZE=.55f,SLIDE0=1.3f,LAND=1.7f,LENGTH=2.7f;static constexpr int NOTES=4;
  Noise noise{0x5a827999u};Smooth low,wide,tick,swLow,swWide;Osc whir,pluck[NOTES],chordA,chordB;Decay click,spark,env[NOTES];
  uint32_t slot=~0u;float at=0;bool live=false,fired=false,clicked=false,rung[NOTES]={};
  Sequel(){click.set(70);spark.set(900);for(int k=0;k<NOTES;++k)env[k].set(k<3?7:3.2f);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The burst, and the tape stop at the freeze.
    if(t<FREEZE+.22f) {
      const float u=t<FREEZE?t*(1/FREEZE):1,stop=t<FREEZE?1:1-(t-FREEZE)*(1/.22f);
      const float drag=stop*stop,env=smooth(t*(1/.15f))*drag;
      v+=.4f*env*(wide.next(x,(.1f+.3f*u)*drag+.01f)-low.next(x,.04f*drag+.005f))+.16f*env*whir.next((500+900*u)*drag+60);
    }
    if(t<FREEZE) {
      const uint32_t now=uint32_t(t*40);
      if(now!=slot){slot=now;at=(slot+rnd(slot,97))*(1/40.0f);live=rnd(slot,98)<.7f;fired=false;}
      if(live && !fired && t>=at){spark.trigger(.3f);fired=true;}
    }
    if(spark.level>.001f)v+=spark.next()*(x-tick.next(x,.4f));
    if(t>=FREEZE+.2f && !clicked){click.trigger(.5f);clicked=true;}
    if(click.level>.001f)v+=click.next()*x;
    // The banner's swish.
    if(t>=SLIDE0 && t<LAND) {
      const float u=(t-SLIDE0)*(1/(LAND-SLIDE0));
      v+=.3f*SINE[uint32_t(u*512)&1023]*(swWide.next(x,.12f+.25f*u)-swLow.next(x,.05f));
    }
    // Three rising plucks as it lands, then a bright chord.
    static constexpr float WHEN[]={LAND-.12f,LAND,LAND+.12f,LAND+.3f},HZ[]={659.3f,830.6f,987.8f,1318.5f};
    for(int k=0;k<NOTES;++k) {
      if(t<WHEN[k])continue;
      if(!rung[k]){env[k].trigger(k<3?.32f:.36f);rung[k]=true;}
      const float e=env[k].next(),p=pluck[k].next(HZ[k]);
      v+=e*(p+.4f*pluck[k].octave()*e);
      if(k==3)v+=.25f*e*(chordA.next(HZ[1])+chordB.next(HZ[2]));
    }
    return v*phantomTail(t,LENGTH);
  }
};
}
}
