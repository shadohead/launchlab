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
  float corrected = NAN;
  float limitedFinish = NAN;
  Decay::RiccatiSupport support{};
};
struct Log {
  Checkpoint rows[COUNT]{};
  unsigned next = 0;
  void reset() { next = 0; for (auto &row : rows) row = Checkpoint{}; }
  bool due(unsigned elapsedMs) const { return next < COUNT && elapsedMs >= TIMES_MS[next]; }

  template <class Trace>
  void observe(unsigned elapsedMs, float launchRpm, const Trace *trace, bool micReady, float published,
               Decay::RiccatiSupport support) {
    if (!due(elapsedMs))
      return;
    // A delayed loop can miss an entire checkpoint. Never invent an earlier
    // prediction from the later snapshot. Keep the actual observation time.
    while (next + 1 < COUNT && elapsedMs >= TIMES_MS[next + 1])
      ++next;
    const unsigned index = next++;
    auto &row = rows[index];
    row.observedMs = elapsedMs;
    row.displayed = EarlySpin::select(published, trace ? trace->earlyReadings() : 0, elapsedMs,
                                      micReady && trace && trace->started());
    if (row.displayed.source == EarlySpin::Source::FixedRiccati) {
      row.support = support;
      if (trace && trace->started()) {
        row.limitedFinish = published;
        if (2 * support.accepted < support.original) {
          const float countFinish = EarlySpin::finish(trace->earlyReadings(), elapsedMs);
          row.limitedFinish = std::min(published, std::max(elapsedMs * .001f, countFinish));
        }
      }
      return;
    }
    if (row.displayed.source != EarlySpin::Source::EarlyMic)
      return;

    // Ridge correction trained on the original 42 pulls, fallback frames only.
    // Same features and ordering as the offline 10/15/30-second comparison.
    constexpr float coefficients[COUNT][5] = {{-1.790140f, -13.191932f, .869314f, 2.749907f, .041277f},
                                              {-.890337f, -6.230617f, .124523f, 2.280434f, .705237f},
                                              {-3.796362f, -12.352269f, -3.058424f, .329759f, 1.930420f}};
    unsigned n = 0, accepted = 0;
    uint16_t recent[5]{};
    for (unsigned i = 0; i < trace->count(); ++i) {
      const auto rpm = trace->rawRpm(i);
      if (!rpm || trace->recovered(i))
        continue;
      recent[n++ % 5] = rpm;
      if (!trace->rejectedRpm(i))
        ++accepted;
    }
    const unsigned size = std::min(n, 5u);
    float ratio = .75f;
    if (size && launchRpm > 0) {
      std::sort(recent, recent + size);
      ratio = (recent[(size - 1) / 2] + recent[size / 2]) / (2.f * launchRpm);
    }
    const auto &c = coefficients[index];
    row.corrected = row.displayed.seconds + c[0] + c[1] * (500.f * n / elapsedMs) +
                    c[2] * (float(accepted) / std::max(n, 1u)) + c[3] * ((launchRpm * .001f - 6.f) / 2.f) +
                    c[4] * (ratio - .75f);
  }
};
} // namespace ForecastShadow
