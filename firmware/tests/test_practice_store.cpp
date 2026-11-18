#include "../LaunchLabMini/practice_store.h"
#include <cassert>
#include <iostream>
int main() {
  PracticeHistory h;PracticeStore store;assert(store.begin(h) && !store.pending(h));
  h.accept(1,true,4000,100);assert(store.pending(h) && store.save(h) && !store.pending(h));
  h.accept(2,true,8000,30100);assert(store.save(h));
  PracticeHistory loaded;PracticeStore reboot;
  assert(reboot.begin(loaded) && loaded.size()==2 && loaded.session()->mean()==6000);
  // Latest slot damaged: load earlier complete snapshot without erasing bytes.
  Preferences::data["ll-practicehistory0"][100]^=1;
  PracticeHistory fallback;PracticeStore recovery;
  assert(recovery.begin(fallback) && fallback.size()==1 && fallback.recent()->rpm==4000);
  fallback.accept(1,true,9000,100);
  Preferences::failWrite=true;assert(!recovery.save(fallback) && recovery.pending(fallback));
  Preferences::failWrite=false;assert(recovery.save(fallback) && !recovery.pending(fallback));
  PracticeHistory durable;PracticeStore reread;assert(reread.begin(durable) && durable.size()==2 && durable.recent()->rpm==9000);
  PracticeHistory scratch;PracticeStore isolated;assert(isolated.begin(scratch,"ll-prac-qa"));
  scratch.accept(1,true,16000,100);assert(isolated.save(scratch));
  PracticeHistory real;PracticeStore realStore;assert(realStore.begin(real) && real.recent()->rpm==9000);
  durable.accept(1,true,10000,100);Preferences::shortRead=true;
  assert(!reread.save(durable) && reread.pending(durable));Preferences::shortRead=false;
  const auto before=Preferences::data;
  for(auto &entry:Preferences::data)if(entry.first.find("ll-practice")==0)entry.second[0]^=1;
  const auto damaged=Preferences::data;
  PracticeHistory empty;PracticeStore broken;assert(!broken.begin(empty));
  assert(Preferences::data==damaged && !broken.ready());
  // The orphaned RF calibration blob is removed alone; history slots and other
  // namespaces stay byte-identical, and a store without it is left alone.
  Preferences::data.clear();
  PracticeHistory kept;PracticeStore owner;assert(owner.begin(kept));
  for(unsigned i=1;i<=3;++i){kept.accept(i,true,5000+i,i*1000);assert(owner.save(kept));}
  Preferences::data["ll-practicecal_data"]=std::vector<unsigned char>(1904,0xa5);
  Preferences::data["ll-powerlog"]={1,2,3};Preferences::data["ll-motioncal_data"]={4};
  auto withoutOrphan=Preferences::data;withoutOrphan.erase("ll-practicecal_data");
  PracticeHistory reopened;PracticeStore reclaim;assert(reclaim.begin(reopened) && reopened.size()==3);
  assert(reclaim.hasOrphan() && reclaim.removeOrphan() && !reclaim.hasOrphan());
  assert(Preferences::data==withoutOrphan);
  assert(!reclaim.removeOrphan() && Preferences::data==withoutOrphan);
  PracticeHistory after;PracticeStore again;assert(again.begin(after) && after.size()==3 && after.generation()==kept.generation());
  PracticeStore closed;assert(!closed.hasOrphan() && !closed.removeOrphan());
  // A restore superseded the device's history (generation 21 over 20) and
  // then failed, leaving the device on its own generation 20. Saving that
  // over the other slot would lose to the restored record at the next boot,
  // so the store refuses it; superseding from the saved generation saves.
  {
    Preferences::data.clear();
    auto pulls=[](unsigned n,float rpm){PracticeHistory h;for(unsigned i=0;i<n;++i)h.accept(i+1,true,rpm+i,1000*(i+1));return h;};
    PracticeStore store;PracticeHistory ram;assert(store.begin(ram));
    ram=pulls(20,8000);assert(store.save(ram) && store.savedGeneration()==20);
    PracticeHistory restored=pulls(5,12000);restored.supersede(ram.generation());assert(store.save(restored));
    assert(store.pending(ram) && !store.save(ram) && store.savedGeneration()==21);
    PracticeHistory loaded;PracticeStore boot;assert(boot.begin(loaded) && loaded.generation()==21 && loaded.size()==5);
    ram.supersede(store.savedGeneration());assert(store.save(ram));
    PracticeStore again;assert(again.begin(loaded) && loaded.generation()==22 && loaded.size()==20);
  }
  std::cout<<"PASS: older-generation save refused; alternating snapshots, exact save/readback, reboot selection, damaged newest fallback, failed write/read retention and no destructive corruption recovery; orphaned calibration blob removed alone\n";
}
