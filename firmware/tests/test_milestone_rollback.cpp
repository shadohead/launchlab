// A unit rolled back from 0.18 to 0.15 or 0.16 (Repair / rollback), using
// the frozen sources themselves (firmware/releases/<version>/source), each
// compiled beside this firmware in its own namespace over one fake NVS.
// 0.17 stores its table exactly as 0.16 does (the same milestones.h and
// appearance_store.h) and reads every table 0.16 reads.
//  - 0.18 holds no more NVS for the table than 0.17 does: two 8-entry slots
//    in 0.16's keys beside 0.15's record, whatever the table, so the older
//    build's saves beside a full history keep their room.
//  - 0.16 shows the table, or for one it cannot read 0.18's view of it, and
//    its next save writes over the slot it cannot read; a later 0.18 then
//    loads the table 0.16 saved, not the one from before the rollback.
//  - 0.15 shows its own record, which 0.16 and later never write.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <Preferences.h>
#include "../LaunchLabMini/milestone_store.h"
namespace Released015 {
#include "../releases/0.15.0-sticks3-milestone-pm/source/appearance_store.h"
#include "../releases/0.15.0-sticks3-milestone-pm/source/milestones.h"
}
namespace Released016 {
#include "../releases/0.16.0-sticks3-effects-pm/source/appearance_store.h"
#include "../releases/0.16.0-sticks3-effects-pm/source/milestones.h"
}
#include <cassert>
#include <iostream>
#include <string>

namespace {
using namespace Milestones;using Appearance::Effect;using EffectSet::flag;
// NVS entries the table's keys hold (0.16's two slots, 0.15's two).
unsigned tableEntries() {
  unsigned n=0;
  for(const char *k:{"mpool0","mpool1","mile0","mile1"}) {
    const auto at=Preferences::data.find("ll-appearance"+std::string(k));
    if(at!=Preferences::data.end())n+=NvsBudget::blobEntries(unsigned(at->second.size()));
  }
  return n;
}
// What 0.17 can hold here: its two slots and 0.15's record.
constexpr unsigned V017_ENTRIES=2*NvsBudget::blobEntries(Config::V2_IMAGE_BYTES)+2*NvsBudget::blobEntries(Config::V1_IMAGE_BYTES);
bool slotsAtMost8(){
  for(const char *k:{"mpool0","mpool1"}) {
    const auto at=Preferences::data.find("ll-appearance"+std::string(k));
    if(at!=Preferences::data.end() && NvsBudget::blobEntries(unsigned(at->second.size()))>8)return false;
  }
  return true;
}
// A boot of each firmware: the table it shows.
struct Boot018 {MilestoneStore store;Config table;bool loaded;Boot018(){loaded=store.begin(table);}};
struct Boot016 {
  Released016::AppearanceStore<Released016::Milestones::Config> store;Released016::Milestones::Config table;bool loaded;
  Boot016(){loaded=store.begin(table,"ll-appearance","mpool0","mpool1",true);}
};
struct Boot015 {
  Released015::AppearanceStore<Released015::Milestones::Config> store;Released015::Milestones::Config table;bool loaded;
  Boot015(){loaded=store.begin(table,"ll-appearance","mile0","mile1",true);}
};
// The same rows in either firmware: thresholds and the effects 0.16 knows.
bool same016(const Released016::Milestones::Config &old,const Config &now) {
  if(unsigned(old.mode())!=unsigned(now.mode()) || old.count()!=now.count())return false;
  for(unsigned i=0;i<now.count();++i)if(old.row(i).rpm!=now.row(i).rpm || old.row(i).pool!=now.row(i).pool.lo || now.row(i).pool.hi)return false;
  return true;
}
}

