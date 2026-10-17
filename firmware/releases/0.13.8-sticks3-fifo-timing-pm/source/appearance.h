#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>

// Appearance is UI-only. No detector, IMU or metric policy lives here.
namespace Appearance {
// Persisted IDs 0..3 and their original colors stay unchanged.
// The palette names are Beyblade-inspired, rather than a usage ranking.
enum class Theme:uint8_t {Classic=0,Mint=1,Amber=2,Violet=3,GolemGreen=4,PhoenixRed=5,CobaltBlue=6,SharkPink=7,ValkyrieWhite=8,TyrannoOrange=9};
// Stored values: Random was added last so older records keep their meaning.
enum class Effect:uint8_t {Orbit,Flames,Sparkles,Shockwave,Lightning,Off,Random};
// Celebration sound level; Off by default. The speaker is powered only while a sound plays.
enum class Sound:uint8_t {Off,Low,High};
// The first DRAWN effects animate; Off and Random do not themselves.
constexpr unsigned THEMES=10,EFFECTS=7,DRAWN=5,SOUNDS=3;
inline bool drawn(unsigned e){return e<DRAWN;}
inline const char *label(Theme t){static const char *s[]={"Pegasus Cyan","Wolf Mint","Wizard Gold","Meteor Purple","Golem Green","Phoenix Red","Cobalt Blue","Shark Pink","Valkyrie White","Tyranno Orange"};return unsigned(t)<THEMES?s[unsigned(t)]:s[0];}
inline const char *label(Effect e){static const char *s[]={"Orbit","Flames","Sparkles","Shockwave","Lightning","Off","Random"};return unsigned(e)<EFFECTS?s[unsigned(e)]:s[0];}
// Settings order: Random first, then each effect, Off last.
inline Effect nextChoice(Effect e){return e==Effect::Random?Effect::Orbit:e==Effect::Off?Effect::Random:Effect(unsigned(e)+1);}
// A Random pick: any drawn effect except the previous one.
inline Effect pick(uint32_t bits,Effect previous) {
  if(!drawn(unsigned(previous)))return Effect(bits%DRAWN);
  const unsigned e=bits%(DRAWN-1);return Effect(e>=unsigned(previous)?e+1:e);
}
inline const char *label(Sound s){static const char *n[]={"Off","Low","High"};return unsigned(s)<SOUNDS?n[unsigned(s)]:n[0];}
inline uint16_t accent(Theme t){static constexpr uint16_t c[]={0x07ff,0x8ff9,0xff10,0xc5bf,0x77e6,0xfa8a,0x5c7f,0xfb9f,0xffff,0xfd00};return unsigned(t)<THEMES?c[unsigned(t)]:c[0];}
inline uint32_t duration(Effect e){static constexpr uint16_t ms[]={1600,1800,1600,1300,1500,0,0};return unsigned(e)<EFFECTS?ms[unsigned(e)]:0;}
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
    if(unsigned(t)>=THEMES || unsigned(e)>=EFFECTS || unsigned(s)>=SOUNDS)return false;
    if(t!=theme || e!=effect || s!=sound){theme=t;effect=e;sound=s;++revision;}return true;
  }
  // Byte 10 holds the sound level; it was reserved as zero before, so older
  // records read as Off. Firmware older than the sound setting rejects a
  // record with sound on and falls back to its other slot or defaults.
  void encode(Image &b)const{b={};put32(b.data(),0x01414c4c);put32(b.data()+4,revision);b[8]=uint8_t(theme);b[9]=uint8_t(effect);b[10]=uint8_t(sound);put32(b.data()+12,hash(b.data(),12));}
  bool decode(const uint8_t *b,size_t n){
    if(n!=16 || get32(b)!=0x01414c4c || get32(b+12)!=hash(b,12) || b[8]>=THEMES || b[9]>=EFFECTS || b[10]>=SOUNDS || b[11])return false;
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
// queued. Replay/navigation cannot re-arm it.
class Celebration {
public:
  void clear(){*this=Celebration{};}
  void queue(bool beat,uint32_t number,Effect effect){clear();pending_=beat;number_=number;effect_=effect;}
  void captured(uint32_t number){if(pending_ && number==number_){pending_=false;badge_=true;}}
  void presented(uint32_t now){if(badge_ && !started_){started_=true;cue_=true;at_=now;}}
  // True once, right after the effect starts: the moment to start its sound.
  bool takeCue(){const bool cue=cue_;cue_=false;return cue;}
  bool badge()const{return badge_;}
  bool started()const{return started_;}
  Effect effect()const{return effect_;}
  uint32_t elapsed(uint32_t now)const{return started_?uint32_t(now-at_):0;}
  bool moving(uint32_t now)const{return badge_ && started_ && elapsed(now)<duration(effect_);}
private:
  uint32_t number_=0,at_=0;Effect effect_=Effect::Off;bool pending_=false,badge_=false,started_=false,cue_=false;
};
}
