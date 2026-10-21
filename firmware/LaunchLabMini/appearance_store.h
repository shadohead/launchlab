#pragma once
#include <Preferences.h>
#include "appearance.h"

// Small alternating snapshots, verified by readback. All writes are made by
// the sketch inside its existing ADC-owned idle/shutdown checkpoints.
// An unreadable record (keys present, neither slot valid) is kept and the
// store refuses to save, so a newer firmware's record survives a visit here.
// With `replaceUnreadable` the caller's defaults stand in for it instead and
// the next change saves over one slot: begin() still returns false, and
// damaged() says so until that save.
template<class Value> class AppearanceStore {
public:
  bool begin(Value &value,const char *space,const char *first,const char *second,bool replaceUnreadable=false){
    keys_[0]=first;keys_[1]=second;ready_=prefs_.begin(space,false);
    if(!ready_)return false;
    bool found=false,present=false;typename Value::Image image{};
    for(unsigned i=0;i<2;++i){
      const size_t n=prefs_.getBytesLength(keys_[i]);present|=n!=0 || prefs_.isKey(keys_[i]);
      Value candidate;
      if(n!=image.size() || prefs_.getBytes(keys_[i],image.data(),image.size())!=image.size() || !candidate.decode(image.data(),image.size()))continue;
      if(!found || int32_t(candidate.generation()-value.generation())>0){value=candidate;slot_=i;found=true;}
    }
    present_=present;
    if(present && !found){damaged_=true;ready_=replaceUnreadable;saved_=value.generation();return false;}
    saved_=value.generation();return true;
  }
  bool save(const Value &value){
    if(!ready_)return false;if(!pending(value))return true;
    typename Value::Image image{},readback{};value.encode(image);
    const unsigned next=slot_^1u;
    if(prefs_.putBytes(keys_[next],image.data(),image.size())!=image.size() ||
       prefs_.getBytes(keys_[next],readback.data(),readback.size())!=readback.size() || image!=readback)return false;
    Value decoded;if(!decoded.decode(readback.data(),readback.size()))return false;
    slot_=next;saved_=value.generation();damaged_=false;return true;
  }
  bool ready()const{return ready_;}
  bool damaged()const{return damaged_;}
  // Whether either key existed at begin(): false on a device that never saved this record.
  bool present()const{return present_;}
  bool pending(const Value &v)const{return v.generation()!=saved_;}
private:
  Preferences prefs_;const char *keys_[2]={};uint32_t saved_=0;unsigned slot_=0;bool ready_=false,damaged_=false,present_=false;
};
