#pragma once
#include "device_wipe.h"

namespace DeviceWipe {
// The small API binding lets native tests execute this exact transaction and
// verification logic with a durable store and injected failures/power cuts.
template<class Api> class BasicNvsStore {
public:
  State inspectIntent() {
    typename Api::Handle h{};
    const auto opened=Api::open(INTENT_SPACE,false,&h);
    if(opened==Api::NotFound)return State::Clean;
    if(opened!=Api::Ok)return State::Error;
    size_t length=0;
    auto error=Api::getBlob(h,INTENT_KEY,nullptr,&length);
    if(error==Api::NotFound){Api::close(h);return State::Clean;}
    if(error==Api::TypeMismatch){Api::close(h);return State::Invalid;}
    if(error!=Api::Ok){Api::close(h);return State::Error;}
    if(length!=sizeof INTENT_BYTES){Api::close(h);return State::Invalid;}
    uint8_t bytes[sizeof INTENT_BYTES]{};
    error=Api::getBlob(h,INTENT_KEY,bytes,&length);Api::close(h);
    if(error==Api::TypeMismatch)return State::Invalid;
    if(error!=Api::Ok)return State::Error;
    return length==sizeof bytes && std::memcmp(bytes,INTENT_BYTES,sizeof bytes)==0?State::Pending:State::Invalid;
  }
  bool setIntent() {
    typename Api::Handle h{};
    if(Api::open(INTENT_SPACE,true,&h)!=Api::Ok)return false;
    auto error=Api::setBlob(h,INTENT_KEY,INTENT_BYTES,sizeof INTENT_BYTES);
    if(error==Api::Ok)error=Api::commit(h);
    Api::close(h);return error==Api::Ok;
  }
  bool clearNamespace(const char *space) {
    if(!ownedNamespace(space))return false;
    // Probe read-only first: do not allocate missing namespaces in a full NVS.
    typename Api::Handle h{};
    const auto opened=Api::open(space,false,&h);
    if(opened==Api::NotFound)return true;
    if(opened!=Api::Ok)return false;
    Api::close(h);
    if(Api::open(space,true,&h)!=Api::Ok)return false;
    auto error=Api::eraseAll(h);
    if(error==Api::Ok)error=Api::commit(h);
    Api::close(h);
    return error==Api::Ok && empty(space);
  }
  bool clearFlash(){return Api::eraseFlash()==Api::Ok;}
  bool clearIntent() {
    typename Api::Handle h{};
    if(Api::open(INTENT_SPACE,true,&h)!=Api::Ok)return false;
    auto error=Api::eraseKey(h,INTENT_KEY);
    if(error==Api::Ok)error=Api::commit(h);
    Api::close(h);return error==Api::Ok;
  }
private:
  bool empty(const char *space) {
    typename Api::Handle h{};
    const auto opened=Api::open(space,false,&h);
    if(opened==Api::NotFound)return true;
    if(opened!=Api::Ok)return false;
    typename Api::Iterator iterator{};
    const auto found=Api::find(h,&iterator);
    if(iterator)Api::release(iterator);
    Api::close(h);
    // A found entry or allocation/flash/iterator error is never "empty".
    return found==Api::NotFound;
  }
};
}

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#include <nvs.h>
#include "esp_partition.h"
namespace DeviceWipe {
struct EspNvsApi {
  using Handle=nvs_handle_t;using Iterator=nvs_iterator_t;
  static constexpr esp_err_t Ok=ESP_OK,NotFound=ESP_ERR_NVS_NOT_FOUND,TypeMismatch=ESP_ERR_NVS_TYPE_MISMATCH;
  static esp_err_t open(const char *name,bool write,Handle *h){return nvs_open(name,write?NVS_READWRITE:NVS_READONLY,h);}
  static esp_err_t getBlob(Handle h,const char *key,void *out,size_t *n){return nvs_get_blob(h,key,out,n);}
  static esp_err_t setBlob(Handle h,const char *key,const void *data,size_t n){return nvs_set_blob(h,key,data,n);}
  static esp_err_t eraseAll(Handle h){return nvs_erase_all(h);}
  static esp_err_t eraseKey(Handle h,const char *key){return nvs_erase_key(h,key);}
  static esp_err_t commit(Handle h){return nvs_commit(h);}
  static void close(Handle h){nvs_close(h);}
  static esp_err_t find(Handle h,Iterator *i){return nvs_entry_find_in_handle(h,NVS_TYPE_ANY,i);}
  static void release(Iterator i){nvs_release_iterator(i);}
  // The whole spiffs partition (1.5 MB, a few seconds of block erases).
  static esp_err_t eraseFlash() {
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,nullptr);
    return p?esp_partition_erase_range(p,0,p->size):ESP_OK;
  }
};
using NvsStore=BasicNvsStore<EspNvsApi>;
}
#endif
