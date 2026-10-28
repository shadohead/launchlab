#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Guided 3-orientation string-launcher retraction wear check:
//   1. Horizontal (0 deg level): 3 pulls
//   2. Vertical Up-retract (90 deg, pull string down toward floor): 3 pulls
//   3. Vertical Down-retract (90 deg flipped 180, pull string up): 3 pulls
// Each pull is: pull the string out, hold it still, let go. Only the
// retraction after the hold is measured (0.20.4). Pulling out spins the gear
// too, and one optical channel cannot tell its direction, so the steps follow
// the strong optical marks: the first turns are the pull out, the turns after
// a still hold are the retraction. A pull out is never reported.
// Retraction pulls stay out of PracticeHistory, PersonalBests and MotionStore.
namespace WearCheck {
static constexpr unsigned STAGES=3;
static constexpr unsigned PULLS_PER_STAGE=3;
static constexpr unsigned TOTAL_PULLS=STAGES*PULLS_PER_STAGE;
static constexpr float TOLERANCE_DEG=12.0f;
static constexpr float RECOIL_TOLERANCE_DEG=18.0f;
static constexpr float VERTICAL_TARGET_DEG=90.0f;
static constexpr float VERTICAL_MIN_DEG=VERTICAL_TARGET_DEG-TOLERANCE_DEG;
static constexpr float DIRECTION_COS_MIN=0.5f;
static constexpr float MIN_RETRACT_RPM=1000.0f;
static constexpr float MAX_RETRACT_RPM=30000.0f;
// Set > 0 once calibrated against a fresh baseline string launcher.
static constexpr float FRESH_BASELINE_RPM=0.0f;
// The string moves when the gear turns in a run: RUN_TURNS strong turns
// (the detector's strongTurns) with no still gap of STILL_US between them.
// Lone marks and the few swings of handling the unit are not movement, and
// still time runs from the last strong turn: a held string wiggles its mark
// without turning the gear. Owner captures (2026-10-27): every pull out and
// retraction turned the gear 9 times or more; handling gave single marks.
static constexpr unsigned RUN_TURNS=5;
// The hold: no strong turn for HOLD_US with the string out, and the detector
// ready to measure (after a measured pull out, its rearm second). LET GO then.
// The owner held 1.0-1.4 s; a slow pull out leaves the detector ready.
static constexpr uint64_t HOLD_US=500000;
// Movement that starts after the string has been still this long, before
// LET GO, is the string let go too early (the detector takes it as the
// pull's rewind). Shorter stops are the pull out continuing.
static constexpr uint64_t STILL_US=250000;
// After LET GO: a retraction whose marks stop with no measurement for this
// long (the detector's quiet second) was too weak or slow to measure.
static constexpr uint64_t MISSED_US=1000000;
// Held this long off level in the selected Mounting while level in the other
// one, the install probably uses the other Mounting: the page says so.
static constexpr uint32_t MOUNT_HINT_MS=2000;

// Where the current pull is: waiting for the string to come out, holding it
// out, or ready for it to be let go (and the retraction under way).
enum class Step:uint8_t {PullOut,Hold,LetGo,Retracting};

inline const char *stepName(Step s) {
  switch(s) {
    case Step::Hold:return "hold";
    case Step::LetGo:return "let_go";
    case Step::Retracting:return "retracting";
    default:return "pull_out";
  }
}

enum class Stage:uint8_t {
  Horizontal=0,
  VerticalUp=1,
  VerticalDown=2,
  Complete=3
};

enum class Alignment:uint8_t {
  NoImu,
  HoldLevel,
  HoldVert,
  FlipVert,
  Ready
};

enum class Outcome:uint8_t {
  None,
  Accepted,
  Completed,
  RejectedSignal,
  RejectedNoImu,
  RejectedLevel,
  RejectedVert,
  RejectedFlip,
  // Not judged: the pull out (never reported), the string let go before
  // LET GO, and a retraction the detector could not measure.
  Extension,
  EarlyRelease,
  Missed
};

struct Tilt {
  bool valid=false;
  float degrees=0.0f,right=0.0f,down=0.0f;
  constexpr Tilt()=default;
  constexpr Tilt(bool v,float deg,float r,float d):valid{v},degrees{deg},right{r},down{d}{}
  template<class R>
  explicit Tilt(const R &reading):valid{reading.valid()},degrees{reading.degrees},right{reading.right},down{reading.down}{}
};

inline const char *stageName(Stage s) {
  switch(s) {
    case Stage::Horizontal:return "level";
    case Stage::VerticalUp:return "pull_down";
    case Stage::VerticalDown:return "pull_up";
    default:return "complete";
  }
}

inline const char *stageHeader(Stage s) {
  switch(s) {
    case Stage::Horizontal:return "1/3 Level";
    case Stage::VerticalUp:return "2/3 Pull Down";
    case Stage::VerticalDown:return "3/3 Pull Up";
    default:return "String wear check";
  }
}

inline const char *stagePrompt(Stage s) {
  switch(s) {
    case Stage::Horizontal:return "Keep level";
    case Stage::VerticalUp:return "Pull down 90";
    case Stage::VerticalDown:return "Pull up 90";
    default:return "Complete";
  }
}

inline const char *alignmentLabel(Alignment a) {
  switch(a) {
    case Alignment::Ready:return "PULL OUT";
    case Alignment::HoldLevel:return "TILT TO 0 DEG";
    case Alignment::HoldVert:return "TILT TO 90 DEG";
    case Alignment::FlipVert:return "FLIP 180 DEG";
    default:return "WAIT FOR IMU";
  }
}

inline const char *outcomeName(Outcome o) {
  switch(o) {
    case Outcome::Accepted:return "accepted";
    case Outcome::Completed:return "completed";
    case Outcome::RejectedSignal:return "rejected_signal";
    case Outcome::RejectedNoImu:return "rejected_no_imu";
    case Outcome::RejectedLevel:return "rejected_level";
    case Outcome::RejectedVert:return "rejected_vertical";
    case Outcome::RejectedFlip:return "rejected_flip";
    case Outcome::Extension:return "extension";
    case Outcome::EarlyRelease:return "early_release";
    case Outcome::Missed:return "missed";
    default:return "none";
  }
}

// What to do now when the tilt is on target. detectorReady: the RPM detector
// would measure a burst now (not in its rearm second).
inline const char *stepLabel(Step s,bool detectorReady) {
  switch(s) {
    case Step::Hold:return "HOLD";
    case Step::LetGo:case Step::Retracting:return "LET GO";
    default:return detectorReady?"PULL OUT":"WAIT";
  }
}

inline float healthPercent(float scoreRpm,float baselineRpm=FRESH_BASELINE_RPM) {
  if(!(baselineRpm>0.0f) || !(scoreRpm>0.0f) || !std::isfinite(scoreRpm) || !std::isfinite(baselineRpm))return 0.0f;
  return std::min(100.0f,std::max(0.0f,scoreRpm/baselineRpm*100.0f));
}

class Session {
public:
  void reset() {
    stage_=Stage::Horizontal;
    for(unsigned s=0;s<STAGES;++s) {
      stageCount_[s]=0;
      for(unsigned p=0;p<PULLS_PER_STAGE;++p)rpms_[s][p]=0.0f;
    }
    verticalAxis_[0]=verticalAxis_[1]=0.0f;
    hasVerticalAxis_=false;
    inBurst_=false;
    preBurstTilt_=burstTilt_=Tilt{};
    lastLaunchCount_=0;
    step_=Step::PullOut;
    lastTurnUs_=0;
    lastTurns_=0;
    run_=0;
    primed_=counted_=false;
    mountSinceMs_=0;mountSuspect_=mountHint_=false;
    lastOutcome_=Outcome::None;
    lastRpm_=0.0f;
    lastTiltDeg_=0.0f;
  }

