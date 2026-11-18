#pragma once
#include <Preferences.h>
#include <memory>
#include "launch_motion.h"

class MotionStore {
public:
  bool begin(LaunchMotion::Trace &reference,const char *space="ll-motion") {
    ready_=prefs_.begin(space,false);if(!ready_)return false;
    auto candidate=std::unique_ptr<LaunchMotion::Trace>(new(std::nothrow) LaunchMotion::Trace);
    if(!candidate)return ready_=false;
    bool present=false,found=false;
    for(unsigned i=0;i<2;++i) {
      const char *key=i?"ref1":"ref0";const size_t n=prefs_.getBytesLength(key);present|=n!=0 || prefs_.isKey(key);
      uint32_t gen=0;
      if(n!=image_.size() || prefs_.getBytes(key,image_.data(),image_.size())!=image_.size() ||
         !LaunchMotion::decode(image_,*candidate,gen))continue;
      if(!found || int32_t(gen-generation_)>0){reference=*candidate;generation_=gen;slot_=i;found=true;}
    }
    if(present && !found)return ready_=false;
    return true;
  }
  bool save(const LaunchMotion::Trace &reference) {
    if(!ready_ || !reference.valid())return false;
    const uint32_t nextGeneration=generation_+1;const unsigned next=slot_^1u;
    const char *key=next?"ref1":"ref0";
    LaunchMotion::encode(reference,nextGeneration,image_);
    const uint32_t hash=LaunchMotion::checksum(image_.data(),image_.size());
    if(prefs_.putBytes(key,image_.data(),image_.size())!=image_.size())return false;
    unverified_=true;
    auto restored=std::unique_ptr<LaunchMotion::Trace>(new(std::nothrow) LaunchMotion::Trace);
    uint32_t gen=0;
    if(!restored || prefs_.getBytes(key,image_.data(),image_.size())!=image_.size() ||
       LaunchMotion::checksum(image_.data(),image_.size())!=hash || !LaunchMotion::decode(image_,*restored,gen) || gen!=nextGeneration)return false;
    generation_=gen;slot_=next;unverified_=false;return true;
  }
  // Removes both slots (a restore putting back "no reference"); nothing else calls it.
  bool clear() {
    if(!ready_)return false;
    for(const char *key:{"ref0","ref1"})if(prefs_.isKey(key) && (!prefs_.remove(key) || prefs_.isKey(key)))return false;
    generation_=0;slot_=0;unverified_=false;return true;
  }
  // A write NVS took did not read back: the slot it went to may hold a
  // reference newer than generation(), which the next boot would load. The
  // next save goes to the same slot and settles it; so does clear().
  bool unverified()const{return unverified_;}
  bool ready()const{return ready_;}
  // The generation of the newest record (0 with none); each save advances it.
  uint32_t generation()const{return generation_;}
private:
  Preferences prefs_;LaunchMotion::Image image_{};uint32_t generation_=0;unsigned slot_=0;bool ready_=false,unverified_=false;
};
