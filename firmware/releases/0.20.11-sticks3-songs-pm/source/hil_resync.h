#pragma once
#include <stdint.h>

// After a failed HIL binary upload the host may still be streaming chunk
// data. Those bytes must never reach a command parser: on 2026-10-17 they ran
// production single-letter commands (backup, restore, a setting, sleep test,
// power off) and switched both rig M5s off. Swallow every byte until the host
// sends a "!sync" line or input has been quiet for QUIET_MS.
struct HilResync {
  static constexpr uint32_t QUIET_MS=1000;
  enum class Result:uint8_t {Pass,Swallowed,Synced,Quiet};
  bool active=false;uint8_t matched=0;uint32_t lastMs=0,discarded=0;
  void begin(uint32_t now){active=true;matched=0;lastMs=now;discarded=0;}
  // Quiet ends the resync and the byte is processed normally (Quiet); the
  // "!sync" line ends it and is consumed (Synced).
  Result feed(char c,uint32_t now) {
    if(!active)return Result::Pass;
    if(uint32_t(now-lastMs)>=QUIET_MS){active=false;return Result::Quiet;}
    lastMs=now;++discarded;
    static constexpr char SYNC[]="!sync\n";
    matched=c==SYNC[matched]?uint8_t(matched+1):c=='!'?1:0;
    if(matched==sizeof(SYNC)-1){active=false;matched=0;return Result::Synced;}
    return Result::Swallowed;
  }
};
