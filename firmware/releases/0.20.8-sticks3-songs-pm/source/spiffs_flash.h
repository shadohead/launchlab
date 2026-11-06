#pragma once
#include "esp_memory_utils.h"
#include "esp_partition.h"
#include "flash_chunks.h"
#include "flash_log.h"

// The spiffs partition (0x670000, 1.5 MB in the 8 MB layout every LaunchLab
// unit has) as FlashLog's flash: esp_partition calls, partition-relative.
// Nothing else on the device or in the updater writes it.
struct SpiffsFlash {
  const esp_partition_t *partition=nullptr;
  bool begin() {
    partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,nullptr);
    return partition && partition->size>=FlashLayout::PARTITION_BYTES;
  }
  bool read(uint32_t at,void *out,size_t n){return partition && esp_partition_read(partition,at,out,n)==ESP_OK;}
#if LAUNCHLAB_HIL
  // HIL only (`!fxcut`): bytes left to program before a simulated power cut,
  // which writes what fits and restarts at once; -1 never.
  long cutAfter=-1;
  bool write(uint32_t at,const void *data,size_t n){
    if(cutAfter>=0) {
      if(long(n)>cutAfter){if(cutAfter)program(at,data,size_t(cutAfter));esp_restart();}
      cutAfter-=long(n);
    }
    return program(at,data,n);
  }
#else
  bool write(uint32_t at,const void *data,size_t n){return program(at,data,n);}
#endif
  bool erase(uint32_t at,size_t n){return partition && esp_partition_erase_range(partition,at,n)==ESP_OK;}
private:
  bool program(uint32_t at,const void *data,size_t n) {
    return partition && FlashChunks::write(at,data,n,esp_ptr_in_dram(data),
      [this](uint32_t a,const void *p,size_t len){return esp_partition_write(partition,a,p,len)==ESP_OK;});
  }
};
