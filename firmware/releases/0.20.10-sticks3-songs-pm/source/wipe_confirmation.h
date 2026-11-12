#pragma once
#include <cstdint>

// Confirmation commands advance warnings only. The final event can come only
// from tick(), whose button states must be the physical buttons, not USB taps
// or synthetic hold helpers. Nothing here erases storage.
class WipeConfirmation {
public:
  enum class Stage:uint8_t {Warning,Confirm,Armed};
  static constexpr uint32_t HOLD_MS=3000,TIMEOUT_MS=30000,MAX_POLL_GAP_MS=250;

  void begin(uint32_t now) {
    active_=true;stage_=Stage::Warning;opened_=now;
    awaitingRelease_=true;holding_=false;holdStarted_=0;pollSeen_=false;
  }
  bool confirm(uint32_t now) {
    if(!active_)return false;
    if(expired(now)){cancel();return false;}
    if(awaitingRelease_ || stage_==Stage::Armed)return false;
    stage_=stage_==Stage::Warning?Stage::Confirm:Stage::Armed;
    awaitingRelease_=true;holding_=false;holdStarted_=0;
    return true;
  }
  void cancel() {
    active_=false;stage_=Stage::Warning;
    awaitingRelease_=holding_=pollSeen_=false;opened_=holdStarted_=lastPoll_=0;
  }
  bool tick(uint32_t now,bool aDown,bool bDown,bool fresh=true) {
    if(!active_)return false;
    // Cancellation and the absolute deadline take priority over completion,
    // including B pressed on the exact last millisecond of the hold.
    if(bDown || expired(now)){cancel();return false;}
    // A skipped button update or a long blocking operation cannot establish a
    // continuous hold. Require a new, freshly observed release and press.
    if(!fresh || (holding_ && pollSeen_ && uint32_t(now-lastPoll_)>MAX_POLL_GAP_MS)) {
      awaitingRelease_=true;holding_=false;holdStarted_=0;pollSeen_=false;
      if(!fresh)return false;
    }
    lastPoll_=now;pollSeen_=true;
    if(awaitingRelease_) {
      if(!aDown)awaitingRelease_=false;
      return false;
    }
    if(stage_!=Stage::Armed)return false;
    if(!aDown){holding_=false;holdStarted_=0;return false;}
    if(!holding_){holding_=true;holdStarted_=now;return false;}
    if(uint32_t(now-holdStarted_)<HOLD_MS)return false;
    cancel();return true; // A single event, consumed by this call.
  }
  uint32_t elapsed(uint32_t now)const {
    if(!active_ || stage_!=Stage::Armed || awaitingRelease_ || !holding_ || expired(now) ||
       !pollSeen_ || uint32_t(now-lastPoll_)>MAX_POLL_GAP_MS)return 0;
    const uint32_t held=uint32_t(now-holdStarted_);
    return held<HOLD_MS?held:HOLD_MS;
  }
  bool awaitingRelease()const{return awaitingRelease_;}
  Stage stage()const{return stage_;}
  bool active()const{return active_;}
private:
  Stage stage_=Stage::Warning;
  uint32_t opened_=0,holdStarted_=0,lastPoll_=0;
  bool active_=false,awaitingRelease_=false,holding_=false,pollSeen_=false;
  bool expired(uint32_t now)const{return uint32_t(now-opened_)>=TIMEOUT_MS;}
};
