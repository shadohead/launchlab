#pragma once
#include <algorithm>
#include <cstdint>

class LaunchFeedback {
public:
  static constexpr uint32_t DISPLAY_MS=5000;
  // After a pull the tilt replay plays twice and holds its end; opened with A
  // it loops until A again or the next pull (0.20.5). Each pass holds its
  // last frame END_HOLD_MS before the next one starts.
  enum class Replay:uint8_t {Twice,Loop};
  static constexpr uint32_t END_HOLD_MS=500,CLOSE_HOLD_MS=1000;
  // `durationMs`: the shown trace's replay (MotionReplay::playbackDurationMs),
  // 0 while its capture is pending.
  void show(uint32_t now,Replay replay=Replay::Twice,uint32_t durationMs=0) {
    started_=now;shown_=true;replayStarted_=false;lastReplayElapsed_=0;replay_=replay;duration_=durationMs;
  }
  void clear(){shown_=false;}
  void toggle(uint32_t now,bool available,uint32_t durationMs=0){if(shown_)clear();else if(available)show(now,Replay::Loop,durationMs);}
  // A pull's recap closes after DISPLAY_MS, or once both passes of its
  // replay and CLOSE_HOLD_MS are over if that is later. A looping recap stays.
  bool tick(uint32_t now) {
    if(!shown_ || replay_==Replay::Loop)return false;
    const uint32_t limit=std::max(DISPLAY_MS,duration_?2*duration_+END_HOLD_MS+CLOSE_HOLD_MS:0);
    if(now-started_>=limit){clear();return true;}
    return false;
  }
  bool shown()const{return shown_;}
  Replay replay()const{return replay_;}
  uint32_t elapsed(uint32_t now)const{return shown_?now-started_:0;}
  uint32_t replayElapsed(uint32_t now)const{return replayStarted_?elapsed(now):0;}
  bool replayStarted()const{return shown_ && replayStarted_;}
  uint32_t lastReplayElapsed()const{return lastReplayElapsed_;}
  // Automatic flash writes wait while this recap shows (a write stalls the
  // replay): a pull's recap throughout, a loop opened with A only through
  // its first pass, so pulls never wait in RAM for as long as it stays open.
  bool protects(uint32_t now)const {
    return shown_ && (replay_==Replay::Twice || !replayStarted_ || elapsed(now)<duration_+END_HOLD_MS);
  }
  // Where the replay is `elapsed` ms after it started: ms into the
  // recording, the end held between passes and after the second pass.
  uint32_t position(uint32_t elapsed)const {
    if(!duration_)return elapsed;
    const uint32_t cycle=duration_+END_HOLD_MS;
    if(replay_==Replay::Twice && elapsed>=cycle+duration_)return duration_;
    return std::min(elapsed%cycle,duration_);
  }
  // Capture, flash and rendering can finish long after the recap was opened.
  // Arm the 1x playback clock only after its first complete frame is presented.
  void replayPresented(uint32_t now,bool completeFrame,uint32_t sampledElapsed) {
    if(!shown_ || !completeFrame)return;
    if(!replayStarted_){started_=now;replayStarted_=true;lastReplayElapsed_=0;}
    else lastReplayElapsed_=sampledElapsed;
  }
private:
  uint32_t started_=0,lastReplayElapsed_=0;bool shown_=false,replayStarted_=false;Replay replay_=Replay::Twice;uint32_t duration_=0;
};
