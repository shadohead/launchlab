#include "analysis_result_service.h"
#include <cassert>
#include <iostream>
int main(){
  AnalysisResult::Service service;AnalysisMode::Session session;
  uint16_t slots[3000]={},raw[3000]={};MicTrace::Trace trace(slots,3000,raw);
  session.begin(1,true,10000,1000000);trace.begin(1,10000);
  for(unsigned i=0;i<150;++i){MicTrace::Step s;s.launch=1;s.index=i;s.rpm=9000-i*20;s.rawRpm=s.rpm+10;trace.add(s);}
  assert(trace.rawRpm(10)==8810 && trace.rejectedRpm(10)==0);
  session.stop(101000000);assert(service.tick(true,session,&trace) && service.busy());
  unsigned tries=0;while(service.busy() && ++tries<10000){service.tick(true,session,&trace);vTaskDelay(1);}
  assert(service.ready() && !service.busy() && !service.recordPending());
  assert(allocations==1); // Only the displayable result remains after fitting.
  session.nextResult();service.tick(true,session,&trace);assert(service.recordPending());
  assert(service.input()->count==150 && service.input()->samples[10].rpm==8810);
  Decay::Results saved;unsigned selected=9;assert(Decay::Record::decode(service.record(),saved,selected) && selected==unsigned(std::max(0,saved.best)));
  assert(saved.opticalAnchor && saved.manualAnchor && !saved.models[0].stopErrorValid);
  service.recordHandled();session.nextResult();service.tick(true,session,&trace);assert(!service.recordPending());
  assert(service.winner() && service.details()->complete);
  service.tick(false,session,&trace);assert(!service.ready() && allocations==0);
  // Exit cancels a running calculation and safely reclaims the job, stack and TCB.
  service.tick(true,session,&trace);service.tick(false,session,&trace);
  tries=0;while(service.busy() && ++tries<10000){service.tick(false,session,&trace);vTaskDelay(1);}assert(allocations==0);
  failAllocation=true;service.tick(true,session,&trace);assert(service.failed() && !service.busy() && allocations==0);
  failAllocation=false;
  for(unsigned failure=1;failure<=4;++failure){
    service.tick(false,session,&trace);
    failOnAllocation=failure;
    service.tick(true,session,&trace);
    assert(service.failed() && !service.busy() && allocations==0);
    failOnAllocation=0;
  }
  service.tick(false,session,&trace);failTask=true;service.tick(true,session,&trace);assert(service.failed() && allocations==0);failTask=false;
  service.tick(false,session,&trace);session.begin(2,true,9000,2000000);session.tick(302000000);session.nextResult();trace.begin(2,9000);
  service.tick(true,session,&trace);tries=0;while(service.busy() && ++tries<10000){service.tick(true,session,&trace);vTaskDelay(1);}
  assert(service.ready() && !service.input()->hasStop && !service.results()->actualStopValid);
  service.tick(false,session,&trace);assert(allocations==0);
  std::cout<<"PASS: asynchronous result ownership, raw evidence, selection persistence, cancellation, timeout and allocation failures\n";
}
