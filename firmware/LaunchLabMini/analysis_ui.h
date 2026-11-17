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
struct Cache {
  bool valid = false, ready = false;
  uint32_t launch = 0;
  float rpm = 0;
  int width = 0;
  uint64_t seconds = 0;
  uint16_t timerColor = 0;
  Palette::Colors colors;
  char state[32] = {};
  void invalidate() { valid = false; }
  bool matches(const AnalysisMode::Session &s, const Palette::Colors &c, int w, const char *name,
               bool sensorReady) const {
    return valid && launch == s.number() && rpm == s.rpm() && colors == c && width == w &&
           (s.number() || (ready == sensorReady && !std::strcmp(state, name)));
  }
};
// Native geometry keeps the stopwatch sharp and follows the selected theme.
inline void stopwatch(lgfx::LGFXBase &g, int cx, int cy, int radius, uint16_t color) {
  const int top = cy - radius;
  g.fillRect(cx - 2, top - 6, 5, 8, color);
  g.fillRect(cx - 8, top - 10, 16, 5, color);
  g.fillCircle(cx - 8, top - 8, 2, color);
  g.fillCircle(cx + 8, top - 8, 2, color);
  const int diagonal = int(std::lround(radius * 0.70710678f));
  const int x = cx - diagonal, y = cy - diagonal;
  // Diagonal stem and rounded upper-left button.
  g.fillTriangle(x + 2, y - 2, x - 5, y - 9, x - 9, y - 5, color);
  g.fillTriangle(x + 2, y - 2, x - 9, y - 5, x - 2, y + 2, color);
  g.fillTriangle(x - 2, y - 10, x - 5, y - 13, x - 13, y - 5, color);
  g.fillTriangle(x - 2, y - 10, x - 13, y - 5, x - 10, y - 2, color);
  g.fillCircle(x - 4, y - 11, 2, color);
  g.fillCircle(x - 11, y - 4, 2, color);
  for (int inset = 0; inset < 3; ++inset)
    g.drawCircle(cx, cy, radius - inset, color);
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
      auto drawTo = [&](float until, uint16_t color) {
        if (until <= from)
          return;
        const int qx = x(until), qy = y(a + (b - a) * ((until - start) / (end - start)));
        g.drawLine(px, py, qx, qy, color);
        px = qx;
        py = qy;
        from = until;
      };
      drawTo(std::min(visibleEnd, fit.acousticStart), Palette::DARKGREY);
      drawTo(std::min(visibleEnd, fit.lastTime), c.accent);
      drawTo(visibleEnd, Palette::DARKGREY);
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
  const int cx = g.width() / 2;
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
  const bool reuse = cache && cache->matches(session, c, g.width(), detectorState, sensorReady);
  if (cache) {
    if (reuse)
      g.fillRect(0, 130, g.width(), 110, c.background);
    else
      g.fillRect(0, 0, g.width(), 240, c.background);
  }
  g.setTextDatum(top_center);
  if (!reuse) {
    g.setTextColor(c.muted, c.background);
    g.drawString("Analysis", cx, 3, 1);
    if (session.number()) {
      snprintf(text, sizeof(text), "%.0f RPM", session.rpm());
      g.setTextColor(c.number, c.background);
      const bool large = g.textWidth(text, &fonts::Font4) <= g.width() - 6;
      g.drawString(text, cx, 16, large ? 4 : 2);
    } else {
      g.setTextColor(sensorReady ? c.ready : c.warning, c.background);
      g.drawString(detectorState, cx, 16, 2);
    }
    if (cache) {
      cache->valid = true;
      cache->launch = session.number();
      cache->rpm = session.rpm();
      cache->colors = c;
      cache->width = g.width();
      cache->ready = sensorReady;
      std::snprintf(cache->state, sizeof(cache->state), "%s", detectorState);
    }
  }

  const uint64_t elapsedUs = session.elapsedUs(now);
  const uint64_t seconds = elapsedUs / 1000000;
  const bool stopped = session.number() && !session.running();
  const uint16_t timerColor = stopped ? c.muted : c.number;
  if (!reuse || cache->seconds != seconds || cache->timerColor != timerColor) {
    snprintf(text, sizeof(text), "%02u:%02u", unsigned(seconds / 60), unsigned(seconds % 60));
    const int width = g.textWidth(text, &fonts::Font4);
    if (reuse) {
      g.fillRect(cx - width / 2, 80, width, 26, c.background);
    } else {
      const float halfWidth = width * .5f;
      const int radius = int(std::ceil(std::sqrt(halfWidth * halfWidth + 13.f * 13.f))) + 2;
      stopwatch(g, cx, 90, radius, c.muted);
    }
    // Visible Font4 digits occupy rows 2..18, centered on the dial at y=90.
    g.setTextColor(timerColor, c.background);
    g.drawString(text, cx, 80, 4);
    if (cache) {
      cache->seconds = seconds;
      cache->timerColor = timerColor;
    }
  }
  static const MicTrace::Trace empty;
  const auto &t = trace && trace->launch() == session.number() && session.number() ? *trace : empty;
  char timestamp[16];
  if (t.heard())
    AnalysisMode::Session::formatMs(timestamp, sizeof(timestamp),
                                    uint64_t(std::llround(t.lastSeconds() * 1000)));
  else
    snprintf(timestamp, sizeof(timestamp), "--:--:---");
  snprintf(text, sizeof(text), "Last %s", timestamp);
  g.setTextColor(c.muted, c.background);
  g.drawString(text, cx, 131, 1);
  const char *status = stopped                                ? "Curve unavailable"
                       : mic == MicView::Mic::Failed          ? "Mic unavailable"
                       : mic == MicView::Mic::NoMemory        ? "Not enough memory"
                       : mic == MicView::Mic::Starting        ? "Mic warming up"
                       : session.running()                    ? "Predicted finish --:--"
                                                              : "Listening";
  g.setTextColor(stopped ? c.muted : c.number, c.background);
  const char *firstLine = status;
  const char *secondLine = nullptr;
  char finishLabel[32];
  float predictedFinish = NAN;
  bool forecastLabel = false;
  if (capturePending) {
    firstLine = "Save pending";secondLine = "Please wait";
    g.setTextColor(c.alert,c.background);
  } else if (stopped && !session.hasManualStop()) {
    firstLine = session.stopReason()==AnalysisMode::StopReason::Timeout ? "Time limit reached" : "Recording interrupted";
    secondLine = "No stop recorded";
  } else if (stopped && fitting) {
    firstLine = "Computing Best Fit";
    secondLine = "RPM Curve. . .";
  } else if (session.running()) {
    forecastLabel = mic == MicView::Mic::On; // Includes the initial --:-- placeholder.
    const bool measured = mic == MicView::Mic::On && t.started();
    const auto active = EarlySpin::select(fit && fit->fixedRiccati && winner ? winner->end : publishedFinish,
                                          t.earlyReadings(), elapsedUs / 1000, measured);
    predictedFinish = active.seconds;
    if(std::isfinite(predictedFinish)){
      forecastLabel = true;
      const unsigned sec=unsigned(std::lround(predictedFinish));
      snprintf(finishLabel,sizeof(finishLabel),"Predicted finish %02u:%02u",sec/60,sec%60);
      firstLine=finishLabel;secondLine=active.source==EarlySpin::Source::FixedRiccati ? "Fixed Riccati" :
          measured ? "Early mic estimate" : "Population estimate";
    }
  } else if (stopped && winner && fit) {
    firstLine = fit->endpointEstimate ? "Fixed Riccati Estimate"
                                     : fit->fixedRiccati ? "Fixed Riccati Model" : "Free Riccati Model";
  }
  if (forecastLabel)
    g.setTextColor(c.warning, c.background);
  g.drawString(firstLine, cx, 141, 1);
  if (secondLine) {
    if (forecastLabel)
      g.setTextColor(c.number, c.background);
    g.drawString(secondLine, cx, 150, 1);
  }
  const int graphTop = secondLine ? 164 : 154;
  const float elapsed = float(elapsedUs) / 1000000.f;
  const int graphBottom = session.running() ? 207 : 216;
  g.setTextColor(c.muted, c.background);
  if (fit && (input || winner) && (stopped || winner))
    fittedPlot(g, c, *fit, input, winner, elapsed, graphTop, graphBottom, session.running(), &t, predictedFinish);
  else {
    const float span = std::max(t.elapsed(), graphSpan(elapsed, session.running(), predictedFinish));
    MicView::plot(g, t, c, 26, g.width() - 5, graphTop, graphBottom,
                  span, true);
    if (session.running())
      timeMarkers(g, c, 26, g.width() - 5, graphTop, graphBottom, elapsed, span, predictedFinish);
  }
  if (fitting && !session.running()) {
    const int center = (26 + g.width() - 5) / 2;
    const unsigned phase = unsigned((now / 250000) % 3);
    g.fillRect(center - 14, 180, 29, 11, c.background);
    for (unsigned i = 0; i < 3; ++i)
      g.fillCircle(center + int(i) * 8 - 8, 185, 3, i == phase ? c.accent : c.muted);
  }
  if (capturePending) {
    ControlsUI::footer(g, "B Retry");
  } else if (session.running()) {
    g.setTextDatum(top_left);
    g.setTextColor(c.muted, c.background);
    g.drawString("A Replay", 2, 226, 1);
    const int promptCenter = g.width() - 41;
    g.setTextDatum(top_center);
    g.setTextColor(c.alert, c.background);
    g.drawString("Press B when", promptCenter, 221, 1);
    g.drawString("spin finishes", promptCenter, 231, 1);
  } else {
    ControlsUI::footer(g, session.number() ? "A Replay  B Details" : "Hold B Exit");
  }
}
}
