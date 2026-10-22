// Shared test vectors for the web effect set editor (beywatch
// tests/launchlab/fixtures/effect-set-vectors.json): the effects, the default
// and full sets, mask verdicts, records, USB requests, the Best effect cycle
// and Random picks, all computed by the firmware's own code. test_effect_set
// checks fixtures/effect_set_vectors.json against this; milestone_fixture.cpp
// prints it with the argument `effect-set`.
#pragma once
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>
#include "../LaunchLabMini/device_backup.h"
#include "../LaunchLabMini/effect_set.h"

namespace EffectSetVectors {
using Appearance::Effect;using EffectSet::flag;using EffectSet::Mask;
inline Mask of(std::initializer_list<unsigned> ids){Mask m;for(unsigned e:ids)m|=flag(e);return m;}
// Valid sets and one failing each rule.
inline std::vector<Mask> masks() {
  Mask anime,later;for(unsigned e=18;e<43;++e)anime|=flag(e);for(unsigned e=43;e<Appearance::EFFECTS;++e)later|=flag(e);
  return {
    EffectSet::DEFAULT_MASK,EffectSet::ALL,of({7}),of({0,18,42}),anime,of({2,8,13,23,33,38}),
    later,                               // 0.18's eight sets
    of({1,63}),of({2,64}),of({0,43,63,64,82}),  // either side of the 64-bit limit
    Mask(),                              // nothing offered
    EffectSet::DEFAULT_MASK|flag(5),      // Off
    of({6}),                             // Random
    of({0,Appearance::EFFECTS}),         // past the last effect
    of({3,127}),                         // the mask's last bit
  };
}
inline std::string hex(const uint8_t *p,size_t n){std::string s;char b[3];for(size_t i=0;i<n;++i){std::snprintf(b,3,"%02x",p[i]);s+=b;}return s;}
inline std::string hexMask(Mask m){char b[33];m.hex(b);return b;}
inline std::string base64(const uint8_t *p,size_t n){std::string s;DeviceBackup::base64Lines(p,n,96,[&](const char *line){s+=line;});return s;}
inline std::string ids(Mask mask){std::string o="[";bool first=true;for(unsigned e=0;e<Mask::BITS;++e)if(mask.has(e)){o+=(first?"":",")+std::to_string(e);first=false;}return o+"]";}
inline std::string json() {
  char b[512];std::string o="{\n  \"schema\": 1,\n";
  std::snprintf(b,sizeof(b),"  \"recordBytes\": %u, \"requestBytes\": %u, \"requestOp\": %u, \"defaultBelow\": %u,\n",
    EffectSet::Config::IMAGE_BYTES,EffectSet::Request::BYTES,unsigned(EffectSet::Request::OP),EffectSet::DEFAULT_BELOW);o+=b;
  o+="  \"all\": \""+hexMask(EffectSet::ALL)+"\", \"default\": \""+hexMask(EffectSet::DEFAULT_MASK)+"\",\n";
  {EffectSet::Config d;EffectSet::Config::Image image;d.encode(image);o+="  \"defaultRecord\": \""+hex(image.data(),image.size())+"\",\n";}
  o+="  \"effects\": [";
  for(unsigned e=0;e<Appearance::EFFECTS;++e){std::snprintf(b,sizeof(b),"%s{\"id\":%u,\"label\":\"%s\",\"drawn\":%s,\"default\":%s}",e?",":"",e,
    Appearance::label(Effect(e)),Appearance::drawn(e)?"true":"false",EffectSet::offers(EffectSet::DEFAULT_MASK,Effect(e))?"true":"false");o+=b;}
  o+="],\n  \"sets\": [\n";
  const auto all=masks();
  for(size_t i=0;i<all.size();++i) {
    const Mask mask=all[i];unsigned at=0;const auto problem=EffectSet::check(mask,&at);
    std::string record="null",request="null",cycle="null";
    if(problem==EffectSet::Problem::None) {
      EffectSet::Config c;c.set(mask);c.revision=7;EffectSet::Config::Image image;c.encode(image);record="\""+hex(image.data(),image.size())+"\"";
      uint8_t q[EffectSet::Request::BYTES];EffectSet::Request::encode(mask,q);request="\""+base64(q,sizeof(q))+"\"";
      // The editor's cycle from Random back to Random.
      cycle="[";Effect e=Effect::Random;bool first=true;
      do{e=EffectSet::next(e,mask);cycle+=(first?"":",")+std::to_string(unsigned(e));first=false;}while(e!=Effect::Random);
      cycle+="]";
    }
    o+="    {\"mask\":\""+hexMask(mask)+"\",\"ids\":"+ids(mask)+",\"problem\":\""+EffectSet::problemName(problem)+"\",\"effect\":"+std::to_string(at)+
       ",\"record\":"+record+",\"request\":"+request+",\"cycle\":"+cycle+"}"+(i+1<all.size()?",":"")+"\n";
  }
  o+="  ],\n";
  // From a saved effect outside the set: the next offered one after it.
  o+="  \"next\": [";
  bool first=true;
  for(Mask mask:{of({0,18,42}),of({7}),of({2,8,13,23,33,38}),of({0,43,63,64,82})})for(unsigned from:{0u,4u,5u,6u,7u,12u,17u,18u,30u,41u,42u,43u,63u,64u,72u,82u}) {
    std::snprintf(b,sizeof(b),"%s{\"mask\":\"%s\",\"from\":%u,\"next\":%u}",first?"":",",hexMask(mask).c_str(),from,unsigned(EffectSet::next(Effect(from),mask)));o+=b;first=false;
  }
  o+="],\n";
  // Random picks for a few random words and previous picks.
  o+="  \"picks\": [";
  first=true;
  for(Mask mask:{EffectSet::DEFAULT_MASK,of({7}),of({0,18,42}),of({2,8,13,23,33,38}),of({0,43,63,64,82})})for(unsigned previous:{0u,5u,7u,18u,42u,64u,82u})for(uint32_t bits:{0u,1u,2u,5u,0x9e3779b9u,0xffffffffu}) {
    std::snprintf(b,sizeof(b),"%s{\"mask\":\"%s\",\"previous\":%u,\"bits\":%lu,\"effect\":%u}",first?"":",",hexMask(mask).c_str(),previous,(unsigned long)bits,
      unsigned(EffectSet::pick(bits,Effect(previous),mask)));o+=b;first=false;
  }
  o+="]\n}\n";
  return o;
}
}
