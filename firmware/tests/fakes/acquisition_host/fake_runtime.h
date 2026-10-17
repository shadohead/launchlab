#pragma once
// Synchronous, single-threaded host stand-ins for the ESP-IDF ADC, timer and
// FreeRTOS calls that acquisition.h makes. Shared by the host harnesses that
// compile the complete production acquisition owner.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_ARG=1,ESP_ERR_TIMEOUT=2;
using adc_unit_t=int;using adc_channel_t=int;using gpio_num_t=int;
constexpr int ADC_UNIT_1=1,ADC_ATTEN_DB_12=12,ADC_CONV_SINGLE_UNIT_1=1,
  ADC_DIGI_OUTPUT_FORMAT_TYPE2=2,GPIO_MODE_INPUT=1,GPIO_FLOATING=0;
static int64_t clockUs=1000000;
static int64_t esp_timer_get_time(){return clockUs;}
struct FakeAdc {bool active=false;};using adc_continuous_handle_t=FakeAdc*;
struct adc_continuous_evt_data_t {};
struct adc_continuous_handle_cfg_t {uint32_t max_store_buf_size=0,conv_frame_size=0;};
struct adc_digi_pattern_config_t {int atten=0,channel=0,unit=0,bit_width=0;};
struct adc_continuous_config_t {uint32_t sample_freq_hz=0;int conv_mode=0,format=0,pattern_num=0;adc_digi_pattern_config_t *adc_pattern=nullptr;};
struct adc_continuous_evt_cbs_t {bool(*on_pool_ovf)(adc_continuous_handle_t,const adc_continuous_evt_data_t*,void*)=nullptr;};
struct adc_digi_output_data_t {struct {uint16_t data;uint8_t channel,unit;} type2;};
constexpr unsigned SOC_ADC_DIGI_RESULT_BYTES=sizeof(adc_digi_output_data_t);
static unsigned stops=0,starts=0,flushes=0,handles=0;
static bool failStop=false,failStart=false,failFlush=false;
static esp_err_t adc_continuous_io_to_channel(int pin,adc_unit_t *unit,adc_channel_t *channel){*unit=ADC_UNIT_1;*channel=pin-1;return ESP_OK;}
static esp_err_t adc_continuous_new_handle(const adc_continuous_handle_cfg_t*,adc_continuous_handle_t *out){*out=new FakeAdc;++handles;return ESP_OK;}
static esp_err_t adc_continuous_config(adc_continuous_handle_t h,const adc_continuous_config_t *c){assert(h && c->sample_freq_hz==50000 && c->pattern_num==1);return ESP_OK;}
static esp_err_t adc_continuous_register_event_callbacks(adc_continuous_handle_t h,const adc_continuous_evt_cbs_t*,void*){assert(h);return ESP_OK;}
static esp_err_t adc_continuous_start(adc_continuous_handle_t h){assert(h && !h->active);++starts;if(failStart){failStart=false;return ESP_FAIL;}h->active=true;return ESP_OK;}
static esp_err_t adc_continuous_stop(adc_continuous_handle_t h){assert(h && h->active);++stops;if(failStop){failStop=false;return ESP_FAIL;}h->active=false;return ESP_OK;}
static esp_err_t adc_continuous_flush_pool(adc_continuous_handle_t h){assert(h && !h->active);++flushes;if(failFlush){failFlush=false;return ESP_FAIL;}return ESP_OK;}
static esp_err_t adc_continuous_deinit(adc_continuous_handle_t h){assert(h && !h->active);delete h;--handles;return ESP_OK;}
static esp_err_t adc_continuous_read(adc_continuous_handle_t,uint8_t*,size_t,uint32_t *bytes,int){*bytes=0;return ESP_ERR_TIMEOUT;}
static void gpio_set_direction(gpio_num_t,int){}static void gpio_set_pull_mode(gpio_num_t,int){}
using TickType_t=uint32_t;using TaskHandle_t=void*;using portMUX_TYPE=int;
#define IRAM_ATTR
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
constexpr int pdTRUE=1,pdPASS=1;static TickType_t pdMS_TO_TICKS(uint32_t ms){return ms;}
static TickType_t xTaskGetTickCount(){return uint32_t(clockUs/1000);}
static void vTaskDelay(uint32_t ms){clockUs+=int64_t(ms)*1000;}
static unsigned uxTaskGetStackHighWaterMark(void*){return 1024;}
static int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t *out,int){*out=reinterpret_cast<void*>(1);return pdPASS;}
struct FakeQueue {size_t capacity,itemSize;std::deque<std::vector<uint8_t>> items;};using QueueHandle_t=FakeQueue*;
static QueueHandle_t xQueueCreate(size_t n,size_t size){return new FakeQueue{n,size,{}};}
static int xQueueSend(QueueHandle_t q,const void *data,TickType_t){assert(q);if(q->items.size()==q->capacity)return 0;const auto p=static_cast<const uint8_t*>(data);q->items.emplace_back(p,p+q->itemSize);return pdTRUE;}
// A harness may set ownerPump to run the acquisition owner's command loop
// whenever a waiter finds a queue empty, as the core-0 task would. A wait that
// still finds nothing advances the clock by its timeout, so reply timeouts in
// the real request() path elapse in simulated rather than wall time.
static void (*ownerPump)()=nullptr;
static int xQueueReceive(QueueHandle_t q,void *out,TickType_t ticks){
  assert(q);static bool pumping=false;
  if(q->items.empty() && ownerPump && !pumping){pumping=true;ownerPump();pumping=false;}
  if(q->items.empty()){clockUs+=int64_t(ticks)*1000;return 0;}
  std::memcpy(out,q->items.front().data(),q->itemSize);q->items.pop_front();return pdTRUE;
}
static void vQueueDelete(QueueHandle_t q){delete q;}
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
static void *heap_caps_malloc(size_t n,int){return std::malloc(n);}
