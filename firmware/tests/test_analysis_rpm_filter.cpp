#include "analysis_rpm_filter.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>
using AnalysisMode::RpmFilter;
int main(){
 RpmFilter f;assert(f.update(6000,0.2f)==6000);
 assert(f.update(7600,0.4f)==0);assert(std::fabs(f.update(6000,0.6f)-6000)<1);
 assert(f.update(4200,0.8f)==0);assert(std::fabs(f.update(6000,1.f)-6000)<1);
 // Three consistent readings establish a genuine step rather than rejecting it forever.
 assert(f.update(4800,1.2f)==0);assert(f.update(4780,1.4f)==0);
 assert(std::fabs(f.update(4760,1.6f)-4780)<1);
 assert(f.update(0,1.8f)==0);assert(f.update(0,3.f)==0);
 assert(f.update(3500,3.2f)==0);assert(f.update(3490,3.4f)==0);assert(f.update(3480,3.6f)>0);
 assert(f.update(9999,3.6f)==0);assert(f.update(9999,3.5f)==0);
 assert(f.update(std::numeric_limits<float>::quiet_NaN(),3.8f)==0);
 assert(f.update(5000,std::numeric_limits<float>::infinity())==0);
 RpmFilter next;assert(next.update(9000,0.2f)==9000); // New launch has no old slope.
 // Noise reduction on a falling signal at both actual acquisition cadences.
 for(float dt:{0.1f,0.2f}){
   RpmFilter smooth;double rawError=0,filteredError=0;unsigned n=0;
   for(unsigned i=0;i<500;++i){float t=(i+1)*dt,truth=9000-70*t,raw=truth+50*std::sin(i*2.1f);float out=smooth.update(raw,t);assert(out>0);
     if(i>20){rawError+=(raw-truth)*(raw-truth);filteredError+=(out-truth)*(out-truth);++n;}}
   assert(filteredError<rawError*0.65);
   std::cout<<"dt="<<dt<<" raw RMS="<<std::sqrt(rawError/n)<<" filtered RMS="<<std::sqrt(filteredError/n)<<" RPM\n";
 }
 // Variable timestamps, withheld measurements, and a moderate valid upward fluctuation.
 RpmFilter variable;assert(variable.update(6000,.2f)>0);assert(variable.update(6100,.31f)>6000);
 assert(variable.update(0,.51f)==0);assert(variable.update(6000,.72f)>0);
 std::cout<<"PASS: spikes, real steps, gaps, reset, invalid timestamps, noise and variable cadence\n";
}
