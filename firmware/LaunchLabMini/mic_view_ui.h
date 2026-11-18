#pragma once
#include <cstdio>
#include "controls_ui.h"
#include "mic_trace.h"
#include "motion_ui.h"
#include "palette.h"

// RPM heard over time, as the apps' spin trace, so the decline shows: the
// pieces of Analysis's graph (analysis_ui.h; through 0.20.11 the Mic RPM page
// drew them alone). Time starts at the launch. The optical peak is a separate
// marker at time zero and the shaded band is the time nothing was heard: the
// line is sound alone, starts where the sound starts, and never joins the marker.
namespace MicView {
enum class Mic:uint8_t {Starting,On,Failed,NoMemory};
static constexpr int LEFT=26,RIGHT=130,TOP=72,BOTTOM=172;
inline const char *withheld(MicRpm::Reason r) {
  switch(r) {
    case MicRpm::Reason::Quiet:return "quiet";
    case MicRpm::Reason::Clipping:return "too loud";
    case MicRpm::Reason::BroadbandNoise:return "noise";
    case MicRpm::Reason::Acquiring:return "locking on";
    default:return "no clear tone";
  }
}
// The optical peak's mark: a hollow diamond, unlike anything on the mic's line.
inline void marker(lgfx::LGFXBase &g,int x,int y,const Palette::Colors &c) {
  g.fillTriangle(x-3,y,x,y-3,x+3,y,c.number);g.fillTriangle(x-3,y,x,y+3,x+3,y,c.number);
  g.drawPixel(x,y,c.background);
}
template<class Y>
inline void axes(lgfx::LGFXBase &g,const Palette::Colors &c,int left,int right,int bottom,float span,float top,Y y) {
  char text[32];
  const int width=right-left;
  g.setTextDatum(top_right);
  for(unsigned i=1;i<=3;++i) {
    const float rpm=top*float(i)/3;const int ly=y(rpm);
    for(int dx=left;dx<=right;dx+=4)g.drawFastHLine(dx,ly,2,c.faint);
    MotionUI::thousands(text,sizeof(text),rpm);g.drawString(text,left-3,ly-3,1);
  }
  g.drawFastHLine(left,bottom,width+1,c.muted);
  g.drawString("0",left-3,bottom-3,1);
  g.setTextDatum(top_left);g.drawString("0s",left,bottom+4,1);
  snprintf(text,sizeof(text),"%.0fs",span/2);g.setTextDatum(top_center);g.drawString(text,left+width/2,bottom+4,1);
  snprintf(text,sizeof(text),"%.0fs",span);g.setTextDatum(top_right);g.drawString(text,right,bottom+4,1);
}
inline void plot(lgfx::LGFXBase &g,const MicTrace::Trace &t,const Palette::Colors &c,
                 int LEFT=26,int RIGHT=130,int TOP=72,int BOTTOM=172,float elapsedSeconds=-1,bool showRejected=false) {
  using namespace MicTrace;
  // An explicit clock span (Analysis) advances even without readings. The standalone page keeps rounded axes.
  const float span=elapsedSeconds>=0?std::max(MIN_SPAN_S,std::max(t.elapsed(),elapsedSeconds)):spanSeconds(t.elapsed());
  const float top=topRpm(std::max(std::max(t.peakRpm(),t.opticalRpm()),showRejected?t.rejectedPeakRpm():uint16_t(0)));
  const int width=RIGHT-LEFT;
  auto y=[&](float rpm){return BOTTOM-int(lroundf(rpm/top*float(BOTTOM-TOP)));};
  auto x=[&](float seconds){return LEFT+int(lroundf(seconds/span*float(width)));};
  // Nothing was heard from the launch to here.
  if(t.started()) {
    const int band=x(t.heard()?t.firstSeconds():t.elapsed())-LEFT;
    if(band>0)g.fillRect(LEFT,TOP,band,BOTTOM-TOP,c.scenePlane);
  }
  axes(g,c,LEFT,RIGHT,BOTTOM,span,top,y);

  int px=0,py=0;
  columns(t,width,span,[&](int column,float rpm,bool join) {
    const int qx=LEFT+column,qy=y(rpm);
    if(join)g.drawLine(px,py,qx,qy,c.accent);else g.fillCircle(qx,qy,1,c.accent);
    px=qx;py=qy;
  });
  if(t.heard())g.fillCircle(px,py,2,c.accent);
  if(t.opticalRpm())marker(g,LEFT,y(t.opticalRpm()),c);
  // Raw measurements withheld by the Analysis filter, never connected to the curve.
  if(showRejected && t.rawCount()>t.heard())for(unsigned i=0;i<t.count();++i)
    if(t.rejectedRpm(i))g.fillCircle(x(Trace::seconds(i)),y(t.rejectedRpm(i)),1,Palette::DARKGREY);

}
}
