// The loaded-effect store in the spiffs partition (effect_store.h) on the NOR
// flash model: it adds its region by docs/FIRMWARE_FLASH_LAYOUT.md's rules
// without disturbing the history log, installs, replaces and deletes
// effects, refuses what does not fit, and after a power cut at any byte of
// an install, replace or delete holds either the old effect or the new one,
// whole.
#include "../LaunchLabMini/effect_store.h"
#include "../LaunchLabMini/history_log.h"
#include "../tools/fx/fx_compiler.h"
#include "../tools/fx/fx_share.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "nor_flash.h"

namespace {
using Nor=NorFlash;
using Store=EffectStore<Nor>;

std::vector<uint8_t> compileText(const std::string &text) {
  const auto r=FxCompiler::compile(text);
  if(!r.ok)std::cerr<<r.line<<": "<<r.error<<"\n";
  assert(r.ok);return r.bytes;
}
// A small effect with its own uid and a body that grows with `size`.
std::vector<uint8_t> effect(uint64_t uid,int size=1) {
  char head[96];std::snprintf(head,sizeof(head),"effect \"E%llx\"\nuid 0x%llx\nduration 2000\n",(unsigned long long)uid,(unsigned long long)uid);
  std::string text=head;
  for(int i=0;i<size;++i)text+="frame {\n  plot(acx + "+std::to_string(i)+", ay1 + 20, accent, 1 - t/2)\n}\n";
  return compileText(text);
}
std::string sourceOf(uint64_t uid){char b[64];std::snprintf(b,sizeof(b),"packed source %llx",(unsigned long long)uid);return b;}
bool never(unsigned){return false;}
unsigned install(Store &s,const std::vector<uint8_t> &file,const std::string &source={}) {
  unsigned slot=99;
  const auto r=s.put(file.data(),file.size(),reinterpret_cast<const uint8_t *>(source.data()),source.size(),never,slot);
  assert(r==Store::Result::Ok);return slot;
}
void prepare(Store &s,unsigned &steps) {
  bool failed=false;steps=0;
  while(!s.prepareStep(failed)){assert(!failed);++steps;}
  assert(!failed && s.isOpen());
}
std::vector<uint8_t> file(const Store &s,unsigned slot) {
  std::vector<uint8_t> out(Store::MAX_PAYLOAD);size_t n=0;
  if(!s.readFile(slot,out.data(),out.size(),n))return {};
  out.resize(n);return out;
}
std::vector<uint8_t> bytes(const Nor &f,uint32_t at,uint32_t n){return {f.bytes.begin()+at,f.bytes.begin()+at+n};}
}

