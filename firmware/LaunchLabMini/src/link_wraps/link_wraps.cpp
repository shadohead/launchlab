// The functions the linker puts in place of an SDK function (--wrap=NAME in
// build-m5.sh and build-m5-hil.sh): a call to NAME runs __wrap_NAME, which
// reaches the SDK's own function as __real_NAME.
//
// They are in this file, apart from the sketch (and under src/, which every
// build script copies whole), because of where the linker puts them. The sketch is built with -mtext-section-literals, so a function's
// constants sit just before its code, and the linker drops the constants
// another function already holds. It then moves the function's symbol back by
// what it dropped, once for each symbol of the object file that names the
// function. With --wrap an object that defines __wrap_NAME and also calls NAME
// has two, so the symbol moves twice and lands before the first instruction.
// That object's own calls then jump into the constants; calls from other
// objects are right, so the image boots and runs until the sketch itself makes
// the call. Found on the rig, 2026-11-18: once the wipe code had a marker to
// read through nvs_get_blob, the HIL image crashed at every boot
// (IllegalInstruction in __wrap_nvs_get_blob, which hil.h then defined).
//
// So this file never calls a wrapped function by its own name and includes
// nothing of the sketch but link_wraps.h. tools/check_link_wraps.py fails a
// build whose wrapper does not start at its first instruction, and
// tests/test_link_wraps.py holds the sources to this rule.
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#include <nvs.h>
#include "../../link_wraps.h"

extern "C" {
#if LAUNCHLAB_RADIO_CAL_WRAP
// The radio's calibration data stays out of NVS (radio_calibration.h).
esp_err_t __real_nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *out);
esp_err_t __wrap_nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *out) {
  return LinkWraps::nvsOpenRefused(name)?ESP_ERR_NVS_NOT_FOUND:__real_nvs_open(name,mode,out);
}
#endif

#if LAUNCHLAB_HIL
// Test builds: NVS writes a test fails, slows or arms, and blob writes that
// land without reading back (hil.h).
esp_err_t __real_nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t length);
esp_err_t __real_nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value);
esp_err_t __real_nvs_set_u32(nvs_handle_t handle,const char *key,uint32_t value);
esp_err_t __real_nvs_get_blob(nvs_handle_t handle,const char *key,void *out,size_t *length);
esp_err_t __wrap_nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t length) {
  if(LinkWraps::hilNvsWriteFails(key,length))return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
  const esp_err_t result=__real_nvs_set_blob(handle,key,value,length);
  if(result==ESP_OK)LinkWraps::hilNvsWroteBlob(key);
  return result;
}
esp_err_t __wrap_nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value) {
  return LinkWraps::hilNvsWriteFails(key,1)?ESP_ERR_NVS_NOT_ENOUGH_SPACE:__real_nvs_set_u8(handle,key,value);
}
esp_err_t __wrap_nvs_set_u32(nvs_handle_t handle,const char *key,uint32_t value) {
  return LinkWraps::hilNvsWriteFails(key,4)?ESP_ERR_NVS_NOT_ENOUGH_SPACE:__real_nvs_set_u32(handle,key,value);
}
esp_err_t __wrap_nvs_get_blob(nvs_handle_t handle,const char *key,void *out,size_t *length) {
  return LinkWraps::hilNvsReadFails(key)?ESP_ERR_NVS_NOT_FOUND:__real_nvs_get_blob(handle,key,out,length);
}
#endif
}
#endif
