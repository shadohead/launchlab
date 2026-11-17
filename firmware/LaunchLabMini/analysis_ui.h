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
inline void timeMarker(lgfx::LGFXBase &g, uint16_t color, int left, int right,
                       int top, int bottom, float seconds, float span) {
  if (!(span > 0) || !std::isfinite(span) || !std::isfinite(seconds) || seconds < 0 || seconds > span)
    return;
  const int x = std::min(right - 1, left + int(std::lround(seconds / span * (right - left))));
  for (int y = top + 1; y < bottom; y += 4)
    for (int dy = 0; dy < 2 && y + dy < bottom; ++dy)
      for (int dx = 0; dx < 2; ++dx)
        g.drawPixel(x + dx, y + dy, color);
}
inline void timeMarkers(lgfx::LGFXBase &g, const Palette::Colors &c, int left, int right,
                        int top, int bottom, float elapsed, float span, float predictedFinish) {
  if (predictedFinish > 0)
    timeMarker(g, c.warning, left, right, top, bottom, predictedFinish, span);
  // Full-contrast 2x2 dots remain distinct from the faint horizontal grid.
  timeMarker(g, c.text, left, right, top, bottom, elapsed, span);
}
// Reuse the existing screen sprite: no additional framebuffer or heap allocation.
// This cache is never used when drawing directly to the display or during replay.
// It keeps the heading, the hero label and the Launch row; the clock redraws
// once a second and everything from the Finish row down every frame.
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
  void invalidate() { valid = false; }
  bool matches(const AnalysisMode::Session &s, const Palette::Colors &c, int w, const char *name,
               bool sensorReady) const {
    return valid && launch == s.number() && rpm == s.rpm() && colors == c && width == w &&
           running == s.running() && reason == s.stopReason() &&
           (s.number() || (ready == sensorReady && !std::strcmp(state, name)));
  }
};
// Layout rows, top to bottom. The clock is the screen's one large number, in
// the main screen's Font6; everything else is a Font2 row or a Font1 caption.
constexpr int CLOCK_Y = 30, LABEL_Y = 82, LAUNCH_Y = 104, FINISH_Y = 122, CAPTION_Y = 140,
              DYNAMIC_Y = 120, GRAPH_TOP = 156, GRAPH_BOTTOM = 214;
