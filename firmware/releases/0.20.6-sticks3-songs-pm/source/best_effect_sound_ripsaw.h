#pragma once
// The ripsaw set's celebration sounds (Rev..Sharkfin), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to the
// animation; a hit with a short tail, silent after its soundDuration().
namespace BestEffectSound {
namespace detail {
// The tail's fade: full until 0.3 s before the end, then smoothly to silence.
BEST_SOUND_INLINE float ripsawTail(float t,float length){return 1-smooth((t-length+.3f)*(1/.3f));}
// A sawtooth from a Q32 phase: bright and buzzy, an engine's voice.
BEST_SOUND_INLINE float ripsawSaw(uint32_t phase){return int32_t(phase)*(1.0f/2147483648.0f);}

// Rev: the cord's ratcheting zip and its snap back, two coughing pops, then
// the engine catches: a buzzy two-stroke roar that climbs, revs with the
// chains and rattles with chain noise, then eases off.
struct Rev {
  static constexpr float YANK=.12f,PULLED=.3f,BACK=.42f,CATCH=.8f,LENGTH=2.6f;
  static constexpr float COUGHS[2]={.46f,.62f};
  Noise noise{0x5be0cd19u};Smooth low,wide,hp,rattle,body;Osc pop;Decay cough,crack,catchHit;
  uint32_t engine=0,sub=0;int coughed=0;bool caught=false;
  Rev(){cough.set(14);crack.set(60);catchHit.set(9);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The cord: a ratchet of clicks under a rising zip while it is yanked, a shorter one as it snaps back.
    if(t>=YANK && t<BACK+.02f) {
      const bool out=t<PULLED;const float u=out?(t-YANK)*(1/(PULLED-YANK)):(t-PULLED)*(1/(BACK-PULLED));
      const float band=wide.next(x,out?.15f+.45f*u:.6f-.4f*u)-low.next(x,.06f);
      const float rate=out?55+90*u:120,clickPhase=(t-YANK)*rate,frac=clickPhase-int(clickPhase);
      v+=(out?.45f:.3f)*SINE[uint32_t(clamp01(u)*512)&1023]*band+(frac<.12f?.35f*(x-hp.next(x,.5f)):0);
    }
    // Two coughs: a pop falling in pitch with a burst of noise.
    if(coughed<2 && t>=COUGHS[coughed]){cough.trigger(coughed?.85f:.7f);crack.trigger(.6f);++coughed;}
    if(coughed) {
      const float a=t-COUGHS[coughed-1],hz=420-180*clamp01(a*(1/.06f));
      v+=cough.next()*(.8f*pop.next(hz)+.3f*body.next(x,.2f))+.4f*crack.next()*x;
    }
    // The engine: a sawtooth at the firing rate with a slightly detuned square for body, climbing to
    // full revs, revving with the chains (every 0.85 s) and easing off.
    if(t>=CATCH) {
      if(!caught){catchHit.trigger(1);caught=true;}
      const float a=t-CATCH,climb=clamp01(a*(1/.2f));
      const float rev=a>.2f?SINE[uint32_t((a-.2f)*(1/.85f)*1024)&1023]:0;
      const float hz=210+170*climb*climb+45*rev,env=smooth(a*(1/.03f))*(1-.35f*smooth((t-1.6f)*(1/.8f)));
      engine+=Osc::step(hz);sub+=Osc::step(hz*1.004f);
      const float saw=ripsawSaw(engine),thump=ripsawSaw(sub)>0?.5f:-.5f;
      v+=env*(.32f*saw+.12f*thump)*(.85f+.15f*rev);
      // Chain rattle: bright noise gated at the tooth rate.
      const float gate=SINE[uint32_t(a*230*1024)&1023]>.2f?1.0f:.4f;
      v+=.2f*env*gate*(x-rattle.next(x,.3f));
      v+=.5f*catchHit.next()*(x-low.next(x,.05f));
    }
    return v*ripsawTail(t,LENGTH);
  }
};
// Hammer: liquid plips gathering faster under a rising swell, a creak as it
// winds back, a whoosh into the swing, then the landing: a crunching thud
// falling in pitch with a wet splash, and drips plinking into the pool.
struct Hammer {
  static constexpr float GATHER=.72f,WIND=.8f,SWING=1.0f,IMPACT=1.14f,LENGTH=2.4f;
  Noise noise{0x1f83d9abu};Smooth low,wide,hp,splashLow,splashWide;Osc plip,body,creak;Decay drop,thud,crash,splash;
  uint32_t slot=~0u;float at=0,plipHz=900;bool live=false,fired=false,hit=false;
  Hammer(){drop.set(30);thud.set(7);crash.set(28);splash.set(5);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // Plips: droplets arriving, denser toward the end of the gathering; drips after the landing.
    const bool gathering=t>.2f && t<GATHER+.05f,dripping=t>IMPACT+.35f;
    const float rate=gathering?10+30*clamp01((t-.2f)*(1/.5f)):10;
    const uint32_t now=uint32_t(t*rate)+(dripping?1000:0);
    if(now!=slot){slot=now;at=(uint32_t(t*rate)+rnd(slot,71))/rate;live=(gathering || dripping) && rnd(slot,72)<(gathering?.85f:.75f);fired=false;}
    if(live && !fired && t>=at){drop.trigger(dripping?.22f:.28f);plipHz=dripping?1400+600*rnd(slot,73):700+700*rnd(slot,73);fired=true;}
    if(drop.level>.001f){const float e=drop.next();v+=e*plip.next(plipHz*(1.35f-.35f*e*4));}
    // A swell as the hammer forms.
    if(t<GATHER+.1f)v+=.16f*smooth(t*(1/.6f))*(1-smooth((t-GATHER)*(1/.1f)))*(wide.next(x,.12f)-low.next(x,.04f));
    // The wind-back: a short creak.
    if(t>=WIND && t<SWING){const float u=(t-WIND)*(1/(SWING-WIND));v+=.14f*SINE[uint32_t(u*512)&1023]*creak.next(330+60*u)*(.6f+.4f*(x>0?1:-1));}
    // The swing: a whoosh rising into the hit.
    if(t>=SWING && t<IMPACT+.02f){const float u=(t-SWING)*(1/(IMPACT-SWING));v+=.6f*u*u*(wide.next(x,.2f+.5f*u)-low.next(x,.06f));}
    if(t>=IMPACT) {
      if(!hit){thud.trigger(1);crash.trigger(.9f);splash.trigger(.5f);hit=true;}
      const float a=t-IMPACT,hz=330-120*clamp01(a*(1/.12f)),y=body.next(hz),crunch=1.5f*y-.5f*y*y*y;
      v+=.7f*thud.next()*smooth(a*(1/.002f))*crunch+.6f*crash.next()*(x-hp.next(x,.3f));
      v+=.45f*splash.next()*(splashWide.next(x,.35f)-splashLow.next(x,.1f));
    }
    return v*ripsawTail(t,LENGTH);
  }
};
// Foxbite: a soft shimmer rising as the hand sign draws itself, one hollow
// wooden knock as it takes aim, an airy rush as the head comes down, then
// the snap: a hard double clack over a growling thump; foxfire breathes out
// in a high, wavering whistle.
struct Foxbite {
  static constexpr float SIGN=.35f,AIM=.5f,DESCEND=.62f,SNAP=1.12f,BURN=1.65f,LENGTH=2.4f;static constexpr int NOTES=4;
  Noise noise{0x5be0cd19u^0x9e3779b9u};Smooth low,wide,hp,growl;Osc chime[NOTES],block,clack,thump,whistle;Decay ring[NOTES],knock,snap,body;
  uint32_t chimeStep[NOTES];bool rung[NOTES]={},knocked=false,snapped=false,second=false;
  Foxbite() {
    static constexpr float hz[]={1567.98f,1864.66f,2349.32f,2793.83f};
    for(int i=0;i<NOTES;++i){chimeStep[i]=Osc::step(hz[i]);ring[i].set(9);}
    knock.set(38);snap.set(70);body.set(9);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The sign: four soft notes rising as it draws.
    for(int i=0;i<NOTES;++i) {
      const float at=.03f+i*(SIGN/NOTES);
      if(t>=at && !rung[i]){ring[i].trigger(.16f);rung[i]=true;}
      if(rung[i] && ring[i].level>.001f)v+=ring[i].next()*chime[i].advance(chimeStep[i]);
    }
    // The aim: a hollow knock.
    if(t>=AIM && !knocked){knock.trigger(.6f);knocked=true;}
    if(knocked && knock.level>.001f){const float e=knock.next();v+=e*(.8f*block.next(820)+.3f*block.octave());}
    // The descent: a rush of air swelling into the bite.
    if(t>=DESCEND && t<SNAP+.03f){const float u=(t-DESCEND)*(1/(SNAP-DESCEND));v+=.42f*u*u*(wide.next(x,.1f+.4f*u)-low.next(x,.05f));}
    // The snap: two clacks 30 ms apart over a growling thump.
    if(t>=SNAP && !snapped){snap.trigger(.9f);body.trigger(.8f);snapped=true;}
    if(t>=SNAP+.03f && !second){snap.trigger(.7f);second=true;}
    if(snapped) {
      const float a=t-SNAP,s=snap.next();
      v+=s*(.7f*(x-hp.next(x,.3f))+.5f*clack.next(2600));
      const float hz=260-40*clamp01(a*(1/.2f)),g=growl.next(x,.08f)*6;
      v+=body.next()*(.55f*thump.next(hz)*(1+.6f*g));
    }
    // Foxfire: a wavering high whistle breathing in and out.
    if(t>=BURN-.1f) {
      const float a=t-BURN+.1f,env=smooth(a*(1/.3f))*(.6f+.4f*SINE[uint32_t(a*1.6f*1024)&1023]);
      const float wob=SINE[uint32_t(a*5.5f*1024)&1023];
      v+=.09f*env*whistle.next(1975+40*wob)+.05f*env*(wide.next(x,.4f)-low.next(x,.2f));
    }
    return v*ripsawTail(t,LENGTH);
  }
};
// Detonate: a metal scrape as the pin is tugged, a bright click and ping as
// it pops, three quickening beeps, then a chain of blasts (a noise crack and
// a body falling in pitch for each, the last the biggest), a rolling rumble
// with crackle, and one late pop.
struct Detonate {
  static constexpr float TUG=.12f,POP=.32f,LENGTH=2.6f;static constexpr int BLASTS=5;
  static constexpr float AT[BLASTS]={.62f,.82f,1.0f,1.42f,2.2f},POWER[BLASTS]={.7f,.75f,.75f,1,.35f};
  static constexpr float BEEPS[3]={.38f,.48f,.55f};
  Noise noise{0x6c62272eu};Smooth low,wide,hp,rumbleLo,rumbleHi,tick;Osc ping,beep,body;Decay click,pinRing,crack,boom,roll,spark;
  int fired=0,beeped=0;bool popped=false;uint32_t slot=~0u;float at=0;bool live=false,ticked=false;
  Detonate(){click.set(120);pinRing.set(14);crack.set(30);boom.set(8);roll.set(1.6f);spark.set(1100);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The tug: a thin scrape.
    if(t>=TUG && t<POP){const float u=(t-TUG)*(1/(POP-TUG));v+=.18f*u*(x-hp.next(x,.6f))*(.6f+.4f*SINE[uint32_t(t*70*1024)&1023]);}
    // The pop: a click and a ringing ping.
    if(t>=POP && !popped){click.trigger(.8f);pinRing.trigger(.3f);popped=true;}
    if(popped){v+=click.next()*x;if(pinRing.level>.001f)v+=pinRing.next()*(ping.next(3520)+.4f*ping.octave());}
    // Three beeps, 40 ms each.
    while(beeped<3 && t>=BEEPS[beeped])++beeped;
    if(beeped && t<BEEPS[beeped-1]+.04f){const float sq=beep.next(1480)>0?1.0f:-1.0f;v+=.16f*sq;}
    // The blasts.
    while(fired<BLASTS && t>=AT[fired]){const float p=POWER[fired];crack.trigger(.8f*p);boom.trigger(p);roll.trigger(std::max(roll.level,.45f*p));++fired;}
    if(fired) {
      const float a=t-AT[fired-1],hz=340-140*clamp01(a*(1/.15f)),y=body.next(hz);
      v+=crack.next()*(x-low.next(x,.15f))+.6f*boom.next()*smooth(a*(1/.002f))*(1.4f*y-.4f*y*y*y);
      v+=.55f*roll.next()*(rumbleHi.next(x,.25f)-rumbleLo.next(x,.08f))*2.5f;
    }
    // Crackle in the smoke.
    const uint32_t now=uint32_t(t*30);
    if(now!=slot){slot=now;at=(slot+rnd(slot,81))*(1/30.0f);live=t>.7f && rnd(slot,82)<.6f*(1-smooth((t-1.9f)*(1/.6f)));ticked=false;}
    if(live && !ticked && t>=at){spark.trigger(.25f+.2f*rnd(slot,83));ticked=true;}
    v+=spark.next()*(x-tick.next(x,.4f));
    return v*ripsawTail(t,LENGTH);
  }
};
// Sharkfin: a gritty hiss as the fin cuts the floor, quickening through its
// lap, a hush as it dips, then the breach: a crash of spray with chips
// clattering and a whoosh over the arc; a plunging gulp as it dives back
// in, and the hiss again, softer, as the fin circles.
struct Sharkfin {
  static constexpr float SURFACE=.2f,DIP=.95f,LEAP=1.1f,DIVE=1.72f,BACK=2.0f,LENGTH=2.4f;
  Noise noise{0x2545f491u};Smooth low,wide,grit,hp,swish,swishLow,tick;Osc gulp;Decay crash,plunge,chip;bool breached=false,dived=false;
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Sharkfin(){crash.set(9);plunge.set(10);chip.set(900);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The cut: band noise with a gritty flutter, swelling with the lap, back softer after the dive.
    const float cut=t<DIP+.05f?smooth((t-SURFACE+.1f)*(1/.2f))*(.5f+.5f*clamp01(t*(1/DIP)))*(1-smooth((t-DIP)*(1/.08f))):.55f*smooth((t-BACK)*(1/.25f));
    if(cut>0){const float flutter=SINE[uint32_t(t*(t<DIP?38:22)*1024)&1023]>0?1.0f:.55f;v+=.55f*cut*flutter*(wide.next(x,.3f)-grit.next(x,.1f));}
    // The breach: a crash of spray, then a whoosh over the arc.
    if(t>=LEAP && !breached){crash.trigger(.9f);breached=true;}
    if(breached)v+=crash.next()*(x-low.next(x,.12f));
    if(t>=LEAP && t<DIVE){const float u=(t-LEAP)*(1/(DIVE-LEAP)),sw=SINE[uint32_t(u*512)&1023];v+=.35f*sw*(swish.next(x,.15f+.35f*u)-swishLow.next(x,.06f));}
    // The dive: a plunging gulp falling in pitch with a softer splash.
    if(t>=DIVE && !dived){plunge.trigger(.8f);dived=true;}
    if(dived){const float a=t-DIVE,e=plunge.next();v+=e*(.6f*gulp.next(900-560*clamp01(a*(1/.12f)))+.45f*(x-hp.next(x,.2f)));}
    // Chips clattering down after each break.
    const bool clatter=(t>LEAP && t<LEAP+.7f) || (t>DIVE && t<DIVE+.5f);
    const uint32_t now=uint32_t(t*45);
    if(now!=slot){slot=now;at=(slot+rnd(slot,91))*(1/45.0f);live=clatter && rnd(slot,92)<.6f;fired=false;}
    if(live && !fired && t>=at){chip.trigger(.3f+.2f*rnd(slot,93));fired=true;}
    v+=chip.next()*(x-tick.next(x,.5f));
    return v*ripsawTail(t,LENGTH);
  }
};
}
}
