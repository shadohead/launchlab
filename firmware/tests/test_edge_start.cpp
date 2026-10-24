// Independent generated-reference oracle: N raw rising reference edges contain
// N-1 complete eligible periods. Both start polarities end at constant LOW,
// so terminal idle adds no closing rise. Four markers are the minimum for a
// three-complete-period result. Optical timing is not shaft-RPM calibration.
#include "../LaunchLabMini/sensor_profile.h"
#include "../LaunchLabMini/rpm_estimator.h"
#include <cmath>
#include <iostream>
#include <stdint.h>
#include <vector>
struct Trace {
 AnalogTachometer d;
 uint64_t samples=0,confirmedUs=0;
 std::vector<uint64_t> references;
 Trace(){StickS3SensorProfile::configure(d);}
 void level(unsigned us,uint16_t value){
  for(unsigned n=0;n<us/20;++n){const auto before=d.phase;d.feed(value);++samples;
   if(!confirmedUs && before==AnalogTachometer::Phase::Ready && d.phase==AnalogTachometer::Phase::Launch)confirmedUs=samples*20;
  }
 }
 void cycles(unsigned period,unsigned turns,float lowDuty,bool highFirst,unsigned amplitude,bool record){
  const unsigned low=unsigned(std::lround(period*lowDuty/20))*20,high=period-low;
  for(unsigned n=0;n<turns;++n){
   if(highFirst){if(record)references.push_back(samples*20);level(high,1200+amplitude);level(low,1200);}
   else {level(low,1200);if(record)references.push_back(samples*20);level(high,1200+amplitude);}
  }
 }
};
static bool generatedRpm(float rpm,unsigned period,float late=100){
 return std::isfinite(rpm) && rpm>=60000000.0f/(period+late)-.01f && rpm<=60000000.0f/(period-100)+.01f;
}
// After idle there is no swing history, so a burst's first rise is judged
// against the noise band, near the start of the rolling mean's 500 us ramp;
// later rises wait for 70% of the swing. Its first period reads up to
// 0.7 x 500 us (+ a sample) long, and the first window's three-turn reading
// (the one kept on these short bursts) includes it; the 100 us grid rounded
// it to 300 us until 0.20.1.
constexpr float FIRST_TURN_LATE=(.7f*500+20)/3;
struct Result {bool pass=false;unsigned launches=0;uint64_t confirmed=0,fourth=0,first=0,last=0;float three=0,single=0;};
static Result burst(Trace &t,unsigned period,unsigned turns,float duty,bool highFirst){
 const unsigned before=t.d.launches;
 t.references.clear();t.confirmedUs=0;
 t.cycles(period,turns,duty,highFirst,1000,true);t.level(1400000,1200);
 Result r;r.launches=t.d.launches-before;r.confirmed=t.confirmedUs;
 r.first=t.references.front();r.last=t.references.back();r.fourth=turns>=4?t.references[3]:0;
 r.three=t.d.sustainedPeakRpm;r.single=t.d.singlePeakRpm;
 const bool count=r.launches==(turns>=4?1u:0u),ready=t.d.phase==AnalogTachometer::Phase::Ready;
 if(turns<4)r.pass=count && ready && !r.confirmed;
 else {
  // A 25-sample mean spans500us and evaluation cadence is100us. Confirmation
  // cannot precede the fourth real marker and must occur within600us of it.
  const bool latency=r.confirmed>=r.fourth && r.confirmed<=r.fourth+600;
  // The timestamp compensates by260us; a conservative300us early/600us late
  // envelope encloses the generated first and last real reference positions.
  const bool boundaries=int64_t(t.d.burstStartUs)>=int64_t(r.first)-300 && t.d.burstStartUs<=r.first+600 &&
    int64_t(t.d.burstEndUs)>=int64_t(r.last)-300 && t.d.burstEndUs<=r.last+600;
  r.pass=count && ready && t.d.resultValid && t.d.ended==AnalogTachometer::End::Gap && latency && boundaries &&
   generatedRpm(RpmEstimator::select(RpmEstimator::Mode::ThreeTurn,r.three,r.single),period,FIRST_TURN_LATE) &&
   generatedRpm(RpmEstimator::select(RpmEstimator::Mode::SingleTurn,r.three,r.single),period);
 }
 return r;
}
int main(){unsigned failures=0;
 for(bool prehistory:{false,true})for(unsigned period:{6000u,10000u})for(unsigned turns:{3u,4u,5u,8u})for(float duty:{.15f,.5f,.85f})for(bool highFirst:{false,true}){
  Trace t;t.level(1100000,1200);
  // These old30-count marks are below Standard's80-count confirmation floor.
  // Their references are not eligible members of the following strong burst.
  if(prehistory){t.cycles(10000,2,.5f,false,30,false);t.level(1100000,1200);}
  if(t.d.phase!=AnalogTachometer::Phase::Ready || t.d.launches)return 2;
  const auto first=burst(t,period,turns,duty,highFirst),repeat=burst(t,period,turns,duty,highFirst);
  const bool pass=first.pass && repeat.pass;if(!pass)++failures;
  std::cout<<"{\"weak_prehistory\":"<<(prehistory?"true":"false")<<",\"period_us\":"<<period<<",\"raw_reference_count\":"<<turns<<",\"complete_periods\":"<<turns-1<<",\"low_duty\":"<<duty<<",\"high_first\":"<<(highFirst?"true":"false")
   <<",\"expected_launches\":"<<(turns>=4?1:0)<<",\"launches\":"<<first.launches<<",\"first_raw_reference_us\":"<<first.first<<",\"fourth_raw_reference_us\":"<<first.fourth<<",\"last_raw_reference_us\":"<<first.last
   <<",\"observed_confirmation_us\":"<<first.confirmed<<",\"three_rpm\":"<<first.three<<",\"single_rpm\":"<<first.single
   <<",\"first_pass\":"<<(first.pass?"true":"false")<<",\"repeat_launches\":"<<repeat.launches<<",\"repeat_fourth_raw_reference_us\":"<<repeat.fourth<<",\"repeat_confirmation_us\":"<<repeat.confirmed
   <<",\"repeat_three_rpm\":"<<repeat.three<<",\"repeat_single_rpm\":"<<repeat.single<<",\"repeat_pass\":"<<(repeat.pass?"true":"false")<<",\"pass\":"<<(pass?"true":"false")<<"}\n";
 }
 return failures?1:0;
}