  Stage stage()const{return stage_;}
  bool complete()const{return stage_==Stage::Complete;}
  Outcome lastOutcome()const{return lastOutcome_;}
  float lastRpm()const{return lastRpm_;}
  float lastTiltDegrees()const{return lastTiltDeg_;}
  bool hasVerticalAxis()const{return hasVerticalAxis_;}
  Step step()const{return step_;}
  bool mountHint()const{return mountHint_;}

  // Called every loop pass with the tilt as the selected Mounting reads it
  // and as the other one would. Only the level stage asks: the ripcord poses
  // read alike in both, and a unit held level between vertical stages is
  // simply not turned yet.
  void observeMount(uint32_t nowMs,const Tilt &tilt,const Tilt &otherMount) {
    const bool suspect=stage_==Stage::Horizontal && tilt.valid && otherMount.valid &&
      evaluate(tilt)!=Alignment::Ready && evaluate(otherMount)==Alignment::Ready;
    if(!suspect){mountSuspect_=mountHint_=false;return;}
    if(!mountSuspect_){mountSuspect_=true;mountSinceMs_=nowMs;}
    if(nowMs-mountSinceMs_>=MOUNT_HINT_MS)mountHint_=true;
  }

  unsigned stageCount(unsigned s)const{return s<STAGES?stageCount_[s]:0u;}
  unsigned currentStageCount()const{return stage_<Stage::Complete?stageCount_[unsigned(stage_)]:PULLS_PER_STAGE;}
  unsigned totalAccepted()const{return unsigned(stageCount_[0])+stageCount_[1]+stageCount_[2];}

