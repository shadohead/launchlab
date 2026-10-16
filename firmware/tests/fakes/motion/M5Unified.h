#pragma once
#include "freertos/semphr.h"
struct MotionFakeImu {
  bool isEnabled(){return true;}
  void setCalibration(int,int,int){}
};
inline struct MotionFakeM5 {MotionFakeImu Imu;} M5;
