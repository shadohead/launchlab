#pragma once
#include "acoustic_clock.h"

namespace StaminaFinish {
constexpr unsigned PACE_SAMPLES = 16;
struct Pace {
    float secondsPerUnit = NAN;
    float timeAt3k = NAN;
};
using Paces = std::array<Pace, 3>;

// A window owns its selected readings. A backfill or filter revision can change
// their contents even when the count stays the same.
class PaceWindow {
    struct Sample { unsigned ms = 0; float rpm = 0; };
    std::array<Sample, PACE_SAMPLES> samples_{};
    Paces paces_{};
    unsigned size_ = 0;
    bool changed_ = false;

    Pace measure(float offset, unsigned size, AcousticClock::Scratch &scratch) const {
        std::array<float, PACE_SAMPLES> times, values;
        for (unsigned i = 0; i < size; ++i) {
            times[i] = samples_[i].ms * .001f;
            const float rpm = samples_[i].rpm * .001f;
            values[i] = (3.f + offset) * (rpm - 3.f) / (rpm + offset);
        }
        unsigned pairs = 0;
        for (unsigned i = 1; i < size; ++i) {
            for (unsigned j = 0; j < i; ++j) {
                const float dt = times[i] - times[j];
                if (dt >= 1.5f)
                    scratch.slopes[pairs++] = (values[j] - values[i]) / dt;
            }
        }
        if (!pairs) return {};
        const float rate = float(AcousticClock::middle(scratch.slopes.data(), pairs));
        if (!(rate > 0)) return {};
        const float pace = std::clamp(1.f / rate, 2.f, 120.f);
        // The coordinate is zero at 3,000 RPM. Each reading supplies a crossing time.
        for (unsigned i = 0; i < size; ++i) times[i] += values[i] * pace;
        return {pace, std::clamp(AcousticClock::middle(times.data(), size), -90.f, 180.f)};
    }

public:
    Pace offsetOne(unsigned size, AcousticClock::Scratch &scratch) const {
        return size >= 3 ? measure(1.f, size, scratch) : Pace{};
    }
    void set(unsigned i, unsigned ms, float rpm) {
        auto &sample = samples_[i];
        if (sample.ms != ms || sample.rpm != rpm) {
            sample = {ms, rpm};
            changed_ = true;
        }
    }

    const Paces &measurements(unsigned size, AcousticClock::Scratch &scratch) {
        if (changed_ || size != size_) {
            constexpr float offsets[] = {0, 1, 3}; // Thousands of RPM, in model order.
            for (unsigned i = 0; i < paces_.size(); ++i)
                paces_[i] = size >= 3 ? measure(offsets[i], size, scratch) : Pace{};
            size_ = size;
            changed_ = false;
        }
        return paces_;
    }
};

// Select evenly spaced ranks from the accepted readings, including both ends.
class PaceSampler {
    unsigned total_, target_, next_ = 0, used_ = 0;
public:
    explicit PaceSampler(unsigned total) : total_(total), target_(std::min(total, PACE_SAMPLES)) {}
    void add(PaceWindow &window, unsigned rank, unsigned ms, float rpm) {
        if (target_ < 3 || used_ == target_ || rank != next_) return;
        window.set(used_++, ms, rpm);
        if (used_ < target_) next_ = used_ * (total_ - 1) / (target_ - 1);
    }
    unsigned size() const { return used_; }
};

struct Scratch {
    AcousticClock::Scratch clock;
    PaceWindow live, opening;
};
}
