#pragma once
#include "decay_fit.h"

namespace Decay {
inline float deceleration(Model m, const Params &p, float x) {
  x = std::max(0.f, x);
  switch (m) {
  case Model::Linear:
    return p.a + p.b * x;
  case Model::Quadratic:
    return p.a + p.c * x * x;
  case Model::Riccati:
    return p.a + p.b * x + p.c * x * x;
  default:
    return p.a + p.k * std::pow(x, p.n);
  }
}
inline auto historicalDrag(Model m, const Params &p) {
  return [m, &p](float x) { return deceleration(m, p, x); };
}
inline float advance(Model m, const Params &p, float x, float dt, const Config &cfg) {
  return advance(historicalDrag(m, p), x, dt, cfg);
}
inline float predictStopTime(const Result &r, float initialRpm, const Config &cfg = {}) {
  return predictStopTime(historicalDrag(r.model, r.params), r, initialRpm, cfg);
}
inline float predictRpm(Model m, const Params &p, float initialRpm, float elapsed, const Config &cfg = {}) {
  if (!validParams(m, p, cfg) || !std::isfinite(initialRpm) || initialRpm < 0)
    return NAN;
  return 1000 * advance(m, p, initialRpm / 1000, elapsed, cfg);
}
inline float predictRpm(const Result &r, float initialRpm, float elapsed, const Config &cfg = {}) {
  return r.valid ? predictRpm(r.model, r.params, initialRpm, elapsed, cfg) : NAN;
}
inline double objective(Model m, const Params &p, float x0, const Workspace &w, unsigned count,
                        const Config &cfg, Continue fn, void *ctx) {
  return objective(historicalDrag(m, p), m, p, x0, w, count, cfg, fn, ctx);
}
inline Params parameters(Model m, const float *q, float x0, float scale, const Config &cfg) {
  Params p;
  p.a = std::max(cfg.aFloor, q[0] * scale);
  if (m == Model::Linear)
    p.b = q[1] * scale / x0;
  else if (m == Model::Quadratic)
    p.c = q[1] * scale / (x0 * x0);
  else if (m == Model::Riccati) {
    p.b = q[1] * scale / x0;
    p.c = q[2] * scale / (x0 * x0);
  } else {
    p.n = q[2];
    p.k = q[1] * scale / std::pow(x0, p.n);
  }
  return p;
}
inline Result optimize(Model m, const Workspace &w, unsigned count, float x0, const Config &cfg, Continue fn,
                       void *ctx) {
  Result result;
  result.model = m;
  result.sampleCount = count;
  const bool anchored = w.stopDuration > 0;
  const bool power = m == Model::Power;
  const unsigned exponentIndex = anchored ? 1 : 2;
  const unsigned dimension = 2 + (power || m == Model::Riccati) - unsigned(anchored);
  if (count < 6 || w.points[count - 1].t + w.startOffset <= 0 || x0 <= 0 || !cfg.evaluations)
    return result;
  const float scale = std::max(.005f, (x0 - w.points[count - 1].x) / (w.points[count - 1].t + w.startOffset));
  // Scaling every drag coefficient by the same factor divides time-to-zero
  // by that factor. Search only the independent shape parameters when timed.
  auto coefficients = [&](const float *q) {
    if (!anchored)
      return parameters(m, q, x0, scale, cfg);
    Params p;
    p.a = 1;
    const float drag = std::expm1(q[0]);
    if (m == Model::Linear || m == Model::Riccati)
      p.b = drag / x0;
    if (m == Model::Quadratic)
      p.c = drag / (x0 * x0);
    if (m == Model::Riccati)
      p.c = std::expm1(q[1]) / (x0 * x0);
    if (power) {
      p.n = q[1];
      p.k = drag / std::pow(x0, p.n);
    }
    float integral = x0;
    if (m == Model::Linear && drag > 0)
      integral = x0 * std::log1p(drag) / drag;
    else if (m == Model::Quadratic && drag > 0)
      integral = x0 * std::atan(std::sqrt(drag)) / std::sqrt(drag);
    else if (m == Model::Riccati || power) {
      integral = anchoredIntegral(historicalDrag(m, p), x0, cfg, fn, ctx);
    }
    const float factor = integral / w.stopDuration;
    p.a *= factor;
    p.b *= factor;
    p.c *= factor;
    p.k *= factor;
    return p;
  };
  auto seed = [&](float *q, unsigned start) {
    q[0] = start == 0 ? .8f : start == 1 ? .2f : .45f;
    q[1] = 1 - q[0];
    q[2] = m == Model::Power ? 1.f + .5f * start : m == Model::Riccati ? .3f : 0;
    if (m == Model::Riccati)
      q[1] *= .5f;
    if (anchored) {
      q[0] = .2f + start;
      q[1] = power ? 1.f + .5f * start : .4f;
    }
  };
  return fitSimplex(m, count, dimension, power ? exponentIndex : 3, anchored, cfg, fn, ctx, coefficients,
                    [&](const Params &p) { return objective(m, p, x0, w, count, cfg, fn, ctx); },
                    seed, [&](const Result &r) { return predictStopTime(r, x0 * 1000, cfg); });
}
inline Results fitAllModels(const Session &session, Workspace &work, const Config &cfg = {},
                            Continue fn = nullptr, void *ctx = nullptr) {
  Results out;
  prepareFit(session, work, out, cfg);
  for (unsigned i = 0; i < MODEL_COUNT; ++i) {
    if (!proceed(fn, ctx)) { out.cancelled = true; return out; }
    Model m = Model(i);
    out.models[i] = fitOneModel(m, out, work, cfg, fn, ctx,
        [m](auto &&...args) { return optimize(m, args...); },
        [](Result &r, auto &&...args) { scoreFit(historicalDrag(r.model, r.params), r, args...); },
        [&](const Result &r) { return predictStopTime(r, startingRpm(out), cfg); });
  }
  if (!proceed(fn, ctx)) {
    out.cancelled = true;
    return out;
  }
  // Sort by held-out evidence. Complexity breaks a near tie, not training loss.
  auto eligible = [&](unsigned i) {
    const auto &r = out.models[i];
    return r.valid && r.converged && r.validationConverged && r.validation.valid;
  };
  std::sort(out.ranking, out.ranking + MODEL_COUNT, [&](unsigned a, unsigned b) {
    const auto &x = out.models[a], &y = out.models[b];
    if (eligible(a) != eligible(b))
      return eligible(a);
    if (!eligible(a))
      return a < b;
    if (x.validation.rmse != y.validation.rmse)
      return x.validation.rmse < y.validation.rmse;
    return x.validation.mae < y.validation.mae;
  });
  if (eligible(out.ranking[0])) {
    out.best = out.ranking[0];
    const auto bestError = out.models[out.best].validation.rmse;
    if (out.best >= 2) {
      int simpler = -1;
      for (unsigned i = 0; i < 2; ++i)
        if (eligible(i) && out.models[i].validation.rmse <= bestError + cfg.validationTieRpm &&
            (simpler < 0 || out.models[i].validation.rmse < out.models[simpler].validation.rmse))
          simpler = int(i);
      if (simpler >= 0)
        out.best = simpler;
    }
    if (out.best != out.ranking[0]) {
      auto at = std::find(out.ranking, out.ranking + MODEL_COUNT, uint8_t(out.best));
      std::rotate(out.ranking, at, at + 1);
    }
  }
  return out;
}
inline void makeCurve(const Results &fits, unsigned selected, Curve &curve, const Config &cfg = {}) {
  const auto &r = fits.models[selected];
  renderCurve(historicalDrag(r.model, r.params), fits, selected, curve, cfg, nullptr, nullptr);
}
} // namespace Decay
