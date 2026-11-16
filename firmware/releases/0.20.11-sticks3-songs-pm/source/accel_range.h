#pragma once
#include <cmath>
#include <cstdint>

// BMI270 accelerometer full scale. M5Unified leaves the reset default (+/-8 g)
// and always converts with that 8 g scale. The ripcord stop at the end of a
// hard pull exceeds 8 g, which clipped otherwise healthy tilt captures.
// Program +/-16 g after every IMU initialization (M5 begin and shake-wake
// restore both soft-reset the chip) and rescale M5's values by the range that
// actually reads back. The gyro already uses the BMI270 maximum, 2000 dps.
class StickS3AccelRange {
public:
  static constexpr uint8_t I2C_ADDRESS=0x68,ACC_RANGE=0x41,RANGE_MASK=0x03,RANGE_16G=0x03;
  static constexpr float M5_FULL_SCALE_G=8,GYRO_FULL_SCALE_DPS=2000,CLIP_FRACTION=.975f;
  // Bus provides bool read(uint8_t reg,uint8_t &value) and bool write(uint8_t reg,uint8_t value).
  // Returns false only when the active range cannot be read back. Initialization
  // has just reset the chip, so the 8 g default is then the best estimate.
  template<class Bus> bool configure(Bus &bus) {
    uint8_t value=0;
    if(!bus.read(ACC_RANGE,value)){fullScaleG_=M5_FULL_SCALE_G;return false;}
    if((value&RANGE_MASK)!=RANGE_16G && bus.write(ACC_RANGE,uint8_t((value&~RANGE_MASK)|RANGE_16G)) &&
       !bus.read(ACC_RANGE,value)){fullScaleG_=M5_FULL_SCALE_G;return false;}
    fullScaleG_=float(2u<<(value&RANGE_MASK));return true;
  }
  float fullScaleG()const{return fullScaleG_;}
  // M5Unified converts raw counts with its fixed 8 g scale.
  float toG(float m5Value)const{return m5Value*(fullScaleG_/M5_FULL_SCALE_G);}
  bool accelClipped(float g)const{return !(std::fabs(g)<CLIP_FRACTION*fullScaleG_);}
  static bool gyroClipped(float dps){return !(std::fabs(dps)<CLIP_FRACTION*GYRO_FULL_SCALE_DPS);}
private:
  float fullScaleG_=M5_FULL_SCALE_G;
};
inline StickS3AccelRange &accelRange(){static StickS3AccelRange range;return range;}

#ifdef ARDUINO
#include <M5Unified.h>
// Caller owns the IMU bus: setup/startup before the IMU task, or the IMU gate.
inline bool configureAccelRange() {
  struct Bus {
    bool read(uint8_t reg,uint8_t &value){return M5.In_I2C.readRegister(StickS3AccelRange::I2C_ADDRESS,reg,&value,1,400000);}
    bool write(uint8_t reg,uint8_t value){return M5.In_I2C.writeRegister(StickS3AccelRange::I2C_ADDRESS,reg,&value,1,400000);}
  } bus;
  const bool ok=accelRange().configure(bus);
  Serial.printf("IMU_RANGE accel_g=%.0f gyro_dps=%.0f verified=%d\n",accelRange().fullScaleG(),
    StickS3AccelRange::GYRO_FULL_SCALE_DPS,ok);
  return ok;
}
#endif
