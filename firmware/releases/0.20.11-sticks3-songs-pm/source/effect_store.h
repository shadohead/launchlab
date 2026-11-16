#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "flash_log.h"
#include "fx_effect.h"

// The loaded-effect store in the spiffs partition (docs/FIRMWARE_FLASH_LAYOUT.md,
// kind 2; docs/FIRMWARE_LOADABLE_EFFECTS.md, "Storage"). 36 physical slots of
// 16 KB hold up to 32 effects, one per logical slot (effect IDs 96-127), so a
// replacement always has a free slot to go to and four stay free.
//
// A slot is a 32-byte header and a payload: the compiled LLFX file, then the
// effect's packed source (for share codes; may be empty). An install erases a
// free slot, writes the payload, reads it back, and writes the header last,
// so a power loss at any point leaves either the old effect or the new one:
// a slot without a valid header is free. The old copy of a replaced effect
// is erased only after the new header is in place; if power fails between
// the two, the boot scan keeps the higher sequence and erases the other.
// Delete clears the header's state byte (a tombstone): the effect is gone,
// but the slot keeps its uid and fallback until it is reused, so a record
// that still names it plays the designer's stand-in. Nothing here touches NVS.
//
// Header, little-endian: "LLFE", version (1), logical slot, the fallback
// effect, state (0xFF live; anything else deleted, outside the CRC so a
// delete is one byte programmed), sequence (32), file bytes (16), source
// bytes (16), uid (64), CRC-32 of the payload, CRC-32 of the header's
// bytes 0-27 with the state byte read as 0xFF.
//
// Flash is FlashLog's: read, write (NOR: clears bits), erase (whole sectors),
// offsets relative to the partition.
template<class Flash> class EffectStore {
public:
  static constexpr uint32_t SECTOR=FlashLayout::SECTOR,SLOT_BYTES=4*SECTOR,HEADER=32,MAX_PAYLOAD=SLOT_BYTES-HEADER;
  static constexpr unsigned PHYSICAL=FlashLayout::EFFECTS_BYTES/SLOT_BYTES,LOGICAL=32,FIRST_ID=96;
  static constexpr uint32_t MAGIC=0x45464c4c;  // "LLFE"
  static_assert(PHYSICAL==36,"36 slots of 16 KB");

  enum class Open:uint8_t {Ready,Unformatted,Unsupported,Failed};
  enum class Result:uint8_t {Ok,Invalid,Engine,TooLarge,Full,NotFound,Unavailable,WriteFailed};
  enum class State:uint8_t {Ok,Damaged};
  struct Entry {
    bool used=false,deleted=false;State state=State::Ok;uint8_t physical=0,logical=0,fallback=LoadableFx::Program::NO_FALLBACK;uint32_t sequence=0;
    uint16_t fileBytes=0,sourceBytes=0;uint64_t uid=0;uint32_t crc=0;
  };

  // Reads the superblock and, when the effect region is in it, scans every
  // slot. No valid superblock, or a valid version-1 table without the region:
  // Unformatted (prepare() adds it). A newer layout, or an effect region
  // other than this one: Unsupported, and the partition is left alone.
  Open open(Flash &flash) {
    flash_=&flash;open_=false;prepared_=0;
    for(auto &e:logical_)e=Entry();
    for(auto &e:gone_)e=Entry();
    for(auto &u:slotUsed_)u=false;
    FlashLayout::Superblock sb;
    if(!readSuperblock(sb,copy_)){table_=FlashLayout::Superblock();haveTable_=false;return Open::Unformatted;}
    table_=sb;haveTable_=true;
    if(sb.version>FlashLayout::LAYOUT_VERSION)return Open::Unsupported;
    const auto *r=sb.find(FlashLayout::Effects);
    if(!r)return Open::Unformatted;
    if(r->offset!=FlashLayout::EFFECTS_OFFSET || r->size!=FlashLayout::EFFECTS_BYTES)return Open::Unsupported;
    if(!scan())return Open::Failed;
    open_=true;return Open::Ready;
  }
  bool isOpen()const{return open_;}

  // Adds the effect region, a sector per call so the caller can keep its
  // loop running (144 erases, 14-30 ms each): true when done. The region's
  // sectors are erased first, then the other superblock copy is written with
  // every existing entry unchanged and the region added. With no valid
  // superblock at all, both superblock sectors are erased and copy 0 lists
  // only this region (docs/FIRMWARE_FLASH_LAYOUT.md's rules). A power loss
  // part way only means erasing again.
  bool prepareStep(bool &failed) {
    failed=false;
    if(open_)return true;
    if(haveTable_ && (table_.version>FlashLayout::LAYOUT_VERSION || table_.find(FlashLayout::Effects))){failed=true;return false;}
    if(prepared_<PHYSICAL*4) {
      if(!flash_->erase(FlashLayout::EFFECTS_OFFSET+prepared_*SECTOR,SECTOR)){failed=true;return false;}
      ++prepared_;return false;
    }
    FlashLayout::Superblock sb;unsigned target=0;
    if(haveTable_) {
      sb=table_;
      if(sb.count>=FlashLayout::MAX_REGIONS){failed=true;return false;}
      sb.regions[sb.count++]={FlashLayout::Effects,0,FlashLayout::EFFECTS_OFFSET,FlashLayout::EFFECTS_BYTES};
      ++sb.sequence;target=1-copy_;
      if(!flash_->erase(FlashLayout::SUPER_OFFSET[target],SECTOR)){failed=true;return false;}
    } else {
      for(uint32_t at:FlashLayout::SUPER_OFFSET)if(!flash_->erase(at,SECTOR)){failed=true;return false;}
      sb.sequence=1;sb.count=1;sb.regions[0]={FlashLayout::Effects,0,FlashLayout::EFFECTS_OFFSET,FlashLayout::EFFECTS_BYTES};
    }
    typename FlashLayout::Superblock::Image b;sb.encode(b);
    if(!flash_->write(FlashLayout::SUPER_OFFSET[target],b.data(),b.size())){failed=true;return false;}
    if(open(*flash_)!=Open::Ready){failed=true;return false;}
    return true;
  }
  unsigned prepareProgress()const{return prepared_;}  // of PHYSICAL*4 sectors

  unsigned installed()const{unsigned n=0;for(const auto &e:logical_)n+=e.used;return n;}
  unsigned freeSlots()const{unsigned n=0;for(bool u:slotUsed_)n+=!u;return n;}
  const Entry &entry(unsigned slot)const{static const Entry none;return slot<LOGICAL?logical_[slot]:none;}
  // A slot's fallback: its effect's, or a deleted effect's until the slot is reused.
  uint8_t fallback(unsigned slot)const{return slot<LOGICAL?(logical_[slot].used?logical_[slot].fallback:gone_[slot].fallback):LoadableFx::Program::NO_FALLBACK;}
  int slotOf(uint64_t uid)const{for(unsigned i=0;i<LOGICAL;++i)if(logical_[i].used && logical_[i].uid==uid)return int(i);return -1;}

  // Installs a file and its packed source. The file must load at this
  // device's engine level. The same uid replaces itself in its slot; a new
  // uid takes the lowest free slot that `referenced(slot)` says nothing
  // points at (a milestone pool, the Best effect, the effect set), so an old
  // reference never picks up a different effect. `slot` is set on success.
  template<class Referenced> Result put(const uint8_t *file,size_t fileBytes,const uint8_t *source,size_t sourceBytes,Referenced referenced,unsigned &slot) {
    return write(file,fileBytes,source,sourceBytes,referenced,-1,slot);
  }
  // A restore: the file into the slot the backup had it in (records name it
  // by that slot's ID), replacing whatever is there.
  Result putAt(unsigned at,const uint8_t *file,size_t fileBytes,const uint8_t *source,size_t sourceBytes) {
    if(at>=LOGICAL)return Result::NotFound;
    unsigned slot=0;
    return write(file,fileBytes,source,sourceBytes,[](unsigned){return false;},int(at),slot);
  }
private:
  template<class Referenced> Result write(const uint8_t *file,size_t fileBytes,const uint8_t *source,size_t sourceBytes,Referenced referenced,int forced,unsigned &slot) {
    if(!open_)return Result::Unavailable;
    if(fileBytes+sourceBytes>MAX_PAYLOAD || fileBytes>0xffff || sourceBytes>0xffff)return Result::TooLarge;
    LoadableFx::Program p;const LoadableFx::Error e=LoadableFx::load(file,fileBytes,p,false);
    if(e==LoadableFx::Error::Engine)return Result::Engine;
    if(e!=LoadableFx::Error::None)return Result::Invalid;
    int target=forced>=0?forced:slotOf(p.uid);
    // The same uid in another slot (a restore placing it elsewhere) goes: one copy per uid.
    if(forced>=0){const int other=slotOf(p.uid);if(other>=0 && other!=forced && remove(unsigned(other))!=Result::Ok)return Result::WriteFailed;}
    if(target<0)for(unsigned i=0;i<LOGICAL;++i)if(!logical_[i].used && !referenced(i)){target=int(i);break;}
    if(target<0)return Result::Full;
    // A free slot in rotation, one without a tombstone first: a tombstone's
    // slot is reused only when nothing else is free.
    int physical=-1;
    for(int pass=0;pass<2 && physical<0;++pass)
      for(unsigned k=0;k<PHYSICAL;++k) {
        const unsigned i=(next_+k)%PHYSICAL;
        if(!slotUsed_[i] && (pass || !holdsTombstone(i))){physical=int(i);break;}
      }
    if(physical<0)return Result::Full;  // cannot happen with 32 logical in 36 physical
    next_=(unsigned(physical)+1)%PHYSICAL;
    const uint32_t at=slotOffset(unsigned(physical));
    uint32_t crc=FlashLayout::crc32(file,fileBytes);crc=FlashLayout::crc32(source,sourceBytes,crc);
    const uint32_t sequence=highest_+1;
    // Erase, payload, read back, header last.
    if(!flash_->erase(at,SLOT_BYTES))return Result::WriteFailed;
    if(!flash_->write(at+HEADER,file,fileBytes) || (sourceBytes && !flash_->write(at+HEADER+fileBytes,source,sourceBytes)))return Result::WriteFailed;
    if(payloadCrc(at,uint32_t(fileBytes)+uint32_t(sourceBytes))!=crc)return Result::WriteFailed;
    uint8_t h[HEADER];encodeHeader(h,uint8_t(target),p.fallback,sequence,uint16_t(fileBytes),uint16_t(sourceBytes),p.uid,crc);
    if(!flash_->write(at,h,HEADER))return Result::WriteFailed;
    Entry fresh;
    if(!readHeader(unsigned(physical),fresh) || fresh.sequence!=sequence)return Result::WriteFailed;
    slotUsed_[physical]=true;highest_=sequence;
    // The replaced copy goes only now that the new one is whole.
    const Entry old=logical_[target];
    logical_[target]=fresh;gone_[target]=Entry();slot=unsigned(target);
    if(old.used){slotUsed_[old.physical]=false;flash_->erase(slotOffset(old.physical),SECTOR);}
    return Result::Ok;
  }
public:
  // Tombstones the slot (one byte): the effect is gone, its uid and fallback
  // kept for the records that still name it; the caller keeps those records.
  Result remove(unsigned slot) {
    if(!open_)return Result::Unavailable;
    if(slot>=LOGICAL || !logical_[slot].used)return Result::NotFound;
    const Entry e=logical_[slot];const uint8_t gone=0;
    if(!flash_->write(slotOffset(e.physical)+7,&gone,1))return Result::WriteFailed;
    logical_[slot]=Entry();slotUsed_[e.physical]=false;
    gone_[slot]=e;gone_[slot].used=false;gone_[slot].deleted=true;
    return Result::Ok;
  }
  Result clear() {
    if(!open_)return Result::Unavailable;
    for(unsigned i=0;i<LOGICAL;++i)if(logical_[i].used && remove(i)!=Result::Ok)return Result::WriteFailed;
    return Result::Ok;
  }
  // The stored file (or packed source) of a slot into `out`; false if
  // absent, damaged or larger than `capacity`.
  bool readFile(unsigned slot,uint8_t *out,size_t capacity,size_t &bytes)const{return readPart(slot,true,out,capacity,bytes);}
  bool readSource(unsigned slot,uint8_t *out,size_t capacity,size_t &bytes)const{return readPart(slot,false,out,capacity,bytes);}

private:
  static uint32_t slotOffset(unsigned physical){return FlashLayout::EFFECTS_OFFSET+physical*SLOT_BYTES;}
  bool holdsTombstone(unsigned physical)const{for(const auto &g:gone_)if(g.deleted && g.physical==physical)return true;return false;}
  bool readSuperblock(FlashLayout::Superblock &best,unsigned &copy) {
    bool found=false;
    for(unsigned i=0;i<2;++i) {
      typename FlashLayout::Superblock::Image b;FlashLayout::Superblock s;
      if(!flash_->read(FlashLayout::SUPER_OFFSET[i],b.data(),b.size()) || !s.decode(b.data()))continue;
      if(!found || int32_t(s.sequence-best.sequence)>0){best=s;copy=i;found=true;}
    }
    return found;
  }
  static void encodeHeader(uint8_t *h,uint8_t logical,uint8_t fallback,uint32_t sequence,uint16_t fileBytes,uint16_t sourceBytes,uint64_t uid,uint32_t crc) {
    std::memset(h,0xff,HEADER);
    FlashLayout::put32(h,MAGIC);h[4]=1;h[5]=logical;h[6]=fallback;
    FlashLayout::put32(h+8,sequence);FlashLayout::put16(h+12,fileBytes);FlashLayout::put16(h+14,sourceBytes);
    FlashLayout::put32(h+16,uint32_t(uid));FlashLayout::put32(h+20,uint32_t(uid>>32));FlashLayout::put32(h+24,crc);
    FlashLayout::put32(h+28,FlashLayout::crc32(h,28));
  }
  bool readHeader(unsigned physical,Entry &e)const {
    uint8_t h[HEADER];
    if(!flash_->read(slotOffset(physical),h,HEADER))return false;
    const bool deleted=h[7]!=0xff;h[7]=0xff;
    if(FlashLayout::get32(h)!=MAGIC || h[4]!=1 || h[5]>=LOGICAL || FlashLayout::get32(h+28)!=FlashLayout::crc32(h,28))return false;
    e=Entry();e.used=!deleted;e.deleted=deleted;e.physical=uint8_t(physical);e.logical=h[5];e.fallback=h[6];e.sequence=FlashLayout::get32(h+8);
    e.fileBytes=FlashLayout::get16(h+12);e.sourceBytes=FlashLayout::get16(h+14);
    e.uid=uint64_t(FlashLayout::get32(h+16))|uint64_t(FlashLayout::get32(h+20))<<32;e.crc=FlashLayout::get32(h+24);
    return uint32_t(e.fileBytes)+e.sourceBytes<=MAX_PAYLOAD;
  }
  uint32_t payloadCrc(uint32_t at,uint32_t n)const {
    uint8_t chunk[256];uint32_t crc=0;
    for(uint32_t done=0;done<n;) {
      const uint32_t k=std::min<uint32_t>(sizeof(chunk),n-done);
      if(!flash_->read(at+HEADER+done,chunk,k))return ~crc;  // differs from any CRC computed so far
      crc=FlashLayout::crc32(chunk,k,crc);done+=k;
    }
    return crc;
  }
  // Every slot: a valid header claims its logical slot (the higher sequence
  // wins; the other copy is erased now, finishing a replace that power cut
  // short), a payload whose CRC fails is reported damaged, a tombstone keeps
  // its slot's fallback (the newest wins) and is free, as is anything else
  // (each free slot is erased before it is written).
  bool scan() {
    highest_=0;
    for(unsigned i=0;i<PHYSICAL;++i) {
      Entry e;if(!readHeader(i,e))continue;
      const unsigned slot=e.logical;
      if(int32_t(e.sequence-highest_)>0)highest_=e.sequence;
      if(e.deleted) {
        if(!gone_[slot].deleted || int32_t(e.sequence-gone_[slot].sequence)>0)gone_[slot]=e;
        continue;
      }
      if(payloadCrc(slotOffset(i),uint32_t(e.fileBytes)+e.sourceBytes)!=e.crc)e.state=State::Damaged;
      Entry &have=logical_[slot];
      if(have.used) {
        const bool newer=int32_t(e.sequence-have.sequence)>0;
        const Entry &loser=newer?have:e;
        flash_->erase(slotOffset(loser.physical),SECTOR);
        if(!newer)continue;
        slotUsed_[have.physical]=false;
      }
      have=e;slotUsed_[i]=true;
    }
    return true;
  }
  bool readPart(unsigned slot,bool file,uint8_t *out,size_t capacity,size_t &bytes)const {
    bytes=0;
    if(!open_ || slot>=LOGICAL || !logical_[slot].used || logical_[slot].state!=State::Ok)return false;
    const Entry &e=logical_[slot];const size_t n=file?e.fileBytes:e.sourceBytes;
    if(n>capacity)return false;
    if(!flash_->read(slotOffset(e.physical)+HEADER+(file?0:e.fileBytes),out,n))return false;
    bytes=n;return true;
  }
  Flash *flash_=nullptr;bool open_=false,haveTable_=false;
  FlashLayout::Superblock table_;unsigned copy_=0,prepared_=0;
  Entry logical_[LOGICAL],gone_[LOGICAL];bool slotUsed_[PHYSICAL]={};
  unsigned next_=0;uint32_t highest_=0;
};
