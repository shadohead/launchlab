#pragma once
#include <cstdint>

class LaunchFeedback {
public:
  static constexpr uint32_t DISPLAY_MS=5000;
  void show(uint32_t now){started_=now;shown_=true;replayStarted_=false;lastReplayElapsed_=0;}
  void clear(){shown_=false;}
  void toggle(uint32_t now,bool available){if(shown_)clear();else if(available)show(now);}
  bool tick(uint32_t now){if(shown_ && now-started_>=DISPLAY_MS){clear();return true;}return false;}
  bool shown()const{return shown_;}
  uint32_t elapsed(uint32_t now)const{return shown_?now-started_:0;}
  uint32_t replayElapsed(uint32_t now)const{return replayStarted_?elapsed(now):0;}
  bool replayStarted()const{return shown_ && replayStarted_;}
  uint32_t lastReplayElapsed()const{return lastReplayElapsed_;}
  // Capture, flash and rendering can finish long after the recap was opened.
  // Arm the 1x playback clock only after its first complete frame is presented.
  void replayPresented(uint32_t now,bool completeFrame,uint32_t sampledElapsed) {
    if(!shown_ || !completeFrame)return;
    if(!replayStarted_){started_=now;replayStarted_=true;lastReplayElapsed_=0;}
    else lastReplayElapsed_=sampledElapsed;
  }
private:
  uint32_t started_=0,lastReplayElapsed_=0;bool shown_=false,replayStarted_=false;
};
