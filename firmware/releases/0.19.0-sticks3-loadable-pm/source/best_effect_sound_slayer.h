#pragma once
// Demon Slayer-inspired celebration sounds (Sunwheel..Butterfly), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to the
// impacts in best_effect_slayer.h. Each is a hit and a short tail, 2.2 to
// 2.6 s long (its LEN), and silent for the rest of the effect. Melodic
// voices use the Japanese In scale (D Eb G A Bb), which gives the five a
// shared voice distinct from the other sets.
namespace BestEffectSound {
namespace detail {
// A plucked string: a decaying sine with its octave, the octave dying first.
struct Pluck {
  Osc osc;Decay body,bright;uint32_t inc=0;
  Pluck(){body.set(5);bright.set(14);}
  void strike(float hz,float level){inc=Osc::step(hz);body.trigger(level);bright.trigger(level*.6f);}
  BEST_SOUND_INLINE float next(){if(body.level<1e-4f)return 0;const float s=osc.advance(inc);return body.next()*s+bright.next()*osc.octave();}
};

// Sunwheel: kagura bells shiver and a breath of fire rises as the blade
// ignites, a whoosh follows the arc round, a temple gong and a burst of
// flame land as the ring closes, and the fire roars and crackles down.
struct Sunwheel {
  static constexpr float IGNITE=.4f,CLOSE=1.05f,BURN=1.85f;static constexpr int BELLS=5,GONG=4;
  Noise noise{0x6a09e667u};Smooth low,wide,roarLow,roarHigh,crack;Osc bell[BELLS],gong[GONG];
  Decay ring[BELLS],toll[GONG],burst,pop;uint32_t bellStep[BELLS],gongStep[GONG];bool tolled=false;
  uint32_t shake=~0u,slot=~0u;float at=0;bool live=false,fired=false;
  Sunwheel() {
    static constexpr float hz[]={2349.3f,2489.0f,3136.0f,3520.0f,3729.3f};  // D7 Eb7 G7 A7 Bb7
    static constexpr float gz[]={587.3f,1318.0f,1975.0f,2793.0f},rate[]={2.2f,3.4f,5,7.5f};
    for(int k=0;k<BELLS;++k){bellStep[k]=Osc::step(hz[k]);ring[k].set(26);}
    for(int k=0;k<GONG;++k){gongStep[k]=Osc::step(gz[k]);toll[k].set(rate[k]);}
    burst.set(5);pop.set(900);
  }
  // 2.6 s long, silent after: a hit and a short tail, never a drone.
  static constexpr float LEN=2.6f;
  float next(float t){return t>=LEN?0:tone(t)*(1-smooth((t-LEN+.3f)*(1/.3f)));}
  float tone(float t) {
    const float x=noise.next();float v=0;
    // Suzu bells: shaken about 22 times a second, a random bell each time,
    // through the ignition and again lightly as the ring swirls.
    const uint32_t now=uint32_t(t*22);
    if(now!=shake){shake=now;const bool on=t<IGNITE+.1f?rnd(now,501)<.9f:(t>CLOSE+.2f && t<BURN && rnd(now,502)<.35f);
      if(on){const int k=int(rnd(now,503)*BELLS)%BELLS;ring[k].trigger((t<IGNITE?.08f+.12f*t/IGNITE:.07f));}}
    for(int k=0;k<BELLS;++k)if(ring[k].level>1e-4f)v+=ring[k].next()*bell[k].advance(bellStep[k]);
    // The fire: band noise rising with the ignition, rushing with the
    // sweep's speed, roaring after the close and dying with the burn.
    const float u=clamp01((t-IGNITE)*(1/(CLOSE-IGNITE))),speed=6*u*(1-u)*(t<CLOSE?1:0);
    const float roar=.12f*smooth(t*(1/IGNITE))+.35f*speed/1.5f+(t<CLOSE?0:.4f*(1-smooth((t-CLOSE)*(1/.9f)))+.2f)*(1-smooth((t-BURN)*(1/.7f)));
    const float band=wide.next(x,.1f+.25f*speed/1.5f)-low.next(x,.025f);
    v+=roar*band;
    if(t>=CLOSE) {
      static constexpr float level[]={.3f,.2f,.14f,.08f};
      if(!tolled){for(int k=0;k<GONG;++k)toll[k].trigger(level[k]);burst.trigger(.6f);tolled=true;}
      const float attack=smooth((t-CLOSE)*(1/.004f));
      for(int k=0;k<GONG;++k)v+=attack*toll[k].next()*gong[k].advance(gongStep[k]);
      v+=burst.next()*(roarHigh.next(x,.35f)-roarLow.next(x,.04f));
    }
    // Crackles while it burns.
    const uint32_t c=uint32_t(t*28);
    if(c!=slot){slot=c;at=(slot+rnd(slot,504))*(1/28.0f);live=t>CLOSE && t<2.5f && rnd(slot,505)<.7f;fired=false;}
    if(live && !fired && t>=at){pop.trigger(.3f+.2f*rnd(slot,506));fired=true;}
    return v+pop.next()*crack.next(x,.6f);
  }
};

// Torrent: drips as the ripples ring, rushing water swelling with each
// hump of the serpent while a plucked koto climbs the In scale, a crash
// and a falling thump as the crest breaks, then spray and receding drips.
struct Torrent {
  static constexpr float SWIM=.4f,BREAK=1.15f,POUR=1.75f;static constexpr int NOTES=6;
  Noise noise{0xbb67ae85u};Smooth low,wide,crashBand,crashLow;Osc thump,drip;Decay hit,body,drop;Pluck koto[3];
  float start[NOTES],pitch=0;bool rung[NOTES]={},broke=false;
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Torrent() {
    for(int n=0;n<NOTES;++n)start[n]=n<5?SWIM+.05f+.13f*n:BREAK;
    hit.set(10);body.set(8);drop.set(40);
  }
  // 2.4 s long, silent after: a hit and a short tail, never a drone.
  static constexpr float LEN=2.4f;
  float next(float t){return t>=LEN?0:tone(t)*(1-smooth((t-LEN+.3f)*(1/.3f)));}
  float tone(float t) {
    static constexpr float scale[NOTES]={587.3f,622.3f,784.0f,880.0f,932.3f,1174.7f};  // D5 Eb5 G5 A5 Bb5 D6
    const float x=noise.next();float v=0;
    // Rushing water: band noise swelling with the swim, rolling with each hump.
    const float swim=t<SWIM?0:smooth((t-SWIM)*(1/.2f))*(1-smooth((t-BREAK-.1f)*(1/.6f)));
    const float humps=.65f+.35f*SINE[uint32_t(std::max(0.0f,t-SWIM)*4.5f*1024)&1023];
    v+=.5f*swim*humps*(wide.next(x,.14f+.12f*swim)-low.next(x,.03f));
    for(int n=0;n<NOTES;++n)if(!rung[n] && t>=start[n]){koto[n%3].strike(scale[n],n<5?.2f:.24f);rung[n]=true;}
    for(auto &k:koto)v+=k.next();
    if(t>=BREAK) {
      if(!broke){hit.trigger(.7f);body.trigger(.5f);broke=true;}
      const float hz=430-190*clamp01((t-BREAK)*(1/.18f));
      v+=hit.next()*(crashBand.next(x,.5f)-crashLow.next(x,.05f))+body.next()*thump.next(hz);
      v+=.18f*(1-smooth((t-POUR)*(1/.6f)))*(wide.y-low.y);
    }
    // Drips: before the swim as the ripples ring, and from the spray.
    const uint32_t now=uint32_t(t*14);
    if(now!=slot){slot=now;at=(slot+rnd(slot,511))*(1/14.0f);live=(t<SWIM) || (t>BREAK+.15f && t<2.35f && rnd(slot,512)<.75f);fired=false;}
    if(live && !fired && t>=at){drop.trigger(.3f);pitch=900+900*rnd(slot,513);fired=true;}
    const float d=drop.level;
    if(d>.001f)v+=drop.next()*drip.next(pitch*(1.9f-d*2.6f));
    return v;
  }
};

// Blitz: static crackles thicken over a thin rising buzz, a breath of
// silence, then the thunderclap: a crack, a zap falling three octaves and
// a rolling boom; a second, softer clap, and the crackle dies away.
struct Blitz {
  static constexpr float HOLD=.62f,DASH=.8f,CLAP=1.02f;
  Noise noise{0x3c6ef372u};Smooth bright,tick,boomLow,boomHigh;Osc buzz,zap;Decay crack,boom,echo,spark,zapEnv;
  bool struck=false,clapped=false;uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Blitz(){crack.set(60);boom.set(4.5f);echo.set(9);spark.set(1400);zapEnv.set(14);}
  // 2.2 s long, silent after: a hit and a short tail, never a drone.
  static constexpr float LEN=2.2f;
  float next(float t){return t>=LEN?0:tone(t)*(1-smooth((t-LEN+.3f)*(1/.3f)));}
  float tone(float t) {
    const float x=noise.next(),hi=x-bright.next(x,.45f);float v=0;
    // The charge: a buzz rising from 300 Hz, cut for the held breath.
    if(t<HOLD+.02f) {
      const float c=t*(1/HOLD),a=.09f*smooth(t*(1/.15f))*(1-smooth((t-HOLD)*(1/.02f)));
      const float s=buzz.next(300+260*c*c);v+=a*(s>0?1:-1)*(.6f+.4f*c);
    }
    // Crackle: ticks that thicken through the charge and thin after the dash.
    const float rate=t<HOLD?14+40*t/HOLD:t<DASH?0:44*(1-clamp01((t-DASH)*(1/1.3f)));
    const uint32_t now=uint32_t(t*60);
    if(now!=slot){slot=now;at=(slot+rnd(slot,521))*(1/60.0f);live=rate>0 && rnd(slot,522)<rate*(1/60.0f);fired=false;}
    if(live && !fired && t>=at){spark.trigger(.28f+.2f*rnd(slot,523));fired=true;}
    v+=spark.next()*tick.next(hi,.8f);
    if(t>=DASH) {
      if(!struck){crack.trigger(.9f);boom.trigger(.55f);zapEnv.trigger(.32f);struck=true;}
      const float d=t-DASH,hz=3200*(1-clamp01(d*(1/.12f)))+330;
      v+=crack.next()*(.7f*hi+.3f*x)+boom.next()*(boomHigh.next(x,.16f)-boomLow.next(x,.03f))*3;
      v+=zapEnv.next()*zap.next(hz)*smooth(d*(1/.003f));
    }
    if(t>=CLAP) {
      if(!clapped){echo.trigger(.6f);clapped=true;}
      v+=echo.next()*(.6f*hi+.4f*x);
    }
    return v;
  }
};

// Tigerclaw: a low growl gathers, three ripping swipes, then a roar:
// growling noise and a rough voice swelling with the flames, crackling as
// the marks burn down.
struct Tigerclaw {
  static constexpr float RAKE=.5f,STAGGER=.06f,STRUCK=RAKE+2*STAGGER+.17f,BURN=1.55f;
  Noise noise{0xa54ff53au};Smooth low,wide,ripLow,ripHigh,crack;Osc voice,growlAm;Decay rip[3],pop;bool ripped[3]={};
  uint32_t slot=~0u;float at=0;bool live=false,fired=false;
  Tigerclaw(){for(auto &r:rip)r.set(20);pop.set(1000);}
  // 2.4 s long, silent after: a hit and a short tail, never a drone.
  static constexpr float LEN=2.4f;
  float next(float t){return t>=LEN?0:tone(t)*(1-smooth((t-LEN+.3f)*(1/.3f)));}
  float tone(float t) {
    const float x=noise.next();float v=0;
    // The growl: band noise and a rough 250 Hz voice, amplitude-modulated
    // at about 30 Hz, low before the strike and a roar after it.
    const float am=.5f+.5f*growlAm.next(29+6*clamp01((t-STRUCK)*2));
    const float before=.22f*smooth(t*(1/RAKE))*(t<STRUCK?1:0);
    const float after=t<STRUCK?0:(.55f*(1-smooth((t-STRUCK)*(1/.6f)))+.22f)*(1-smooth((t-BURN)*(1/.85f)));
    const float g=(before+after)*1.2f;
    if(g>0) {
      const float band=wide.next(x,.2f+.15f*(after>0))-low.next(x,.07f),o=voice.next(250+30*am);
      v+=g*am*(band+.35f*(o+.4f*o*o*o));
    }
    // Three swipes: fast noise rips, the band sweeping down.
    for(int j=0;j<3;++j) {
      const float a=t-RAKE-STAGGER*j;if(a<0 || a>.2f)continue;
      if(!ripped[j]){rip[j].trigger(.55f);ripped[j]=true;}
      const float k=.7f-.55f*clamp01(a*(1/.12f));
      v+=rip[j].next()*(ripHigh.next(x,k)-ripLow.next(x,.06f));
    }
    // Crackles in the burning marks.
    const uint32_t now=uint32_t(t*26);
    if(now!=slot){slot=now;at=(slot+rnd(slot,531))*(1/26.0f);live=t>STRUCK && t<2.35f && rnd(slot,532)<.7f;fired=false;}
    if(live && !fired && t>=at){pop.trigger(.28f+.2f*rnd(slot,533));fired=true;}
    return v+pop.next()*crack.next(x,.6f);
  }
};

// Butterfly: soft wingbeats flutter in, four bright stings rise up the In
// scale, and a glittering arpeggio of small bells lifts away with the
// butterflies.
struct Butterfly {
  static constexpr float STING=.92f,GAP=.13f,LIFT=STING+3*GAP+.06f;static constexpr int CHIMES=9;
  Noise noise{0x510e527fu};Smooth low,wide,tick;Osc sting[4],chime[CHIMES];Decay stingEnv[4],chimeEnv[CHIMES],click[4];
  uint32_t stingStep[4],chimeStep[CHIMES];float chimeAt[CHIMES];bool stung[4]={},rung[CHIMES]={};
  Butterfly() {
    static constexpr float sz[]={2349.3f,2489.0f,3136.0f,3520.0f};  // D7 Eb7 G7 A7
    static constexpr float cz[]={1174.7f,1244.5f,1568.0f,1760.0f,1864.7f,2349.3f,2489.0f,3136.0f,3520.0f};
    for(int k=0;k<4;++k){stingStep[k]=Osc::step(sz[k]);stingEnv[k].set(9);click[k].set(600);}
    for(int k=0;k<CHIMES;++k){chimeStep[k]=Osc::step(cz[k]);chimeEnv[k].set(5);chimeAt[k]=LIFT+.07f*k;}
  }
  // 2.4 s long, silent after: a hit and a short tail, never a drone.
  static constexpr float LEN=2.4f;
  float next(float t){return t>=LEN?0:tone(t)*(1-smooth((t-LEN+.3f)*(1/.3f)));}
  float tone(float t) {
    const float x=noise.next();float v=0;
    // Wingbeats: airy noise pulsed at the flap rate, faster in flight.
    const float flap=t<.85f?11:t<LIFT?6.5f:14;
    const float beat=SINE[uint32_t(t*flap*1024)&1023],pulse=beat*beat;
    const float air=(.16f*smooth(t*(1/.3f))+(t>LIFT?.16f*(1-smooth((t-LIFT)*(1/.8f))):0))*(1-smooth((t-2.0f)*(1/.4f)));
    v+=air*pulse*(wide.next(x,.4f)-low.next(x,.1f));
    for(int k=0;k<4;++k) {
      const float a=t-STING-GAP*k;if(a<0 || a>.5f)continue;
      if(!stung[k]){stingEnv[k].trigger(.3f);click[k].trigger(.35f);stung[k]=true;}
      // A short downward glint in pitch on the attack: the needle going in.
      const float glide=1+.08f*(1-clamp01(a*(1/.03f)));
      v+=smooth(a*(1/.002f))*stingEnv[k].next()*sting[k].advance(uint32_t(stingStep[k]*glide))+click[k].next()*tick.next(x-low.y,.8f);
    }
    for(int k=0;k<CHIMES;++k) {
      const float a=t-chimeAt[k];if(a<0 || a>1.1f)continue;
      if(!rung[k]){chimeEnv[k].trigger(.16f);rung[k]=true;}
      v+=smooth(a*(1/.003f))*chimeEnv[k].next()*chime[k].advance(chimeStep[k]);
    }
    return v;
  }
};
}
}
