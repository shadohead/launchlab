#pragma once
// Hardware-in-the-loop BMI270 FIFO. Test builds only (LAUNCHLAB_HIL=1).
//
// Bmi270Fifo::readRegisters/writeRegister route through here. Every FIFO
// transfer still runs on the real I2C bus (same timing, same real errors);
// in a virtual mode the returned bytes come from a modelled header-mode FIFO
// instead: gyro frames every 5 ms on a virtual sensor clock, accelerometer on
// every second frame, 2 KiB capacity with overflow at the same fill the real
// chip reports. Faults are injected at the transfer boundary, so the real
// parser, timeline, fusion, ring, capture and UI paths all run unchanged.
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <M5Unified.h>
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_heap_caps.h"
#include "accel_range.h"
#include "bmi270_fifo.h"
#include "hil_optical.h"

namespace Hil {
class Imu {
public:
  enum class Mode:uint8_t {Pass,Constant,Profile};
  // One profile frame per 5 ms gyro period, board axes: accel in mg, gyro in
  // 0.1 dps. Values beyond the sensor's full scale saturate like real counts.
  struct Frame {int16_t a[3],g[3];};
  static constexpr uint32_t PROFILE_CAPACITY=8000,PROFILE_PERIOD_US=5000;
  static const char *name(Mode m){return m==Mode::Constant?"constant":m==Mode::Profile?"profile":"pass";}
  struct Faults {
    std::atomic<uint32_t> lengthFail{0},dataFail{0},corrupt{0},garble{0},overflow{0},oversize{0},stall{0};
    std::atomic<uint32_t> stallMs{20},skip{0},jitterUs{0},sensorJitterUs{0},freeze{0};
    std::atomic<int32_t> driftPpm{0};
    void clear(){lengthFail=dataFail=corrupt=garble=overflow=oversize=stall=skip=jitterUs=sensorJitterUs=freeze=0;stallMs=20;driftPpm=0;}
  } faults;
  struct Counters {std::atomic<uint32_t> frames{0},flushes{0},lengthFails{0},dataFails{0},corrupts{0},garbles{0},
    overflows{0},naturalOverflows{0},oversizes{0},stalls{0},skips{0},activations{0};} counters;

