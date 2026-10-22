#include "../LaunchLabMini/device_wipe_nvs.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using DeviceWipe::State;
using DeviceWipe::Result;
struct PowerCut {};

// Durable NVS model, not a replacement wipe algorithm. Transactions belong to
// handles; reboot discards uncommitted writes and handles. The real adapter is
// exercised at every API boundary, including fresh readbacks and iterators.
struct Nvs {
  using Handle=unsigned;using Iterator=unsigned;
  static constexpr int Ok=0,NotFound=1,TypeMismatch=2,Error=3;
  struct Value {std::vector<uint8_t> bytes;bool blob=true;bool operator==(const Value &v)const{return bytes==v.bytes && blob==v.blob;}};
  using Space=std::map<std::string,Value>;
  using Data=std::map<std::string,Space>;
  struct Open {std::string name;bool write=false,pending=false;Space staged;};
  enum class Fault { None, ErrorBefore, PowerBefore, PowerAfter, CommitErrorAfter };
  struct Call {std::string op,name;bool mayFail=true;};
  static inline Data data,original;
  static inline std::map<Handle,Open> handles;
  static inline std::map<Iterator,bool> iterators;
  static inline std::vector<Call> calls;
  static inline Handle nextHandle=1;static inline Iterator nextIterator=1;
  static inline Fault fault=Fault::None;static inline size_t trip=0;
  static inline unsigned erases=0,sets=0,removals=0;
  static inline bool corruptMarker=false,retainEntry=false,retainMarker=false,partialEraseError=false,shortRead=false;
  static inline std::string partialEraseName;
  static const std::vector<std::string> &appNames() {
    // Independent exact whitelist oracle, including power logs and actual QA
    // names. A misspelled or framework namespace is deliberately not owned.
    static const std::vector<std::string> names={"launchlab-mini","ll-practice","ll-appearance","ll-bests",
      "ll-motion","ll-power","ll-prac-qa","ll-mot-qa"};return names;
  }
  static Value marker(){return {{DeviceWipe::INTENT_BYTES,DeviceWipe::INTENT_BYTES+sizeof DeviceWipe::INTENT_BYTES},true};}
  static bool pending() {
    const auto s=data.find("ll-reset");if(s==data.end())return false;
    const auto v=s->second.find("pending");return v!=s->second.end() && v->second==marker();
  }
  static bool emptyApps(){for(const auto &name:appNames())if(data.count(name) && !data.at(name).empty())return false;return true;}
  static bool originalApps(){for(const auto &name:appNames())if(!data.count(name) || data.at(name)!=original.at(name))return false;return true;}
  static void unrelatedIntact() {
    for(const auto &item:original)if(!DeviceWipe::ownedNamespace(item.first.c_str()) && item.first!="ll-reset")
      assert(data.count(item.first) && data.at(item.first)==item.second);
  }
  static void seed(bool withMarker=false) {
    data.clear();for(const auto &name:appNames())data[name]={{"old0",{{1,2,3},true}},{"old1",{{4,5},false}}};
    for(const char *name:{"M5Unified","phy","nvs.net80211","other-app","ll-moti-qa"})data[name]={{"keep",{{8,9,10},true}}};
    if(withMarker)data["ll-reset"]["pending"]=marker();original=data;reboot();
  }
  static void reboot() {
    handles.clear();iterators.clear();calls.clear();nextHandle=nextIterator=1;
    fault=Fault::None;trip=0;erases=sets=removals=0;
    corruptMarker=retainEntry=retainMarker=partialEraseError=shortRead=false;
    partialEraseName.clear();
  }
  static bool enter(const char *op,const std::string &name,bool mayFail=true) {
    calls.push_back({op,name,mayFail});
    if(calls.size()==trip && fault==Fault::PowerBefore)throw PowerCut{};
    return !(calls.size()==trip && fault==Fault::ErrorBefore && mayFail);
  }
  static int leave(int result) {
    if(calls.size()==trip && fault==Fault::PowerAfter)throw PowerCut{};
    return result;
  }
  static int open(const char *name,bool write,Handle *out) {
    if(!enter(write?"open-write":"open-read",name))return Error;
    if(!data.count(name)){if(!write)return leave(NotFound);data[name]={};}
    *out=nextHandle++;handles[*out]={name,write,false,{}};return leave(Ok);
  }
  static int getBlob(Handle h,const char *key,void *out,size_t *length) {
    const auto name=handles.at(h).name;
    if(!enter(out?"read-bytes":"read-length",name))return Error;
    const auto &space=data.at(name);const auto i=space.find(key);if(i==space.end())return leave(NotFound);
    if(!i->second.blob)return leave(TypeMismatch);
    const auto size=i->second.bytes.size();
    if(out && *length<size)return leave(Error);
    if(out)std::memcpy(out,i->second.bytes.data(),size);
    *length=size;if(out && shortRead){*length=size/2;shortRead=false;}
    return leave(Ok);
  }
  static int setBlob(Handle h,const char *key,const void *bytes,size_t length) {
    auto &s=handles.at(h);assert(s.write && s.name=="ll-reset" && std::string(key)=="pending");
    if(!enter("set-intent",s.name))return Error;
    s.staged=data.at(s.name);const auto *p=static_cast<const uint8_t *>(bytes);
    s.staged[key]={{p,p+length},true};s.pending=true;++sets;return leave(Ok);
  }
  static int eraseAll(Handle h) {
    auto &s=handles.at(h);assert(s.write && DeviceWipe::ownedNamespace(s.name.c_str()));
    assert(pending()); // Durable verified intent must precede even a partial erase.
    if(!enter("erase-namespace",s.name))return Error;
    ++erases;
    if(partialEraseError && (partialEraseName.empty() || partialEraseName==s.name)) {
      data.at(s.name).erase(data.at(s.name).begin());partialEraseError=false;return leave(Error);
    }
    s.staged={};s.pending=true;return leave(Ok);
  }
  static int eraseKey(Handle h,const char *key) {
    auto &s=handles.at(h);assert(s.write && s.name=="ll-reset" && std::string(key)=="pending");
    assert(emptyApps()); // Removing the journal before any survivor is unsafe.
    if(!enter("erase-intent",s.name))return Error;
    if(!data.at(s.name).count(key))return leave(NotFound);
    s.staged=data.at(s.name);s.staged.erase(key);s.pending=true;++removals;return leave(Ok);
  }
  static int commit(Handle h) {
    auto &s=handles.at(h);assert(s.write);
    if(!enter("commit",s.name))return Error;
    if(s.pending) {
      data[s.name]=s.staged;s.pending=false;
      if(corruptMarker && s.name=="ll-reset" && data[s.name].count("pending")) {
        data[s.name]["pending"].bytes.back()^=1;corruptMarker=false;
      }
      if(retainEntry && DeviceWipe::ownedNamespace(s.name.c_str())){data[s.name]["survivor"]={{55},true};retainEntry=false;}
      if(retainMarker && s.name=="ll-reset" && !data[s.name].count("pending")){data[s.name]["pending"]=marker();retainMarker=false;}
    }
    if(calls.size()==trip && fault==Fault::CommitErrorAfter)return leave(Error);
    return leave(Ok);
  }
  static void close(Handle h) {
    const auto name=handles.at(h).name;enter("close",name,false);handles.erase(h);leave(Ok);
  }
  static int find(Handle h,Iterator *out) {
    const auto name=handles.at(h).name;assert(!handles.at(h).write);
    if(!enter("verify-empty",name))return Error;
    if(data.at(name).empty())return leave(NotFound);
    *out=nextIterator++;iterators[*out]=true;return leave(Ok);
  }
  static void release(Iterator i){enter("release","",false);assert(iterators.erase(i)==1);leave(Ok);}
  static inline unsigned flashErases=0;
  static int eraseFlash() {
    assert(pending() && emptyApps()); // after every namespace, before the journal goes
    if(!enter("erase-flash","spiffs"))return Error;
    ++flashErases;return leave(Ok);
  }
};
using Store=DeviceWipe::BasicNvsStore<Nvs>;

