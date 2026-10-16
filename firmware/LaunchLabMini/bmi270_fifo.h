#pragma once
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include "accel_range.h"

// BMI270 hardware FIFO in header mode. M5Unified's polling path decides
// freshness from clear-on-read data-ready flags; on StickS3 it lost several
// consecutive samples every 5-25 s at rest (35 ms gaps with every poll on
// time), which reset fusion, blanked the live level and rejected pulls. The
// FIFO retains every sample for about 1 s, so a late read only delays data.
// Overflow is detected and becomes a real gap.
namespace Bmi270Fifo {
static constexpr uint8_t ACC_CONF=0x40,GYR_CONF=0x42,GYR_RANGE=0x43,FIFO_DOWNS=0x45,
  FIFO_CONFIG_0=0x48,FIFO_CONFIG_1=0x49,FIFO_LENGTH_0=0x24,FIFO_DATA=0x26,CMD=0x7E;
// Accel 100 Hz / gyro 200 Hz, performance filters, normal bandwidth (M5's values).
static constexpr uint8_t ACC_CONF_VALUE=0xA8,GYR_CONF_VALUE=0xA9,GYR_RANGE_2000=0x00;
// Filtered data, no downsampling; streaming; header mode with gyro and accel.
static constexpr uint8_t FIFO_DOWNS_VALUE=0x88,FIFO_CONFIG_0_VALUE=0x00,FIFO_CONFIG_1_VALUE=0xD0,FLUSH=0xB0;
static constexpr uint8_t ACC_GYR=0x8C,GYR=0x88,ACC=0x84,SKIP=0x40,TIME=0x44,INPUT_CONFIG=0x48,OVER_READ=0x80;
static constexpr unsigned CAPACITY=2048,GYRO_PERIOD_US=5000,SETTLE_MS=60;
// Treat a FIFO this close to full as overflowed. On device a full header-mode
// FIFO reported 2009-2015 bytes; accel+gyro fill it at about 2000 bytes/s.
static constexpr unsigned FULL_MARGIN=64;
// Measured on device: a single-transaction FIFO read costs about 38 us per
// byte (600 bytes in 23 ms). A transfer more than 10 ms over that crossed a
// preemption or flash stall; the I2C hardware aborts a bus held for 13 ms.
static constexpr uint32_t BYTE_US=40,STALL_SLACK_US=10000;
inline uint32_t transferBudgetUs(unsigned bytes){return 1000+bytes*BYTE_US+STALL_SLACK_US;}

struct Frame {bool accel=false,gyro=false;int16_t a[3]={},g[3]={};};
inline int16_t get16(const uint8_t *p){return int16_t(uint16_t(p[0])|(uint16_t(p[1])<<8));}
// Parses exactly n FIFO bytes. frame(Frame) is called per data frame and
// skipped(count) per overflow marker. Returns false for an unknown header or
// a truncated frame; the caller then flushes and treats it as a gap.
template<class OnFrame,class OnSkip> bool parse(const uint8_t *p,size_t n,OnFrame frame,OnSkip skipped) {
  size_t i=0;
  while(i<n) {
    const uint8_t header=p[i++];
    if(header==OVER_READ)return true;
    if(header==ACC_GYR || header==GYR || header==ACC) {
      Frame f;f.gyro=header!=ACC;f.accel=header!=GYR;
      const size_t need=(f.gyro?6:0)+(f.accel?6:0);
      if(i+need>n)return false;
      // Within a frame the gyro precedes the accelerometer.
      if(f.gyro){for(unsigned j=0;j<3;++j)f.g[j]=get16(p+i+2*j);i+=6;}
      if(f.accel){for(unsigned j=0;j<3;++j)f.a[j]=get16(p+i+2*j);i+=6;}
      frame(f);
    } else if(header==SKIP) {
      if(i+1>n)return false;
      skipped(p[i++]);
    } else if(header==TIME) {
      if(i+3>n)return false;i+=3;
    } else if(header==INPUT_CONFIG) {
      if(i+4>n)return false;i+=4;
    } else return false;
  }
  return true;
}
// StickS3 board frame from BMI270 axes, as in M5Unified: X=+Y, Y=-X, Z=+Z.
inline void boardAxes(const int16_t *raw,float scale,float *out) {
  out[0]=raw[1]*scale;out[1]=-raw[0]*scale;out[2]=raw[2]*scale;
}

// Host timestamps for gyro frames. Frames are produced by the sensor clock at
// a nominal 5 ms. As with the earlier polled path, the newest frame in a read
// is stamped at read time (on average half a period late, a constant offset).
// The period follows the measured rate and each step is corrected by at most
// 3%, so integrated time stays within a fraction of a frame of the host clock.
// Skipped (overflowed) frames advance time by whole periods. Any other
// discontinuity beyond 35 ms (reconfiguration, a stopped sensor) re-anchors,
// which fusion sees as a gap.
class Timeline {
public:
  static constexpr double MIN_PERIOD_US=4750,MAX_PERIOD_US=5250,MAX_STEP_FRACTION=.03,RESYNC_US=35000;
  static constexpr double PHASE_GAIN=.05,PERIOD_GAIN=.0002;
  void reset(){started_=false;pendingSkip_=0;}
  void skip(unsigned frames){pendingSkip_+=frames;}
  // Call once per read, before stamping its n gyro frames with next().
  void begin(uint64_t readUs,unsigned n) {
    first_=true;if(!n)return;
    const double newest=double(readUs);
    const double projected=time_+(n+pendingSkip_)*period_;
    if(!started_ || std::fabs(projected-newest)>RESYNC_US) {
      if(started_)++anchors_;
      // Never move backwards: timestamps must stay strictly increasing.
      const double start=std::max(0.0,newest-n*period_);
      time_=started_?std::max(time_,start):start;
      started_=true;pendingSkip_=0;step_=period_;return;
    }
    // Phase-locked loop: newest-frame age varies over one period, so low
    // gains average that jitter while following the sensor clock.
    const double error=newest-projected,bound=period_*MAX_STEP_FRACTION;
    step_=period_+std::max(-bound,std::min(bound,error*PHASE_GAIN/n));
    period_=std::max(MIN_PERIOD_US,std::min(MAX_PERIOD_US,period_+error*PERIOD_GAIN));
  }
  uint64_t next() {
    double step=step_;
    if(first_){step+=pendingSkip_*period_;pendingSkip_=0;first_=false;}
    time_+=step;return uint64_t(time_+.5);
  }
  double periodUs()const{return period_;}
  uint32_t anchors()const{return anchors_;}
private:
  double time_=0,period_=GYRO_PERIOD_US,step_=GYRO_PERIOD_US;
  unsigned pendingSkip_=0;uint32_t anchors_=0;bool started_=false,first_=true;
};
}

