#pragma once
#include "esp_timer.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>
using TickType_t=uint32_t;
using TaskHandle_t=void*;
struct MotionFakeMutex {int64_t availableAt=0;};
using SemaphoreHandle_t=MotionFakeMutex*;
static constexpr int pdTRUE=1,pdPASS=1;
static constexpr TickType_t portMAX_DELAY=UINT32_MAX;
inline TickType_t pdMS_TO_TICKS(uint32_t ms){return ms;}
namespace MotionFake {
inline std::vector<std::unique_ptr<MotionFakeMutex>> gates;
inline std::function<void()> task,afterPoll;
}
inline SemaphoreHandle_t xSemaphoreCreateMutex(){
  MotionFake::gates.emplace_back(new MotionFakeMutex);return MotionFake::gates.back().get();
}
// Virtual blocking: an in-flight bus owner releases at availableAt. No sleeps,
// host scheduling or wall-clock races are involved in these regression tests.
inline int xSemaphoreTake(SemaphoreHandle_t gate,TickType_t timeout){
  if(gate->availableAt>MotionFake::now){
    if(timeout!=portMAX_DELAY && gate->availableAt-MotionFake::now>int64_t(timeout)*1000){
      MotionFake::now+=int64_t(timeout)*1000;return 0;
    }
    MotionFake::now=gate->availableAt;
  }
  return pdTRUE;
}
inline void xSemaphoreGive(SemaphoreHandle_t gate){gate->availableAt=MotionFake::now;}
inline int xTaskCreatePinnedToCore(void(*entry)(void*),const char*,int,void* self,int,TaskHandle_t* out,int){
  MotionFake::task=[=]{entry(self);};*out=self;return pdPASS;
}
inline TickType_t xTaskGetTickCount(){return TickType_t(MotionFake::now/1000);}
inline unsigned uxTaskGetStackHighWaterMark(void*){return 8192;}
inline void vTaskDelayUntil(TickType_t*,TickType_t){MotionFake::afterPoll();}
