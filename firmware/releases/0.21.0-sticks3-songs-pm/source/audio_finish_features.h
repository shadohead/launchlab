#pragma once
#include "audio_metadata.h"
#include "stamina_pace.h"

namespace AudioFinish {
// Model features are measured on completed 200 ms metadata intervals in the first 30 s.
// Use the same conservative delivery bound as calibration exports, including lag overflow.
struct Sound {
    double broadband = NAN, flatness = NAN, openingBalance = NAN, coverage = NAN;
    double margin = NAN, decay = NAN, harmonics = NAN, recentBroadband = NAN, balance = NAN, level5 = NAN;
    bool available = false;
};

inline double relativeBandLevel(const uint8_t *row) {
    static const auto power = [] {
        std::array<double, 129> table{};
        for (unsigned i = 1; i < table.size(); ++i)
            table[i] = std::pow(10., (int(i) - 128) / 10.);
        return table;
    }();
    for (unsigned i = 8; i < 16; ++i)
        if (!row[i] || row[i] > 128) return NAN;
    const double low = power[row[8]] + power[row[9]] + power[row[10]];
    const double high = power[row[12]] + power[row[13]] + power[row[14]] + power[row[15]];
    return 10. * std::log10(low / high);
}

template<class Row>
Sound soundFeatures(unsigned now, Row rowAt) {
    const unsigned until = std::min(now, 30000u);
    struct Counts {
        unsigned rows = 0, frames = 0, broad = 0, bands = 0;
        double balance = 0;
        double meanBand() const { return std::clamp(balance / std::max(1u, bands), -60., 60.); }
    } full, opening, recent;
    unsigned flatCount = 0, pitches = 0, harmonics = 0;
    double flat = 0;
    // Dense metadata supplies at most 25 levels in 5 s, and 150 margins in 30 s.
    std::array<int, 25> levels3{}, levels5{};
    std::array<int, 26> latest{};
    std::array<int, AudioMetadata::DENSE_RECORDS> margins{};
    unsigned n3 = 0, n5 = 0, nLatest = 0, nMargins = 0;
    for (unsigned i = 0; AudioMetadata::end(i) <= until; ++i) {
        const unsigned end = AudioMetadata::end(i);
        const auto *r = rowAt(i);
        if (!r || !r[16] || r[18] == 255) continue;
        const unsigned available = end + 20u * r[18];
        if (available > until) continue;
        const double band = relativeBandLevel(r);
        const unsigned broad = r[17] & 15;
        auto add = [&](Counts &c) {
            ++c.rows;
            c.frames += r[16];
            c.broad += broad;
            if (std::isfinite(band)) { ++c.bands; c.balance += band; }
        };
        add(full);
        if (available <= 10000) {
            add(opening);
            if (r[3] != 255) { flatCount += r[16]; flat += r[16] * r[3] / 254.; }
        }
        if (end > until - 5000) add(recent);
        if (r[0] || r[1]) { ++pitches; harmonics += r[7]; }
        if (r[5] != 255) margins[nMargins++] = int(r[5]) - 128;
        if (r[2]) {
            const int level = int(r[2]) - 128;
            if (end <= 3000) levels3[n3++] = level;
            if (available <= 5000) levels5[n5++] = level;
            if (end >= until - 5000) {
                latest[nLatest++] = level;
            }
        }
    }
    Sound s;
    // A nearly empty or late-starting capture has no representative opening.
    // Stay on the RPM baseline until five populated bins and some opening audio are available.
    if (full.rows < 5 || !opening.rows) return s;
    s.available = true;
    if (opening.frames) s.broadband = double(opening.broad) / opening.frames;
    if (flatCount) s.flatness = flat / flatCount;
    if (opening.bands) s.openingBalance = opening.meanBand();
    s.coverage = double(pitches) / full.rows;
    s.harmonics = pitches ? double(harmonics) / pitches : 0.;
    s.balance = full.meanBand();
    if (recent.frames) s.recentBroadband = double(recent.broad) / recent.frames;
    auto median = [](auto &v, unsigned n) -> double {
        if (!n) return NAN;
        std::sort(v.begin(), v.begin() + n);
        return .5 * (v[(n-1)/2] + v[n/2]);
    };
    s.margin = median(margins, nMargins);
    s.level5 = median(levels5, n5);
    s.decay = std::clamp((n3 ? median(levels3, n3) : 0.) - (nLatest ? median(latest, nLatest) : 0.), -60., 60.);
    return s;
}

struct Rpm {
    double launch, logCount, ratio, log10, openingRatio;
    StaminaFinish::Pace live, opening;
    std::array<double, 10> neighbor;
};

// All readings are positive integer RPMs in the 100 ms trace slots. Only original locks
// enter this model. A window is available 200 ms after its center.
template<class Get>
Rpm rpmFeatures(unsigned now, float ir, unsigned count, Get get, AcousticClock::Scratch &scratch) {
    unsigned total = 0, to15 = 0, first10 = 0, first5 = 0, raw10 = 0, count30 = 0;
    unsigned first = 0, last = 0;
    double last30 = 0;
    std::array<unsigned, 5> tail{};
    std::array<float, 100> openingRpm{};
    for (unsigned i = 0; i < count; ++i) {
        auto p = get(i);
        if (p.recovered || !(p.rpm > 0) || p.ms + 200 > now) continue;
        const unsigned at = p.ms + 200;
        if (at <= 10000) ++raw10;
        if (p.rejected) continue;
        if (at <= 10000 && first10 < openingRpm.size()) {
            if (!first10) first = at;
            openingRpm[first10++] = p.rpm;
            if (at <= 5000) ++first5;
        }
        if (at <= 30000) { ++count30; last30 = p.rpm; }
        if (p.ms <= 15000) ++to15;
        tail[total++ % tail.size()] = unsigned(p.rpm);
        last = at;
    }
    StaminaFinish::PaceWindow live, opening;
    StaminaFinish::PaceSampler liveSample(total), openingSample(to15);
    unsigned rank = 0;
    for (unsigned i = 0; i < count; ++i) {
        auto p = get(i);
        if (p.recovered || p.rejected || !(p.rpm > 0) || p.ms + 200 > now) continue;
        liveSample.add(live, rank, p.ms, p.rpm);
        if (p.ms <= 15000) openingSample.add(opening, rank, p.ms, p.rpm);
        ++rank;
    }
    const auto all = live.offsetOne(liveSample.size(), scratch);
    const auto start = to15 == total ? all : opening.offsetOne(openingSample.size(), scratch);
    std::sort(openingRpm.begin(), openingRpm.begin()+first10);
    const double median10 = first10 ? .5 * (openingRpm[(first10-1)/2] + openingRpm[first10/2]) : NAN;
    const unsigned nTail = std::min(total, unsigned(tail.size()));
    std::sort(tail.begin(), tail.begin() + nTail);
    const double lastRatio = nTail ? .5 * (tail[(nTail-1)/2] + tail[nTail/2]) / ir : NAN;
    const double log10 = std::log1p(double(first10));
    return {ir * .001, std::log1p(double(count30)), count30 ? last30 / ir : NAN, log10, median10 / ir,
            all, start, {log10, std::log1p(double(first5)), first10 ? first * .001 : 10.,
            ir * .001, std::log1p(double(total)), lastRatio, std::min(30., (now-last)*.001),
            all.secondsPerUnit, all.timeAt3k, std::log1p(double(raw10))}};
}
} // namespace AudioFinish