#ifdef ARDUINO
#include <M5Unified.h>
namespace Bmi270Fifo {
inline bool readRegisters(uint8_t reg,uint8_t *data,size_t n) {
  return M5.In_I2C.readRegister(StickS3AccelRange::I2C_ADDRESS,reg,data,n,400000);
}
inline bool writeRegister(uint8_t reg,uint8_t value) {
  return M5.In_I2C.writeRegister(StickS3AccelRange::I2C_ADDRESS,reg,&value,1,400000);
}
// Program and verify sensor rates, gyro range and FIFO, then flush. Caller
// owns the IMU bus. Every M5 begin and shake-wake arm soft-resets these.
inline bool configure() {
  struct {uint8_t reg,value;} const settings[]={{ACC_CONF,ACC_CONF_VALUE},{GYR_CONF,GYR_CONF_VALUE},
    {GYR_RANGE,GYR_RANGE_2000},{FIFO_DOWNS,FIFO_DOWNS_VALUE},{FIFO_CONFIG_0,FIFO_CONFIG_0_VALUE},{FIFO_CONFIG_1,FIFO_CONFIG_1_VALUE}};
  bool ok=true;
  for(const auto &s:settings) {
    uint8_t value=0;
    ok=ok && writeRegister(s.reg,s.value) && readRegisters(s.reg,&value,1) && value==s.value;
  }
  // Frames produced while the range and filters change still carry the old
  // full scale (seen on device as 1.9 g at rest). Let them settle, then drop them.
  delay(SETTLE_MS);
  ok=ok && writeRegister(CMD,FLUSH);
  return ok;
}
// Newest FIFO accelerometer frame in g (board axes), for the startup check.
inline bool latestAccel(float *out) {
  uint8_t length[2];static uint8_t data[CAPACITY];
  if(!readRegisters(FIFO_LENGTH_0,length,2))return false;
  const unsigned bytes=unsigned(length[0])|(unsigned(length[1]&0x3F)<<8);
  if(!bytes || bytes>CAPACITY)return false;
  if(!readRegisters(FIFO_DATA,data,bytes))return false;
  bool found=false;
  const bool parsed=parse(data,bytes,[&](const Frame &f){if(f.accel){boardAxes(f.a,accelRange().fullScaleG()/32768,out);found=true;}},[](unsigned){});
  return parsed && found;
}
}
// Range, rates and FIFO after every IMU initialization. At startup a resting
// comparison against M5Unified's own conversion proves the FIFO axis mapping
// and scale; while moving it is skipped, never failed. After shake-wake
// restore M5's data registers were seen to report stale 8 g-scale values while
// the FIFO was correct, and the mapping cannot change, so restore skips it.
inline bool configureMotionSensor(bool compareWithM5=true) {
  if(!configureAccelRange())return false;
  const bool fifo=Bmi270Fifo::configure();
  float frame[3]={},m5[3]={};bool checked=false,match=true,haveFrame=false;
  // The accelerometer produces a frame every 10 ms; allow ten periods.
  for(unsigned waited=0;fifo && !haveFrame && waited<100;waited+=10){delay(10);haveFrame=Bmi270Fifo::latestAccel(frame);}
  if(compareWithM5 && haveFrame && (M5.Imu.update() & m5::IMU_Class::sensor_mask_accel)) {
    const auto data=M5.Imu.getImuData();const auto &range=accelRange();
    m5[0]=range.toG(data.accel.x);m5[1]=range.toG(data.accel.y);m5[2]=range.toG(data.accel.z);
    const float a=std::sqrt(frame[0]*frame[0]+frame[1]*frame[1]+frame[2]*frame[2]);
    const float b=std::sqrt(m5[0]*m5[0]+m5[1]*m5[1]+m5[2]*m5[2]);
    if(a>.8f && a<1.2f && b>.8f && b<1.2f) {
      checked=true;for(unsigned j=0;j<3;++j)match&=std::fabs(frame[j]-m5[j])<.25f;
    }
  }
  Serial.printf("IMU_FIFO configured=%d frame=%d checked=%d match=%d fifo_g=%.3f,%.3f,%.3f m5_g=%.3f,%.3f,%.3f\n",
    fifo,haveFrame,checked,match,frame[0],frame[1],frame[2],m5[0],m5[1],m5[2]);
  return fifo && haveFrame && match;
}
#endif
