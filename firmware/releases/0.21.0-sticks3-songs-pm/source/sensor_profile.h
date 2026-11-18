#pragma once
#include "analog_tachometer.h"

// One application with saved runtime sensitivity. Keep the existing
// QRE/shared sensitivity unchanged. The 2026-10-07 TCRT trial uses
// 60 counts: its two reported pulls contain 62.72-71.40 count marks, below the
// previous 80-count gate. Replay cannot establish absolute shaft RPM.
namespace StickS3SensorProfile {
enum class Mode:uint8_t {Standard,Tcrt};
static constexpr Mode DEFAULT_MODE=Mode::Standard;
static constexpr float MIN_MARK=80.0f, EDGE_SWING_FRACTION=0.70f;
inline bool valid(uint32_t value){return value<=uint32_t(Mode::Tcrt);}
inline float minMark(Mode mode){return mode==Mode::Tcrt?60.0f:MIN_MARK;}
inline const char *name(Mode mode){return mode==Mode::Tcrt?"tcrt5000":"standard";}
inline const char *label(Mode mode){return mode==Mode::Tcrt?"TCRT5000":"QRE / Standard";}
static inline void configure(AnalogTachometer &detector,Mode mode=DEFAULT_MODE) {
  detector.minMark=minMark(mode);
  detector.edgeSwingFraction=EDGE_SWING_FRACTION;
  detector.singleTurnPeak=false;
  detector.trackSingleTurnPeak=true;
}
}
