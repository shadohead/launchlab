#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../../LaunchLabMini/flash_log.h"

// The spiffs partition as NOR flash: program only clears bits, erase sets
// whole sectors to 0xFF, and a power loss can stop a write after any byte.
struct NorFlash {
  std::vector<uint8_t> bytes=std::vector<uint8_t>(FlashLayout::PARTITION_BYTES,0xff);
  long budget=-1;  // bytes left to program before a power loss; -1 never
  bool dead=false;unsigned erases=0,writes=0;
  bool begin(){return true;}
  bool read(uint32_t at,void *out,size_t n){if(at+n>bytes.size())return false;std::memcpy(out,bytes.data()+at,n);return true;}
  bool write(uint32_t at,const void *data,size_t n) {
    if(dead || at+n>bytes.size())return false;
    ++writes;const auto *p=static_cast<const uint8_t *>(data);
    for(size_t i=0;i<n;++i) {
      if(budget==0){dead=true;return false;}
      if(budget>0)--budget;
      bytes[at+i]&=p[i];
    }
    return true;
  }
  bool erase(uint32_t at,size_t n) {
    if(dead || at%FlashLayout::SECTOR || n%FlashLayout::SECTOR || at+n>bytes.size())return false;
    if(budget==0){dead=true;return false;}
    for(size_t s=at;s<at+n;s+=FlashLayout::SECTOR){++erases;std::fill(bytes.begin()+s,bytes.begin()+s+FlashLayout::SECTOR,0xff);}
    return true;
  }
  // Power comes back: writes work again.
  void restore(){budget=-1;dead=false;}
};
