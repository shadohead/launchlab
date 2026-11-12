#pragma once
#include <cstdint>
#include "power_status.h"
#include "sensor_idle.h"

// Battery savers whose benefit and cost are still being measured
// (docs/battery). The defaults are the shipped behaviour; the battery lab
// (power_lab_device.h) changes them per profile so one discharge compares
// them. The CPU and IMU rate knobs live with their owners (cpu_power.h,
// motion_acquisition.h).
namespace PowerSavers {
struct Settings {
  // The screen dims after dimMs without use: a button, a pull, a USB command
  // or the recap, as a phone does. Through 0.20.8 movement counted as use
  // too (dimIgnoresMotion false), so a unit in a hand never dimmed.
  uint32_t dimMs=IdlePowerPolicy::DIM_MS;
  bool dimIgnoresMotion=true;
  uint8_t dimBacklight=0;  // raw backlight value while dimmed; 0: DIM_PERCENT
  uint32_t restMs=SensorIdle::IDLE_MS;  // stillness before the optical ADC rests
  bool changedRowsOnly=true;  // push only the rows that changed (frame_rows.h)
};
inline Settings settings;
}
