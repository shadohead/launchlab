// Desktop post-stop comparison. The complete trace and its manual endpoint
// are inputs, just as they were for the original on-device model selector.
#include "offline_decay_models.h"
#include <iomanip>
#include <iostream>

int main() {
  static Decay::Session spin;
  static Decay::Workspace work;
  spin.hasStop = true;
  if (!(std::cin >> spin.launcherRpm >> spin.stopMs >> spin.count) || spin.count > Decay::MAX_SAMPLES)
    return 1;
  for (unsigned i = 0; i < spin.count; ++i) {
    unsigned recovered, confidence;
    if (!(std::cin >> spin.samples[i].ms >> spin.samples[i].rpm >> recovered >> confidence) ||
        recovered > 1 || confidence > 255 || (confidence && !recovered))
      return 1;
    spin.samples[i].recovered = recovered != 0;
    spin.samples[i].pitchConfidence = uint8_t(confidence);
  }
  const auto fits = Decay::fitAllModels(spin, work);
  std::cout << std::setprecision(10);
  for (const auto &model : fits.models) {
    std::cout << unsigned(model.model) << ' ' << (fits.best == int(model.model)) << ' '
              << model.valid << ' ' << (model.validation.valid ? model.validation.rmse : -1) << ' '
              << (model.all.valid ? model.all.rmse : -1) << ' ' << fits.sampleCount << '\n';
  }
}
