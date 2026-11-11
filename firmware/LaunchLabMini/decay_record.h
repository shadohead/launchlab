#pragma once
#include "decay_models.h"
#include <cstring>
namespace Decay {
// Explicit little-endian versioned representation, never a dump of padded C++ structs.
struct Record {
  static constexpr size_t BYTES=512;
  using Image=std::array<uint8_t,BYTES>;
  static uint32_t checksum(const uint8_t *p,size_t n){uint32_t crc=~0u;for(size_t i=0;i<n;++i){crc^=p[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}return ~crc;}
  static void encode(const Results &r,unsigned selected,Image &image){
    image.fill(0);size_t at=0;
    auto u=[&](uint32_t v){for(unsigned i=0;i<4;++i)image[at++]=uint8_t(v>>(8*i));};
    auto f=[&](float v){uint32_t bits;std::memcpy(&bits,&v,4);u(bits);};
    u(0x52444c4c);u(Results::VERSION);u(selected);
    for(float v:{r.acousticStart,r.initialRpm,r.launcherRpm,r.launcherTime,r.lastTime,r.actualStop})f(v);
    u(unsigned(r.actualStopValid) | (r.invalidStop<<1) | (r.validationPossible<<2) | (r.opticalAnchor<<3) | (r.manualAnchor<<4));u(r.sourceCount);u(r.sampleCount);u(r.spikes);u(uint32_t(r.best+1));
    for(auto rank:r.ranking)u(rank);
    for(const auto &m:r.models){
      u(unsigned(m.valid) | (m.converged<<1) | (m.validationConverged<<2) | (m.boundary<<3) | (m.unstable<<4) | (m.stopValid<<5) | (m.stopErrorValid<<6));
      for(float v:{m.params.a,m.params.b,m.params.c,m.params.k,m.params.n})f(v);
      for(const Metric *v:{&m.training,&m.validation,&m.all}){u(v->valid);f(v->rmse);f(v->mae);f(v->maxError);}
      for(float v:{m.stopSeconds,m.stopError,m.absoluteStopError,m.loss})f(v);
      u(m.evaluations);u(m.sampleCount);
    }
    f(r.tailRpm);f(r.tailSlope); // Previously reserved bytes are zero in older records.
    at=BYTES-4;u(checksum(image.data(),BYTES-4));
  }
  static bool decode(const Image &image,Results &r,unsigned &selected){
    size_t at=BYTES-4;auto u=[&](){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=uint32_t(image[at++])<<(8*i);return v;};
    if(u()!=checksum(image.data(),BYTES-4))return false;at=0;
    if(u()!=0x52444c4c || u()!=Results::VERSION)return false;
    selected=u();if(selected>=MODEL_COUNT)return false;r={};
    bool finite=true;auto f=[&](){uint32_t bits=u();float v;std::memcpy(&v,&bits,4);finite&=std::isfinite(v);return v;};
    r.acousticStart=f();r.initialRpm=f();r.launcherRpm=f();r.launcherTime=f();r.lastTime=f();r.actualStop=f();
    uint32_t flags=u();if(flags>31)return false;r.opticalAnchor=flags&8;r.manualAnchor=flags&16;r.actualStopValid=flags&1;r.invalidStop=flags&2;r.validationPossible=flags&4;
    r.sourceCount=u();r.sampleCount=u();r.spikes=u();const uint32_t best=u();if(best>MODEL_COUNT)return false;r.best=int(best)-1;
    if(r.sourceCount>MAX_SAMPLES || r.sampleCount>r.sourceCount || r.best< -1 || r.best>=int(MODEL_COUNT))return false;
    unsigned ranks=0;for(auto &rank:r.ranking){uint32_t value=u();if(value>=MODEL_COUNT || (ranks&(1u<<value)))return false;ranks|=1u<<value;rank=uint8_t(value);}
    for(unsigned i=0;i<MODEL_COUNT;++i){auto &m=r.models[i];m.model=Model(i);flags=u();if(flags>127)return false;
      m.valid=flags&1;m.converged=flags&2;m.validationConverged=flags&4;m.boundary=flags&8;m.unstable=flags&16;m.stopValid=flags&32;m.stopErrorValid=flags&64;
      m.params.a=f();m.params.b=f();m.params.c=f();m.params.k=f();m.params.n=f();
      for(Metric *v:{&m.training,&m.validation,&m.all}){uint32_t valid=u();if(valid>1)return false;v->valid=valid;v->rmse=f();v->mae=f();v->maxError=f();if(v->rmse<0 || v->mae<0 || v->maxError<0)return false;}
      m.stopSeconds=f();m.stopError=f();m.absoluteStopError=f();m.loss=f();m.evaluations=u();m.sampleCount=u();
      if((m.valid && !validParams(m.model,m.params)) || m.sampleCount>MAX_SAMPLES || (m.stopValid && m.stopSeconds<r.acousticStart))return false;
    }
    r.tailRpm=f();r.tailSlope=f();
    if(r.tailRpm<0 || r.tailSlope>0 || (r.tailRpm>0 && (!r.manualAnchor || r.actualStop<=r.lastTime)))return false;
    return finite && r.acousticStart>=r.launcherTime && r.initialRpm>=0 && r.lastTime>=r.acousticStart;
  }
};
}
