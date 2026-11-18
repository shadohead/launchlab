#pragma once
#include <algorithm>
#include <cmath>

namespace EarlySpin {
constexpr unsigned FIRST_MS = 10000, WINDOW_MS = 30000, REFIT_MS = 2000;

// Regressions of manual finish time on original tracker locks, from the
// development pulls still spinning at 10, 15 and 30 seconds respectively.
inline float finish(unsigned count, unsigned elapsedMs, bool micAvailable = true) {
  if (elapsedMs < FIRST_MS) return NAN;
  if (!micAvailable) return 94.131f; // Median manual finish of the 36 development pulls.
  const bool first = elapsedMs < 15000;
  const float blend = first ? (elapsedMs - FIRST_MS) / 5000.f
                            : std::min(elapsedMs - 15000, 15000u) / 15000.f;
  const float start = first ? 39.0676f + 2.34672f * count : 35.4858f + 1.67287f * count;
  const float end = first ? 35.4858f + 1.67287f * count : 35.8013f + .959951f * count;
  return start + blend * (end - start);
}

enum class Source : unsigned { None=0, FixedRiccati=5, EarlyMic=6, Population=7, Stamina=8 };
struct Forecast {
  unsigned atMs = 0, rawCount = 0;
  float seconds = NAN;
};
struct Estimate {
  Source source = Source::None;
  float seconds = NAN;
};
inline Estimate select(float published, unsigned count, unsigned elapsedMs, bool measured) {
  if (std::isfinite(published)) return {Source::FixedRiccati,published};
  if (elapsedMs < FIRST_MS) return {};
  return {measured ? Source::EarlyMic : Source::Population,finish(count,elapsedMs,measured)};
}

inline bool refitDue(unsigned ms, unsigned rawCount, unsigned previousMs, unsigned previousCount) {
  return ms >= FIRST_MS && rawCount >= 10 && rawCount != previousCount && ms >= previousMs + REFIT_MS;
}
}
