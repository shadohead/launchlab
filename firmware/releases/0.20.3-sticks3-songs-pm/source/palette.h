#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Every color the UI draws, by role (RGB565). The built-in themes keep the
// exact values the screens used before palettes existed; custom themes set the
// editable roles and the rest are derived from them. The replay keeps the
// StickS3's own colors (blue screen, violet back, white USB cap): it depicts
// the device, not the theme.
namespace Palette {
// The LGFX named colors the UI used before palettes.
constexpr uint16_t BLACK=0x0000,WHITE=0xffff,DARKGREY=0x7bef,ORANGE=0xfda0,GREEN=0x07e0,RED=0xf800;

struct Colors {
  uint16_t background=BLACK; // every screen; celebrations add light onto it
  uint16_t text=WHITE;       // headings, values, list rows
  uint16_t muted=DARKGREY;   // footers, captions, chart axes, gauge rings
  uint16_t faint=0x39e7;     // the editor's cycle hint, replay scrub track and near box edges
  uint16_t accent=0x07ff;    // selection, highlights, personal best, level in range
  uint16_t number=WHITE;     // the main screen's RPM readout
  uint16_t warning=ORANGE;   // demo labels, out of level, low battery, failures
  uint16_t ready=GREEN;      // the detector's ready status
  uint16_t alert=RED;        // the tournament's recording-only card
  uint16_t sceneRear=0x18c3; // replay box rear edges
  uint16_t scenePlane=0x2965;// replay floor plane
  bool operator==(const Colors &o)const{return std::memcmp(this,&o,sizeof(Colors))==0;}
};

// Roles a custom theme sets, in record order. The others are derived.
enum Role:uint8_t {Background,Text,Muted,Accent,Number,Warning,Ready,Alert};
constexpr unsigned ROLES=8;
inline const char *roleName(unsigned r) {
  static const char *n[]={"background","text","muted","accent","number","warning","ready","alert"};
  return r<ROLES?n[r]:"";
}

// Channel-wise a+(b-a)*num/den, truncated toward a: a shade of b over a.
constexpr uint16_t mix(uint16_t a,uint16_t b,int num,int den) {
  const int ar=a>>11,ag=(a>>5)&63,ab=a&31,br=b>>11,bg=(b>>5)&63,bb=b&31;
  return uint16_t((ar+(br-ar)*num/den)<<11 | (ag+(bg-ag)*num/den)<<5 | (ab+(bb-ab)*num/den));
}
// The derived roles. On the built-in black and dark grey these give exactly
// the original faint, rear-edge and floor greys.
constexpr Colors derive(uint16_t background,uint16_t text,uint16_t muted,uint16_t accent,uint16_t number,
                        uint16_t warning,uint16_t ready,uint16_t alert) {
  Colors c{};
  c.background=background;c.text=text;c.muted=muted;c.accent=accent;c.number=number;
  c.warning=warning;c.ready=ready;c.alert=alert;
  c.faint=mix(background,muted,1,2);c.sceneRear=mix(background,muted,1,5);c.scenePlane=mix(background,muted,9,25);
  return c;
}
// A built-in theme: the original black UI with its accent. Pegasus Cyan keeps
// its white RPM readout; every other built-in draws it in the accent.
constexpr Colors builtIn(uint16_t accent,bool whiteNumber) {
  return derive(BLACK,WHITE,DARKGREY,accent,whiteNumber?WHITE:accent,ORANGE,GREEN,RED);
}

// ---- contrast ----------------------------------------------------------------
// WCAG relative luminance of an RGB565 color in millionths, from the sRGB
// transfer of each 5/6-bit channel as the panel shows it. Integer tables so
// the device and the web editor reach the same verdict at every boundary.
constexpr uint32_t LINEAR5[32]={0,2497,5371,9529,15134,22299,31130,41723,54165,68538,84921,103387,124004,146839,171956,
  199415,229275,261592,296421,333816,373826,416503,461894,510046,561007,614820,671530,731180,793811,859464,928181,1000000};
constexpr uint32_t LINEAR6[64]={0,1229,2457,3725,5261,7113,9300,11835,14732,18007,21671,25736,30215,35118,40456,46240,
  52479,59184,66364,74027,82184,90842,100010,109696,119908,130654,141942,153780,166174,179132,192661,206769,221461,
  236744,252626,269113,286211,303926,322265,341233,360838,381084,401978,423525,445731,468602,492144,516361,541259,
  566845,593122,620096,647772,676156,705253,735067,765604,796868,828865,861599,895074,929297,964270,1000000};
constexpr uint32_t luminance(uint16_t c) {
  return uint32_t((2126ull*LINEAR5[c>>11]+7152ull*LINEAR6[(c>>5)&63]+722ull*LINEAR5[c&31]+5000)/10000);
}
// Contrast ratio x100, (L1+0.05)/(L2+0.05) with the lighter color on top.
constexpr uint32_t contrast100(uint16_t a,uint16_t b) {
  const uint64_t la=luminance(a)+50000,lb=luminance(b)+50000;
  return uint32_t(la>=lb?la*100/lb:lb*100/la);
}
// Advice, not rules: a custom theme may use any colors, and these thresholds
// only produce warnings (the USB `T` reply, the web editor). Text roles are
// advised 4.5:1 on the background (WCAG AA for normal text), muted captions
// 3:1; under 1.5:1 a role is nearly invisible. A light background is allowed,
// but celebrations draw by adding light, which it washes out.
constexpr uint32_t TEXT_CONTRAST100=450,MUTED_CONTRAST100=300,INVISIBLE_CONTRAST100=150,LIGHT_BACKGROUND_LUMINANCE=50000;
inline uint32_t advisedContrast100(unsigned role){return role==Muted?MUTED_CONTRAST100:role==Background?0:TEXT_CONTRAST100;}
enum class Legibility:uint8_t {Ok,Low,Invisible};
inline Legibility legibility(unsigned role,uint16_t color,uint16_t background) {
  if(role==Background || role>=ROLES)return Legibility::Ok;
  const uint32_t c=contrast100(color,background);
  return c<INVISIBLE_CONTRAST100?Legibility::Invisible:c<advisedContrast100(role)?Legibility::Low:Legibility::Ok;
}
struct Advice {
  bool lightBackground=false;
  Legibility role[ROLES]={};
  bool any()const{for(auto l:role)if(l!=Legibility::Ok)return true;return lightBackground;}
  // "none", or a comma list of light_background, low_contrast:<role> and
  // invisible:<role> in role order. 192 bytes always suffice.
  void format(char *out,size_t n)const {
    if(!n)return;
    size_t at=0;out[0]=0;
    auto put=[&](const char *a,const char *b){const int w=std::snprintf(out+at,n-at,"%s%s%s",at?",":"",a,b);if(w>0)at=std::min(n-1,at+size_t(w));};
    if(lightBackground)put("light_background","");
    for(unsigned r=Text;r<ROLES;++r)if(role[r]!=Legibility::Ok)put(role[r]==Legibility::Invisible?"invisible:":"low_contrast:",roleName(r));
    if(!at)std::snprintf(out,n,"none");
  }
};
constexpr size_t ADVICE_TEXT=192;

// ---- custom themes -----------------------------------------------------------
// The only refusal left is a malformed name: colors are never refused.
enum class Problem:uint8_t {None,Name};
inline const char *problemName(Problem p){return p==Problem::Name?"name":"none";}
struct Custom {
  static constexpr unsigned NAME_BYTES=15,MAX_NAME=NAME_BYTES-1,BYTES=1+NAME_BYTES+2*ROLES;
  using Image=uint8_t[BYTES];
  char name[NAME_BYTES]={};
  uint16_t role[ROLES]={};
  // One to 14 printable ASCII characters, not starting or ending with a
  // space, NUL padded; the Settings editor shows it like a built-in name.
  static bool validName(const char *n,size_t bytes) {
    size_t length=0;while(length<bytes && n[length])++length;
    if(!length || length>MAX_NAME || n[0]==' ' || n[length-1]==' ')return false;
    for(size_t i=0;i<length;++i)if(n[i]<0x20 || n[i]>0x7e)return false;
    for(size_t i=length;i<bytes;++i)if(n[i])return false;
    return true;
  }
  Problem check()const{return validName(name,NAME_BYTES)?Problem::None:Problem::Name;}
  bool valid()const{return check()==Problem::None;}
  Advice advice()const {
    Advice a;a.lightBackground=luminance(role[Background])>LIGHT_BACKGROUND_LUMINANCE;
    for(unsigned r=Text;r<ROLES;++r)a.role[r]=legibility(r,role[r],role[Background]);
    return a;
  }
  Colors colors()const{return derive(role[Background],role[Text],role[Muted],role[Accent],role[Number],role[Warning],role[Ready],role[Alert]);}
  // Record: a used flag (1), the name, then each role little-endian.
  void encode(uint8_t *out)const {
    out[0]=1;std::memcpy(out+1,name,NAME_BYTES);
    for(unsigned r=0;r<ROLES;++r){out[1+NAME_BYTES+2*r]=uint8_t(role[r]);out[2+NAME_BYTES+2*r]=uint8_t(role[r]>>8);}
  }
  // Decodes a used record; false for an empty one or a malformed flag.
  bool decode(const uint8_t *in) {
    if(in[0]!=1)return false;
    std::memcpy(name,in+1,NAME_BYTES);
    for(unsigned r=0;r<ROLES;++r)role[r]=uint16_t(in[1+NAME_BYTES+2*r]|in[2+NAME_BYTES+2*r]<<8);
    return true;
  }
};
}
