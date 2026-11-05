// When the mic runs: only while the Mic RPM page is open, the speaker waits its
// turn, and every way off the page ends with the mic down and the speaker free.
#include "../LaunchLabMini/mic_session.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace MicSession;

static constexpr int32_t ROOM=NEEDED_BYTES+1000;
struct Run {
  Machine m;
  Action go(bool wanted,bool speaker=false,int32_t room=ROOM,Worker worker=Worker::None){return m.step(wanted,speaker,room,worker);}
  // The page opens and the mic comes up.
  void toOn() {
    assert(go(true)==Action::None && m.state()==State::Clearing && m.muted());
    assert(go(true)==Action::Spawn && m.state()==State::Starting);
    assert(go(true,false,ROOM,Worker::Running)==Action::None && m.state()==State::On && m.muted());
  }
};
static void anOwnerWhoNeverOpensThePageNeverLeavesOff() {
  Machine m;
  for(unsigned i=0;i<1000;++i){assert(m.step(false,i&1,0,Worker::None)==Action::None);assert(m.off() && !m.muted());}
}
static void theSpeakerPowersDownFirst() {
  Run r;
  assert(r.go(true,true)==Action::None && r.m.state()==State::Clearing && r.m.muted());
  for(unsigned i=0;i<5;++i)assert(r.go(true,true)==Action::None && r.m.state()==State::Clearing);
  assert(r.go(true,false)==Action::Spawn);
}
// Device, 2e0ba398: a sound playing as the page opened showed Not enough
// memory. The speaker's own buffers were the shortage.
static void aPlayingSpeakerIsNotAMemoryShortage() {
  Run r;
  r.go(true,true,0);
  assert(r.go(true,true,0)==Action::None && r.m.state()==State::Clearing);
  assert(r.go(true,false,ROOM)==Action::Spawn);
}
static void leavingWhileTheSpeakerPowersDownStartsNothing() {
  Run r;
  r.go(true,true);
  assert(r.go(false,true)==Action::None && r.m.off() && !r.m.muted());
}
static void leavingStopsItAndGivesTheSpeakerBack() {
  Run r;r.toOn();
  assert(r.go(false,false,ROOM,Worker::Running)==Action::Quit && r.m.state()==State::Stopping && r.m.muted());
  assert(r.go(false,false,ROOM,Worker::Running)==Action::None && r.m.muted());  // the speaker waits for the codec
  assert(r.go(false,false,ROOM,Worker::Stopped)==Action::Reap && r.m.off() && !r.m.muted());
}
static void leavingDuringTheStartStillStops() {
  Run r;r.go(true);r.go(true);
  assert(r.go(false,false,ROOM,Worker::Starting)==Action::Quit && r.m.state()==State::Stopping);
  assert(r.go(true,false,ROOM,Worker::Stopped)==Action::Reap && r.m.off());
  // Back on the page at once: a clean start, with nothing left over.
  assert(r.go(true)==Action::None && r.m.state()==State::Clearing);
}
// Sleep waits for the mic to stop, but a stop that never ends holds it
// STOP_LIMIT_MS at most (check_keep_awake.py).
static void aStopThatNeverEndsHoldsSleepForALimitedTime() {
  assert(!holdsSleep(State::Off,0));
  for(State s:{State::Clearing,State::Starting,State::On,State::Refused})assert(holdsSleep(s,STOP_LIMIT_MS*10));
  assert(holdsSleep(State::Stopping,0) && holdsSleep(State::Stopping,STOP_LIMIT_MS-1));
  assert(!holdsSleep(State::Stopping,STOP_LIMIT_MS) && !holdsSleep(State::Stopping,STOP_LIMIT_MS*10));
  // Off the page each other state leaves at once, so only a stop can last.
  Run r;r.go(true);
  assert(r.go(false)==Action::None && r.m.off());
  r=Run{};r.go(true);r.go(true);
  assert(r.go(false,false,ROOM,Worker::Starting)==Action::Quit && r.m.state()==State::Stopping);
  r=Run{};r.go(true,false,0);r.go(true,false,0);
  assert(r.m.state()==State::Refused && r.go(false)==Action::None && r.m.off());
  r=Run{};r.toOn();
  assert(r.go(false,false,ROOM,Worker::Running)==Action::Quit);
  for(unsigned i=0;i<100;++i)assert(r.go(false,false,ROOM,Worker::Running)==Action::None && r.m.state()==State::Stopping);
}
static void reopeningWhileItStopsWaitsForIt() {
  Run r;r.toOn();
  r.go(false,false,ROOM,Worker::Running);
  assert(r.go(true,false,ROOM,Worker::Running)==Action::None && r.m.state()==State::Stopping);
  assert(r.go(true,false,ROOM,Worker::Stopped)==Action::Reap && r.m.off());
  assert(r.go(true)==Action::None);assert(r.go(true)==Action::Spawn);
}
static void tooLittleHeapRefusesAndLeavesTheSpeakerFree() {
  Run r;
  r.go(true);
  assert(r.go(true,false,NEEDED_BYTES-1)==Action::None && r.m.state()==State::Refused && r.m.why()==Why::Memory && !r.m.muted());
  // It stays refused while the page is open: no retry loop over the floor.
  assert(r.go(true,false,ROOM)==Action::None && r.m.state()==State::Refused);
  assert(r.go(false)==Action::None && r.m.off() && r.m.why()==Why::None);
}
static void exactlyEnoughHeapStarts() {
  Run r;r.go(true);
  assert(r.go(true,false,NEEDED_BYTES)==Action::Spawn);
}
static void fallingThroughTheFloorStopsIt() {
  Run r;r.toOn();
  assert(r.go(true,false,0,Worker::Running)==Action::None && r.m.state()==State::On);
  assert(r.go(true,false,-1,Worker::Running)==Action::Quit && r.m.state()==State::Stopping);
  assert(r.go(true,false,-1,Worker::Stopped)==Action::Reap && r.m.state()==State::Refused && r.m.why()==Why::Memory && !r.m.muted());
  assert(r.go(false)==Action::None && r.m.off());
}
static void aFailedStartIsShownAndRecovers() {
  Run r;r.go(true);r.go(true);
  assert(r.go(true,false,ROOM,Worker::Failed)==Action::Reap && r.m.state()==State::Refused && r.m.why()==Why::Failed && !r.m.muted());
  assert(r.go(false)==Action::None && r.m.off());
  r.toOn();
}
static void aTaskThatCannotBeCreatedIsAFailedStart() {
  Run r;r.go(true);r.go(true);
  r.m.spawnFailed();
  assert(r.m.state()==State::Refused && r.m.why()==Why::Failed && !r.m.muted());
}
static void aFailureWhileOnEndsIt() {
  Run r;r.toOn();
  assert(r.go(true,false,ROOM,Worker::Failed)==Action::Reap && r.m.state()==State::Refused);
}
static void aHealthyChainHasNothingToRelight() {
  Chain c;c.begin();
  assert(!c.refilled(true,false,true) && !c.relight(true));
  assert(!c.refilled(false,false,true) && !c.relight(true));  // a queue already full
}
static void aDeadChainIsLitAgainOnce() {
  Chain c;c.begin();
  assert(c.refilled(false,false,false));
  assert(c.relight(false) && !c.relight(false));
}
// Device, a8ee2234: the driver refuses a refill as the mic stops. That is no
// miss: it counted one in each quick toggle.
static void aStoppingMicIsNotAMiss() {
  Chain c;c.begin();
  assert(!c.refilled(false,true,false) && !c.relight(false));
}
// Device, a8ee2234: a break left by one session restarted capture in the next,
// over its requests in flight.
static void aBreakDoesNotOutliveItsSession() {
  Chain c;c.begin();
  c.refilled(false,false,false);
  c.begin();
  assert(!c.relight(false));
}
// The flag can be up by the time a request lands: nothing to light then.
static void aChainThatRecoveredIsLeftAlone() {
  Chain c;c.begin();
  c.refilled(false,false,false);
  assert(!c.relight(true) && !c.relight(false));
}
// The listener's ring: 8 s at 16 kHz less two chunks in flight, a second of room.
static constexpr uint64_t RATE=16000,USABLE=RATE*8-512*2,MARGIN=RATE;
// A release the ring still holds is read from the release.
static void aReleaseInTheRingIsReadFromTheRelease() {
  assert(resume(100000,100000+RATE,USABLE,MARGIN)==100000);
  assert(resume(100000,100000+USABLE-1,USABLE,MARGIN)==100000);
  assert(resume(5000,4000,USABLE,MARGIN)==5000);  // not yet written: wait for it
}
// a8ee2234, owner's pulls 3-6: each release was 8 s or more behind the newest
// sample when the task took it, and the spin was dropped whole. It is heard
// from the oldest sample still clear of the writer.
static void aLappedReleaseIsHeardLateNotDropped() {
  const uint64_t written=40*RATE,release=written-9*RATE;
  const uint64_t from=resume(release,written,USABLE,MARGIN);
  assert(from>release && from<written);
  assert(written-from==USABLE-MARGIN);
  assert(resume(from,written,USABLE,MARGIN)==from);  // and it stays there
}
// A release older than the session itself (sampleAt() gives 0) is the same case.
static void aReleaseBeforeTheSessionIsHeardFromTheOldestSample() {
  assert(resume(0,30*RATE,USABLE,MARGIN)==30*RATE-(USABLE-MARGIN));
  assert(resume(0,USABLE-1,USABLE,MARGIN)==0);
}
int main() {
  aReleaseInTheRingIsReadFromTheRelease();aLappedReleaseIsHeardLateNotDropped();aReleaseBeforeTheSessionIsHeardFromTheOldestSample();
  anOwnerWhoNeverOpensThePageNeverLeavesOff();theSpeakerPowersDownFirst();aPlayingSpeakerIsNotAMemoryShortage();leavingWhileTheSpeakerPowersDownStartsNothing();
  leavingStopsItAndGivesTheSpeakerBack();leavingDuringTheStartStillStops();reopeningWhileItStopsWaitsForIt();
  aStopThatNeverEndsHoldsSleepForALimitedTime();
  tooLittleHeapRefusesAndLeavesTheSpeakerFree();exactlyEnoughHeapStarts();fallingThroughTheFloorStopsIt();
  aFailedStartIsShownAndRecovers();aTaskThatCannotBeCreatedIsAFailedStart();aFailureWhileOnEndsIt();
  aHealthyChainHasNothingToRelight();aDeadChainIsLitAgainOnce();aStoppingMicIsNotAMiss();aBreakDoesNotOutliveItsSession();aChainThatRecoveredIsLeftAlone();
  std::puts("mic_session tests passed");
}
