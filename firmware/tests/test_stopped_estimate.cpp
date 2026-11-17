#include "fixed_riccati.h"
#include "decay_record.h"
#include "analysis_details.h"
#include <cassert>
#include <iostream>

int main() {
  static Decay::Session input;
  static Decay::Workspace work;
  input.hasStop = true;
  input.launcherRpm = 7300;
  input.stopMs = 82000;
  Decay::Curve curve;
  for (unsigned count : {0u, 1u, 2u, 9u, 30u}) {
    input.count = count;
    for (unsigned i = 0; i < count; ++i) input.samples[i] = {1000 + 100 * i, 4000};
    const auto fit = Decay::fitStoppedModels(input, work, curve);
    assert(fit.best == int(Decay::Model::Riccati) && fit.endpointEstimate && fit.fixedRiccati);
    assert(fit.sourceCount == count && fit.sampleCount == 0 && work.count == 0);
    assert(!fit.validationPossible && !fit.models[fit.best].all.valid);
    assert(curve.rpm[0] == 7300 && curve.end == 82 && curve.rpm[Decay::CURVE_POINTS - 1] == 0);
    // This is the nonlinear shared drag shape, not the sparse fitter's line.
    assert(curve.rpm[Decay::CURVE_POINTS / 2] < 3650);
    const auto details = AnalysisDetails::summarize(curve, input.launcherRpm, 82);
    assert(details.complete && details.revolutions > 0 && details.count == 7);
    assert(details.battleStaminaSeconds > 0 && details.battleStaminaSeconds < 82);
  }
  for (float rpm : {1.f, 500.f, 7300.f, 65535.f}) for (uint32_t ms : {1u, 100u, 10000u, 300000u}) {
    Decay::Results fit, restored;
    assert(Decay::FixedRiccati::estimate(rpm, 0, ms, 2, fit, curve));
    for (unsigned i = 0; i < Decay::CURVE_POINTS; ++i) {
      assert(std::isfinite(curve.rpm[i]) && curve.rpm[i] >= 0 && curve.rpm[i] <= rpm);
      if (i) assert(curve.rpm[i] <= curve.rpm[i - 1]);
    }
    Decay::Record::Image record;
    Decay::Record::encode(fit, fit.best, record);
    unsigned slot;
    assert(Decay::Record::decode(record, restored, slot) && restored.endpointEstimate);
    Decay::Curve rebuilt;
    Decay::makeRiccatiCurve(restored, rebuilt);
    for (unsigned i = 0; i < Decay::CURVE_POINTS; ++i) assert(rebuilt.rpm[i] == curve.rpm[i]);
    fit.models[fit.best].params.a *= 2;
    Decay::Record::encode(fit, fit.best, record);
    assert(!Decay::Record::decode(record, restored, slot));
  }
  input.count = 0;
  input.hasStop = false;
  assert(!Decay::fitStoppedModels(input, work, curve).endpointEstimate);
  input.hasStop = true;
  const auto cancelled = Decay::fitStoppedModels(input, work, curve, [](void *) { return false; });
  assert(cancelled.cancelled && !cancelled.endpointEstimate);
  input.stopMs = 0;
  assert(!Decay::fitStoppedModels(input, work, curve).endpointEstimate);
  Decay::Results fit;
  for (float rpm : {0.f, -1.f, NAN, INFINITY})
    assert(!Decay::FixedRiccati::estimate(rpm, 0, 82000, 0, fit, curve));
  std::cout << "PASS: stopped endpoint estimates, monotonic curves, details and export round-trips\n";
}
