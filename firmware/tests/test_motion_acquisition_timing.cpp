// Run the actual acquisition owner, FIFO parser, timeline and fusion. Only
// the clock, RTOS and I2C transport are simulated; no hardware claims follow.
#include "esp_timer.h"
#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace Bmi270Fifo {
bool readRegisters(uint8_t,uint8_t*,size_t);
bool writeRegister(uint8_t,uint8_t);
}
#include "motion_acquisition.h"
namespace {
struct Finished {};
std::vector<uint8_t> fifo;
int64_t lengthUs=100,burstUs=48000;
uint64_t countedAt=0;
unsigned reads=0,flushes=0;
bool failLength=false,failData=false;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void batch(unsigned frames,bool accel=true){
  fifo.clear();
  for(unsigned i=0;i<frames;++i){
    fifo.push_back(accel && i%2==0?Bmi270Fifo::ACC_GYR:Bmi270Fifo::GYR);
    for(unsigned j=0;j<6;++j)fifo.push_back(0);
    if(accel && i%2==0){for(unsigned j=0;j<4;++j)fifo.push_back(0);fifo.push_back(0);fifo.push_back(16);}
  }
}
void reset(){
  MotionFake::now=1000000;MotionFake::gates.clear();MotionFake::task={};MotionFake::afterPoll={};
  reads=flushes=0;lengthUs=100;burstUs=48000;failLength=failData=false;batch(120);
}
void poll(){try{MotionFake::task();}catch(const Finished&) {}}
void quietTest(){
  // No gate exists if the sensor never started.
  {StickS3MotionAcquisition inactive;inactive.quiet();inactive.endQuiet();}
  for(int64_t heldUs:{0,19000,90000,120000}){
    reset();StickS3MotionAcquisition owner;check(owner.begin(),"begin failed");
    const int64_t releaseAt=MotionFake::now+heldUs;
    MotionFake::gates[0]->availableAt=releaseAt;
    owner.quiet();
    std::cout<<"quiet: read holds gate "<<heldUs<<" us, checkpoint starts after "<<MotionFake::now-1000000<<" us\n";
    check(MotionFake::now>=releaseAt,"quiet returned while a FIFO read still owns the bus");
    MotionFake::afterPoll=[]{throw Finished{};};poll();
    check(reads==0,"a FIFO read started during the checkpoint");
    owner.endQuiet();poll();check(reads==1,"endQuiet did not resume reads");
  }
  // Waiting out a read must not consume the caller's requested quiet window.
  reset();StickS3MotionAcquisition owner;owner.begin();
  MotionFake::gates[0]->availableAt=MotionFake::now+90000;
  owner.quiet(10);MotionFake::afterPoll=[]{throw Finished{};};poll();
  check(reads==0,"short quiet window expired while waiting for the read");
  MotionFake::now+=10000;poll();check(reads==1,"quiet window failed to expire");
  owner.quiet(UINT32_MAX);MotionFake::now+=StickS3MotionAcquisition::QUIET_MAX_MS*1000-1;
  poll();check(reads==1,"capped quiet window ended early");
  ++MotionFake::now;poll();check(reads==2,"missing-end safety cap failed");
}
void timestampTest(){
  for(int64_t transfer:{48000,0,23000,58000,90000}){
    reset();burstUs=transfer;
    const unsigned frames=transfer==90000?198:120,nextFrames=transfer==90000?19:12;
    batch(frames); // 1980 bytes is just below the 1984-byte overflow threshold.
    StickS3MotionAcquisition owner;owner.begin();
    unsigned polls=0;uint64_t last=0;
    MotionFake::afterPoll=[&]{
      auto s=owner.snapshot();
      if(++polls==1){
        std::cout<<"timestamp: "<<transfer<<" us burst, length captured at "<<countedAt<<", newest sample "<<s.pairedUs<<"\n";
        check(s.pairedSamples==frames && s.fifoReads==1 && !s.fifoErrors && !s.fifoStalls,"valid catch-up burst was lost");
        check(s.pairedUs<=countedAt && countedAt-s.pairedUs<5000,"FIFO samples were shifted by burst transfer time");
        last=s.pairedUs;
        // Frames appended during that burst are returned on the following poll.
        MotionFake::now=transfer==90000?1095000:1060000;batch(nextFrames);burstUs=4800;
      }else{
        check(s.pairedSamples==frames+nextFrames && s.pairedUs>last && s.pairedUs<=countedAt,"catch-up poisoned subsequent timestamps");
        check(s.fifoAnchors==0 && s.gaps==0,"valid backlog introduced an artificial timeline gap");
        throw Finished{};
      }
    };poll();
  }
  reset();batch(1);fifo[0]=Bmi270Fifo::ACC;fifo.resize(7);burstUs=280;
  StickS3MotionAcquisition accelOnly;accelOnly.begin();MotionFake::afterPoll=[]{throw Finished{};};poll();
  check(accelOnly.snapshot().accelUs<=countedAt,"accelerometer-only timestamp includes transfer delay");
  // Existing transport failure/stall behavior must remain intact.
  for(unsigned failure=0;failure<3;++failure){
    reset();failLength=failure==0;failData=failure==1;burstUs=100000;
    StickS3MotionAcquisition owner;owner.begin();MotionFake::afterPoll=[]{throw Finished{};};poll();
    auto s=owner.snapshot();check(s.pairedSamples==0,"failed/stalled data reached fusion");
    check(failure==0?s.fifoLengthErrors==1:failure==1?s.fifoDataErrors==1:s.fifoStalls==1,"transport diagnostics changed");
    check(flushes==(failure==0?0u:1u),"FIFO recovery changed");
  }
}
}
namespace Bmi270Fifo {
bool readRegisters(uint8_t reg,uint8_t* out,size_t n){
  if(reg==FIFO_LENGTH_0){
    MotionFake::now+=lengthUs;countedAt=MotionFake::now;
    if(failLength)return false;
    out[0]=uint8_t(fifo.size());out[1]=uint8_t(fifo.size()>>8);return true;
  }
  if(reg!=FIFO_DATA || n!=fifo.size())throw std::runtime_error("unexpected bus read");
  ++reads;MotionFake::now+=burstUs;if(failData)return false;
  std::memcpy(out,fifo.data(),n);return true;
}
bool writeRegister(uint8_t reg,uint8_t value){
  check(reg==CMD && value==FLUSH,"unexpected bus write");++flushes;return true;
}
}
int main(int argc,char** argv){
  try{
    if(argc==1 || std::string(argv[1])=="quiet")quietTest();
    if(argc==1 || std::string(argv[1])=="timestamp")timestampTest();
    std::cout<<"PASS: acquisition timing regressions\n";
  }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
