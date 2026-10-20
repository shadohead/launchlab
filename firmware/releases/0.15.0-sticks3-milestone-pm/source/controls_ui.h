#pragma once
#include "controls.h"
#include "rpm_estimator.h"
#include "sensor_profile.h"
#include "appearance.h"
#include "milestones.h"
#include "palette.h"
#include "theme_library.h"
#include "mount_orientation.h"
#include "wipe_confirmation.h"

namespace ControlsUI {
static constexpr const char *MENU[]={"History","New session","Battery","Settings","Tournament","Back"};
static constexpr const char *HISTORY[]={"Launches","Pull trend","Session trend","Back"};
static constexpr const char *SETTINGS[]={"Sleep after","RPM method","Brightness","Flip display","Sensor profile","Theme","Best effect","Celebrate","Sound","Mounting","Wipe all","Back"};
// Set by the UI owner before rendering: the visible theme's colors, the
// names the Theme row shows (custom themes; the built-in names otherwise)
// and the milestone table the Celebrate editor summarizes.
static Palette::Colors colors=Themes::builtIn(Themes::FALLBACK);
static const char *(*themeLabel)(Appearance::Theme)=nullptr;
static const Milestones::Config *milestones=nullptr;
inline void footer(lgfx::LGFXBase &g,const char *label) {
  g.fillRect(0,229,g.width(),11,colors.background);
  g.setTextDatum(top_center);g.setTextColor(colors.muted,colors.background);
  g.drawString(label,g.width()/2,231,1);
}
inline void heading(lgfx::LGFXBase &g,const char *label) {
  g.setTextDatum(top_center);g.setTextColor(colors.text,colors.background);
  g.drawString(label,g.width()/2,8,2);
}
inline void list(lgfx::LGFXBase &g,const char *title,const char *const *labels,unsigned count,unsigned selected) {
  heading(g,title);
  for(unsigned i=0;i<count;++i) {
    const int y=42+int(i)*27;
    if(i==selected)g.drawRoundRect(4,y-4,g.width()-8,25,4,colors.accent);
    g.setTextColor(i==selected?colors.accent:colors.text,colors.background);
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
    case Controls::Setting::Theme:snprintf(out,length,"%s",themeLabel?themeLabel(Appearance::Theme(value)):Appearance::label(Appearance::Theme(value)));break;
    case Controls::Setting::BestEffect:snprintf(out,length,"%s",Appearance::label(Appearance::Effect(value)));break;
    case Controls::Setting::Celebrate:snprintf(out,length,"%s",Milestones::label(Milestones::Mode(value)));break;
    case Controls::Setting::Sound:snprintf(out,length,"%s",Appearance::label(Appearance::Sound(value)));break;
    case Controls::Setting::Mount:snprintf(out,length,"%s",MountOrientation::label(MountOrientation::Mode(value)));break;
    case Controls::Setting::WipeAll:snprintf(out,length,"Erase device data");break;
  }
}
inline void settings(lgfx::LGFXBase &g,const Controls::State &state,const uint8_t *values) {
  heading(g,"Settings");char text[24];
  // Pages of five rows: general, appearance, then Wipe all and Back.
  const unsigned first=state.setting<5?0:state.setting<10?5:10,last=std::min(first+4,Controls::SETTING_COUNT);
  for(unsigned i=first;i<=last;++i) {
    const int y=33+int(i-first)*31;
    if(i==state.setting)g.drawRoundRect(4,y-3,g.width()-8,30,4,colors.accent);
    g.setTextColor(i==state.setting?colors.accent:colors.text,colors.background);
    g.drawString(SETTINGS[i],g.width()/2,y,2);
    if(i<Controls::SETTING_COUNT){value(text,sizeof(text),i,i<Controls::EDITABLE_SETTING_COUNT?values[i]:0);g.drawString(text,g.width()/2,y+18,1);}
  }
  if(first<10){g.setTextColor(colors.muted,colors.background);g.drawString(first?"B Next: wipe all":"B Next: appearance",g.width()/2,194,1);}
  footer(g,state.setting==unsigned(Controls::Setting::WipeAll)?"A Open  B Next":state.setting<Controls::SETTING_COUNT?"A Edit  B Next":"A Back  B Next");
}
inline void editor(lgfx::LGFXBase &g,const Controls::State &state,bool failed,bool changedMetric) {
  heading(g,SETTINGS[state.setting]);char text[24];value(text,sizeof(text),state.setting,state.draft);
  g.setTextColor(colors.muted,colors.background);g.drawString("Hold B cancels",g.width()/2,35,1);
  // Center the value at full width, with a quiet cycle hint below its card.
  g.drawRoundRect(4,68,g.width()-8,64,5,colors.muted);
  const int valueWidth=g.width()-16,valueX=g.width()/2;
  // Theme names share one readable size while cycling; a custom name too
  // wide for it drops to the small font.
  const int valueFont=state.setting==unsigned(Controls::Setting::Theme)?(g.textWidth(text,&fonts::Font2)<=valueWidth?2:1):
    g.textWidth(text,&fonts::Font4)<=valueWidth?4:
    g.textWidth(text,&fonts::Font2)<=valueWidth?2:1;
  g.setTextColor(colors.accent,colors.background);
  g.drawString(text,valueX,valueFont==4?86:valueFont==2?91:95,valueFont);
  g.setTextColor(colors.faint,colors.background);g.drawString("B next option ->",g.width()/2,139,1);
  if(changedMetric) {
    g.setTextColor(colors.text,colors.background);
    g.drawString("Next pull starts",g.width()/2,154,1);
    g.drawString("a new session",g.width()/2,169,1);
  }
  if(state.setting==unsigned(Controls::Setting::Celebrate)) {
    // What the choice celebrates; the milestone table is edited on the web.
    const auto mode=Milestones::Mode(state.draft);char line[28];
    g.setTextColor(colors.text,colors.background);
    if(!Milestones::milestones(mode) || !milestones){g.drawString("Each new personal",g.width()/2,154,1);g.drawString("best celebrates",g.width()/2,169,1);}
    else {
      const auto &m=*milestones;char low[12],high[12];
      Milestones::rpmText(low,sizeof(low),m.row(0).rpm);Milestones::rpmText(high,sizeof(high),m.row(m.count()-1).rpm);
      if(m.count()==1)snprintf(line,sizeof(line),"1 milestone, %s",low);
      else snprintf(line,sizeof(line),"%u steps %s to %s",m.count(),low,high);
      g.drawString(line,g.width()/2,154,1);
      g.drawString(mode==Milestones::Mode::Both?"and new bests":"Edit table online",g.width()/2,169,1);
    }
  }
  if(state.setting==unsigned(Controls::Setting::Mount)) {
    g.setTextColor(colors.text,colors.background);
    g.drawString("Align level + replay",g.width()/2,154,1);
    g.drawString("to your mount",g.width()/2,169,1);
  }
  if(failed){g.setTextColor(colors.warning,colors.background);g.drawString("Could not apply",g.width()/2,196,1);}
  footer(g,"A Apply  B Next");
}
inline void session(lgfx::LGFXBase &g,bool start) {
  heading(g,"New session");g.setTextColor(colors.text,colors.background);
  g.drawString("Keep past launches",g.width()/2,42,1);
  const char *labels[]={"Cancel","Start"};
  for(unsigned i=0;i<2;++i) {
    const int y=91+int(i)*42;
    if(bool(i)==start)g.drawRoundRect(12,y-5,g.width()-24,32,4,colors.accent);
    g.setTextColor(bool(i)==start?colors.accent:colors.text,colors.background);
    g.drawString(labels[i],g.width()/2,y,2);
  }
  footer(g,"A Choose  B Next");
}
inline void hold(lgfx::LGFXBase &g,const char *label,unsigned elapsed,unsigned duration) {
  footer(g,label);
  g.fillRect(4,225,int((g.width()-8)*std::min(elapsed,duration)/duration),2,colors.accent);
}
inline void battery(lgfx::LGFXBase &g,bool valid,int percent,unsigned millivolts,bool chargeKnown,bool charging) {
  const int cx=g.width()/2;
  g.setTextDatum(top_center);g.setTextColor(colors.text,colors.background);
  g.drawString("Battery",cx,8,2);
  char line[32];
  if(valid)snprintf(line,sizeof(line),"%d%%",percent);else snprintf(line,sizeof(line),"--");
  const bool low=valid && percent<=20;
  g.setTextColor(low?colors.warning:colors.accent,colors.background);
  g.drawString(line,cx,43,4);
  g.drawRoundRect(29,96,74,25,3,colors.muted);g.fillRect(103,103,4,11,colors.muted);
  if(valid)g.fillRect(33,100,66*percent/100,17,low?colors.warning:colors.accent);
  g.setTextColor(colors.text,colors.background);
  if(valid)snprintf(line,sizeof(line),"%.2f V",millivolts/1000.0);else snprintf(line,sizeof(line),"Reading unavailable");
  g.drawString(line,cx,133,2);
  g.drawString(!chargeKnown?"Charge status unknown":charging?"Charging":"Not charging",cx,161,1);
  g.setTextColor(colors.muted,colors.background);
  footer(g,"A Done");
}
inline void wipe(lgfx::LGFXBase &g,const WipeConfirmation &flow,uint32_t now,bool failed,bool blocked,bool complete) {
  heading(g,"Wipe all");
  if(complete) {
    g.setTextColor(colors.text,colors.background);g.drawString("Data erased",g.width()/2,78,2);
    g.drawString("Release buttons",g.width()/2,123,1);
    g.drawString("to restart",g.width()/2,141,1);
    footer(g,"Restart after release");return;
  }
  if(blocked && !flow.active()) {
    g.setTextColor(colors.warning,colors.background);g.drawString("Wipe incomplete",g.width()/2,72,2);
    g.setTextColor(colors.text,colors.background);g.drawString("Review and retry",g.width()/2,113,1);
    g.drawString("to finish wiping",g.width()/2,131,1);
    footer(g,"A Review  B Restart");return;
  }
  char step[16];snprintf(step,sizeof(step),"Step %u of 3",unsigned(flow.stage())+1);
  g.setTextColor(colors.muted,colors.background);g.drawString(step,g.width()/2,35,1);
  g.setTextColor(colors.text,colors.background);
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
      g.drawRoundRect(14,155,g.width()-28,17,3,colors.muted);
      g.fillRect(16,157,int((g.width()-32)*flow.elapsed(now)/WipeConfirmation::HOLD_MS),13,colors.warning);
      footer(g,"Hold A  B Cancel");break;
  }
  if(failed){g.setTextColor(colors.warning,colors.background);g.drawString(blocked?"Wipe needs retry":"Try again when idle",g.width()/2,199,1);}
}
}
