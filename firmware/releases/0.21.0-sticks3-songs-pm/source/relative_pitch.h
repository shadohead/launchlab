#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>

// Analysis-only measurements between normal microphone locks. A recovery is
// anchored to measured spectral peaks, never to the fitted RPM curve.
namespace RelativePitch {
struct Estimate {
  float rpm = 0;
  uint8_t confidence = 0;
};

class Tracker {
  struct Peak { float hz = 0, contrast = 0; unsigned harmonic = 0; };
  static constexpr unsigned HARMONICS = 6, MAX_PEAKS = 24;
  static constexpr float MAX_AGE = 2.f, MIN_CONTRAST = 8.f;
  Peak anchor_[HARMONICS];
  unsigned anchors_ = 0, streak_ = 0;
  float anchorRpm_ = 0, anchorTime_ = 0, previousRatio_ = 0, previousTime_ = 0;

  template<class Db>
  static float peakHz(unsigned bin, float spacing, Db db) {
    const float hz = bin * spacing;
    const float left = db(hz - spacing), center = db(hz), right = db(hz + spacing);
    const float curvature = left - 2 * center + right;
    const float offset = curvature < -.001f ? std::clamp(.5f * (left - right) / curvature, -.5f, .5f) : 0.f;
    return (bin + offset) * spacing;
  }
  static float tolerance(float hz) { return std::max(1.f, .015f * hz); }

public:
  void reset() { anchors_ = streak_ = 0; anchorRpm_ = previousRatio_ = 0; }
  void pause() { streak_ = 0; previousRatio_ = 0; }

  template<class Db, class Contrast>
  void anchor(float hz, float rpm, float seconds, float spacing, Db db, Contrast contrast) {
    reset();
    if (!(hz > 0 && rpm > 0) || !std::isfinite(seconds)) return;
    for (unsigned harmonic = 1; harmonic <= HARMONICS; ++harmonic) {
      const float center = hz * harmonic;
      if (center >= 1800.f) break;
      const float radius = std::max(2 * spacing, .02f * center);
      const unsigned first = unsigned(std::max(1.f, std::ceil((center - radius) / spacing)));
      const unsigned last = unsigned(std::floor((center + radius) / spacing));
      unsigned best = first;
      for (unsigned bin = first + 1; bin <= last; ++bin)
        if (db(bin * spacing) > db(best * spacing)) best = bin;
      const float at = best * spacing;
      if (db(at) < db(at - spacing) || db(at) < db(at + spacing)) continue;
      const float measured = peakHz(best, spacing, db), strength = contrast(measured);
      if (strength >= MIN_CONTRAST && std::fabs(measured - center) <= radius)
        anchor_[anchors_++] = {measured, strength, harmonic};
    }
    if (anchors_ < 3) { reset(); return; }
    anchorRpm_ = rpm;
    anchorTime_ = seconds;
  }