static void recover() {
  Nvs::unrelatedIntact();Nvs::reboot();Store store;const auto state=DeviceWipe::inspect(store);
  // Clean after interruption means either nothing erased or everything erased.
  // A partial wipe may only be observed with its durable pending intent.
  assert(state==State::Pending || (state==State::Clean && (Nvs::emptyApps() || Nvs::originalApps())));
  assert((state==State::Pending?DeviceWipe::resume(store):DeviceWipe::begin(store))==Result::Complete);
  assert(Nvs::emptyApps() && DeviceWipe::inspect(store)==State::Clean);
  Nvs::unrelatedIntact();assert(Nvs::handles.empty() && Nvs::iterators.empty());
}

static unsigned testFaultBoundaries(bool resume) {
  Nvs::seed(resume);Store baseline;
  assert((resume?DeviceWipe::resume(baseline):DeviceWipe::begin(baseline))==Result::Complete);
  const auto calls=Nvs::calls;unsigned checked=0;
  for(size_t n=1;n<=calls.size();++n) {
    for(const auto fault:{Nvs::Fault::PowerBefore,Nvs::Fault::PowerAfter,Nvs::Fault::ErrorBefore}) {
      if(fault==Nvs::Fault::ErrorBefore && !calls[n-1].mayFail)continue;
      Nvs::seed(resume);Nvs::trip=n;Nvs::fault=fault;Store store;bool interrupted=false;
      try {assert((resume?DeviceWipe::resume(store):DeviceWipe::begin(store))==Result::Failed);}
      catch(const PowerCut &){interrupted=true;}
      assert(interrupted==(fault!=Nvs::Fault::ErrorBefore));recover();++checked;
    }
    if(calls[n-1].op=="commit") {
      Nvs::seed(resume);Nvs::trip=n;Nvs::fault=Nvs::Fault::CommitErrorAfter;Store store;
      assert((resume?DeviceWipe::resume(store):DeviceWipe::begin(store))==Result::Failed);recover();++checked;
    }
  }
  return checked;
}

