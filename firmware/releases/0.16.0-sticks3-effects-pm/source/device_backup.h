#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include "appearance.h"
#include "milestones.h"
#include "launch_motion.h"
#include "power_status.h"
#include "practice_history.h"
#include "rpm_estimator.h"
#include "sensor_profile.h"
#include "mount_orientation.h"
#include "theme_library.h"

// A portable copy of everything a player would miss after a crash or on a new
// M5: history, personal bests, appearance, custom themes, the Celebrate mode
// and milestone table, the last recap and settings. Bench
// settings (sensor GPIO, 5 V output) and the power log are device-specific
// and stay out. Pure data: encoding, validation and the USB text transfer.
// The sketch applies a restore inside its ADC-owned idle checkpoint.
namespace DeviceBackup {
// Format 2 adds the custom themes section. A backup without custom themes,
// whose look selects a built-in, is still written as format 1, so firmware
// and hosts that predate custom themes keep reading it; both formats restore.
// Format 3 adds the milestones section (mode and table), written only when
// they differ from the defaults; its custom themes section is optional. A
// backup with default milestones stays format 1 or 2. Format 4 (0.16) carries
// a table 0.15's record cannot hold (effect pools, more than eight rows or an
// effect 0.15 lacks) in the pooled section instead; any other table is still
// written as format 3, byte for byte, so 0.15 restores it. Older firmware
// refuses format 4 before writing anything. Every format restores here.
constexpr uint32_t MAGIC=0x4b424c4c,FORMAT=4,MILESTONES_FORMAT=3,THEMES_FORMAT=2,LEGACY_FORMAT=1; // "LLBK"
constexpr unsigned HEADER_BYTES=44,RUNTIME_BYTES=32;
enum Section:uint8_t {Practice=1,Look=2,Bests=3,Motion=4,Settings=5,CustomThemes=6,MilestoneTable=7,MilestonePools=8};

struct UserSettings {
  uint8_t sleepMinutes=InactivityTimer::DEFAULT_MINUTES,brightness=40;
  RpmEstimator::Mode rpmMethod=RpmEstimator::DEFAULT_MODE;
  StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
  bool flipped=false;
  MountOrientation::Mode mount=MountOrientation::Mode::Top;
  static bool validBrightness(uint8_t value){return value>=10 && value<=100 && value%10==0;}
};

struct Contents {
  PracticeHistory practice;
  Appearance::Config look;
  Appearance::Bests bests;
  bool hasMotion=false;
  LaunchMotion::Trace motion;
  UserSettings settings;
  // Format 2: the custom themes record. A restore replaces the device's
  // themes only when the backup carries them.
  bool hasThemes=false;
  Themes::Library themes;
  // Format 3 or 4: the milestones record. A restore replaces the device's
  // milestones only when the backup carries them.
  bool hasMilestones=false;
  Milestones::Config milestones;
  char runtime[RUNTIME_BYTES+1]={};
  bool themesSection()const{return hasThemes || Appearance::custom(look.theme);}
  uint32_t format()const{return hasMilestones?(milestones.fitsV1()?MILESTONES_FORMAT:FORMAT):themesSection()?THEMES_FORMAT:LEGACY_FORMAT;}
};

constexpr unsigned SETTINGS_BYTES=8;
constexpr unsigned MAX_BYTES=HEADER_BYTES+6*8+PracticeHistory::IMAGE_BYTES+16+32+LaunchMotion::IMAGE_BYTES+SETTINGS_BYTES+
  Themes::Library::IMAGE_BYTES+8+Milestones::Config::IMAGE_BYTES+4;
using Buffer=std::array<uint8_t,MAX_BYTES>;

inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline uint32_t get32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline uint32_t checksum(const uint8_t *p,size_t n){uint32_t h=2166136261u;while(n--){h^=*p++;h*=16777619u;}return h;}

// Header: magic, format, total length, runtime (NUL padded). Then sections of
// id, three zero bytes, length and the record's own versioned image, and a
// trailing checksum over everything before it.
// The record images are kilobytes: they live on the heap, never the stack.
// On the device these run from the 8 KB loop task (0.11.0 overflowed it).
inline size_t encode(const Contents &c,const char *runtime,Buffer &out) {
  auto practice=std::unique_ptr<PracticeHistory::Image>(new(std::nothrow) PracticeHistory::Image);
  auto motion=std::unique_ptr<LaunchMotion::Image>(new(std::nothrow) LaunchMotion::Image);
  if(!practice || !motion)return 0;
  out.fill(0);size_t p=HEADER_BYTES;
  auto section=[&](Section id,const uint8_t *data,size_t n){out[p]=id;put32(out.data()+p+4,uint32_t(n));std::memcpy(out.data()+p+8,data,n);p+=8+n;};
  c.practice.encode(*practice);section(Practice,practice->data(),practice->size());
  Appearance::Config::Image look;c.look.encode(look);section(Look,look.data(),look.size());
  Appearance::Bests::Image bests;c.bests.encode(bests);section(Bests,bests.data(),bests.size());
  if(c.hasMotion && c.motion.valid()){LaunchMotion::encode(c.motion,1,*motion);section(Motion,motion->data(),motion->size());}
  const uint8_t settings[SETTINGS_BYTES]={c.settings.sleepMinutes,c.settings.brightness,uint8_t(c.settings.rpmMethod),
    uint8_t(c.settings.sensorProfile),uint8_t(c.settings.flipped),uint8_t(c.settings.mount),0,0};
  section(Settings,settings,sizeof(settings));
  const uint32_t format=c.format();
  if(c.themesSection()){Themes::Library::Image themes;c.themes.encode(themes);section(CustomThemes,themes.data(),themes.size());}
  if(format==MILESTONES_FORMAT){Milestones::Config::V1Image table;c.milestones.encodeV1(table);section(MilestoneTable,table.data(),table.size());}
  if(format==FORMAT){Milestones::Config::Image table;c.milestones.encode(table);section(MilestonePools,table.data(),table.size());}
  put32(out.data(),MAGIC);put32(out.data()+4,format);put32(out.data()+8,uint32_t(p+4));
  std::strncpy(reinterpret_cast<char *>(out.data()+12),runtime,RUNTIME_BYTES);
  out[12+RUNTIME_BYTES-1]=0;
  put32(out.data()+p,checksum(out.data(),p));
  return p+4;
}

// All-or-nothing: every record must pass its own validation, each section may
// appear once, and history, appearance, bests and settings are all required.
// Custom themes appear in format 2 (always) and 3-4 (optional); milestones in
// format 3 (0.15's record) or 4 (the pooled record), and always there.
inline bool decode(const uint8_t *data,size_t n,Contents &out) {
  const uint32_t format=n>=8?get32(data+4):0;
  if(n<HEADER_BYTES+4 || n>MAX_BYTES || get32(data)!=MAGIC || format<LEGACY_FORMAT || format>FORMAT || get32(data+8)!=n ||
     get32(data+n-4)!=checksum(data,n-4) || std::memchr(data+12,0,RUNTIME_BYTES)==nullptr)return false;
  auto c=std::unique_ptr<Contents>(new(std::nothrow) Contents);
  if(!c)return false;
  std::memcpy(c->runtime,data+12,RUNTIME_BYTES);
  unsigned seen=0;size_t p=HEADER_BYTES;
  while(p<n-4) {
    if(n-4-p<8 || data[p+1] || data[p+2] || data[p+3])return false;
    const uint8_t id=data[p];const uint32_t length=get32(data+p+4);const uint8_t *body=data+p+8;
    const uint8_t last=format==FORMAT?MilestonePools:format==MILESTONES_FORMAT?MilestoneTable:format==THEMES_FORMAT?CustomThemes:Settings;
    if(length>n-4-p-8 || id<Practice || id>last || (seen&(1u<<id)) || (format==FORMAT && id==MilestoneTable))return false;
    seen|=1u<<id;
    bool ok=false;
    switch(Section(id)) {
      case Practice:ok=c->practice.decode(body,length);break;
      case Look:ok=c->look.decode(body,length);break;
      case CustomThemes:ok=c->themes.decode(body,length);c->hasThemes=ok;break;
      case MilestoneTable:ok=length==Milestones::Config::V1_IMAGE_BYTES && c->milestones.decode(body,length);c->hasMilestones=ok;break;
      case MilestonePools:ok=length==Milestones::Config::IMAGE_BYTES && c->milestones.decode(body,length);c->hasMilestones=ok;break;
      case Bests:ok=c->bests.decode(body,length);break;
      case Motion:{
        if(length!=LaunchMotion::IMAGE_BYTES)break;
        auto image=std::unique_ptr<LaunchMotion::Image>(new(std::nothrow) LaunchMotion::Image);
        if(!image)break;
        std::memcpy(image->data(),body,length);uint32_t generation=0;
        ok=LaunchMotion::decode(*image,c->motion,generation) && c->motion.valid();c->hasMotion=ok;break;
      }
      case Settings:{
        // The first reserved byte now carries mounting. Legacy backups are
        // zero (Top); older firmware rejects Sidecar before writing anything.
        if(length!=SETTINGS_BYTES || body[6] || body[7])break;
        auto &s=c->settings;
        ok=InactivityTimer::validMinutes(body[0]) && UserSettings::validBrightness(body[1]) &&
          RpmEstimator::valid(body[2]) && StickS3SensorProfile::valid(body[3]) && body[4]<=1 && MountOrientation::valid(body[5]);
        if(ok){s.sleepMinutes=body[0];s.brightness=body[1];s.rpmMethod=RpmEstimator::Mode(body[2]);
          s.sensorProfile=StickS3SensorProfile::Mode(body[3]);s.flipped=body[4];s.mount=MountOrientation::Mode(body[5]);}
        break;
      }
    }
    if(!ok)return false;
    p+=8+length;
  }
  // Format 2 exists to carry the custom themes, and formats 3 and 4 the milestones.
  const unsigned required=(1u<<Practice)|(1u<<Look)|(1u<<Bests)|(1u<<Settings)|
    (format==THEMES_FORMAT?1u<<CustomThemes:0u)|(format==MILESTONES_FORMAT?1u<<MilestoneTable:0u)|(format==FORMAT?1u<<MilestonePools:0u);
  if(p!=n-4 || (seen&required)!=required)return false;
  out=*c;return true;
}

// Base64 over USB, one line per chunk, so a host can pace its writes on the
// device's acknowledgements.
constexpr char ALPHABET[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr unsigned TEXT_BYTES=(MAX_BYTES+2)/3*4;
inline int sextet(char c) {
  if(c>='A' && c<='Z')return c-'A';
  if(c>='a' && c<='z')return c-'a'+26;
  if(c>='0' && c<='9')return c-'0'+52;
  return c=='+'?62:c=='/'?63:-1;
}
// Writes the base64 of data[0..n) in lines of at most `width` characters.
template<class Line> void base64Lines(const uint8_t *data,size_t n,unsigned width,Line line) {
  char text[97];unsigned used=0;width=width>96?96:width-width%4;
  for(size_t i=0;i<n;i+=3) {
    const uint32_t v=uint32_t(data[i])<<16|(i+1<n?uint32_t(data[i+1])<<8:0)|(i+2<n?data[i+2]:0);
    text[used++]=ALPHABET[v>>18&63];text[used++]=ALPHABET[v>>12&63];
    text[used++]=i+1<n?ALPHABET[v>>6&63]:'=';text[used++]=i+2<n?ALPHABET[v&63]:'=';
    if(used>=width || i+3>=n){text[used]=0;line(text);used=0;}
  }
}
inline bool base64Decode(const char *text,size_t n,uint8_t *out,size_t capacity,size_t &written) {
  written=0;
  if(n%4)return false;
  for(size_t i=0;i<n;i+=4) {
    const int a=sextet(text[i]),b=sextet(text[i+1]);
    const bool pad2=text[i+2]=='=',pad3=text[i+3]=='=';
    const int c=pad2?0:sextet(text[i+2]),d=pad3?0:sextet(text[i+3]);
    if(a<0 || b<0 || c<0 || d<0 || (pad2 && !pad3) || ((pad2 || pad3) && i+4!=n))return false;
    const uint32_t v=uint32_t(a)<<18|uint32_t(b)<<12|uint32_t(c)<<6|uint32_t(d);
    const unsigned bytes=pad2?1:pad3?2:3;
    if(written+bytes>capacity)return false;
    out[written++]=uint8_t(v>>16);if(bytes>1)out[written++]=uint8_t(v>>8);if(bytes>2)out[written++]=uint8_t(v);
  }
  return true;
}

// Collects a transfer sent as base64 lines ending in a "." line: a restore,
// or a theme request. Bounded in size and time; any stray character abandons
// the transfer.
enum class TransferEvent {None,Chunk,Complete,Error};
template<unsigned CAPACITY> class LineReceiver {
public:
  static constexpr uint32_t IDLE_MS=15000;
  using Event=TransferEvent;
  // Reset in place: a `*this=LineReceiver{}` temporary would put the 5 KB buffer on the stack.
  void start(uint32_t now){length_=line_=0;dot_=false;error_="";active_=true;last_=now;}
  void cancel(){active_=false;}
  bool active()const{return active_;}
  bool timedOut(uint32_t now)const{return active_ && uint32_t(now-last_)>=IDLE_MS;}
  unsigned received()const{return length_;}
  const char *error()const{return error_;}
  Event feed(char c,uint32_t now) {
    if(!active_)return Event::None;
    last_=now;
    if(c=='\r')return Event::None;
    if(c=='\n') {
      if(dot_){dot_=false;active_=false;return Event::Complete;}
      if(!line_)return Event::None;
      line_=0;return Event::Chunk;
    }
    if(c=='.' && !line_ && !dot_){dot_=true;return Event::None;}
    if(dot_)return fail("bad_terminator");
    if(sextet(c)<0 && c!='=')return fail("bad_character");
    if(length_>=CAPACITY)return fail("too_large");
    text_[length_++]=c;++line_;return Event::None;
  }
  bool take(uint8_t *out,size_t capacity,size_t &written)const{return base64Decode(text_,length_,out,capacity,written);}
private:
  Event fail(const char *why){error_=why;active_=false;return Event::Error;}
  char text_[CAPACITY]={};
  unsigned length_=0,line_=0;
  uint32_t last_=0;
  bool active_=false,dot_=false;
  const char *error_="";
};
using Receiver=LineReceiver<TEXT_BYTES>;
}
