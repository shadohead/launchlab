#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The history log in the spiffs partition (docs/FIRMWARE_FLASH_LAYOUT.md).
// A save appends a checksummed record to a sector erased ahead of time, so it
// only programs flash (a few ms per 4 KB), never erases on the save path and
// never touches NVS, whose page garbage collection made a full store's
// history rewrite take 460-480 ms. Every flash operation here is one sector
// erase (14-30 ms) or one program of at most a sector: the ADC's DMA ring
// (about 100 ms) rides each one, so saves need not stop sampling.
//
// Flash is any type with
//   bool read(uint32_t offset,void *out,size_t n)
//   bool write(uint32_t offset,const void *data,size_t n)  // NOR: 1 bits to 0
//   bool erase(uint32_t offset,size_t n)                   // whole sectors
// over the partition, offsets relative to its start.
namespace FlashLayout {
constexpr uint32_t SECTOR=4096,PARTITION_BYTES=0x180000;
// Superblock pair, then regions; a later firmware only adds a region where the
// table leaves space free and never moves one without a layout version bump.
constexpr uint32_t SUPER_OFFSET[2]={0x000000,0x001000};
constexpr uint32_t HISTORY_OFFSET=0x002000,HISTORY_BYTES=0x020000;   // 32 sectors (0.18)
constexpr uint32_t PULLS_OFFSET=0x022000,PULLS_BYTES=0x0CE000;       // free: full pull history, later
constexpr uint32_t EFFECTS_OFFSET=0x0F0000,EFFECTS_BYTES=0x090000;   // free: loaded effects, later
enum Kind:uint16_t {History=1,Effects=2,Pulls=3};
constexpr uint16_t LAYOUT_VERSION=1;
constexpr unsigned MAX_REGIONS=8;
static_assert(HISTORY_OFFSET+HISTORY_BYTES==PULLS_OFFSET && PULLS_OFFSET+PULLS_BYTES==EFFECTS_OFFSET &&
              EFFECTS_OFFSET+EFFECTS_BYTES==PARTITION_BYTES,"regions tile the partition");

inline uint32_t crc32(const void *data,size_t n,uint32_t crc=0) {
  const auto *p=static_cast<const uint8_t *>(data);crc=~crc;
  while(n--){crc^=*p++;for(unsigned k=0;k<8;++k)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1)));}
  return ~crc;
}
inline void put16(uint8_t *p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
inline uint16_t get16(const uint8_t *p){return uint16_t(p[0]|p[1]<<8);}
inline uint32_t get32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}

// One copy: "LLFS", layout version, region count, sequence, up to eight
// regions {kind, flags, offset, size}, CRC-32 over everything before it.
struct Superblock {
  static constexpr uint32_t MAGIC=0x5346464c;  // "LLFS"
  static constexpr unsigned BYTES=12+MAX_REGIONS*12+4;
  struct Region {uint16_t kind=0,flags=0;uint32_t offset=0,size=0;};
  uint16_t version=LAYOUT_VERSION,count=0;uint32_t sequence=0;Region regions[MAX_REGIONS]={};
  using Image=std::array<uint8_t,BYTES>;
  void encode(Image &b)const {
    b.fill(0xff);put32(b.data(),MAGIC);put16(b.data()+4,version);put16(b.data()+6,count);put32(b.data()+8,sequence);
    for(unsigned i=0;i<MAX_REGIONS;++i) {
      uint8_t *r=b.data()+12+12*i;
      if(i<count){put16(r,regions[i].kind);put16(r+2,regions[i].flags);put32(r+4,regions[i].offset);put32(r+8,regions[i].size);}
    }
    put32(b.data()+BYTES-4,crc32(b.data(),BYTES-4));
  }
  bool decode(const uint8_t *b) {
    if(get32(b)!=MAGIC || get32(b+BYTES-4)!=crc32(b,BYTES-4))return false;
    Superblock s;s.version=get16(b+4);s.count=get16(b+6);s.sequence=get32(b+8);
    if(!s.version || s.count>MAX_REGIONS)return false;
    for(unsigned i=0;i<s.count;++i) {
      const uint8_t *r=b+12+12*i;s.regions[i]={get16(r),get16(r+2),get32(r+4),get32(r+8)};
      const auto &g=s.regions[i];
      if(g.offset%SECTOR || g.size%SECTOR || g.offset<HISTORY_OFFSET || g.offset+g.size>PARTITION_BYTES || g.offset+g.size<g.offset)return false;
    }
    *this=s;return true;
  }
  const Region *find(uint16_t kind)const{for(unsigned i=0;i<count;++i)if(regions[i].kind==kind)return &regions[i];return nullptr;}
};

