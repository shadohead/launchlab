#pragma once
#include <Preferences.h>
#include "appearance_store.h"
#include "milestones.h"
#include "nvs_budget.h"
#include "practice_history.h"

// The milestone table in 0.16's and 0.17's own keys (mpool0, mpool1):
// alternating slots, each write read back. A table whose pools stay within
// effects 0-63 is their v2 record, byte for byte; a wider one is the packed
// v3 record, which takes the same 8 NVS entries. 0.16 and 0.17 skip a slot
// they cannot read (another length, or effects they lack): they show the
// other slot's table, else their defaults, and their next save writes over
// the slot they skipped. 0.15's record (mile0, mile1) stays as they leave it.
// So 0.18 never holds more NVS than 0.17 does, and a unit rolled back to
// 0.15, 0.16 or 0.17 with a full history still saves its pulls and tables: a
// full history leaves about 100 free entries, and those builds need 92
// (0.15) or 95 for a table save.
// A table 0.16 cannot read also puts forV016() in the other slot, one
// revision older: a rolled-back unit shows it, and the slot its next save
// writes over is the newer table's, so a later 0.18 loads the table the
// older build saved rather than the one from before the rollback.
// On a first boot of 0.16 or later with no record here, 0.15's is carried over.
class MilestoneStore {
public:
  enum class Source:uint8_t {None,Narrow,Wide,Legacy};
  static constexpr unsigned SLOT_ENTRIES=NvsBudget::blobEntries(Milestones::Config::V2_IMAGE_BYTES);
  static_assert(NvsBudget::blobEntries(Milestones::Config::PACKED_IMAGE_BYTES)==SLOT_ENTRIES,
    "a packed slot takes the NVS entries of 0.17's slot");
  // A save needs room for a history rewrite after it, and for one slot while
  // it writes: 0.16's and 0.17's figure. One that fills empty slots also
  // keeps the reserve (NvsBudget::optionalNeeded).
  unsigned neededEntries(const Milestones::Config &c) {
    const unsigned history=NvsBudget::blobEntries(PracticeHistory::IMAGE_BYTES),next=slot_^1u;
    if(!ready_)return history+SLOT_ENTRIES;
    unsigned grows=held(next)<SLOT_ENTRIES?SLOT_ENTRIES-held(next):0;
    if(!c.fitsV016() && held(slot_)<SLOT_ENTRIES)grows+=SLOT_ENTRIES-held(slot_);
    return NvsBudget::optionalNeeded(SLOT_ENTRIES,grows,history);
  }
  // False when no record could be read (the defaults stand in); damaged()
  // says whether one was present and unreadable, and the next save replaces it.
  bool begin(Milestones::Config &c,const char *space="ll-appearance") {
    using namespace Milestones;
    source_=Source::None;damaged_=false;slot_=0;
    ready_=prefs_.begin(space,false);
    if(!ready_)return false;
    bool present=false;
    for(unsigned i=0;i<2;++i) {
      const size_t n=prefs_.getBytesLength(key(i));present|=n!=0 || prefs_.isKey(key(i));
      Config::PackedImage image{};Config candidate;
      if((n!=Config::V2_IMAGE_BYTES && n!=Config::PACKED_IMAGE_BYTES) || prefs_.getBytes(key(i),image.data(),n)!=n ||
         !(n==Config::V2_IMAGE_BYTES?candidate.decode(image.data(),n):candidate.decodePacked(image.data(),n)))continue;
      if(source_==Source::None || int32_t(candidate.generation()-c.generation())>0) {
        c=candidate;slot_=i;source_=candidate.fitsV2()?Source::Narrow:Source::Wide;
      }
    }
    if(source_==Source::None) {
      if(present)damaged_=true;
      else {
        V1Record legacy;AppearanceStore<V1Record> reader;
        if(reader.begin(legacy,space,V1_KEY0,V1_KEY1,true) && reader.present()){c=legacy.table;source_=Source::Legacy;}
        else damaged_=reader.damaged();
      }
    }
    saved_=c.generation();
    return source_!=Source::None;
  }
  bool ready()const{return ready_;}
  bool damaged()const{return damaged_;}
  // Where begin() found the table.
  Source source()const{return source_;}
  bool pending(const Milestones::Config &c)const{return c.generation()!=saved_;}
  // Writes the table's record over the older slot, then (a table 0.16
  // cannot read) forV016() over the other. Run inside the caller's
  // checkpoint; each write is read back and decoded. The table is saved once
  // its own record is: should the second write fail, this firmware still
  // loads the table, and only a rolled-back unit sees the older slot.
  bool save(const Milestones::Config &c) {
    if(!ready_)return false;
    if(!pending(c) && !damaged_)return true;
    const unsigned next=slot_^1u;
    if(!write(next,c))return false;
    slot_=next;saved_=c.generation();damaged_=false;source_=c.fitsV2()?Source::Narrow:Source::Wide;
    if(!c.fitsV016())write(next^1u,c.forV016());
    return true;
  }
private:
  static const char *key(unsigned slot){return slot?Milestones::KEY1:Milestones::KEY0;}
  // NVS entries a slot holds now.
  unsigned held(unsigned slot) {
    const size_t n=prefs_.getBytesLength(key(slot));
    return n?NvsBudget::blobEntries(unsigned(n)):0;
  }
  bool write(unsigned slot,const Milestones::Config &c) {
    using namespace Milestones;
    Config::PackedImage image{},readback{};unsigned n=Config::V2_IMAGE_BYTES;
    if(c.fitsV2()){Config::V2Image v2;c.encodeV2(v2);std::copy(v2.begin(),v2.end(),image.begin());}
    else if(c.encodePacked(image))n=Config::PACKED_IMAGE_BYTES;
    else return false;
    if(prefs_.putBytes(key(slot),image.data(),n)!=n || prefs_.getBytes(key(slot),readback.data(),n)!=n ||
       !std::equal(image.begin(),image.begin()+n,readback.begin()))return false;
    Config decoded;
    return n==Config::V2_IMAGE_BYTES?decoded.decode(readback.data(),n):decoded.decodePacked(readback.data(),n);
  }
  Preferences prefs_;uint32_t saved_=0;unsigned slot_=0;Source source_=Source::None;bool ready_=false,damaged_=false;
};
