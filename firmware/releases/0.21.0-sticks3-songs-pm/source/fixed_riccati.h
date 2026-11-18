#pragma once
#include "decay_models.h"

namespace Decay {
namespace FixedRiccati {
// x is in thousands of RPM. The shared shape leaves only the time scale and
// acoustic intercept to estimate from a new spin.
constexpr float A = .00360f, B = .00380f, C = .00589f, TOPPLE = .578f;
constexpr float PITCH_WEIGHT = .25f;
// A manual stop supplies two measured endpoints even when the mic cannot
// support a fit. Use the shared shape between them, without claiming inliers
// or validation. This also works when there is no memory for a fitting job.
inline bool estimate(float launchRpm, uint32_t launchMs, uint32_t stopMs, unsigned sourceCount,
                     Results &out, Curve &curve) {
  if (!(launchRpm > 0 && launchRpm <= 65535) || stopMs <= launchMs)
    return false;
  const float startTime = launchMs / 1000.f, stopTime = stopMs / 1000.f;
  if (stopTime <= startTime) return false;
  out = {};
  out.fixedRiccati = out.endpointEstimate = true;
  out.opticalAnchor = out.manualAnchor = out.actualStopValid = true;
  out.launcherRpm = out.initialRpm = launchRpm;
  out.launcherTime = out.acousticStart = out.lastTime = startTime;
  out.actualStop = stopTime;
  const double q = std::sqrt(4. * A * C - double(B) * B);
  const double angle = std::atan((2. * C * launchRpm / 1000 + B) / q) - std::atan(B / q);
  const float scale = float(2 * angle / (q * (out.actualStop - out.launcherTime)));
  out.sourceCount = std::min(sourceCount, MAX_SAMPLES);
  out.best = int(Model::Riccati);
  std::rotate(out.ranking, out.ranking + out.best, out.ranking + out.best + 1);
  auto &result = out.models[out.best];
  result.model = Model::Riccati;
  result.params = {scale * A, scale * B, scale * C};
  result.valid = result.stopValid = true;
  result.stopSeconds = out.actualStop;
  makeRiccatiCurve(out, curve);
  return true;
}
inline bool fit(const Session &input, Workspace &work, Results &out, Curve &curve, Continue fn = nullptr,
                void *ctx = nullptr) {
  const unsigned count = input.count;
  if (count < 10 || count > MAX_SAMPLES)
    return false;
  const uint32_t firstMs = input.samples[0].ms, lastMs = input.samples[count - 1].ms;
  if (lastMs < firstMs || lastMs - firstMs < 2000)
    return false;
  if (input.hasStop && (!(input.launcherRpm > 0) || !std::isfinite(input.launcherRpm) ||
                        firstMs < input.launcherMs || lastMs >= input.stopMs))
    return false;

  const float q = std::sqrt(4 * A * C - B * B);
  auto angle = [&](float x) { return std::atan((2 * C * x + B) / q); };
  auto speed = [&](float u) {
    if (std::fabs(u) >= 1.5707963f)
      return float(NAN);
    return (q * std::tan(u) - B) / (2 * C);
  };
  unsigned evidenceCount = 0;
  uint32_t firstEvidenceMs = 0, lastEvidenceMs = 0;
  for (unsigned i = 0; i < count; ++i) {
    const auto &s = input.samples[i];
    if (!(s.rpm > 0 && s.rpm <= 65535) || (i && s.ms <= input.samples[i - 1].ms))
      return false;
    const float x = .001f * s.rpm;
    const float derivative = 2 * (A + B * x + C * x * x) / q; // dx/du
    work.points[i] = {.001f * s.ms, angle(x), derivative * derivative};
    if (s.pitchConfidence)
      work.points[i].w *= PITCH_WEIGHT * s.pitchConfidence / 255.f;
    else {
      if (!evidenceCount++)
        firstEvidenceMs = s.ms;
      lastEvidenceMs = s.ms;
    }
  }
  // Weak relative observations can refine an independently supported curve,
  // but cannot supply its minimum observation count or time coverage.
  if (evidenceCount < 10 || lastEvidenceMs - firstEvidenceMs < 2000)
    return false;
  const bool hasPitch = evidenceCount != count;
  double intercept = 0, slope = 0;
  for (unsigned pass = 0; pass < (hasPitch ? 16u : 8u); ++pass) {
    const bool includePitch = hasPitch && pass >= 8;
    auto robustWeight = [&](unsigned i) {
      double weight = work.points[i].w;
      if (input.samples[i].pitchConfidence && !includePitch)
        return 0.;
      if (pass) {
        const float error = std::fabs(speed(intercept + slope * work.points[i].t) -
                                      .001f * input.samples[i].rpm);
        if (!std::isfinite(error))
          return 0.;
        if (error > .15f)
          weight *= .15f / error;
      }
      return weight;
    };
    double pitchScale = 1;
    if (includePitch) {
      double evidenceWeight = 0, pitchWeight = 0;
      for (unsigned i = 0; i < count; ++i) {
        if (i % 64 == 0 && !proceed(fn, ctx))
          return false;
        const double weight = robustWeight(i);
        (input.samples[i].pitchConfidence ? pitchWeight : evidenceWeight) += weight;
      }
      // A dense recovered run must not outweigh the genuine observations.
      // Reapply the cap after Huber weighting, including when originals are noisy.
      if (pitchWeight > PITCH_WEIGHT * evidenceWeight)
        pitchScale = PITCH_WEIGHT * evidenceWeight / pitchWeight;
    }
    double sum = 0, meanTime = 0, meanAngle = 0, variance = 0, covariance = 0;
    for (unsigned i = 0; i < count; ++i) {
      if (i % 64 == 0 && !proceed(fn, ctx))
        return false;
      const auto &p = work.points[i];
      double weight = robustWeight(i);
      if (input.samples[i].pitchConfidence)
        weight *= pitchScale;
      if (!(weight > 0))
        continue;
      // Weighted covariance around the running means avoids subtracting
      // nearly equal sums when the mic first locks late in the spin.
      sum += weight;
      const double dt = p.t - meanTime, du = p.x - meanAngle;
      meanTime += weight * dt / sum;
      meanAngle += weight * du / sum;
      variance += weight * dt * (p.t - meanTime);
      covariance += weight * dt * (p.x - meanAngle);
    }
    if (!(variance > 0))
      return false;
    slope = covariance / variance;
    intercept = meanAngle - slope * meanTime;
  }
  const double scale = -2 * slope / q;
  if (!(scale >= .3 && scale <= 3))
    return false;
  const float finish = (angle(TOPPLE) - intercept) / slope;
  if (!(finish > 0 && std::isfinite(finish)))
    return false;

  unsigned kept = 0, keptEvidence = 0;
  RiccatiSupport support{};
  float first = 0, last = 0, firstEvidence = 0, lastEvidence = 0;
  for (unsigned i = 0; i < count; ++i) {
    const float t = work.points[i].t, x = .001f * input.samples[i].rpm;
    if (std::fabs(speed(intercept + slope * t) - x) <= .15f) {
      if (!input.samples[i].pitchConfidence) {
        if (!keptEvidence++)
          firstEvidence = t;
        lastEvidence = t;
      }
      if (!input.samples[i].recovered && !input.samples[i].pitchConfidence) {
        ++support.original;
        if (!input.samples[i].rejected)
          ++support.accepted;
      }
      if (!kept)
        first = t;
      last = t;
      work.points[kept++] = {t - first, x, 1};
    }
  }
  if (10 * keptEvidence < 7 * evidenceCount ||
      (hasPitch && (keptEvidence < 10 || lastEvidence - firstEvidence < 2.f)))
    return false;
  if (input.hasStop) {
    const float firstRpm = 1000 * speed(intercept + slope * first);
    const float lastRpm = 1000 * speed(intercept + slope * last);
    if (!(last > first && firstRpm > 0 && lastRpm > 0) ||
        !std::isfinite(firstRpm) || !std::isfinite(lastRpm))
      return false;
    out.initialRpm = firstRpm;
    out.tailRpm = lastRpm;
  }
  work.count = kept;
  out.fixedRiccati = true;
  out.support = support;
  out.best = int(Model::Riccati);
  auto &result = out.models[out.best];
  result.model = Model::Riccati;
  result.valid = result.stopValid = true;
  result.params = {float(scale * A), float(scale * B), float(scale * C)};
  result.stopSeconds = finish;
  out.acousticStart = first;
  out.lastTime = last;
  out.sourceCount = count;
  out.sampleCount = kept;
  out.launcherTime = input.launcherMs * .001f;
  out.launcherRpm = input.launcherRpm;
  if (input.hasStop) {
    // Retain the acoustic fit between its inliers. The stopped graph joins it
    // to the IR launch and uses its final tangent to ease into the manual zero.
    out.actualStop = input.stopMs / 1000.f;
    out.opticalAnchor = out.manualAnchor = out.actualStopValid = true;
    const float tangent = -1000 * riccatiDrag(result.params)(out.tailRpm / 1000);
    out.tailSlope = std::clamp(tangent, -3 * out.tailRpm / (out.actualStop - last), 0.f);
    result.sampleCount = kept;
    result.stopSeconds = out.actualStop;
    std::rotate(out.ranking, out.ranking + out.best, out.ranking + out.best + 1);
    makeRiccatiCurve(out, curve, fn, ctx);
    return true;
  }
  curve.end = finish;
  for (unsigned i = 0; i < CURVE_POINTS; ++i)
    curve.rpm[i] = 1000 * speed(intercept + slope * curve.seconds(i));
  return true;
}
} // namespace FixedRiccati

inline Results fitLiveModels(const Session &input, Workspace &work, Curve &curve, Continue fn = nullptr,
                             void *ctx = nullptr) {
  Results out;
  curve = {};
  if (!input.hasStop && FixedRiccati::fit(input, work, out, curve, fn, ctx))
    return out;
  if (!proceed(fn, ctx)) {
    out.cancelled = true;
    return out;
  }
  // No credible fitted curve. The display can still report the empirical
  // finish from the initial observation count, without drawing a false tail.
  prepare(input, work, out, {});
  return out;
}

inline Results fitStoppedModels(const Session &input, Workspace &work, Curve &curve, Continue fn = nullptr,
                               void *ctx = nullptr) {
  Results out;
  curve = {};
  if (FixedRiccati::fit(input, work, out, curve, fn, ctx)) {
    // A timeout records elapsed time without asserting a manual endpoint.
    if (!input.hasStop) out.actualStop = input.stopMs / 1000.f;
    return out;
  }
  out = fitFreeRiccati(input, work, fn, ctx);
  if (!proceed(fn, ctx)) {
    out.cancelled = true;
    return out;
  }
  if (out.best < 0 && input.hasStop && FixedRiccati::estimate(
          input.launcherRpm, input.launcherMs, input.stopMs, input.count, out, curve)) {
    work.count = 0; // The estimate did not fit any mic readings; keep them gray.
    return out;
  }
  if (out.best >= 0 && proceed(fn, ctx))
    makeRiccatiCurve(out, curve, fn, ctx);
  return out;
}
} // namespace Decay
