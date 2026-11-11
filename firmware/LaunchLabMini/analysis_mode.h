#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>

// Loop-owned elapsed time, independent of redraw and microphone confidence.
namespace AnalysisMode {
class Session {
public:
  static constexpr uint64_t LIMIT_US = 5ULL * 60 * 1000000;
  void clear() { *this = Session{}; }
  bool begin(uint32_t number, bool valid, float rpm, uint64_t endUs) {
    if (!valid || !number || !endUs || number == number_)
      return false;
    clear();
    number_ = number;
    rpm_ = rpm;
    start_ = endUs;
    running_ = true;
    return true;
  }
  bool stop(uint64_t now, bool manual = true) {
    if (!running_)
      return false;
    const uint64_t elapsed = now >= start_ ? now - start_ : 0;
    stop_ = start_ + std::min(elapsed, LIMIT_US);
    running_ = false;
    manualStop_ = manual && now >= start_ && elapsed <= LIMIT_US;
    return true;
  }
  bool tick(uint64_t now) {
    if (!running_ || now < start_ || now - start_ < LIMIT_US)
      return false;
    return stop(now, false);
  }
  uint64_t elapsedUs(uint64_t now) const {
    if (!number_)
      return 0;
    const auto end = running_ ? now : stop_;
    return end > start_ ? end - start_ : 0;
  }
  static void formatMs(char *out, unsigned size, uint64_t ms) {
    std::snprintf(out, size, "%02u:%02u:%03u", unsigned(ms / 60000), unsigned(ms / 1000 % 60),
                  unsigned(ms % 1000));
  }
  void format(char *out, unsigned size, uint64_t now) const { formatMs(out, size, elapsedUs(now) / 1000); }
  bool results() const { return results_; }
  bool hasManualStop() const { return manualStop_; }
  unsigned tablePage() const { return tablePage_; }
  bool nextResult() {
    if (running_ || !number_)
      return false;
    if (results_)
      ++tablePage_;
    else {
      results_ = true;
      replay_ = false;
      tablePage_ = 0;
    }
    return true;
  }
  void toggleReplay(uint64_t now) {
    if (results_) {
      results_ = false;
      replay_ = false;
      return;
    }
    if (number_) {
      replay_ = !replay_;
      replayAt_ = now;
    }
  }
  bool replay() const { return replay_; }
  uint32_t replayElapsedMs(uint64_t now) const {
    return now >= replayAt_ ? uint32_t((now - replayAt_) / 1000) : 0;
  }
  // Main graph remains clock-driven through silence; replay holds its endpoint without redraws.
  bool needsFrame(uint64_t now, uint32_t sinceDrawMs, uint32_t replayDurationMs) const {
    if (results_)
      return false;
    if (!replay_)
      return running_ && sinceDrawMs >= 100;
    if (sinceDrawMs < 50 || !replayDurationMs)
      return false;
    const uint32_t elapsed = replayElapsedMs(now), cycle = replayDurationMs + 500;
    if (sinceDrawMs >= cycle)
      return true;
    const uint32_t previous = elapsed > sinceDrawMs ? elapsed - sinceDrawMs : 0;
    const uint32_t currentPosition = elapsed % cycle, previousPosition = previous % cycle;
    return (currentPosition < replayDurationMs ? currentPosition : replayDurationMs) !=
           (previousPosition < replayDurationMs ? previousPosition : replayDurationMs);
  }
  bool running() const { return running_; }
  uint32_t number() const { return number_; }
  float rpm() const { return rpm_; }

private:
  bool results_ = false, manualStop_ = false;
  unsigned tablePage_ = 0;
  uint64_t start_ = 0, stop_ = 0;
  uint32_t number_ = 0;
  float rpm_ = 0;
  bool running_ = false, replay_ = false;
  uint64_t replayAt_ = 0;
};
}
