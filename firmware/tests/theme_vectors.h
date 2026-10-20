// Shared test vectors for the web theme editor (beywatch
// tests/launchlab/fixtures/theme-vectors.json): luminance, contrast and
// validation verdicts, the built-in palettes, slot and request records and a
// library image, all computed by the firmware's own code. test_palette checks
// fixtures/theme_vectors.json against this; theme_fixture.cpp prints it.
#pragma once
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../LaunchLabMini/device_backup.h"
#include "../LaunchLabMini/theme_library.h"

namespace ThemeVectors {
inline Palette::Custom theme(const char *name,std::initializer_list<uint16_t> roles) {
  Palette::Custom t;std::strncpy(t.name,name,Palette::Custom::NAME_BYTES);
  unsigned i=0;for(uint16_t r:roles)t.role[i++]=r;return t;
}
// Valid and invalid themes covering every rule and each contrast boundary.
inline std::vector<Palette::Custom> themes() {
  return {
    theme("Dragoon Storm",{0x0000,0xffff,0x7bef,0x5c7f,0x5c7f,0xfda0,0x07e0,0xf800}),
    theme("Night Ember",{0x1082,0xef5d,0x8410,0xfb20,0xffff,0xfe60,0x5fea,0xfa49}),
    theme("Abyss",{0x0006,0xffff,0x5acb,0x2d7f,0x2d7f,0xfda0,0x07e0,0xf800}),   // muted just under 3:1
    theme("Abyss",{0x0006,0xffff,0x5aeb,0x2d7f,0x2d7f,0xfda0,0x07e0,0xf800}),   // muted just over 3:1
    theme("Grey Day",{0x4208,0xffff,0xc618,0xffe0,0xffe0,0xfda0,0x07e0,0xffff}),// background too light
    theme("Low Ink",{0x0000,0x4a49,0x7bef,0x07ff,0x07ff,0xfda0,0x07e0,0xf800}),  // text under 4.5:1
    theme("Dim Alert",{0x0000,0xffff,0x7bef,0x07ff,0x07ff,0xfda0,0x07e0,0x9000}),// alert under 4.5:1
    theme("",{0x0000,0xffff,0x7bef,0x07ff,0x07ff,0xfda0,0x07e0,0xf800}),         // no name
    theme(" Lead",{0x0000,0xffff,0x7bef,0x07ff,0x07ff,0xfda0,0x07e0,0xf800}),    // leading space
    theme("Fourteen chars",{0x0000,0xffff,0x7bef,0x07ff,0x07ff,0xfda0,0x07e0,0xf800}),
    theme("Tab\tName",{0x0000,0xffff,0x7bef,0x07ff,0x07ff,0xfda0,0x07e0,0xf800}),// control character
  };
}
inline std::string hex(const uint8_t *p,size_t n){std::string s;char b[3];for(size_t i=0;i<n;++i){std::snprintf(b,3,"%02x",p[i]);s+=b;}return s;}
inline std::string base64(const uint8_t *p,size_t n){std::string s;DeviceBackup::base64Lines(p,n,96,[&](const char *line){s+=line;});return s;}
inline std::string str(const char *s) {
  std::string out="\"";
  for(const char *c=s;*c;++c){if(*c=='"' || *c=='\\')out+='\\';if(uint8_t(*c)<0x20){char b[8];std::snprintf(b,8,"\\u%04x",*c);out+=b;}else out+=*c;}
  return out+"\"";
}
inline std::string colors(const Palette::Colors &c) {
  char b[400];std::snprintf(b,sizeof(b),"{\"background\":%u,\"text\":%u,\"muted\":%u,\"faint\":%u,\"accent\":%u,\"number\":%u,"
    "\"warning\":%u,\"ready\":%u,\"alert\":%u,\"sceneRear\":%u,\"scenePlane\":%u}",c.background,c.text,c.muted,c.faint,c.accent,
    c.number,c.warning,c.ready,c.alert,c.sceneRear,c.scenePlane);
  return b;
}
inline std::string json() {
  std::string o="{\n  \"schema\": 1,\n";
  o+="  \"roles\": [";for(unsigned r=0;r<Palette::ROLES;++r)o+=(r?",":"")+str(Palette::roleName(r));o+="],\n";
  char b[160];
  std::snprintf(b,sizeof(b),"  \"rules\": {\"text\": %u, \"muted\": %u, \"maxBackgroundLuminance\": %u, \"maxName\": %u, \"slots\": %u, \"builtIns\": %u},\n",
    Palette::TEXT_CONTRAST100,Palette::MUTED_CONTRAST100,Palette::MAX_BACKGROUND_LUMINANCE,Palette::Custom::MAX_NAME,Themes::SLOTS,Appearance::THEMES);o+=b;
  o+="  \"builtIns\": [\n";
  for(unsigned t=0;t<Appearance::THEMES;++t)o+="    {\"id\": "+std::to_string(t)+", \"name\": "+str(Appearance::label(Appearance::Theme(t)))+", \"colors\": "+
    colors(Themes::builtIn(Appearance::Theme(t)))+"}"+(t+1<Appearance::THEMES?",\n":"\n");
  o+="  ],\n  \"luminance\": [";
  const uint16_t samples[]={0x0000,0x0001,0x0020,0x0800,0x0841,0x7bef,0x39e7,0x07ff,0xfda0,0x07e0,0xf800,0xffff,0x4208,0x6b6d};
  for(size_t i=0;i<sizeof(samples)/2;++i){std::snprintf(b,sizeof(b),"%s[%u, %u]",i?", ":"",samples[i],Palette::luminance(samples[i]));o+=b;}
  o+="],\n  \"contrast\": [";
  for(size_t i=0;i+1<sizeof(samples)/2;++i){std::snprintf(b,sizeof(b),"%s[%u, %u, %u]",i?", ":"",samples[i],samples[i+1],Palette::contrast100(samples[i],samples[i+1]));o+=b;}
  o+="],\n  \"themes\": [\n";
  const auto all=themes();
  for(size_t i=0;i<all.size();++i) {
    const auto &t=all[i];unsigned failed=Palette::ROLES;const auto problem=t.check(&failed);
    uint8_t slot[Themes::SLOT_BYTES];t.encode(slot);
    o+="    {\"name\": "+str(t.name)+", \"roles\": [";
    for(unsigned r=0;r<Palette::ROLES;++r)o+=(r?", ":"")+std::to_string(t.role[r]);
    o+="], \"problem\": "+str(Palette::problemName(problem))+", \"failedRole\": "+
      (problem==Palette::Problem::Contrast?str(Palette::roleName(failed)):std::string("null"))+
      ", \"slot\": "+str(base64(slot,sizeof(slot)).c_str())+", \"colors\": "+colors(t.colors())+"}"+(i+1<all.size()?",\n":"\n");
  }
  o+="  ],\n  \"requests\": [\n";
  struct R {Themes::Request::Op op;uint8_t argument;int theme;};
  const R requests[]={{Themes::Request::Op::Put,0,0},{Themes::Request::Op::Put,3,1},{Themes::Request::Op::Delete,2,-1},
                      {Themes::Request::Op::Select,11,-1},{Themes::Request::Op::Select,4,-1},{Themes::Request::Op::Clear,0,-1}};
  for(size_t i=0;i<sizeof(requests)/sizeof(requests[0]);++i) {
    Themes::Request q;q.op=requests[i].op;q.argument=requests[i].argument;if(requests[i].theme>=0)q.theme=all[size_t(requests[i].theme)];
    uint8_t bytes[Themes::Request::BYTES];q.encode(bytes);
    o+="    {\"op\": "+str(Themes::Request::name(q.op))+", \"argument\": "+std::to_string(q.argument)+", \"theme\": "+
      (requests[i].theme>=0?std::to_string(requests[i].theme):std::string("null"))+", \"base64\": "+str(base64(bytes,sizeof(bytes)).c_str())+"}"+
      (i+1<sizeof(requests)/sizeof(requests[0])?",\n":"\n");
  }
  Themes::Library library;library.put(0,all[0]);library.put(3,all[1]);library.put(1,all[1]);library.remove(1);
  Themes::Library::Image image;library.encode(image);
  o+="  ],\n  \"library\": {\"revision\": "+std::to_string(library.revision)+", \"image\": "+str(base64(image.data(),image.size()).c_str())+
    ", \"states\": [";
  for(unsigned i=0;i<Themes::SLOTS;++i)o+=(i?", ":"")+str(Themes::stateName(library.state(i)));
  o+="]}\n}\n";
  return o;
}
}