  template<class Db, class Contrast>
  Estimate recover(float seconds, float spacing, Db db, Contrast contrast) {
    const float age = seconds - anchorTime_;
    if (anchors_ < 3 || !std::isfinite(age) || age <= 0 || age > MAX_AGE) {
      pause(); return {};
    }
    // A short interruption can include real deceleration. The lower bound
    // also keeps an octave change from masquerading as the same harmonic.
    const float lowRatio = std::max(.70f, std::exp(-.6f * age)), highRatio = 1.02f;
    Peak peaks[MAX_PEAKS];
    unsigned count = 0;
    const unsigned first = unsigned(std::max(1.f, std::floor(anchor_[0].hz * lowRatio / spacing)));
    const unsigned last = unsigned(std::min(1800.f, anchor_[anchors_ - 1].hz * highRatio) / spacing);
    for (unsigned bin = first; bin <= last; ++bin) {
      const float at = bin * spacing, level = db(at);
      if (level <= db(at - spacing) || level < db(at + spacing)) continue;
      const float hz = peakHz(bin, spacing, db), strength = contrast(hz);
      if (strength < MIN_CONTRAST) continue;
      unsigned slot = count;
      if (count == MAX_PEAKS) {
        slot = 0;
        for (unsigned i = 1; i < count; ++i)
          if (peaks[i].contrast < peaks[slot].contrast) slot = i;
        if (strength <= peaks[slot].contrast) continue;
      } else ++count;
      peaks[slot] = {hz, strength};
    }

    struct Match { float ratio = 0, score = 0, quality = 0; unsigned hits = 0; };
    auto match = [&](float ratio) {
      Match out; float ratioSum = 0, weightSum = 0;
      uint32_t used = 0; unsigned harmonicDivisor = 0;
      for (unsigned a = 0; a < anchors_; ++a) {
        const float target = anchor_[a].hz * ratio, limit = tolerance(target);
        unsigned best = count; float error = limit;
        for (unsigned p = 0; p < count; ++p) {
          const float distance = std::fabs(peaks[p].hz - target);
          if (!(used & (1u << p)) && distance < error) { best = p; error = distance; }
        }
        if (best == count) continue;
        used |= 1u << best;
        const float strength = std::min(24.f, std::min(anchor_[a].contrast, peaks[best].contrast));
        out.score += strength * (1.f - .25f * error / limit);
        ratioSum += strength * peaks[best].hz / anchor_[a].hz;
        weightSum += strength;
        harmonicDivisor = std::gcd(harmonicDivisor, anchor_[a].harmonic);
        ++out.hits;
      }
      // Even-only support also describes a tone one octave higher. More
      // generally, a common harmonic divisor leaves the pitch unresolved.
      if (out.hits >= 2 && harmonicDivisor == 1) {
        out.ratio = ratioSum / weightSum;
        out.quality = std::min(1.f, weightSum / (12.f * out.hits));
      } else out.score = 0;
      return out;
    };
    // Retain the best competing scale as well. A harmonic swap often gives
    // two plausible paths; continuity alone is not enough to choose one.
    Match best;
    float scores[HARMONICS][MAX_PEAKS];
    for (unsigned a = 0; a < anchors_; ++a)
      for (unsigned p = 0; p < count; ++p) {
        const float ratio = peaks[p].hz / anchor_[a].hz;
        if (ratio < lowRatio || ratio > highRatio) continue;
        const auto candidate = match(ratio);
        scores[a][p] = candidate.score;
        if (candidate.score > best.score) best = candidate;
      }
    if (!best.score) { pause(); return {}; }
    float alternative = 0;
    for (unsigned a = 0; a < anchors_; ++a)
      for (unsigned p = 0; p < count; ++p) {
        const float ratio = peaks[p].hz / anchor_[a].hz;
        if (ratio < lowRatio || ratio > highRatio || std::fabs(ratio - best.ratio) < .04f) continue;
        // The same candidate was matched above. Reuse its score instead of
        // repeating the bounded harmonic search on every competing scale.
        alternative = std::max(alternative, scores[a][p]);
      }
    if (alternative >= .8f * best.score) { pause(); return {}; }
    const float dt = seconds - previousTime_;
    const bool continuous = previousRatio_ > 0 && dt > 0 && dt <= .251f &&
                            std::fabs(best.ratio - previousRatio_) <= .025f + .15f * dt;
    streak_ = continuous ? streak_ + 1 : 1;
    previousRatio_ = best.ratio; previousTime_ = seconds;
    if (streak_ < 3) return {};
    const float support = best.hits == 2 ? .55f : best.hits == 3 ? .75f : .9f;
    const float confidence = support * best.quality * (1.f - .5f * age / MAX_AGE);
    if (confidence < .25f) return {};
    return {anchorRpm_ * best.ratio, uint8_t(std::lround(255.f * confidence))};
  }
};
} // namespace RelativePitch
