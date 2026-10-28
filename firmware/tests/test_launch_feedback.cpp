#include "../LaunchLabMini/launch_feedback.h"
#include <cassert>
#include <iostream>
int main() {
  LaunchFeedback view;view.toggle(100,false);assert(!view.shown());
  view.show(200);assert(view.shown() && view.elapsed(1600)==1400 && !view.tick(5199));assert(view.tick(5200) && !view.shown() && view.elapsed(5200)==0);
  view.toggle(6000,true);assert(view.shown());view.toggle(6001,true);assert(!view.shown());
  view.show(7000);view.show(10000);assert(!view.tick(12000) && view.shown());assert(view.tick(15000));
  view.show(UINT32_MAX-1000);assert(view.elapsed(3998)==4999 && !view.tick(3998) && view.shown());assert(view.tick(3999));
  view.show(1);view.clear();assert(!view.tick(9000) && !view.shown());
  // 0.20.5: after a pull the replay plays twice, each pass holding its end
  // 500 ms, then holds the end; a recap opened with A loops and stays open.
  using R=LaunchFeedback::Replay;
  view.show(0,R::Twice,1000);assert(view.replay()==R::Twice);
  assert(view.position(0)==0 && view.position(999)==999 && view.position(1000)==1000 && view.position(1499)==1000);
  assert(view.position(1500)==0 && view.position(2499)==999 && view.position(2500)==1000 && view.position(60000)==1000);
  assert(!view.tick(4999) && view.tick(5000));
  view.show(0,R::Twice,3000);assert(!view.tick(2*3000+1499) && view.tick(2*3000+1500));
  view.toggle(0,true,1000);assert(view.shown() && view.replay()==R::Loop);
  assert(view.position(2500)==1000 && view.position(3000)==0 && view.position(3600)==600 && !view.tick(3600000));
  // A loop holds automatic writes back only through its first pass.
  view.replayPresented(0,true,0);assert(view.protects(1499) && !view.protects(1500));
  view.show(5,R::Loop,1000);view.show(10);assert(view.replay()==R::Twice);
  // A pending capture has no replay yet: its clock runs straight through.
  assert(view.position(1234)==1234);
  view.replayPresented(10,true,0);assert(view.protects(60000));view.clear();assert(!view.protects(60000));
  std::cout<<"PASS: automatic five-second feedback, two passes after a pull, a loop opened with A, manual recent/live toggle, absent result, new-pull restart and timer wrap\n";
}
