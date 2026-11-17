#pragma once
#include <cmath>
#include <cstdio>
#include "level_indicator.h"
#include "palette.h"

// The main screen, the level gauge, tournament and the signal diagnostics
// (menus and Battery are in controls_ui.h): pure drawing from values the sketch reads, so the host
// renderer and the web theme preview draw exactly what the device does.
namespace ScreensUI {
// How the status line reads: plain text, the detector ready, or a problem.
enum class Tone:uint8_t {Text,Ready,Warning};
inline uint16_t tone(const Palette::Colors &c,Tone t){return t==Tone::Ready?c.ready:t==Tone::Warning?c.warning:c.text;}

// The detector state above the signal readout, after "New session ready" for
// a moment after a new session.
inline void diagnosticsHeader(lgfx::LGFXBase &g,const Palette::Colors &c,bool sessionNotice,const char *state,bool ready) {
  const int cx=g.width()/2;
  if(sessionNotice)g.drawString("New session ready",cx,8,2);
  g.setTextColor(ready?c.ready:c.warning,c.background);
  g.drawString(state,cx,32,2);
}
inline void diagnostics(lgfx::LGFXBase &g,unsigned pin,uint32_t last,uint32_t minimum,uint32_t maximum,float rate) {
  g.setTextDatum(top_left);
  char line[64];
  snprintf(line,sizeof(line),"AO G%u: %lu",pin,(unsigned long)last);
  g.drawString(line,8,64,2);
  snprintf(line,sizeof(line),"Range %lu-%lu",(unsigned long)minimum,(unsigned long)maximum);
  g.drawString(line,8,93,2);
  snprintf(line,sizeof(line),"%.0f samples/s",rate);
  g.drawString(line,8,122,2);
}
// A static card keeps launch results out of the competitor's view.
inline void tournament(lgfx::LGFXBase &g,const Palette::Colors &c,bool sensorOk,bool recordingOnly,bool valid,float rpm) {
  const int cx=g.width()/2;
  if(!sensorOk) {
    g.setTextColor(c.warning,c.background);
    g.drawString("Check sensor",cx,92,2);
  } else if(recordingOnly) {
    g.setTextColor(c.alert,c.background);
    g.drawString("RECORDING",cx,84,2);
    g.drawString("ONLY",cx,108,4);
  } else {
    char text[16];
    if(valid)snprintf(text,sizeof(text),"%.0f",rpm);
    else snprintf(text,sizeof(text),"--");
    const int font=g.textWidth(text,&fonts::Font6)<=g.width()-8?6:4;
    g.drawString(text,cx,86,font);
    g.drawString("RPM",cx,137,2);
  }
}
// The status line and the last result's RPM.
inline void live(lgfx::LGFXBase &g,const Palette::Colors &c,const char *status,Tone statusTone,bool valid,float rpm) {
  const int cx=g.width()/2;
  g.setTextColor(tone(c,statusTone),c.background);
  g.drawString(status,cx,8,2);
  char text[16];
  if(valid)snprintf(text,sizeof(text),"%.0f",rpm);
  else snprintf(text,sizeof(text),"--");
  g.setTextDatum(top_center);
  // Keep large values inside the narrow portrait width as well.
  const int font=g.textWidth(text,&fonts::Font6)<=g.width()-8?6:4;
  g.setTextColor(c.number,c.background);
  g.drawString(text,cx,47,font);
  g.drawString("RPM",cx,99,2);
}
// The live level gauge under the RPM, in display coordinates.
inline void level(lgfx::LGFXBase &g,const Palette::Colors &c,int cx,const StickS3Level::Reading &drawn) {
  const int cy=165,radius=38;
  g.setTextColor(c.muted,c.background);
  g.drawCircle(cx,cy,radius,c.muted);
  g.drawFastHLine(cx-radius+3,cy,2*radius-5,c.muted);
  g.drawFastVLine(cx,cy-radius+3,2*radius-5,c.muted);
  g.drawCircle(cx,cy,6,c.muted);
  if(drawn.valid()) {
    const auto color=drawn.degrees<=2?c.accent:c.warning;
    g.fillCircle(cx+lroundf(drawn.right*32),cy+lroundf(drawn.down*32),5,color);
    char angle[20];snprintf(angle,sizeof(angle),"%.1f deg",drawn.degrees);
    g.setTextColor(color,c.background);g.drawString(angle,cx,207,2);
  } else {
    g.drawString("--",cx,207,2);
  }
}
}
