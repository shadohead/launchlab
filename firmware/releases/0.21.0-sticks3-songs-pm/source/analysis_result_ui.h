#pragma once
#include "analysis_details.h"
#include "analysis_mode.h"
#include "controls_ui.h"
#include "palette.h"
namespace AnalysisResultUI {
// Rows, top to bottom: five Font2 label/value rows, then the time spent in
// each 1,000 RPM band as a Font1 table with one bar per band.
constexpr int ROW_Y = 30, ROW_PITCH = 18, NOTE_Y = 119, TABLE_Y = 128, BAND_Y = 143, BAND_PITCH = 14;
constexpr int BAR_LEFT = 34, BAR_RIGHT = 88, BAR_HEIGHT = 4;
inline void draw(lgfx::LGFXBase &g, const AnalysisMode::Session &session,
                 const AnalysisDetails::Summary *details, bool fitting, bool failed,
                 const Palette::Colors &c) {
  char text[40];
  const int cx = g.width() / 2;
  ControlsUI::heading(g, "Details");
  int y = ROW_Y;
  auto row = [&](const char *label, const char *value, uint16_t ink) {
    g.setTextDatum(top_left);
    g.setTextColor(c.muted, c.background);
    g.drawString(label, 4, y, 2);
    g.setTextDatum(top_right);
    g.setTextColor(ink, c.background);
    g.drawString(value, g.width() - 4, y, 2);
    y += ROW_PITCH;
  };
  const bool manual = session.hasManualStop();
  const bool estimated = details && details->complete;
  const float duration = session.elapsedUs(0) * .000001f;
  snprintf(text, sizeof(text), "%.0f RPM", session.rpm());
  row("Launch", text, c.text);
  snprintf(text, sizeof(text), "%.1f s", duration);
  row(manual ? "Spin time" : "Recorded", text, c.text);
  if (manual && duration > 0)
    snprintf(text, sizeof(text), "%.0f RPM/s", session.rpm() / duration);
  else if (estimated)
    snprintf(text, sizeof(text), "%.0f RPM/s", details->decayRate);
  else
    snprintf(text, sizeof(text), "--");
  row("Decay", text, c.text);
  if (estimated)
    snprintf(text, sizeof(text), "%.0f", details->revolutions);
  else
    snprintf(text, sizeof(text), "--");
  row("Revs", text, c.text);
  // Battle stamina is a model estimate (time to fall below 1,000 RPM): "~".
  if (session.rpm() > 0 && session.rpm() <= 1000)
    snprintf(text, sizeof(text), "0.0 s");
  else if (details && std::isfinite(details->battleStaminaSeconds))
    snprintf(text, sizeof(text), "~%.1f s", details->battleStaminaSeconds);
  else
    snprintf(text, sizeof(text), "--");
  row("Stamina", text, c.accent);
  g.setTextDatum(top_center);
  if (details && details->endpointEstimate) {
    g.setTextColor(c.muted, c.background);
    g.drawString("Estimated curve", cx, NOTE_Y, 1);
  }
  g.setTextColor(c.muted, c.background);
  g.setTextDatum(top_left);
  g.drawString("RPM", 4, TABLE_Y, 1);
  g.setTextDatum(top_right);
  g.drawString("Time", g.width() - 4, TABLE_Y, 1);
  g.drawFastHLine(4, TABLE_Y + 11, g.width() - 8, c.faint);
  unsigned pages = 1;
  if (details && details->count) {
    pages = (details->count + AnalysisDetails::ROWS - 1) / AnalysisDetails::ROWS;
    const unsigned first = session.tablePage() % pages * AnalysisDetails::ROWS;
    // One scale for every page, so bars compare across pages.
    float longest = 0;
    for (unsigned i = 0; i < details->count; ++i)
      longest = std::max(longest, details->seconds[i]);
    for (unsigned i = first; i < std::min(details->count, first + AnalysisDetails::ROWS); ++i) {
      const unsigned band = details->firstBand - i;
      const int by = BAND_Y + int(i - first) * BAND_PITCH;
      char label[16];
      snprintf(label, sizeof(label), "%u-%uK", band, band - 1);
      g.setTextColor(c.text, c.background);
      g.setTextDatum(top_left);
      g.drawString(label, 4, by, 1);
      const int width = longest > 0 ? std::max(1, int(std::lround(details->seconds[i] / longest *
                                                                   (BAR_RIGHT - BAR_LEFT)))) : 0;
      g.fillRect(BAR_LEFT, by + 2, width, BAR_HEIGHT, c.accent);
      snprintf(text, sizeof(text), "%.1f s", details->seconds[i]);
      g.setTextDatum(top_right);
      g.drawString(text, g.width() - 4, by, 1);
    }
  } else {
    g.setTextDatum(top_center);
    g.setTextColor(c.muted, c.background);
    g.drawString(fitting   ? "Fitting curve"
                 : failed  ? "Fit unavailable"
                 : details ? "No complete bands"
                           : "No winning model",
                 cx, BAND_Y + 20, 2);
  }
  ControlsUI::footer(g, pages > 1 ? "A Back  B More" : "A Back");
}
}
