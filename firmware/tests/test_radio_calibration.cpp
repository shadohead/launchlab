// The radio's calibration data stays out of NVS (radio_calibration.h). On the
// rig a unit that had had Bluetooth on once kept 67 entries of it and then
// refused every custom theme and milestone save (2026-11-10).
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "radio_calibration.h"

// An NVS with one namespace that may exist and hold entries.
struct Nvs {
  using Handle=int;
  static constexpr int Ok=0,NotFound=1,Broken=2;
  static bool exists;static size_t entries;
  static int failOpenWrite,failUsed,failErase,failCommit;
  static unsigned opens,writeOpens,open_,erases,commits;
  static bool writing;
  static void reset(bool e,size_t n){exists=e;entries=n;failOpenWrite=failUsed=failErase=failCommit=Ok;opens=writeOpens=open_=erases=commits=0;writing=false;}
  static int open(const char *name,bool write,Handle *h) {
    assert(std::string(name)=="phy");++opens;
    if(write) {
      ++writeOpens;
      if(failOpenWrite!=Ok)return failOpenWrite;
      exists=true;  // as nvs_open does: opening for writing makes the namespace
    } else if(!exists)return NotFound;
    ++open_;writing=write;*h=7;return Ok;
  }
  static int used(Handle h,size_t *n){assert(h==7 && open_);if(failUsed!=Ok)return failUsed;*n=entries;return Ok;}
  static int eraseAll(Handle h){assert(h==7 && open_ && writing);++erases;if(failErase!=Ok)return failErase;entries=0;return Ok;}
  static int commit(Handle h){assert(h==7 && open_ && writing);++commits;return failCommit;}
  static void close(Handle h){assert(h==7 && open_);--open_;}
};
bool Nvs::exists=false,Nvs::writing=false;size_t Nvs::entries=0;
int Nvs::failOpenWrite=0,Nvs::failUsed=0,Nvs::failErase=0,Nvs::failCommit=0;
unsigned Nvs::opens=0,Nvs::writeOpens=0,Nvs::open_=0,Nvs::erases=0,Nvs::commits=0;

static std::string file(const char *path){std::ifstream in(path);std::stringstream s;s<<in.rdbuf();return s.str();}
static bool has(const std::string &text,const char *what){return text.find(what)!=std::string::npos;}

int main() {
  using RadioCalibration::reclaim;
  // The SDK's namespace, exactly, and nothing else: every other store opens as before.
  assert(RadioCalibration::refuses("phy"));
  for(const char *other:{"launchlab-mini","ll-practice","ll-motion","ll-appearance","ll-power","ll-bests","M5GFX","phy2","ph","PHY",""})
    assert(!RadioCalibration::refuses(other));
  assert(!RadioCalibration::refuses(nullptr));

  // A unit that never had the radio on: nothing to remove, and the namespace
  // is not made (opening it for writing would take an entry for good).
  Nvs::reset(false,0);assert(reclaim<Nvs>()==0 && !Nvs::exists && !Nvs::writeOpens && !Nvs::open_);
  // What an earlier build left: all 67 entries go, committed, and it is closed.
  Nvs::reset(true,67);assert(reclaim<Nvs>()==67 && Nvs::entries==0 && Nvs::erases==1 && Nvs::commits==1 && !Nvs::open_);
  // Already empty (every boot after that): no flash write at all.
  Nvs::reset(true,0);assert(reclaim<Nvs>()==0 && !Nvs::writeOpens && !Nvs::erases && !Nvs::open_);
  // Any failure is reported as one, never as "removed", and nothing stays open.
  Nvs::reset(true,67);Nvs::failUsed=Nvs::Broken;assert(reclaim<Nvs>()==-1 && !Nvs::writeOpens && !Nvs::open_);
  Nvs::reset(true,67);Nvs::failOpenWrite=Nvs::Broken;assert(reclaim<Nvs>()==-1 && !Nvs::erases && !Nvs::open_);
  Nvs::reset(true,67);Nvs::failErase=Nvs::Broken;assert(reclaim<Nvs>()==-1 && Nvs::entries==67 && !Nvs::open_);
  Nvs::reset(true,67);Nvs::failCommit=Nvs::Broken;assert(reclaim<Nvs>()==-1 && !Nvs::open_);

  // The builds: the define and the linker's wrap go together in every script
  // build, and a test build adds its own wraps without replacing this one.
  const std::string base=file("firmware/build-m5.sh"),pm=file("firmware/build-m5-pm.sh");
  if(base.empty()) {
    // The public copy (firmware/SYNC.json) builds with scripts/build-firmware.sh:
    // one build function for the production and the HIL image.
    const std::string pub=file("scripts/build-firmware.sh");
    assert(!file("firmware/SYNC.json").empty() && !pub.empty());
    assert(has(pub,"radio_cal_define=-DLAUNCHLAB_RADIO_CAL_WRAP=1") && has(pub,"radio_cal_link=-Wl,--wrap=nvs_open"));
    assert(has(pub,"compiler.c.elf.extra_flags=$radio_cal_link${link:+ $link}"));
    assert(has(pub,"$radio_cal_define${extra:+ $extra}\""));
    assert(pub.find("compiler.c.elf.extra_flags")==pub.rfind("compiler.c.elf.extra_flags"));
  } else {
    assert(has(base,"radio_cal_define=-DLAUNCHLAB_RADIO_CAL_WRAP=1") && has(base,"radio_cal_link=-Wl,--wrap=nvs_open"));
    assert(has(base,"compiler.c.elf.extra_flags=$radio_cal_link${LAUNCHLAB_EXTRA_LINK_FLAGS:+ $LAUNCHLAB_EXTRA_LINK_FLAGS}"));
    assert(has(base,"$radio_cal_define\""));
    // build-m5-pm.sh replaces the C++ flags, so it names the define itself.
    assert(has(pm,"-DLAUNCHLAB_RADIO_CAL_WRAP=1") && !has(pm,"compiler.c.elf.extra_flags"));
    for(const char *path:{"firmware/build-m5-hil.sh","firmware/build-m5-lab.sh","firmware/build-m5-release.sh"}) {
      const std::string script=file(path);
      assert(!script.empty() && !has(script,"compiler.c.elf.extra_flags"));
    }
  }
  std::cout<<"radio calibration: the SDK's namespace is refused, what an earlier build left is removed, and every build wraps nvs_open\n";
}
