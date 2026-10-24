#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "device_backup.h"

// USB `X`: one loaded-effect request (docs/FIRMWARE_LOADABLE_EFFECTS.md,
// "Transport and the web"), sent like a theme or milestone request: after
// FX_READY, base64 lines each acknowledged with FX_CHUNK, then a "." line.
// The text goes to a buffer the caller owns (PSRAM on the device: a 16 KB
// effect is about 22 KB of base64), so nothing here takes internal RAM.
namespace FxTransfer {
using Event=DeviceBackup::TransferEvent;

// Lines of base64 into the caller's buffer (DeviceBackup::BufferReceiver).
using Receiver=DeviceBackup::BufferReceiver;

// The request: "LLX\1", op, slot (delete, source), two zero bytes, the
// file's length (32), the packed source's length (32), the file, the source,
// and FNV-1a over everything before it. Put installs (replacing the same
// uid), Delete removes a slot (0-31), Clear removes every one, Source reads a
// slot's packed source back (for its share code).
struct Request {
  static constexpr uint32_t MAGIC=0x01584c4c;  // "LLX\1"
  static constexpr unsigned HEADER=16,TRAILER=4;
  enum class Op:uint8_t {Put=1,Delete=2,Clear=3,Source=4};
  Op op=Op::Put;uint8_t slot=0;
  const uint8_t *file=nullptr,*source=nullptr;uint32_t fileBytes=0,sourceBytes=0;
  static const char *name(Op op){return op==Op::Put?"put":op==Op::Delete?"delete":op==Op::Clear?"clear":op==Op::Source?"source":"unknown";}
  static uint32_t hash(const uint8_t *p,size_t n){uint32_t h=2166136261u;while(n--){h^=*p++;h*=16777619u;}return h;}
  static size_t bytes(uint32_t fileBytes,uint32_t sourceBytes){return HEADER+size_t(fileBytes)+sourceBytes+TRAILER;}
  // Encodes into out (bytes(...) long); the web builds the same frame.
  static size_t encode(Op op,uint8_t slot,const uint8_t *file,uint32_t fileBytes,const uint8_t *source,uint32_t sourceBytes,uint8_t *out) {
    std::memset(out,0,HEADER);
    DeviceBackup::put32(out,MAGIC);out[4]=uint8_t(op);out[5]=slot;
    DeviceBackup::put32(out+8,fileBytes);DeviceBackup::put32(out+12,sourceBytes);
    if(fileBytes)std::memcpy(out+HEADER,file,fileBytes);
    if(sourceBytes)std::memcpy(out+HEADER+fileBytes,source,sourceBytes);
    const size_t n=HEADER+size_t(fileBytes)+sourceBytes;
    DeviceBackup::put32(out+n,hash(out,n));
    return n+TRAILER;
  }
  // Shape and checksum: a known op, zero reserved bytes, lengths that add up
  // (a put carries a file; the others carry nothing), a delete's or a
  // source's slot in range. The file itself is the loader's to judge.
  bool decode(const uint8_t *b,size_t n) {
    if(n<HEADER+TRAILER || DeviceBackup::get32(b)!=MAGIC || b[6] || b[7])return false;
    if(DeviceBackup::get32(b+n-TRAILER)!=hash(b,n-TRAILER))return false;
    const uint32_t f=DeviceBackup::get32(b+8),s=DeviceBackup::get32(b+12);
    if(uint64_t(HEADER)+f+s+TRAILER!=n)return false;
    if(b[4]<1 || b[4]>4)return false;
    op=Op(b[4]);slot=b[5];
    if(op==Op::Put?!f:(f || s))return false;
    if(op==Op::Delete || op==Op::Source?slot>=32:slot!=0)return false;
    fileBytes=f;sourceBytes=s;file=f?b+HEADER:nullptr;source=s?b+HEADER+f:nullptr;
    return true;
  }
};
}
