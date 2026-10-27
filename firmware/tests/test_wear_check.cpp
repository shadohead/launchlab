// Arduino.h defines a function-like degrees(rad) macro; wear_check.h must compile cleanly with it active.
#define degrees(rad) ((rad) * 57.2957795f)
#include "wear_check.h"
#include "level_indicator.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

using namespace WearCheck;

static bool near(float a,float b,float eps=0.05f){return std::fabs(a-b)<=eps;}

int main() {
  Session s;
  assert(s.stage()==Stage::Horizontal && !s.complete() && s.totalAccepted()==0);
  assert(s.lastOutcome()==Outcome::None && s.overallMean()==0.0f && s.overallMedianMean()==0.0f);

  // Stage 1: Horizontal (0 deg target, <= 12 deg tolerance).
  const Tilt noImu{false,0.0f,0.0f,0.0f};
  const Tilt levelOk{true,0.8f,0.05f,-0.02f};
  const Tilt levelEdge{true,12.0f,0.6f,0.5f};
  const Tilt levelBad{true,12.5f,0.7f,0.5f};
  assert(s.evaluate(noImu)==Alignment::NoImu);
  assert(s.evaluate(levelOk)==Alignment::Ready);
  assert(s.evaluate(levelEdge)==Alignment::Ready);
  assert(s.evaluate(levelBad)==Alignment::HoldLevel);

  // Rejections do not advance the pull count or stage, and duplicate launch counts are ignored.
  assert(s.observePull(0,true,3259.0f,levelOk)==Outcome::None);
  assert(s.observePull(1,false,0.0f,levelOk)==Outcome::RejectedSignal && s.totalAccepted()==0);
  assert(s.observePull(1,true,3259.0f,levelOk)==Outcome::None); // duplicate launch count 1
  assert(s.observePull(2,true,500.0f,levelOk)==Outcome::RejectedSignal && s.totalAccepted()==0);
  assert(s.observePull(3,true,3259.0f,noImu)==Outcome::RejectedNoImu && s.totalAccepted()==0);
  assert(s.observePull(4,true,3259.0f,levelBad)==Outcome::RejectedLevel && s.totalAccepted()==0);

  // Pre-burst level rescues slight release recoil (<= 18 deg) at the end of a pull,
  // but still rejects large tilt excursions (> 18 deg).
  s.observeLevel(levelOk,false);
  s.observeLevel(levelOk,true);
  assert(s.observePull(5,true,3240.0f,Tilt{true,15.5f,0.8f,0.2f})==Outcome::Accepted);
  assert(s.stageCount(0)==1 && near(s.pullRpm(0,0),3240.0f) && near(s.lastTiltDegrees(),0.8f));

  s.observeLevel(levelOk,false);
  s.observeLevel(levelOk,true);
  assert(s.observePull(6,true,3250.0f,Tilt{true,21.0f,0.9f,0.4f})==Outcome::RejectedLevel);
  assert(s.stageCount(0)==1);

  // The sketch observes the level before it takes the pull's event, so the
  // loop pass that first sees the burst over (phase back to Hold) reports the
  // recoiled tilt as not measuring before the pull arrives. The rescue still
  // judges the tilt from before the burst.
  {
    Session order;
    const Tilt recoil{true,15.5f,0.8f,0.2f};
    order.observeLevel(levelOk,false);
    order.observeLevel(levelOk,true);
    order.observeLevel(recoil,true);
    order.observeLevel(recoil,false);
    assert(order.observePull(1,true,3240.0f,recoil)==Outcome::Accepted && near(order.lastTiltDegrees(),0.8f));
    // The next burst starts from the tilt held before it, not this one's.
    order.observeLevel(levelBad,false);
    order.observeLevel(levelBad,true);
    order.observeLevel(recoil,false);
    assert(order.observePull(2,true,3240.0f,recoil)==Outcome::RejectedLevel && order.stageCount(0)==1);
    // A burst the loop never saw has no tilt of its own; the end tilt decides.
    order.observeLevel(levelOk,false);
    assert(order.observePull(3,true,3240.0f,recoil)==Outcome::RejectedLevel && order.stageCount(0)==1);
  }

  assert(s.observePull(7,true,3259.0f,levelOk)==Outcome::Accepted && s.stageCount(0)==2);
  assert(s.observePull(8,true,3278.0f,levelEdge)==Outcome::Accepted && s.stageCount(0)==3);
  assert(s.stage()==Stage::VerticalUp && !s.complete());
  assert(near(s.stageMean(0),(3240.0f+3259.0f+3278.0f)/3.0f));
  assert(near(s.stageMedian(0),3259.0f));

  // Stage 2: Vertical Up-retract (pull string down toward floor, ~90 deg).
  const Tilt vertDownPull{true,88.2f,0.0f,1.0f};
  const Tilt vertUpPull{true,87.9f,0.0f,-1.0f};
  const Tilt vertShallow{true,75.0f,0.0f,1.0f};
  assert(s.evaluate(levelOk)==Alignment::HoldVert);
  assert(s.evaluate(vertShallow)==Alignment::HoldVert);
  assert(s.evaluate(vertDownPull)==Alignment::Ready);

  assert(s.observePull(9,true,2029.0f,vertShallow)==Outcome::RejectedVert && s.stageCount(1)==0);
  // First valid Stage 2 pull locks the vertical axis direction.
  assert(s.observePull(10,true,2010.0f,vertDownPull)==Outcome::Accepted && s.stageCount(1)==1 && s.hasVerticalAxis());
  // Pulling in the opposite vertical orientation during Stage 2 is flagged as FlipVert.
  assert(s.evaluate(vertUpPull)==Alignment::FlipVert);
  assert(s.observePull(11,true,3700.0f,vertUpPull)==Outcome::RejectedFlip && s.stageCount(1)==1);
  assert(s.observePull(12,true,2029.0f,vertDownPull)==Outcome::Accepted && s.stageCount(1)==2);
  assert(s.observePull(13,true,2048.0f,Tilt{true,79.5f,0.2f,0.98f})==Outcome::Accepted && s.stageCount(1)==3);
  assert(s.stage()==Stage::VerticalDown && !s.complete());
  assert(near(s.stageMean(1),2029.0f) && near(s.stageMedian(1),2029.0f));

  // Stage 3: Vertical Down-retract (pull string up toward ceiling, ~90 deg flipped 180).
  // Repeating Stage 2's orientation is rejected with FlipVert; the opposite vertical direction is Ready.
  assert(s.evaluate(vertDownPull)==Alignment::FlipVert);
  assert(s.evaluate(vertUpPull)==Alignment::Ready);
  assert(s.observePull(14,true,2030.0f,vertDownPull)==Outcome::RejectedFlip && s.stageCount(2)==0);
  assert(s.observePull(15,true,3680.0f,vertUpPull)==Outcome::Accepted && s.stageCount(2)==1);
  assert(s.observePull(16,true,3701.0f,vertUpPull)==Outcome::Accepted && s.stageCount(2)==2);
  assert(s.observePull(17,true,3722.0f,Tilt{true,86.0f,-0.15f,-0.98f})==Outcome::Completed && s.stageCount(2)==3);

  assert(s.complete() && s.stage()==Stage::Complete && s.totalAccepted()==TOTAL_PULLS);
  assert(near(s.stageMean(2),3701.0f) && near(s.stageMedian(2),3701.0f));
  const float expectedOverall=(3259.0f+2029.0f+3701.0f)/3.0f;
  assert(near(s.overallMean(),expectedOverall) && near(s.overallMedianMean(),expectedOverall));

  // Further pulls while complete are ignored until reset().
  assert(s.observePull(18,true,3500.0f,vertUpPull)==Outcome::None && s.totalAccepted()==TOTAL_PULLS);

  // StickS3Level::Reading adapter and healthPercent helper.
  StickS3Level::Reading reading{StickS3Level::State::Valid,1.5f,0.1f,-0.05f};
  s.reset();
  assert(!s.complete() && s.totalAccepted()==0 && !s.hasVerticalAxis());
  assert(s.evaluate(reading)==Alignment::Ready);
  assert(near(healthPercent(3000.0f,0.0f),0.0f));
  assert(near(healthPercent(2700.0f,3000.0f),90.0f));
  assert(near(healthPercent(3500.0f,3000.0f),100.0f));

  std::cout<<"PASS: 3-stage guided wear check protocol, 12-deg tolerance, recoil rescue, 180-deg vertical flip enforcement and aggregate RPM statistics\n";
}
