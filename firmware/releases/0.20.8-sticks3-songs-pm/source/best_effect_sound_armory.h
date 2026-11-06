#pragma once
// The armory set's celebration sounds (Swordland..Trace), in the style of
// best_effect_sound.h, which includes this file before the Synth: fixed
// seeds, no libm per sample, content above about 200 Hz, timed to the
// animation; a hit with a short tail, silent after its soundDuration().
namespace BestEffectSound {
namespace detail {
// The tail's fade: full until 0.3 s before the end, then smoothly to silence.
BEST_SOUND_INLINE float armoryTail(float t,float length){return 1-smooth((t-length+.3f)*(1/.3f));}

// Swordland: a breath of fire catches and rushes outward, a bright metallic
// ring as each of the eight swords rises in its path, then the gears engage
// with a deep clank and turn on in slowing ratchet ticks.
struct Swordland {
  static constexpr float IGNITE=.3f,ENGAGE=1.0f,LENGTH=2.6f;static constexpr int SWORDS=8;
  Noise noise{0x5be0cd19u};Smooth low,wide,tick,thud,floor;Osc bar[SWORDS],over[SWORDS],clank;Decay shing[SWORDS],ring,knock,click;
  uint32_t barStep[SWORDS],overStep[SWORDS],clankStep=Osc::step(277),slot=~0u;bool rung[SWORDS]={},engaged=false,fired=false,live=false;float at=0;
  Swordland() {
    static constexpr float hz[]={2349.0f,1864.0f,2960.0f,2093.0f,2637.0f,1760.0f,3136.0f,2489.0f};
    for(int k=0;k<SWORDS;++k){barStep[k]=Osc::step(hz[k]);overStep[k]=Osc::step(hz[k]*2.76f);shing[k].set(9+k*.6f);}
    ring.set(3.5f);knock.set(26);click.set(300);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The fire: a band of noise that swells as the spark catches, rushes
    // brighter as the ring sweeps out and dies down to a crackle.
    const float swell=smooth(t*(1/IGNITE))*(1-smooth((t-.85f)*(1/.5f)));
    const float rush=clamp01((t-IGNITE)*(1/.75f));
    v+=(.24f+.24f*rush*(1-rush*.5f))*swell*(wide.next(x,.1f+.35f*rush)-low.next(x,.05f));
    // Each sword rings as the front reaches it.
    static constexpr float rise[SWORDS]={.42f,.5f,.57f,.63f,.7f,.78f,.86f,.95f};
    for(int k=0;k<SWORDS;++k) {
      if(t<rise[k])continue;
      if(!rung[k]){shing[k].trigger(.2f-.008f*k);rung[k]=true;}
      if(shing[k].level<.0005f)continue;
      const float e=smooth((t-rise[k])*(1/.002f))*shing[k].next();
      v+=e*(bar[k].advance(barStep[k])+.35f*over[k].advance(overStep[k]));
    }
    // The gears engage: a deep clank with a knock of noise.
    if(t>=ENGAGE) {
      if(!engaged){ring.trigger(.32f);knock.trigger(.5f);engaged=true;}
      const float c=clank.advance(clankStep);
      v+=ring.next()*(c+.5f*c*c*c)+knock.next()*(thud.next(x,.35f)-floor.next(x,.06f));
    }
    // Ratchet ticks as the gears turn, slowing a little.
    const float rate=t<ENGAGE?0:11-3*clamp01((t-ENGAGE)*(1/1.4f));
    if(rate>0) {
      const uint32_t now=uint32_t((t-ENGAGE)*rate);
      if(now!=slot){slot=now;at=ENGAGE+(now+.05f)/rate;live=true;fired=false;}
      if(live && !fired && t>=at){click.trigger(.22f+.06f*(now&1));fired=true;}
      v+=click.next()*(x-tick.next(x,.4f));
    }
    return v*armoryTail(t,LENGTH);
  }
};
// Treasury: each gate opens with a rising glassy chime over a soft
// shimmer, the weapons slide out with a metallic hiss, then the volley:
// seven whooshes, each landing with a thunk and a bright sparkle, and the
// shimmer lingers as the second rank hangs.
struct Treasury {
  static constexpr int GATES=7;static constexpr float VOLLEY=.8f,STEP=.08f,FLIGHT=.1f,LENGTH=2.4f;
  Noise noise{0x1f83d9abu};Smooth low,wide,hiss,crack,air;Osc chime[GATES],sparkle[GATES],body,pad[3];
  Decay ring[GATES],spark[GATES],thump[GATES];bool opened[GATES]={},hit[GATES]={};
  uint32_t chimeStep[GATES],sparkStep[GATES],padStep[3];float bodyHz=300;
  Treasury() {
    static constexpr float hz[]={1568.0f,1760.0f,1975.5f,2093.0f,2349.3f,2637.0f,3136.0f},chord[]={784.0f,987.8f,1174.7f};
    for(int k=0;k<GATES;++k){chimeStep[k]=Osc::step(hz[k]);sparkStep[k]=Osc::step(hz[(k*3+2)%GATES]*2);ring[k].set(7);spark[k].set(14);thump[k].set(28);}
    for(int k=0;k<3;++k)padStep[k]=Osc::step(chord[k]);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The shimmer under it all: a soft chord trembling in and out.
    const float padK=smooth(t*(1/.25f))*(1-smooth((t-1.6f)*(1/.6f)))*(.75f+.25f*SINE[uint32_t(t*7*1024)&1023]);
    float chord=0;for(int k=0;k<3;++k)chord+=pad[k].advance(padStep[k]);
    v+=.05f*padK*chord+.06f*padK*(air.next(x,.5f)-wide.next(x,.25f));
    // The weapons slide out: a metallic hiss.
    if(t>.24f && t<.85f){const float e=smooth((t-.24f)*(1/.1f))*(1-smooth((t-.55f)*(1/.3f)));v+=.16f*e*(x-hiss.next(x,.6f));}
    for(int g=0;g<GATES;++g) {
      const float open=.05f*g,launch=VOLLEY+STEP*g,land=launch+FLIGHT;
      if(t>=open){if(!opened[g]){ring[g].trigger(.12f);opened[g]=true;}if(ring[g].level>.0005f)v+=smooth((t-open)*(1/.004f))*ring[g].next()*chime[g].advance(chimeStep[g]);}
      // The whoosh of each launch: a short band of noise falling in pitch.
      const float a=t-launch;
      if(a>=0 && a<FLIGHT+.02f){const float e=SINE[uint32_t(a*(1/(FLIGHT+.02f))*512)&1023];v+=.22f*e*(crack.next(x,.45f-2.5f*a)-low.next(x,.04f));}
      if(t>=land) {
        if(!hit[g]){thump[g].trigger(.42f);spark[g].trigger(.13f);hit[g]=true;bodyHz=340;}
        if(thump[g].level>.0005f){const float d=thump[g].next();v+=d*(.8f*body.next(bodyHz)+.5f*x);}
        if(spark[g].level>.0005f)v+=spark[g].next()*sparkle[g].advance(sparkStep[g]);
      }
    }
    bodyHz+=(170-bodyHz)*.0015f;
    return v*armoryTail(t,LENGTH);
  }
};
// Sunblade: a choir-like chord swells and climbs as the light gathers,
// with bright pings as motes arrive; a breath in as the blade draws back,
// a whoosh through the swing, then the release: a crack, a bright major
// chord and a falling boom, ringing out as the dust settles.
struct Sunblade {
  static constexpr float CHARGE=.85f,BACK=1.0f,SWING=1.13f,LENGTH=2.6f;static constexpr int VOICES=3,CHORD=4;
  Noise noise{0xa54ff53au};Smooth low,wide,crack,breath;Osc voice[VOICES],shimmer[VOICES],chord[CHORD],boom,vib,ping;
  Decay blast[CHORD],thud,flash,pip;bool released=false;uint32_t chordStep[CHORD],slot=~0u,pingStep=0;float at=0;bool live=false,fired=false;
  Sunblade() {
    static constexpr float hz[]={1174.7f,1480.0f,1760.0f,2349.3f},rate[]={2.2f,2.6f,3.0f,3.6f};
    for(int k=0;k<CHORD;++k){chordStep[k]=Osc::step(hz[k]);blast[k].set(rate[k]);}
    thud.set(6);flash.set(30);pip.set(40);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // The gathering choir: A, C# and E, gliding up a fifth, with vibrato.
    if(t<SWING+.05f) {
      static constexpr float base[]={440.0f,554.4f,659.3f};
      const float s=clamp01(t*(1/CHARGE)),glide=1+.5f*s*s,wob=1+.006f*vib.next(5.5f);
      const float env=(.04f+.12f*s*s)*smooth(t*(1/.15f))*(1-smooth((t-BACK)*(1/.1f)));
      for(int k=0;k<VOICES;++k){const float f=base[k]*glide*wob;v+=env*(voice[k].next(f)+.4f*shimmer[k].next(f*2));}
    }
    // Motes arriving: quick high pings, denser as it charges.
    if(t<CHARGE) {
      const uint32_t now=uint32_t(t*30);
      if(now!=slot){slot=now;at=(now+rnd(now,71))*(1/30.0f);live=rnd(now,72)<.3f+.6f*t*(1/CHARGE);fired=false;}
      if(live && !fired && t>=at){static constexpr float scale[]={2637.0f,2960.0f,3520.0f,3951.1f,4434.9f};pip.trigger(.07f+.05f*rnd(now,73));pingStep=Osc::step(scale[hash(now*5+2)%5]);fired=true;}
    }
    if(pip.level>.0005f)v+=pip.next()*ping.advance(pingStep);
    // The draw back: a breath in; the swing: a whoosh rising in pitch.
    if(t>=CHARGE && t<BACK+.02f){const float u=(t-CHARGE)*(1/(BACK-CHARGE+.02f));v+=.18f*SINE[uint32_t(u*512)&1023]*(breath.next(x,.08f+.25f*u)-low.next(x,.03f));}
    if(t>=BACK && t<SWING+.06f){const float u=(t-BACK)*(1/(SWING-BACK+.06f));v+=.42f*SINE[uint32_t(u*512)&1023]*(wide.next(x,.2f+.5f*u)-low.next(x,.05f));}
    // The release.
    if(t>=SWING) {
      if(!released){static constexpr float level[]={.2f,.16f,.14f,.1f};for(int k=0;k<CHORD;++k)blast[k].trigger(level[k]);thud.trigger(.55f);flash.trigger(.6f);released=true;}
      const float a=t-SWING,attack=smooth(a*(1/.004f));
      for(int k=0;k<CHORD;++k)v+=attack*blast[k].next()*chord[k].advance(chordStep[k]);
      v+=thud.next()*boom.next(330-120*clamp01(a*(1/.3f)))*attack+flash.next()*crack.next(x,.6f);
    }
    return v*armoryTail(t,LENGTH);
  }
};
// Aegis: seven glassy notes rise as the petals bloom, the spear whistles
// in and lands with a heavy clang, a grinding buzz while it drills, a
// crack and a spray of high glass pings as each of six petals breaks, then
// a bright held chord as the last petal throws the spear back.
struct Aegis {
  static constexpr int PETALS=7,BREAKS=6;static constexpr float ARRIVE=.78f,BREAK0=.92f,STEP=.15f,REPEL=1.85f,LENGTH=2.6f;
  Noise noise{0x510e527fu};Smooth low,wide,crack,grit;Osc note[PETALS],shard[3],clang[3],buzz,whistle,hold[3];
  Decay bloom[PETALS],glass[BREAKS],snap[BREAKS],ring,chord;bool bloomed[PETALS]={},broke[BREAKS]={},struck=false,repelled=false;
  uint32_t noteStep[PETALS],shardStep[BREAKS][3],clangStep[3],holdStep[3];
  Aegis() {
    static constexpr float scale[]={1318.5f,1480.0f,1661.2f,1975.5f,2217.5f,2637.0f,2960.0f};
    for(int k=0;k<PETALS;++k){noteStep[k]=Osc::step(scale[k]);bloom[k].set(9);}
    for(int k=0;k<BREAKS;++k){for(int j=0;j<3;++j)shardStep[k][j]=Osc::step(3100+900*j+350*float(hash(k*3+j)%5));glass[k].set(22);snap[k].set(70);}
    static constexpr float metal[]={523.0f,1187.0f,1913.0f},bright[]={1760.0f,2217.5f,2637.0f};
    for(int k=0;k<3;++k){clangStep[k]=Osc::step(metal[k]);holdStep[k]=Osc::step(bright[k]);}
    ring.set(4.5f);chord.set(1.8f);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    for(int k=0;k<PETALS;++k) {
      const float at=.04f+.07f*k;if(t<at)continue;
      if(!bloomed[k]){bloom[k].trigger(.11f);bloomed[k]=true;}
      if(bloom[k].level>.0005f){const float y=note[k].advance(noteStep[k]);v+=smooth((t-at)*(1/.003f))*bloom[k].next()*(y+.3f*y*y*y);}
    }
    // The spear's whistle: a tone climbing as it closes in, over a rush.
    if(t>=.5f && t<ARRIVE){const float u=(t-.5f)*(1/(ARRIVE-.5f));v+=(.08f+.1f*u)*whistle.next(900+1300*u*u)+.2f*u*u*(wide.next(x,.3f)-low.next(x,.05f));}
    if(t>=ARRIVE) {
      if(!struck){ring.trigger(.32f);struck=true;}
      const float r=ring.next(),attack=smooth((t-ARRIVE)*(1/.002f));
      v+=attack*r*(clang[0].advance(clangStep[0])+.7f*clang[1].advance(clangStep[1])+.45f*clang[2].advance(clangStep[2]));
      // The drill: a gritty buzz until the spear is thrown back.
      if(t<REPEL){const float y=buzz.next(330+20*SINE[uint32_t(t*13*1024)&1023]);v+=.1f*smooth((t-ARRIVE)*(1/.05f))*(y>0?1.0f:-1.0f)*(.6f+.4f*(grit.next(x,.4f)*2));}
    }
    for(int k=0;k<BREAKS;++k) {
      const float at=BREAK0+STEP*k;if(t<at)continue;
      if(!broke[k]){glass[k].trigger(.1f);snap[k].trigger(.45f);broke[k]=true;}
      if(snap[k].level>.0005f)v+=snap[k].next()*(x-crack.next(x,.25f));
      if(glass[k].level>.0005f){const float g=glass[k].next();for(int j=0;j<3;++j)v+=g*shard[j].advance(shardStep[k][j]);}
    }
    if(t>=REPEL) {
      if(!repelled){chord.trigger(.14f);repelled=true;}
      const float c=chord.next()*smooth((t-REPEL)*(1/.01f));
      for(int k=0;k<3;++k)v+=c*hold[k].advance(holdStep[k]);
      if(t<REPEL+.25f){const float u=(t-REPEL)*4;v+=.25f*SINE[uint32_t(u*512)&1023]*(wide.next(x,.5f-.3f*u)-low.next(x,.05f));}
    }
    return v*armoryTail(t,LENGTH);
  }
};
// Trace: a soft two-note power-on, then the pen: a thin tone wandering in
// pitch with a fast scratch of ticks as the lines are drawn and a click at
// each corner; then the fill: a rising shimmer into a bright metallic
// shing, and the two blades ringing on together, slightly apart in pitch.
struct Trace {
  static constexpr float PEN=.22f,DRAWN=1.15f,FILL=1.22f,LENGTH=2.2f;static constexpr int PARTIALS=4;
  Noise noise{0x9b05688cu};Smooth tick,low,wide;Osc on,pen,ring[PARTIALS],twin[2],sweep;Decay blip,scratch,shing[PARTIALS],hum,corner;
  uint32_t ringStep[PARTIALS],twinStep[2],slot=~0u,cornerSlot=~0u;bool rung=false,blipped[2]={};float at=0;bool live=false,fired=false;
  Trace() {
    static constexpr float hz[]={2093.0f,3271.0f,4519.0f,5867.0f},rate[]={3.5f,5,7,10};
    for(int k=0;k<PARTIALS;++k){ringStep[k]=Osc::step(hz[k]);shing[k].set(rate[k]);}
    twinStep[0]=Osc::step(1318.5f);twinStep[1]=Osc::step(1326.0f);
    blip.set(30);scratch.set(500);hum.set(1.6f);corner.set(160);
  }
  float next(float t) {
    if(t>=LENGTH)return 0;
    const float x=noise.next();float v=0;
    // Power on: two quick rising blips.
    for(int k=0;k<2;++k){const float a=t-.03f-.09f*k;if(a<0)continue;if(!blipped[k]){blip.trigger(.18f);blipped[k]=true;}}
    if(t<.3f && blip.level>.0005f)v+=blip.next()*on.next(t<.12f?880:1320);
    // The pen: a thin wandering tone and a scratch of ticks.
    if(t>=PEN && t<DRAWN+.05f) {
      const float u=(t-PEN)*(1/(DRAWN-PEN)),wander=SINE[uint32_t(t*3.1f*1024)&1023]*.5f+SINE[uint32_t(t*7.7f*1024)&1023]*.25f;
      const float env=smooth((t-PEN)*(1/.06f))*(1-smooth((t-DRAWN)*(1/.05f)));
      v+=.07f*env*pen.next(1500+500*u+260*wander);
      const uint32_t now=uint32_t(t*45);
      if(now!=slot){slot=now;at=(now+rnd(now,91)*.6f)*(1/45.0f);live=rnd(now,92)<.75f;fired=false;}
      if(live && !fired && t>=at){scratch.trigger(.12f+.08f*rnd(now,93));fired=true;}
      // A firmer click at each of the outline's corners, about every 0.07 s.
      const uint32_t c=uint32_t((t-PEN)*(1/.071f));
      if(c!=cornerSlot){cornerSlot=c;corner.trigger(.2f);}
      v+=env*(scratch.next()*(x-tick.next(x,.5f))+corner.next()*(x-low.next(x,.2f)));
    }
    // The fill: a rising shimmer, then the shing and the twin ring.
    if(t>=FILL-.12f && t<FILL+.25f){const float u=(t-FILL+.12f)*(1/.37f);v+=.16f*SINE[uint32_t(u*512)&1023]*sweep.next(1800+2600*u)+.1f*SINE[uint32_t(u*512)&1023]*(wide.next(x,.5f)-tick.next(x,.2f));}
    if(t>=FILL) {
      if(!rung){static constexpr float level[]={.3f,.2f,.14f,.09f};for(int k=0;k<PARTIALS;++k)shing[k].trigger(level[k]);hum.trigger(.14f);rung=true;}
      const float attack=smooth((t-FILL)*(1/.003f));
      for(int k=0;k<PARTIALS;++k)v+=attack*shing[k].next()*ring[k].advance(ringStep[k]);
      const float h=hum.next()*smooth((t-FILL)*(1/.08f));v+=h*(twin[0].advance(twinStep[0])+twin[1].advance(twinStep[1]));
    }
    return v*armoryTail(t,LENGTH);
  }
};
}
}
