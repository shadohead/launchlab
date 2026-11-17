#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

// Independent of acquisition, display, RTOS and persistence. Units: kRPM, seconds.
namespace Decay {
constexpr unsigned MODEL_COUNT = 4, MAX_SAMPLES = 3000, CURVE_POINTS = 128;
// Stable slots in version-1 calibration records, including older four-model results.
enum class Model : uint8_t { Linear, Quadratic, Riccati, Power };
struct Params {
  float a = 0, b = 0, c = 0, k = 0, n = 1;
};
struct Config {
  float huber = .15f, step = .1f, aFloor = .0001f, maxCoefficient = 10, nMin = .5f, nMax = 3;
  float maxStop = 3600, lossTolerance = .000001f, validationTieRpm = 15;
  unsigned evaluations = 480, stopResolution = 256, stopMaxResolution = 8192;
};
struct Sample {
  uint32_t ms = 0;
  float rpm = 0;
  bool rejected = false, excludedFromFit = false, recovered = false;
  // Zero preserves original/confirmed acquisition evidence. Nonzero identifies
  // a weaker observation recovered from relative pitch, with confidence 1..255.
  uint8_t pitchConfidence = 0;
};
struct Session {
  float launcherRpm = 0;
  uint32_t launcherMs = 0, stopMs = 0;
  bool hasStop = false;
  unsigned count = 0;
  Sample samples[MAX_SAMPLES];
};
struct Observation {
  float t = 0, x = 0, w = 0;
};
struct Workspace {
  Observation points[MAX_SAMPLES];
  static constexpr unsigned initialWindow = 15;
  float scratch[initialWindow * (initialWindow - 1) / 2]; // One slope per pair in the initial window.
  unsigned count = 0;
  float startOffset = 0, stopDuration = 0;
};
struct Metric {
  bool valid = false;
  float rmse = 0, mae = 0, maxError = 0;
};
struct Result {
  Model model = Model::Linear;
  Params params;
  bool valid = false, converged = false, validationConverged = false, boundary = false, unstable = false;
  bool stopValid = false, stopErrorValid = false;
  Metric training, validation, all;
  float stopSeconds = 0, stopError = 0, absoluteStopError = 0, loss = 0;
  unsigned evaluations = 0, sampleCount = 0;
};
// Original mic locks within 150 RPM of the published Fixed Riccati fit.
struct RiccatiSupport {
  uint16_t accepted = 0, original = 0;
};
struct Results {
  static constexpr uint32_t VERSION = 1;
  Result models[MODEL_COUNT];
  float acousticStart = 0, initialRpm = 0, launcherRpm = 0, launcherTime = 0, lastTime = 0, actualStop = 0;
  float tailRpm = 0, tailSlope = 0; // Last included reading and incoming slope, RPM and RPM/s.
  bool fixedRiccati = false; // The Riccati slot uses the shared drag shape.
  bool endpointEstimate = false; // Shape scaled to the IR launch and manual stop, without a mic fit.
  RiccatiSupport support{};
  bool opticalAnchor = false, manualAnchor = false, actualStopValid = false, invalidStop = false,
       validationPossible = false, cancelled = false;
  unsigned sourceCount = 0, sampleCount = 0, spikes = 0;
  int best = -1;
  uint8_t ranking[MODEL_COUNT] = {0, 1, 2, 3};
};
struct Curve {
  float end = 0, joinTime = 0;
  unsigned joinIndex = 0;
  float rpm[CURVE_POINTS] = {};
  float seconds(unsigned i) const {
    const unsigned last = CURVE_POINTS - 1;
    if (i == last)
      return end;
    if (!joinIndex)
      return end * i / last;
    if (i == joinIndex)
      return joinTime;
    if (i < joinIndex)
      return joinTime * (float(i) / joinIndex);
    return joinTime + (end - joinTime) * (float(i - joinIndex) / (last - joinIndex));
  }
};
using Continue = bool (*)(void *);
inline bool proceed(Continue fn, void *ctx) { return !fn || fn(ctx); }
inline bool validParams(Model m, const Params &p, const Config &cfg = {}) {
  const float values[] = {p.a, p.b, p.c, p.k, p.n};
  for (float v : values)
    if (!std::isfinite(v))
      return false;
  return p.a >= cfg.aFloor && p.a <= cfg.maxCoefficient && p.b >= 0 && p.b <= cfg.maxCoefficient &&
         p.c >= 0 && p.c <= cfg.maxCoefficient && p.k >= 0 && p.k <= cfg.maxCoefficient &&
         (m != Model::Power || (p.n >= cfg.nMin && p.n <= cfg.nMax));
}
} // namespace Decay
