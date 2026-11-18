#pragma once
#include "audio_finish_features.h"
#include "audio_finish_weights.h"

namespace AudioFinish {
template<size_t N>
double regression(const std::array<double,N> &x, const double *mean, const double *sd,
                  const double *beta, double limit) {
    double value = beta[0];
    for (size_t i = 0; i < N; ++i)
        if (std::isfinite(x[i])) value += beta[i+1] * std::clamp((x[i]-mean[i])/sd[i], -limit, limit);
    return value;
}

template<class Phases>
const auto &phaseAt(const Phases &phases, unsigned second) {
    const auto *phase = &phases[0];
    for (const auto &p : phases) {
        if (p.start > second) break;
        phase = &p;
    }
    return *phase;
}

inline double correctedBaseline(unsigned sec, double base, const Sound &s, const Rpm &r) {
    const std::array<double,14> x{base, double(sec), r.launch, r.logCount, r.ratio, s.broadband,
        s.flatness, s.openingBalance, s.coverage, s.margin, s.decay, s.harmonics, s.recentBroadband, s.balance};
    const double delta = regression(x, Weights::anchorMean, Weights::anchorSd, Weights::anchorBeta, 3.);
    const double first = base + .75 * (std::max(double(sec), base + std::clamp(delta, -12., 12.)) - base);
    const std::array<double,9> match{first, r.launch, r.logCount, r.ratio, s.broadband, s.flatness, s.coverage, s.decay, s.balance};
    const auto &phase = phaseAt(Weights::templatePhases, sec);
    double total = 0., weight = 2.;
    for (unsigned i = 0; i < phase.count; ++i) {
        const auto &example = Weights::templates[phase.first+i];
        double distance = 0.;
        for (unsigned j = 0; j < match.size(); ++j) {
            const double z = (match[j]-example.values[j])/phase.scales[j];
            distance += std::isfinite(z) ? std::min(z*z,16.) : 1.;
        }
        const double w = std::exp(-.5 * distance/match.size());
        weight += w;
        total += w * example.correction;
    }
    return std::max(double(sec), first + total/weight);
}

inline double neighborFinish(const Weights::NeighborPhase &phase, const std::array<double,10> &x, unsigned sec) {
    struct Match { double distance, stop; };
    std::array<Match,7> nearest{};
    unsigned n = 0;
    for (unsigned i = 0; i < phase.count; ++i) {
        const auto &example = Weights::neighbors[phase.first+i];
        if (example.stop <= sec) continue;
        double distance = 0.;
        for (unsigned j = 0; j < x.size(); ++j) {
            const double z = (x[j]-example.values[j])/phase.scales[j];
            if (std::isfinite(z)) distance += std::min(z*z,25.);
            else distance += !std::isfinite(x[j]) && !std::isfinite(example.values[j]) ? .25 : 2.;
        }
        distance /= x.size();
        // Equal distances keep the training-table order, including at the seventh position.
        unsigned pos = 0;
        while (pos < n && nearest[pos].distance <= distance) ++pos;
        if (pos == nearest.size()) continue;
        if (n < nearest.size()) ++n;
        std::move_backward(nearest.begin()+pos, nearest.begin()+n-1, nearest.begin()+n);
        nearest[pos] = {distance, example.stop};
    }
    if (n < 2) return NAN;
    double sum = 0., weight = 0.;
    for (unsigned i = 0; i < n; ++i) {
        const double w = std::exp((nearest[0].distance-nearest[i].distance)/.5);
        sum += w*nearest[i].stop;
        weight += w;
    }
    return sum/weight;
}

inline double infer(unsigned sec, double base, const Sound &s, const Rpm &r,
                    const Weights::NeighborPhase &phase, const std::array<double,10> &atPhase) {
    const double prior = correctedBaseline(sec,base,s,r);
    const auto &a = r.opening, &b = r.live;
    const std::array<double,8> pace{r.launch, a.secondsPerUnit, a.timeAt3k, b.secondsPerUnit, b.timeAt3k,
        r.log10, r.openingRatio, double(std::isfinite(b.secondsPerUnit))};
    const std::array<double,10> joint{r.launch, r.log10, r.openingRatio, a.secondsPerUnit, a.timeAt3k,
        b.secondsPerUnit, b.timeAt3k, s.broadband, s.openingBalance, s.level5};
    double sum = 0.;
    for (unsigned i = 0; i < 3; ++i) {
        const auto &w = Weights::branches[i];
        double end = i == 2 ? regression(joint,w.mean,w.sd,w.beta,4.) : regression(pace,w.mean,w.sd,w.beta,4.);
        if (i != 0) end = std::exp(std::clamp(end,2.,6.));
        end = std::clamp(end,12.,290.);
        const double z = (end-sec)/4.;
        const double projected = sec + 4. * (std::max(z,0.) + std::log1p(std::exp(-std::abs(z))));
        sum += prior + (i == 2 ? .25 : .5) * (projected-prior);
    }
    double estimate = sum/3.;
    const double sparse = std::clamp((21.-std::expm1(atPhase[0]))/13.,0.,1.);
    double overrideWeight = 0.;
    if (sparse > 0.) {
        const double other = neighborFinish(phase,atPhase,sec);
        if (std::isfinite(other)) {
            estimate += sparse * (std::isfinite(atPhase[7]) ? .3 : 1.) * (other-estimate);
            overrideWeight = sparse * std::clamp((prior-other-25.)/25.,0.,1.);
        }
    }
    // Taper the adjustment outside the range covered by at least two training runs.
    double nearest = INFINITY, second = INFINITY;
    for (double stop : Weights::supportedDurations) {
        const double d = std::abs(stop-prior);
        if (d < nearest) { second = nearest; nearest = d; }
        else if (d < second) second = d;
    }
    const double gate = std::max(overrideWeight,std::clamp((32.-second)/20.,0.,1.));
    return prior + gate * (estimate-prior);
}

class Predictor {
    Sound sound_;
    std::array<double,10> phaseFeatures_{};
    unsigned audioThrough_ = 0, phaseStart_ = 0;
public:
    template<class Get, class Row>
    double predict(unsigned ms, float ir, double baseline, unsigned size, Get get, Row row,
                   AcousticClock::Scratch &scratch) {
        if (ms < 10000 || !std::isfinite(baseline)) return baseline;
        const unsigned sec = ms/1000, tick = sec*1000, through = std::min(tick,30000u);
        if (audioThrough_ != through) {
            sound_ = soundFeatures(through,row);
            audioThrough_ = through;
        }
        if (!sound_.available) return baseline;
        const Rpm rpm = rpmFeatures(tick,ir,size,get,scratch);
        const auto &phase = phaseAt(Weights::neighborPhases, sec);
        if (phaseStart_ != phase.start) {
            phaseFeatures_ = sec == phase.start ? rpm.neighbor :
                rpmFeatures(phase.start*1000,ir,size,get,scratch).neighbor;
            phaseStart_ = phase.start;
        }
        return infer(sec,baseline,sound_,rpm,phase,phaseFeatures_);
    }
};
} // namespace AudioFinish
