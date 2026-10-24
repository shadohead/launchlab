#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace PowerDiagnostics {
constexpr uint32_t MAGIC=0x50575231;
constexpr unsigned CAPACITY=48;
enum Event:uint8_t {Boot=1,Sample,Sleep,Wake};
enum Flag:uint8_t {Charging=1,ChargeKnown=2,Led=4,Boost=8,Tournament=16,SensorRail=32};
struct Record {
  uint64_t elapsedSeconds;
  uint32_t boot;
  uint16_t batteryMv;
  uint8_t event,flags,brightness,cpuMhz,wake,reserved;
};
struct Image {
  uint32_t magic,version,boots,powerButtonWakes,shakeWakes,otherWakes,sleeps,count,next,pendingSleep,unknownSleepIntervals;
  uint64_t awakeMs,sleepMs,chargingMs,ledMs,boostMs,displayMs;
  Record records[CAPACITY];
  uint32_t checksum;
};
inline uint32_t hash(const Image &v) {
  const auto *p=reinterpret_cast<const uint8_t *>(&v);uint32_t h=2166136261u;
  for(size_t i=0;i<offsetof(Image,checksum);++i)h=(h^p[i])*16777619u;
  return h;
}
inline void fresh(Image &v){std::memset(&v,0,sizeof(v));v.magic=MAGIC;v.version=1;v.checksum=hash(v);}
inline bool valid(const Image &v){return v.magic==MAGIC && v.version==1 && v.count<=CAPACITY && v.next<CAPACITY && v.pendingSleep<=1 && v.checksum==hash(v);}
inline void append(Image &v,uint8_t event,uint16_t mv,uint8_t flags,uint8_t brightness,uint8_t cpu,uint8_t wake=0) {
  auto &r=v.records[v.next];std::memset(&r,0,sizeof(r));
  r.elapsedSeconds=(v.awakeMs+v.sleepMs)/1000;r.boot=v.boots;r.event=event;r.batteryMv=mv;
  r.flags=flags;r.brightness=brightness;r.cpuMhz=cpu;r.wake=wake;
  v.next=(v.next+1)%CAPACITY;if(v.count<CAPACITY)++v.count;v.checksum=hash(v);
}
inline void account(Image &v,uint32_t dt,uint8_t flags,uint8_t brightness) {
  v.awakeMs+=dt;if(flags&Charging)v.chargingMs+=dt;if(flags&Led)v.ledMs+=dt;
  if(flags&Boost)v.boostMs+=dt;if(brightness)v.displayMs+=dt;
}
}
