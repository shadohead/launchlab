// Every screen the theme editor previews and the host render check pins,
// composed the way LaunchLabMini.ino's draw() composes them, from the
// firmware's own drawing code and M5GFX. Shared by the host renderer
// (ui_render.cpp) and the browser build (ui_wasm.cpp).
#pragma once
#include <cstdint>
#include <cstring>
#include "controls_ui.h"
#include "practice_ui.h"
#include "motion_ui.h"
#include "screens_ui.h"
#include "best_effect_ui.h"
#include "milestones.h"
#include "theme_library.h"
#include "brightness.h"
#include "power_status.h"
#include "rpm_estimator.h"
#include "mic_view_ui.h"

namespace UiCatalog {
constexpr int WIDTH=135,HEIGHT=240;
// What the screen shows: its colors, the selected theme ID and the name the
// Theme rows show for it.
struct Look {
  Palette::Colors colors;
  Appearance::Theme theme=Appearance::Theme::Classic;
  const char *name="Pegasus Cyan";
};
struct Screen {const char *id;const char *title;};
inline constexpr Screen SCREENS[]={
  {"main","Main"},{"main-tilted","Main, tilted"},{"main-idle","Main, new session"},{"main-charging","Main, charging"},{"main-idle-charging","Main, new session, charging"},
  {"recap","Recap"},{"recap-best","Recap, new best (Orbit)"},{"recap-flames","Best: Flames"},{"recap-sparkles","Best: Sparkles"},
  {"recap-shockwave","Best: Shockwave"},{"recap-lightning","Best: Lightning"},{"recap-slash","Best: Slash"},
  {"recap-milestone","Recap, 12K milestone (Flames)"},{"recap-demo","Recap demo"},{"recap-capturing","Recap, capturing"},
  {"menu","Menu"},{"menu-battery-low","Menu, battery low"},{"menu-charging","Menu, charging"},{"menu-settings","Menu, Settings"},{"menu-wear-check","Menu, String wear check"},{"history-menu","History menu"},
  {"settings","Settings"},{"settings-appearance","Settings, appearance"},{"settings-more","Settings, wipe and back"},{"about","About"},
  {"menu-bluetooth","Menu, Bluetooth"},{"bluetooth","Bluetooth, off"},{"bluetooth-enable","Bluetooth, experimental notice"},{"bluetooth-on","Bluetooth, on with a phone"},
  {"bluetooth-forget","Bluetooth, forget phones"},{"bluetooth-pair","Bluetooth, pairing number"},
  {"theme-editor","Theme editor"},{"effect-editor","Best effect editor"},{"celebrate-editor","Celebrate editor"},
  {"battery-icon-editor","Battery icon editor, Off"},
  {"history","Launch history"},{"pull-trend","Pull trend"},{"session-trend","Session trend"},{"history-empty","History, empty"},
  {"battery","Battery"},{"battery-low","Battery, low"},{"new-session","New session"},{"hold","Hold to enter tournament"},
  {"wipe","Wipe all"},{"wear-check","Wear check"},{"wear-check-retry","Wear check, retry level"},
  {"wear-check-down","Wear check, pull down"},{"wear-check-wait","Wear check, rearming"},
  {"wear-check-hold","Wear check, hold"},{"wear-check-let-go","Wear check, let go"},
  {"wear-check-early","Wear check, let go early"},{"wear-check-missed","Wear check, not measured"},{"wear-check-flip","Wear check, flip"},{"wear-check-sidecar","Wear check, Sidecar mount"},{"wear-check-mount-hint","Wear check, other Mounting"},{"wear-check-no-imu","Wear check, no tilt"},
  {"wear-check-result","Wear check result"},{"tournament","Tournament"},{"tournament-rpm","Tournament RPM"},{"diagnostics","Signal diagnostics"},
};
constexpr unsigned COUNT=sizeof(SCREENS)/sizeof(SCREENS[0]);

namespace detail {
// The corner battery on the catalog's screens: a fixed reading, so no pinned
// frame moves with the host.
inline ControlsUI::CornerBattery corner(int percent=72,bool charging=false){return ControlsUI::cornerBattery(true,percent,charging);}
// The device's 't' history demo, value for value.
inline const PracticeHistory &demoHistory() {
  static PracticeHistory h;static bool made=false;
  if(!made) {
    uint32_t count=0;
    for(unsigned session=0;session<18;++session) {
      h.newSession();
      for(unsigned i=0;i<12;++i)h.accept(++count,true,4200+session*130+float(int(i%5)-2)*260+i*35,session*900000+i*20000);
    }
    made=true;
  }
  return h;
}
inline const LaunchMotion::Trace &demoTrace() {
  static LaunchMotion::Trace t;static bool made=false;
  if(!made){MotionUI::makeDemo(t,false);made=true;}
  return t;
}
// The recap a celebration draws on: the demo pull at another RPM.
inline const LaunchMotion::Trace &demoTrace(float rpm) {
  static LaunchMotion::Trace t;t=demoTrace();t.rpm=rpm;return t;
}
}

// The wear check `id` shows: its session, the raw tilt and the one drawn.
inline void wearCheck(lgfx::LGFX_Sprite &g,const char *id,bool flip) {
  using WearCheck::Tilt;
  auto is=[&](const char *name){return !std::strcmp(id,name);};
  WearCheck::Session wear;uint32_t launch=0;
  const Tilt level{true,1.2f,.03f,-.02f},down{true,88.6f,.12f,.99f},up{true,89.1f,-.08f,-1.0f};
  auto pull=[&](float rpm,const Tilt &t){wear.observePull(++launch,true,rpm,t);};
  Tilt shown{true,3.4f,.08f,.06f};
  // The steps of a pull: the string out (marks), held still, let go.
  uint64_t t=1000000;uint32_t turns=0;
  auto marks=[&]{t+=WearCheck::STILL_US;wear.observeOptics(t,turns+=WearCheck::RUN_TURNS,true);};
  auto still=[&](uint64_t us,bool ready){t+=us;wear.observeOptics(t,turns,ready);};
  wear.observeOptics(t,turns,true);
  if(is("wear-check-retry")){pull(3259,level);pull(3301,Tilt{true,21.5f,.6f,.4f});shown={true,17.8f,.48f,.33f};}
  else if(is("wear-check-wait")){pull(3259,level);shown=level;}
  else if(is("wear-check-mount-hint")) {
    // A Sidecar install measured as Top: 90 degrees off here, level there.
    shown={true,89.6f,0.99f,.05f};
    for(uint32_t ms:{1000u,1000u+WearCheck::MOUNT_HINT_MS})wear.observeMount(ms,shown,Tilt{true,0.6f,.01f,.01f});
  }
  else if(is("wear-check-hold")){pull(3259,level);marks();still(400000,false);}
  else if(is("wear-check-let-go")){pull(3259,level);marks();still(WearCheck::HOLD_US,true);}
  else if(is("wear-check-early")){pull(3259,level);marks();still(400000,false);marks();}
  else if(is("wear-check-missed")){pull(3259,level);marks();still(WearCheck::HOLD_US,true);marks();still(WearCheck::MISSED_US,true);}
  else if(is("wear-check-down")){for(float r:{3259.f,3240.f,3278.f})pull(r,level);pull(2029,down);pull(2048,down);shown={true,84.3f,.2f,.98f};}
  else if(is("wear-check-flip")) {
    for(float r:{3259.f,3240.f,3278.f})pull(r,level);
    for(float r:{2029.f,2010.f,2048.f})pull(r,down);
    pull(2031,down);shown=down;
  }
  else if(is("wear-check-no-imu")){wear.observePull(++launch,false,0,level);shown=Tilt{};}
  else if(is("wear-check-result")) {
    for(float r:{3259.f,3240.f,3278.f})pull(r,level);
    for(float r:{2029.f,2010.f,2048.f})pull(r,down);
    for(float r:{3680.f,3701.f,3722.f})pull(r,up);
  }
  Tilt drawn=shown;
  drawn.right*=DisplayOrientation::axisSign(flip);drawn.down*=DisplayOrientation::axisSign(flip);
  ControlsUI::wearCheck(g,wear,shown,drawn,!is("wear-check-wait"),is("wear-check-sidecar")?"Sidecar mount":"Top mount");
}

// The recap with a celebration `elapsed` ms in: the frame the device draws
// for a pull of `rpm` labelled `badge` (NEW BEST, a milestone, or none).
inline void celebration(lgfx::LGFX_Sprite &g,const Palette::Colors &c,bool flip,float rpm,const char *badge,Appearance::Effect effect,uint32_t elapsed) {
  ControlsUI::colors=c;
  g.fillScreen(c.background);g.setTextDatum(top_center);g.setTextColor(c.text,c.background);
  const auto &trace=detail::demoTrace(rpm);
  MotionUI::feedback(g,trace,false,false,MotionReplay::playbackProgress(trace,elapsed),flip,c,badge,MotionUI::demoCurve(rpm));
  ControlsUI::footer(g,"A Main  B Menu");
  static BestEffectUI::Renderer renderer;
  renderer.draw({static_cast<uint16_t *>(g.getBuffer()),WIDTH,HEIGHT},effect,elapsed,c.accent,BestEffectUI::Renderer::Anchor(),c.background);
}

// The Mic RPM page's screens: pinned by the render check, and not in the theme
// preview, which shows SCREENS.
inline constexpr Screen MIC_SCREENS[]={
  {"menu-mic","Menu, Mic RPM"},{"mic-starting","Mic RPM, warming up"},{"mic-failed","Mic RPM, failed"},{"mic-memory","Mic RPM, no memory"},{"mic-ready","Mic RPM, ready"},
  {"mic-listening","Mic RPM, listening"},{"mic-first","Mic RPM, first reading"},{"mic-spin","Mic RPM, 8 s spin"},
  {"mic-gaps","Mic RPM, gaps"},{"mic-long","Mic RPM, 2 minutes"},{"mic-ended","Mic RPM, ended"},{"mic-none","Mic RPM, nothing heard"},
  {"mic-fast","Mic RPM, above the default scale"},{"mic-no-peak","Mic RPM, no optical peak"},
};
constexpr unsigned MIC_COUNT=sizeof(MIC_SCREENS)/sizeof(MIC_SCREENS[0]);
inline void micScreen(lgfx::LGFX_Sprite &g,unsigned index,const Look &look) {
  using namespace MicTrace;
  const auto &c=look.colors;
  ControlsUI::colors=c;
  const char *id=MIC_SCREENS[index].id;
  auto is=[&](const char *name){return !std::strcmp(id,name);};
  g.fillScreen(c.background);g.setTextDatum(top_center);g.setTextColor(c.text,c.background);
  if(is("menu-mic")){ControlsUI::list(g,"Menu",ControlsUI::MENU,Controls::MENU_COUNT,Controls::MIC_ROW,Controls::MENU_GROUPS);ControlsUI::cornerBattery(g,detail::corner());return;}
  static StoredTrace t;t.clear();
  auto mark=[&](unsigned index,unsigned rpm,MicRpm::Reason reason,bool last){
    Step s;s.index=uint16_t(index);s.rpm=uint16_t(rpm);s.reason=reason;s.last=last;t.add(s);
  };
  auto mic=MicView::Mic::On;
  if(is("mic-starting"))mic=MicView::Mic::Starting;
  else if(is("mic-failed"))mic=MicView::Mic::Failed;
  else if(is("mic-memory"))mic=MicView::Mic::NoMemory;
  else if(is("mic-listening")){t.begin(0,6900);for(unsigned i=0;i<4;++i)mark(i,0,MicRpm::Reason::BroadbandNoise,false);}
  else if(is("mic-first"))demo(t,8,6);
  else if(is("mic-spin"))demo(t,80,6);
  else if(is("mic-gaps")) {
    demo(t,30,6);
    for(unsigned i=30;i<36;++i)mark(i,0,MicRpm::Reason::WeakHarmonics,false);
    for(unsigned i=36;i<70;++i)mark(i,unsigned(5600-(i-36)*14),MicRpm::Reason::Tracking,false);
    mark(70,0,MicRpm::Reason::WeakHarmonics,false);mark(71,0,MicRpm::Reason::WeakHarmonics,false);
    mark(75,4900,MicRpm::Reason::Tracking,false);mark(76,0,MicRpm::Reason::WeakHarmonics,false);
  }
  else if(is("mic-long"))demo(t,MAX_READINGS,8,100);
  else if(is("mic-ended")){demo(t,120,9);for(unsigned i=120;i<200;++i)mark(i,0,MicRpm::Reason::Quiet,i==199);}
  else if(is("mic-none")){t.begin(0,5200);for(unsigned i=0;i<=LOST_READINGS;++i)mark(i,0,MicRpm::Reason::WeakHarmonics,i==LOST_READINGS);}
  else if(is("mic-fast")){t.begin(0,12900);for(unsigned i=9;i<60;++i)mark(i,unsigned(12400-(i-9)*40),MicRpm::Reason::Tracking,false);}
  else if(is("mic-no-peak")){t.begin(0);for(unsigned i=9;i<60;++i)mark(i,unsigned(6100-(i-9)*20),MicRpm::Reason::Tracking,false);}
  MicView::draw(g,&t,mic,c);
}

// Draws screen `index` into a 135x240 16-bit canvas.
inline void draw(lgfx::LGFX_Sprite &g,unsigned index,const Look &look,bool flip) {
  using Controls::Page;
  const auto &c=look.colors;
  static const Look *current=nullptr;current=&look;
  ControlsUI::colors=c;
  ControlsUI::themeLabel=[](Appearance::Theme t){return t==current->theme?current->name:Appearance::label(t);};
  const char *id=index<COUNT?SCREENS[index].id:"main";
  auto is=[&](const char *name){return !std::strcmp(id,name);};
  auto prefix=[&](const char *name){return !std::strncmp(id,name,std::strlen(name));};
  const int cx=WIDTH/2;
  g.fillScreen(c.background);
  g.setTextDatum(top_center);
  g.setTextColor(c.text,c.background);
  const bool tournament=prefix("tournament"),diagnostics=is("diagnostics");
  const bool recap=prefix("recap"),main=prefix("main") || is("hold");
  if(diagnostics)ScreensUI::diagnosticsHeader(g,c,false,"Ready",true);
  g.setTextColor(c.text,c.background);
  if(tournament) {
    ScreensUI::tournament(g,c,true,is("tournament"),true,7340);
    ControlsUI::footer(g,"B View  Hold A Exit");
  } else if(diagnostics) {
    ScreensUI::diagnostics(g,1,612,388,3410,50000);
  } else if(recap) {
    g.fillScreen(c.background);
    const bool best=!is("recap") && !is("recap-demo") && !is("recap-capturing");
    const bool milestone=is("recap-milestone");
    const float progress=is("recap-demo")?.3f:is("recap")?.55f:.8f;
    const auto &trace=milestone?detail::demoTrace(12340):detail::demoTrace();
    MotionUI::feedback(g,trace,is("recap-capturing"),is("recap-demo"),progress,flip,c,!best?nullptr:milestone?"MILESTONE 12K":"NEW BEST",
      MotionUI::demoCurve(trace.rpm));
    ControlsUI::footer(g,"A Main  B Menu");
    if(best) {
      static BestEffectUI::Renderer renderer;
      const auto effect=is("recap-flames") || milestone?Appearance::Effect::Flames:is("recap-sparkles")?Appearance::Effect::Sparkles:
        is("recap-shockwave")?Appearance::Effect::Shockwave:is("recap-lightning")?Appearance::Effect::Lightning:
        is("recap-slash")?Appearance::Effect::Slash:Appearance::Effect::Orbit;
      // Slash mid-split; the others at their busiest.
      renderer.draw({static_cast<uint16_t *>(g.getBuffer()),WIDTH,HEIGHT},effect,effect==Appearance::Effect::Slash?600:450,c.accent,
        BestEffectUI::Renderer::Anchor(),c.background);
    }
  } else if(main) {
    const bool tilted=is("main-tilted"),idle=is("main-idle") || is("main-idle-charging");
    ScreensUI::live(g,c,idle?"New session ready":tilted?"Settling":"Ready",
      idle?ScreensUI::Tone::Text:tilted?ScreensUI::Tone::Warning:ScreensUI::Tone::Ready,!tilted,idle?12345:6512);
    StickS3Level::Reading level;
    if(tilted)level={StickS3Level::State::Valid,7.5f,.8f,.4f};
    else if(!idle)level={StickS3Level::State::Valid,1.4f,.2f,-.35f};
    ScreensUI::level(g,c,cx,StickS3Level::forDisplay(level,flip));
  } else {
    g.fillScreen(c.background);
    Controls::State state;
    // A new unit's settings: every value is its default.
    const uint8_t values[]={InactivityTimer::DEFAULT_MINUTES,uint8_t(RpmEstimator::DEFAULT_MODE),Brightness::DEFAULT_PERCENT,uint8_t(flip),0,uint8_t(look.theme),uint8_t(Appearance::Effect::Random),uint8_t(Milestones::DEFAULT_MODE),0,0,uint8_t(InactivityTimer::SHAKE_WAKE_DEFAULT),
      0,0,uint8_t(Controls::BATTERY_ICON_DEFAULT),0};  // Wipe all and About have no value; Bluetooth is Off
    static_assert(sizeof(values)==Controls::SETTING_COUNT,"a default for every setting");
    static const Milestones::Config defaults;ControlsUI::milestones=&defaults;
    ControlsUI::runtime="0.20.8-sticks3-songs-pm";  // fixed, so a release number never moves a pinned frame
    if(is("bluetooth"))ControlsUI::bluetoothPage(g,0,false,false,"BEYBLADE_TOOL01",0);
    else if(is("bluetooth-enable"))ControlsUI::enableBluetooth(g,true);
    else if(is("bluetooth-on"))ControlsUI::bluetoothPage(g,1,true,true,"BEYBLADE_TOOL01",1);
    else if(is("bluetooth-forget"))ControlsUI::forgetPhones(g,false);
    else if(is("bluetooth-pair"))ControlsUI::comparison(g,123456,false);
    else if(prefix("menu")){state.menu=is("menu-settings")?Controls::SETTINGS_ROW:is("menu-wear-check")?Controls::WEAR_ROW:is("menu-bluetooth")?Controls::BLUETOOTH_ROW:0;ControlsUI::list(g,"Menu",ControlsUI::MENU,Controls::MENU_COUNT,state.menu,Controls::MENU_GROUPS);}
    else if(is("history-menu"))ControlsUI::list(g,"History",ControlsUI::HISTORY,4,0);
    else if(is("settings")){state.setting=0;ControlsUI::settings(g,state,values);}
    else if(is("settings-appearance")){state.setting=uint8_t(Controls::rowAt(5));ControlsUI::settings(g,state,values);}
    else if(is("settings-more")){state.setting=unsigned(Controls::Setting::WipeAll);ControlsUI::settings(g,state,values);}
    else if(is("about"))ControlsUI::about(g,"7C:E8:B1:12:34:56",128);
    else if(is("celebrate-editor")) {
      state.page=Page::Edit;state.setting=unsigned(Controls::Setting::Celebrate);state.draft=uint8_t(Milestones::Mode::Both);
      ControlsUI::editor(g,state,false,false);
    }
    else if(is("theme-editor")){state.page=Page::Edit;state.setting=5;state.draft=uint8_t(look.theme);ControlsUI::editor(g,state,false,false);}
    else if(is("battery-icon-editor")){state.page=Page::Edit;state.setting=unsigned(Controls::Setting::BatteryIcon);state.draft=0;ControlsUI::editor(g,state,false,false);}
    else if(is("effect-editor")) {
      state.page=Page::Edit;state.setting=unsigned(Controls::Setting::BestEffect);state.draft=uint8_t(Appearance::Effect::Orbit);ControlsUI::editor(g,state,false,false);
      static BestEffectUI::Renderer renderer;BestEffectUI::Renderer::Anchor card;
      card.top=72;card.bottom=128;card.left=8;card.right=WIDTH-9;
      renderer.draw({static_cast<uint16_t *>(g.getBuffer()),WIDTH,HEIGHT},Appearance::Effect::Orbit,500,c.accent,card,c.background);
    }
    else if(is("history"))PracticeUI::draw(g,detail::demoHistory(),PracticeUI::View::Recent,0,true,c);
    else if(is("pull-trend"))PracticeUI::draw(g,detail::demoHistory(),PracticeUI::View::Pulls,0,true,c);
    else if(is("session-trend"))PracticeUI::draw(g,detail::demoHistory(),PracticeUI::View::Sessions,0,true,c);
    else if(is("history-empty")){static PracticeHistory empty;PracticeUI::draw(g,empty,PracticeUI::View::Recent,0,false,c);}
    else if(is("battery"))ControlsUI::battery(g,true,72,3984,true,true);
    else if(is("battery-low"))ControlsUI::battery(g,true,15,3512,true,false);
    else if(is("new-session"))ControlsUI::session(g,false);
    else if(is("wipe")){static WipeConfirmation flow;flow.begin(0);ControlsUI::wipe(g,flow,0,false,false,false);}
    else if(prefix("wear-check"))wearCheck(g,id,flip);
  }
  if(!tournament && (diagnostics || main)) {
    g.setTextDatum(top_center);g.setTextColor(c.muted,c.background);
    ControlsUI::footer(g,"A Recap  B Menu");
  }
  if(is("hold"))ControlsUI::hold(g,"Enter tournament",600,1000);
  // The corner battery (Battery icon, on by default) on the Menu and Settings
  // pages, where the unit draws it (Controls::showsCornerBattery); the Battery
  // icon editor previews Off.
  if(prefix("menu") || is("history-menu") || prefix("settings") || is("about") || (std::strstr(id,"-editor") && !is("battery-icon-editor")))
    ControlsUI::cornerBattery(g,detail::corner(is("menu-battery-low")?15:72,is("menu-charging")));
  // Main shows it only while charging (Controls::showsChargingBattery).
  if(is("main-charging") || is("main-idle-charging"))ControlsUI::cornerBattery(g,detail::corner(is("main-idle-charging")?100:72,true));
}
}
