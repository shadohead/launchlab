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
#include "battle_ui.h"
#include "analysis_ui.h"
#include "analysis_result_ui.h"

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
  {"bluetooth-beyblade-x","Bluetooth, Beyblade X page"},{"bluetooth-beyblade-x-held","Bluetooth, Beyblade X, pass button held"},
  {"theme-editor","Theme editor"},{"effect-editor","Best effect editor"},{"celebrate-editor","Celebrate editor"},
  {"battery-icon-editor","Battery icon editor, Off"},{"min-rpm-editor","Min RPM editor, 10000"},
  {"history","Launch history"},{"pull-trend","Pull trend"},{"session-trend","Session trend"},{"history-empty","History, empty"},
  {"battery","Device"},{"battery-low","Device, battery low"},{"new-session","New session"},{"hold","Hold to enter tournament"},
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

// The battle cards and the Battles pages: pinned by the render check, and not
// in the theme preview, which shows SCREENS.
inline constexpr Screen BATTLE_SCREENS[]={
  {"menu-battles","Menu, Battles"},{"battle-versus","Battle, open"},{"battle-versus-slower","Battle, the slower launch"},
  {"battle-versus-wide","Battle, long name and five digits"},{"battle-versus-skip","Battle, hold B to skip"},
  {"battle-versus-start","Battle, a match starts"},{"battle-versus-match-point","Battle, match point"},{"battle-versus-long","Battle, first to 7"},
  {"battle-pick-won","Battle, won by"},{"battle-pick-lost","Battle, lost by"},
  {"battle-result-win","Battle result, win"},{"battle-result-loss","Battle result, loss being told"},
  {"battle-result-match","Battle result, match won"},{"battle-result-match-lost","Battle result, match lost"},
  {"battle-result-void","Battle result, both claimed"},{"battle-result-later","Battle result, rival out of reach"},
  {"battle-result-match-point","Battle result, now match point"},{"battle-result-long","Battle result, first to 7"},
  {"battle-result-long-match","Battle result, first to 7 won"},
  {"battles","Battles"},{"battles-off","Battles, off"},{"battles-bluetooth","Battles, Bluetooth off"},{"battles-rival","Battles, a rival row"},
  {"battles-match","Battles, match length"},{"battles-match-long","Battles, first to 7"},
  {"battles-more","Battles, second page"},{"battles-back","Battles, Back"},{"rival","Rival"},{"rival-new","Rival, no result yet"},
};
constexpr unsigned BATTLE_COUNT=sizeof(BATTLE_SCREENS)/sizeof(BATTLE_SCREENS[0]);
inline void battleScreen(lgfx::LGFX_Sprite &g,unsigned index,const Look &look,bool flip) {
  using Battle::Outcome;using Battle::Finish;
  const auto &c=look.colors;
  ControlsUI::colors=c;
  const char *id=BATTLE_SCREENS[index].id;
  auto is=[&](const char *name){return !std::strcmp(id,name);};
  auto prefix=[&](const char *name){return !std::strncmp(id,name,std::strlen(name));};
  g.fillScreen(c.background);g.setTextDatum(top_center);g.setTextColor(c.text,c.background);
  if(is("menu-battles")){ControlsUI::list(g,UiText::t(UiText::Text::Menu),ControlsUI::MENU,Controls::MENU_COUNT,Controls::BATTLES_ROW,Controls::MENU_GROUPS);ControlsUI::cornerBattery(g,detail::corner(),true);return;}
  // A stick with some history: Kai most of all, the others in passing.
  const uint32_t KAI=0xd5d21001,MAX=0x9fd52002,LONG=0x1d043003,NEW=0xb1e23c11;
  Battle::Book book;uint16_t tag=0;
  // Each battle takes the next two tags, mine first (one statement each: the
  // order of a call's arguments is unspecified).
  auto begin=[&](uint32_t id,uint16_t mine,uint16_t theirs,uint8_t matchTo=Battle::MATCH_SHORT){
    const uint16_t myTag=++tag;const uint16_t theirTag=++tag;book.begin(id,myTag,theirTag,mine,theirs,matchTo);};
  auto battle=[&](uint32_t id,uint16_t mine,uint16_t theirs,Outcome o,Finish f){begin(id,mine,theirs);if(o!=Outcome::None)book.claim(id,o,f);};
  battle(MAX,5980,6105,Outcome::Lost,Finish::Over);battle(MAX,6230,6012,Outcome::Won,Finish::Spin);book.rename(MAX,"Max");
  battle(LONG,6100,11250,Outcome::Lost,Finish::Xtreme);book.rename(LONG,"SixteenCharsLong");
  battle(NEW,6050,5400,Outcome::None,Finish::Spin);
  // Kai: a match won 4 - 1, and 2 - 1 into the next.
  const struct {Outcome outcome;Finish finish;} kai[]={{Outcome::Won,Finish::Xtreme},{Outcome::Lost,Finish::Spin},{Outcome::Won,Finish::Spin},
    {Outcome::Won,Finish::Over},{Outcome::Lost,Finish::Spin}};
  for(unsigned i=0;i<5;++i)battle(KAI,uint16_t(6000+i*31),uint16_t(6200-i*47),kai[i].outcome,kai[i].finish);
  book.rename(KAI,"Kai");
  if(prefix("battles")) {
    if(is("battles-more"))for(uint32_t i=0;i<5;++i){battle(0x70000000u+i,6000,5900,Outcome::Won,Finish::Spin);}
    const unsigned row=is("battles-rival")?BattleUI::FIRST_RIVAL_ROW:prefix("battles-match")?BattleUI::MATCH_ROW:is("battles-more")?8:
      is("battles-back")?book.count()+BattleUI::FIRST_RIVAL_ROW:0;
    BattleUI::battles(g,book,row,!is("battles-bluetooth"),!is("battles-off"),is("battles-match-long")?Battle::MATCH_LONG:Battle::MATCH_SHORT);
    ControlsUI::cornerBattery(g,detail::corner());return;
  }
  if(is("rival")){BattleUI::rival(g,*book.find(KAI));return;}
  if(is("rival-new")){BattleUI::rival(g,*book.find(NEW));return;}
  // Zed: first to 7, 4 - 2 into the match.
  const uint32_t ZED=0x2e0d4004;
  for(auto o:{Outcome::Won,Outcome::Lost,Outcome::Won}){begin(ZED,6100,6000,Battle::MATCH_LONG);book.claim(ZED,o,Finish::Over);}
  book.rename(ZED,"Zed");
  // The battle on screen: against Kai, 2 - 1 into the match.
  if(is("battle-versus-match-point"))battle(KAI,6100,6000,Outcome::Won,Finish::Spin);
  if(is("battle-versus-start"))battle(KAI,6100,6000,Outcome::Won,Finish::Spin),battle(KAI,6100,6000,Outcome::Won,Finish::Spin);
  begin(KAI,is("battle-versus-slower")?5870:6420,5980);
  uint32_t rival=KAI;
  if(is("battle-versus-wide")){begin(LONG,12480,11250);rival=LONG;}
  if(prefix("battle-versus-long") || prefix("battle-result-long")){begin(ZED,6380,6215,Battle::MATCH_LONG);rival=ZED;}
  if(prefix("battle-versus")) {
    BattleUI::versus(g,c,"Ready",ScreensUI::Tone::Ready,*book.find(rival));
    ScreensUI::level(g,c,WIDTH/2,StickS3Level::forDisplay({StickS3Level::State::Valid,1.4f,.2f,-.35f},flip));
    ControlsUI::footer(g,"A Won  B Lost");
    if(is("battle-versus-skip"))ControlsUI::hold(g,"Skip battle",600,1000);
    return;
  }
  if(is("battle-pick-won")){BattleUI::picker(g,c,*book.find(rival),Outcome::Won,Finish::Spin);return;}
  if(is("battle-pick-lost")){BattleUI::picker(g,c,*book.find(rival),Outcome::Lost,Finish::Burst);return;}
  auto told=BattleUI::Told::Agreed;
  if(is("battle-result-win"))book.claim(KAI,Outcome::Won,Finish::Spin);
  else if(is("battle-result-loss")){book.claim(KAI,Outcome::Lost,Finish::Burst);told=BattleUI::Told::Telling;}
  else if(is("battle-result-match"))book.claim(KAI,Outcome::Won,Finish::Xtreme);
  else if(is("battle-result-match-lost")){book.claim(KAI,Outcome::Lost,Finish::Xtreme);}
  else if(is("battle-result-void")){Battle::Claim both;both.rev=1;both.outcome=Outcome::Void;book.merge(KAI,book.find(KAI)->theirTag,both);}
  else if(is("battle-result-later")){book.claim(KAI,Outcome::Won,Finish::Over);told=BattleUI::Told::Later;}
  else if(is("battle-result-match-point"))book.claim(KAI,Outcome::Won,Finish::Spin);
  else if(is("battle-result-long"))book.claim(ZED,Outcome::Lost,Finish::Burst);
  else if(is("battle-result-long-match"))book.claim(ZED,Outcome::Won,Finish::Xtreme);
  BattleUI::result(g,c,*book.find(rival),told);
}
// The Analysis page's screens, composed as LaunchLabMini.ino's draw() composes
// them: pinned by the render check, and not in the theme preview.
inline constexpr Screen ANALYSIS_SCREENS[]={
  {"analysis-ready","Analysis, ready"},{"analysis-warming","Analysis, mic warming up"},
  {"analysis-early","Analysis, 6 s in"},{"analysis-early-forecast","Analysis, early mic forecast"},
  {"analysis-forecast","Analysis, Fixed Riccati forecast"},{"analysis-word","Analysis, forecast with a word cue"},
  {"analysis-mic-failed","Analysis, mic unavailable"},
  {"analysis-fitting","Analysis, fitting after stop"},{"analysis-fitted","Analysis, fitted"},
  {"analysis-timeout","Analysis, time limit"},{"analysis-save-pending","Analysis, save pending"},
  {"analysis-details","Analysis details"},{"analysis-details-more","Analysis details, second page"},
  {"analysis-details-fitting","Analysis details, fitting"},
};
constexpr unsigned ANALYSIS_COUNT=sizeof(ANALYSIS_SCREENS)/sizeof(ANALYSIS_SCREENS[0]);
namespace detail {
// A 7,300 RPM launch that topples near 600 RPM at 96 s: the shared drag shape.
inline float demoSpin(float t){return t>=96?0.f:600.f+6700.f*std::pow(1.f-t/96.f,1.6f);}
}
inline void analysisScreen(lgfx::LGFX_Sprite &g,unsigned index,const Look &look) {
  using namespace MicTrace;
  const auto &c=look.colors;
  ControlsUI::colors=c;
  const char *id=ANALYSIS_SCREENS[index].id;
  auto is=[&](const char *name){return !std::strcmp(id,name);};
  g.fillScreen(c.background);g.setTextDatum(top_center);g.setTextColor(c.text,c.background);
  constexpr uint32_t LAUNCH=7;constexpr float RPM=7300;constexpr uint64_t START=1000000;
  AnalysisMode::Session session;
  static StoredTrace t;t.clear();
  static Decay::Session input;input=Decay::Session{};
  Decay::Results fit;Decay::Curve curve;
  auto spinTo=[&](float seconds,bool trace){
    if(trace){t.begin(LAUNCH,RPM);
      for(unsigned i=0;seconds>0 && Trace::seconds(i)<=std::min(seconds,float(MAX_READINGS-1)*HOP_S);++i){
        Step s;s.launch=LAUNCH;s.index=uint16_t(i);
        const float at=Trace::seconds(i);
        // Silent for the first 0.8 s, then a reading most hops, with the odd miss.
        const bool heard=at>=.8f && at<95.f && (i%9)!=4;
        s.reason=heard?MicRpm::Reason::Tracking:MicRpm::Reason::WeakHarmonics;
        if(heard)s.rpm=uint16_t(std::lround(detail::demoSpin(at)+(int((i*37)%11)-5)*18));
        t.add(s);
      }}
  };
  auto fitTo=[&](float lastHeard,float end,bool samples){
    fit.launcherRpm=RPM;fit.launcherTime=0;fit.initialRpm=detail::demoSpin(.8f);fit.acousticStart=.8f;fit.lastTime=lastHeard;
    fit.fixedRiccati=true;fit.best=int(Decay::Model::Riccati);fit.sampleCount=400;
    curve.end=end;for(unsigned i=0;i<Decay::CURVE_POINTS;++i)curve.rpm[i]=detail::demoSpin(curve.seconds(i)*96.f/end);
    if(samples){
      input.launcherRpm=RPM;
      for(float at=.8f;at<=lastHeard && input.count<Decay::MAX_SAMPLES;at+=.1f){
        const unsigned i=input.count;
        input.samples[input.count++]={uint32_t(std::lround(at*1000)),detail::demoSpin(at)+float(int((i*37)%11)-5)*22.f,false,(i%23)==7};
      }
    }
  };
  const MicView::Mic mic=is("analysis-warming")?MicView::Mic::Starting:is("analysis-mic-failed")?MicView::Mic::Failed:MicView::Mic::On;
  const char *state="READY";bool ready=true,fitting=false,pending=false;float published=NAN;
  const Decay::Results *fitIn=nullptr;const Decay::Session *inputIn=nullptr;const Decay::Curve *curveIn=nullptr;
  uint64_t now=START;
  if(is("analysis-ready")){}
  else if(is("analysis-warming")){session.begin(LAUNCH,true,RPM,START);now=START+1200000;t.begin(LAUNCH,RPM);}
  else if(is("analysis-early")){session.begin(LAUNCH,true,RPM,START);now=START+6400000;spinTo(6.3f,true);}
  else if(is("analysis-early-forecast")){session.begin(LAUNCH,true,RPM,START);now=START+12300000;spinTo(12.2f,true);published=EarlySpin::finish(t.earlyReadings(),12300);}
  else if(is("analysis-forecast")){
    session.begin(LAUNCH,true,RPM,START);now=START+42500000;spinTo(42.4f,true);
    fitTo(42.4f,97.f,false);fitIn=&fit;curveIn=&curve;published=97.f;
  }
  else if(is("analysis-word")){
    // Into the last three seconds of a ten-second cue cycle: a word, settled.
    session.begin(LAUNCH,true,RPM,START);now=START+57800000;spinTo(57.7f,true);
    fitTo(57.7f,97.f,false);fitIn=&fit;curveIn=&curve;published=97.f;
  }
  else if(is("analysis-mic-failed")){session.begin(LAUNCH,true,RPM,START);now=START+18000000;published=94.131f;}
  else if(is("analysis-fitting")){session.begin(LAUNCH,true,RPM,START);session.stop(START+95400000);now=START+96000000;spinTo(95.f,true);fitting=true;}
  else if(is("analysis-fitted") || is("analysis-details") || is("analysis-details-more") || is("analysis-details-fitting")){
    session.begin(LAUNCH,true,RPM,START);session.stop(START+95400000);now=START+97000000;spinTo(95.f,true);
    fitTo(94.f,95.4f,true);fitIn=&fit;inputIn=&input;curveIn=&curve;
  }
  else if(is("analysis-timeout")){session.begin(LAUNCH,true,RPM,START);session.stop(START+AnalysisMode::Session::LIMIT_US+1000,false);now=START+AnalysisMode::Session::LIMIT_US+2000000;spinTo(120.f,true);}
  else if(is("analysis-save-pending")){session.begin(LAUNCH,true,RPM,START);session.stop(START+95400000);now=START+95500000;spinTo(95.f,true);pending=true;}
  if(is("analysis-details") || is("analysis-details-more") || is("analysis-details-fitting")) {
    session.nextResult();if(is("analysis-details-more"))session.nextResult();
    const auto summary=AnalysisDetails::summarize(curve,RPM,95.4f);
    AnalysisResultUI::draw(g,session,is("analysis-details-fitting")?nullptr:&summary,is("analysis-details-fitting"),false,c);
    return;
  }
  static const LaunchMotion::Trace motion{};
  AnalysisUI::draw(g,session,now,&t,mic,motion,false,false,c,MotionUI::demoCurve(RPM),state,ready,nullptr,
                   fitIn,inputIn,curveIn,fitting,published,pending);
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
      0,0,uint8_t(Controls::BATTERY_ICON_DEFAULT),0,MinRpm::DEFAULT_STEPS,uint8_t(UiText::current())};  // Wipe all and About have no value; Bluetooth is Off
    static_assert(sizeof(values)==Controls::SETTING_COUNT,"a default for every setting");
    static const Milestones::Config defaults;ControlsUI::milestones=&defaults;
    ControlsUI::runtime="0.20.8-sticks3-songs-pm";  // fixed, so a release number never moves a pinned frame
    if(is("bluetooth"))ControlsUI::bluetoothPage(g,0,false,false,"BEYBLADE_TOOL01",0);
    else if(is("bluetooth-enable"))ControlsUI::enableBluetooth(g,true);
    else if(is("bluetooth-on"))ControlsUI::bluetoothPage(g,1,true,true,"BEYBLADE_TOOL01",1);
    else if(is("bluetooth-forget"))ControlsUI::forgetPhones(g,false);
    else if(is("bluetooth-pair"))ControlsUI::comparison(g,123456,false);
    else if(is("bluetooth-beyblade-x") || is("bluetooth-beyblade-x-held")) {
      ControlsUI::PassView v;v.on=v.app=v.phone=true;v.button=is("bluetooth-beyblade-x-held");v.waiting=3;v.synced=2;
      ControlsUI::beybladeX(g,v);
    }
    else if(prefix("menu")){state.menu=is("menu-settings")?Controls::SETTINGS_ROW:is("menu-wear-check")?Controls::WEAR_ROW:is("menu-bluetooth")?Controls::BLUETOOTH_ROW:0;ControlsUI::list(g,UiText::t(UiText::Text::Menu),ControlsUI::MENU,Controls::MENU_COUNT,state.menu,Controls::MENU_GROUPS);}
    else if(is("history-menu"))ControlsUI::list(g,UiText::t(UiText::Text::MenuHistory),ControlsUI::HISTORY,4,0);
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
    // Its widest value.
    else if(is("min-rpm-editor")){state.page=Page::Edit;state.setting=unsigned(Controls::Setting::MinRpm);state.draft=MinRpm::MAX_STEPS;ControlsUI::editor(g,state,false,false);}
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
    else if(is("battery"))ControlsUI::device(g,true,72,3984,true,true,{8388608,2200000,0x330000});
    else if(is("battery-low"))ControlsUI::device(g,true,15,3512,true,false,{8388608,2200000,0x330000});
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
    ControlsUI::cornerBattery(g,detail::corner(is("menu-battery-low")?15:72,is("menu-charging")),prefix("menu"));
  // Main shows it only while charging (Controls::showsChargingBattery).
  if(is("main-charging") || is("main-idle-charging"))ControlsUI::cornerBattery(g,detail::corner(is("main-idle-charging")?100:72,true));
}
}