static void testJournal() {
  Store store;Nvs::seed();assert(DeviceWipe::inspect(store)==State::Clean);
  Nvs::calls.clear();assert(DeviceWipe::resume(store)==Result::Complete && Nvs::erases==0 && Nvs::sets==0);
  assert(DeviceWipe::begin(store)==Result::Complete && Nvs::sets==1 && Nvs::erases==8 && Nvs::removals==1);
  assert(Nvs::emptyApps() && DeviceWipe::inspect(store)==State::Clean);Nvs::unrelatedIntact();
  Nvs::seed(true);assert(DeviceWipe::resume(store)==Result::Complete && Nvs::sets==0);
  Nvs::seed(true);assert(DeviceWipe::begin(store)==Result::Complete && Nvs::sets==0);
  assert(DeviceWipe::NAMESPACE_COUNT==Nvs::appNames().size());
  for(size_t i=0;i<Nvs::appNames().size();++i)assert(Nvs::appNames()[i]==DeviceWipe::NAMESPACES[i]);
  for(size_t i=0;i<sizeof DeviceWipe::INTENT_BYTES;++i) {
    Nvs::seed(true);Nvs::data["ll-reset"]["pending"].bytes[i]^=1;
    assert(DeviceWipe::inspect(store)==State::Invalid && DeviceWipe::resume(store)==Result::Failed);
    assert(Nvs::erases==0 && Nvs::sets==0 && Nvs::originalApps());
    // Only the explicit human-confirmed route may replace malformed intent.
    assert(DeviceWipe::begin(store)==Result::Complete && Nvs::emptyApps());Nvs::unrelatedIntact();
  }
  for(size_t length:{0u,1u,7u,9u,16u}) {
    Nvs::seed(true);Nvs::data["ll-reset"]["pending"].bytes.resize(length);
    assert(DeviceWipe::inspect(store)==State::Invalid && DeviceWipe::resume(store)==Result::Failed && Nvs::erases==0);
  }
  Nvs::seed(true);Nvs::data["ll-reset"]["pending"].blob=false;
  assert(DeviceWipe::inspect(store)==State::Invalid && DeviceWipe::resume(store)==Result::Failed && Nvs::erases==0);
  assert(DeviceWipe::begin(store)==Result::Complete && Nvs::emptyApps());
  Nvs::seed();Nvs::fault=Nvs::Fault::ErrorBefore;Nvs::trip=1;
  assert(DeviceWipe::begin(store)==Result::Failed && Nvs::erases==0 && Nvs::sets==0);
}

static void testVerification() {
  Store store;Nvs::seed();Nvs::corruptMarker=true;
  assert(DeviceWipe::begin(store)==Result::Failed && Nvs::erases==0 && Nvs::originalApps());
  assert(DeviceWipe::inspect(store)==State::Invalid);
  assert(DeviceWipe::begin(store)==Result::Complete && Nvs::emptyApps());
  Nvs::seed();Nvs::shortRead=true;
  assert(DeviceWipe::begin(store)==Result::Failed && Nvs::erases==0 && Nvs::originalApps());recover();
  Nvs::seed();Nvs::retainEntry=true;
  assert(DeviceWipe::begin(store)==Result::Failed && Nvs::removals==0 && Nvs::pending());
  assert(Nvs::iterators.empty());recover();
  Nvs::seed();Nvs::retainMarker=true;
  assert(DeviceWipe::begin(store)==Result::Failed && Nvs::emptyApps() && Nvs::pending());recover();
  for(const auto &name:Nvs::appNames()) {
    Nvs::seed();Nvs::partialEraseError=true;Nvs::partialEraseName=name;
    assert(DeviceWipe::begin(store)==Result::Failed && Nvs::pending() && !Nvs::originalApps());recover();
  }
  for(const char *name:{"ll-reset","M5Unified","phy","other-app","ll-moti-qa","ll-practice ","","nvs"}) {
    Nvs::seed(true);const auto before=Nvs::data;assert(!store.clearNamespace(name));assert(Nvs::data==before && Nvs::calls.empty());
  }
  Nvs::seed(true);assert(!store.clearNamespace(nullptr) && Nvs::calls.empty());
  // No allocation of missing target namespaces, even when every target is
  // absent. This also avoids needing spare namespace entries to wipe a device.
  for(size_t missing=0;missing<=Nvs::appNames().size();++missing) {
    Nvs::seed();for(size_t i=0;i<missing;++i)Nvs::data.erase(Nvs::appNames()[i]);
    assert(DeviceWipe::begin(store)==Result::Complete && Nvs::emptyApps());Nvs::unrelatedIntact();
    for(size_t i=0;i<missing;++i)assert(!Nvs::data.count(Nvs::appNames()[i]));
  }
}

int main() {
  testJournal();testVerification();
  const unsigned boundaries=testFaultBoundaries(false)+testFaultBoundaries(true);
  std::cout<<"PASS: resumable app-only wipe; durable complete intent; explicit invalid-intent repair; fresh erase/readback verification; marker removal last; "
    <<boundaries<<" fault/power-cut boundaries; partial commit/erase recovery; unrelated namespace preservation; missing namespace and iterator/error guards.\n";
}
