#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Flash writes from a buffer outside internal RAM (the history log's PSRAM
// records, an added effect's file) go through an internal chunk. ESP-IDF
// writes such a buffer to the main flash 32 bytes at a time, each with the
// cache off (esp_flash_write: SPI1 writes directly only from DRAM), which
// made a 2.7 KB history save 30 ms instead of 13 (0.20 gate, 2026-10-23).
// From an internal chunk every write is direct again, and chunks end on
// 256-byte flash pages, so the programs are those of one direct write.
namespace FlashChunks {
constexpr size_t BYTES=256;
template<class Write> bool write(uint32_t at,const void *data,size_t n,bool internal,Write &&write) {
  if(internal)return write(at,data,n);
  uint8_t chunk[BYTES];
  const auto *p=static_cast<const uint8_t *>(data);
  while(n) {
    const size_t len=std::min<size_t>(n,BYTES-size_t(at%BYTES));
    std::memcpy(chunk,p,len);
    if(!write(at,static_cast<const void *>(chunk),len))return false;
    at+=uint32_t(len);p+=len;n-=len;
  }
  return true;
}
}
