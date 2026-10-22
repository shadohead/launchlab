#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <cmath>

// Appearance is UI-only. No detector, IMU or metric policy lives here.
namespace Appearance {
// Persisted IDs 0..3 and their original colors stay unchanged.
// The palette names are Beyblade-inspired, rather than a usage ranking.
enum class Theme:uint8_t {Classic=0,Mint=1,Amber=2,Violet=3,GolemGreen=4,PhoenixRed=5,CobaltBlue=6,SharkPink=7,ValkyrieWhite=8,TyrannoOrange=9};
// Stored values: each effect keeps its number, so older records keep their
// meaning. Random came after Off, Slash after Random, then the elemental
// effects (Tidal..Radiance) and the physical ones (Spin..Bloom) in 0.16,
// then five anime-inspired sets of five in 0.17 (Crimson..Ravage) and six
// more in 0.18 (Haymaker..Blink), the first with a value above 63.
// Firmware that predates a value rejects a record selecting it and falls
// back to its other slot or the defaults.
enum class Effect:uint8_t {Orbit,Flames,Sparkles,Shockwave,Lightning,Off,Random,Slash,
  Tidal,Frost,Gale,Eclipse,Radiance,Spin,Storm,Quickdraw,Leaves,Bloom,
  Crimson,Bulwark,Crux,Klaxon,Prism,
  Sunwheel,Torrent,Blitz,Tigerclaw,Butterfly,
  Birdsong,Spiral,Pinwheel,Overdrive,Summon,
  Breach,Bladework,Moonpetal,Barrage,Glitch,
  Rupture,Boundless,Precision,Umbra,Ravage,
  Haymaker,Palms,Elastic,Chains,Waterleaf,
  Transmute,Ignition,Spires,Gate,Fracture,
  Timestop,Zipper,Rush,Menace,Sequel,
  Swordland,Treasury,Sunblade,Aegis,Trace,
  Volley,Starfall,Mimic,Scales,Meadow,
  Rev,Hammer,Foxbite,Detonate,Sharkfin,
  Slingshot,Presence,Threefold,Banner,Vapor,
  Crescent,Release,Petalfall,Mask,Blink};
// Celebration sound level; Off by default. The speaker is powered only while a sound plays.
enum class Sound:uint8_t {Off,Low,High};
// DRAWN effects animate: every effect except Off and Random.
constexpr unsigned THEMES=10,EFFECTS=83,DRAWN=81,SOUNDS=3;
// Theme IDs THEMES.. select the user's custom theme slots (theme_library.h).
// Firmware without custom themes rejects such a record and falls back to its
// other slot or the defaults, as it does for any newer record.
constexpr unsigned CUSTOM_THEMES=4,ALL_THEMES=THEMES+CUSTOM_THEMES;
inline bool custom(Theme t){return unsigned(t)>=THEMES && unsigned(t)<ALL_THEMES;}
inline bool drawn(unsigned e){return e<EFFECTS && e!=unsigned(Effect::Off) && e!=unsigned(Effect::Random);}
// The drawn effects in Settings order, 0..DRAWN-1, and back: the stored
// order without Off and Random.
inline Effect drawnEffect(unsigned i){return Effect(i<5?i:i+2);}
inline unsigned drawnIndex(Effect e){return unsigned(e)<5?unsigned(e):unsigned(e)-2;}
inline const char *label(Theme t){static const char *s[]={"Pegasus Cyan","Wolf Mint","Wizard Gold","Meteor Purple","Golem Green","Phoenix Red","Cobalt Blue","Shark Pink","Valkyrie White","Tyranno Orange"};return unsigned(t)<THEMES?s[unsigned(t)]:s[0];}
inline const char *label(Effect e) {
  static const char *s[]={"Orbit","Flames","Sparkles","Shockwave","Lightning","Off","Random","Slash",
    "Tidal","Frost","Gale","Eclipse","Radiance","Spin","Storm","Quickdraw","Leaves","Bloom",
    "Crimson","Bulwark","Crux","Klaxon","Prism",
    "Sunwheel","Torrent","Blitz","Tigerclaw","Butterfly",
    "Birdsong","Spiral","Pinwheel","Overdrive","Summon",
    "Breach","Bladework","Moonpetal","Barrage","Glitch",
    "Rupture","Boundless","Precision","Umbra","Ravage",
    "Haymaker","Palms","Elastic","Chains","Waterleaf",
    "Transmute","Ignition","Spires","Gate","Fracture",
    "Timestop","Zipper","Rush","Menace","Sequel",
    "Swordland","Treasury","Sunblade","Aegis","Trace",
    "Volley","Starfall","Mimic","Scales","Meadow",
    "Rev","Hammer","Foxbite","Detonate","Sharkfin",
    "Slingshot","Presence","Threefold","Banner","Vapor",
    "Crescent","Release","Petalfall","Mask","Blink"};
  static_assert(sizeof(s)/sizeof(s[0])==EFFECTS,"one label per effect");
  return unsigned(e)<EFFECTS?s[unsigned(e)]:s[0];
}
// Settings order: Random first, then each drawn effect, Off last.
inline Effect nextChoice(Effect e) {
  if(e==Effect::Random)return Effect::Orbit;
  if(!drawn(unsigned(e)))return Effect::Random;
  const unsigned i=drawnIndex(e)+1;return i<DRAWN?drawnEffect(i):Effect::Off;
}
// A Random pick: any drawn effect except the previous one.
inline Effect pick(uint32_t bits,Effect previous) {
  if(!drawn(unsigned(previous)))return drawnEffect(bits%DRAWN);
  const unsigned e=bits%(DRAWN-1);return drawnEffect(e>=drawnIndex(previous)?e+1:e);
}
inline const char *label(Sound s){static const char *n[]={"Off","Low","High"};return unsigned(s)<SOUNDS?n[unsigned(s)]:n[0];}
inline uint16_t accent(Theme t){static constexpr uint16_t c[]={0x07ff,0x8ff9,0xff10,0xc5bf,0x77e6,0xfa8a,0x5c7f,0xfb9f,0xffff,0xfd00};return unsigned(t)<THEMES?c[unsigned(t)]:c[0];}
inline uint32_t duration(Effect e) {
  static constexpr uint16_t ms[]={5000,5100,5000,4900,5000,0,0,5000,
    5000,5000,4800,5200,5000,  // Tidal, Frost, Gale, Eclipse, Radiance
    5000,5100,4900,5000,5100,  // Spin, Storm, Quickdraw, Leaves, Bloom
    5000,4800,5000,4800,5000,  // Crimson..Prism (Evangelion)
    5000,5000,4800,5000,5000,  // Sunwheel..Butterfly (Demon Slayer)
    5000,5000,5200,4800,5000,  // Birdsong..Summon (Naruto)
    5000,4800,5200,5000,4800,  // Breach..Glitch (NieR:Automata)
    4800,5200,4800,4800,5000,  // Rupture..Ravage (Jujutsu Kaisen)
    5000,5000,5000,5000,5000,  // Haymaker..Waterleaf (aura set)
    5000,5000,5000,5000,5000,  // Transmute..Fracture (alchemy set)
    5000,4800,4800,4800,4800,  // Timestop..Sequel (phantom set)
    5000,5000,5000,5000,5000,  // Swordland..Trace (armory set)
    5000,5000,5000,5000,5000,  // Volley..Meadow (grimoire set)
    5000,5000,5000,5000,5000,  // Rev..Sharkfin (ripsaw set)
    4900,4900,4800,5000,5000,  // Slingshot..Vapor (corsair set)
    4900,5000,5100,5000,4900}; // Crescent..Blink (reaper set)
  static_assert(sizeof(ms)/sizeof(ms[0])==EFFECTS,"one duration per effect");
  return unsigned(e)<EFFECTS?ms[unsigned(e)]:0;
}
// How long each effect's sound runs, from the effect's start: the hit and a
// short tail, never a drone under a long effect, and never past the effect.
inline uint32_t soundDuration(Effect e) {
  static constexpr uint16_t ms[]={1700,1800,1600,1300,1500,0,0,1700,
    2600,2600,2600,2800,2700,  // Tidal, Frost, Gale, Eclipse, Radiance
    2600,2600,2300,2500,2600,  // Spin, Storm, Quickdraw, Leaves, Bloom
    2600,2300,2300,2100,2400,  // Crimson..Prism
    2600,2400,1900,2400,2400,  // Sunwheel..Butterfly
    2400,2800,2800,2600,2300,  // Birdsong..Summon
    2600,2400,2800,2400,2000,  // Breach..Glitch
    1900,2800,2000,2200,2600,  // Rupture..Ravage
    2300,2600,2400,2400,2500,  // Haymaker..Waterleaf
    2400,2200,2200,2600,2400,  // Transmute..Fracture
    2600,2200,2400,2600,2700,  // Timestop..Sequel
    2600,2400,2600,2600,2200,  // Swordland..Trace
    2300,2400,2400,2300,2400,  // Volley..Meadow
    2600,2400,2400,2600,2400,  // Rev..Sharkfin
    2200,2500,2200,2400,2400,  // Slingshot..Vapor
    2200,2600,2400,2400,2000}; // Crescent..Blink
  static_assert(sizeof(ms)/sizeof(ms[0])==EFFECTS,"one sound length per effect");
  return unsigned(e)<EFFECTS?std::min<uint32_t>(ms[unsigned(e)],duration(e)):0;
}
inline uint32_t hash(const uint8_t *p,size_t n){uint32_t h=2166136261u;while(n--){h^=*p++;h*=16777619u;}return h;}
inline uint32_t get32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
struct Config {
  using Image=std::array<uint8_t,16>;
  Theme theme=Theme::Classic;Effect effect=Effect::Random;Sound sound=Sound::Off;
  uint32_t revision=0;
  uint32_t generation()const{return revision;}
  bool select(Theme t,Effect e){return select(t,e,sound);}
  bool select(Theme t,Effect e,Sound s){
    if(unsigned(t)>=ALL_THEMES || unsigned(e)>=EFFECTS || unsigned(s)>=SOUNDS)return false;
    if(t!=theme || e!=effect || s!=sound){theme=t;effect=e;sound=s;++revision;}return true;
  }
  // Byte 10 holds the sound level; it was reserved as zero before, so older
  // records read as Off. Firmware older than the sound setting rejects a
  // record with sound on and falls back to its other slot or defaults.
  void encode(Image &b)const{b={};put32(b.data(),0x01414c4c);put32(b.data()+4,revision);b[8]=uint8_t(theme);b[9]=uint8_t(effect);b[10]=uint8_t(sound);put32(b.data()+12,hash(b.data(),12));}
  bool decode(const uint8_t *b,size_t n){
    if(n!=16 || get32(b)!=0x01414c4c || get32(b+12)!=hash(b,12) || b[8]>=ALL_THEMES || b[9]>=EFFECTS || b[10]>=SOUNDS || b[11])return false;
    revision=get32(b+4);theme=Theme(b[8]);effect=Effect(b[9]);sound=Sound(b[10]);return true;
  }
};
// First post-upgrade valid pull seeds a profile/method baseline silently.
// Older bounded history has no provenance and is intentionally not migrated.
struct Bests {
  using Image=std::array<uint8_t,32>;
  float rpm[4]={};uint32_t revision=0;
  uint32_t generation()const{return revision;}
  bool observe(unsigned sensor,unsigned method,bool valid,float value){
    if(sensor>=2 || method>=2 || !valid || !std::isfinite(value) || value<1000 || value>1500000)return false;
    float &best=rpm[sensor*2+method];if(value<=best)return false;
    const bool beat=best>0;best=value;++revision;return beat;
  }
  void encode(Image &b)const{
    b={};put32(b.data(),0x01424c4c);put32(b.data()+4,revision);
    for(unsigned i=0;i<4;++i){uint32_t v;std::memcpy(&v,&rpm[i],4);put32(b.data()+8+i*4,v);}
    put32(b.data()+28,hash(b.data(),28));
  }
  bool decode(const uint8_t *b,size_t n){
    if(n!=32 || get32(b)!=0x01424c4c || get32(b+28)!=hash(b,28) || get32(b+24))return false;
    float values[4];for(unsigned i=0;i<4;++i){const auto v=get32(b+8+i*4);std::memcpy(&values[i],&v,4);if(!std::isfinite(values[i]) || (values[i]!=0 && (values[i]<1000 || values[i]>1500000)))return false;}
    revision=get32(b+4);std::memcpy(rpm,values,sizeof(rpm));return true;
  }
};
// A result owns one presentation token for the effect chosen when it was
// queued. Replay/navigation cannot re-arm it. It is a personal best, an RPM
// milestone (the threshold reached) or both; the recap's label says which.
class Celebration {
public:
  void clear(){*this=Celebration{};}
  void queue(bool show,uint32_t number,Effect effect,bool best=true,uint16_t milestone=0) {
    clear();pending_=show;number_=number;effect_=effect;best_=best;milestone_=milestone;
  }
  void captured(uint32_t number){if(pending_ && number==number_){pending_=false;badge_=true;}}
  void presented(uint32_t now){if(badge_ && !started_){started_=true;cue_=true;at_=now;}}
  // True once, right after the effect starts: the moment to start its sound.
  bool takeCue(){const bool cue=cue_;cue_=false;return cue;}
  bool badge()const{return badge_;}
  bool started()const{return started_;}
  Effect effect()const{return effect_;}
  bool best()const{return best_;}
  uint16_t milestone()const{return milestone_;}
  uint32_t elapsed(uint32_t now)const{return started_?uint32_t(now-at_):0;}
  bool moving(uint32_t now)const{return badge_ && started_ && elapsed(now)<duration(effect_);}
private:
  uint32_t number_=0,at_=0;Effect effect_=Effect::Off;bool pending_=false,badge_=false,started_=false,cue_=false,best_=true;
  uint16_t milestone_=0;
};
}
