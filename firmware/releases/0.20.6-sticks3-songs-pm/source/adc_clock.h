#pragma once
#include <cstdint>

// The ESP32-S3's continuous ADC is timed from the APB clock: IDF divides it
// to a 5 MHz controller clock (divider 15+1 at 80 MHz) and triggers every
// `interval` controller cycles, two per sample, so 50 at 50 kS/s. IDF reads
// the APB frequency when the ADC is configured, but the ADC samples at the
// APB its own power lock holds once started: a configure while DFS had the
// CPU at 40 MHz and a start at 80 would sample twice as fast, and the battery
// lab's fixed 40 MHz CPU half as fast. timing() gives the divider and interval
// that keep the controller at 5 MHz and the rate exact at the APB the ADC is
// actually running on (acquisition.h applies it after every start).
namespace AdcClock {
constexpr uint32_t CONTROLLER_HZ=5000000;
struct Timing {uint32_t divider=0,interval=0;bool valid=false;};  // divider: divide by divider+1
constexpr Timing timing(uint32_t apbHz,uint32_t sampleHz) {
  return apbHz>=CONTROLLER_HZ && apbHz%CONTROLLER_HZ==0 && sampleHz && (CONTROLLER_HZ/2)%sampleHz==0 &&
    apbHz/CONTROLLER_HZ-1<=255 && CONTROLLER_HZ/2/sampleHz<=4095 ?
    Timing{apbHz/CONTROLLER_HZ-1,CONTROLLER_HZ/2/sampleHz,true}:Timing{};
}
}
