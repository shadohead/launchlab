#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Only the application's own records are erased. The journal is outside this
// list, so interruption always leaves either intact data or a resumable wipe.
namespace DeviceWipe {
enum class State:uint8_t { Clean, Pending, Invalid, Error };
enum class Result:uint8_t { Complete, Failed };
inline constexpr const char *NAMESPACES[]={"launchlab-mini","ll-practice","ll-appearance","ll-bests",
  "ll-motion","ll-power","ll-prac-qa","ll-mot-qa"};
inline constexpr size_t NAMESPACE_COUNT=sizeof NAMESPACES/sizeof NAMESPACES[0];
inline constexpr const char *INTENT_SPACE="ll-reset",*INTENT_KEY="pending";
// Fixed complete blob, including format version and reserved bytes. NVS adds
// its own storage CRC. Every byte must match before automatic boot recovery.
inline constexpr uint8_t INTENT_BYTES[]={'L','L','W','P',1,0,0,0};
inline bool ownedNamespace(const char *space) {
  if(!space)return false;
  for(const auto *name:NAMESPACES)if(std::strcmp(space,name)==0)return true;
  return false;
}

// Store contract:
//   inspectIntent() reads through a fresh handle, distinguishing absent,
//     exact pending blob, malformed/type-mismatched blob and storage errors.
//   setIntent() commits the complete canonical marker durably.
//   clearNamespace(name) commits erasure and verifies absence through a fresh
//     handle. Missing namespaces are already clear; other errors fail closed.
//   clearFlash() erases the spiffs partition (the history log and any later
//     store there, docs/FIRMWARE_FLASH_LAYOUT.md); no partition is clear.
//   clearIntent() commits marker removal. Core verifies its absence last.
// Calls must be serialized against every app writer. After any final confirmed
// attempt, the caller must prevent old RAM records from being saved or used,
// and restart only after success. At boot inspect/resume precedes record loads.
template<class Store> State inspect(Store &store) {
  const auto state=store.inspectIntent();
  switch(state){case State::Clean:case State::Pending:case State::Invalid:case State::Error:return state;}
  return State::Error;
}
namespace Detail {
template<class Store> Result finish(Store &store) {
  for(const auto *space:NAMESPACES)if(!store.clearNamespace(space))return Result::Failed;
  if(!store.clearFlash())return Result::Failed;
  if(!store.clearIntent())return Result::Failed;
  return inspect(store)==State::Clean?Result::Complete:Result::Failed;
}
}
// Explicitly confirmed user action may replace an invalid journal. It must
// still durably commit and read back a valid marker before erasing anything.
template<class Store> Result begin(Store &store) {
  const auto state=inspect(store);
  if(state==State::Error)return Result::Failed;
  if(state!=State::Pending) {
    if(!store.setIntent() || inspect(store)!=State::Pending)return Result::Failed;
  }
  return Detail::finish(store);
}
// Automatic recovery never replaces a malformed journal or guesses intent.
// Restart the small fixed list rather than persisting a progress counter.
template<class Store> Result resume(Store &store) {
  const auto state=inspect(store);
  if(state==State::Clean)return Result::Complete;
  return state==State::Pending?Detail::finish(store):Result::Failed;
}
}
