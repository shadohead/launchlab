// What firmware without the effect set does with it, using the frozen 0.15.0
// and 0.16.0 sources themselves (firmware/releases/<version>/source), each
// compiled beside this firmware in its own namespace.
//  - A set_effects request (U op 3) is refused whole: MILESTONE_ERROR
//    reason=invalid_request written=0.
//  - A format 5 backup (one with a non-default set) is refused whole before
//    anything is written: RESTORE_ERROR reason=invalid_backup written=0.
//  - With the default set, backups stay format 1-3 (0.15) or 1-4 (0.16) byte
//    for byte, and old firmware restores them; milestone requests are
//    byte-identical.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <stdint.h>
#include "../LaunchLabMini/device_backup.h"
#include "../LaunchLabMini/effect_set.h"
namespace Released015 {
#include "../releases/0.15.0-sticks3-milestone-pm/source/device_backup.h"
}
namespace Released016 {
#include "../releases/0.16.0-sticks3-effects-pm/source/device_backup.h"
}
#include <cassert>
#include <iostream>

// The same backup contents in either firmware's types.
#define FILL(NS,c,themed,tabled) do { \
  for(uint32_t i=1;i<=40;++i)c.practice.accept(i,true,5000+i*37,i*20000); \
  c.practice.newSession();c.practice.accept(41,true,9100,900000); \
  c.look.select(NS Appearance::Theme::Amber,NS Appearance::Effect::Lightning); \
  c.bests.observe(0,0,true,8800);c.bests.observe(1,1,true,7700); \
  c.settings={7,60,NS RpmEstimator::Mode::SingleTurn,NS StickS3SensorProfile::Mode::Tcrt,true}; \
  if(themed){NS Palette::Custom t;std::strncpy(t.name,"Dragoon Storm",NS Palette::Custom::MAX_NAME); \
    const uint16_t roles[]={0x0006,0xffff,0x5aeb,0x2d7f,0x2d7f,0xfda0,0x07e0,0xf800};std::memcpy(t.role,roles,sizeof(roles)); \
    c.hasThemes=c.themes.put(1,t);} \
  if(tabled){const NS Milestones::Row rows[]={{9000,NS Appearance::Effect::Sparkles},{12500,NS Appearance::Effect::Lightning},{15000,NS Appearance::Effect::Slash}}; \
    c.hasMilestones=c.milestones.set(NS Milestones::Mode::Both,rows,3)==NS Milestones::Problem::None;} \
} while(0)

