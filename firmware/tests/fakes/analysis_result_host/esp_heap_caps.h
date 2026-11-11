#pragma once
#include <cstdlib>
#include <cassert>
inline unsigned allocations=0;inline bool failAllocation=false;inline unsigned failOnAllocation=0;
constexpr unsigned MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_INTERNAL=4;
inline void *heap_caps_malloc(size_t n,unsigned){if(failAllocation || (failOnAllocation && --failOnAllocation==0))return nullptr;auto p=std::malloc(n);if(p)++allocations;return p;}
inline void heap_caps_free(void *p){if(p){assert(allocations);--allocations;std::free(p);}}
