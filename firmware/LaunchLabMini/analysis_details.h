#pragma once
#include "decay_models.h"

namespace AnalysisDetails {
constexpr unsigned ROWS = 6;
struct Summary {
  float revolutions = 0, battleStaminaSeconds = NAN, decayRate = 0;
  bool complete = false;
  unsigned firstBand = 0, count = 0;
  float seconds[65] = {}; // Positive acoustic RPM fits in uint16_t.
};

// Integrate the same piecewise-linear graph the user sees, clipped to recording time.
inline Summary summarize(const Decay::Curve &curve, float launchRpm, float duration) {
  Summary result;
  if (duration <= 0 || launchRpm <= 0)
    return result;
  if (launchRpm <= 1000)
    result.battleStaminaSeconds = 0;
  result.firstBand = std::min(65u, unsigned(launchRpm / 1000));
  int boundary = int(result.firstBand);
  float entered = NAN;
  for (unsigned i = 1; i < Decay::CURVE_POINTS; ++i) {
    const float start = curve.seconds(i - 1), end = curve.seconds(i);
    if (start >= duration)
      break;
    const float a = curve.rpm[i - 1], b = curve.rpm[i];
    if (!std::isfinite(a) || !std::isfinite(b) || end <= start)
      return result;
    const float until = std::min(end, duration);
    auto interpolate = [&](float time) { return a + (b - a) * ((time - start) / (end - start)); };
    const float rpm = interpolate(until);
    result.revolutions += (a + rpm) * (until - start) / 120;
    while (boundary >= 0 && a > rpm && a >= boundary * 1000 && rpm <= boundary * 1000) {
      float crossed = start + (until - start) * ((a - boundary * 1000) / (a - rpm));
      if (boundary == 1 && !std::isfinite(result.battleStaminaSeconds))
        result.battleStaminaSeconds = crossed;
      if (std::isfinite(entered))
        result.seconds[result.count++] = crossed - entered;
      entered = crossed;
      --boundary;
    }
    if (until == duration) {
      result.complete = true;
      result.decayRate = (launchRpm - rpm) / duration;
    }
  }
  return result;
}
}
