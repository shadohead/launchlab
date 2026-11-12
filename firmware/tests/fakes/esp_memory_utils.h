#pragma once
// Host stand-in: external RAM is whatever the fake heap served from PSRAM.
#include "esp_heap_caps.h"

inline bool esp_ptr_external_ram(const void *pointer) {
  for(const auto &block:FakeHeap::blocks.list)if(block.memory==pointer)return block.external;
  return false;
}
