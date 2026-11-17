#pragma once
#include "analysis_mode.h"
#include "decay_types.h"
#include "mic_view_ui.h"
namespace AnalysisUI {
inline float graphSpan(float elapsed, bool recording, float predictedFinish = NAN) {
  if (recording && std::isfinite(predictedFinish) && predictedFinish > 0 && elapsed <= predictedFinish)
    return std::max(MicTrace::MIN_SPAN_S, predictedFinish + 10.f);
  return std::max(MicTrace::MIN_SPAN_S, elapsed + (recording ? 10.f : 0.f));
}
// A cheap deterministic scramble: the same frame always draws the same glyphs.
inline uint32_t scramble(uint64_t step, unsigned i) {
  uint32_t x = uint32_t(step) * 2654435761u ^ (i + 1) * 40503u;
  x ^= x >> 13;
  x *= 0x5bd1e995u;
  return x ^ (x >> 15);
}
inline int markerX(int left, int right, float seconds, float span) {
  return std::min(right - 1, left + int(std::lround(seconds / span * (right - left))));
}
inline void timeMarker(lgfx::LGFXBase &g, uint16_t color, int left, int right,
                       int top, int bottom, float seconds, float span) {
  if (!(span > 0) || !std::isfinite(span) || !std::isfinite(seconds) || seconds < 0 || seconds > span)
    return;
  const int x = markerX(left, right, seconds, span);
  for (int y = top + 1; y < bottom; y += 4)
    for (int dy = 0; dy < 2 && y + dy < bottom; ++dy)
      for (int dx = 0; dx < 2; ++dx)
        g.drawPixel(x + dx, y + dy, color);
}
// While the model works, short trails of 2x2 dots fall between the now line
// and the finish line (to the graph's edge before there is a finish), on the
// markers' own 4 px rows. Mostly gray; one column in three leads in accent.
constexpr int RAIN_PITCH = 6, RAIN_TRAIL = 3;
constexpr unsigned RAIN_STEP_US = 100000;
inline void rain(lgfx::LGFXBase &g, const Palette::Colors &c, int left, int right, int top, int bottom,
                 float elapsed, float span, float predictedFinish, uint64_t now) {
  if (!(span > 0) || !std::isfinite(span) || !std::isfinite(elapsed))
    return;
  const int from = markerX(left, right, elapsed, span) + 4;
  const int to = predictedFinish > 0 ? markerX(left, right, predictedFinish, span) - 2 : right;
  const int rows = (bottom - top - 1) / 4;
  const uint64_t step = now / RAIN_STEP_US;
  // Columns sit on a fixed screen grid, so they do not slide as the now line moves.
  for (int x = left + RAIN_PITCH * ((from - left + RAIN_PITCH - 1) / RAIN_PITCH); x + 2 <= to; x += RAIN_PITCH) {
    const uint32_t h = scramble(uint64_t(x), 7);
    const unsigned period = unsigned(rows + RAIN_TRAIL) + h % unsigned(rows / 2 + 1);
    const int head = int((step * (1 + h % 2) + h) % period);
    for (int k = 0; k < RAIN_TRAIL; ++k) {
      const int r = head - k;
      if (r < 0 || r >= rows)
        continue;
      const uint16_t ink = k == 0 ? (h % 3 == 0 ? c.accent : c.muted) : k == 1 ? c.muted : c.faint;
      g.fillRect(x, top + 1 + r * 4, 2, 2, ink);
    }
  }
}
inline void timeMarkers(lgfx::LGFXBase &g, const Palette::Colors &c, int left, int right,
                        int top, int bottom, float elapsed, float span, float predictedFinish,
                        bool raining = false, uint64_t now = 0) {
  if (raining)
    rain(g, c, left, right, top, bottom, elapsed, span, predictedFinish, now);
  if (predictedFinish > 0)
    timeMarker(g, c.warning, left, right, top, bottom, predictedFinish, span);
  // Full-contrast 2x2 dots remain distinct from the faint horizontal grid.
  timeMarker(g, c.text, left, right, top, bottom, elapsed, span);
}
// Reuse the existing screen sprite: no additional framebuffer or heap allocation.
// This cache is never used when drawing directly to the display or during replay.
// It keeps the heading and the Launch row; the clock redraws once a second and
// everything from the RPM row down every frame. It also remembers the shown
// finish estimate so a new value can decode into place.
struct Cache {
  bool valid = false, ready = false, running = false;
  uint32_t launch = 0;
  float rpm = 0;
  int width = 0;
  uint64_t seconds = 0;
  uint16_t timerColor = 0;
  AnalysisMode::StopReason reason = AnalysisMode::StopReason::Timeout;
  Palette::Colors colors;
  char state[32] = {};
  char finish[8] = {}, previous[8] = {};
  uint64_t finishSince = 0;
  void invalidate() { valid = false; finish[0] = previous[0] = 0; }
  bool matches(const AnalysisMode::Session &s, const Palette::Colors &c, int w, const char *name,
               bool sensorReady) const {
    return valid && launch == s.number() && rpm == s.rpm() && colors == c && width == w &&
           running == s.running() && reason == s.stopReason() &&
           (s.number() || (ready == sensorReady && !std::strcmp(state, name)));
  }
};
// Layout rows, top to bottom. The clock is the screen's one large number, in
// the main screen's Font6; a hairline under it runs to the estimated finish;
// then three Font2 rows and a Font1 caption line above the graph.
constexpr int CLOCK_Y = 30, PROGRESS_Y = 80, LAUNCH_Y = 86, RPM_Y = 104, FINISH_Y = 122,
              CAPTION_Y = 141, DYNAMIC_Y = 102, GRAPH_TOP = 156, GRAPH_BOTTOM = 214;
// While the microphone feeds the forecast, the caption line is one row of
// 2x2 dots: it grows over the first 10 s as readings gather, then a single lit
// dot with a short tail glides across while the model refits. Every ten
// seconds the row gives way to a word for three seconds, decoded from glyphs.
constexpr unsigned MODEL_STEP_US = 250000, WORD_CYCLE_US = 10000000, WORD_SHOW_US = 3000000,
                   DECODE_CHAR_US = 60000, VALUE_DECODE_US = 400000;
constexpr int FORECAST_PITCH = 5, COMPUTE_DOTS = 25, COMPUTE_PITCH = 4;
enum class Work : uint8_t { Listening, Forecasting, Fitting };
inline int computeLeft(const lgfx::LGFXBase &g) { return (g.width() - (COMPUTE_DOTS - 1) * COMPUTE_PITCH - 2) / 2; }
inline const char *workWord(Work work, uint64_t cycle) {
  static const char *const listening[] = {"LISTENING", "ANALYZING"};
  static const char *const forecasting[] = {"ANALYZING", "LISTENING", "CALCULATING"};
  static const char *const fitting[] = {"CALCULATING", "ANALYZING"};
  switch (work) {
  case Work::Listening: return listening[cycle % 2];
  case Work::Forecasting: return forecasting[cycle % 3];
  default: return fitting[cycle % 2];
  }
}
// The word, if one shows on this frame; `resolved` is how many letters have
// settled, the rest still cycling glyphs.
inline const char *cueWord(Work work, uint64_t now, unsigned &resolved) {
  const uint64_t into = now % WORD_CYCLE_US;
  if (into < WORD_CYCLE_US - WORD_SHOW_US)
    return nullptr;
  resolved = unsigned((into - (WORD_CYCLE_US - WORD_SHOW_US)) / DECODE_CHAR_US);
  return workWord(work, now / WORD_CYCLE_US);
}
inline void computing(lgfx::LGFXBase &g, const Palette::Colors &c, uint64_t now, float gathered, Work work) {
  unsigned resolved = 0;
  if (const char *word = cueWord(work, now, resolved)) {
    static constexpr char GLYPHS[] = "01<>/*+=:#%";
    const unsigned n = unsigned(std::strlen(word));
    const int pitch = 7, left = (g.width() - int(n) * pitch + 1) / 2;
    g.setTextDatum(top_left);
    char one[2] = {};
    for (unsigned i = 0; i < n; ++i) {
      const bool settled = i < resolved;
      one[0] = settled ? word[i] : GLYPHS[scramble(now / DECODE_CHAR_US, i) % (sizeof(GLYPHS) - 1)];
      g.setTextColor(settled ? c.muted : c.accent, c.background);
      g.drawString(one, left + int(i) * pitch, CAPTION_Y, 1);
    }
    g.setTextDatum(top_center);
    return;
  }
  const int left = computeLeft(g), y = CAPTION_Y + 3;
  const int filled = int(std::clamp(gathered, 0.f, 1.f) * COMPUTE_DOTS);
  const int head = int((now / MODEL_STEP_US) % COMPUTE_DOTS);
  for (int i = 0; i < COMPUTE_DOTS; ++i) {
    uint16_t ink = c.faint;
    if (work != Work::Listening)
      ink = i == head ? c.accent : i == head - 1 || (head == 0 && i == COMPUTE_DOTS - 1) ? c.muted : c.faint;
    else if (i < filled)
      ink = c.muted;
    else if (i == filled)
      ink = c.accent;
    else
      continue; // Gathering: the row grows from nothing.
    g.fillRect(left + i * COMPUTE_PITCH, y, 2, 2, ink);
  }
}
// m:ss, which Font6 draws: Analysis stops itself at five minutes.
inline void clock(char *out, unsigned size, uint64_t seconds) {
  std::snprintf(out, size, "%u:%02u", unsigned(seconds / 60), unsigned(seconds % 60));
}
// A colour key, the label muted, and its value on the right. The key matches
// what the row draws on the graph: white launch, cyan readings, orange finish.
inline void label(lgfx::LGFXBase &g, const Palette::Colors &c, int y, const char *text, uint16_t key) {
  g.fillRect(4, y + 6, 3, 3, key);
  g.setTextDatum(top_left);
  g.setTextColor(c.muted, c.background);
  g.drawString(text, 12, y, 2);
}
inline void row(lgfx::LGFXBase &g, const Palette::Colors &c, int y, const char *name, uint16_t key,
                const char *value, uint16_t ink) {
  label(g, c, y, name, key);
  g.setTextDatum(top_right);
  g.setTextColor(ink, c.background);
  g.drawString(value, g.width() - 4, y, 2);
  g.setTextDatum(top_center);
}
// A right-aligned value whose digits after `settled` still cycle, drawn in
// cells of the true characters' widths so nothing shifts as they settle.
inline void decodingValue(lgfx::LGFXBase &g, const Palette::Colors &c, int y, const char *value,
                          const bool *settled, uint16_t ink, uint64_t step) {
  bool all = true;
  for (unsigned i = 0; value[i]; ++i)
    all = all && settled[i];
  if (all) {
    g.setTextDatum(top_right);
    g.setTextColor(ink, c.background);
    g.drawString(value, g.width() - 4, y, 2);
    g.setTextDatum(top_center);
    return;
  }
  int x = g.width() - 4 - g.textWidth(value, &fonts::Font2);
  g.setTextDatum(top_left);
  char one[2] = {};
  for (unsigned i = 0; value[i]; ++i) {
    const bool digit = value[i] >= '0' && value[i] <= '9';
    const bool fixed = !digit || settled[i];
    one[0] = fixed ? value[i] : char('0' + scramble(step, i) % 10);
    g.setTextColor(fixed ? ink : c.muted, c.background);
    g.drawString(one, x, y, 2);
    one[0] = value[i];
    x += g.textWidth(one, &fonts::Font2);
  }
  g.setTextDatum(top_center);
}
inline void centered(lgfx::LGFXBase &g, const Palette::Colors &c, int y, const char *text, uint16_t ink,
                     int font = 2) {
  g.setTextDatum(top_center);
  g.setTextColor(ink, c.background);
  g.drawString(text, g.width() / 2, y, font);
}
// The hairline under the clock: faint end to end, cyan up to now, with an
// orange cap at the estimated finish.
inline void progress(lgfx::LGFXBase &g, const Palette::Colors &c, float elapsed, float finish) {
  const int left = 4, right = g.width() - 4;
  g.drawFastHLine(left, PROGRESS_Y, right - left, c.faint);
  if (!(finish > 0) || !std::isfinite(finish))
    return;
  const int x = left + int(std::lround(std::clamp(elapsed / finish, 0.f, 1.f) * (right - left - 2)));
  g.drawFastHLine(left, PROGRESS_Y, x - left, c.accent);
  g.fillRect(right - 2, PROGRESS_Y - 1, 2, 3, c.warning);
}
inline void fittedPlot(lgfx::LGFXBase &g, const Palette::Colors &c, const Decay::Results &fit,
                       const Decay::Session *input, const Decay::Curve *winner, float elapsed, int top = 154,
                       int bottom = 216, bool livePreview = false, const MicTrace::Trace *trace = nullptr,
                       float predictedFinish = NAN, bool raining = false, uint64_t now = 0) {
  const int left = 26, right = g.width() - 5;
  // An allocation-free endpoint estimate has no copied input. Its raw trace
  // is still available for context, with every unused reading shown in gray.
  const unsigned count = input ? input->count : trace ? trace->count() : 0;
  auto time = [&](unsigned i) { return input ? input->samples[i].ms * .001f : MicTrace::Trace::seconds(i); };
  auto rpm = [&](unsigned i) { return input ? input->samples[i].rpm : float(trace->rawRpm(i)); };
  float span = graphSpan(elapsed, livePreview, predictedFinish);
  float peak = fit.launcherRpm;
  for (unsigned i = 0; i < count; ++i) {
    span = std::max(span, time(i));
    peak = std::max(peak, rpm(i));
  }
  peak = MicTrace::topRpm(peak);
  auto x = [&](float t) { return left + int(std::lround(t / span * (right - left))); };
  auto y = [&](float rpm) {
    return bottom - int(std::lround(std::clamp(rpm / peak, 0.f, 1.f) * (bottom - top)));
  };
  g.setTextColor(c.muted, c.background);
  MicView::axes(g, c, left, right, bottom, span, peak, y);
  if (livePreview)
    timeMarkers(g, c, left, right, top, bottom, elapsed, span, predictedFinish, raining, now);
  // Keep measured times intact and stretch only the projected tail to the
  // displayed finish. The stamina forecast can differ from the raw Riccati end.
  const bool retime = livePreview && winner && winner->end > fit.lastTime && predictedFinish > fit.lastTime;
  auto curveX = [&](float t) {
    if (retime && t > fit.lastTime)
      t = fit.lastTime + (t - fit.lastTime) * (predictedFinish - fit.lastTime) / (winner->end - fit.lastTime);
    return x(t);
  };
  if (winner)
    for (unsigned i = 1; i < Decay::CURVE_POINTS; ++i) {
      const float start = winner->seconds(i - 1), end = winner->seconds(i);
      if (!retime && start >= span)
        break;
      const float visibleEnd = retime ? end : std::min(end, span);
      const float a = winner->rpm[i - 1], b = winner->rpm[i];
      if (!std::isfinite(a) || !std::isfinite(b) || end <= start)
        continue;
      // Split even when the first/last acoustic timestamp falls between vertices.
      float from = start;
      int px = curveX(start), py = y(a);
      auto drawTo = [&](float until, uint16_t color, bool forecast = false) {
        if (until <= from)
          return;
        const int qx = curveX(until), qy = y(a + (b - a) * ((until - start) / (end - start)));
        if (forecast) {
          // The prediction ahead of the last reading: 2x2 dots, so it reads
          // apart from what was measured.
          for (int dx = px; dx < qx; ++dx)
            if ((dx - left) % FORECAST_PITCH == 0)
              g.fillRect(dx, py + (qy - py) * (dx - px) / std::max(1, qx - px), 2, 2, c.accent);
        } else {
          g.drawLine(px, py, qx, qy, color);
        }
        px = qx;
        py = qy;
        from = until;
      };
      drawTo(std::min(visibleEnd, fit.acousticStart), Palette::DARKGREY);
      drawTo(std::min(visibleEnd, fit.lastTime), c.accent);
      drawTo(visibleEnd, Palette::DARKGREY, livePreview);
    }
  for (unsigned i = 0; i < count; ++i) {
    if (rpm(i) <= 0) continue;
    const bool excluded = fit.endpointEstimate || !input || input->samples[i].excludedFromFit;
    g.drawPixel(x(time(i)), y(rpm(i)), excluded ? Palette::DARKGREY : c.accent);
  }
  if (fit.launcherRpm > 0)
    MicView::marker(g, x(fit.launcherTime), y(fit.launcherRpm), c);
}
inline void draw(lgfx::LGFXBase &g, const AnalysisMode::Session &session, uint64_t now,
                 const MicTrace::Trace *trace, MicView::Mic mic, const LaunchMotion::Trace &motion,
                 bool capturing, bool flipped, const Palette::Colors &c, const AnalogTachometer::Curve &curve,
                 const char *detectorState = "READY", bool sensorReady = true, Cache *cache = nullptr,
                 const Decay::Results *fit = nullptr, const Decay::Session *input = nullptr,
                 const Decay::Curve *winner = nullptr, bool fitting = false, float activeFinish = NAN,
                 bool capturePending = false) {
  char text[32];
  if (session.replay() && !capturePending) {
    if (cache)
      cache->invalidate();
    const uint32_t duration = MotionReplay::playbackDurationMs(motion);
    const uint32_t position = duration ? session.replayElapsedMs(now) % (duration + 500) : 0;
    MotionUI::feedback(g, motion, capturing, false,
                       duration ? std::min(1.f, float(position) / duration) : 1.f, flipped, c, nullptr,
                       curve);
    ControlsUI::footer(g, session.running() ? "A Back  B Stop" : "A Back  B Details");
    return;
  }
  const bool launched = session.number() != 0;
  const bool stopped = launched && !session.running();
  const bool reuse = cache && cache->matches(session, c, g.width(), detectorState, sensorReady);
  if (cache) {
    if (reuse)
      g.fillRect(0, DYNAMIC_Y, g.width(), g.height() - DYNAMIC_Y, c.background);
    else
      g.fillRect(0, 0, g.width(), g.height(), c.background);
  }
  g.setTextDatum(top_center);
  if (!reuse) {
    ControlsUI::heading(g, "Stamina Analysis");
    // Before a launch the detector's state, as on Main.
    if (launched) {
      snprintf(text, sizeof(text), "%.0f RPM", session.rpm());
      row(g, c, LAUNCH_Y, "Launch", c.text, text, c.text);
    } else {
      centered(g, c, LAUNCH_Y, detectorState, sensorReady ? c.ready : c.warning);
    }
    if (cache) {
      cache->valid = true;
      cache->launch = session.number();
      cache->rpm = session.rpm();
      cache->colors = c;
      cache->width = g.width();
      cache->ready = sensorReady;
      cache->running = session.running();
      cache->reason = session.stopReason();
      std::snprintf(cache->state, sizeof(cache->state), "%s", detectorState);
    }
  }

  const uint64_t elapsedUs = session.elapsedUs(now);
  const uint64_t seconds = elapsedUs / 1000000;
  // The clock stays at full strength once stopped: it is the result.
  const uint16_t timerColor = launched ? c.number : c.muted;
  if (!reuse || cache->seconds != seconds || cache->timerColor != timerColor) {
    if (reuse)
      g.fillRect(0, CLOCK_Y, g.width(), 48, c.background);
    clock(text, sizeof(text), seconds);
    g.setTextColor(timerColor, c.background);
    g.drawString(text, g.width() / 2, CLOCK_Y, 6);
    if (cache) {
      cache->seconds = seconds;
      cache->timerColor = timerColor;
    }
  }
  static const MicTrace::Trace empty;
  const auto &t = trace && trace->launch() == session.number() && launched ? *trace : empty;
  const float predictedFinish = session.running() ? activeFinish : NAN;
  const bool measured = mic == MicView::Mic::On && t.started();
  // The RPM row: the latest heard reading, cyan as the readings on the graph.
  if (!launched) {
    centered(g, c, RPM_Y, "Launch to start", c.muted);
  } else if (session.running() || t.heard()) {
    const unsigned latest = t.latestRpm();
    if (latest)
      snprintf(text, sizeof(text), "%u RPM", latest);
    row(g, c, RPM_Y, session.running() ? "Now" : "Last", c.accent, latest ? text : "- RPM",
        latest && session.running() ? c.accent : c.muted);
  }
  if (session.running() && reuse)
    g.fillRect(0, PROGRESS_Y - 1, g.width(), 3, c.background);
  if (session.running())
    progress(g, c, float(elapsedUs) / 1000000.f, predictedFinish);
  // The Finish row and one caption line under it.
  if (capturePending) {
    centered(g, c, FINISH_Y, "Save pending", c.alert);
    centered(g, c, CAPTION_Y, "Please wait", c.muted, 1);
  } else if (stopped && !session.hasManualStop()) {
    centered(g, c, FINISH_Y,
             session.stopReason() == AnalysisMode::StopReason::Timeout ? "Time limit" : "Interrupted", c.muted);
    centered(g, c, CAPTION_Y, "No stop recorded", c.muted, 1);
  } else if (stopped && fitting) {
    centered(g, c, FINISH_Y, "Fitting curve", c.muted);
    computing(g, c, now, 1, Work::Fitting);
  } else if (stopped) {
    // The endpoint estimate scales the shared shape to the launch and the
    // stop without a microphone fit: say so. Model names stay off screen.
    centered(g, c, FINISH_Y, !(winner && fit) ? "Curve unavailable"
                             : fit->endpointEstimate ? "Estimated curve"
                                                     : "Curve fitted", c.muted);
  } else if (session.running()) {
    const char *status = mic == MicView::Mic::Failed     ? "Mic unavailable"
                         : mic == MicView::Mic::NoMemory ? "Not enough memory"
                         : mic == MicView::Mic::Starting ? "Mic warming up"
                                                         : nullptr;
    if (std::isfinite(predictedFinish)) {
      // "~" marks it as the current estimate; orange, as the finish line on the
      // graph. A new value decodes into place, its changed digits cycling.
      char value[8];
      value[0] = '~';
      clock(value + 1, sizeof(value) - 1, uint64_t(std::lround(predictedFinish)));
      bool settled[8];
      std::fill(std::begin(settled), std::end(settled), true);
      if (cache) {
        if (std::strcmp(cache->finish, value)) {
          std::snprintf(cache->previous, sizeof(cache->previous), "%s", cache->finish);
          std::snprintf(cache->finish, sizeof(cache->finish), "%s", value);
          cache->finishSince = now;
        }
        const uint64_t age = now - cache->finishSince;
        const unsigned n = unsigned(std::strlen(value));
        for (unsigned i = 0; i < n; ++i) {
          const bool changed = std::strlen(cache->previous) != n || cache->previous[i] != value[i];
          // Left to right, a digit at a time over the decode.
          settled[i] = !changed || age >= VALUE_DECODE_US * (i + 1) / n;
        }
      }
      label(g, c, FINISH_Y, "Finish", c.warning);
      decodingValue(g, c, FINISH_Y, value, settled, c.warning, now / (DECODE_CHAR_US));
      if (status)
        centered(g, c, CAPTION_Y, status, c.warning, 1);
      else
        computing(g, c, now, 1, measured ? Work::Forecasting : Work::Listening);
    } else if (status) {
      centered(g, c, FINISH_Y, status, mic == MicView::Mic::Starting ? c.muted : c.warning);
    } else if (measured) {
      // Gathering the first ten seconds: the value cycles until there is one.
      label(g, c, FINISH_Y, "Finish", c.warning);
      static constexpr bool none[8] = {};
      decodingValue(g, c, FINISH_Y, "~0:00", none, c.warning, now / MODEL_STEP_US);
      computing(g, c, now, float(elapsedUs) / (EarlySpin::FIRST_MS * 1000.f), Work::Listening);
    } else {
      row(g, c, FINISH_Y, "Finish", c.warning, "-:--", c.muted);
    }
  } else if (mic == MicView::Mic::Failed || mic == MicView::Mic::NoMemory) {
    centered(g, c, FINISH_Y, mic == MicView::Mic::Failed ? "Mic unavailable" : "Not enough memory", c.warning);
  }
  const float elapsed = float(elapsedUs) / 1000000.f;
  // The model is at work while the microphone feeds a running analysis.
  const bool raining = session.running() && measured && !capturePending;
  g.setTextColor(c.muted, c.background);
  if (fit && (input || winner) && (stopped || winner))
    fittedPlot(g, c, *fit, input, winner, elapsed, GRAPH_TOP, GRAPH_BOTTOM, session.running(), &t, predictedFinish,
               raining, now);
  else {
    const float span = std::max(t.elapsed(), graphSpan(elapsed, session.running(), predictedFinish));
    MicView::plot(g, t, c, 26, g.width() - 5, GRAPH_TOP, GRAPH_BOTTOM, span, true);
    if (session.running())
      timeMarkers(g, c, 26, g.width() - 5, GRAPH_TOP, GRAPH_BOTTOM, elapsed, span, predictedFinish, raining, now);
  }
  ControlsUI::footer(g, capturePending      ? "B Retry"
                        : session.running() ? "A Replay  B Stop"
                        : launched          ? "A Replay  B Details"
                                            : "Hold B Exit");
}
}
