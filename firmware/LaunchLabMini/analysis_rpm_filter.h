#pragma once
#include <algorithm>
#include <cmath>

namespace AnalysisMode {
// Post-process only acoustically valid readings. Zero means withheld, never a prediction.
// One instance per launch; no heap storage and no feedback into the acoustic tracker.
class RpmFilter {
public:
  static constexpr float ALPHA = 0.6f, BETA = 0.08f, MAX_GAP_S = 1.f;
  float update(float rpm, float seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || (seen_ && seconds <= seenAt_))
      return 0;
    seen_ = true;
    seenAt_ = seconds;
    if (initialized_ && seconds - goodAt_ > MAX_GAP_S) {
      initialized_ = false;
      reacquiring_ = true;
      pending_ = 0;
    }
    if (!std::isfinite(rpm) || rpm <= 0 || rpm > 65535) {
      pending_ = 0;
      return 0;
    }
    if (!initialized_) {
      // The tracker has already confirmed initial acquisition. After a long gap,
      // require a new cluster instead of letting one returning spike reseed us.
      if (!reacquiring_)
        return seed(rpm, seconds);
      return confirm(rpm, seconds);
    }
    const float dt = seconds - goodAt_;
    const float predicted = std::max(0.f, value_ + rate_ * dt);
    const float residual = rpm - predicted;
    if (std::fabs(residual) > std::max(200.f, 0.05f * predicted))
      return confirm(rpm, seconds);
    pending_ = 0;
    value_ = std::clamp(predicted + ALPHA * residual, 0.f, 65535.f);
    rate_ += BETA * residual / dt;
    goodAt_ = seconds;
    return value_;
  }

private:
  float seed(float rpm, float seconds) {
    initialized_ = true;
    reacquiring_ = false;
    pending_ = 0;
    value_ = rpm;
    rate_ = 0;
    goodAt_ = seconds;
    return rpm;
  }
  float confirm(float rpm, float seconds) {
    if (pending_ &&
        (seconds - pendingAt_ > 0.4f || std::fabs(rpm - pendingLast_) > std::max(150.f, 0.03f * rpm)))
      pending_ = 0;
    if (!pending_)
      pendingSum_ = 0;
    pendingSum_ += rpm;
    pendingLast_ = rpm;
    pendingAt_ = seconds;
    if (++pending_ < 3)
      return 0;
    return seed(pendingSum_ / 3.f, seconds);
  }
  bool seen_ = false, initialized_ = false, reacquiring_ = false;
  unsigned pending_ = 0;
  float seenAt_ = 0, goodAt_ = 0, value_ = 0, rate_ = 0, pendingSum_ = 0, pendingLast_ = 0, pendingAt_ = 0;
};
}