  // ---- Host configuration (UI task).
  void setMode(Mode m){mode_=m;}
  Mode mode()const{return mode_.load();}
  void setConstant(const float *a,const float *g,float noiseG,float noiseDps) {
    portENTER_CRITICAL(&mux_);
    for(unsigned j=0;j<3;++j){base_.a[j]=a[j];base_.g[j]=g[j];}
    noiseG_=noiseG;noiseDps_=noiseDps;portEXIT_CRITICAL(&mux_);
  }
  // Leave Profile mode before rewriting the buffer; it is never freed.
  Frame *profile() {
    if(!profile_)profile_=static_cast<Frame *>(heap_caps_malloc(PROFILE_CAPACITY*sizeof(Frame),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    return profile_;
  }
  void setProfile(uint32_t frames,int32_t offsetMs){
    portENTER_CRITICAL(&mux_);profileLength_=std::min(frames,PROFILE_CAPACITY);offsetUs_=int64_t(offsetMs)*1000;portEXIT_CRITICAL(&mux_);
  }
  void setOffset(int32_t offsetMs){portENTER_CRITICAL(&mux_);offsetUs_=int64_t(offsetMs)*1000;portEXIT_CRITICAL(&mux_);}
  uint32_t profileLength()const{return profileLength_;}
  int32_t offsetMs()const{return int32_t(offsetUs_/1000);}
  void base(float *a,float *g)const{for(unsigned j=0;j<3;++j){a[j]=base_.a[j];g[j]=base_.g[j];}}

  // ---- IMU owner (gate held): transfer hooks.
  bool read(uint8_t reg,uint8_t *data,size_t n) {
    const Mode mode=mode_.load();
    if(mode==Mode::Pass) {
      // Bytes read while virtual can leave the real FIFO mid-frame.
      if(active_){active_=false;realWrite(Bmi270Fifo::CMD,Bmi270Fifo::FLUSH);}
      return realRead(reg,data,n);
    }
    if(reg!=Bmi270Fifo::FIFO_LENGTH_0 && reg!=Bmi270Fifo::FIFO_DATA)return realRead(reg,data,n);
    if(!active_)activate();
    return reg==Bmi270Fifo::FIFO_LENGTH_0?readLength(data,n):readData(data,n);
  }
  bool write(uint8_t reg,uint8_t value) {
    const bool ok=realWrite(reg,value);
    if(active_ && reg==Bmi270Fifo::CMD && value==Bmi270Fifo::FLUSH){consumed_=framesAt(esp_timer_get_time());latched_=consumed_;skipPending_=0;++counters.flushes;}
    return ok;
  }
private:
  struct Value {float a[3]={0,0,1},g[3]={};};
  static bool realRead(uint8_t reg,uint8_t *data,size_t n){return M5.In_I2C.readRegister(StickS3AccelRange::I2C_ADDRESS,reg,data,n,400000);}
  static bool realWrite(uint8_t reg,uint8_t value){return M5.In_I2C.writeRegister(StickS3AccelRange::I2C_ADDRESS,reg,&value,1,400000);}
  void activate() {
    realWrite(Bmi270Fifo::CMD,Bmi270Fifo::FLUSH);
    lastUs_=esp_timer_get_time();phase_=0;consumed_=produced_=latched_=0;skipPending_=0;active_=true;++counters.activations;
  }
  double periodUs()const{return Bmi270Fifo::GYRO_PERIOD_US*(1.0+faults.driftPpm.load()*1e-6);}
  // The virtual sensor clock integrates its current period, so a drift change
  // alters the rate from now on and never re-times frames already produced.
  uint64_t framesAt(int64_t now) {
    const double period=periodUs();
    if(!faults.freeze.load())phase_+=double(now-lastUs_)/period;
    lastUs_=now;
    const uint32_t jitter=faults.sensorJitterUs.load();
    const double ready=phase_-(jitter?double(noise_.next()%jitter)/period:0);
    produced_=std::max(produced_,ready>0?uint64_t(ready):0);return produced_;
  }
  // Host time at which frame k was produced (recent frames only).
  int64_t frameUs(uint64_t k)const{return lastUs_-int64_t((phase_-double(k))*periodUs());}
  static unsigned frameBytes(uint64_t k){return k%2==0?13:7;}
  uint32_t pendingBytes(uint64_t from,uint64_t to)const {
    // Accelerometer frames sit on even indices: 13 bytes, gyro-only 7 bytes.
    const uint64_t n=to>from?to-from:0;const uint64_t evens=(to+1)/2-(from+1)/2;
    return uint32_t(evens*13+(n-evens)*7);
  }
  bool readLength(uint8_t *data,size_t n) {
    if(const uint32_t jitter=faults.jitterUs.load())esp_rom_delay_us(noise_.next()%jitter);
    const bool real=realRead(Bmi270Fifo::FIFO_LENGTH_0,data,n);
    if(take(faults.lengthFail)){++counters.lengthFails;return false;}
    if(!real || n<2)return real;
    latched_=framesAt(esp_timer_get_time());
    unsigned bytes=pendingBytes(consumed_,latched_)+(skipPending_?2:0);
    if(take(faults.oversize)){bytes=0x3fff;++counters.oversizes;}
    else if(take(faults.overflow)){bytes=2012;++counters.overflows;}
    else if(bytes>=Bmi270Fifo::CAPACITY){bytes=2012;++counters.naturalOverflows;}
    data[0]=uint8_t(bytes);data[1]=uint8_t((bytes>>8)&0x3f);
    return true;
  }
  bool readData(uint8_t *data,size_t n) {
    if(take(faults.dataFail)){realRead(Bmi270Fifo::FIFO_DATA,data,n);++counters.dataFails;return false;}
    const bool real=realRead(Bmi270Fifo::FIFO_DATA,data,n);
    if(take(faults.stall)){delay(faults.stallMs.load());++counters.stalls;}
    if(!real)return false;
    if(const uint32_t frames=faults.skip.exchange(0)) {
      // Frames lost inside the sensor are reported by one skip frame.
      consumed_=std::min(latched_,consumed_+frames);skipPending_=std::min<uint32_t>(255,skipPending_+frames);++counters.skips;
    }
    size_t i=0;
    if(skipPending_ && i+2<=n){data[i++]=Bmi270Fifo::SKIP;data[i++]=uint8_t(skipPending_);skipPending_=0;}
    Value base;float noiseG,noiseDps;int64_t offset;uint32_t length;
    portENTER_CRITICAL(&mux_);base=base_;noiseG=noiseG_;noiseDps=noiseDps_;offset=offsetUs_;length=profileLength_;portEXIT_CRITICAL(&mux_);
    const bool profiled=mode_.load()==Mode::Profile && profile_;
    const int64_t pull=pullStartUs.load();
    const float accelCounts=32768/accelRange().fullScaleG(),gyroCounts=32768/StickS3AccelRange::GYRO_FULL_SCALE_DPS;
    while(consumed_<latched_ && i+frameBytes(consumed_)<=n) {
      const uint64_t k=consumed_++;
      const int64_t at=frameUs(k);
      Value v=base;
      if(profiled && pull && at>=pull+offset) {
        const uint64_t j=uint64_t(at-pull-offset)/PROFILE_PERIOD_US;
        if(j<length)for(unsigned axis=0;axis<3;++axis){v.a[axis]=profile_[j].a[axis]*.001f;v.g[axis]=profile_[j].g[axis]*.1f;}
      }
      for(unsigned axis=0;axis<3;++axis){v.a[axis]+=noiseG*noise_.unit();v.g[axis]+=noiseDps*noise_.unit();}
      const bool accel=k%2==0;
      data[i++]=accel?Bmi270Fifo::ACC_GYR:Bmi270Fifo::GYR;
      putAxes(data+i,v.g,gyroCounts);i+=6;
      if(accel){putAxes(data+i,v.a,accelCounts);i+=6;}
      ++counters.frames;
    }
    while(i<n)data[i++]=Bmi270Fifo::OVER_READ;
    if(n && take(faults.corrupt)){data[0]=0x13;++counters.corrupts;}
    if(n>3 && take(faults.garble)){data[1+noise_.next()%(n-1)]^=0x7f;++counters.garbles;}
    return true;
  }
  // Inverse of Bmi270Fifo::boardAxes: board X=+rawY, Y=-rawX, Z=+rawZ.
  static void putAxes(uint8_t *p,const float *board,float counts) {
    const float raw[3]={-board[1]*counts,board[0]*counts,board[2]*counts};
    for(unsigned j=0;j<3;++j) {
      const int32_t v=int32_t(std::lround(std::max(-32768.0f,std::min(32767.0f,raw[j]))));
      p[2*j]=uint8_t(v);p[2*j+1]=uint8_t(uint16_t(v)>>8);
    }
  }
  static bool take(std::atomic<uint32_t> &count) {
    uint32_t value=count.load();
    while(value && !count.compare_exchange_weak(value,value-1)){}
    return value!=0;
  }
  portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;
  std::atomic<Mode> mode_{Mode::Pass};
  Value base_;float noiseG_=0,noiseDps_=0;
  Frame *profile_=nullptr;uint32_t profileLength_=0;int64_t offsetUs_=0;
  bool active_=false;int64_t lastUs_=0;double phase_=0;uint64_t consumed_=0,produced_=0,latched_=0;uint32_t skipPending_=0;
  Noise noise_;
};
inline Imu &imu(){static Imu instance;return instance;}
}
