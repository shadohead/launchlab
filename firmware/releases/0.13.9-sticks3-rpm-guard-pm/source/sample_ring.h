#pragma once
#include <algorithm>
#include <stdint.h>

// Owned by the acquisition task. The UI may read samples only after a
// successful Suspend acknowledgement, while the writer is stopped.
class SampleRing {
public:
  SampleRing(uint16_t *storage, uint32_t capacity): storage_(storage), capacity_(capacity) {}
  void record(uint16_t sample) {
    if(!storage_ || !capacity_)return;
    storage_[next_]=sample;
    if(++next_==capacity_)next_=0;
    ++total_;
  }
  void discontinuity(){contiguousFrom_=total_;}
  uint64_t total() const{return total_;}
  uint32_t count() const{return uint32_t(std::min<uint64_t>(total_-contiguousFrom_,capacity_));}
  uint64_t first() const{return total_-count();}
private:
  uint16_t *storage_;
  uint32_t capacity_,next_=0;
  uint64_t total_=0,contiguousFrom_=0;
};
