#pragma once
#include <Preferences.h>
#include "nvs_budget.h"
#include "practice_history.h"
#include "theme_library.h"

// The custom themes as one checksummed record beside the appearance record.
// One key, not alternating slots: NVS replaces a blob atomically, and a lost
// record only costs the custom looks (the UI falls back to the default). An
// unreadable record is kept rather than overwritten, as the other stores do;
// only an explicit clear (the USB Clear request, Wipe all) or a restore
// replaces it. Writes run inside the sketch's ADC-owned checkpoints.
class ThemeStore {
public:
  static constexpr const char *SPACE="ll-appearance",*KEY="themes";
  enum class Status:uint8_t {Empty,Loaded,Unreadable,Unavailable};
  static const char *name(Status s) {
    switch(s){case Status::Empty:return "empty";case Status::Loaded:return "ok";case Status::Unreadable:return "unreadable";default:return "unavailable";}
  }
  // An upload must leave room for a history rewrite in the 20 KB NVS.
  static constexpr unsigned NEEDED_ENTRIES=NvsBudget::blobEntries(Themes::Library::IMAGE_BYTES)+
    NvsBudget::blobEntries(PracticeHistory::IMAGE_BYTES);
  static bool fits(unsigned availableEntries){return availableEntries>=NEEDED_ENTRIES;}

  Status begin(Themes::Library &library) {
    library=Themes::Library{};
    if(!prefs_.begin(SPACE,false))return status_=Status::Unavailable;
    const size_t n=prefs_.getBytesLength(KEY);
    if(!n && !prefs_.isKey(KEY))return status_=Status::Empty;
    Themes::Library::Image image{};Themes::Library loaded;
    if(n!=image.size() || prefs_.getBytes(KEY,image.data(),image.size())!=image.size() ||
       !loaded.decode(image.data(),image.size()))return status_=Status::Unreadable;
    library=loaded;saved_=library;return status_=Status::Loaded;
  }
  Status status()const{return status_;}
  bool pending(const Themes::Library &library)const{return !(library==saved_);}
  // Writes and reads back the record. An unreadable record is replaced only
  // when `replaceUnreadable` (a restore).
  bool save(const Themes::Library &library,bool replaceUnreadable=false) {
    if(status_==Status::Unavailable || (status_==Status::Unreadable && !replaceUnreadable))return false;
    if(status_!=Status::Unreadable && !pending(library))return true;
    Themes::Library::Image image{},readback{};library.encode(image);
    Themes::Library decoded;
    if(prefs_.putBytes(KEY,image.data(),image.size())!=image.size() ||
       prefs_.getBytes(KEY,readback.data(),readback.size())!=readback.size() || image!=readback ||
       !decoded.decode(readback.data(),readback.size()))return false;
    saved_=library;status_=Status::Loaded;return true;
  }
  // Removes the record, readable or not.
  bool clear() {
    if(status_==Status::Unavailable)return false;
    if(prefs_.isKey(KEY) && (!prefs_.remove(KEY) || prefs_.isKey(KEY)))return false;
    saved_=Themes::Library{};status_=Status::Empty;return true;
  }
private:
  Preferences prefs_;
  Themes::Library saved_;
  Status status_=Status::Unavailable;
};