int main() {
  namespace Old=Released015;
  static_assert(Old::DeviceBackup::FORMAT==3 && Old::Milestones::Request::BYTES==EffectSet::Request::BYTES && Milestones::Request::V1_BYTES==40 &&
                Old::Milestones::Request::MAGIC==EffectSet::Request::MAGIC);
  // U op 3: the old decoder takes the frame and refuses the op.
  {
    using EffectSet::flag;using Appearance::Effect;
    for(EffectSet::Mask mask:{EffectSet::DEFAULT_MASK,EffectSet::ALL,flag(Effect::Slash),flag(Effect::Orbit)|flag(Effect::Crimson)|flag(Effect::Ravage)}) {
      uint8_t b[EffectSet::Request::BYTES];EffectSet::Request::encode(mask,b);
      Old::Milestones::Request q;Old::Milestones::Problem p;unsigned row=0;
      assert(!q.decode(b,sizeof(b),p,row));
    }
    // Milestone requests are unchanged: the old firmware's own bytes.
    Milestones::Config table;Old::Milestones::Config oldTable;
    const Milestones::Row rows[]={{9500,Effect::Shockwave},{14500,Effect::Slash}};
    const Old::Milestones::Row oldRows[]={{9500,Old::Appearance::Effect::Shockwave},{14500,Old::Appearance::Effect::Slash}};
    assert(table.set(Milestones::Mode::Both,rows,2)==Milestones::Problem::None && oldTable.set(Old::Milestones::Mode::Both,oldRows,2)==Old::Milestones::Problem::None);
    // 0.15's editor sends the v1 frame, which this firmware still writes.
    uint8_t now[40],then[40];
    Milestones::Request::encode(Milestones::Request::Op::Set,&table,now,true);Old::Milestones::Request::encode(Old::Milestones::Request::Op::Set,&oldTable,then);
    assert(!std::memcmp(now,then,40));
    Milestones::Request::encode(Milestones::Request::Op::Reset,nullptr,now,true);Old::Milestones::Request::encode(Old::Milestones::Request::Op::Reset,nullptr,then);
    assert(!std::memcmp(now,then,40));
  }
  // Backups: the default set adds nothing; every format the old firmware
  // writes comes out byte for byte, and it restores ours.
  for(bool themed:{false,true})for(bool tabled:{false,true}) {
    auto c=std::make_unique<DeviceBackup::Contents>();auto o=std::make_unique<Old::DeviceBackup::Contents>();
    FILL(,(*c),themed,tabled);FILL(Old::,(*o),themed,tabled);
    assert(!c->hasEffectSet && c->format()==o->format() && c->format()<=DeviceBackup::MILESTONES_FORMAT);
    auto ours=std::make_unique<DeviceBackup::Buffer>();auto theirs=std::make_unique<Old::DeviceBackup::Buffer>();
    const size_t n=DeviceBackup::encode(*c,"0.17.0-sticks3-anime",*ours),m=Old::DeviceBackup::encode(*o,"0.17.0-sticks3-anime",*theirs);
    assert(n && n==m && !std::memcmp(ours->data(),theirs->data(),n));
    auto back=std::make_unique<Old::DeviceBackup::Contents>();assert(Old::DeviceBackup::decode(ours->data(),n,*back));
    // A set that differs from the default makes format 5, which 0.15 refuses whole.
    c->hasEffectSet=c->effectSet.set(EffectSet::flag(Appearance::Effect::Slash)|EffectSet::flag(Appearance::Effect::Crimson))==EffectSet::Problem::None;
    assert(c->format()==DeviceBackup::FORMAT);
    const size_t k=DeviceBackup::encode(*c,"0.17.0-sticks3-anime",*ours);
    assert(DeviceBackup::get32(ours->data()+4)==5);
    auto refused=std::make_unique<Old::DeviceBackup::Contents>();assert(!Old::DeviceBackup::decode(ours->data(),k,*refused));
    // Even claiming format 3 (resealed), the extra section is refused.
    DeviceBackup::put32(ours->data()+4,3);DeviceBackup::put32(ours->data()+k-4,DeviceBackup::checksum(ours->data(),k-4));
    assert(!Old::DeviceBackup::decode(ours->data(),k,*refused));
    // This firmware still restores the old firmware's backups.
    auto mine=std::make_unique<DeviceBackup::Contents>();assert(DeviceBackup::decode(theirs->data(),m,*mine) && !mine->hasEffectSet && mine->hasThemes==themed && mine->hasMilestones==tabled);
  }
  // 0.16.0: the pooled milestones of format 4, and the v2 request.
  {
    namespace New=Released016;
    static_assert(New::DeviceBackup::FORMAT==4 && DeviceBackup::POOLS_FORMAT==4 && New::Milestones::Request::V1_BYTES==EffectSet::Request::BYTES &&
                  New::Milestones::Request::V1_MAGIC==EffectSet::Request::MAGIC);
    using EffectSet::flag;using Appearance::Effect;
    for(EffectSet::Mask mask:{EffectSet::DEFAULT_MASK,EffectSet::ALL,flag(Effect::Slash),flag(Effect::Orbit)|flag(Effect::Crimson)|flag(Effect::Ravage)}) {
      uint8_t b[EffectSet::Request::BYTES];EffectSet::Request::encode(mask,b);
      New::Milestones::Request q;New::Milestones::Problem p;unsigned row=0;
      assert(!q.decode(b,sizeof(b),p,row));
    }
    // A pooled table of 0.16's effects: the same request bytes as 0.16's.
    const uint64_t pool=(flag(Effect::Tidal)|flag(Effect::Bloom)).lo;  // milestone pools stay 64-bit
    const Milestones::Row rows[]={{9000,Effect::Sparkles},{12000,pool},{15000,Effect::Slash}};
    const New::Milestones::Row newRows[]={{9000,New::Appearance::Effect::Sparkles},{12000,pool},{15000,New::Appearance::Effect::Slash}};
    Milestones::Config table;New::Milestones::Config newTable;
    assert(table.set(Milestones::Mode::Both,rows,3)==Milestones::Problem::None && newTable.set(New::Milestones::Mode::Both,newRows,3)==New::Milestones::Problem::None);
    uint8_t now[Milestones::Request::BYTES],then[New::Milestones::Request::BYTES];
    static_assert(sizeof(now)==sizeof(then));
    Milestones::Request::encode(Milestones::Request::Op::Set,&table,now);New::Milestones::Request::encode(New::Milestones::Request::Op::Set,&newTable,then);
    assert(!std::memcmp(now,then,sizeof(now)));
    // A row naming an effect 0.16 lacks is refused there as a bad table.
    const Milestones::Row anime[]={{9000,Effect::Crimson}};
    assert(table.set(Milestones::Mode::Milestones,anime,1)==Milestones::Problem::None);
    Milestones::Request::encode(Milestones::Request::Op::Set,&table,now);
    {New::Milestones::Request q;New::Milestones::Problem p;unsigned row=9;assert(q.decode(now,sizeof(now),p,row) && p!=New::Milestones::Problem::None && row==0);}
    for(bool themed:{false,true})for(int tabled:{0,1,2}) {
      auto c=std::make_unique<DeviceBackup::Contents>();auto o=std::make_unique<New::DeviceBackup::Contents>();
      FILL(,(*c),themed,tabled==1);FILL(New::,(*o),themed,tabled==1);
      if(tabled==2) {
        c->hasMilestones=c->milestones.set(Milestones::Mode::Both,rows,3)==Milestones::Problem::None;
        o->hasMilestones=o->milestones.set(New::Milestones::Mode::Both,newRows,3)==New::Milestones::Problem::None;
      }
      assert(!c->hasEffectSet && c->format()==o->format() && c->format()<=DeviceBackup::POOLS_FORMAT && (tabled!=2 || c->format()==4));
      auto ours=std::make_unique<DeviceBackup::Buffer>();auto theirs=std::make_unique<New::DeviceBackup::Buffer>();
      const size_t n=DeviceBackup::encode(*c,"0.17.0-sticks3-anime",*ours),m=New::DeviceBackup::encode(*o,"0.17.0-sticks3-anime",*theirs);
      assert(n && n==m && !std::memcmp(ours->data(),theirs->data(),n));
      auto back=std::make_unique<New::DeviceBackup::Contents>();assert(New::DeviceBackup::decode(ours->data(),n,*back));
      // A set that differs from the default makes format 5, which 0.16 refuses whole.
      c->hasEffectSet=c->effectSet.set(flag(Effect::Slash)|flag(Effect::Crimson))==EffectSet::Problem::None;
      const size_t k=DeviceBackup::encode(*c,"0.17.0-sticks3-anime",*ours);
      assert(DeviceBackup::get32(ours->data()+4)==5);
      auto refused=std::make_unique<New::DeviceBackup::Contents>();assert(!New::DeviceBackup::decode(ours->data(),k,*refused));
      // Even claiming format 4 (resealed), the extra section is refused.
      DeviceBackup::put32(ours->data()+4,4);DeviceBackup::put32(ours->data()+k-4,DeviceBackup::checksum(ours->data(),k-4));
      assert(!New::DeviceBackup::decode(ours->data(),k,*refused));
      // This firmware restores 0.16's backups.
      auto mine=std::make_unique<DeviceBackup::Contents>();
      assert(DeviceBackup::decode(theirs->data(),m,*mine) && !mine->hasEffectSet && mine->hasThemes==themed && mine->hasMilestones==(tabled!=0));
      if(tabled)assert(mine->milestones.same(c->milestones));
    }
  }
  std::cout<<"PASS: frozen 0.15 and 0.16 sources refuse set_effects requests and format 5 backups whole; default-set backups stay their formats byte for byte and restore there; milestone requests unchanged\n";
}