// While the microphone feeds the forecast, the caption line is one row of
// 2x2 dots: it grows over the first 10 s as readings gather, then a single lit
// dot with a short tail glides across while the model refits. It is the
// screen's only animation, one step every 250 ms on the recording's frames.
constexpr unsigned MODEL_STEP_US = 250000;
constexpr int FORECAST_PITCH = 5, COMPUTE_DOTS = 25, COMPUTE_PITCH = 4;
inline int computeLeft(const lgfx::LGFXBase &g) { return (g.width() - (COMPUTE_DOTS - 1) * COMPUTE_PITCH - 2) / 2; }
inline void computing(lgfx::LGFXBase &g, const Palette::Colors &c, uint64_t now, float gathered, bool forecasting) {
  const int left = computeLeft(g), y = CAPTION_Y + 3;
  const int filled = int(std::clamp(gathered, 0.f, 1.f) * COMPUTE_DOTS);
  const int head = int((now / MODEL_STEP_US) % COMPUTE_DOTS);
  for (int i = 0; i < COMPUTE_DOTS; ++i) {
    uint16_t ink = c.faint;
    if (forecasting)
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
// A muted label on the left and its value on the right, as on Device and Details.
inline void row(lgfx::LGFXBase &g, const Palette::Colors &c, int y, const char *label, const char *value,
                uint16_t ink) {
  g.setTextDatum(top_left);
  g.setTextColor(c.muted, c.background);
  g.drawString(label, 4, y, 2);
  g.setTextDatum(top_right);
  g.setTextColor(ink, c.background);
  g.drawString(value, g.width() - 4, y, 2);
  g.setTextDatum(top_center);
}
inline void centered(lgfx::LGFXBase &g, const Palette::Colors &c, int y, const char *text, uint16_t ink,
                     int font = 2) {
  g.setTextDatum(top_center);
  g.setTextColor(ink, c.background);
  g.drawString(text, g.width() / 2, y, font);
}
inline void fittedPlot(lgfx::LGFXBase &g, const Palette::Colors &c, const Decay::Results &fit,
                       const Decay::Session *input, const Decay::Curve *winner, float elapsed, int top = 154,
                       int bottom = 216, bool livePreview = false, const MicTrace::Trace *trace = nullptr,
                       float predictedFinish = NAN) {
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
    timeMarkers(g, c, left, right, top, bottom, elapsed, span, predictedFinish);
  if (winner)
    for (unsigned i = 1; i < Decay::CURVE_POINTS; ++i) {
      const float start = winner->seconds(i - 1), end = winner->seconds(i);
      if (start >= span)
        break;
      const float visibleEnd = std::min(end, span);
      const float a = winner->rpm[i - 1], b = winner->rpm[i];
      if (!std::isfinite(a) || !std::isfinite(b) || end <= start)
        continue;
      // Split even when the first/last acoustic timestamp falls between vertices.
      float from = start;
      int px = x(start), py = y(a);
      auto drawTo = [&](float until, uint16_t color, bool forecast = false) {
        if (until <= from)
          return;
        const int qx = x(until), qy = y(a + (b - a) * ((until - start) / (end - start)));
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
                 const Decay::Curve *winner = nullptr, bool fitting = false, float publishedFinish = NAN,
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
    ControlsUI::heading(g, "Analysis");
    // What the clock is: before a launch the detector's state, as on Main.
    if (!launched)
      centered(g, c, LABEL_Y, detectorState, sensorReady ? c.ready : c.warning);
    else
      centered(g, c, LABEL_Y,
               session.running()                                            ? "Spin time"
               : session.hasManualStop()                                    ? "Spin time"
               : session.stopReason() == AnalysisMode::StopReason::Timeout ? "Time limit"
                                                                            : "Interrupted",
               c.muted);
    if (launched) {
      snprintf(text, sizeof(text), "%.0f RPM", session.rpm());
      row(g, c, LAUNCH_Y, "Launch", text, c.text);
    } else {
      centered(g, c, LAUNCH_Y, "Launch to start", c.muted);
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
  // One status row and one caption under the Launch row.
  float predictedFinish = NAN;
  if (capturePending) {
    centered(g, c, FINISH_Y, "Save pending", c.alert);
    centered(g, c, CAPTION_Y, "Please wait", c.muted, 1);
  } else if (stopped && !session.hasManualStop()) {
    centered(g, c, FINISH_Y, "No stop recorded", c.muted);
  } else if (stopped && fitting) {
    centered(g, c, FINISH_Y, "Fitting curve", c.muted);
    computing(g, c, now, 1, true);
  } else if (stopped) {
    if (winner && fit) {
      // The endpoint estimate scales the shared shape to the launch and the
      // stop without a microphone fit: say so.
      centered(g, c, FINISH_Y, fit->endpointEstimate ? "Estimated curve"
                               : fit->fixedRiccati   ? "Fixed Riccati fit"
                                                     : "Free Riccati fit", c.muted);
    } else {
      centered(g, c, FINISH_Y, "Curve unavailable", c.muted);
    }
  } else if (session.running()) {
    const char *status = mic == MicView::Mic::Failed     ? "Mic unavailable"
                         : mic == MicView::Mic::NoMemory ? "Not enough memory"
                         : mic == MicView::Mic::Starting ? "Mic warming up"
                                                         : nullptr;
    const bool measured = mic == MicView::Mic::On && t.started();
    const auto active = EarlySpin::select(fit && fit->fixedRiccati && winner ? winner->end : publishedFinish,
                                          t.earlyReadings(), elapsedUs / 1000, measured);
    predictedFinish = active.seconds;
    if (std::isfinite(predictedFinish)) {
      // Orange, as the dotted finish line on the graph.
      clock(text, sizeof(text), uint64_t(std::lround(predictedFinish)));
      row(g, c, FINISH_Y, "Finish", text, c.warning);
      if (status)
        centered(g, c, CAPTION_Y, status, c.warning, 1);
      else
        computing(g, c, now, 1, measured);
    } else if (status) {
      centered(g, c, FINISH_Y, status, mic == MicView::Mic::Starting ? c.muted : c.warning);
    } else {
      row(g, c, FINISH_Y, "Finish", "-:--", c.muted);
      computing(g, c, now, float(elapsedUs) / (EarlySpin::FIRST_MS * 1000.f), false);
    }
  } else if (mic == MicView::Mic::Failed || mic == MicView::Mic::NoMemory) {
    centered(g, c, FINISH_Y, mic == MicView::Mic::Failed ? "Mic unavailable" : "Not enough memory", c.warning);
  }
  const float elapsed = float(elapsedUs) / 1000000.f;
  g.setTextColor(c.muted, c.background);
  if (fit && (input || winner) && (stopped || winner))
    fittedPlot(g, c, *fit, input, winner, elapsed, GRAPH_TOP, GRAPH_BOTTOM, session.running(), &t, predictedFinish);
  else {
    const float span = std::max(t.elapsed(), graphSpan(elapsed, session.running(), predictedFinish));
    MicView::plot(g, t, c, 26, g.width() - 5, GRAPH_TOP, GRAPH_BOTTOM, span, true);
    if (session.running())
      timeMarkers(g, c, 26, g.width() - 5, GRAPH_TOP, GRAPH_BOTTOM, elapsed, span, predictedFinish);
  }
  ControlsUI::footer(g, capturePending      ? "B Retry"
                        : session.running() ? "A Replay  B Stop"
                        : launched          ? "A Replay  B Details"
                                            : "Hold B Exit");
}
}