  float pullRpm(unsigned s,unsigned p)const {
    return (s<STAGES && p<stageCount_[s])?rpms_[s][p]:0.0f;
  }

  float stageMean(unsigned s)const {
    if(s>=STAGES || !stageCount_[s])return 0.0f;
    double sum=0.0;
    for(unsigned i=0;i<stageCount_[s];++i)sum+=rpms_[s][i];
    return float(sum/stageCount_[s]);
  }

  float stageMedian(unsigned s)const {
    if(s>=STAGES || !stageCount_[s])return 0.0f;
    float sorted[PULLS_PER_STAGE]={};
    const unsigned n=stageCount_[s];
    for(unsigned i=0;i<n;++i)sorted[i]=rpms_[s][i];
    std::sort(sorted,sorted+n);
    return n%2?sorted[n/2]:0.5f*(sorted[n/2-1]+sorted[n/2]);
  }

  float overallMean()const {
    const unsigned n=totalAccepted();
    if(!n)return 0.0f;
    double sum=0.0;
    for(unsigned s=0;s<STAGES;++s)
      for(unsigned i=0;i<stageCount_[s];++i)sum+=rpms_[s][i];
    return float(sum/n);
  }

  float overallMedianMean()const {
    unsigned readyStages=0;
    double sum=0.0;
    for(unsigned s=0;s<STAGES;++s) {
      if(stageCount_[s]) {
        sum+=stageMedian(s);
        ++readyStages;
      }
    }
    return readyStages?float(sum/readyStages):0.0f;
  }

  Alignment evaluate(const Tilt &tilt,float maxErrorDeg=TOLERANCE_DEG)const {
    if(stage_==Stage::Complete)return Alignment::Ready;
    if(!tilt.valid || !std::isfinite(tilt.degrees) || !std::isfinite(tilt.right) || !std::isfinite(tilt.down))
      return Alignment::NoImu;
    if(stage_==Stage::Horizontal)
      return tilt.degrees<=maxErrorDeg?Alignment::Ready:Alignment::HoldLevel;
    const float err=std::fabs(VERTICAL_TARGET_DEG-tilt.degrees);
    if(err>maxErrorDeg)return Alignment::HoldVert;
    const float xy=std::hypot(tilt.right,tilt.down);
    if(xy<0.1f)return Alignment::HoldVert;
    if(hasVerticalAxis_) {
      const float ux=tilt.right/xy,uy=tilt.down/xy;
      const float dot=ux*verticalAxis_[0]+uy*verticalAxis_[1];
      if(stage_==Stage::VerticalUp && dot<DIRECTION_COS_MIN)return Alignment::FlipVert;
      if(stage_==Stage::VerticalDown && dot>-DIRECTION_COS_MIN)return Alignment::FlipVert;
    }
    return Alignment::Ready;
  }

  template<class R>
  Alignment evaluate(const R &reading,float maxErrorDeg=TOLERANCE_DEG)const {
    return evaluate(Tilt(reading),maxErrorDeg);
  }

  // Called every loop pass with the detector's phase. The tilt held when a
  // burst begins is latched for its pull: the pass that first sees the burst
  // over may run before the pull's event arrives, with the recoiled tilt.
  void observeLevel(const Tilt &tilt,bool measuring) {
    if(measuring && !inBurst_)burstTilt_=preBurstTilt_.valid?preBurstTilt_:tilt;
    if(!measuring && tilt.valid)preBurstTilt_=tilt;
    inBurst_=measuring;
  }

  template<class R>
  void observeLevel(const R &reading,bool measuring) {
    observeLevel(Tilt(reading),measuring);
  }

