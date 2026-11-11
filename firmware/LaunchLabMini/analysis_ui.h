#pragma once
#include "analysis_mode.h"
#include "decay_models.h"
#include "mic_view_ui.h"
namespace AnalysisUI {
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
                          const Decay::Session &input, const Decay::Curve *winner, float elapsed,
                          int top = 154, int bottom = 218, bool livePreview = false) {
  const int left = 26, right = g.width() - 5;
  // The live axis is a time window, independent of the unconstrained stop estimate.
  constexpr float previewAheadSeconds = 10.f;
  float span = std::max(MicTrace::MIN_SPAN_S, livePreview ? elapsed + previewAheadSeconds : elapsed);
  if (winner && !livePreview)
    span = std::max(span, winner->end);
  float peak = fit.launcherRpm;
  for (unsigned i = 0; i < input.count; ++i) {
    span = std::max(span, input.samples[i].ms * .001f);
    peak = std::max(peak, input.samples[i].rpm);
  }
  peak = MicTrace::topRpm(peak);
  auto x = [&](float t) { return left + int(std::lround(t / span * (right - left))); };
  auto y = [&](float rpm) {
    return bottom - int(std::lround(std::clamp(rpm / peak, 0.f, 1.f) * (bottom - top)));
  };
  MicView::axes(g, c, left, right, bottom, span, peak, y);
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
  for (unsigned i = 0; i < input.count; ++i) {
    const auto &point = input.samples[i];
    g.drawPixel(x(point.ms * .001f), y(point.rpm), point.excludedFromFit ? Palette::DARKGREY : c.accent);
  }
  if (fit.launcherRpm > 0)
    MicView::marker(g, x(fit.launcherTime), y(fit.launcherRpm), c);
}
inline void draw(lgfx::LGFXBase &g, const AnalysisMode::Session &session, uint64_t now,
                 const MicTrace::Trace *trace, MicView::Mic mic, const LaunchMotion::Trace &motion,
                 bool capturing, bool flipped, const Palette::Colors &c, const AnalogTachometer::Curve &curve,
                 const char *detectorState = "READY", bool sensorReady = true, Cache *cache = nullptr,
                 const Decay::Results *fit = nullptr, const Decay::Session *input = nullptr,
                 const Decay::Curve *winner = nullptr, bool fitting = false) {
  const int cx = g.width() / 2;
  char text[32];
  if (session.replay()) {
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

  const uint64_t seconds = session.elapsedUs(now) / 1000000;
  const uint16_t timerColor = session.number() && !session.running() ? c.muted : c.number;
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
  const char *status = !session.running() && session.number() ? "Stopped"
                       : mic == MicView::Mic::Failed          ? "Mic unavailable"
                       : mic == MicView::Mic::NoMemory        ? "Not enough memory"
                       : mic == MicView::Mic::Starting        ? "Mic warming up"
                       : session.running()                    ? "Predicted finish --:--"
                       : t.heard()                            ? "Mic RPM"
                                                              : "Listening";
  const bool stopped = session.number() && !session.running();
  g.setTextColor(stopped ? c.muted : c.number, c.background);
  const char *firstLine = status;
  const char *secondLine = nullptr;
  char finishLabel[32];
  if (stopped && fitting) {
    firstLine = "Computing Best Fit";
    secondLine = "RPM Curve. . .";
  } else if (session.running() && winner && fit && unsigned(fit->best) < Decay::MODEL_COUNT) {
    const float estimate = Decay::liveFinishSeconds(*fit, float(session.elapsedUs(now)) / 1000000.f);
    firstLine = "Predicted finish --:--";
    if (std::isfinite(estimate)) {
      const unsigned finish = unsigned(std::lround(estimate));
      snprintf(finishLabel, sizeof(finishLabel), "Predicted finish %02u:%02u", finish / 60, finish % 60);
      firstLine = finishLabel;
    }
    const char *name = Decay::modelName(unsigned(fit->best));
    const char *suffix = std::strrchr(name, ' ');
    snprintf(text, sizeof(text), "%.*s", int(suffix ? suffix - name : std::strlen(name)), name);
    secondLine = text;
  } else if (stopped && winner && fit) {
    firstLine = Decay::modelName(unsigned(fit->best));
    // Font 1 is six pixels per character. Break long model names before "Model".
    if (std::strlen(firstLine) * 6u > unsigned(g.width() - 4)) {
      if (const char *space = std::strrchr(firstLine, ' ')) {
        snprintf(text, sizeof(text), "%.*s", int(space - firstLine), firstLine);
        firstLine = text;
        secondLine = space + 1;
      }
    }
  }
  g.drawString(firstLine, cx, 141, 1);
  if (secondLine)
    g.drawString(secondLine, cx, 150, 1);
  const int graphTop = secondLine ? 164 : 154;
  const float elapsed = float(session.elapsedUs(now)) / 1000000.f;
  if (fit && input && (stopped || winner))
    fittedPlot(g, c, *fit, *input, winner, elapsed, graphTop, stopped ? 218 : 207, session.running());
  else
    MicView::plot(g, t, c, 26, g.width() - 5, graphTop, session.running() ? 207 : 218, elapsed, true);
  if (fitting && !session.running()) {
    const int center = (26 + g.width() - 5) / 2;
    const unsigned phase = unsigned((now / 250000) % 3);
    g.fillRect(center - 14, 180, 29, 11, c.background);
    for (unsigned i = 0; i < 3; ++i)
      g.fillCircle(center + int(i) * 8 - 8, 185, 3, i == phase ? c.accent : c.muted);
  }
  if (session.running()) {
    g.setTextDatum(top_left);
    g.setTextColor(c.muted, c.background);
    g.drawString("A Replay", 2, 226, 1);
    const int promptCenter = g.width() - 41;
    g.setTextDatum(top_center);
    g.setTextColor(c.alert, c.background);
    g.drawString("Press B when", promptCenter, 221, 1);
    g.drawString("spin finishes", promptCenter, 231, 1);
  } else {
    ControlsUI::footer(g, "A Replay  B Details");
  }
}
}
