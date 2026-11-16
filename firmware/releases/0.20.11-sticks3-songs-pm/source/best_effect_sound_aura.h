#pragma once
// The aura set's celebration sounds (Haymaker..Waterleaf), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to the
// animation; a hit with a short tail, silent after its soundDuration().
namespace BestEffectSound {
namespace detail {
// The tail's fade: full until 0.3 s before the end, then smoothly to silence.
BEST_SOUND_INLINE float auraTail(float t,float length){return 1-smooth((t-length+.3f)*(1/.3f));}

// Haymaker: three counted thumps climbing in pitch over a roar of aura that
// swells with each, a sucked-in breath as the fist cocks, a whoosh on the
// drive, then the hit: a crack, a crunching body falling in pitch and a
// bright ring, and the roar burning down with crackle.
struct Haymaker {
  static constexpr float BEAT[3]={.14f,.44f,.74f};
  static constexpr float COCK=.84f,PUNCH=1.0f,HIT=1.07f,LENGTH=2.3f;
  Noise noise{0x1f83d9abu};Smooth low,wide,whooshLo,whooshHi,crackLp,hp;Osc drum,body,ring,ring2;
  Decay thump,crack,crunch,shine,spark;int beats=0;bool hit=false;float drumHz=330;
  uint32_t slot=~0u,ringStep=Osc::step(1567.98f),ring2Step=Osc::step(2349.32f);float at=0;bool live=false,fired=false;
  Haymaker(){thump.set(14);crack.set(40);crunch.set(9);shine.set(6);spark.set(1100);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The count: a tom-like thump per beat, each a step higher.
    if(beats<3 && t>=BEAT[beats]){static constexpr float hz[3]={330,392,494};drumHz=hz[beats];thump.trigger(.3f+.07f*beats);++beats;}
    if(thump.level>.001f){const float d=thump.level;v+=thump.next()*drum.next(drumHz*(.8f+.35f*d));}
    // The aura's roar: band noise stepping up with each beat, surging into
    // the cock, then burning down after the hit.
    const float build=t<HIT?.12f+.1f*beats+.2f*smooth((t-COCK)*(1/.16f)):.45f*(1-smooth((t-HIT-.1f)*(1/1.0f)))+.08f;
    const float flick=.75f+.25f*SINE[uint32_t(t*11*1024)&1023];
    v+=build*flick*smooth(t*(1/.1f))*(wide.next(x,.22f)-low.next(x,.05f));
    // The breath in as it cocks, and the whoosh of the drive.
    if(t>=COCK && t<HIT) {
      const float u=(t-COCK)*(1/(HIT-COCK)),swell=u*u;
      v+=.5f*swell*(whooshHi.next(x,.1f+.5f*u)-whooshLo.next(x,.05f+.15f*u));
    }
    // The hit.
    if(t>=HIT) {
      if(!hit){crack.trigger(1.1f);crunch.trigger(1);shine.trigger(.26f);hit=true;}
      const float a=t-HIT,y=body.next(520-300*clamp01(a*(1/.18f))),c=1.5f*y-.5f*y*y*y;
      v+=.7f*crack.next()*crackLp.next(x,.6f)+.65f*crunch.next()*smooth(a*(1/.002f))*c;
      const float sh=shine.next();v+=sh*(ring.advance(ringStep)+.6f*ring2.advance(ring2Step));
    }
    // Crackle off the burning aura after the hit.
    const uint32_t now=uint32_t(t*30);
    if(now!=slot){slot=now;at=(slot+rnd(slot,91))*(1/30.0f);live=t>HIT+.05f && rnd(slot,92)<.7f*(1-smooth((t-HIT-.3f)*(1/.9f)));fired=false;}
    if(live && !fired && t>=at){spark.trigger(.25f+.2f*rnd(slot,93));fired=true;}
    v+=spark.next()*(x-hp.next(x,.4f));
    return v*auraTail(t,LENGTH);
  }
};
// Palms: a soft shimmer as the halo of hands opens, a breath and a sharp
// clap as the palms meet in prayer, then eight heavy slaps in quick
// succession, and a temple bell over one great slap as every hand strikes,
// ringing out.
struct Palms {
  static constexpr float CLAP=.74f,FIRST=.86f,STEP=.075f,ALL=1.55f,LENGTH=2.6f;static constexpr int BELL=4;
  Noise noise{0x2545f491u};Smooth low,wide,slapLp,clapHp,airLo,airHi;Osc shimmer[2],thud,bell[BELL];
  Decay clap,slap,body,ring[BELL];uint32_t bellStep[BELL],shimStep[2];int slaps=0;bool clapped=false,rang=false;float thudHz=300;
  Palms() {
    static constexpr float hz[]={587.3f,1396.9f,1971.0f,2793.8f},rate[]={2.2f,3.4f,4.6f,7};
    for(int k=0;k<BELL;++k){bellStep[k]=Osc::step(hz[k]);ring[k].set(rate[k]);}
    shimStep[0]=Osc::step(1174.7f);shimStep[1]=Osc::step(1760);
    clap.set(60);slap.set(30);body.set(16);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The halo opening: a soft two-note shimmer with a tremolo.
    const float sh=smooth(t*(1/.15f))*(1-smooth((t-.45f)*(1/.4f)))*(.6f+.4f*SINE[uint32_t(t*9*1024)&1023]);
    v+=.07f*sh*(shimmer[0].advance(shimStep[0])+shimmer[1].advance(shimStep[1]));
    // The prayer: a breath rising into the clap.
    if(t>.35f && t<CLAP){const float u=(t-.35f)*(1/(CLAP-.35f));v+=.25f*u*u*(airHi.next(x,.3f)-airLo.next(x,.1f));}
    if(t>=CLAP && !clapped){clap.trigger(.85f);clapped=true;}
    if(clap.level>.001f)v+=clap.next()*(x-clapHp.next(x,.25f));
    // The strikes: a slap and a thump each, the last ones a little lower.
    if(slaps<8 && t>=FIRST+STEP*slaps){slap.trigger(.55f);body.trigger(.6f);thudHz=330-12.0f*slaps;++slaps;}
    if(t>=ALL && !rang){slap.trigger(1);body.trigger(.9f);thudHz=260;static constexpr float level[]={.3f,.2f,.13f,.08f};for(int k=0;k<BELL;++k)ring[k].trigger(level[k]);rang=true;}
    if(slap.level>.001f)v+=slap.next()*slapLp.next(x,.45f);
    if(body.level>.001f){const float d=body.level;v+=body.next()*thud.next(thudHz*(.75f+.4f*d));}
    if(rang){const float a=smooth((t-ALL)*(1/.003f));for(int k=0;k<BELL;++k)v+=a*ring[k].next()*bell[k].advance(bellStep[k]);}
    return v*auraTail(t,LENGTH);
  }
};
// Elastic: two wet smacks as the gum flies in and sticks, a rubbery creak
// rising as it stretches and hums tighter, then the snap: a twang that
// falls in pitch with a pop, and springy boings as the blobs bounce on
// their strings.
struct Elastic {
  static constexpr float SHOOT=.06f,FLY=.24f,STRETCH=.42f,SNAP=1.12f,DONE=2.0f,LENGTH=2.4f;
  Noise noise{0x7a3c1d55u};Smooth low,wide,smackLp,popHp;Osc creak,twang,twang2,boing;
  Decay smack,pop,ring,bounce;int smacks=0,boings=0;bool snapped=false;float boingHz=600;
  Elastic(){smack.set(35);pop.set(70);ring.set(5.5f);bounce.set(9);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The flights: a short whoosh each, then a wet smack as each sticks.
    if(t<SHOOT+FLY+.1f){const float u=clamp01((t-SHOOT)*(1/(FLY+.07f)));v+=.18f*u*(1-u)*4*(wide.next(x,.3f)-low.next(x,.1f));}
    if(smacks<2 && t>=SHOOT+FLY+.07f*smacks){smack.trigger(.6f);++smacks;}
    if(smack.level>.001f)v+=smack.next()*(smackLp.next(x,.25f)*1.6f);
    // The stretch: a rubbery creak, pulsing faster and rising in pitch.
    if(t>=STRETCH && t<SNAP) {
      const float s=(t-STRETCH)*(1/(SNAP-STRETCH)),hz=240+420*s*s,pulse=.55f+.45f*SINE[uint32_t((t-STRETCH)*(5+11*s)*1024)&1023];
      const float y=creak.next(hz);v+=.22f*s*pulse*(y+.4f*y*y*y);
    }
    // The snap: a twang falling in pitch, with a pop.
    if(t>=SNAP) {
      if(!snapped){pop.trigger(.9f);ring.trigger(.45f);snapped=true;}
      const float a=t-SNAP,hz=900*(1-.45f*clamp01(a*(1/.25f)))*(1+.04f*SINE[uint32_t(a*16*1024)&1023]);
      v+=pop.next()*(x-popHp.next(x,.3f))+ring.next()*(twang.next(hz)+.35f*twang2.next(hz*2.01f));
    }
    // Boings as the blobs bounce on their strings.
    static constexpr float BOING[4]={1.5f,1.95f,2.2f,2.32f};
    if(boings<4 && t>=BOING[boings]){bounce.trigger(.3f-.04f*boings);boingHz=520+90*boings;++boings;}
    if(bounce.level>.001f){const float d=bounce.level;v+=bounce.next()*boing.next(boingHz*(.7f+1.2f*d));}
    return v*auraTail(t,LENGTH);
  }
};
// Chains: a whirring rush of clinking links as the chains shoot in, a
// heavy metal clank with a ringing tail as the hooks lock and the loop
// snaps taut, a rattle dying away, and one soft shiver along the links.
struct Chains {
  static constexpr float SHOOT0=.12f,LOCK=.62f,RIPPLE=1.9f,LENGTH=2.4f;static constexpr int PARTIALS=4;
  Noise noise{0x4f1bbcdcu};Smooth low,wide,clinkHp,thudLp;Osc ping,ring[PARTIALS],thud;Decay clink,clank[PARTIALS],body;
  uint32_t ringStep[PARTIALS],slot=~0u,pingStep=0;float at=0;bool live=false,fired=false,locked=false;
  Chains() {
    static constexpr float hz[]={1180.0f,1943.0f,2717.0f,3861.0f},rate[]={4.5f,6,8,11};
    for(int k=0;k<PARTIALS;++k){ringStep[k]=Osc::step(hz[k]);clank[k].set(rate[k]);}
    clink.set(160);body.set(18);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The rush of the chains through the air.
    if(t>=SHOOT0 && t<LOCK+.05f){const float u=(t-SHOOT0)*(1/(LOCK+.05f-SHOOT0));v+=.22f*SINE[uint32_t(u*512)&1023]*(wide.next(x,.35f)-low.next(x,.12f));}
    // Clinks: dense as they shoot, a rattle after the lock, a shiver later.
    float density=0,rate=40;
    if(t>=SHOOT0 && t<LOCK)density=.8f;
    else if(t>=LOCK && t<LOCK+.9f){density=.9f*(1-smooth((t-LOCK)*(1/.9f)));rate=34;}
    else if(t>=RIPPLE && t<RIPPLE+.35f){density=.6f*(1-(t-RIPPLE)*(1/.35f));rate=26;}
    const uint32_t now=uint32_t(t*rate);
    if(now!=slot){slot=now;at=(slot+rnd(slot,101))/rate;live=rnd(slot,102)<density;fired=false;}
    if(live && !fired && t>=at){clink.trigger(.22f+.18f*rnd(slot,103));pingStep=Osc::step(2600+2400*rnd(slot,104));fired=true;}
    if(clink.level>.001f){const float c=clink.next();v+=c*(.7f*ping.advance(pingStep)+.5f*(x-clinkHp.next(x,.5f)));}
    // The lock: a clank with a ringing tail and a thud under it.
    if(t>=LOCK) {
      static constexpr float level[]={.32f,.24f,.17f,.11f};
      if(!locked){for(int k=0;k<PARTIALS;++k)clank[k].trigger(level[k]);body.trigger(.7f);locked=true;}
      const float a=smooth((t-LOCK)*(1/.002f));
      for(int k=0;k<PARTIALS;++k)v+=a*clank[k].next()*ring[k].advance(ringStep[k]);
      v+=a*body.next()*(.7f*thud.next(330-110*clamp01((t-LOCK)*(1/.1f)))+.3f*thudLp.next(x,.3f));
    }
    return v*auraTail(t,LENGTH);
  }
};
// Waterleaf: a soft glassy ring as the glass appears, a rising shimmer as
// the hands' aura gathers, water trembling in quick droplet plinks, then
// the surge: a splash, a sharp crack of glass and a bright chime, and drips
// pattering away.
struct Waterleaf {
  static constexpr float HANDS=.25f,SURGE=1.25f,LENGTH=2.5f;static constexpr int GLASS=3;
  Noise noise{0x6c8e9cf5u};Smooth low,wide,splashLp,crackHp;Osc glass[GLASS],shimmer,plink,chime,chime2;
  Decay ring[GLASS],drop,splash,crack,bell;uint32_t glassStep[GLASS],slot=~0u;float at=0,dropHz=900;bool live=false,fired=false,surged=false;
  Waterleaf() {
    static constexpr float hz[]={1661.2f,2489.0f,3322.4f},rate[]={3,4.5f,6.5f};
    for(int k=0;k<GLASS;++k){glassStep[k]=Osc::step(hz[k]);ring[k].set(rate[k]);ring[k].trigger(.12f-.03f*k);}
    drop.set(26);splash.set(9);crack.set(55);bell.set(4.5f);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    const float in=smooth(t*(1/.01f));
    for(int k=0;k<GLASS;++k)v+=in*ring[k].next()*glass[k].advance(glassStep[k]);
    // The aura gathering: a shimmer rising in pitch and tremolo.
    if(t>=HANDS && t<SURGE+.05f) {
      const float s=(t-HANDS)*(1/(SURGE-HANDS)),trem=.6f+.4f*SINE[uint32_t(t*(6+14*s)*1024)&1023];
      v+=.09f*s*trem*shimmer.next(700+900*s*s);
    }
    // Plinks as the water trembles, then drips after the surge.
    const float rate=t<SURGE?14:9,density=t<.55f?0:t<SURGE?.8f:.7f*(1-smooth((t-SURGE-.3f)*(1/.8f)));
    const uint32_t now=uint32_t(t*rate);
    if(now!=slot){slot=now;at=(slot+rnd(slot,111))/rate;live=rnd(slot,112)<density;fired=false;}
    if(live && !fired && t>=at){drop.trigger(.22f+.12f*rnd(slot,113));dropHz=700+700*rnd(slot,114);fired=true;}
    if(drop.level>.001f){const float d=drop.level;v+=drop.next()*plink.next(dropHz*(1.6f-d*1.8f));}
    // The surge: a splash, the glass cracking and a bright chime.
    if(t>=SURGE) {
      if(!surged){splash.trigger(.7f);crack.trigger(.8f);bell.trigger(.24f);surged=true;}
      v+=splash.next()*(wide.next(x,.35f)-low.next(x,.06f))*1.3f+crack.next()*(x-crackHp.next(x,.6f));
      const float b=bell.next();v+=b*(chime.next(2093)+.5f*chime2.next(3136));
    }
    return v*auraTail(t,LENGTH);
  }
};
}
}
