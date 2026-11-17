#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

// UI-owned practice data. Session means cover ALL their accepted launches,
// even when the bounded individual-record ring has overwritten older pulls.
class PracticeHistory {
public:
  static constexpr unsigned LAUNCH_CAPACITY=128,SESSION_CAPACITY=24;
  static constexpr uint32_t SESSION_IDLE_MS=10*60*1000;
  static constexpr unsigned IMAGE_BYTES=40+LAUNCH_CAPACITY*16+SESSION_CAPACITY*24+4;
  using Image=std::array<uint8_t,IMAGE_BYTES>;
  struct Launch {uint32_t number=0,session=0;float rpm=0;uint32_t seconds=0;};
  struct Session {
    uint32_t number=0,count=0;uint64_t milliSum=0;float best=0;uint32_t seconds=0;
    float mean()const{return count?float(double(milliSum)/(1000.0*count)):0;}
  };
  static bool validRpm(float rpm){return std::isfinite(rpm) && rpm>=1000 && rpm<=1500000;}
  bool accept(uint32_t deviceCount,bool valid,float rpm,uint32_t now) {
    if(!valid || !validRpm(rpm) || deviceCount<=seenDeviceCount_)return false;
    seenDeviceCount_=deviceCount;
    if(forceSession_ || !sessionCount_ || uint32_t(now-lastLaunchMs_)>=SESSION_IDLE_MS) {
      sessions_[sessionNext_]={++lastSession_,0,0,0,0};
      sessionNext_=(sessionNext_+1)%SESSION_CAPACITY;
      sessionCount_=std::min(sessionCount_+1,SESSION_CAPACITY);
      forceSession_=false;sessionStartMs_=now;
    }
    Session &session=sessions_[(sessionNext_+SESSION_CAPACITY-1)%SESSION_CAPACITY];
    ++session.count;session.milliSum+=uint64_t(std::llround(double(rpm)*1000));
    session.best=std::max(session.best,rpm);session.seconds=uint32_t(now-sessionStartMs_)/1000;
    launches_[launchNext_]={++lastLaunch_,session.number,rpm,session.seconds};
    launchNext_=(launchNext_+1)%LAUNCH_CAPACITY;
    launchCount_=std::min(launchCount_+1,LAUNCH_CAPACITY);
    lastLaunchMs_=now;++generation_;return true;
  }
  void newSession(){forceSession_=true;}
  bool sessionPending()const{return forceSession_;}
  unsigned size()const{return launchCount_;}
  unsigned sessions()const{return sessionCount_;}
  uint32_t generation()const{return generation_;}
  // A restored backup must outrank both saved slots, whatever its own generation was.
  void supersede(uint32_t current){generation_=current+1;}
  const Launch *recent(unsigned index=0)const {
    return index<launchCount_?&launches_[(launchNext_+LAUNCH_CAPACITY-1-index)%LAUNCH_CAPACITY]:nullptr;
  }
  const Session *session(unsigned index=0)const {
    return index<sessionCount_?&sessions_[(sessionNext_+SESSION_CAPACITY-1-index)%SESSION_CAPACITY]:nullptr;
  }
  float recentMean(unsigned offset,unsigned count)const {
    double sum=0;unsigned n=0;
    for(unsigned i=offset;i<launchCount_ && n<count;++i,++n)sum+=recent(i)->rpm;
    return n?float(sum/n):0;
  }
  float recentSessionMean(unsigned offset,unsigned count)const {
    double sum=0;unsigned n=0;
    for(unsigned i=offset;i<sessionCount_ && n<count;++i,++n)sum+=session(i)->mean();
    return n?float(sum/n):0;
  }
  // Explicit little-endian encoding; no structure padding or platform ABI on flash.
  void encode(Image &out)const {
    unsigned p=0;
    auto put32=[&](uint32_t v){for(unsigned b=0;b<4;++b)out[p++]=uint8_t(v>>(8*b));};
    auto real=[&](float v){uint32_t bits;std::memcpy(&bits,&v,4);put32(bits);};
    const uint32_t header[]={0x4c505231u,1u,uint32_t(IMAGE_BYTES),generation_,lastLaunch_,lastSession_,
                            uint32_t(launchCount_),uint32_t(launchNext_),uint32_t(sessionCount_),uint32_t(sessionNext_)};
    for(uint32_t v:header)put32(v);
    for(const auto &r:launches_){put32(r.number);put32(r.session);real(r.rpm);put32(r.seconds);}
    for(const auto &s:sessions_){put32(s.number);put32(s.count);put32(uint32_t(s.milliSum));put32(uint32_t(s.milliSum>>32));real(s.best);put32(s.seconds);}
    put32(checksum(out.data(),IMAGE_BYTES-4));
  }
  bool decode(const uint8_t *data,unsigned length) {
    if(length!=IMAGE_BYTES)return false;
    unsigned p=0;
    auto get32=[&](){uint32_t v=0;for(unsigned b=0;b<4;++b)v|=uint32_t(data[p++])<<(8*b);return v;};
    auto real=[&](){uint32_t v=get32();float f;std::memcpy(&f,&v,4);return f;};
    if(get32()!=0x4c505231u || get32()!=1 || get32()!=IMAGE_BYTES)return false;
    auto candidate=std::unique_ptr<PracticeHistory>(new(std::nothrow) PracticeHistory);
    if(!candidate)return false;
    candidate->generation_=get32();candidate->lastLaunch_=get32();candidate->lastSession_=get32();
    candidate->launchCount_=get32();candidate->launchNext_=get32();candidate->sessionCount_=get32();candidate->sessionNext_=get32();
    if(candidate->launchCount_>LAUNCH_CAPACITY || candidate->launchNext_>=LAUNCH_CAPACITY ||
       candidate->sessionCount_>SESSION_CAPACITY || candidate->sessionNext_>=SESSION_CAPACITY)return false;
    for(auto &r:candidate->launches_){r.number=get32();r.session=get32();r.rpm=real();r.seconds=get32();}
    for(auto &s:candidate->sessions_){s.number=get32();s.count=get32();s.milliSum=get32();s.milliSum|=uint64_t(get32())<<32;s.best=real();s.seconds=get32();}
    if(get32()!=checksum(data,IMAGE_BYTES-4))return false;
    if((candidate->launchCount_==0)!=(candidate->sessionCount_==0))return false;
    for(unsigned i=0;i<candidate->launchCount_;++i) {
      const auto &r=*candidate->recent(i);
      if(!validRpm(r.rpm) || r.number!=candidate->lastLaunch_-i || !r.session || r.session>candidate->lastSession_)return false;
    }
    for(unsigned i=0;i<candidate->sessionCount_;++i) {
      const auto &s=*candidate->session(i);
      if(s.number!=candidate->lastSession_-i || !s.count || !validRpm(s.best) ||
         !validRpm(s.mean()) || s.mean()>s.best+0.01f)return false;
    }
    // Uptime cannot span a power cycle: next accepted pull begins a new session.
    *this=*candidate;return true;
  }
  static uint32_t checksum(const uint8_t *data,unsigned n) {
    uint32_t value=2166136261u;for(unsigned i=0;i<n;++i)value=(value^data[i])*16777619u;return value;
  }
private:
  std::array<Launch,LAUNCH_CAPACITY> launches_{};
  std::array<Session,SESSION_CAPACITY> sessions_{};
  uint32_t generation_=0,lastLaunch_=0,lastSession_=0,seenDeviceCount_=0,lastLaunchMs_=0,sessionStartMs_=0;
  unsigned launchCount_=0,launchNext_=0,sessionCount_=0,sessionNext_=0;
  bool forceSession_=true;
};
