// Host-side adapter for replaying a live Analysis snapshot. Input contains only
// the launch RPM, observation times and RPMs, and the requested clock ticks.
#include "fixed_riccati.h"
#include "early_spin.h"
#include "stamina_finish.h"
#include "audio_finish.h"
#include <vector>
#include <string>
#include <iomanip>
#include <iostream>

int main() {
  static Decay::Session session;
  static Decay::Workspace work;
  unsigned ticks;
  if (!(std::cin >> session.launcherRpm >> ticks))
    return 1;
  std::vector<std::array<uint8_t,AudioMetadata::RECORD_BYTES>> audio;
  if ((std::cin >> std::ws).peek() == 'A') {
    std::string tag, hex;
    unsigned rows;
    if (!(std::cin >> tag >> rows) || tag != "AUDIO" || rows > AudioMetadata::DENSE_RECORDS) return 1;
    audio.resize(rows);
    for (auto &row : audio) {
      if (!(std::cin >> hex) || hex.size() != 2 * row.size() || hex.find_first_not_of("0123456789abcdef") != std::string::npos)
        return 1;
      for (unsigned i = 0; i < row.size(); ++i)
        row[i] = uint8_t(std::stoul(hex.substr(i*2, 2), nullptr, 16));
    }
  }
  AudioFinish::Predictor audioPredictor;
  std::cout << std::setprecision(10);
  // Each block adds the observations newly available at this tick.
  Decay::Results fits;
  struct Arrival { unsigned at; Decay::Sample sample; };
  std::vector<Arrival> arrivals;
  StaminaFinish::Publications published;
  StaminaFinish::EarlyReference early;
  static StaminaFinish::Scratch scratch;
  const auto get = [&](unsigned j) {
    const auto &s = session.samples[j];
    return StaminaFinish::Observation{s.ms, s.rpm, s.recovered, s.rejected};
  };
  double finish = NAN;
  Decay::Results pending;
  Decay::Curve pendingCurve;
  unsigned fittedCount = 0, lastFitMs = 0, readyAt = 0, cursor = 0, nextMs = 100;
  for (unsigned i = 0; i < ticks; ++i) {
    unsigned ms, count;
    if (!(std::cin >> ms >> count) || count > Decay::MAX_SAMPLES - arrivals.size() || ms < nextMs)
      return 1;
    for (unsigned n = 0; n < count; ++n) {
      Arrival a{};
      unsigned recovered, confidence, rejected;
      auto &s = a.sample;
      if (!(std::cin >> a.at >> s.ms >> s.rpm >> recovered >> confidence >> rejected) ||
          a.at > ms || s.ms > a.at || recovered > 1 || confidence > 255 ||
          (confidence && !recovered) || rejected > 1 || (!arrivals.empty() && a.at < arrivals.back().at))
        return 1;
      s.recovered = recovered;
      s.pitchConfidence = uint8_t(confidence);
      s.rejected = rejected;
      arrivals.push_back(a);
    }
    // Model delivery in 100 ms loop steps. Publish completed jobs one step
    // later so a requested checkpoint cannot use a fit that just started.
    for (; nextMs <= ms; nextMs += 100) {
      const auto before = cursor;
      while (cursor < arrivals.size() && arrivals[cursor].at <= nextMs)
        session.samples[session.count++] = arrivals[cursor++].sample;
      if (cursor != before)
        std::sort(session.samples, session.samples + session.count,
                  [](const auto &a, const auto &b) { return a.ms < b.ms; });
      if (readyAt && readyAt <= nextMs) {
        fits = pending;
        published.publish(fits.fixedRiccati ? pendingCurve.end : NAN);
        readyAt = 0;
      } else if (!readyAt && EarlySpin::refitDue(nextMs, session.count, lastFitMs, fittedCount)) {
        session.stopMs = lastFitMs = nextMs;
        fittedCount = session.count;
        pending = Decay::fitLiveModels(session, work, pendingCurve);
        readyAt = nextMs + 100;
      }
      // The Stick updates its forecast each second, even between reported checkpoints.
      if (nextMs >= EarlySpin::FIRST_MS && nextMs % 1000 == 0) {
        const StaminaFinish::LiveFit input{nextMs, session.launcherRpm, fits.support.original, fits.support.accepted};
        finish = StaminaFinish::predict(input, published, session.count, get, scratch, early);
        if (!audio.empty())
          finish = audioPredictor.predict(nextMs, session.launcherRpm, finish, session.count, get,
              [&](unsigned i) { return i < audio.size() ? audio[i].data() : nullptr; }, scratch.clock);
      }
    }
    session.stopMs = ms;
    Decay::Results observed;
    Decay::prepare(session, work, observed, {});
    const float lastTime = observed.lastTime;
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
