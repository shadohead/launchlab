// Host-side adapter for replaying a live Analysis snapshot. Input contains only
// the launch RPM, observation times and RPMs, and the requested clock ticks.
#include "fixed_riccati.h"
#include "early_spin.h"
#include <iomanip>
#include <iostream>

int main() {
  static Decay::Session session;
  static Decay::Workspace work;
  unsigned ticks;
  if (!(std::cin >> session.launcherRpm >> ticks))
    return 1;
  std::cout << std::setprecision(10);
  // Each block adds the observations newly available at this tick.
  Decay::Results fits;
  Decay::Curve curve;
  unsigned fittedCount = 0, earlyReadings = 0, lastFitMs = 0;
  auto update = [&](unsigned ms) {
    if (!EarlySpin::refitDue(ms, session.count, lastFitMs, fittedCount)) return;
    session.stopMs = ms;
    fits = Decay::fitLiveModels(session, work, curve);
    fittedCount = session.count;
    lastFitMs = ms;
  };
  for (unsigned i = 0; i < ticks; ++i) {
    unsigned ms, count;
    if (!(std::cin >> ms >> count) ||
        count > Decay::MAX_SAMPLES - session.count)
      return 1;
    for (unsigned n = 0; n < count; ++n) {
      Decay::Sample s;
      unsigned recovered, confidence;
      if (!(std::cin >> s.ms >> s.rpm >> recovered >> confidence) || s.ms > ms ||
          recovered > 1 || confidence > 255 || (confidence && !recovered))
        return 1;
      s.recovered = recovered != 0;
      s.pitchConfidence = uint8_t(confidence);
      const unsigned due = std::max(EarlySpin::FIRST_MS, lastFitMs + EarlySpin::REFIT_MS);
      if (due < s.ms) update(due);
      session.samples[session.count++] = s;
      if (!recovered && s.ms <= EarlySpin::WINDOW_MS) ++earlyReadings;
      update(s.ms);
    }
    update(std::min(ms, std::max(EarlySpin::FIRST_MS,lastFitMs + EarlySpin::REFIT_MS)));
    // Recompute the feature prefix separately when new points are waiting for
    // the next fit. Preserve the published model used by the finish label.
    Decay::Results observed;
    if (fittedCount != session.count) Decay::prepare(session, work, observed, {});
    const float lastTime = fittedCount == session.count ? fits.lastTime : observed.lastTime;
    const float finish = fits.fixedRiccati ? curve.end : EarlySpin::finish(earlyReadings, ms);
    // Five-second secant on retained observations. A lone point has no rate.
    unsigned begin = work.count;
    if (begin) {
      --begin;
      while (begin &&
             work.points[work.count - 1].t - work.points[begin - 1].t <= 5.f)
        --begin;
    }
    const auto &end = work.points[work.count ? work.count - 1 : 0];
    float rate = work.count > begin + 1
                     ? 1000 * (end.x - work.points[begin].x) /
                           (end.t - work.points[begin].t)
                     : NAN;
    std::cout << ms << ' ' << work.count << ' ' << lastTime << ' '
              << (work.count ? 1000 * end.x : NAN) << ' ' << rate << ' '
              << (std::isfinite(finish) ? finish : -1) << ' '
              << (std::isfinite(finish) ? finish : -1) << '\n';
  }
}
