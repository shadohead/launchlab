#pragma once
#include "controls.h"
#include "rpm_estimator.h"
#include "sensor_profile.h"
#include "appearance.h"
#include "mount_orientation.h"
#include "wipe_confirmation.h"

namespace ControlsUI {
static constexpr const char *MENU[]={"History","New session","Battery","Settings","Tournament","Back"};
static constexpr const char *HISTORY[]={"Launches","Pull trend","Session trend","Back"};
static constexpr const char *SETTINGS[]={"Sleep after","RPM method","Brightness","Flip display","Sensor profile","Theme","Best effect","Sound","Mounting","Wipe all","Back"};
// Updated by the UI owner before rendering; warnings/status keep their colors.
static uint16_t accent=TFT_CYAN;
inline void footer(lgfx::LGFXBase &g,const char *label) {
  g.fillRect(0,229,g.width(),11,TFT_BLACK);
  g.setTextDatum(top_center);g.setTextColor(TFT_DARKGREY,TFT_BLACK);
  g.drawString(label,g.width()/2,231,1);
}
inline void heading(lgfx::LGFXBase &g,const char *label) {
  g.setTextDatum(top_center);g.setTextColor(TFT_WHITE,TFT_BLACK);
  g.drawString(label,g.width()/2,8,2);
}
inline void list(lgfx::LGFXBase &g,const char *title,const char *const *labels,unsigned count,unsigned selected) {
  heading(g,title);
  for(unsigned i=0;i<count;++i) {
    const int y=42+int(i)*27;
    if(i==selected)g.drawRoundRect(4,y-4,g.width()-8,25,4,accent);
    g.setTextColor(i==selected?accent:TFT_WHITE,TFT_BLACK);
    g.drawString(labels[i],g.width()/2,y,2);
  }
  footer(g,selected+1==count?"A Back  B Next":"A Open  B Next");
}
inline void value(char *out,unsigned length,unsigned setting,unsigned value) {
  switch(Controls::Setting(setting)) {
    case Controls::Setting::Sleep:snprintf(out,length,"%u min",value);break;
    case Controls::Setting::Rpm:snprintf(out,length,"%s",RpmEstimator::label(RpmEstimator::Mode(value)));break;
    case Controls::Setting::Brightness:snprintf(out,length,"%u%%",value);break;
    case Controls::Setting::Flip:snprintf(out,length,"%s",value?"On":"Off");break;
    case Controls::Setting::Sensor:snprintf(out,length,"%s",StickS3SensorProfile::label(StickS3SensorProfile::Mode(value)));break;
    case Controls::Setting::Theme:snprintf(out,length,"%s",Appearance::label(Appearance::Theme(value)));break;
    case Controls::Setting::BestEffect:snprintf(out,length,"%s",Appearance::label(Appearance::Effect(value)));break;
    case Controls::Setting::Sound:snprintf(out,length,"%s",Appearance::label(Appearance::Sound(value)));break;
    case Controls::Setting::Mount:snprintf(out,length,"%s",MountOrientation::label(MountOrientation::Mode(value)));break;
    case Controls::Setting::WipeAll:snprintf(out,length,"Erase device data");break;
  }
}
inline void settings(lgfx::LGFXBase &g,const Controls::State &state,const uint8_t *values) {
  heading(g,"Settings");char text[24];
  const unsigned first=state.setting<5?0:5,last=first?Controls::SETTING_COUNT:4;
  for(unsigned i=first;i<=last;++i) {
    const int y=33+int(i-first)*31;
    if(i==state.setting)g.drawRoundRect(4,y-3,g.width()-8,30,4,accent);
    g.setTextColor(i==state.setting?accent:TFT_WHITE,TFT_BLACK);
    g.drawString(SETTINGS[i],g.width()/2,y,2);
    if(i<Controls::SETTING_COUNT){value(text,sizeof(text),i,i<Controls::EDITABLE_SETTING_COUNT?values[i]:0);g.drawString(text,g.width()/2,y+18,1);}
  }
  if(!first){g.setTextColor(TFT_DARKGREY,TFT_BLACK);g.drawString("B Next: appearance",g.width()/2,194,1);}
  footer(g,state.setting==unsigned(Controls::Setting::WipeAll)?"A Open  B Next":state.setting<Controls::SETTING_COUNT?"A Edit  B Next":"A Back  B Next");
}
inline void editor(lgfx::LGFXBase &g,const Controls::State &state,bool failed,bool changedMetric) {
  heading(g,SETTINGS[state.setting]);char text[24];value(text,sizeof(text),state.setting,state.draft);
  g.setTextColor(TFT_DARKGREY,TFT_BLACK);g.drawString("Hold B cancels",g.width()/2,35,1);
  // Center the value at full width, with a quiet cycle hint below its card.
  g.drawRoundRect(4,68,g.width()-8,64,5,TFT_DARKGREY);
  const int valueWidth=g.width()-16,valueX=g.width()/2;
  // Palette names share one readable size while cycling between all ten choices.
  const int valueFont=state.setting==unsigned(Controls::Setting::Theme)?2:
    g.textWidth(text,&fonts::Font4)<=valueWidth?4:
    g.textWidth(text,&fonts::Font2)<=valueWidth?2:1;
  g.setTextColor(accent,TFT_BLACK);
  g.drawString(text,valueX,valueFont==4?86:valueFont==2?91:95,valueFont);
  g.setTextColor(0x39e7,TFT_BLACK);g.drawString("B next option ->",g.width()/2,139,1);
  if(changedMetric) {
    g.setTextColor(TFT_WHITE,TFT_BLACK);
    g.drawString("Next pull starts",g.width()/2,154,1);
    g.drawString("a new session",g.width()/2,169,1);
  }
  if(state.setting==unsigned(Controls::Setting::Mount)) {
    g.setTextColor(TFT_WHITE,TFT_BLACK);
    g.drawString("Align level + replay",g.width()/2,154,1);
    g.drawString("to your mount",g.width()/2,169,1);
  }
  if(failed){g.setTextColor(TFT_ORANGE,TFT_BLACK);g.drawString("Could not apply",g.width()/2,196,1);}
  footer(g,"A Apply  B Next");
}
inline void session(lgfx::LGFXBase &g,bool start) {
  heading(g,"New session");g.setTextColor(TFT_WHITE,TFT_BLACK);
  g.drawString("Keep past launches",g.width()/2,42,1);
  const char *labels[]={"Cancel","Start"};
  for(unsigned i=0;i<2;++i) {
    const int y=91+int(i)*42;
    if(bool(i)==start)g.drawRoundRect(12,y-5,g.width()-24,32,4,accent);
    g.setTextColor(bool(i)==start?accent:TFT_WHITE,TFT_BLACK);
    g.drawString(labels[i],g.width()/2,y,2);
  }
  footer(g,"A Choose  B Next");
}
inline void hold(lgfx::LGFXBase &g,const char *label,unsigned elapsed,unsigned duration) {
  footer(g,label);
  g.fillRect(4,225,int((g.width()-8)*std::min(elapsed,duration)/duration),2,accent);
}
inline void wipe(lgfx::LGFXBase &g,const WipeConfirmation &flow,uint32_t now,bool failed,bool blocked,bool complete) {
  heading(g,"Wipe all");
  if(complete) {
    g.setTextColor(TFT_WHITE,TFT_BLACK);g.drawString("Data erased",g.width()/2,78,2);
    g.drawString("Release buttons",g.width()/2,123,1);
    g.drawString("to restart",g.width()/2,141,1);
    footer(g,"Restart after release");return;
  }
  if(blocked && !flow.active()) {
    g.setTextColor(TFT_ORANGE,TFT_BLACK);g.drawString("Wipe incomplete",g.width()/2,72,2);
    g.setTextColor(TFT_WHITE,TFT_BLACK);g.drawString("Review and retry",g.width()/2,113,1);
    g.drawString("to finish wiping",g.width()/2,131,1);
    footer(g,"A Review  B Restart");return;
  }
  char step[16];snprintf(step,sizeof(step),"Step %u of 3",unsigned(flow.stage())+1);
  g.setTextColor(TFT_DARKGREY,TFT_BLACK);g.drawString(step,g.width()/2,35,1);
  g.setTextColor(TFT_WHITE,TFT_BLACK);
  switch(flow.stage()) {
    case WipeConfirmation::Stage::Warning:
      g.drawString("Erase this device?",g.width()/2,63,1);
      g.drawString("All launches",g.width()/2,93,2);
      g.drawString("Personal bests",g.width()/2,116,2);
      g.drawString("Tilt recordings",g.width()/2,139,2);
      g.drawString("All settings",g.width()/2,162,2);
      footer(g,"A Continue  B Cancel");break;
    case WipeConfirmation::Stage::Confirm:
      g.drawString("This can't be undone.",g.width()/2,69,1);
      g.drawString("Back up first",g.width()/2,111,2);
      g.drawString("Erase everything?",g.width()/2,148,1);
      footer(g,"A Confirm  B Cancel");break;
    case WipeConfirmation::Stage::Armed:
      g.drawString("Hold A for 3 sec",g.width()/2,77,2);
      g.drawString(flow.awaitingRelease()?"Release buttons":"Release to stop",g.width()/2,115,1);
      g.drawString("B cancels",g.width()/2,133,1);
      g.drawRoundRect(14,155,g.width()-28,17,3,TFT_DARKGREY);
      g.fillRect(16,157,int((g.width()-32)*flow.elapsed(now)/WipeConfirmation::HOLD_MS),13,TFT_ORANGE);
      footer(g,"Hold A  B Cancel");break;
  }
  if(failed){g.setTextColor(TFT_ORANGE,TFT_BLACK);g.drawString(blocked?"Wipe needs retry":"Try again when idle",g.width()/2,199,1);}
}
}
