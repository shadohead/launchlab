#pragma once
#include "stamina_pace.h"
#include "stamina_weights.h"

namespace StaminaFinish {

// Reset for each launch. Call once per completed live fit, including failed fits.
struct Publications {
    double previous = NAN;
    unsigned consistent = 0;
    void publish(double finish) {
        if (!std::isfinite(finish)) consistent = 0;
        else if (std::abs(finish - previous) <= 5.) consistent = std::min(consistent + 1, 3u);
        else consistent = 1;
        previous = finish;
    }
};

struct Observation {
    unsigned ms;                 // Center of the mic measurement window.
    float rpm;
    bool recovered, rejected;
};

struct LiveFit {
    unsigned elapsedMs;
    float launchRpm;
    unsigned originalInliers, acceptedInliers;
};

// Keep one acoustically supported estimate per second from the opening window.
// If those estimates agree, use their median to temper later downward revisions.
class EarlyReference {
    std::array<double, 6> samples_{};
    unsigned count_ = 0;
    unsigned lastSecond_ = 0;

public:
    double adjust(unsigned elapsedMs, double estimate, bool supported) {
        const unsigned second = elapsedMs / 1000;
        if (second <= 15) {
            if (second >= 10 && second > lastSecond_ && supported && std::isfinite(estimate)) {
                lastSecond_ = second;
                auto end = samples_.begin() + count_;
                auto pos = std::lower_bound(samples_.begin(), end, estimate);
                std::move_backward(pos, end, end + 1);
                *pos = estimate;
                ++count_;
            }
            return estimate;
        }
        if (count_ < 3 || samples_[count_ - 1] - samples_[0] > 10.) return estimate;
        const double median = (samples_[(count_ - 1) / 2] + samples_[count_ / 2]) / 2.;
        return estimate + std::clamp((median - estimate) * .5, 0., 12.);
    }
};

inline double regression(const Features &values, const Regression &model) {
    double output = model.beta[0];
    for (unsigned i = 0; i < values.size(); ++i) {
        // A missing pace feature takes its training mean (standardized value zero).
        if (!std::isfinite(values[i])) continue;
        const double z = (values[i] - model.mean[i]) / model.sd[i];
        output += model.beta[i + 1] * std::clamp(z, -4., 4.);
    }
    return output;
}

inline double displayFinish(double elapsed, double estimate) {
    const double z = (estimate - elapsed) / 6.;
    return elapsed + 6. * (std::max(z, 0.) + std::log1p(std::exp(-std::abs(z))));
}

inline double blendFinish(const LiveFit &fit, double baseline, const Paces &live, const Paces &opening,
                          const Model &model) {
    const double launch = fit.launchRpm * .001;
    double sum = 0.;
    bool observed = false;
    for (unsigned i = 0; i < live.size(); ++i) {
        const double pace = live[i].secondsPerUnit;
        observed = observed || std::isfinite(pace) || std::isfinite(opening[i].secondsPerUnit);
        const Features inputs{pace, live[i].timeAt3k, opening[i].secondsPerUnit, opening[i].timeAt3k,
                              launch, pace * pace, pace * launch};
        const double end = std::clamp(regression(inputs, model.pace[i]), 15., 280.);
        sum += displayFinish(fit.elapsedMs / 1000., end);
    }
    if (!observed) return baseline;
    const double weight = std::clamp(.1 * (int(fit.elapsedMs / 1000) - 10), 0., .5);
    return baseline + weight * (sum / live.size() - baseline);
}

// Input contains only delivered observations in measurement order. Reuse the
// scratch space per launch. No allocation or training takes place on the device.
template<class Get>
double predict(const LiveFit &fit, const Publications &published, unsigned size, Get get,
               Scratch &scratch, EarlyReference &early, const Model &model = WEIGHTS) {
    if (fit.elapsedMs < 10000 || !(fit.launchRpm > 0)) return NAN;
    const double elapsed = fit.elapsedMs / 1000.;
    unsigned trusted = 0, opening = 0, firstThirty = 0, first = 0, last = 0;
    unsigned liveCount = 0, openingCount = 0;
    double latestRpm = 0;
    for (unsigned i = 0; i < size; ++i) {
        const auto p = get(i);
        if (!(p.rpm > 0) || p.recovered) continue;
        // Replay treats an original lock as available 200 ms after its center.
        if (p.ms <= 9800) ++opening;
        if (p.ms <= 30000) ++firstThirty;
        if (p.rejected) continue;
        if (p.ms <= fit.elapsedMs) {
            ++liveCount;
            if (p.ms <= 15000) ++openingCount;
        }
        if (!trusted) first = p.ms;
        last = p.ms;
        latestRpm = p.rpm;
        ++trusted;
    }

    const double launchAngle = AcousticClock::angle(fit.launchRpm);
    const double toppleAngle = AcousticClock::angle(578);
    const unsigned stride = std::max(1u, (trusted + 15) / 16);
    const unsigned start = trusted ? (trusted - 1) % stride : 0;
    std::array<double, 16> durations{};
    unsigned seen = 0, used = 0;
    const bool usePace = fit.elapsedMs >= 11000 && liveCount >= 3;
    const bool separateOpening = openingCount != liveCount;
    PaceSampler liveSamples(liveCount), openingSamples(openingCount);
    for (unsigned i = 0; i < size; ++i) {
        const auto p = get(i);
        if (!(p.rpm > 0) || p.recovered || p.rejected) continue;
        const unsigned k = seen++;
        if (usePace) {
            liveSamples.add(scratch.live, k, p.ms, p.rpm);
            if (separateOpening) openingSamples.add(scratch.opening, k, p.ms, p.rpm);
        }
        if (k < start || (k - start) % stride || p.ms < 2000) continue;
        const double drop = launchAngle - AcousticClock::angle(p.rpm);
        if (drop > .015)
            durations[used++] = std::clamp(p.ms / 1000. * (launchAngle - toppleAngle) / drop, 15., 260.);
    }
    const double optical = used ? AcousticClock::middle(durations.data(), used) : 125.;
    const auto mic = last - first >= 2000 ? AcousticClock::fitted(size, get, trusted, scratch.clock)
                                          : AcousticClock::Clock{};
    const bool hasMic = mic.end > 0 && mic.count >= 6;
    const bool hasFixed = std::isfinite(published.previous);
    const double ageFeature = std::min(elapsed, 30.) / 30.;
    double raw;
    if (hasMic && hasFixed) {
        const double fraction = fit.acceptedInliers / double(std::max(1u, fit.originalInliers));
        const double weight = std::min(1., fit.acceptedInliers / 30.) * (published.consistent / 3.) * fraction;
        raw = mic.end + weight * (published.previous - mic.end);
    } else if (hasMic) raw = mic.end;
    else if (hasFixed) raw = published.previous;
    else {
        const Features inputs{fit.launchRpm / 7000., std::log1p(double(opening)), std::log1p(double(firstThirty)),
            std::log1p(double(trusted)), std::clamp(latestRpm / fit.launchRpm, 0., 1.5),
            std::clamp(optical, 15., 240.) / 100., ageFeature};
        raw = std::clamp(regression(inputs, model.fallback), 15., 280.);
    }
    const double anchor = hasMic ? mic.end : hasFixed ? published.previous : trusted >= 3 ? optical : 125.;
    const Features inputs{raw / 100., std::clamp(anchor - raw, -40., 40.) / 25.,
        std::clamp(optical - raw, -50., 50.) / 25., fit.launchRpm / 7000., std::min(1., mic.count / 30.),
        std::min(1., mic.span / 20.), ageFeature};
    const double adjusted = raw + std::clamp(regression(inputs, model.residual), -30., 30.);
    const double baseline = displayFinish(elapsed, early.adjust(fit.elapsedMs, adjusted, hasMic || hasFixed));
    if (!usePace) return baseline;
    const auto &live = scratch.live.measurements(liveSamples.size(), scratch.clock);
    const auto &first15 = separateOpening ? scratch.opening.measurements(openingSamples.size(), scratch.clock) : live;
    return blendFinish(fit, baseline, live, first15, model);
}
}