// The 0.18 table: the history log only; the rest reads as free.
inline Superblock historyLayout(uint32_t sequence) {
  Superblock s;s.sequence=sequence;s.count=1;s.regions[0]={History,0,HISTORY_OFFSET,HISTORY_BYTES};return s;
}
}

// Typed records in the history region's sectors. A sector starts with a
// 32-byte header: "LLHL", its erase count and a CRC of both, written right
// after its erase; then, when the log moves into it, its sequence and the
// sequence's complement. Records follow on 16-byte boundaries: a 16-byte
// header {type, state, length, CRC-32 of type, length and payload} and the
// payload. A record is written with state 0xFFFF and counts only once a
// second write clears state to 0 (NOR flash can only clear bits), so a record
// cut short by a power loss never counts and the log moves to a new sector.
// The newest committed record of a type is the one in the highest sequence
// sector at the highest offset.
template<class Flash> class FlashLog {
public:
  static constexpr uint32_t SECTOR=FlashLayout::SECTOR,HEADER=32,RECORD_HEADER=16;
  static constexpr uint32_t MAGIC=0x4c48484c;  // "LLHL"
  static constexpr uint32_t ERASED=0xffffffffu;
  static constexpr unsigned MAX_SECTORS=32,TYPES=4,READY_AHEAD=3;
  static constexpr uint32_t MAX_PAYLOAD=SECTOR-HEADER-RECORD_HEADER;
  struct Located {bool found=false;uint32_t offset=0,length=0,sequence=0;};

  enum class Open:uint8_t {Ready,Formatted,Unsupported,Failed};
  // Reads the layout; formats the partition when neither superblock is valid
  // (blank, erased, or another firmware's leftovers): erases both superblock
  // sectors and the history sectors only, then writes superblock 0. A valid
  // superblock of a newer layout, or one without a history region, is left
  // alone and the log stays closed (Unsupported).
  Open open(Flash &flash) {
    flash_=&flash;open_=false;
    FlashLayout::Superblock best;bool found=false;unsigned bestCopy=0;
    for(unsigned i=0;i<2;++i) {
      typename FlashLayout::Superblock::Image b;FlashLayout::Superblock s;
      if(!flash.read(FlashLayout::SUPER_OFFSET[i],b.data(),b.size()) || !s.decode(b.data()))continue;
      if(!found || int32_t(s.sequence-best.sequence)>0){best=s;found=true;bestCopy=i;}
    }
    (void)bestCopy;
    Open result=Open::Ready;
    if(!found) {
      for(uint32_t at:FlashLayout::SUPER_OFFSET)if(!flash.erase(at,SECTOR))return Open::Failed;
      if(!flash.erase(FlashLayout::HISTORY_OFFSET,FlashLayout::HISTORY_BYTES))return Open::Failed;
      for(uint32_t at=FlashLayout::HISTORY_OFFSET;at<FlashLayout::HISTORY_OFFSET+FlashLayout::HISTORY_BYTES;at+=SECTOR)
        if(!writeHeader(flash,at,1))return Open::Failed;
      best=FlashLayout::historyLayout(1);
      typename FlashLayout::Superblock::Image b;best.encode(b);
      if(!flash.write(FlashLayout::SUPER_OFFSET[0],b.data(),b.size()))return Open::Failed;
      result=Open::Formatted;
    }
    const auto *region=best.find(FlashLayout::History);
    if(best.version>FlashLayout::LAYOUT_VERSION || !region || region->size<SECTOR*(READY_AHEAD+2))return Open::Unsupported;
    base_=region->offset;sectors_=std::min<unsigned>(MAX_SECTORS,region->size/SECTOR);
    if(!scan())return Open::Failed;
    open_=true;return result;
  }
  bool isOpen()const{return open_;}
  // The newest committed record of `type`, if any.
  Located newest(uint16_t type)const{return type<TYPES?newest_[type]:Located{};}
  bool readPayload(const Located &r,void *out,uint32_t n)const {
    return r.found && n<=r.length && flash_->read(r.offset+RECORD_HEADER,out,n);
  }
  // Appends a committed record. A full sector moves the log to the next ready
  // sector (erasing one first only when none is ready, which readyAhead()
  // avoids). False leaves the previous newest record of the type in place.
  bool append(uint16_t type,const void *payload,uint32_t length) {
    if(!open_ || !type || type>=TYPES || length>MAX_PAYLOAD)return false;
    const uint32_t need=RECORD_HEADER+align(length);
    if(active_<0 || tail_+need>SECTOR) {if(!advance())return false;}
    const uint32_t at=sectorOffset(unsigned(active_))+tail_;
    uint8_t h[RECORD_HEADER];std::memset(h,0xff,sizeof(h));
    FlashLayout::put16(h,type);FlashLayout::put32(h+4,length);
    uint32_t crc=FlashLayout::crc32(h,2);crc=FlashLayout::crc32(h+4,4,crc);crc=FlashLayout::crc32(payload,length,crc);
    FlashLayout::put32(h+8,crc);
    // The header first (state still erased), then the payload, then commit.
    tail_+=need;  // whatever happens next, this space is spent
    if(!flash_->write(at,h,sizeof(h)) || !flash_->write(at+RECORD_HEADER,payload,length))return false;
    const uint8_t committed[2]={0,0};
    if(!flash_->write(at+2,committed,2))return false;
    // Read the record back as boot would before calling it saved.
    Located r;if(!readRecord(at,r) || r.length!=length)return false;
    r.sequence=sequence_[active_];newest_[type]=r;return true;
  }
  // At idle: erase one sector so READY_AHEAD stay ready. A sector that holds
  // the newest record of a type is never erased (at most TYPES-1 are), so a
  // rarely written type keeps its record while the rotation passes it.
  // Returns whether it erased one.
  bool readyAhead() {
    if(!open_ || readyCount()>=READY_AHEAD)return false;
    const int victim=reclaimable();
    return victim>=0 && eraseSector(unsigned(victim));
  }
  unsigned readyCount()const{unsigned n=0;for(unsigned i=0;i<sectors_;++i)n+=state_[i]==SectorState::Ready;return n;}
  uint32_t eraseCount(unsigned sector)const{return sector<sectors_?erases_[sector]:0;}
  unsigned sectors()const{return sectors_;}
private:
  enum class SectorState:uint8_t {Ready,Used,Dirty};
  static uint32_t align(uint32_t n){return (n+15)&~15u;}
  uint32_t sectorOffset(unsigned i)const{return base_+i*SECTOR;}
  unsigned sectorOf(uint32_t offset)const{return (offset-base_)/SECTOR;}
  bool readRecord(uint32_t at,Located &out)const {
    uint8_t h[RECORD_HEADER];
    if(!flash_->read(at,h,sizeof(h)) || FlashLayout::get16(h+2)!=0)return false;
    const uint16_t type=FlashLayout::get16(h);const uint32_t length=FlashLayout::get32(h+4);
    if(!type || type>=TYPES || length>MAX_PAYLOAD)return false;
    uint32_t crc=FlashLayout::crc32(h,2);crc=FlashLayout::crc32(h+4,4,crc);
    uint8_t chunk[256];
    for(uint32_t done=0;done<length;) {
      const uint32_t n=std::min<uint32_t>(sizeof(chunk),length-done);
      if(!flash_->read(at+RECORD_HEADER+done,chunk,n))return false;
      crc=FlashLayout::crc32(chunk,n,crc);done+=n;
    }
    if(crc!=FlashLayout::get32(h+8))return false;
    out={true,at,length,0};return true;
  }
  bool scan() {
    active_=-1;uint32_t best=0;
    for(auto &n:newest_)n={};
    for(unsigned i=0;i<sectors_;++i) {
      uint8_t h[HEADER];state_[i]=SectorState::Dirty;erases_[i]=0;sequence_[i]=ERASED;
      if(!flash_->read(sectorOffset(i),h,sizeof(h)))return false;
      const bool headed=FlashLayout::get32(h)==MAGIC && FlashLayout::get32(h+8)==FlashLayout::crc32(h,8);
      if(!headed)continue;  // erased without a header, torn or foreign: erase before use
      erases_[i]=FlashLayout::get32(h+4);
      const uint32_t seq=FlashLayout::get32(h+12),cmp=FlashLayout::get32(h+16);
      bool rest=true;for(unsigned k=12;k<HEADER;++k)rest=rest && h[k]==0xff;
      if(rest){state_[i]=SectorState::Ready;continue;}
      if(seq==ERASED || cmp!=~seq)continue;  // a torn open: erase before use
      state_[i]=SectorState::Used;sequence_[i]=seq;
      if(active_<0 || int32_t(seq-best)>0){best=seq;active_=int(i);}
    }
    // Records, oldest sector first, so a later record of a type wins.
    unsigned order[MAX_SECTORS];unsigned used=0;
    for(unsigned i=0;i<sectors_;++i)if(state_[i]==SectorState::Used)order[used++]=i;
    std::sort(order,order+used,[&](unsigned a,unsigned b){return int32_t(sequence_[a]-sequence_[b])<0;});
    for(unsigned k=0;k<used;++k) {
      const unsigned i=order[k];uint32_t at=HEADER;bool clean=true;
      while(at+RECORD_HEADER<=SECTOR) {
        uint8_t h[RECORD_HEADER];
        if(!flash_->read(sectorOffset(i)+at,h,sizeof(h)))return false;
        bool blank=true;for(uint8_t v:h)blank=blank && v==0xff;
        if(blank)break;
        const uint32_t length=FlashLayout::get32(h+4);
        Located r;
        if(length>MAX_PAYLOAD || at+RECORD_HEADER+align(length)>SECTOR){clean=false;break;}
        if(readRecord(sectorOffset(i)+at,r)){r.sequence=sequence_[i];newest_[FlashLayout::get16(h)]=r;}
        else clean=false;  // torn or uncommitted: the log continues in a new sector
        at+=RECORD_HEADER+align(length);
      }
      if(int(i)==active_)tail_=clean?at:SECTOR;
    }
    return true;
  }
  static bool writeHeader(Flash &flash,uint32_t at,uint32_t erases) {
    uint8_t h[12];FlashLayout::put32(h,MAGIC);FlashLayout::put32(h+4,erases);FlashLayout::put32(h+8,FlashLayout::crc32(h,8));
    return flash.write(at,h,sizeof(h));
  }
  bool eraseSector(unsigned i) {
    // A sector without a readable header lost its count: continue from the highest one seen.
    uint32_t count=erases_[i];
    if(state_[i]==SectorState::Dirty && !count)for(unsigned k=0;k<sectors_;++k)count=std::max(count,erases_[k]);
    ++count;
    state_[i]=SectorState::Dirty;sequence_[i]=ERASED;
    if(!flash_->erase(sectorOffset(i),SECTOR) || !writeHeader(*flash_,sectorOffset(i),count))return false;
    erases_[i]=count;state_[i]=SectorState::Ready;return true;
  }
  bool holdsNewest(unsigned i)const {
    for(uint16_t t=1;t<TYPES;++t)if(newest_[t].found && sectorOf(newest_[t].offset)==i)return true;
    return false;
  }
  // A torn or unheaded sector first, else the oldest used one that is not
  // active and holds no newest record.
  int reclaimable()const {
    int pick=-1;
    for(unsigned i=0;i<sectors_;++i) {
      if(state_[i]==SectorState::Dirty)return int(i);
      if(state_[i]==SectorState::Used && int(i)!=active_ && !holdsNewest(i) &&
         (pick<0 || int32_t(sequence_[i]-sequence_[unsigned(pick)])<0))pick=int(i);
    }
    return pick;
  }
  // The next ready sector after the active one, in rotation (even wear).
  bool advance() {
    int next=-1;
    for(unsigned k=1;k<=sectors_;++k) {
      const unsigned i=(unsigned(active_<0?sectors_-1:active_)+k)%sectors_;
      if(state_[i]==SectorState::Ready){next=int(i);break;}
    }
    if(next<0) {
      // None ready (readyAhead() at idle normally keeps three): erase one now.
      const int victim=reclaimable();
      if(victim<0 || !eraseSector(unsigned(victim)))return false;
      next=victim;
    }
    uint32_t seq=1;
    if(active_>=0)seq=sequence_[unsigned(active_)]+1;
    else for(unsigned i=0;i<sectors_;++i)if(state_[i]==SectorState::Used && int32_t(sequence_[i]+1-seq)>0)seq=sequence_[i]+1;
    uint8_t s[8];FlashLayout::put32(s,seq);FlashLayout::put32(s+4,~seq);
    if(!flash_->write(sectorOffset(unsigned(next))+12,s,sizeof(s)))return false;
    sequence_[next]=seq;state_[next]=SectorState::Used;active_=next;tail_=HEADER;return true;
  }
  Flash *flash_=nullptr;bool open_=false;
  uint32_t base_=0;unsigned sectors_=0;
  SectorState state_[MAX_SECTORS]={};uint32_t erases_[MAX_SECTORS]={},sequence_[MAX_SECTORS]={};
  int active_=-1;uint32_t tail_=0;
  Located newest_[TYPES]={};
};
