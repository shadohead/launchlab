#pragma once

// The radio's calibration data stays out of NVS.
// The first time the radio starts, the SDK saves what it calibrated in the
// NVS namespace "phy": 67 entries of the 504 a unit has, kept for good, even
// once Bluetooth is off again. On a unit with a full history that is the room
// a custom theme or a milestone table needs (found on the rig, 2026-11-10: 81
// entries free, every theme and milestone save refused). The SDK also writes
// it from the radio's own task while the ADC and the IMU run.
// So the build refuses that namespace (the linker wraps nvs_open, see
// build-m5.sh): nothing is read or written, and the radio calibrates in full
// each time it starts, which Bluetooth On pays once a boot. What an earlier
// build or an unwrapped one (the Arduino IDE) left is removed at boot.
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RadioCalibration {
constexpr const char *NAMESPACE="phy";
inline bool refuses(const char *name){return name && !std::strcmp(name,NAMESPACE);}

// Removes the namespace's entries. Returns how many, 0 when there were none
// and -1 when they could not be removed. A unit that never had the namespace
// is not given one: it is opened for writing only once it holds something.
template<class Api> int reclaim() {
  typename Api::Handle h{};
  const auto opened=Api::open(NAMESPACE,false,&h);
  if(opened==Api::NotFound)return 0;
  if(opened!=Api::Ok)return -1;
  size_t used=0;
  const auto counted=Api::used(h,&used);
  Api::close(h);
  if(counted!=Api::Ok)return -1;
  if(!used)return 0;
  if(Api::open(NAMESPACE,true,&h)!=Api::Ok)return -1;
  const bool ok=Api::eraseAll(h)==Api::Ok && Api::commit(h)==Api::Ok;
  Api::close(h);
  return ok?int(used):-1;
}
}

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#include <nvs.h>
#ifndef LAUNCHLAB_RADIO_CAL_WRAP
#define LAUNCHLAB_RADIO_CAL_WRAP 0
#endif
#if LAUNCHLAB_RADIO_CAL_WRAP
extern "C" esp_err_t __real_nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *out);
#endif
namespace RadioCalibration {
constexpr bool WRAPPED=LAUNCHLAB_RADIO_CAL_WRAP;
static volatile uint32_t refused=0;
static int reclaimed=0;
struct EspNvsApi {
  using Handle=nvs_handle_t;
  static constexpr esp_err_t Ok=ESP_OK,NotFound=ESP_ERR_NVS_NOT_FOUND;
  static esp_err_t open(const char *name,bool write,Handle *h) {
#if LAUNCHLAB_RADIO_CAL_WRAP
    return __real_nvs_open(name,write?NVS_READWRITE:NVS_READONLY,h);
#else
    return nvs_open(name,write?NVS_READWRITE:NVS_READONLY,h);
#endif
  }
  static esp_err_t used(Handle h,size_t *n){return nvs_get_used_entry_count(h,n);}
  static esp_err_t eraseAll(Handle h){return nvs_erase_all(h);}
  static esp_err_t commit(Handle h){return nvs_commit(h);}
  static void close(Handle h){nvs_close(h);}
};
// Boot only, before the ADC and the IMU start: it writes flash.
inline void reclaimAtBoot() {
  reclaimed=reclaim<EspNvsApi>();
  if(reclaimed)Serial.printf("NVS_RECLAIM key=%s entries=%d removed=%d\n",NAMESPACE,reclaimed<0?0:reclaimed,reclaimed>0);
}
inline void status() {
  Serial.printf("RADIO_CAL wrapped=%d refused=%lu reclaimed=%d\n",WRAPPED,(unsigned long)refused,reclaimed);
}
}
#if LAUNCHLAB_RADIO_CAL_WRAP
extern "C" esp_err_t __wrap_nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *out) {
  if(RadioCalibration::refuses(name)){RadioCalibration::refused=RadioCalibration::refused+1;return ESP_ERR_NVS_NOT_FOUND;}
  return __real_nvs_open(name,mode,out);
}
#endif
#endif
