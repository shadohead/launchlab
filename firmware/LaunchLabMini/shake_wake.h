#pragma once
#include <M5Unified.h>
#include "esp_sleep.h"
#include "src/bmi270/bmi270.h"
#include "bmi270_fifo.h"
#include "pm1_idle.h"
#include "power_key.h"

// Bosch BMI270 API from SparkFun's official M5 wake example driver, commit
// 21ea234de321da07c552f7a43cb36f7df4f73a27. Uses M5's existing I2C bus;
// caller must hold the IMU gate and stop optical acquisition before arming.
class StickS3ShakeWake {
public:
  static constexpr unsigned THRESHOLD=2047; // Maximum 11-bit value; 0.48 mg/LSB: about 983 mg.
  static constexpr unsigned DURATION=6;    // 20 ms/LSB: 120 ms.
  static_assert(THRESHOLD<=BMI2_ANY_NO_MOT_THRES_MASK,"Wake threshold must fit the BMI270 field");
  bool clear() {
    // Manual power off stays manual; only an inactivity sleep arms wake.
    const bool wake=masked(0x18,0x18,0);
    const bool hold=masked(0x07,0x60,0);
    // PM1 survives CPU sleep/reset. arm() enables its LED for the documented
    // standby sequence; explicitly clear it on startup and IMU restore.
    const bool led=masked(0x06,0x10,0);
    return wake && hold && led;
  }
  bool arm() {
    dev_={};dev_.intf=BMI2_I2C_INTF;dev_.read=read;dev_.write=write;
    dev_.delay_us=wait;dev_.read_write_len=32;
    int8_t result=bmi270_init(&dev_);
    if(result==BMI2_OK) {
      bmi2_sens_config configs[2]={};configs[0].type=BMI2_ACCEL;configs[1].type=BMI2_ANY_MOTION;
      result=bmi270_get_sensor_config(configs,2,&dev_);
      if(result==BMI2_OK) {
        configs[0].cfg.acc.odr=BMI2_ACC_ODR_50HZ;
        configs[0].cfg.acc.filter_perf=BMI2_POWER_OPT_MODE;
        configs[0].cfg.acc.bwp=BMI2_ACC_NORMAL_AVG4;
        configs[1].cfg.any_motion.threshold=THRESHOLD;
        configs[1].cfg.any_motion.duration=DURATION;
        configs[1].cfg.any_motion.select_x=configs[1].cfg.any_motion.select_y=configs[1].cfg.any_motion.select_z=BMI2_ENABLE;
        result=bmi270_set_sensor_config(configs,2,&dev_);
        if(result==BMI2_OK)result=bmi270_get_sensor_config(configs,2,&dev_);
        if(result==BMI2_OK && (configs[1].cfg.any_motion.threshold!=THRESHOLD ||
          configs[1].cfg.any_motion.duration!=DURATION))result=BMI2_E_INVALID_INPUT;
      }
    }
    uint8_t enabled[]={BMI2_ACCEL,BMI2_ANY_MOTION};
    if(result==BMI2_OK)result=bmi270_sensor_enable(enabled,2,&dev_);
    bmi2_int_pin_config pin={};pin.pin_type=BMI2_INT1;pin.int_latch=BMI2_INT_NON_LATCH;
    pin.pin_cfg[0].lvl=BMI2_INT_ACTIVE_LOW;pin.pin_cfg[0].od=BMI2_INT_PUSH_PULL;
    pin.pin_cfg[0].output_en=BMI2_INT_OUTPUT_ENABLE;pin.pin_cfg[0].input_en=BMI2_INT_INPUT_DISABLE;
    if(result==BMI2_OK)result=bmi2_set_int_pin_config(&pin,&dev_);
    if(result==BMI2_OK)result=bmi2_map_feat_int(BMI2_ANY_MOTION,BMI2_INT1,&dev_);
    if(result==BMI2_OK)result=bmi2_set_adv_power_save(BMI2_ENABLE,&dev_);
    auto &pm=M5.Power.M5pm1;
    bool ok=result==BMI2_OK && pm1Idle().ensureAwake()
      && pm.setGPIOMode(m5::M5PM1_Class::gpio4,m5::M5PM1_Class::input)
      && pm.setGPIOFunction(m5::M5PM1_Class::gpio4,m5::M5PM1_Class::gpio)
      && pm.setGPIOPull(m5::M5PM1_Class::gpio4,m5::M5PM1_Class::pull_up)
      && masked(0x19,0x10,0)       // GPIO4 falling-edge wake.
      && masked(0x18,0x18,0x10)   // GPIO3/4 share a wake line; only 4 enabled.
      && pm.setLDOOutput(true) && masked(0x07,0x60,0x20) // Hold IMU, never boost.
      && pm.setLedEnLevel(true) && pm.setExtOutput(false);
    // Also route IMU IRQ to G13 for deep-sleep fallback if USB retains power.
    ok=ok && pm.setGPIOMode(m5::M5PM1_Class::gpio1,m5::M5PM1_Class::output)
      && pm.setGPIODrive(m5::M5PM1_Class::gpio1,m5::M5PM1_Class::push_pull)
      && pm.setGPIOFunction(m5::M5PM1_Class::gpio1,m5::M5PM1_Class::irq)
      && pm.setGPIOIRQMaskBits(0x0F) && pm.setSystemIRQMaskBits(0xFF)
      && pm.setButtonIRQMaskBits(0xFF) && pm.clearIRQStatus();
    uint8_t hold=0,wake=0,edge=0,power=0;
    ok=ok && pm1Idle().read(0x07,&hold,1) && pm1Idle().read(0x18,&wake,1)
      && pm1Idle().read(0x19,&edge,1) && read(BMI2_PWR_CTRL_ADDR,&power,1,nullptr)==0;
    ok=ok && (hold&0x60)==0x20 && (wake&0x18)==0x10 && !(edge&0x10)
      && (power&0x06)==0x04; // Accelerometer on, gyroscope off.
    Serial.printf("SHAKE_WAKE_ARM ok=%d bmi_error=%d threshold_mg=983 duration_ms=120 hold=0x%02x wake=0x%02x edge=0x%02x imu_power=0x%02x\n",
      ok,result,hold,wake,edge,power);
    return ok;
  }
  bool restore() {
    const bool cleared=clear();
    // arm() routes the IMU instead of the side key to the PM1 IRQ line.
    const bool key=powerKey().configure();
    // arm uploads a separate feature configuration. Always restore M5's
    // measurement configuration before letting its IMU owner poll again.
    const bool imu=M5.Imu.begin(&M5.In_I2C,m5::board_t::board_M5StickS3);
    M5.Imu.setCalibration(0,0,0);
    const bool range=imu && configureMotionSensor(false);
    return cleared && imu && range && key;
  }
  bool sleep() {
    // Battery removes CPU/sensor L2 power; USB may retain it, so keep ext0 fallback.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if(esp_sleep_enable_ext0_wakeup(GPIO_NUM_13,0)!=ESP_OK)return false;
    if(!pm1Idle().run([](){return M5.Power.M5pm1.powerOff();}))return false;
    delay(30);
    esp_deep_sleep_start();
    return false;
  }

private:
  bmi2_dev dev_={};
  static BMI2_INTF_RETURN_TYPE read(uint8_t reg,uint8_t *data,uint32_t len,void *) {
    return M5.In_I2C.readRegister(0x68,reg,data,len,400000)?0:-1;
  }
  static BMI2_INTF_RETURN_TYPE write(uint8_t reg,const uint8_t *data,uint32_t len,void *) {
    return M5.In_I2C.writeRegister(0x68,reg,data,len,400000)?0:-1;
  }
  static void wait(uint32_t us,void *) {if(us>=1000)delay((us+999)/1000);else delayMicroseconds(us);}
  static bool masked(uint8_t reg,uint8_t mask,uint8_t value) {
    return pm1Idle().masked(reg,mask,value);
  }
};