int main() {
  // A blank partition: unformatted until prepared, which erases the effect
  // region and writes superblock copy 0 with that region only.
  {
    Nor f;Store s;assert(s.open(f)==Store::Open::Unformatted && !s.isOpen());
    unsigned slot=0;const auto e=effect(1);
    assert(s.put(e.data(),e.size(),nullptr,0,never,slot)==Store::Result::Unavailable);
    unsigned steps=0;prepare(s,steps);assert(steps==144);
    FlashLayout::Superblock sb;assert(sb.decode(f.bytes.data()) && sb.count==1 && sb.find(FlashLayout::Effects) && !sb.find(FlashLayout::History));
    assert(s.installed()==0 && s.freeSlots()==36);
  }
  // After the history log (0.18 and on): the region is added in the other
  // superblock copy with the history entry unchanged, garbage in the effect
  // region is erased, and the history log still opens with its records.
  {
    Nor f;std::srand(19);
    for(uint32_t at=FlashLayout::EFFECTS_OFFSET;at<FlashLayout::PARTITION_BYTES;++at)f.bytes[at]=uint8_t(std::rand());
    HistoryLog::Store<Nor> log;assert(log.open(f)==FlashLog<Nor>::Open::Formatted);
    PracticeHistory h;for(unsigned i=0;i<10;++i)assert(h.accept(1+i,true,9000+11.0f*float(i),1000*(1+i)));
    assert(log.saveHistory(h,3));
    const auto historyBytes=bytes(f,FlashLayout::HISTORY_OFFSET,FlashLayout::HISTORY_BYTES);
    Store s;assert(s.open(f)==Store::Open::Unformatted);
    unsigned steps=0;prepare(s,steps);
    FlashLayout::Superblock old,added;
    assert(old.decode(f.bytes.data()+FlashLayout::SUPER_OFFSET[0]) && added.decode(f.bytes.data()+FlashLayout::SUPER_OFFSET[1]));
    assert(added.sequence==old.sequence+1 && added.count==2 && added.regions[0].kind==FlashLayout::History &&
           added.regions[0].offset==old.regions[0].offset && added.regions[0].size==old.regions[0].size && added.find(FlashLayout::Effects));
    assert(bytes(f,FlashLayout::HISTORY_OFFSET,FlashLayout::HISTORY_BYTES)==historyBytes);
    for(uint32_t at=FlashLayout::EFFECTS_OFFSET;at<FlashLayout::PARTITION_BYTES;++at)assert(f.bytes[at]==0xff);
    HistoryLog::Store<Nor> again;assert(again.open(f)==FlashLog<Nor>::Open::Ready);
    PracticeHistory back;uint32_t m=0;assert(again.loadHistory(back,m) && m==3);
    // And the effect store keeps working beside it.
    install(s,effect(5),sourceOf(5));Store reopened;assert(reopened.open(f)==Store::Open::Ready && reopened.installed()==1);
  }
  // A newer layout, or an effect region somewhere else: left alone.
  {
    for(int variant=0;variant<2;++variant) {
      Nor f;FlashLayout::Superblock sb=FlashLayout::historyLayout(4);
      if(variant==0)sb.version=2;else sb.regions[sb.count++]={FlashLayout::Effects,0,FlashLayout::PULLS_OFFSET,FlashLayout::EFFECTS_BYTES};
      FlashLayout::Superblock::Image b;sb.encode(b);std::copy(b.begin(),b.end(),f.bytes.begin());
      const auto before=f.bytes;Store s;assert(s.open(f)==Store::Open::Unsupported);
      bool failed=false;assert(!s.prepareStep(failed) && failed && f.bytes==before);
    }
  }

  Nor f;Store s;unsigned steps=0;assert(s.open(f)==Store::Open::Unformatted);prepare(s,steps);
  // Install and read back, also after a reboot; the packed source travels with the file.
  {
    const auto e=effect(0x100,3);const unsigned slot=install(s,e,sourceOf(0x100));
    assert(slot==0 && s.installed()==1 && file(s,0)==e && s.entry(0).uid==0x100);
    Store again;assert(again.open(f)==Store::Open::Ready && again.installed()==1 && file(again,0)==e);
    std::vector<uint8_t> src(64);size_t n=0;assert(again.readSource(0,src.data(),src.size(),n) && std::string(src.begin(),src.begin()+long(n))==sourceOf(0x100));
  }
  // The same uid replaces itself in its slot; the old copy is erased.
  {
    const unsigned before=s.entry(0).physical;
    const auto e=effect(0x100,5);assert(install(s,e)==0 && file(s,0)==e && s.entry(0).physical!=before && s.installed()==1);
    for(uint32_t i=0;i<Store::SECTOR;++i)assert(f.bytes[FlashLayout::EFFECTS_OFFSET+before*Store::SLOT_BYTES+i]==0xff);
    Store again;assert(again.open(f)==Store::Open::Ready && again.installed()==1 && file(again,0)==e && again.freeSlots()==35);
  }
  // A new uid takes the lowest free slot nothing references.
  {
    unsigned slot=0;const auto e=effect(0x200);
    assert(s.put(e.data(),e.size(),nullptr,0,[](unsigned i){return i==1;},slot)==Store::Result::Ok && slot==2);
    assert(s.remove(2)==Store::Result::Ok && s.remove(2)==Store::Result::NotFound && file(s,2).empty());
    Store again;assert(again.open(f)==Store::Open::Ready && again.installed()==1);
  }
  // Refusals: a damaged file, one for a newer engine, too large, and a full store.
  {
    unsigned slot=0;auto e=effect(0x300);
    auto bad=e;bad[40]^=1;assert(s.put(bad.data(),bad.size(),nullptr,0,never,slot)==Store::Result::Invalid);
    auto newer=e;newer[5]=LoadableFx::ENGINE+1;LoadableFx::put32(newer.data()+28,0);LoadableFx::put32(newer.data()+28,LoadableFx::crc32(newer.data(),newer.size()));
    assert(s.put(newer.data(),newer.size(),nullptr,0,never,slot)==Store::Result::Engine);
    const std::vector<uint8_t> big(Store::MAX_PAYLOAD);
    assert(s.put(e.data(),e.size(),big.data(),big.size()-e.size()+1,never,slot)==Store::Result::TooLarge);
    for(uint64_t u=0x1000;s.installed()<32;++u)install(s,effect(u));
    assert(s.installed()==32 && s.freeSlots()==4);
    assert(s.put(e.data(),e.size(),nullptr,0,never,slot)==Store::Result::Full);
    // A replace still works when full.
    const auto again=effect(0x100,2);assert(install(s,again)==0 && file(s,0)==again);
    assert(s.clear()==Store::Result::Ok && s.installed()==0);
    Store reopened;assert(reopened.open(f)==Store::Open::Ready && reopened.installed()==0 && reopened.freeSlots()==36);
  }
  // A restore puts each file into the slot the backup had it in: over
  // whatever is there, and the same uid elsewhere goes (one copy per uid).
  {
    const auto a=effect(0x600),b=effect(0x601);
    assert(s.putAt(5,a.data(),a.size(),nullptr,0)==Store::Result::Ok && file(s,5)==a && s.entry(5).uid==0x600);
    assert(s.putAt(5,b.data(),b.size(),nullptr,0)==Store::Result::Ok && file(s,5)==b && s.slotOf(0x600)<0);
    assert(s.putAt(9,b.data(),b.size(),nullptr,0)==Store::Result::Ok && file(s,9)==b && !s.entry(5).used && s.slotOf(0x601)==9);
    assert(s.putAt(32,a.data(),a.size(),nullptr,0)==Store::Result::NotFound);
    Store again;assert(again.open(f)==Store::Open::Ready && again.slotOf(0x601)==9 && again.installed()==s.installed());
    assert(s.remove(9)==Store::Result::Ok);
  }
  // A payload damaged in flash (a bit gone): reported damaged, never read.
  {
    const auto e=effect(0x400,4);const unsigned slot=install(s,e);
    const uint32_t at=FlashLayout::EFFECTS_OFFSET+s.entry(slot).physical*Store::SLOT_BYTES+Store::HEADER+20;
    f.bytes[at]&=0x7f;f.bytes[at]^=0;if(f.bytes[at]==e[20])f.bytes[at]^=0x80;
    Store again;assert(again.open(f)==Store::Open::Ready && again.entry(slot).used && again.entry(slot).state==Store::State::Damaged && file(again,slot).empty());
    assert(again.remove(slot)==Store::Result::Ok);
    assert(s.open(f)==Store::Open::Ready);
  }
  // Power cut at every point of a fresh install, a replace and a delete: on
  // the next boot the slot holds the old effect or the new one, whole, and
  // never two copies.
  {
    const auto first=effect(0x500,2),second=effect(0x500,6);const std::string src=sourceOf(0x500);
    const size_t total=second.size()+src.size()+Store::HEADER;
    for(int op=0;op<3;++op) {
      unsigned outcomes[2]={0,0};
      for(long cut=0;cut<=long(total)+1;cut+=(cut<64 || cut>long(total)-64)?1:29) {
        Nor g;Store a;unsigned n=0;assert(a.open(g)==Store::Open::Unformatted);prepare(a,n);
        if(op>0)install(a,first,src);
        g.budget=cut;
        unsigned slot=0;
        if(op<2)a.put(second.data(),second.size(),reinterpret_cast<const uint8_t *>(src.data()),src.size(),never,slot);
        else {g.budget=cut?-1:0;a.remove(0);}
        g.restore();
        Store b;assert(b.open(g)==Store::Open::Ready);
        unsigned copies=0;for(unsigned i=0;i<Store::LOGICAL;++i)copies+=b.entry(i).used;
        assert(copies<=1);
        if(!copies){assert(op==0 || op==2);++outcomes[0];continue;}
        const auto got=file(b,0);
        assert(b.entry(0).state==Store::State::Ok);
        if(op==0)assert(got==second);
        if(op==1)assert(got==first || got==second);
        if(op==2)assert(got==first);
        ++outcomes[1];
        // Exactly one physical slot holds a valid header after the boot scan (a torn one is free).
        unsigned headed=0;
        for(unsigned p=0;p<Store::PHYSICAL;++p) {
          const uint8_t *h=g.bytes.data()+FlashLayout::EFFECTS_OFFSET+p*Store::SLOT_BYTES;
          headed+=FlashLayout::get32(h)==Store::MAGIC && FlashLayout::get32(h+28)==FlashLayout::crc32(h,28);
        }
        assert(headed==1);
      }
      std::printf("power cuts, op %d: %u boots without it, %u with it\n",op,outcomes[0],outcomes[1]);
      assert(op==1 || (outcomes[0] && outcomes[1]));
    }
  }
  std::puts("test_effect_store: ok");
}
