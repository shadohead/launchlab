#pragma once
#include "analysis_details.h"
#include "analysis_mode.h"
#include "controls_ui.h"
#include "palette.h"
namespace AnalysisResultUI {
inline void draw(lgfx::LGFXBase &g, const AnalysisMode::Session &session,
                 const AnalysisDetails::Summary *details, bool fitting, bool failed,
                 const Palette::Colors &c) {
  char text[40];
  const int cx = g.width() / 2;
  g.setTextDatum(top_center);
  g.setTextColor(c.muted, c.background);
  g.drawString("Analysis Details", cx, 3, 1);
  snprintf(text, sizeof(text), "%.0f RPM", session.rpm());
  g.setTextColor(c.number, c.background);
  g.drawString(text, cx, 16, g.textWidth(text, &fonts::Font4) <= g.width() - 6 ? 4 : 2);
  g.setTextColor(c.text, c.background);
  const float duration = session.elapsedUs(0) * .000001f;
  auto row = [&](const char *label, const char *value, int y) {
    g.setTextDatum(top_left);
    g.drawString(label, 2, y, 1);
    g.setTextDatum(top_right);
    g.drawString(value, g.width() - 2, y, 1);
  };
  snprintf(text, sizeof(text), "%.1fs", duration);
  row(session.hasManualStop() ? "Total spin time" : "Recorded time", text, 49);
  bool estimated = details && details->complete;
  if (session.hasManualStop() && duration > 0)
    snprintf(text, sizeof(text), "%.0f", session.rpm() / duration);
  else if (estimated)
    snprintf(text, sizeof(text), "%.0f", details->decayRate);
  else
    snprintf(text, sizeof(text), "--");
  row("Avg decay RPM/s", text, 64);
  if (estimated)
    snprintf(text, sizeof(text), "%.0f", details->revolutions);
  else
    snprintf(text, sizeof(text), "--");
  row("Total revs", text, 79);
  if (session.rpm() > 0 && session.rpm() <= 1000)
    snprintf(text, sizeof(text), "0.0s");
  else if (details && std::isfinite(details->battleStaminaSeconds))
    snprintf(text, sizeof(text), "%.1fs", details->battleStaminaSeconds);
  else
    snprintf(text, sizeof(text), "--");
  g.setTextColor(c.accent, c.background);
  g.setTextDatum(top_left);
  g.drawString("Estimated", 2, 92, 1);
  g.drawString("Battle Stamina", 2, 101, 1);
  g.setTextColor(c.text, c.background);
  g.setTextDatum(top_right);
  g.drawString(text, g.width() - 2, 101, 1);
  g.setTextColor(c.muted, c.background);
  row("RPM band", "Time", 115);
  g.drawFastHLine(2, 126, g.width() - 4, c.accent);
  unsigned pages = 1;
  if (details && details->count) {
    pages = (details->count + AnalysisDetails::ROWS - 1) / AnalysisDetails::ROWS;
    unsigned first = session.tablePage() % pages * AnalysisDetails::ROWS;
    for (unsigned i = first; i < std::min(details->count, first + AnalysisDetails::ROWS); ++i) {
      unsigned band = details->firstBand - i;
      char label[24];
      snprintf(label, sizeof(label), "%uK - %uK", band, band - 1);
      snprintf(text, sizeof(text), "%.1fs", details->seconds[i]);
      const int y = 132 + int(i - first) * 15;
      g.setTextColor(c.text, c.background);
      row(label, text, y);
      g.drawFastHLine(2, y + 10, g.width() - 4, c.accent);
    }
  } else {
    g.setTextDatum(top_center);
    g.drawString(fitting   ? "Comparing models..."
                 : failed  ? "Fit unavailable"
                 : details ? "No complete bands"
                           : "No winning model",
                 cx, 145, 1);
  }
  ControlsUI::footer(g, pages > 1 ? "A Back  B More" : "A Back");
}
}