  // Called every loop pass, before the pass's events, with the detector's
  // clock, its strong turns so far, whether it would measure a burst now, and
  // whether its signal is settled (not starting, paused or faulted). Returns
  // EarlyRelease or Missed when a pull ends unmeasured (and the step goes
  // back to PullOut), else None.
  Outcome observeOptics(uint64_t nowUs,uint32_t strongTurns,bool detectorReady,bool settled=true) {
    if(stage_==Stage::Complete)return Outcome::None;
    // The signal swings while the detector starts (at boot, or after a pause
    // or an export): those marks are not the string, and a pull it interrupts
    // is lost with the detector's data. Start over once it has settled.
    if(!settled){primed_=false;step_=Step::PullOut;return Outcome::None;}
    // Turns from before the check began are old, and so is the rest of their run.
    if(!primed_ || strongTurns<lastTurns_) {
      primed_=counted_=true;lastTurnUs_=nowUs;lastTurns_=strongTurns;run_=RUN_TURNS;
      return Outcome::None;
    }
    // A run starts with turns after STILL_US still and is movement once, on
    // its RUN_TURNS-th turn (so always after a still gap).
    bool moving=false;
    if(const uint32_t turns=strongTurns-lastTurns_) {
      if(nowUs-lastTurnUs_>=STILL_US){run_=0;counted_=false;}
      run_=uint8_t(std::min<uint32_t>(RUN_TURNS,run_+turns));
      if(run_>=RUN_TURNS && !counted_)moving=counted_=true;
      lastTurns_=strongTurns;lastTurnUs_=nowUs;
    }
    const uint64_t still=nowUs-lastTurnUs_;
    switch(step_) {
      case Step::PullOut:
        if(moving)step_=Step::Hold;
        break;
      case Step::Hold:
        if(moving)return endPull(Outcome::EarlyRelease);
        if(still>=HOLD_US && detectorReady)step_=Step::LetGo;
        break;
      case Step::LetGo:
        if(moving)step_=Step::Retracting;
        break;
      case Step::Retracting:
        if(still>=MISSED_US && detectorReady)return endPull(Outcome::Missed);
        break;
    }
    return Outcome::None;
  }

  // A measured burst (the detector's launch) on this page: before LET GO it
  // is the pull out and only moves the step on; after it, the retraction.
  // An invalid result is no pull: the detector publishes one when it loses
  // data (an export, a sensor pause or fault) without counting a launch.
  Outcome observeBurst(uint32_t launchCount,bool valid,float rpm,const Tilt &endTilt) {
    if(stage_==Stage::Complete || !valid || !launchCount || launchCount==lastLaunchCount_)return Outcome::None;
    if(step_==Step::PullOut || step_==Step::Hold) {
      lastLaunchCount_=launchCount;
      burstTilt_=Tilt{};
      inBurst_=false;
      // The marks were seen before the burst was confirmed; a burst the loop
      // never saw move still means the string came out.
      step_=Step::Hold;
      counted_=true;
      return Outcome::Extension;
    }
    step_=Step::PullOut;
    counted_=true;
    return observePull(launchCount,valid,rpm,endTilt);
  }

  template<class R>
  Outcome observeBurst(uint32_t launchCount,bool valid,float rpm,const R &endReading) {
    return observeBurst(launchCount,valid,rpm,Tilt(endReading));
  }

