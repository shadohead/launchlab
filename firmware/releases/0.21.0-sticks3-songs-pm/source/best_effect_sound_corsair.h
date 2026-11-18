#pragma once
// The corsair set's celebration sounds (Slingshot..Vapor), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to the
// animation; a hit with a short tail, silent after its soundDuration().
namespace BestEffectSound {
namespace detail {
// The tail's fade: full until 0.3 s before the end, then smoothly to silence.
BEST_SOUND_INLINE float corsairTail(float t,float length){return 1-smooth((t-length+.3f)*(1/.3f));}

// Slingshot: a whoosh at each pass of the wind-up, quickening and rising, a
// rubbery creak as the arm stretches back, the snap's whoosh, a heavy
// punch (a crack, a thump falling in pitch), a twanging wobble as the arm
// shakes, and a springy boing as it rebounds.
struct Slingshot {
  static constexpr float IN=.2f,WIND=.76f,LAUNCH=.88f,HIT=.98f,RETRACT=1.3f,LENGTH=2.2f;
  Noise noise{0x3c6ef372u};Smooth low,wide,crack;Osc creak,thump,twang,boing;Decay hit,body,shake,spring;bool struck=false,sprung=false;
  Slingshot(){hit.set(38);body.set(9);shake.set(6);spring.set(4.5f);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The wind-up: band noise swelling at every pass of the fist.
    if(t>=IN && t<WIND+.05f) {
      const float u=std::min(1.0f,(t-IN)*(1/(WIND-IN))),phi=.5f+2.5f*u*u,pass=SINE[uint32_t(phi*1024)&1023];
      const float band=wide.next(x,.1f+.35f*u)-low.next(x,.05f);
      v+=(.15f+.35f*u)*pass*pass*band*(1-smooth((t-WIND)*(1/.05f)));
    }
    // The stretch back: a rubbery creak sliding up with a fast wobble.
    if(t>=WIND-.04f && t<LAUNCH) {
      const float u=(t-WIND+.04f)*(1/(LAUNCH-WIND+.04f)),hz=260+520*u*u+40*SINE[uint32_t(t*38*1024)&1023];
      const float y=creak.next(hz);v+=.22f*smooth(u*4)*(1-smooth((u-.8f)*5))*(y+.4f*y*y*y);
    }
    // The snap: a short, bright whoosh.
    if(t>=LAUNCH-.02f && t<HIT+.02f) {
      const float u=(t-LAUNCH+.02f)*(1/.14f);
      v+=.55f*smooth(u*3)*(1-smooth((u-.75f)*4))*(wide.next(x,.45f)-low.next(x,.12f));
    }
    if(t>=HIT) {
      const float a=t-HIT;
      if(!struck){hit.trigger(1);body.trigger(.95f);shake.trigger(.5f);struck=true;}
      const float hz=360-150*clamp01(a*(1/.12f)),y=thump.next(hz);
      v+=.7f*hit.next()*crack.next(x,.6f)+.75f*body.next()*smooth(a*(1/.002f))*(1.4f*y-.4f*y*y*y);
      // The arm's taut wobble: a twang with a fast vibrato.
      const float vib=SINE[uint32_t(a*31*1024)&1023];
      v+=.2f*shake.next()*twang.next(520+45*vib);
    }
    // The rebound: a springy boing whose pitch swings and settles.
    if(t>=RETRACT) {
      const float a=t-RETRACT;
      if(!sprung){spring.trigger(.38f);sprung=true;}
      const float hz=430+190*SINE[uint32_t(a*7*1024)&1023]*(1-clamp01(a*(1/.8f)));
      const float y=boing.next(hz);v+=spring.next()*smooth(a*(1/.01f))*(y+.25f*boing.octave());
    }
    return v*corsairTail(t,LENGTH);
  }
};
// Presence: a low, tense hum swells through the hush under two heartbeats,
// then the release: a deep boom with a tearing crack and a buzzing crackle
// of lightning, the wave's whoosh, and an aftershock rumble as the crackle
// thins out.
struct Presence {
  static constexpr float RELEASE=.55f,LENGTH=2.5f;
  Noise noise{0xa54ff53au};Smooth low,wide,tick,tear;Osc hum,hum2,beat,boom,buzz;Decay pulse[2],blast,crack,zap,spark,shock;
  bool beaten[2]={},released=false,shocked=false;uint32_t slot=~0u,buzzStep=Osc::step(277);float at=0;bool live=false,fired=false;
  Presence(){for(auto &p:pulse)p.set(14);blast.set(4.5f);crack.set(30);zap.set(5);spark.set(1100);shock.set(7);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The hush: a hum (a tight minor second) swelling, cut at the release.
    if(t<RELEASE) {
      const float s=t*(1/RELEASE),env=.16f*smooth(s*2.5f)*(1-smooth((t-RELEASE+.04f)*(1/.04f)));
      v+=env*(hum.next(233)+.8f*hum2.next(247))*(.7f+.3f*s);
      static constexpr float beats[]={.12f,.36f};
      for(int k=0;k<2;++k)if(t>=beats[k]){if(!beaten[k]){pulse[k].trigger(k?.36f:.26f);beaten[k]=true;}v+=pulse[k].next()*beat.next(250-60*clamp01((t-beats[k])*(1/.08f)));}
    }
    if(t>=RELEASE) {
      const float a=t-RELEASE;
      if(!released){blast.trigger(1);crack.trigger(.9f);zap.trigger(.45f);released=true;}
      const float hz=300-110*clamp01(a*(1/.25f)),y=boom.next(hz);
      v+=.75f*blast.next()*smooth(a*(1/.003f))*(1.4f*y-.4f*y*y*y)+.6f*crack.next()*tear.next(x,.6f);
      const float sq=buzz.advance(buzzStep)>0?1.0f:-1.0f;
      v+=.2f*zap.next()*sq*(.6f+.4f*x);
      // The wave's whoosh as it rolls out after the crackle.
      const float u=(a-.3f)*(1/.7f);
      if(u>0 && u<1)v+=.32f*SINE[uint32_t(u*512)]*(wide.next(x,.2f)-low.next(x,.04f));
      // The first aftershock: a softer rumble.
      if(a>=1.45f){if(!shocked){shock.trigger(.45f);shocked=true;}v+=shock.next()*boom.next(240)*(.8f+.2f*x);}
    }
    // Crackle: dense through the release, thinning after.
    const float density=t<RELEASE?0:.9f*(1-smooth((t-RELEASE-.4f)*(1/1.2f)))+.08f;
    const uint32_t now=uint32_t(t*60);
    if(now!=slot){slot=now;at=(slot+rnd(slot,71))*(1/60.0f);live=rnd(slot,72)<density;fired=false;}
    if(live && !fired && t>=at){spark.trigger(.28f+.25f*rnd(slot,73));fired=true;}
    v+=spark.next()*(x-tick.next(x,.35f));
    return v*corsairTail(t,LENGTH);
  }
};
// Threefold: three bright metallic glints as the blades line up, a sharp
// whoosh as they dash across, a held breath of near silence with one tiny
// click, then the three cuts at once: a tearing rip, three bright rings
// a few hundredths apart and a knock under them, ringing out over a soft
// airy hiss.
struct Threefold {
  static constexpr float DASH=.52f,PASSED=.62f,CUT=.95f,LENGTH=2.2f;static constexpr int RINGS=3,PARTIALS=3;
  Noise noise{0x510e527fu};Smooth low,wide,rip,air;Osc glint[3],ring[RINGS][PARTIALS],knock,click;
  Decay glintEnv[3],ringEnv[RINGS],tear,thud,tick;bool glinted[3]={},rang[RINGS]={},cut=false,clicked=false;
  uint32_t ringStep[RINGS][PARTIALS],glintStep[3];
  Threefold() {
    static constexpr float base[RINGS]={1760.0f,2093.0f,2489.0f},ratio[PARTIALS]={1,1.47f,2.13f},g[3]={3520.0f,3951.0f,4699.6f};
    for(int r=0;r<RINGS;++r){ringEnv[r].set(5.5f+r);for(int k=0;k<PARTIALS;++k)ringStep[r][k]=Osc::step(base[r]*ratio[k]);}
    for(int k=0;k<3;++k){glintStep[k]=Osc::step(g[k]);glintEnv[k].set(22);}
    tear.set(26);thud.set(16);tick.set(260);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The glints, one per blade.
    for(int k=0;k<3;++k){const float at=.12f+.11f*k;if(t<at)continue;
      if(!glinted[k]){glintEnv[k].trigger(.18f);glinted[k]=true;}
      v+=glintEnv[k].next()*glint[k].advance(glintStep[k]);}
    // The dash: a fast whoosh, its band sweeping up.
    if(t>=DASH-.03f && t<PASSED+.08f) {
      const float u=(t-DASH+.03f)*(1/(PASSED-DASH+.11f));
      v+=.6f*SINE[uint32_t(u*512)]*(wide.next(x,.2f+.5f*u)-low.next(x,.06f+.2f*u));
    }
    // The beat: a tiny click as the blades are sheathed.
    if(t>=CUT-.12f){if(!clicked){tick.trigger(.25f);clicked=true;}v+=tick.next()*click.next(3300);}
    if(t>=CUT) {
      const float a=t-CUT;
      if(!cut){tear.trigger(.8f);thud.trigger(.5f);cut=true;}
      v+=.7f*tear.next()*rip.next(x,.7f)+thud.next()*smooth(a*(1/.002f))*knock.next(330-90*clamp01(a*(1/.06f)));
      static constexpr float level[PARTIALS]={.2f,.12f,.07f};
      for(int r=0;r<RINGS;++r){if(a<.03f*r)continue;
        if(!rang[r]){ringEnv[r].trigger(1);rang[r]=true;}
        const float e=ringEnv[r].next()*smooth((a-.03f*r)*(1/.002f));
        for(int k=0;k<PARTIALS;++k)v+=level[k]*e*ring[r][k].advance(ringStep[r][k]);}
      // Smoke: a soft airy hiss rising after the cut.
      v+=.12f*smooth((a-.2f)*(1/.4f))*(air.next(x,.3f)-low.next(x,.1f));
    }
    return v*corsairTail(t,LENGTH);
  }
};
// Banner: a halyard's ratchet clicking as the flag is hoisted, a whip
// crack as it snaps open, then the wind: gusting band noise with the cloth
// flapping in it and a thin hiss of spray, dying away.
struct Banner {
  static constexpr float RAISE=.22f,HOISTED=.82f,SNAP=.86f,LENGTH=2.4f;
  Noise noise{0x9b05688cu};Smooth low,wide,crack,flapLow,spray;Osc click,thump;Decay tick,whip,body;bool snapped=false;
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Banner(){tick.set(240);whip.set(40);body.set(14);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The hoist: ratchet clicks at about 14 a second.
    if(t>=RAISE && t<HOISTED) {
      const uint32_t now=uint32_t(t*14);
      if(now!=slot){slot=now;at=now*(1/14.0f);fired=false;}
      if(!fired && t>=at){tick.trigger(.3f);fired=true;}
      v+=tick.next()*(.6f*click.next(2100)+.4f*x);
    }
    if(t>=SNAP) {
      const float a=t-SNAP;
      if(!snapped){whip.trigger(1);body.trigger(.55f);snapped=true;}
      v+=.75f*whip.next()*crack.next(x,.7f)+body.next()*smooth(a*(1/.002f))*thump.next(300-80*clamp01(a*(1/.06f)));
      // The wind, gusting, and the cloth flapping in it.
      const float gust=.75f+.25f*SINE[uint32_t(a*1.3f*(1/6.2832f)*1024)&1023];
      const float env=smooth(a*(1/.15f))*gust;
      const float flap=.6f+.4f*SINE[uint32_t(a*7*1024)&1023];
      v+=.3f*env*(wide.next(x,.15f)-low.next(x,.05f))+.22f*env*flap*(flapLow.next(x,.35f)-low.y);
      v+=.07f*env*(x-spray.next(x,.5f));
    }
    return v*corsairTail(t,LENGTH);
  }
};
// Vapor: five pumping thumps, quickening, each with a puff of steam hiss,
// then a surge of steam as the speed comes and three sharp jet whooshes,
// one per burst of speed, over a hiss that thins away.
struct Vapor {
  static constexpr float GEAR=.98f,LENGTH=2.4f;
  static constexpr float BEATS[]={0,.3f,.53f,.71f,.84f,GEAR},BURSTS[]={1.12f,1.42f,1.72f,2.3f};
  Noise noise{0x1f83d9abu};Smooth low,wide,hiss,jetLow,jetWide;Osc thump;Decay beat[6],steam;bool hit[6]={};
  Vapor(){for(auto &b:beat)b.set(16);steam.set(3.5f);}
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The pump: a thump falling in pitch on each beat, with a puff of hiss.
    float puffs=0;
    for(int k=0;k<6;++k){if(t<BEATS[k])continue;
      if(!hit[k]){beat[k].trigger(k==5?.8f:.42f+.06f*k);hit[k]=true;if(k==5)steam.trigger(1);}
      const float a=t-BEATS[k],e=beat[k].next();
      v+=e*smooth(a*(1/.002f))*thump.next(320-110*clamp01(a*(1/.07f)));puffs+=e;}
    const float h=hiss.next(x,.45f)-low.next(x,.12f);
    v+=(.35f*puffs+.32f*steam.next())*h;
    // The jets: a fast whoosh per burst of speed, its band sweeping up.
    for(int n=0;n<4;++n){const float a=t-BURSTS[n];if(a<0 || a>=.2f)continue;
      const float u=a*(1/.2f),env=SINE[uint32_t(u*512)]*(n<3?1:.5f);
      v+=.55f*env*(jetWide.next(x,.25f+.5f*u)-jetLow.next(x,.08f+.25f*u));}
    // A steady hiss of steam after the gear-up, thinning out.
    if(t>GEAR)v+=.1f*smooth((t-GEAR)*(1/.3f))*h;
    return v*corsairTail(t,LENGTH);
  }
};
}
}
