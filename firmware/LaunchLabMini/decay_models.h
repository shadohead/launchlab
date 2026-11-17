#pragma once
#include "decay_fit.h"

namespace Decay {
inline auto riccatiDrag(const Params &p) {
  return [&p](float x) {
    x = std::max(0.f, x);
    return p.a + p.b * x + p.c * x * x;
  };
}

inline Result optimizeRiccati(const Workspace &work, unsigned count, float x0, const Config &cfg,
                              Continue fn, void *ctx) {
  constexpr Model model = Model::Riccati;
  const bool anchored = work.stopDuration > 0;
  const unsigned dimension = anchored ? 2 : 3;
  const float scale = std::max(.005f, (x0 - work.points[count - 1].x) /
                                      (work.points[count - 1].t + work.startOffset));
  auto coefficients = [&](const float *q) {
    Params p;
    if (!anchored) {
      p.a = std::max(cfg.aFloor, q[0] * scale);
      p.b = q[1] * scale / x0;
      p.c = q[2] * scale / (x0 * x0);
      return p;
    }
    p.a = 1;
    p.b = std::expm1(q[0]) / x0;
    p.c = std::expm1(q[1]) / (x0 * x0);
    const float factor = anchoredIntegral(riccatiDrag(p), x0, cfg, fn, ctx) / work.stopDuration;
    p.a *= factor;
    p.b *= factor;
    p.c *= factor;
    return p;
  };
  auto seed = [&](float *q, unsigned start) {
    q[0] = start == 0 ? .8f : start == 1 ? .2f : .45f;
    q[1] = (1 - q[0]) * .5f;
    q[2] = .3f;
    if (anchored) {
      q[0] = .2f + start;
      q[1] = .4f;
    }
  };
  return fitSimplex(model, count, dimension, 3, anchored, cfg, fn, ctx, coefficients,
      [&](const Params &p) { return objective(riccatiDrag(p), model, p, x0, work, count, cfg, fn, ctx); },
      seed, [&](const Result &r) { return predictStopTime(riccatiDrag(r.params), r, x0 * 1000, cfg); });
}

inline Results fitFreeRiccati(const Session &session, Workspace &work, Continue fn, void *ctx) {
  Results out;
  Config cfg;
  prepareFit(session, work, out, cfg);
  constexpr unsigned slot = unsigned(Model::Riccati);
  auto &r = out.models[slot];
  r = fitOneModel(Model::Riccati, out, work, cfg, fn, ctx, optimizeRiccati,
      [](Result &fit, auto &&...args) { scoreFit(riccatiDrag(fit.params), fit, args...); },
      [&](const Result &fit) { return predictStopTime(riccatiDrag(fit.params), fit, startingRpm(out), cfg); });
  if (!proceed(fn, ctx)) out.cancelled = true;
  else if (r.valid && r.converged && r.validationConverged && r.validation.valid) {
    out.best = int(slot);
    std::rotate(out.ranking, out.ranking + slot, out.ranking + slot + 1);
  }
  return out;
}

inline void makeRiccatiCurve(const Results &fit, Curve &curve, Continue fn = nullptr, void *ctx = nullptr) {
  const auto slot = unsigned(Model::Riccati);
  if (fit.endpointEstimate) {
    // Evaluate the endpoint estimate directly. Tiny recording durations can
    // require coefficients far outside the numerical fitter's search bounds.
    curve = {};
    curve.end = fit.actualStop;
    const auto &p = fit.models[slot].params;
    const double q = std::sqrt(4. * p.a * p.c - double(p.b) * p.b);
    const double start = std::atan((2. * p.c * fit.launcherRpm / 1000 + p.b) / q);
    const double end = std::atan(p.b / q);
    for (unsigned i = 0; i < CURVE_POINTS; ++i) {
      const float at = curve.seconds(i);
      const double fraction = (at - fit.launcherTime) / (fit.actualStop - fit.launcherTime);
      curve.rpm[i] = at < fit.launcherTime ? NAN : std::clamp(
          float(1000 * (q * std::tan(start + fraction * (end - start)) - p.b) / (2 * p.c)),
          0.f, fit.launcherRpm);
    }
    if (fit.launcherTime == 0) curve.rpm[0] = fit.launcherRpm;
    curve.rpm[CURVE_POINTS - 1] = 0;
    return;
  }
  renderCurve(riccatiDrag(fit.models[slot].params), fit, slot, curve, {}, fn, ctx);
}
} // namespace Decay
