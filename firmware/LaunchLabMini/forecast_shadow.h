#pragma once
#include "early_spin.h"
#include "decay_types.h"
#include <cstdint>

namespace ForecastShadow {
constexpr unsigned TIMES_MS[] = {10000, 15000, 30000};
constexpr unsigned COUNT = 3;
struct Checkpoint {
  uint32_t observedMs = 0;
  EarlySpin::Estimate displayed{};
};
struct Log {
  Checkpoint rows[COUNT]{};
  unsigned next = 0;
  void reset() { next = 0; for (auto &row : rows) row = Checkpoint{}; }
  bool due(unsigned elapsedMs) const { return next < COUNT && elapsedMs >= TIMES_MS[next]; }

  void observe(const EarlySpin::Forecast &forecast) {
    if (!due(forecast.atMs) || !std::isfinite(forecast.seconds))
      return;
    // A delayed loop can miss a checkpoint. Record only the time actually evaluated.
    while (next + 1 < COUNT && forecast.atMs >= TIMES_MS[next + 1])
      ++next;
    rows[next++] = {forecast.atMs, {EarlySpin::Source::Stamina, forecast.seconds}};
  }
};
} // namespace ForecastShadow
