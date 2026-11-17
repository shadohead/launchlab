#pragma once
#include <atomic>
#include <cstdint>

// When the mic runs. It holds the ES8311 and its clock pins, which the speaker
// needs, and the task behind it takes DMA-capable heap, so it runs only while
// the Mic RPM page is open: every way off the page ends it, and an owner who
// never opens the page never leaves Off. Pure, so every path is host-tested;
// mic_listener.h supplies the task and the sketch the page.
namespace MicSession {
// DMA-capable heap, HEAP_STATUS's dma_free, not its larger internal_free. The
// floor is the release gate's; the mic needs NEEDED_BYTES above it to start, and
// stops if it falls through. Its ring, tracker and task stack are all in PSRAM:
// this is the I2S driver, its DMA and the capture task.
constexpr int32_t FLOOR_BYTES=32*1024,NEEDED_BYTES=9*1024;
enum class State:uint8_t {
  Off,       // nothing runs and the speaker is free
  Clearing,  // the speaker is muted and powering down
  Starting,  // the task is bringing the mic up
  On,        // capturing
  Stopping,  // the task is taking the mic down
  Refused    // the page is open and the mic is not: the speaker is free again
};
enum class Why:uint8_t {None,Memory,Failed};
// Whether the chain of capture requests has died. The capture callback refills
// it as each chunk lands, and a refused refill is harmless while a request is
// still queued or while the mic is stopping, when the driver refuses it on
// purpose. A break belongs to its session: carried into the next it would
// relight a healthy chain, over the requests already in flight.
class Chain {
public:
  void begin(){broken_=false;}
  // From the capture callback, after a refill. True when this is a real miss.
  bool refilled(bool queued,bool stopping,bool recording) {
    if(queued || stopping || recording)return false;
    broken_=true;return true;
  }
  // From the task: true once when the chain must be lit again.
  bool relight(bool recording){return broken_.exchange(false) && !recording;}
private:
  std::atomic<bool> broken_{false};
};
// Where a reader of the capture ring goes on: at `at`, or, once the writer at
// `written` is within `usable` samples of lapping it, `margin` clear of that
// point. A spin whose release the ring has already lost (a late result, a late
// task) is heard from there rather than dropped (owner, 2026-11-02: four of six
// pulls were never analyzed), and a reader that falls behind mid-spin skips
// ahead the same way.
inline uint64_t resume(uint64_t at,uint64_t written,uint64_t usable,uint64_t margin) {
  if(written<at || written-at<usable)return at;
  return written-(usable>margin?usable-margin:0);
}
// Whether sleep or power-off waits for the mic, which holds the codec and its
// clock pins. A stop normally takes milliseconds; one that never ends (the task
// stuck in a spin, in M5Unified's mic end() or on the gate) must not keep the unit
// awake, so a stop holds them STOP_LIMIT_MS at most after it was asked. Every
// other state leaves at the loop's next step once the page is gone.
constexpr uint32_t STOP_LIMIT_MS=30000;
inline bool holdsSleep(State state,uint32_t stoppingMs) {
  return state!=State::Off && (state!=State::Stopping || stoppingMs<STOP_LIMIT_MS);
}
// What the task says it is doing.
enum class Worker:uint8_t {None,Starting,Running,Failed,Stopped};
enum class Action:uint8_t {None,Spawn,Quit,Reap};
class Machine {
public:
  State state()const{return state_;}
  Why why()const{return why_;}
  bool muted()const{return state_==State::Clearing || state_==State::Starting || state_==State::On || state_==State::Stopping;}
  bool off()const{return state_==State::Off;}
  // Each loop pass: `room` is the heap above the floor.
  Action step(bool wanted,bool speakerPowered,int32_t room,Worker worker) {
    switch(state_) {
      case State::Off:
        if(wanted){state_=State::Clearing;why_=Why::None;}
        break;
      case State::Clearing:
        // A playing speaker holds the heap the mic needs: judge it once that is back.
        if(!wanted)state_=State::Off;
        else if(speakerPowered)break;
        else if(room<NEEDED_BYTES)refuse(Why::Memory);
        else {state_=State::Starting;return Action::Spawn;}
        break;
      case State::Starting:
        if(worker==Worker::Failed)return reap(Why::Failed);
        if(!wanted || room<0)return quit(wanted?Why::Memory:Why::None);
        if(worker==Worker::Running)state_=State::On;
        break;
      case State::On:
        if(worker==Worker::Failed)return reap(Why::Failed);
        if(!wanted || room<0)return quit(wanted?Why::Memory:Why::None);
        break;
      case State::Stopping:
        if(worker==Worker::Stopped || worker==Worker::Failed)return reap(why_);
        break;
      case State::Refused:
        if(!wanted){state_=State::Off;why_=Why::None;}
        break;
    }
    return Action::None;
  }
  // The task could not be created.
  void spawnFailed(){refuse(Why::Failed);}
private:
  void refuse(Why why){state_=State::Refused;why_=why;}
  Action quit(Why why){state_=State::Stopping;why_=why;return Action::Quit;}
  Action reap(Why why) {
    if(why==Why::None)state_=State::Off;
    else refuse(why);
    return Action::Reap;
  }
  State state_=State::Off;Why why_=Why::None;
};
}
