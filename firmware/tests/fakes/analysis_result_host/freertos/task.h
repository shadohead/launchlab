#pragma once
#include "FreeRTOS.h"
#include <thread>
#include <chrono>
#include <cassert>
struct Finished{};using TaskHandle_t=std::thread*;
inline bool failTask=false;
inline TaskHandle_t xTaskCreateStaticPinnedToCore(void (*entry)(void*),const char*,unsigned stack,void *ctx,unsigned priority,StackType_t*,StaticTask_t*,unsigned core){
 assert(stack==8192 && priority==0 && core==1);if(failTask)return nullptr;
 return new std::thread([=]{try{entry(ctx);}catch(const Finished&){};});
}
inline void vTaskDelete(TaskHandle_t task){task->join();delete task;}
inline void vTaskDelay(unsigned ticks){std::this_thread::sleep_for(std::chrono::milliseconds(ticks));}
inline unsigned ulTaskNotifyTake(unsigned,unsigned){throw Finished{};}
