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
    const unsigned next=slot_^1u;
    if(!write(next,value))return false;
    slot_=next;saved_=value.generation();damaged_=false;return true;
  }
  // A record that firmware without loaded effects cannot read (it names a
  // loaded slot) goes with the view that firmware can read, one generation
  // below it, in the other slot: the view first, then the record over the
  // slot that held the newest. Older firmware loads the view, and its own
  // next save lands on the record's slot, so a change made there wins when
  // the unit is upgraded again. A power cut after the view leaves the view
  // as the newest record: the change without its loaded effects.
  bool saveWithOlderView(const Value &value,const Value &older){
    if(!ready_)return false;if(!pending(value))return true;
    if(older.generation()!=value.generation()-1)return false;
    const unsigned view=slot_^1u,record=view^1u;
    if(!write(view,older))return false;
    slot_=view;saved_=older.generation();damaged_=false;
    if(!write(record,value))return false;
    slot_=record;saved_=value.generation();return true;
  }
  // save() for a record that may name a loaded effect (Value has
  // needsOlderView() and olderView()): such a record goes with the view
  // older firmware reads, through saveWithOlderView(); the extra revision
  // keeps that view newer than the record it replaces.
  bool saveViewed(Value &value){
    if(!value.needsOlderView())return save(value);
    if(!pending(value))return true;
    ++value.revision;return saveWithOlderView(value,value.olderView());
  }
  bool ready()const{return ready_;}
  // Whether both slots hold a record, so the next save replaces one in place.
  bool bothSlots(){return ready_ && prefs_.isKey(keys_[0]) && prefs_.isKey(keys_[1]);}
  bool damaged()const{return damaged_;}
  // Whether either key existed at begin(): false on a device that never saved this record.
  bool present()const{return present_;}
  bool pending(const Value &v)const{return v.generation()!=saved_;}
  // The generation the store holds now (saved or loaded).
  uint32_t savedGeneration()const{return saved_;}
private:
  bool write(unsigned slot,const Value &value){
    typename Value::Image image{},readback{};value.encode(image);
    if(prefs_.putBytes(keys_[slot],image.data(),image.size())!=image.size() ||
       prefs_.getBytes(keys_[slot],readback.data(),readback.size())!=readback.size() || image!=readback)return false;
    Value decoded;return decoded.decode(readback.data(),readback.size());
  }
  Preferences prefs_;const char *keys_[2]={};uint32_t saved_=0;unsigned slot_=0;bool ready_=false,damaged_=false,present_=false;
};
