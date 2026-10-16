#pragma once
#include <cstdint>
namespace MotionFake {inline int64_t now=1000000;}
inline int64_t esp_timer_get_time(){return MotionFake::now;}
