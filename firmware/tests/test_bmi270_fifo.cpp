#include "bmi270_fifo.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using namespace Bmi270Fifo;

static void put(std::vector<uint8_t> &v,const int16_t *x){for(unsigned j=0;j<3;++j){v.push_back(uint8_t(x[j]));v.push_back(uint8_t(uint16_t(x[j])>>8));}}
static void accGyr(std::vector<uint8_t> &v,const int16_t *g,const int16_t *a){v.push_back(ACC_GYR);put(v,g);put(v,a);}
static void gyr(std::vector<uint8_t> &v,const int16_t *g){v.push_back(GYR);put(v,g);}
int main(){
  // Header-mode stream: gyro precedes accel inside a frame; control frames skipped.
  const int16_t g1[3]={100,-200,300},a1[3]={-4096,2048,16384},g2[3]={-32768,32767,0};
  std::vector<uint8_t> v;accGyr(v,g1,a1);gyr(v,g2);v.push_back(SKIP);v.push_back(9);
  v.push_back(TIME);v.push_back(1);v.push_back(2);v.push_back(3);
  v.push_back(INPUT_CONFIG);for(unsigned i=0;i<4;++i)v.push_back(0);accGyr(v,g2,a1);
  std::vector<Frame> frames;unsigned skipped=0;
  assert(parse(v.data(),v.size(),[&](const Frame &f){frames.push_back(f);},[&](unsigned n){skipped+=n;}));
  assert(frames.size()==3 && skipped==9);
  assert(frames[0].gyro && frames[0].accel && frames[0].g[1]==-200 && frames[0].a[2]==16384);
  assert(frames[1].gyro && !frames[1].accel && frames[1].g[0]==-32768 && frames[1].g[1]==32767);
  // Over-read padding ends the data; unknown headers and truncation are errors.
  std::vector<uint8_t> padded=v;padded.push_back(OVER_READ);padded.push_back(0x00);
  unsigned count=0;assert(parse(padded.data(),padded.size(),[&](const Frame &){++count;},[](unsigned){}) && count==3);
  std::vector<uint8_t> truncated(v.begin(),v.begin()+10);
  assert(!parse(truncated.data(),truncated.size(),[](const Frame &){},[](unsigned){}));
  const uint8_t junk[]={0x13,0,0};assert(!parse(junk,3,[](const Frame &){},[](unsigned){}));
  std::cout<<"parser: gyro/accel order, gyro-only, skip/time/config frames, padding, truncation and junk\n";

  // StickS3 board axes match M5Unified: X=+Y, Y=-X, Z=+Z.
  float out[3];boardAxes(a1,16.0f/32768,out);
  assert(out[0]==1 && out[1]==2 && out[2]==8);
  std::cout<<"axes: StickS3 BMI270 mapping X=+Y, Y=-X, Z=+Z\n";

  // Timeline: a sensor clock 1% slow, read every 5 ms host with 0-3 frames and
  // jitter, stays strictly increasing, within 2 ms of truth and never anchors.
  Timeline line;double sensor=0;uint64_t last=0;double worst=0;unsigned seed=1;
  const double truePeriod=5050;std::vector<double> produced;
  for(uint64_t host=100000;host<60000000;host+=5000) {
    seed=seed*1103515245+12345;const uint64_t read=host+(seed>>16)%900;
    while(sensor+truePeriod<=double(read))produced.push_back(sensor+=truePeriod);
    line.begin(read,unsigned(produced.size()));
    for(double t:produced) {
      const uint64_t stamp=line.next();assert(stamp>last);last=stamp;
      // Stamps follow read time: the newest frame is up to one period old.
      if(host>2000000)worst=std::max(worst,std::fabs(double(stamp)-t-truePeriod/2));
    }
    produced.clear();
  }
  assert(line.anchors()==0 && worst<2000 && std::fabs(line.periodUs()-truePeriod)<20);
  std::cout<<"timeline: 1% clock error tracked, jittered reads, max error "<<worst<<" us, period "<<line.periodUs()<<" us\n";

  // A 300 ms read stall delivers 60 frames at once: same spacing, no anchor.
  Timeline stall;uint64_t t=1000000,prev=0;
  for(unsigned i=0;i<100;++i,t+=5000){stall.begin(t,1);prev=stall.next();}
  t+=295000;stall.begin(t,60);uint64_t first=stall.next();
  assert(first-prev>4500 && first-prev<5500 && stall.anchors()==0);
  for(unsigned i=1;i<60;++i){const uint64_t s=stall.next();assert(s-first>4000 && s-first<6000);first=s;}
  std::cout<<"timeline: 300 ms host stall delivers buffered frames at sensor spacing without a gap\n";

  // Overflow: skipped frames advance time, so fusion sees a real gap.
  Timeline over;t=1000000;for(unsigned i=0;i<100;++i,t+=5000){over.begin(t,1);prev=over.next();}
  over.skip(40);t+=200000;over.begin(t,1);first=over.next();assert(first-prev>=200000);
  // Reconfiguration (flushed FIFO, 120 ms of nothing then fresh frames) re-anchors forward.
  Timeline flush;t=1000000;for(unsigned i=0;i<100;++i,t+=5000){flush.begin(t,1);prev=flush.next();}
  flush.reset();t+=120000;flush.begin(t,2);first=flush.next();
  assert(first>prev && first-prev>35000 && flush.next()>first);
  Timeline gap;t=1000000;for(unsigned i=0;i<100;++i,t+=5000){gap.begin(t,1);prev=gap.next();}
  t+=200000;gap.begin(t,1);first=gap.next();assert(gap.anchors()==1 && first-prev>35000);
  std::cout<<"PASS: BMI270 FIFO parsing, StickS3 axes and sample timeline (drift, stall, overflow, resync)\n";
}
