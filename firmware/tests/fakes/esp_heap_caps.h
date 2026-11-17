#pragma once
// Host stand-in for ESP-IDF's capability allocator. There is no PSRAM unless a
// test turns it on, so the owner under test takes its internal-RAM fallback.
#include <cstddef>
#include <cstdlib>
#include <vector>

static constexpr unsigned MALLOC_CAP_8BIT=1u<<2,MALLOC_CAP_SPIRAM=1u<<10,MALLOC_CAP_INTERNAL=1u<<11;

namespace FakeHeap {
struct Block {void *memory;bool external;};
// Production never frees these: it allocates once for the life of the owner.
// The list frees them at exit, so a replaced owner is not reported as a leak.
struct Blocks {
  std::vector<Block> list;
  ~Blocks(){for(const auto &block:list)std::free(block.memory);}
};
inline Blocks blocks;
inline bool psram=false,internal=true;
inline unsigned allocations(){return unsigned(blocks.list.size());}
inline void reset(){psram=false;internal=true;}
}

inline void *heap_caps_malloc(size_t bytes,unsigned caps) {
  const bool external=caps&MALLOC_CAP_SPIRAM;
  if(external?!FakeHeap::psram:!FakeHeap::internal)return nullptr;
  void *memory=std::malloc(bytes);
  if(memory)FakeHeap::blocks.list.push_back({memory,external});
  return memory;
}
