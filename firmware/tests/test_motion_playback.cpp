// Host regression for the actual .ino feedback call and redraw predicate.
// check_motion_playback.py extracts those two blocks verbatim at test time.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>
static constexpr uint16_t TFT_BLACK=0,TFT_WHITE=0xffff,TFT_DARKGREY=0x7bef,TFT_CYAN=0x07ff,TFT_ORANGE=0xfd20;
static constexpr int top_center=0,top_right=1,top_left=2;
namespace fonts {static int Font4=4,Font6=6;}
namespace lgfx {
class LGFXBase {
public:
  std::vector<int> primitives;
  std::vector<int> triangles,usbLines;
  std::vector<std::pair<int,int>> bubbles;
  std::vector<std::pair<int,int>> liveBubbles;
  std::vector<int> radii;
  int width()const{return 135;}
  void setTextDatum(int){} void setTextColor(uint16_t,uint16_t){}
  int textWidth(const char *s,const int*)const{unsigned n=0;while(s[n])++n;return int(n)*13;}
  void drawString(const char*,int,int,int){}
  void drawLine(int a,int b,int c,int d,uint16_t ink){append({1,a,b,c,d,ink});if(ink==TFT_WHITE)usbLines.insert(usbLines.end(),{a,b,c,d});}
  void drawFastHLine(int a,int b,int c,uint16_t ink){drawLine(a,b,a+c-1,b,ink);}
  void drawFastVLine(int a,int b,int c,uint16_t ink){drawLine(a,b,a,b+c-1,ink);}
  void drawRect(int a,int b,int c,int d,uint16_t ink){append({2,a,b,c,d,ink});}
  void drawCircle(int a,int b,int c,uint16_t ink){append({3,a,b,c,ink});}
  void fillCircle(int a,int b,int c,uint16_t ink){append({4,a,b,c,ink});radii.push_back(c);if(c==4)bubbles.emplace_back(a,b);if(c==5)liveBubbles.emplace_back(a,b);}
  void fillTriangle(int a,int b,int c,int d,int e,int f,uint16_t ink){append({5,a,b,c,d,e,f,ink});triangles.insert(triangles.end(),{a,b,c,d,e,f,ink});}
private:
  void append(std::initializer_list<int> values){primitives.insert(primitives.end(),values);}
};
}
#include "../LaunchLabMini/motion_ui.h"
#include "../LaunchLabMini/appearance.h"
#include "../LaunchLabMini/milestones.h"
#include "../LaunchLabMini/theme_library.h"
#include "../LaunchLabMini/screens_ui.h"
static Appearance::Celebration bestEffect;static Palette::Colors ui(){return Themes::builtIn(Appearance::Theme::Classic);}
#include "../LaunchLabMini/launch_feedback.h"
#include "fixtures/tilt_launch96.h"
static uint32_t hostNow=0;
static uint32_t millis(){return hostNow;}
static LaunchFeedback launchFeedback;
static LaunchMotion::Trace latestMotion,referenceMotion,demoMotion,demoReference;
static AnalogTachometer::Curve latestCurve;
struct PracticeFake {}practice;
struct StoreFake {bool pending(const PracticeFake&)const{return false;}}practiceStore;
static bool motionPending=false,motionDemo=false,tournamentMode=false,diagnostics=false,historyPage=false,screenReady=true;
static bool displayFlipped=false;
static StickS3Level level;
static StickS3Level::Reading drawnLevel;
struct {struct {uint8_t rotation=2;uint8_t getRotation()const{return rotation;}}Display;}M5;
#include "motion_playback_wiring.h"
static unsigned failures=0,frames=0;
static void require(bool ok,const char *message) {
  if(!ok){if(failures<12)std::cerr<<"FAIL: "<<message<<'\n';++failures;}
}
static LaunchMotion::Trace capture(uint32_t durationMs) {
  auto ring=std::make_unique<LaunchMotion::Ring>();
  const uint64_t start=2000000,end=start+durationMs*1000;
  for(uint64_t us=start-500000;us<=end+260000;us+=10000) {
    LaunchMotion::Sample s;s.us=us;s.fused=s.settled=true;s.a[2]=1;
    const float rate[3]={0,45,0};s.q.integrate(rate,float(us-(start-500000))*.000001f);
    ring->feed(s);
  }
  auto t=ring->build(start,end,6000,7);
  require(t.valid() && t.fused && t.durationMs==durationMs,"recorded optical duration retained");
  return t;
}
static void checkFrame(uint32_t elapsed) {
  lgfx::LGFXBase actual,expected;
  const auto sampledElapsed=drawActualFeedback(actual);
  // At elapsed E, display the recorded pose E ms after context start. This
  // oracle uses the existing normalized renderer, independently of the new
  // clock helper and regardless of how many frames were rendered before it.
  // 0.20.5: passes of the recording, each holding its end 500 ms; after a
  // pull two, then the end stays; opened with A they repeat.
  const uint32_t span=latestMotion.points[latestMotion.count-1].ms-latestMotion.points[0].ms;
  const uint32_t cycle=span+500;
  const bool twice=launchFeedback.replay()==LaunchFeedback::Replay::Twice;
  const uint32_t at=twice && elapsed>=cycle+span?span:std::min(elapsed%cycle,span);
  const float progress=at/float(span);
  drawExpectedFeedback(expected,progress);
  require(actual.primitives==expected.primitives,"rendered pose/progress must use recording milliseconds, not fixed recap duration");
  finishActualFeedbackFrame(sampledElapsed);
  ++frames;
}
static void schedule(const LaunchMotion::Trace &t,const std::vector<uint32_t> &intervals,uint32_t start=10000) {
  const uint32_t span=t.points[t.count-1].ms-t.points[0].ms,cycle=span+500,last=cycle+span;
  latestMotion=t;launchFeedback.show(start,LaunchFeedback::Replay::Twice,span);hostNow=start;checkFrame(0);
  uint32_t lastDraw=start,elapsed=0;unsigned index=0;
  bool endpointDisplayed=false,secondPass=false;uint32_t endpointAt=0;
  while(elapsed<last+1000) {
    elapsed+=intervals[index++%intervals.size()];hostNow=start+elapsed;
    if(actualNeedsReplayFrame(hostNow,lastDraw)) {
      checkFrame(elapsed);lastDraw=hostNow;
      if(elapsed>=cycle && elapsed<last)secondPass=true;
      if(elapsed>=last && !endpointDisplayed){endpointDisplayed=true;endpointAt=elapsed;}
    }
  }
  require(secondPass,"the replay plays a second time after its first pass's end");
  require(endpointDisplayed,"redraw scheduler must present the second pass's endpoint, including a stalled frame that crosses it");
  const auto longestInterval=*std::max_element(intervals.begin(),intervals.end());
  require(endpointAt<last+50+longestInterval,"visible endpoint latency must be bounded by redraw cadence, not a fixed replay duration");
  require(!actualNeedsReplayFrame(hostNow,lastDraw),"redraw scheduler must stop after the second pass's endpoint is displayed");
  hostNow=start+last;checkFrame(last);
  hostNow=start+last+1000;checkFrame(last+1000);
}
// Opened with A, the replay repeats until the recap closes: frames keep
// coming through every pass and its end hold, and each pass starts over.
static void loops(const LaunchMotion::Trace &t,uint32_t start=20000) {
  const uint32_t span=t.points[t.count-1].ms-t.points[0].ms,cycle=span+500;
  latestMotion=t;launchFeedback.show(start,LaunchFeedback::Replay::Loop,span);hostNow=start;checkFrame(0);
  uint32_t lastDraw=start,restarts=0;bool held=false;
  for(uint32_t elapsed=50;elapsed<=5*cycle;elapsed+=50) {
    hostNow=start+elapsed;
    if(actualNeedsReplayFrame(hostNow,lastDraw)){checkFrame(elapsed);lastDraw=hostNow;restarts+=elapsed%cycle<50;}
    else held|=elapsed%cycle>span;
  }
  require(restarts>=4,"a looping replay starts every pass over");
  require(held || span<50,"a looping replay holds each pass's end without redrawing it");
  require(!launchFeedback.tick(start+60000) && launchFeedback.shown(),"a recap opened with A stays until A or a pull");
}
int main() {
  // Actual live-level and recap .ino callsites share applied LCD axes. Check
  // the four signed physical sensor directions independently of that helper.
  for(bool flip:{false,true})for(unsigned axis=0;axis<2;++axis)for(float direction:{-1.0f,1.0f}) {
    level={};hostNow=1000;M5.Display.rotation=flip?0:2;
    level.feed(axis?0:direction*.5f,axis?direction*.5f:0,std::sqrt(.75f),hostNow);
    const int physicalSign=flip?1:-1;
    lgfx::LGFXBase live;drawLiveLevel(live,67);
    const auto expected=std::pair<int,int>{67+(axis?0:int(direction*physicalSign*32)),165+(axis?int(-direction*physicalSign*32):0)};
    require(live.liveBubbles==std::vector<std::pair<int,int>>{expected},"actual live level must use LCD rotation for both sensor axes and signed tilts");
    require(level.reading(hostNow).right==(axis?0:direction),"live display cannot mutate recorded board axes");
  }
  LaunchMotion::Trace dots;MotionUI::makeDemo(dots,false);
  dots.startLevel={StickS3Level::State::Valid,30,.6f,.8f};
  dots.endLevel={StickS3Level::State::Valid,30,-.8f,.6f};
  lgfx::LGFXBase flipOff,flipOn;
  MotionUI::feedback(flipOff,dots,false,false,1,false);
  MotionUI::feedback(flipOn,dots,false,false,1,true);
  // 0.20.5: the RPM curve took the frozen Start/End level dots' place.
  require(flipOff.bubbles.empty() && flipOn.bubbles.empty(),"the recap draws no frozen Start/End level dots");
  {
    auto count=[](const lgfx::LGFXBase &g,int radius){return unsigned(std::count(g.radii.begin(),g.radii.end(),radius));};
    const auto &curve=MotionUI::demoCurve(dots.rpm);
    lgfx::LGFXBase none,drawn,flipped;
    MotionUI::feedback(none,dots,false,false,1,false);
    MotionUI::feedback(drawn,dots,false,false,1,false,ui(),nullptr,curve);
    MotionUI::feedback(flipped,dots,false,false,1,true,ui(),nullptr,curve);
    // The replay scrub's dot is the only other filled circle.
    require(count(none,1)==0 && count(none,2)==1,"no curve draws no turn dots");
    require(count(drawn,2)==2 && count(drawn,1)==curve.count-1u,"one dot per turn, the fastest larger");
    require(count(flipped,2)==2 && count(flipped,1)==curve.count-1u,"Flip does not change the curve");
  }
  lgfx::LGFXBase defaultFlip;MotionUI::feedback(defaultFlip,dots,false);
  require(defaultFlip.primitives==flipOff.primitives,"omitted Flip choice must use Off's new physical orientation");
  require(dots.startLevel.right==.6f && dots.endLevel.down==.6f,"display orientation cannot change the stored launch");
  // Independent display-frame oracle: change every recorded board sample to
  // the screen's upside-down axes before the existing heading normalization.
  // This must match applying the setting to a cached native replay at draw time.
  auto displayAxes=dots;
  for(auto &p:displayAxes.points) {
    const auto old=p;
    p.q[0]=-old.q[3];p.q[1]=old.q[2];p.q[2]=-old.q[1];p.q[3]=old.q[0];
  }
  for(float progress:{.31f,.51f,.74f,.92f}) {
    lgfx::LGFXBase native,inverted,oracle,restored;
    MotionUI::feedback(oracle,displayAxes,false,false,progress,true);
    MotionUI::feedback(native,dots,false,false,progress,true);
    MotionUI::feedback(inverted,dots,false,false,progress,false);
    require(!inverted.triangles.empty() && inverted.triangles==oracle.triangles,"flipped 3D body must follow the display axes, including cached replay");
    require((inverted.triangles!=native.triangles)==(progress>.4f),"asymmetric tilt must change direction after display flip while neutral stays aligned");
    require(inverted.usbLines!=oracle.usbLines,"USB marker must stay on the physical USB end, not the logical screen bottom");
    MotionUI::feedback(restored,dots,false,false,progress,true);
    require(restored.primitives==native.primitives,"returning to native orientation must restore the same cached frame");
  }
  // Both RPM estimators feed the same trace/recap consumer; RPM selection must
  // not alter its clock. Durations bracket short real pulls and the 2500 ms cap.
  for(bool flip:{false,true})for(uint32_t duration:{1u,78u,83u,88u,400u,450u,1250u,1800u,2500u}) {
    displayFlipped=flip;M5.Display.rotation=flip?0:2;
    auto t=capture(duration);
    for(const auto &intervals:std::vector<std::vector<uint32_t>>{{16},{33},{50},{100},{7,43,120,5,60,200},{50,700,50,50}})
      schedule(t,intervals);
    t.rpm=5900;schedule(t,{50});
  }
  const auto saved=savedTiltLaunch96();
  for(const auto &intervals:std::vector<std::vector<uint32_t>>{{16},{50},{100},{7,43,120,5,60,200}})schedule(saved,intervals);
  latestMotion=capture(400);launchFeedback.show(10000);hostNow=10200;checkFrame(0);
  // The actual firmware restarts show() after post-roll capture becomes ready.
  launchFeedback.show(10300);hostNow=10300;checkFrame(0);hostNow=10500;checkFrame(200);
  launchFeedback.toggle(10501,true);launchFeedback.toggle(11000,true);hostNow=11000;checkFrame(0);
  require(launchFeedback.replay()==LaunchFeedback::Replay::Loop && !launchFeedback.tick(16000),"a recap opened with A loops and stays");
  // A pull's recap closes after five seconds, or once both passes and a
  // second's hold are over (a 1,160 ms recording: 1,160+500+1,160+1,000).
  const uint32_t span=latestMotion.points[latestMotion.count-1].ms-latestMotion.points[0].ms;
  launchFeedback.show(11000,LaunchFeedback::Replay::Twice,span);
  require(!launchFeedback.tick(15999) && launchFeedback.tick(16000),"five-second recap expiry retained for a short replay");
  launchFeedback.show(11000,LaunchFeedback::Replay::Twice,2500);
  require(!launchFeedback.tick(11000+2*2500+1499) && launchFeedback.tick(11000+2*2500+1500),"a long replay's recap stays for both passes");
  for(uint32_t duration:{83u,400u,1250u,2500u})loops(capture(duration));
  schedule(saved,{50},UINT32_MAX-30);
  latestMotion=saved;launchFeedback.show(10000);hostNow=10000;motionPending=true;
  require(!actualNeedsReplayFrame(10060,10000),"pending capture must not animate");motionPending=false;
  std::cout<<(failures?"FAIL":"PASS")<<": "<<frames<<" actual firmware-rendered frames; recorded/replay duration across 16/33/50/100 ms, jitter, 700 ms stall, saved 83 ms pull, bounded visible endpoint latency, capture restart, manual reopen, five-second hold, timer wrap; failures="<<failures<<'\n';
  return failures?1:0;
}
