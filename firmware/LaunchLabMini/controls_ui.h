#pragma once
#include <cstring>
#include "controls.h"
#include "rpm_estimator.h"
#include "sensor_profile.h"
#include "appearance.h"
#include "milestones.h"
#include "palette.h"
#include "theme_library.h"
#include "mount_orientation.h"
#include "wipe_confirmation.h"
#include "ui_text.h"
#include "ui_draw.h"

namespace ControlsUI {
using UiText::Text;
inline const char *tr(Text key){return UiText::t(key);}
static constexpr Text MENU[]={Text::MenuHistory,Text::MenuNewSession,Text::MenuTournament,Text::MenuBattles,Text::MenuWearCheck,Text::MenuAnalysis,
  Text::MenuDevice,Text::MenuBluetooth,Text::MenuSettings,Text::Back};
static_assert(sizeof(MENU)/sizeof(MENU[0])==Controls::MENU_COUNT,"one label per menu row");
static constexpr Text HISTORY[]={Text::HistoryLaunches,Text::HistoryPullTrend,Text::HistorySessionTrend,Text::Back};
// By Controls::Setting value; the list shows them in Controls::ORDER.
static constexpr Text SETTINGS[]={Text::SettingSleep,Text::SettingRpm,Text::SettingBrightness,Text::SettingFlip,Text::SettingSensor,Text::SettingTheme,
  Text::SettingBestEffect,Text::SettingCelebrate,Text::SettingSound,Text::SettingMount,Text::SettingShakeWake,Text::SettingWipeAll,Text::SettingAbout,
  Text::SettingBatteryIcon,Text::SettingBluetooth,Text::SettingMinRpm,Text::SettingLanguage,Text::Back};
static_assert(Controls::LANGUAGES==UiText::LANGS,"Language cycles through every language");
static_assert(sizeof(SETTINGS)/sizeof(SETTINGS[0])==Controls::SETTING_COUNT+1,"one label per setting, then Back");
// Set by the UI owner before rendering: the visible theme's colors, the
// names the Theme row shows (custom themes; the built-in names otherwise),
// the milestone table the Celebrate editor summarizes and the firmware's
// runtime ("0.20.8-sticks3-songs-pm"), whose version the About row shows.
static Palette::Colors colors=Themes::builtIn(Themes::FALLBACK);
static const char *(*themeLabel)(Appearance::Theme)=nullptr;
static const Milestones::Config *milestones=nullptr;
static const char *runtime="";
// The release number alone: the runtime up to its build name.
inline int versionLength(const char *text){return int(strcspn(text,"-"));}
// Text in the screen's language (ui_draw.h); the tests read these.
using UiDraw::fallbacks;using UiDraw::lastOverflow;
inline void drawText(lgfx::LGFXBase &g,const char *text,int x,int y,int font,int room=0){UiDraw::text(g,text,x,y,font,room);}
inline void footer(lgfx::LGFXBase &g,const char *label) {
  g.fillRect(0,229,g.width(),11,colors.background);
  g.setTextDatum(top_center);g.setTextColor(colors.muted,colors.background);
  drawText(g,label,g.width()/2,231,1,g.width());
}
inline void heading(lgfx::LGFXBase &g,const char *label) {
  g.setTextDatum(top_center);g.setTextColor(colors.text,colors.background);
  drawText(g,label,g.width()/2,8,2);
}
// `groups` (bit i: row i starts a group) puts a little space above those rows.
inline void list(lgfx::LGFXBase &g,const char *title,const char *const *labels,unsigned count,unsigned selected,unsigned groups=0) {
  heading(g,title);
  int space=0;
  // Eight rows close up to clear the footer; nine, in groups, further, and
  // the Menu's ten (with Battles) to a row for each line of text.
  const bool ten=count>9;
  for(unsigned i=0;i<count;++i) {
    if((groups>>i)&1u)space+=ten?3:5;
    const int pitch=ten?19:count>8?20:count>7?23:27,y=(ten?28:count>8?30:42)+int(i)*pitch+space;
    if(i==selected)g.drawRoundRect(4,y-(count>8?2:4),g.width()-8,ten?19:count>8?20:25,4,colors.accent);
    g.setTextColor(i==selected?colors.accent:colors.text,colors.background);
    drawText(g,labels[i],g.width()/2,y,2);
  }
  footer(g,tr(selected+1==count?Text::FooterBackNext:Text::FooterOpenNext));
}
// The same list from text keys (MENU, HISTORY), in the screen's language.
inline void list(lgfx::LGFXBase &g,const char *title,const Text *keys,unsigned count,unsigned selected,unsigned groups=0) {
  const char *labels[Controls::MENU_COUNT];
  if(count>Controls::MENU_COUNT)count=Controls::MENU_COUNT;
  for(unsigned i=0;i<count;++i)labels[i]=tr(keys[i]);
  list(g,title,labels,count,selected,groups);
}
// A setting's value as text, in `lang`. The screen shows it in the screen's
// language (value()); the Bluetooth options (opcode 7) always carry English
// (protocolValue()), so a language never changes the protocol.
inline void value(char *out,unsigned length,unsigned setting,unsigned value,UiText::Lang lang) {
  auto say=[lang](Text key){return UiText::t(key,lang);};
  auto onOff=[&](unsigned v){return say(v?Text::On:Text::Off);};
  switch(Controls::Setting(setting)) {
    case Controls::Setting::Sleep:snprintf(out,length,say(Text::ValueMinutes),value);break;
    case Controls::Setting::Rpm:snprintf(out,length,"%s",say(RpmEstimator::Mode(value)==RpmEstimator::Mode::ThreeTurn?Text::ValueRpmThreeTurn:Text::ValueRpmOneTurn));break;
    case Controls::Setting::Brightness:snprintf(out,length,"%u%%",value);break;
    case Controls::Setting::Flip:snprintf(out,length,"%s",onOff(value));break;
    case Controls::Setting::Sensor:snprintf(out,length,"%s",say(StickS3SensorProfile::Mode(value)==StickS3SensorProfile::Mode::Tcrt?Text::ValueSensorTcrt:Text::ValueSensorStandard));break;
    case Controls::Setting::Theme:snprintf(out,length,"%s",themeLabel?themeLabel(Appearance::Theme(value)):Appearance::label(Appearance::Theme(value)));break;
    case Controls::Setting::BestEffect: {
      const auto effect=Appearance::Effect(value);
      snprintf(out,length,"%s",effect==Appearance::Effect::Off?say(Text::Off):effect==Appearance::Effect::Random?say(Text::ValueRandom):Appearance::label(effect));
      break;
    }
    case Controls::Setting::Celebrate: {
      const auto mode=Milestones::Mode(value);
      snprintf(out,length,"%s",say(mode==Milestones::Mode::Milestones?Text::ValueCelebrateMilestones:mode==Milestones::Mode::Both?Text::ValueCelebrateBoth:Text::ValueCelebrateBest));
      break;
    }
    case Controls::Setting::Sound: {
      const auto sound=Appearance::Sound(value);
      snprintf(out,length,"%s",say(sound==Appearance::Sound::Low?Text::ValueSoundLow:sound==Appearance::Sound::High?Text::ValueSoundHigh:Text::Off));
      break;
    }
    case Controls::Setting::Mount:snprintf(out,length,"%s",say(MountOrientation::Mode(value)==MountOrientation::Mode::Sidecar?Text::ValueMountSidecar:Text::ValueMountTop));break;
    case Controls::Setting::ShakeWake:snprintf(out,length,"%s",onOff(value));break;
    case Controls::Setting::Bluetooth:snprintf(out,length,"%s",onOff(value));break;
    case Controls::Setting::WipeAll:snprintf(out,length,"%s",say(Text::ValueWipeAll));break;
    case Controls::Setting::About:snprintf(out,length,say(Text::ValueFirmware),versionLength(runtime),runtime);break;
    case Controls::Setting::BatteryIcon:snprintf(out,length,"%s",onOff(value));break;
    case Controls::Setting::MinRpm:MinRpm::label(out,length,uint8_t(value));break;
    // Each language by its own name, in every language.
    case Controls::Setting::Language:snprintf(out,length,"%s",UiText::name(UiText::Lang(value)));break;
  }
}
inline void value(char *out,unsigned length,unsigned setting,unsigned v){value(out,length,setting,v,UiText::current());}
inline void protocolValue(char *out,unsigned length,unsigned setting,unsigned v){value(out,length,setting,v,UiText::Lang::English);}
inline void settings(lgfx::LGFXBase &g,const Controls::State &state,const uint8_t *values) {
  heading(g,tr(Text::MenuSettings));char text[24];
  // Pages of five rows (Controls::ORDER): general, appearance, then the
  // unit's own seven: Language, Mounting, Flip display, Battery icon, Wipe
  // all, About and Back, in the space the "B Next" hint has on the others
  // and 28 px apart, not 31.
  using Controls::PAGE_ROWS;using Controls::LAST_PAGE;
  const unsigned at=Controls::position(state.setting),first=at<LAST_PAGE?at/PAGE_ROWS*PAGE_ROWS:LAST_PAGE,
    last=first<LAST_PAGE?first+PAGE_ROWS-1:Controls::ROWS;
  for(unsigned p=first;p<=last;++p) {
    const unsigned i=Controls::rowAt(p);
    const int y=33+int(p-first)*(last-first<PAGE_ROWS?31:28);
    if(i==state.setting)g.drawRoundRect(4,y-3,g.width()-8,30,4,colors.accent);
    g.setTextColor(i==state.setting?colors.accent:colors.text,colors.background);
    drawText(g,tr(SETTINGS[i]),g.width()/2,y,2);
    if(i<Controls::SETTING_COUNT){value(text,sizeof(text),i,values[i]);drawText(g,text,g.width()/2,y+18,1);}
  }
  if(first<LAST_PAGE){g.setTextColor(colors.muted,colors.background);drawText(g,tr(first?Text::SettingsNextMounting:Text::SettingsNextAppearance),g.width()/2,194,1);}
  const bool opens=state.setting==unsigned(Controls::Setting::WipeAll) || state.setting==unsigned(Controls::Setting::About);
  footer(g,tr(opens?Text::FooterOpenNext:state.setting<Controls::SETTING_COUNT?Text::FooterEditNext:Text::FooterBackNext));
}
inline void editor(lgfx::LGFXBase &g,const Controls::State &state,bool failed,bool changedMetric) {
  heading(g,tr(SETTINGS[state.setting]));char text[24];value(text,sizeof(text),state.setting,state.draft);
  g.setTextColor(colors.muted,colors.background);drawText(g,tr(Text::EditHoldCancels),g.width()/2,35,1);
  // Center the value at full width, with a quiet cycle hint below its card.
  g.drawRoundRect(4,68,g.width()-8,64,5,colors.muted);
  const int valueWidth=g.width()-16,valueX=g.width()/2;
  // Theme names share one readable size while cycling; a custom name too
  // wide for it drops to the small font.
  const int valueFont=state.setting==unsigned(Controls::Setting::Theme)?(g.textWidth(text,&fonts::Font2)<=valueWidth?2:1):
    g.textWidth(text,&fonts::Font4)<=valueWidth?4:
    g.textWidth(text,&fonts::Font2)<=valueWidth?2:1;
  g.setTextColor(colors.accent,colors.background);
  drawText(g,text,valueX,valueFont==4?86:valueFont==2?91:95,valueFont,valueWidth);
  g.setTextColor(colors.faint,colors.background);drawText(g,tr(Text::EditNextOption),g.width()/2,139,1);
  if(changedMetric) {
    g.setTextColor(colors.text,colors.background);
    drawText(g,tr(Text::EditNewSession1),g.width()/2,154,1);
    drawText(g,tr(Text::EditNewSession2),g.width()/2,169,1);
  }
  if(state.setting==unsigned(Controls::Setting::Celebrate)) {
    // What the choice celebrates; the milestone table is edited on the web.
    const auto mode=Milestones::Mode(state.draft);char line[28];
    g.setTextColor(colors.text,colors.background);
    if(!Milestones::milestones(mode) || !milestones){drawText(g,tr(Text::EditBest1),g.width()/2,154,1);drawText(g,tr(Text::EditBest2),g.width()/2,169,1);}
    else {
      const auto &m=*milestones;char low[12],high[12];
      Milestones::rpmText(low,sizeof(low),m.row(0).rpm);Milestones::rpmText(high,sizeof(high),m.row(m.count()-1).rpm);
      if(m.count()==1)snprintf(line,sizeof(line),tr(Text::EditOneMilestone),low);
      else snprintf(line,sizeof(line),tr(Text::EditSteps),m.count(),low,high);
      drawText(g,line,g.width()/2,154,1);
      drawText(g,tr(mode==Milestones::Mode::Both?Text::EditAndBests:Text::EditTableOnline),g.width()/2,169,1);
    }
  }
  if(state.setting==unsigned(Controls::Setting::MinRpm)) {
    g.setTextColor(colors.text,colors.background);
    drawText(g,tr(Text::EditMinRpm1),g.width()/2,154,1);
    drawText(g,tr(Text::EditMinRpm2),g.width()/2,169,1);
  }
  if(state.setting==unsigned(Controls::Setting::Mount)) {
    g.setTextColor(colors.text,colors.background);
    drawText(g,tr(Text::EditMount1),g.width()/2,154,1);
    drawText(g,tr(Text::EditMount2),g.width()/2,169,1);
  }
  if(failed){g.setTextColor(colors.warning,colors.background);drawText(g,tr(Text::EditFailed),g.width()/2,196,1);}
  footer(g,tr(Text::FooterApplyNext));
}
inline void session(lgfx::LGFXBase &g,bool start) {
  heading(g,tr(Text::MenuNewSession));g.setTextColor(colors.text,colors.background);
  drawText(g,tr(Text::SessionKeep),g.width()/2,42,1);
  const char *labels[]={tr(Text::Cancel),tr(Text::SessionStart)};
  for(unsigned i=0;i<2;++i) {
    const int y=91+int(i)*42;
    if(bool(i)==start)g.drawRoundRect(12,y-5,g.width()-24,32,4,colors.accent);
    g.setTextColor(bool(i)==start?colors.accent:colors.text,colors.background);
    drawText(g,labels[i],g.width()/2,y,2);
  }
  footer(g,tr(Text::FooterChooseNext));
}
// Saving, then restarting to apply Bluetooth.
inline void restarting(lgfx::LGFXBase &g) {
  g.fillRoundRect(6,90,g.width()-12,40,5,colors.background);
  g.drawRoundRect(6,90,g.width()-12,40,5,colors.warning);
  g.setTextColor(colors.warning,colors.background);
  drawText(g,tr(Text::Restarting),g.width()/2,103,1);
}
// The Bluetooth page: On or Off (A changes it, which restarts the stick), the
// name the stick appears under, which is the Battle Pass's so that its apps
// find it (on two lines if longer than the screen is wide), and Forget phones.
inline void bluetoothPage(lgfx::LGFXBase &g,unsigned row,bool on,bool connected,const char *name,int bonds) {
  heading(g,tr(Text::MenuBluetooth));
  const int cx=g.width()/2;
  auto choice=[&](unsigned i,int y,int height,const char *label,const char *detail) {
    if(i==row)g.drawRoundRect(4,y-3,g.width()-8,height,4,colors.accent);
    g.setTextColor(i==row?colors.accent:colors.text,colors.background);
    drawText(g,label,cx,y,2);
    if(detail)drawText(g,detail,cx,y+18,1);
  };
  choice(0,33,30,tr(Text::MenuBluetooth),tr(on?Text::BluetoothOnExperimental:Text::Off));
  g.setTextColor(colors.muted,colors.background);drawText(g,tr(Text::BluetoothAppearsAs),cx,72,1);
  g.setTextColor(colors.text,colors.background);drawText(g,name,cx,83,1);
  g.setTextColor(connected?colors.ready:colors.muted,colors.background);
  drawText(g,tr(!on?Text::BluetoothRadioOff:connected?Text::BluetoothPhoneConnected:Text::BluetoothNoPhone),cx,97,1);
  char line[24];snprintf(line,sizeof(line),tr(bonds==1?Text::BluetoothPairedOne:Text::BluetoothPairedOther),bonds);
  g.setTextColor(colors.muted,colors.background);drawText(g,line,cx,109,1);
  choice(1,128,24,tr(Text::BluetoothBeybladeX),nullptr);
  choice(2,160,24,tr(Text::BluetoothForget),nullptr);
  choice(3,192,24,tr(Text::Back),nullptr);
  footer(g,tr(row==0?(on?Text::FooterTurnOffNext:Text::FooterTurnOnNext):row<3?Text::FooterOpenNext:Text::FooterBackNext));
}
// What the Beyblade X page shows: the Battle Pass profile as an app sees it.
struct PassView {
  bool on=false;          // Bluetooth on
  bool app=false;         // an app subscribed to the Battle Pass profile
  bool phone=false;       // any phone on the link
  bool button=false;      // A held: the pass's button
  bool tournament=false;  // tournament mode hides RPM, so the pass tells nothing
  unsigned waiting=0;     // launches an app has not yet confirmed (0x75)
  unsigned synced=0;      // launches the last sync confirmed, 0 for none yet
};
// The Beyblade X page (Battle Pass profile, docs/BLE_BATTLE_PASS.md): whether
// an app is here, A held as the pass's button, which apps ask for to register
// a pass, and how many launches wait for the next sync.
inline void beybladeX(lgfx::LGFXBase &g,const PassView &v) {
  heading(g,tr(Text::BluetoothBeybladeX));
  const int cx=g.width()/2;
  g.setTextColor(!v.on || v.tournament?colors.warning:v.app?colors.ready:colors.muted,colors.background);
  drawText(g,tr(!v.on?Text::PassBluetoothOff:v.tournament?Text::PassTournament:v.app?Text::PassAppConnected:v.phone?Text::BluetoothPhoneConnected:Text::PassWaiting),cx,34,1);
  // The button: filled while held.
  const int by=96,radius=34;
  if(v.button)g.fillCircle(cx,by,radius,colors.accent);
  else g.drawCircle(cx,by,radius,colors.accent);
  g.setTextColor(v.button?colors.background:colors.accent,v.button?colors.accent:colors.background);
  drawText(g,tr(Text::PassButton),cx,by-8,2);
  g.setTextColor(v.button?colors.background:colors.muted,v.button?colors.accent:colors.background);
  drawText(g,tr(v.button?Text::PassPressed:Text::PassHoldA),cx,by+10,1);
  g.setTextColor(colors.muted,colors.background);
  drawText(g,tr(Text::PassRegister1),cx,146,1);
  drawText(g,tr(Text::PassRegister2),cx,158,1);
  char line[24];
  snprintf(line,sizeof(line),tr(Text::PassToSync),v.waiting);
  g.setTextColor(colors.text,colors.background);drawText(g,line,cx,182,1);
  if(v.synced) {
    snprintf(line,sizeof(line),tr(Text::PassLastSync),v.synced);
    g.setTextColor(colors.ready,colors.background);drawText(g,line,cx,196,1);
  }
  footer(g,tr(Text::PassFooter));
}
// Forget phones: chosen like New session, since it needs the phone paired again.
inline void forgetPhones(lgfx::LGFXBase &g,bool forget) {
  heading(g,tr(Text::BluetoothForget));g.setTextColor(colors.text,colors.background);
  drawText(g,tr(Text::ForgetAgain),g.width()/2,42,1);
  const char *labels[]={tr(Text::Cancel),tr(Text::ForgetConfirm)};
  for(unsigned i=0;i<2;++i) {
    const int y=91+int(i)*42;
    if(bool(i)==forget)g.drawRoundRect(12,y-5,g.width()-24,32,4,colors.accent);
    g.setTextColor(bool(i)==forget?colors.accent:colors.text,colors.background);
    drawText(g,labels[i],g.width()/2,y,2);
  }
  footer(g,tr(Text::FooterChooseNext));
}
// Before Bluetooth turns on (0.20.9): it is experimental, and says so. Chosen
// like New session, Cancel first, so a stray A changes nothing.
inline void enableBluetooth(lgfx::LGFXBase &g,bool enable) {
  heading(g,tr(Text::MenuBluetooth));
  const int cx=g.width()/2;
  g.setTextColor(colors.warning,colors.background);drawText(g,tr(Text::EnableExperimental),cx,34,2);
  g.setTextColor(colors.text,colors.background);
  drawText(g,tr(Text::EnableTested),cx,58,1);
  drawText(g,tr(Text::EnableBattery1),cx,71,1);
  drawText(g,tr(Text::EnableBattery2),cx,82,1);
  const char *labels[]={tr(Text::Cancel),tr(Text::EnableTurnOn)};
  for(unsigned i=0;i<2;++i) {
    const int y=112+int(i)*42;
    if(bool(i)==enable)g.drawRoundRect(12,y-5,g.width()-24,32,4,colors.accent);
    g.setTextColor(bool(i)==enable?colors.accent:colors.text,colors.background);
    drawText(g,labels[i],cx,y,2);
  }
  footer(g,tr(Text::FooterChooseNext));
}
// A phone asks to pair: over whatever was on screen, until answered or timed out.
// Automatic: the stick has no bond, so it accepts by itself shortly; B still rejects.
inline void comparison(lgfx::LGFXBase &g,uint32_t number,bool automatic) {
  g.fillScreen(colors.background);
  heading(g,tr(Text::PairTitle));
  const int cx=g.width()/2;
  g.setTextColor(colors.text,colors.background);drawText(g,tr(automatic?Text::PairCheck:Text::PairSame),cx,60,1);
  char digits[8];snprintf(digits,sizeof(digits),"%03u %03u",unsigned(number/1000%1000),unsigned(number%1000));
  g.setTextColor(colors.accent,colors.background);drawText(g,digits,cx,95,4);
  footer(g,tr(automatic?Text::PairReject:Text::PairConfirmReject));
}
inline void hold(lgfx::LGFXBase &g,const char *label,unsigned elapsed,unsigned duration) {
  footer(g,label);
  g.fillRect(4,225,int((g.width()-8)*std::min(elapsed,duration)/duration),2,colors.accent);
}
// The charge in the top right corner, as a phone shows it (Battery icon,
// 0.20.8): an outline whose fill follows the charge, no percentage. The fill
// takes the warning color at 20% or less, as the Battery page does, and the
// ready color while charging, with a bolt through the middle (0.20.9); there
// is no icon without a fresh reading.
struct CornerBattery {
  static constexpr int RIGHT=24,TOP=2,WIDTH=18,HEIGHT=8,FILL=WIDTH-4;
  // The charging bolt: each row's first column and width, from BOLT_LEFT.
  static constexpr int BOLT_ROWS=10,BOLT_LEFT=6;
  static constexpr uint8_t BOLT[BOLT_ROWS][2]={{4,2},{3,2},{2,2},{1,2},{0,5},{1,5},{3,2},{2,2},{1,2},{0,2}};
  enum Tone:uint8_t {Normal,Low,Charging};
  int8_t percent=-1;
  uint8_t fill=0;  // columns of FILL; 0 draws nothing
  Tone tone=Normal;
  bool operator==(const CornerBattery &o)const{return fill==o.fill && tone==o.tone && percent==o.percent;}
  bool operator!=(const CornerBattery &o)const{return !(*this==o);}
};
inline CornerBattery cornerBattery(bool valid,int percent,bool charging) {
  CornerBattery b;
  if(!valid || percent<0)return b;
  b.fill=uint8_t(std::max(1,(std::min(percent,100)*CornerBattery::FILL+50)/100));
  b.percent=int8_t(std::min(percent,100));
  b.tone=charging?CornerBattery::Charging:percent<=20?CornerBattery::Low:CornerBattery::Normal;
  return b;
}
inline void cornerBattery(lgfx::LGFXBase &g,CornerBattery b,bool showPercent=false) {
  using B=CornerBattery;
  if(!b.fill)return;
  const int x=g.width()-B::RIGHT;
  g.drawRoundRect(x,B::TOP,B::WIDTH,B::HEIGHT,2,colors.muted);
  g.fillRect(x+B::WIDTH,B::TOP+2,1,B::HEIGHT-4,colors.muted);  // the terminal
  g.fillRect(x+2,B::TOP+2,b.fill,B::HEIGHT-4,b.tone==B::Charging?colors.ready:b.tone==B::Low?colors.warning:colors.text);
  if(showPercent){char label[8];snprintf(label,sizeof(label),"%d%%",int(b.percent));
    g.setTextDatum(top_right);g.setTextColor(b.tone==B::Charging?colors.ready:b.tone==B::Low?colors.warning:colors.text,colors.background);
    drawText(g,label,x-2,B::TOP,1);
  }
  if(b.tone!=B::Charging)return;
  // The bolt through its middle, a row above and below the outline, on a
  // background halo so it reads over the fill at any charge.
  for(const bool halo:{true,false})for(int row=0;row<B::BOLT_ROWS;++row) {
    const int left=x+B::BOLT_LEFT+B::BOLT[row][0],y=B::TOP-1+row,width=B::BOLT[row][1];
    if(halo)g.fillRect(left-1,y-1,width+2,3,colors.background);else g.fillRect(left,y,width,1,colors.text);
  }
}
struct FlashInfo {
  uint32_t total=0,app=0,slot=0;
  bool appKnown()const{return app>0 && slot>0 && app<=slot;}
};
inline void device(lgfx::LGFXBase &g,bool valid,int percent,unsigned millivolts,bool chargeKnown,bool charging,const FlashInfo &flash) {
  const int cx=g.width()/2;char line[32];heading(g,tr(Text::MenuDevice));
  g.setTextColor(colors.muted,colors.background);drawText(g,tr(Text::DeviceBattery),cx,32,1);
  if(valid)snprintf(line,sizeof(line),"%d%%",std::clamp(percent,0,100));else snprintf(line,sizeof(line),"--");
  const bool low=valid && percent<=20;
  g.setTextColor(low?colors.warning:colors.accent,colors.background);drawText(g,line,cx,44,4);
  g.drawRoundRect(29,77,74,17,3,colors.muted);g.fillRect(103,82,4,7,colors.muted);
  if(valid)g.fillRect(33,81,66*std::clamp(percent,0,100)/100,9,low?colors.warning:colors.accent);
  g.setTextColor(colors.text,colors.background);
  if(valid)snprintf(line,sizeof(line),"%.2f V",millivolts/1000.0);else snprintf(line,sizeof(line),"%s",tr(Text::DeviceNoReading));
  drawText(g,line,cx,101,valid?2:1);
  drawText(g,tr(!chargeKnown?Text::DeviceChargeUnknown:charging?Text::DeviceCharging:Text::DeviceNotCharging),cx,124,1);
  g.setTextColor(colors.text,colors.background);drawText(g,tr(Text::DeviceFlash),cx,147,2);
  constexpr float MIB=1048576.f;
  if(flash.total)snprintf(line,sizeof(line),tr(Text::DeviceTotal),flash.total/MIB);else snprintf(line,sizeof(line),"%s",tr(Text::DeviceTotalUnknown));
  drawText(g,line,cx,171,1);
  if(flash.appKnown())snprintf(line,sizeof(line),tr(Text::DeviceApp),flash.app/MIB,flash.slot/MIB);else snprintf(line,sizeof(line),"%s",tr(Text::DeviceAppUnknown));
  drawText(g,line,cx,187,1);
  if(flash.appKnown())snprintf(line,sizeof(line),tr(Text::DeviceAppFree),(flash.slot-flash.app)/MIB);else snprintf(line,sizeof(line),"%s",tr(Text::DeviceAppFreeUnknown));
  drawText(g,line,cx,203,1);
  g.setTextColor(colors.muted,colors.background);drawText(g,tr(Text::DeviceAppSlot),cx,218,1);
  footer(g,tr(Text::FooterDone));
}
// What the unit runs, for an owner and for support: the firmware release and
// its build, the unit's ID (its MAC, also its USB serial number) and how many
// pulls its history holds.
inline void about(lgfx::LGFXBase &g,const char *deviceId,unsigned pulls) {
  const int cx=g.width()/2;
  heading(g,tr(Text::SettingAbout));
  const int length=versionLength(runtime);char line[32];
  g.setTextColor(colors.muted,colors.background);drawText(g,tr(Text::AboutFirmware),cx,38,1);
  snprintf(line,sizeof(line),"%.*s",length,runtime);
  g.setTextColor(colors.accent,colors.background);drawText(g,line,cx,52,4);
  g.setTextColor(colors.faint,colors.background);drawText(g,runtime[length]?runtime+length+1:"",cx,82,1);
  g.setTextColor(colors.muted,colors.background);drawText(g,tr(Text::AboutDeviceId),cx,108,1);
  g.setTextColor(colors.text,colors.background);drawText(g,deviceId,cx,122,1);
  g.setTextColor(colors.muted,colors.background);drawText(g,tr(Text::AboutPulls),cx,148,1);
  snprintf(line,sizeof(line),"%u",pulls);
  g.setTextColor(colors.text,colors.background);drawText(g,line,cx,162,2);
  g.setTextColor(colors.muted,colors.background);drawText(g,"launchlab.beywatch.gg",cx,200,1);
  footer(g,tr(Text::FooterBack));
}
inline void wipe(lgfx::LGFXBase &g,const WipeConfirmation &flow,uint32_t now,bool failed,bool blocked,bool complete) {
  heading(g,tr(Text::SettingWipeAll));
  if(complete) {
    g.setTextColor(colors.text,colors.background);drawText(g,tr(Text::WipeErased),g.width()/2,78,2);
    drawText(g,tr(Text::WipeRelease),g.width()/2,123,1);
    drawText(g,tr(Text::WipeToRestart),g.width()/2,141,1);
    footer(g,tr(Text::WipeRestartAfter));return;
  }
  if(blocked && !flow.active()) {
    g.setTextColor(colors.warning,colors.background);drawText(g,tr(Text::WipeIncomplete),g.width()/2,72,2);
    g.setTextColor(colors.text,colors.background);drawText(g,tr(Text::WipeReview),g.width()/2,113,1);
    drawText(g,tr(Text::WipeToFinish),g.width()/2,131,1);
    footer(g,tr(Text::WipeFooterReview));return;
  }
  char step[24];snprintf(step,sizeof(step),tr(Text::WipeStep),unsigned(flow.stage())+1);
  g.setTextColor(colors.muted,colors.background);drawText(g,step,g.width()/2,35,1);
  g.setTextColor(colors.text,colors.background);
  switch(flow.stage()) {
    case WipeConfirmation::Stage::Warning:
      drawText(g,tr(Text::WipeErase),g.width()/2,63,1);
      drawText(g,tr(Text::WipeLaunches),g.width()/2,93,2);
      drawText(g,tr(Text::WipeBests),g.width()/2,116,2);
      drawText(g,tr(Text::WipeTilts),g.width()/2,139,2);
      drawText(g,tr(Text::WipeSettings),g.width()/2,162,2);
      footer(g,tr(Text::WipeFooterContinue));break;
    case WipeConfirmation::Stage::Confirm:
      drawText(g,tr(Text::WipeUndone),g.width()/2,69,1);
      drawText(g,tr(Text::WipeBackUp),g.width()/2,111,2);
      drawText(g,tr(Text::WipeEverything),g.width()/2,148,1);
      footer(g,tr(Text::WipeFooterConfirm));break;
    case WipeConfirmation::Stage::Armed:
      drawText(g,tr(Text::WipeHold),g.width()/2,77,2);
      drawText(g,tr(flow.awaitingRelease()?Text::WipeRelease:Text::WipeReleaseStop),g.width()/2,115,1);
      drawText(g,tr(Text::WipeBCancels),g.width()/2,133,1);
      g.drawRoundRect(14,155,g.width()-28,17,3,colors.muted);
      g.fillRect(16,157,int((g.width()-32)*flow.elapsed(now)/WipeConfirmation::HOLD_MS),13,colors.warning);
      footer(g,tr(Text::WipeFooterHold));break;
  }
  if(failed){g.setTextColor(colors.warning,colors.background);drawText(g,tr(blocked?Text::WipeRetry:Text::WipeIdle),g.width()/2,199,1);}
}
// detectorReady: the RPM detector would measure a burst now. In its rearm
// second after a burst a release counts as that burst's rewind and is never
// measured, so PULL OUT shows WAIT and LET GO waits for it (0.20.4).
// mount: the Mounting setting the tilt is read in (Settings, Mounting); a
// Sidecar install measured as Top (or a turned Top) reads its level 90 degrees
// off, and the page then asks about the other one (otherMount).
inline void wearCheck(lgfx::LGFXBase &g,const WearCheck::Session &wear,const WearCheck::Tilt &rawTilt,const WearCheck::Tilt &displayTilt,
  bool detectorReady=true,const char *mount=nullptr,const char *otherMount=nullptr) {
  const int cx=g.width()/2;
  if(!mount)mount=tr(Text::WearTopMount);
  if(!otherMount)otherMount=tr(Text::WearSidecarInstall);
  if(wear.complete()) {
    heading(g,tr(Text::MenuWearCheck));
    g.drawRoundRect(8,31,g.width()-16,58,5,colors.accent);
    char line[28];
    if(WearCheck::FRESH_BASELINE_RPM>0.0f) {
      snprintf(line,sizeof(line),tr(Text::WearHealth),unsigned(std::lround(WearCheck::healthPercent(wear.overallMean()))));
    } else {
      snprintf(line,sizeof(line),"%s",tr(Text::WearRetraction));
    }
    g.setTextColor(colors.muted,colors.background);
    drawText(g,line,cx,35,1);
    snprintf(line,sizeof(line),"%ld",std::lround(wear.overallMean()));
    g.setTextColor(colors.number,colors.background);
    drawText(g,line,cx,46,4);
    snprintf(line,sizeof(line),tr(Text::WearMedian),std::lround(wear.overallMedianMean()));
    g.setTextColor(colors.muted,colors.background);
    drawText(g,line,cx,76,1);
    // By the direction each stage asked for (2/3 Pull Down, 3/3 Pull Up).
    const char *stageNames[WearCheck::STAGES]={tr(Text::WearLevel),tr(Text::WearDown),tr(Text::WearUp)};
    for(unsigned s=0;s<WearCheck::STAGES;++s) {
      const int y=93+int(s)*40;
      g.drawRoundRect(8,y,g.width()-16,36,4,colors.muted);
      // Five-digit pulls drop the unit and the spaces to fit the card (font 1 is 6 px a glyph).
      const bool wide=std::max({wear.pullRpm(s,0),wear.pullRpm(s,1),wear.pullRpm(s,2)})>=9999.5f;
      snprintf(line,sizeof(line),tr(wide?Text::WearStageWide:Text::WearStage),stageNames[s],std::lround(wear.stageMean(s)));
      g.setTextColor(colors.text,colors.background);
      drawText(g,line,cx,y+4,2);
      snprintf(line,sizeof(line),wide?"%ld/%ld/%ld":"%ld / %ld / %ld",
        std::lround(wear.pullRpm(s,0)),std::lround(wear.pullRpm(s,1)),std::lround(wear.pullRpm(s,2)));
      g.setTextColor(colors.muted,colors.background);
      drawText(g,line,cx,y+23,1);
    }
    footer(g,tr(Text::WearFooterDone));
    return;
  }
  const auto stage=wear.stage();
  heading(g,WearCheck::stageHeader(stage));
  g.setTextColor(colors.text,colors.background);
  drawText(g,WearCheck::stagePrompt(stage),cx,30,2);
  char sub[28];
  const auto outcome=wear.lastOutcome();
  if(outcome==WearCheck::Outcome::RejectedLevel) {
    g.setTextColor(colors.warning,colors.background);
    drawText(g,tr(Text::WearRetryLevel),cx,50,1);
  } else if(outcome==WearCheck::Outcome::RejectedVert) {
    g.setTextColor(colors.warning,colors.background);
    drawText(g,tr(Text::WearRetryVertical),cx,50,1);
  } else if(outcome==WearCheck::Outcome::RejectedFlip) {
    g.setTextColor(colors.warning,colors.background);
    drawText(g,tr(Text::WearRetryFlip),cx,50,1);
  } else if(outcome==WearCheck::Outcome::RejectedNoImu) {
    g.setTextColor(colors.warning,colors.background);
    drawText(g,tr(Text::WearRetryImu),cx,50,1);
  } else if(outcome==WearCheck::Outcome::EarlyRelease) {
    g.setTextColor(colors.warning,colors.background);
    drawText(g,tr(Text::WearRetryLonger),cx,50,1);
  } else if(outcome==WearCheck::Outcome::RejectedSignal || outcome==WearCheck::Outcome::Missed) {
    g.setTextColor(colors.warning,colors.background);
    drawText(g,tr(Text::WearRetryMissed),cx,50,1);
  } else if(wear.currentStageCount()>0) {
    snprintf(sub,sizeof(sub),tr(Text::WearLast),std::lround(wear.lastRpm()));
    g.setTextColor(colors.ready,colors.background);
    drawText(g,sub,cx,50,1);
  } else {
    g.setTextColor(colors.muted,colors.background);
    drawText(g,tr(Text::WearIntro),cx,50,1);
  }
  for(unsigned p=0;p<WearCheck::PULLS_PER_STAGE;++p) {
    const int px=9+int(p)*40,pw=37,ph=18;
    const bool done=p<wear.currentStageCount();
    g.drawRoundRect(px,63,pw,ph,4,done?colors.accent:colors.muted);
    if(done)snprintf(sub,sizeof(sub),"%ld",std::lround(wear.pullRpm(unsigned(stage),p)));
    else snprintf(sub,sizeof(sub),"#%u",p+1);
    g.setTextColor(done?colors.number:colors.muted,colors.background);
    drawText(g,sub,px+pw/2,68,1);
  }
  const int cy=125,radius=32;
  const auto align=wear.evaluate(rawTilt);
  const uint16_t ringColor=align==WearCheck::Alignment::Ready?colors.ready:colors.muted;
  g.drawRoundRect(cx-radius,cy-radius,2*radius,2*radius,radius,colors.muted);
  g.fillRect(cx-radius+3,cy,2*radius-6,1,colors.muted);
  g.fillRect(cx,cy-radius+3,1,2*radius-6,colors.muted);
  g.drawRoundRect(cx-14,cy-14,28,28,14,ringColor);
  if(rawTilt.valid) {
    float dx=0.0f,dy=0.0f;
    wear.reticleOffset(rawTilt,displayTilt,dx,dy);
    const int dotX=cx+int(std::lround(dx*26.0f));
    const int dotY=cy+int(std::lround(dy*26.0f));
    const uint16_t dotColor=align==WearCheck::Alignment::Ready?colors.ready:colors.warning;
    g.fillRect(dotX-3,dotY-3,7,7,dotColor);
    snprintf(sub,sizeof(sub),tr(Text::WearDegrees),rawTilt.degrees);
    g.setTextColor(dotColor,colors.background);
    drawText(g,sub,cx,166,2);
  } else {
    g.setTextColor(colors.muted,colors.background);
    drawText(g,tr(Text::WearNoDegrees),cx,166,2);
  }
  // On target the label is the pull's step: PULL OUT (WAIT while the
  // detector rearms), HOLD, then LET GO.
  const bool aligned=align==WearCheck::Alignment::Ready;
  const char *label=aligned?WearCheck::stepLabel(wear.step(),detectorReady):WearCheck::alignmentLabel(align);
  const bool waiting=aligned && wear.step()==WearCheck::Step::PullOut && !detectorReady;
  const uint16_t alignColor=waiting || align==WearCheck::Alignment::NoImu?colors.muted:
    !aligned?colors.warning:wear.step()==WearCheck::Step::Hold?colors.accent:colors.ready;
  g.setTextColor(alignColor,colors.background);
  drawText(g,label,cx,188,2);
  if(wear.mountHint()) {
    // Level in the other Mounting, not in this one.
    g.setTextColor(colors.warning,colors.background);
    drawText(g,otherMount,cx,207,1);
    drawText(g,tr(Text::WearSettingsMount),cx,217,1);
  } else {
    g.setTextColor(colors.muted,colors.background);
    drawText(g,mount,cx,210,1);
  }
  footer(g,tr(Text::WearFooter));
}
}
