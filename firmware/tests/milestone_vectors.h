// Shared test vectors for the web milestone editor (beywatch
// tests/launchlab/fixtures/milestone-vectors.json): the effects, the default
// table, table verdicts, records, USB requests, labels and celebration
// decisions, all computed by the firmware's own code. test_milestones checks
// fixtures/milestone_vectors.json against this; milestone_fixture.cpp prints it.
#pragma once
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>
#include "../LaunchLabMini/device_backup.h"
#include "../LaunchLabMini/milestones.h"

namespace MilestoneVectors {
using Milestones::Row;using Milestones::Mode;using Appearance::Effect;
struct Table {Mode mode;std::vector<Row> rows;};
inline uint64_t pool(std::initializer_list<Effect> effects){uint64_t m=0;for(auto e:effects)m|=uint64_t(1)<<unsigned(e);return m;}
// A ladder from `start` every `step` RPM, cycling through every drawn effect.
inline std::vector<Row> ladder(unsigned start,unsigned step,unsigned count) {
  std::vector<Row> r;for(unsigned i=0;i<count;++i)r.push_back({uint16_t(start+i*step),Appearance::drawnEffect(i%Appearance::DRAWN)});return r;
}
// Valid tables and one failing each rule.
inline std::vector<Table> tables() {
  return {
    {Mode::Milestones,{{9000,Effect::Sparkles},{12000,Effect::Flames},{15000,Effect::Slash}}},
    {Mode::Both,{{1000,Effect::Orbit},{30000,Effect::Lightning}}},
    {Mode::Best,{{12345,Effect::Shockwave}}},
    {Mode::Both,{{8000,Effect::Orbit},{9000,Effect::Flames},{10000,Effect::Sparkles},{11000,Effect::Shockwave},
                 {12000,Effect::Lightning},{13000,Effect::Slash},{14000,Effect::Orbit},{15000,Effect::Slash}}},
    {Mode::Milestones,{}},                                              // no rows
    {Mode::Milestones,{{999,Effect::Orbit}}},                           // under 1,000
    {Mode::Milestones,{{9000,Effect::Orbit},{30001,Effect::Slash}}},     // over 30,000
    {Mode::Milestones,{{9000,Effect::Orbit},{9000,Effect::Slash}}},      // not unique
    {Mode::Milestones,{{10000,Effect::Orbit},{9000,Effect::Slash}}},     // not rising
    {Mode::Milestones,{{9000,Effect::Off}}},                            // not a drawn effect
    {Mode::Milestones,{{9000,Effect::Random}}},
    {Mode::Milestones,{{9000,Effect(8)}}},
    {Mode(3),{{9000,Effect::Orbit}}},                                   // no such mode
    {Mode::Both,{{8000,Effect::Orbit},{9000,Effect::Flames},{10000,Effect::Sparkles},{11000,Effect::Shockwave},
                 {12000,Effect::Lightning},{13000,Effect::Slash},{14000,Effect::Orbit},{15000,Effect::Slash},{16000,Effect::Slash}}}, // nine rows: 0.16 only
    // Effect pools (0.16): three favourites at one threshold, a 500 RPM ladder of twelve rows.
    {Mode::Milestones,{{9000,Effect::Sparkles},{12000,pool({Effect::Tidal,Effect::Frost,Effect::Bloom})},{15000,Effect::Slash}}},
    {Mode::Both,ladder(9000,500,12)},
    {Mode::Both,ladder(9000,500,13)},                                  // thirteen rows
    {Mode::Milestones,{{9000,uint64_t(0)}}},                           // an empty pool
    {Mode::Milestones,{{9000,pool({Effect::Orbit,Effect::Off})}}},     // Off in a pool
    {Mode::Milestones,{{9000,uint64_t(1)<<Appearance::EFFECTS}}},      // an effect past the last
  };
}
inline std::string hex(const uint8_t *p,size_t n){std::string s;char b[3];for(size_t i=0;i<n;++i){std::snprintf(b,3,"%02x",p[i]);s+=b;}return s;}
inline std::string base64(const uint8_t *p,size_t n){std::string s;DeviceBackup::base64Lines(p,n,96,[&](const char *line){s+=line;});return s;}
inline std::string rows(const std::vector<Row> &r) {
  std::string o="[";
  for(size_t i=0;i<r.size();++i) {
    char b[48];std::snprintf(b,sizeof(b),"%s{\"rpm\":%u,\"effects\":[",i?",":"",unsigned(r[i].rpm));o+=b;
    bool first=true;for(unsigned e=0;e<64;++e)if((r[i].pool>>e)&1u){std::snprintf(b,sizeof(b),"%s%u",first?"":",",e);o+=b;first=false;}
    o+="]}";
  }
  return o+"]";
}
inline std::string json() {
  // Schema 2 (0.16): rows hold effect pools; v1 bytes are 0.15's, given for
  // tables 0.15 can hold (at most eight single-effect rows of its effects).
  char b[512];std::string o="{\n  \"schema\": 2,\n";
  std::snprintf(b,sizeof(b),"  \"maxRows\": %u, \"v1MaxRows\": %u, \"minRpm\": %u, \"maxRpm\": %u, \"recordBytes\": %u, \"requestBytes\": %u, \"v1RecordBytes\": %u, \"v1RequestBytes\": %u,\n",
    Milestones::MAX_ROWS,Milestones::V1_ROWS,unsigned(Milestones::MIN_RPM),unsigned(Milestones::MAX_RPM),Milestones::Config::IMAGE_BYTES,Milestones::Request::BYTES,
    Milestones::Config::V1_IMAGE_BYTES,Milestones::Request::V1_BYTES);o+=b;
  o+="  \"modes\": [";for(unsigned m=0;m<Milestones::MODES;++m){std::snprintf(b,sizeof(b),"%s{\"id\":%u,\"name\":\"%s\",\"label\":\"%s\"}",m?",":"",m,Milestones::name(Mode(m)),Milestones::label(Mode(m)));o+=b;}o+="],\n";
  o+="  \"effects\": [";
  for(unsigned e=0;e<Appearance::EFFECTS;++e){std::snprintf(b,sizeof(b),"%s{\"id\":%u,\"label\":\"%s\",\"drawn\":%s,\"durationMs\":%u}",e?",":"",e,
    Appearance::label(Effect(e)),Appearance::drawn(e)?"true":"false",unsigned(Appearance::duration(Effect(e))));o+=b;}
  o+="],\n";
  {Milestones::Config d;std::vector<Row> r;for(unsigned i=0;i<d.count();++i)r.push_back(d.row(i));
   Milestones::Config::Image image;d.encode(image);
   o+="  \"defaults\": {\"mode\": "+std::to_string(unsigned(d.mode()))+", \"rows\": "+rows(r)+", \"record\": \""+hex(image.data(),image.size())+"\"},\n";}
  o+="  \"tables\": [\n";
  const auto all=tables();
  for(size_t i=0;i<all.size();++i) {
    const auto &t=all[i];unsigned row=0;
    const auto problem=Milestones::Config::check(t.mode,t.rows.data(),unsigned(t.rows.size()),&row);
    std::string record="null",request="null",recordV1="null",requestV1="null";
    if(problem==Milestones::Problem::None) {
      Milestones::Config c;c.set(t.mode,t.rows.data(),unsigned(t.rows.size()));c.revision=7;
      Milestones::Config::Image image;c.encode(image);record="\""+hex(image.data(),image.size())+"\"";
      uint8_t q[Milestones::Request::BYTES];Milestones::Request::encode(Milestones::Request::Op::Set,&c,q);request="\""+base64(q,sizeof(q))+"\"";
      if(c.fitsV1()) {
        Milestones::Config::V1Image v1;c.encodeV1(v1);recordV1="\""+hex(v1.data(),v1.size())+"\"";
        uint8_t q1[Milestones::Request::V1_BYTES];Milestones::Request::encode(Milestones::Request::Op::Set,&c,q1,true);requestV1="\""+base64(q1,sizeof(q1))+"\"";
      }
    }
    std::snprintf(b,sizeof(b),"    {\"mode\":%u,\"rows\":",unsigned(t.mode));o+=b;o+=rows(t.rows);
    std::snprintf(b,sizeof(b),",\"problem\":\"%s\",\"row\":%u,\"record\":",Milestones::problemName(problem),row);o+=b;
    o+=record+",\"request\":"+request+",\"recordV1\":"+recordV1+",\"requestV1\":"+requestV1+"}"+(i+1<all.size()?",":"")+"\n";
  }
  o+="  ],\n";
  {uint8_t q[Milestones::Request::BYTES];Milestones::Request::encode(Milestones::Request::Op::Reset,nullptr,q);o+="  \"resetRequest\": \""+base64(q,sizeof(q))+"\",\n";}
  {uint8_t q[Milestones::Request::V1_BYTES];Milestones::Request::encode(Milestones::Request::Op::Reset,nullptr,q,true);o+="  \"resetRequestV1\": \""+base64(q,sizeof(q))+"\",\n";}
  o+="  \"labels\": [";
  bool first=true;for(uint16_t rpm:{1000,9000,12500,15000,12345,30000,10050}){char v[12];Milestones::rpmText(v,sizeof(v),rpm);
    std::snprintf(b,sizeof(b),"%s{\"rpm\":%u,\"text\":\"%s\"}",first?"":",",unsigned(rpm),v);o+=b;first=false;}
  o+="],\n";
  // Decisions on the default table, the Best effect set to Orbit.
  o+="  \"decisions\": [\n";
  first=true;
  for(unsigned m=0;m<Milestones::MODES;++m)for(float rpm:{8500.0f,8999.4f,8999.5f,9000.0f,12300.0f,14999.5f,15500.0f,29000.0f})for(bool beat:{false,true})for(bool valid:{true,false}) {
    Milestones::Config c;c.select(Mode(m));
    const auto d=Milestones::decide(c,valid,rpm,beat && valid,Effect::Orbit);
    std::snprintf(b,sizeof(b),"%s    {\"mode\":%u,\"rpm\":%.1f,\"best\":%s,\"valid\":%s,\"show\":%s,\"milestone\":%u,\"effect\":%u}",first?"":",\n",
      m,double(rpm),beat && valid?"true":"false",valid?"true":"false",d.show?"true":"false",unsigned(d.milestone),unsigned(d.effect));o+=b;first=false;
  }
  o+="\n  ]\n}\n";
  return o;
}
}
