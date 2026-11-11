#include "analysis_mode.h"
#include "controls.h"
#include "mic_trace.h"
#include <cassert>
#include <cstring>
#include <iostream>
int main(){
  AnalysisMode::Session s;char text[32];s.format(text,sizeof(text),9000000);assert(!strcmp(text,"00:00:000"));
  assert(!s.stop(1) && !s.begin(1,false,6000,1000000));
  assert(s.begin(1,true,6000,1000000));s.format(text,sizeof(text),84456000);assert(!strcmp(text,"01:23:456"));
  assert(!s.begin(1,true,9999,2000000) && s.rpm()==6000);
  assert(s.stop(84456000) && !s.stop(90000000));assert(s.elapsedUs(100000000)==83456000);
  assert(s.hasManualStop() && s.nextResult() && s.results() && s.tablePage()==0);
  for(unsigned i=1;i<=4;++i){assert(s.nextResult() && s.tablePage()==i);}
  s.toggleReplay(90000000);assert(!s.results() && !s.replay());
  assert(s.begin(2,true,7000,100000000) && s.running());assert(!s.tick(100000001));
  assert(s.tick(100000000+AnalysisMode::Session::LIMIT_US+999999));s.format(text,sizeof(text),900000000);assert(!strcmp(text,"05:00:000"));
  assert(!s.hasManualStop() && !s.results());
  s.toggleReplay(400000000);assert(s.replay());s.toggleReplay(401000000);assert(!s.replay());
  s.clear();assert(!s.running() && !s.number());
  assert(s.begin(3,true,8000,(1ULL<<32)*1000));assert(s.elapsedUs((1ULL<<32)*1000+123000)==123000);
  AnalysisMode::Session frames;frames.begin(1,true,6000,1000000);
  assert(!frames.needsFrame(1100000,99,1000) && frames.needsFrame(1100000,100,1000));
  frames.toggleReplay(1000000);
  assert(!frames.needsFrame(1100000,49,1000) && frames.needsFrame(1100000,50,1000));
  assert(frames.needsFrame(2025000,50,1000)); // Draw the final endpoint once.
  assert(!frames.needsFrame(2100000,50,1000)); // Hold endpoint.
  assert(frames.needsFrame(2500000,500,1000)); // Start next loop.
  assert(!frames.needsFrame(2600000,100,0)); // Missing motion is static.
  frames.toggleReplay(2600000);frames.stop(2600000);assert(!frames.needsFrame(4000000,1000,1000));
  Controls::State c;assert(c.holdB()==Controls::Effect::None && c.page==Controls::Page::Analysis);
  assert(c.tapA()==Controls::Effect::AnalysisReplay && c.tapB()==Controls::Effect::AnalysisStop);
  Controls::State restored;uint8_t view=0;assert(Controls::decodeWake(Controls::encodeWake(c,0),restored,view) && restored.page==Controls::Page::Main);
  Controls::Gesture gesture;assert(!gesture.click(true,false,false));assert(!gesture.click(false,true,false));assert(!gesture.click(false,false,true));
  assert(c.holdB()==Controls::Effect::None && c.page==Controls::Page::Main);
  MicTrace::StoredTrace trace;trace.begin(1,6000);MicTrace::Step step;step.launch=1;step.rpm=5800;trace.add(step);trace.freeze();
  step.index=1;step.rpm=5700;trace.add(step);assert(trace.count()==1 && trace.latestRpm()==5800);
  trace.begin(2,7000);step.launch=2;step.index=0;trace.add(step);assert(trace.count()==1 && trace.latestRpm()==5700);
  uint16_t slots[MicTrace::ANALYSIS_READINGS]={};MicTrace::Trace extended(slots,MicTrace::ANALYSIS_READINGS);extended.begin(9,6000);
  MicTrace::Follower longFollow(true),normal;MicRpm::Reading quiet;
  for(unsigned i=0;i<MicTrace::ANALYSIS_READINGS;++i){auto q=longFollow.next(9,quiet,MicTrace::Trace::seconds(i));extended.add(q);assert(longFollow.done()==(i+1==MicTrace::ANALYSIS_READINGS));}
  assert(extended.count()==3000);normal.next(1,quiet,MicTrace::Trace::seconds(80));assert(normal.done());
  std::cout<<"PASS: Analysis timer, rollover, stop, relaunch, safety limit, navigation, wake and frozen graph\n";
}