  // Judges a retraction: its rpm, and the tilt it was pulled at.
  Outcome observePull(uint32_t launchCount,bool valid,float rpm,const Tilt &endTilt) {
    if(stage_==Stage::Complete || !launchCount || launchCount==lastLaunchCount_)return Outcome::None;
    lastLaunchCount_=launchCount;
    if(!valid || !std::isfinite(rpm) || rpm<MIN_RETRACT_RPM || rpm>MAX_RETRACT_RPM) {
      inBurst_=false;
      lastOutcome_=Outcome::RejectedSignal;
      return lastOutcome_;
    }
    // Release recoil: a pull held on target before its burst may end up to
    // RECOIL_TOLERANCE_DEG off. Without an end tilt, the last valid one counts.
    const Tilt before=burstTilt_;
    burstTilt_=Tilt{};
    Tilt chosen=endTilt;
    Alignment align=evaluate(endTilt,TOLERANCE_DEG);
    if(align!=Alignment::Ready && before.valid &&
       evaluate(before,TOLERANCE_DEG)==Alignment::Ready &&
       evaluate(endTilt,RECOIL_TOLERANCE_DEG)==Alignment::Ready) {
      chosen=before;
      align=Alignment::Ready;
    } else if(!endTilt.valid && (before.valid || preBurstTilt_.valid)) {
      chosen=before.valid?before:preBurstTilt_;
      align=evaluate(chosen,TOLERANCE_DEG);
    }
    lastRpm_=rpm;
    lastTiltDeg_=chosen.valid?chosen.degrees:0.0f;
    inBurst_=false;
    if(align!=Alignment::Ready) {
      switch(align) {
        case Alignment::NoImu:lastOutcome_=Outcome::RejectedNoImu;break;
        case Alignment::HoldLevel:lastOutcome_=Outcome::RejectedLevel;break;
        case Alignment::HoldVert:lastOutcome_=Outcome::RejectedVert;break;
        case Alignment::FlipVert:lastOutcome_=Outcome::RejectedFlip;break;
        default:lastOutcome_=Outcome::RejectedLevel;break;
      }
      return lastOutcome_;
    }
    if(stage_==Stage::VerticalUp && !hasVerticalAxis_) {
      const float xy=std::hypot(chosen.right,chosen.down);
      if(xy>=0.1f) {
        verticalAxis_[0]=chosen.right/xy;
        verticalAxis_[1]=chosen.down/xy;
        hasVerticalAxis_=true;
      }
    }
    const unsigned idx=unsigned(stage_);
    rpms_[idx][stageCount_[idx]++]=rpm;
    if(stageCount_[idx]>=PULLS_PER_STAGE) {
      stage_=Stage(idx+1);
      preBurstTilt_=Tilt{};
    }
    lastOutcome_=stage_==Stage::Complete?Outcome::Completed:Outcome::Accepted;
    return lastOutcome_;
  }

  template<class R>
  Outcome observePull(uint32_t launchCount,bool valid,float rpm,const R &endReading) {
    return observePull(launchCount,valid,rpm,Tilt(endReading));
  }

  // Normalized reticle offset in [-1, 1] where radius <= 0.5 corresponds to
  // being within the 12-degree target zone for the active stage.
  void reticleOffset(const Tilt &rawTilt,const Tilt &displayTilt,float &dx,float &dy)const {
    dx=dy=0.0f;
    if(!rawTilt.valid || stage_==Stage::Complete)return;
    constexpr float FULL_SCALE_DEG=TOLERANCE_DEG*2.0f; // 24 deg -> outer rim, 12 deg -> 0.5 target ring
    if(stage_==Stage::Horizontal) {
      const float xy=std::hypot(displayTilt.right,displayTilt.down);
      const float mag=std::min(1.0f,rawTilt.degrees/FULL_SCALE_DEG);
      if(xy>1e-4f){dx=displayTilt.right/xy*mag;dy=displayTilt.down/xy*mag;}
      return;
    }
    const Alignment align=evaluate(rawTilt,TOLERANCE_DEG);
    if(align==Alignment::FlipVert) {
      dx=0.0f;
      dy=stage_==Stage::VerticalDown?1.0f:-1.0f;
      return;
    }
    const float err=std::fabs(VERTICAL_TARGET_DEG-rawTilt.degrees);
    const float mag=std::min(1.0f,err/FULL_SCALE_DEG);
    const float xy=std::hypot(displayTilt.right,displayTilt.down);
    if(xy>1e-4f){dx=displayTilt.right/xy*mag;dy=displayTilt.down/xy*mag;}
    else dy=mag;
  }

private:
  Outcome endPull(Outcome why) {
    step_=Step::PullOut;
    lastOutcome_=why;
    return why;
  }

  Stage stage_=Stage::Horizontal;
  uint8_t stageCount_[STAGES]={};
  float rpms_[STAGES][PULLS_PER_STAGE]={};
  float verticalAxis_[2]={};
  bool hasVerticalAxis_=false;
  bool inBurst_=false;
  Tilt preBurstTilt_{},burstTilt_{};
  uint32_t lastLaunchCount_=0;
  Outcome lastOutcome_=Outcome::None;
  float lastRpm_=0.0f;
  float lastTiltDeg_=0.0f;
  Step step_=Step::PullOut;
  uint64_t lastTurnUs_=0;
  uint32_t lastTurns_=0;
  uint8_t run_=0;
  bool primed_=false,counted_=false;
  uint32_t mountSinceMs_=0;
  bool mountSuspect_=false,mountHint_=false;
};
}