int main() {
  // A unit upgraded from 0.15: its table, then 0.18's tables of every kind.
  Preferences::data.clear();
  {Released015::Milestones::Config t;const Released015::Milestones::Row rows[]={{9000,Released015::Appearance::Effect::Orbit},{13000,Released015::Appearance::Effect::Slash}};
   assert(t.set(Released015::Milestones::Mode::Both,rows,2)==Released015::Milestones::Problem::None);
   Boot015 b;assert(!b.loaded || b.table.isDefault());assert(b.store.save(t));}
  const auto legacy=Preferences::data;
  {Boot018 b;assert(b.loaded && b.store.source()==MilestoneStore::Source::Legacy && b.table.count()==2);}

  const Row wideRows[]={{9000,flag(Effect::Sparkles)|flag(Effect::Blink)},{11500,flag(Effect::Crimson)|flag(Effect::Volley)},{15500,flag(Effect::Petalfall)}};
  const Row lateRows[]={{9500,flag(Effect::Tidal)},{14000,flag(Effect::Slash)|flag(Effect::Haymaker)}};  // 0.17's and 0.18's effects within 0-63
  const Row oldRows[]={{10000,flag(Effect::Lightning)},{12000,flag(Effect::Orbit)|flag(Effect::Flames)}};
  struct Case {const char *name;const Row *rows;unsigned count;};
  const Case cases[]={{"wide",wideRows,3},{"late",lateRows,2},{"old",oldRows,2}};
  for(const Case &k:cases) {
    // 0.18 saves the table (twice, so both slots have been written by 0.18).
    Config table;
    {Boot018 b;table=b.table;assert(table.set(Mode::Both,k.rows,k.count)==Problem::None && b.store.save(table));
     assert(table.select(Mode::Milestones) && b.store.save(table));}
    assert(tableEntries()<=V017_ENTRIES && slotsAtMost8());
    for(const auto &[key,value]:legacy)assert(Preferences::data.at(key)==value);
    // 0.15 shows its own record.
    {Boot015 b;assert(b.loaded && b.table.count()==2 && b.table.row(1).rpm==13000);}
    // 0.16 shows the table, or 0.18's view of one it cannot read; nothing is damaged.
    const Config shown=table.fitsV016()?table:table.forV016();
    {Boot016 b;assert(b.loaded && !b.store.damaged() && same016(b.table,shown));}
    // Back on 0.18 with no save in between: the table itself.
    {Boot018 b;assert(b.loaded && b.table.same(table) && b.table.revision==table.revision);}
    // 0.16 saves a change: its slot replaces the one it could not read, and 0.18 loads 0.16's table.
    Released016::Milestones::Config edited;
    {Boot016 b;edited=b.table;assert(edited.select(Released016::Milestones::Mode::Best) && b.store.save(edited));
     assert(tableEntries()<=V017_ENTRIES && slotsAtMost8());}
    {Boot016 b;assert(b.loaded && !b.store.damaged() && b.table.same(edited));}
    {Boot018 b;assert(b.loaded && b.table.mode()==Mode::Best && b.table.revision==edited.revision && b.store.source()==MilestoneStore::Source::Narrow);
     for(unsigned i=0;i<shown.count();++i)assert(b.table.row(i)==shown.row(i));}
    // And a second 0.16 save, then 0.18 again.
    {Boot016 b;edited=b.table;assert(edited.select(Released016::Milestones::Mode::Milestones) && b.store.save(edited));}
    {Boot018 b;assert(b.loaded && b.table.mode()==Mode::Milestones && b.table.revision==edited.revision);}
    for(const auto &[key,value]:legacy)assert(Preferences::data.at(key)==value);
  }
  // A unit that never ran 0.15: two slots only.
  Preferences::data.clear();
  {Boot018 b;Config t=b.table;assert(t.set(Mode::Both,wideRows,3)==Problem::None && b.store.save(t));}
  assert(tableEntries()==2*NvsBudget::blobEntries(Config::V2_IMAGE_BYTES));
  {Boot016 b;assert(b.loaded && !b.store.damaged());}
  std::cout<<"PASS: rolled back to 0.16 or 0.15 (frozen sources): 0.18's table holds no more NVS than 0.17's; 0.16 shows the table or 0.18's view of it and "
             "its saves replace the slot it cannot read, so 0.18 loads them; 0.15's record stays untouched\n";
}
