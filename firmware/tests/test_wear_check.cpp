// Arduino.h defines a function-like degrees(rad) macro; wear_check.h must compile cleanly with it active.
#define degrees(rad) ((rad) * 57.2957795f)
#include "wear_check.h"
#include "level_indicator.h"
#include "mount_orientation.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

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

  // Each pull (0.20.4): pull out, hold, let go. Only the burst after the hold
  // is judged; the pull out is never reported.
  {
    Session p;uint64_t t=5000000;
    // passes of strong marks 20 ms apart (a loop pass each), then still time.
    uint32_t turns=1000;
    // passes: loop passes 20 ms apart with a strong turn each.
    auto marks=[&](unsigned passes){Outcome o=Outcome::None;for(unsigned i=0;i<passes;++i){t+=20000;o=p.observeOptics(t,++turns,true);}return o;};
    auto still=[&](uint64_t us,bool ready=true){t+=us;return p.observeOptics(t,turns,ready);};
    // Turns from before the check (the last pull on Main) are old news, and
    // so is the rest of their run.
    assert(p.observeOptics(t,turns,true)==Outcome::None && p.step()==Step::PullOut);
    marks(8);assert(p.step()==Step::PullOut);
    // A lone turn or a few (handling) are not the string coming out; a run is.
    still(1000000);marks(1);still(400000);marks(RUN_TURNS-1);still(400000);assert(p.step()==Step::PullOut);
    // Several turns in one pass count, as the run does across passes.
    t+=20000;turns+=RUN_TURNS-1;p.observeOptics(t,turns,true);assert(p.step()==Step::PullOut);
    still(400000);
    marks(RUN_TURNS);assert(p.step()==Step::Hold);marks(10);
    // Its measured burst is the pull out: never judged.
    assert(p.observeBurst(1,true,4800.0f,levelOk)==Outcome::Extension && p.step()==Step::Hold && !p.totalAccepted());
    assert(p.observeBurst(1,true,4800.0f,levelOk)==Outcome::None);
    // HOLD lasts HOLD_US with no strong turn and until the detector is
    // ready (its rearm second after a measured pull out); a lone turn
    // restarts it.
    still(HOLD_US-1);assert(p.step()==Step::Hold);
    marks(1);still(HOLD_US-1);assert(p.step()==Step::Hold);
    still(1,false);still(500000,false);assert(p.step()==Step::Hold);
    still(20000);assert(p.step()==Step::LetGo);
    // LET GO: a lone turn is not the retraction either; the run is.
    marks(1);still(400000);assert(p.step()==Step::LetGo);
    marks(RUN_TURNS);assert(p.step()==Step::Retracting);marks(8);
    assert(p.observeBurst(2,true,3100.0f,levelOk)==Outcome::Accepted && p.step()==Step::PullOut && near(p.pullRpm(0,0),3100.0f));
    // The gear's last turns after its result belong to its run.
    marks(3);still(100000);marks(2);assert(p.step()==Step::PullOut);
    // Out again (a pause under STILL_US is the same pull out), then let go
    // early: a run that starts STILL_US after the last mark, before LET GO.
    still(1500000);marks(RUN_TURNS);assert(p.step()==Step::Hold);
    still(STILL_US-40000);marks(6);assert(p.step()==Step::Hold);
    still(STILL_US);marks(RUN_TURNS-1);assert(p.step()==Step::Hold);
    assert(marks(1)==Outcome::EarlyRelease && p.lastOutcome()==Outcome::EarlyRelease);
    assert(p.step()==Step::PullOut && p.stageCount(0)==1);
    // The rest of its rewind is the same run: not a pull out.
    marks(10);assert(p.step()==Step::PullOut);
    // A retraction with marks but no result: missed, once the detector is
    // ready and the marks have stopped for MISSED_US.
    still(1500000);marks(RUN_TURNS);still(HOLD_US);assert(p.step()==Step::LetGo);
    marks(RUN_TURNS+4);
    assert(still(MISSED_US-1)==Outcome::None && p.step()==Step::Retracting);
    assert(still(1)==Outcome::Missed && p.step()==Step::PullOut && p.stageCount(0)==1);
    // A burst before its turns made a run: still the pull out, or after LET
    // GO the retraction.
    still(1500000);marks(2);assert(p.observeBurst(3,true,4700.0f,levelOk)==Outcome::Extension && p.step()==Step::Hold);
    marks(2);assert(p.step()==Step::Hold);
    still(HOLD_US);assert(p.step()==Step::LetGo);
    marks(2);assert(p.observeBurst(4,true,3150.0f,levelOk)==Outcome::Accepted && p.stageCount(0)==2);
    marks(4);assert(p.step()==Step::PullOut);
    // reset() starts over: old marks are primed away again.
    p.reset();assert(p.step()==Step::PullOut);
    marks(RUN_TURNS+2);assert(p.step()==Step::PullOut);
    assert(stepLabel(Step::PullOut,true)==std::string("PULL OUT") && stepLabel(Step::PullOut,false)==std::string("WAIT"));
    assert(stepLabel(Step::Hold,false)==std::string("HOLD") && stepLabel(Step::LetGo,true)==std::string("LET GO"));
  }

  // The tilt is read in the selected Mounting (0.20.4, owner): IMU samples are
  // mapped to the Top launcher frame before fusion, so the same launcher pose
  // passes the same stage for both mounts. A Sidecar unit sits on its side:
  // its +X is up when the launcher is level, and its screen faces up only
  // with the launcher on end. Measured as Top, a Sidecar install is refused.
  {
    auto tiltFor=[](MountOrientation::Mode mount,float x,float y,float z){
      const float device[3]={x,y,z};float launcher[3];MountOrientation::mapVector(mount,device,launcher);
      StickS3Level level;level.feed(launcher[0],launcher[1],launcher[2],1000);return Tilt(level.reading(1000));};
    using MountOrientation::Mode;
    // Device gravity for a level launcher, then ripcord down and up.
    struct Pose {Mode mount;float level[3],down[3],up[3];};
    for(const Pose pose:{Pose{Mode::Top,{0,0,1},{0,-1,0},{0,1,0}},Pose{Mode::Sidecar,{1,0,0},{0,-1,0},{0,1,0}}}) {
      Session m;
      const Tilt level=tiltFor(pose.mount,pose.level[0],pose.level[1],pose.level[2]);
      assert(level.valid && level.degrees<0.5f && m.evaluate(level)==Alignment::Ready);
      // The other mount's level pose is 90 degrees off for this one.
      const Tilt other=pose.mount==Mode::Top?tiltFor(Mode::Top,1,0,0):tiltFor(Mode::Sidecar,0,0,1);
      assert(other.valid && near(other.degrees,90.0f,0.5f) && m.evaluate(other)==Alignment::HoldLevel);
      uint32_t n=0;
      assert(m.observePull(++n,true,3000.0f,other)==Outcome::RejectedLevel);
      for(unsigned i=0;i<3;++i)assert(m.observePull(++n,true,3000.0f+i,level)==Outcome::Accepted);
      const Tilt down=tiltFor(pose.mount,pose.down[0],pose.down[1],pose.down[2]);
      const Tilt up=tiltFor(pose.mount,pose.up[0],pose.up[1],pose.up[2]);
      assert(m.evaluate(level)==Alignment::HoldVert && m.evaluate(down)==Alignment::Ready);
      for(unsigned i=0;i<3;++i)assert(m.observePull(++n,true,2000.0f+i,down)==Outcome::Accepted);
      assert(m.evaluate(down)==Alignment::FlipVert && m.evaluate(up)==Alignment::Ready);
      for(unsigned i=0;i<3;++i)assert(m.observePull(++n,true,3500.0f+i,up)==(i<2?Outcome::Accepted:Outcome::Completed));
      assert(m.complete() && m.totalAccepted()==9);
    }
    // Measured in the wrong Mounting, a Sidecar unit held level is 90 degrees
    // off: the page shows its Mounting so the user can tell.
    assert(Session{}.evaluate(tiltFor(Mode::Top,1,0,0))==Alignment::HoldLevel);
    // remapVector: the same device sample as the other Mounting reads it.
    for(const Mode from:{Mode::Top,Mode::Sidecar})for(const Mode to:{Mode::Top,Mode::Sidecar}) {
      const float device[3]={0.3f,-0.5f,0.8f};float in[3],expect[3],got[3];
      MountOrientation::mapVector(from,device,in);MountOrientation::mapVector(to,device,expect);
      MountOrientation::remapVector(from,to,in,got);
      for(unsigned i=0;i<3;++i)assert(near(got[i],expect[i],1e-6f));
    }
    // The hint: off level here and level in the other Mounting for
    // MOUNT_HINT_MS, on the level stage only.
    auto otherFor=[](Mode mount,float x,float y,float z){
      const float device[3]={x,y,z};float launcher[3];MountOrientation::mapVector(mount,device,launcher);
      StickS3Level level;level.feed(launcher[0],launcher[1],launcher[2],1000);
      const Mode other=mount==Mode::Top?Mode::Sidecar:Mode::Top;
      return Tilt(level.reading(1000,[&](const float *in,float *out){MountOrientation::remapVector(mount,other,in,out);}));};
    for(const Mode mount:{Mode::Top,Mode::Sidecar}) {
      const Mode other=mount==Mode::Top?Mode::Sidecar:Mode::Top;
      // The other install's level pose: level for it, not for this Mounting.
      const float pose[3]={other==Mode::Sidecar?1.0f:0.0f,0.0f,other==Mode::Sidecar?0.0f:1.0f};
      Session h;
      const Tilt here=tiltFor(mount,pose[0],pose[1],pose[2]),there=otherFor(mount,pose[0],pose[1],pose[2]);
      assert(h.evaluate(here)==Alignment::HoldLevel && h.evaluate(there)==Alignment::Ready);
      h.observeMount(5000,here,there);assert(!h.mountHint());
      h.observeMount(5000+MOUNT_HINT_MS-1,here,there);assert(!h.mountHint());
      h.observeMount(5000+MOUNT_HINT_MS,here,there);assert(h.mountHint());
      // Level in this Mounting clears it at once.
      const float own[3]={mount==Mode::Sidecar?1.0f:0.0f,0.0f,mount==Mode::Sidecar?0.0f:1.0f};
      const Tilt level=tiltFor(mount,own[0],own[1],own[2]),levelThere=otherFor(mount,own[0],own[1],own[2]);
      h.observeMount(9000,level,levelThere);assert(!h.mountHint());
      // Never on the vertical stages: a unit held level between them is not
      // turned yet, and the ripcord poses read alike in both Mountings.
      for(unsigned i=0;i<3;++i)h.observePull(i+1,true,3000.0f,level);
      assert(h.stage()==Stage::VerticalUp);
      for(uint32_t t=10000;t<=20000;t+=1000)h.observeMount(t,here,there);
      assert(!h.mountHint());
    }
  }

  std::cout<<"PASS: 3-stage guided wear check protocol, 12-deg tolerance, recoil rescue, 180-deg vertical flip enforcement, "
             "pull out / hold / let go steps (only the retraction judged), Top and Sidecar mounts and aggregate RPM statistics\n";
}
