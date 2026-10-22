// Actual production prepareMotion/finishMotion/saveHistory/loop blocks are
// extracted by check_motion_pipeline.py; only device peripherals are mocked.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <set>
#include <vector>
static constexpr uint16_t TFT_BLACK=0,TFT_WHITE=0xffff,TFT_DARKGREY=0x7bef,TFT_CYAN=0x07ff,TFT_ORANGE=0xfd20;
static constexpr int top_center=0,top_right=1;
namespace fonts {static int Font4=4;}
namespace lgfx {
class LGFXBase {
public:
  std::vector<std::string> text;
  std::vector<int> body;
  int width()const{return 135;}void setTextDatum(int){}void setTextColor(uint16_t,uint16_t){}
  int textWidth(const char *s,const int*)const{return int(std::string(s).size())*13;}
  void drawString(const char *s,int,int,int){text.emplace_back(s);}
  void drawLine(int a,int b,int c,int d,uint16_t ink){if(ink==0x5dff || ink==TFT_WHITE || ink==0x7e9f)body.insert(body.end(),{1,a,b,c,d,ink});}void drawFastHLine(int,int,int,uint16_t){}
  void drawFastVLine(int,int,int,uint16_t){}void drawRect(int,int,int,int,uint16_t){}
  void drawCircle(int,int,int,uint16_t){}void fillCircle(int,int,int,uint16_t){}
  void fillTriangle(int a,int b,int c,int d,int e,int f,uint16_t ink){body.insert(body.end(),{2,a,b,c,d,e,f,ink});}
  bool contains(const char *s)const{return std::find(text.begin(),text.end(),s)!=text.end();}
};
}
#include "../LaunchLabMini/accel_range.h"
#include "../LaunchLabMini/motion_ui.h"
#include "../LaunchLabMini/theme_library.h"
#include "../LaunchLabMini/launch_feedback.h"
#include "../LaunchLabMini/appearance_store.h"
#include "../LaunchLabMini/milestones.h"
#include "../LaunchLabMini/rpm_estimator.h"
#include "../LaunchLabMini/sensor_profile.h"
#include "../LaunchLabMini/history_checkpoint.h"
static LaunchMotion::Trace syntheticPipelineTrace(uint32_t durationMs=75) {
  LaunchMotion::Trace t;t.quality=LaunchMotion::Quality::Valid;t.fused=true;t.number=7;t.rpm=6000;t.durationMs=durationMs;t.count=48;t.gravity[2]=1;
  for(unsigned i=0;i<t.count;++i) {
    auto &p=t.points[i];p.ms=i<12?-500+int(500*i/12):i<36?int(durationMs*(i-12)/23):durationMs+int(250*(i-35)/12);
    LaunchMotion::Quaternion q;const float rate[3]={30,90,45};q.integrate(rate,(p.ms+500)*.001f);
    p.q[0]=std::lround(q.w*16384);p.q[1]=std::lround(q.x*16384);p.q[2]=std::lround(q.y*16384);p.q[3]=std::lround(q.z*16384);
  }
  LaunchMotion::recordLevels(t);return t;
}
#include "motion_pipeline_trace.h"
static uint64_t hostUs=0,drawCostUs=24000,saveCostUs=178432;
static uint32_t millis(){return uint32_t(hostUs/1000);}
static uint64_t esp_timer_get_time(){return hostUs;}
static void advance(uint64_t us){hostUs+=us;}
struct Snapshot {
  AnalogTachometer::Phase phase=HAS_HISTORY_COALESCING?AnalogTachometer::Phase::Ready:AnalogTachometer::Phase::Hold;
  uint64_t burstStartHostUs=0,burstEndHostUs=0;
  bool valid=true,signalFault=false;StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;RpmEstimator::Mode estimator=RpmEstimator::DEFAULT_MODE;
  float resultRpm=6122.449f;uint32_t launches=1;
};
enum class Action {CheckpointBegin,CheckpointEnd,HistoryIdleCheckpointBegin,IdleCheckpointBegin};
struct SerialFake {
  template<typename... T>void printf(const char*,T...){}void println(const char*){}
}Serial;
struct PracticeFake {
  struct Record {uint32_t number=463;}record;
  const Record *recent()const{return &record;}uint32_t generation()const{return record.number;}
}practice;
struct StoreFake {
  bool pending_=false;unsigned attempts=0;uint64_t firstSaveUs=0;
  bool pending(const PracticeFake&)const{return pending_;}bool ready()const{return true;}
  bool save(const PracticeFake&){if(!attempts)firstSaveUs=hostUs;++attempts;advance(saveCostUs);pending_=false;return true;}
}practiceStore;
static LaunchMotion::Trace completedTrace,latestMotion,referenceMotion,demoMotion,demoReference;
struct ImuFake {
  struct State {uint64_t fusedUs;};
  State snapshot(){return {hostUs};}
  unsigned failCaptures=0;
  bool capture(uint64_t,uint64_t,float rpm,uint32_t number,LaunchMotion::Trace &out){
    if(failCaptures){--failCaptures;return false;}
    out=completedTrace;out.rpm=rpm;out.number=number;return true;}
}imuAcquisition;
struct AcquisitionFake {Snapshot state;Snapshot snapshot(){return state;}bool request(Action,uint32_t=0){return true;}}acquisition;
struct InactivityFake {void touch(uint32_t){}}inactivity;
namespace PracticeUI {enum class View {Other,Battery};}
static PracticeUI::View historyView=PracticeUI::View::Other;
static LaunchFeedback launchFeedback;
static Appearance::Config appearance;static Appearance::Bests personalBests;static AppearanceStore<Appearance::Bests> bestStore;static Appearance::Celebration bestEffect,editorEffect;static bool effectWasMoving=false;
static Milestones::Config milestones;
// Random's pick is device-side (esp_random); a fixed pick keeps the fixture deterministic.
static Appearance::Effect celebrationEffect(){return appearance.effect==Appearance::Effect::Random?Appearance::Effect::Orbit:appearance.effect;}
// Single-effect rows here; pools are covered by test_milestones and check_best_sound_pipeline.
static const Appearance::Effect *milestonePicks(){return nullptr;}
static Palette::Colors ui(){return Themes::builtIn(appearance.theme);}
static StickS3Level level;static StickS3Level::Reading drawnLevel;
static uint64_t motionStart=0,motionEnd=0;
static uint32_t motionDrops=0,lastAcceptedAt=0,historyPendingSince=0,lastSaveTry=0,maxSaveUs=0,saveCount=0,saveErrors=0,lastDraw=0,newSessionAt=0;
static bool motionPending=false,motionDemo=false,historyPage=false,diagnostics=false,demo=false,tournamentMode=false,dirty=false,stress=false,saveError=false,screenReady=true;
static constexpr uint64_t MOTION_CAPTURE_RETRY_US=3000000;static uint32_t motionCaptureRetries=0;
static bool displayFlipped=false;
static bool visibleDisplayFlipped(){return displayFlipped;}
static bool restorePreview=false;static uint32_t tournamentHintUntil=0;
static void previewSetting(){}
struct {struct {bool isPressed(){return false;}} BtnA,BtnB;} M5;
static void pollBattery(uint32_t,const Snapshot&){}
// This rendering fixture has a healthy owner. Missing-owner recovery is
// exercised with the actual startup/retry functions in check_imu_startup.py.
static void retryMotion(uint32_t,const Snapshot&){}
static void pollUsbAwake(uint32_t,const Snapshot&,bool=false){}static void tickPowerLog(uint32_t,const Snapshot&){}static void autoPowerOff(uint32_t,const Snapshot&){}
// Side-key power-off has its own production harness (check_power_key_pipeline.py).
static void powerKeyOff(uint32_t,const Snapshot&){}
// Display electrical policy is exercised separately by check_idle_power_pipeline.
static void tickDisplayPower(uint32_t,const Snapshot&){}
// Sound playback is device-only; its scheduling has no effect on capture or drawing here.
static void tickSound(const Snapshot&){}
// Settings saves and orphaned-checkpoint release have their own production
// harness with the actual ADC owner (check_save_paths.py).
static void saveSettings(uint32_t,const Snapshot&){}static void releaseOrphanCheckpoint(uint32_t,const Snapshot&){}
static int actualSourceTime(const LaunchMotion::Trace&,float);
static float actualProgress(uint32_t);
struct Frame {uint64_t drawnUs,presentedUs;int sourceMs;bool replay,pending,unavailable;std::string reason;std::vector<int> body;std::string label;};
static std::vector<Frame> frames;
static void recordFrame(const lgfx::LGFXBase &g,uint32_t replayFrameElapsed) {
  const bool unavailable=g.contains("No tilt data") || g.contains("Tilt warming up") || g.contains("Tilt incomplete") || g.contains("Tilt out of range") || g.contains("Pull too long");
  std::string reason;for(const char *label:{"Tilt warming up","Tilt incomplete","Tilt out of range","Pull too long","No tilt data"})if(g.contains(label))reason=label;
  frames.push_back({hostUs,hostUs+drawCostUs,actualSourceTime(latestMotion,actualProgress(replayFrameElapsed)),g.contains("Tilt replay"),g.contains("Capturing"),unavailable,reason,g.body,g.text.empty()?"":g.text.front()});
}
#include "motion_pipeline_wiring.h"
static unsigned failures=0,cases=0;
static void require(bool ok,const char *s){if(!ok){++failures;std::cerr<<"FAIL: "<<s<<'\n';}}
static void reset(uint64_t startUs=5000000) {
  hostUs=startUs;acquisition={};launchFeedback={};bestEffect={};personalBests={};bestStore={};effectWasMoving=false;completedTrace=pipelineTrace();latestMotion={};practiceStore={};
  milestones={};appearance={};
  motionPending=motionDemo=historyPage=diagnostics=demo=tournamentMode=dirty=stress=saveError=false;screenReady=true;
  drawCostUs=24000;lastDraw=millis();lastSaveTry=maxSaveUs=saveCount=saveErrors=motionDrops=newSessionAt=lastAcceptedAt=historyPendingSince=0;frames.clear();
}
static Snapshot event() {Snapshot s;s.burstEndHostUs=hostUs;s.burstStartHostUs=hostUs-completedTrace.durationMs*1000;return s;}
static std::vector<Frame> replayFrames(){std::vector<Frame> out;for(const auto &f:frames)if(f.replay)out.push_back(f);return out;}
static void makeSaveDue(){practiceStore.pending_=true;
#if HAS_HISTORY_COALESCING
  historyPendingSince=millis()-HISTORY_COALESCE_MS;lastAcceptedAt=historyPendingSince;
#endif
}
static void readyAfter(unsigned pendingMs,bool historyDirty,bool forceOverdue=true) {
  auto e=event();practiceStore.pending_=historyDirty;prepareMotion(e);if(historyDirty && forceOverdue)makeSaveDue();productionStep();
  require(motionPending && frames.back().pending,"pending trace must draw Capturing, not arm/consume replay");
  hostUs=e.burstEndHostUs+pendingMs*1000;practiceStore.pending_=historyDirty;productionStep();
  require(!motionPending && latestMotion.valid(),"actual captureReady/finish flow must complete the valid trace");
}
static void checkTimeline() {
  const auto shown=replayFrames();require(!shown.empty(),"a completed valid capture must produce a replay frame");if(shown.empty())return;
  const int first=latestMotion.points[0].ms,last=latestMotion.points[latestMotion.count-1].ms;
  require(shown.front().sourceMs==first,"first completed visible frame must display first recorded context pose");
  const uint64_t firstPresentedUs=shown.front().presentedUs;
  for(unsigned i=1;i<shown.size();++i) {
    const int elapsed=int((shown[i].drawnUs-firstPresentedUs)/1000);
    require(shown[i].sourceMs==std::min(last,first+elapsed),"each frame must select actual full-context milliseconds since first presented frame");
  }
}
static void completeReplay() {for(unsigned i=0;i<250;++i){advance(50000);productionStep();}}
int main() {
  for(unsigned pendingMs:{160u,260u,400u,900u})for(bool historyDirty:{false,true}) {
    reset();readyAfter(pendingMs,historyDirty);
    completeReplay();checkTimeline();
    if(historyDirty)require(practiceStore.attempts==1,"history save must resume after visible replay, without permanent starvation");
    auto shown=replayFrames();unsigned moving=0;for(const auto &f:shown)if(f.sourceMs<latestMotion.points[latestMotion.count-1].ms)++moving;
    require(moving>=8,"recorded full context at 1x must offer multiple frames for the short 75 ms optical pull");
    std::set<std::vector<int>> distinct;for(const auto &f:shown)distinct.insert(f.body);
    require(distinct.size()>=3,"actual rendered body pixel geometry must show at least three distinct poses");
    require(std::any_of(shown.begin()+1,shown.end()-1,[&](const Frame &f){return f.body!=shown.front().body && f.body!=shown.back().body;}),"actual rendered body must include an intermediate pose distinct from both endpoints");
    std::cout<<"CASE pending_ms="<<pendingMs<<" history_write="<<historyDirty<<" completed_replay_frames="<<shown.size()<<" distinct_body_pixel_geometry="<<distinct.size()<<" first_source_ms="<<shown.front().sourceMs<<" save_attempts="<<practiceStore.attempts<<'\n';++cases;
  }
  // A write becoming due on the next frame must not wipe out the replay.
  // A transient capture failure (ring copy allocation or busy ring gate) is
  // retried while the samples are buffered; past the deadline it gives up.
  reset();{auto e=event();prepareMotion(e);hostUs=e.burstEndHostUs+300000;imuAcquisition.failCaptures=2;
    productionStep();require(motionPending,"first failed capture stays pending");
    productionStep();require(motionPending,"second failed capture stays pending");
    productionStep();require(!motionPending && latestMotion.valid() && imuAcquisition.failCaptures==0,"retried capture completes the valid trace");}
  reset();{auto e=event();prepareMotion(e);hostUs=e.burstEndHostUs+300000;imuAcquisition.failCaptures=1000;
    productionStep();require(motionPending,"failing capture retried inside the retention window");
    hostUs=e.burstEndHostUs+MOTION_CAPTURE_RETRY_US+1;productionStep();
    require(!motionPending && !latestMotion.valid(),"capture abandoned only after the retention deadline");imuAcquisition.failCaptures=0;}
  reset();readyAfter(300,false);makeSaveDue();advance(50000);productionStep();
  require(practiceStore.attempts==0,"next-frame history write must be deferred through active replay");completeReplay();checkTimeline();require(practiceStore.attempts==1,"next-frame deferred write eventually completes");++cases;
  // External scheduling stalls skip ahead at 1x, rather than slowing playback.
  reset();readyAfter(300,false);advance(700000);productionStep();advance(50000);productionStep();checkTimeline();++cases;
  // Different draw/transfer costs change frame availability, never playback
  // speed. Keep the first presentation timestamp separate from later costs.
  reset();readyAfter(300,false);const unsigned costs[]={1000,7000,24000,80000,180000};
  for(unsigned i=0;i<20;++i){drawCostUs=costs[i%5];advance(50000);productionStep();}
  checkTimeline();require(replayFrames().back().sourceMs==latestMotion.points[latestMotion.count-1].ms,"variable drawing cost must still deliver the recorded endpoint");++cases;
  // Manual A recall begins a new visible epoch, including after a write is due.
  reset();readyAfter(300,false);completeReplay();frames.clear();toggleFeedback();makeSaveDue();productionStep();
  require(replayFrames().front().sourceMs==latestMotion.points[0].ms,"manual A first pose must survive a due pre-presentation write");completeReplay();checkTimeline();++cases;
  // A new pull must reset the completed-frame epoch and protect its pending phase.
  reset();readyAfter(300,false);advance(100000);productionStep();frames.clear();readyAfter(300,true);completeReplay();checkTimeline();++cases;
  reset((uint64_t(UINT32_MAX)-350)*1000);readyAfter(300,true);completeReplay();checkTimeline();++cases;
  reset();screenReady=false;readyAfter(300,true);completeReplay();checkTimeline();require(practiceStore.attempts==1,"direct-display fallback must arm replay and release persistence");++cases;
  reset();completedTrace=syntheticPipelineTrace(2500);readyAfter(300,HAS_HISTORY_COALESCING,false);if(!HAS_HISTORY_COALESCING)makeSaveDue();advance(2200000);productionStep();require(practiceStore.attempts==0,"long 3250 ms recorded context must protect playback beyond the 2 s soft coalescing boundary");completeReplay();checkTimeline();require(practiceStore.attempts==1,"long replay protection must eventually permit persistence");++cases;
  // Rendering that begins before the endpoint and presents after it must not
  // mistake its completion timestamp for an endpoint pose already displayed.
  reset();readyAfter(300,false);const auto span=latestMotion.points[latestMotion.count-1].ms-latestMotion.points[0].ms;
  advance((span-12)*1000);productionStep();makeSaveDue();
  require(replayFrames().back().sourceMs==latestMotion.points[latestMotion.count-1].ms-12,"pre-endpoint draw must contain its sampled pre-end pose");
  advance(50000);productionStep();require(practiceStore.attempts==0,"history must wait while the endpoint pose is still undelivered despite presentation time crossing end");
  require(replayFrames().back().sourceMs==latestMotion.points[latestMotion.count-1].ms,"scheduler must deliver the endpoint after a draw crosses its timestamp");
  advance(50000);productionStep();require(practiceStore.attempts==0,"checkpoint must protect the full recap after endpoint presentation");completeReplay();require(practiceStore.attempts==1,"checkpoint resumes after recap and quiet interval");checkTimeline();++cases;
#if HAS_HISTORY_COALESCING
  // Exercise the actual normal ten-second quiet path separately from the overdue
  // pre-first-frame writes above. The short clip ends before persistence is due.
  reset();readyAfter(300,true,false);const auto accepted=lastAcceptedAt;
  while(millis()-accepted<HISTORY_COALESCE_MS-100){advance(50000);productionStep();}
  require(practiceStore.attempts==0,"normal pending history must coalesce until ten quiet seconds after the latest accepted pull");
  require(replayFrames().back().sourceMs==latestMotion.points[latestMotion.count-1].ms,"short replay must show its endpoint before normal coalesced write");
  while(practiceStore.attempts==0){advance(50000);productionStep();}
  require(uint32_t(practiceStore.firstSaveUs/1000)-accepted>=HISTORY_COALESCE_MS,"actual normal save must meet its source coalescing interval");checkTimeline();++cases;
#endif
  reset();for(auto &p:completedTrace.points){p.q[0]=16384;p.q[1]=p.q[2]=p.q[3]=0;}LaunchMotion::recordLevels(completedTrace);readyAfter(300,false);completeReplay();std::set<std::vector<int>> constant;for(const auto &f:replayFrames())constant.insert(f.body);require(constant.size()==1,"constant recorded poses must not acquire invented body animation");++cases;
  // Guard remains bounded and excludes hidden and invalid recaps. Gap/clipping
  // rejection is intentionally retained, never bypassed to invent tilt data.
  for(auto quality:{LaunchMotion::Quality::Gap,LaunchMotion::Quality::Clipped,LaunchMotion::Quality::Warming}) {
    reset();completedTrace.quality=quality;auto e=event();prepareMotion(e);hostUs+=300000;makeSaveDue();productionStep();
    require(!latestMotion.valid() && replayFrames().empty() && frames.back().unavailable,"invalid capture must remain No tilt data");
    const char *expected=quality==LaunchMotion::Quality::Gap?"Tilt incomplete":quality==LaunchMotion::Quality::Clipped?"Tilt out of range":"Tilt warming up";
    require(frames.back().reason==expected,"rejected trace must identify the actual capture reason without weakening rejection");
    require(practiceStore.attempts==0,"unavailable recap must still protect the displayed result");completeReplay();require(practiceStore.attempts==1,"invalid capture must not starve history persistence");++cases;
  }
  reset();readyAfter(300,false);historyPage=true;makeSaveDue();advance(50000);productionStep();require(practiceStore.attempts==0,"an active recap timer defers persistence");completeReplay();require(practiceStore.attempts==1,"hidden recap timer eventually releases persistence");++cases;
  reset();readyAfter(300,false);makeSaveDue();advance(5000001);productionStep();require(practiceStore.attempts==1,"five-second expiry must release persistence protection");++cases;
  for(unsigned page=0;page<3;++page) {
    reset();historyPage=page==0;diagnostics=page==1;tournamentMode=page==2;
    auto e=event();prepareMotion(e);hostUs+=300000;productionStep();
    require(!motionPending && latestMotion.valid(),"hidden page must still finish tilt recording");
    require(historyPage==(page==0) && diagnostics==(page==1) && tournamentMode==(page==2),"accepted pull must preserve navigation and tournament");
    require(!launchFeedback.shown() && replayFrames().empty(),"hidden page must never auto-open recap or reveal tournament results");++cases;
  }
  // Real per-profile records and presentation token through exact sketch
  // prepare/finish/loop/save handlers, with flash faults and another pull.
  reset();Preferences::data.clear();assert(bestStore.begin(personalBests,"pipeline-best","best0","best1"));
  readyAfter(300,false);require(!bestEffect.badge() && bestStore.pending(personalBests),"first accepted pull establishes a silent baseline without flash writes");
  auto newer=event();newer.resultRpm=8120;prepareMotion(newer);productionStep();require(!bestEffect.badge(),"pending capture must never show a best badge");
  advance(300000);productionStep();require(bestEffect.badge() && bestEffect.moving(millis()) && bestEffect.effect()==celebrationEffect(),"strict higher accepted RPM starts once after completed capture is presented");
  // Effects run about 5 s from the recap's first frame: the recap stays up
  // past its own 5 s until the celebration ends, then closes.
  require(Appearance::duration(celebrationEffect())>=4800,"the 0.16 effects run about five seconds");
  advance(4800000);productionStep();require(launchFeedback.shown() && bestEffect.moving(millis()),"the recap stays up while its celebration plays past five seconds");
  advance(Appearance::duration(celebrationEffect())*1000ull-4800000+100000);productionStep();
  require(launchFeedback.shown() && bestEffect.badge() && !bestEffect.moving(millis()),"bounded animation ends but static label remains");
  advance(1000000);productionStep();require(!launchFeedback.shown(),"the recap closes a second after its celebration ends");
  toggleFeedback();toggleFeedback();require(!bestEffect.badge(),"manual replay cannot retrigger the best presentation token");
  prepareMotion(newer);advance(300000);productionStep();require(!bestEffect.badge(),"ties cannot celebrate again");
  newer.resultRpm=8200;prepareMotion(newer);advance(300000);productionStep();require(bestEffect.badge(),"next strict best can celebrate");
  acquisition.state.phase=AnalogTachometer::Phase::Launch;const auto before=frames.size();productionStep();
  require(!bestEffect.badge() && frames.size()==before,"new pull cancels effect immediately while existing LCD freeze guard remains");
  acquisition.state.phase=AnalogTachometer::Phase::Ready;productionStep();completeReplay();
  require(!bestStore.pending(personalBests),"PB-only pending record flushes after the ordinary quiet interval");
  Appearance::Bests reboot;AppearanceStore<Appearance::Bests> restored;
  require(restored.begin(reboot,"pipeline-best","best0","best1") &&
    reboot.rpm[unsigned(StickS3SensorProfile::DEFAULT_MODE)*2+unsigned(RpmEstimator::DEFAULT_MODE)]==8200,"verified record survives reboot");
  require(!reboot.observe(unsigned(StickS3SensorProfile::DEFAULT_MODE),unsigned(RpmEstimator::DEFAULT_MODE),true,8200),"restored tie stays silent after reboot");
  ++cases;
  reset();Preferences::data.clear();assert(bestStore.begin(personalBests,"pipeline-retry","best0","best1"));readyAfter(300,false);
  launchFeedback.clear();lastAcceptedAt=millis()-10000;Preferences::failWrite=true;saveHistory(millis(),acquisition.snapshot());
  const auto failedAt=lastSaveTry;require(saveError && bestStore.pending(personalBests),"PB write failure retains pending RAM best");
  advance(4000000);saveHistory(millis(),acquisition.snapshot());require(lastSaveTry==failedAt,"PB retry obeys existing five-second failure backoff");
  Preferences::failWrite=false;advance(1000000);saveHistory(millis(),acquisition.snapshot());require(!saveError && !bestStore.pending(personalBests),"PB write recovers at guarded quiet checkpoint");++cases;
  // RPM milestones through the same production handlers: the highest
  // threshold reached plays its effect and labels the recap; a personal best
  // keeps NEW BEST; one animation per pull; every pull that reaches one.
  {
    using Appearance::Effect;using Milestones::Mode;
    struct Pull {float rpm;bool shown;bool best;Effect effect;const char *label;};
    auto play=[&](float rpm){auto e=event();e.resultRpm=rpm;prepareMotion(e);advance(300000);productionStep();};
    auto check=[&](const Pull &p,const char *mode){
      char what[96];snprintf(what,sizeof(what),"%s: %.0f RPM",mode,p.rpm);
      require(bestEffect.badge()==p.shown,what);
      if(!p.shown){require(frames.back().label=="Last launch",what);return;}
      require(bestEffect.best()==p.best && bestEffect.effect()==p.effect,what);
      require(p.label && frames.back().label==p.label,what);
    };
    for(Mode mode:{Mode::Best,Mode::Milestones,Mode::Both}) {
      reset();Preferences::data.clear();assert(bestStore.begin(personalBests,"pipeline-mile","best0","best1"));
      appearance.effect=Effect::Orbit;assert(milestones.select(mode));
      play(8000); // The first pull only sets the personal-best baseline.
      require(!bestEffect.badge(),"baseline pull is silent");
      const bool m=mode!=Mode::Best,b=mode!=Mode::Milestones;
      const Pull pulls[]={
        {8500,true,true,b?Effect::Orbit:Effect::Off,"NEW BEST"},          // a best below every milestone
        {8400,false,false,Effect::Off,nullptr},                           // neither
        {9000,true,true,m?Effect::Sparkles:Effect::Orbit,"NEW BEST"},      // both: the milestone's effect, the best's label
        {12300,true,true,m?Effect::Flames:Effect::Orbit,"NEW BEST"},       // the highest reached is 12K
        {9600,m,false,Effect::Sparkles,"MILESTONE 9K"},                   // a milestone again, not a best
        {15500,true,true,m?Effect::Slash:Effect::Orbit,"NEW BEST"},       // the top milestone
        {14999.4f,m,false,Effect::Lightning,"MILESTONE 14K"},              // shows 14999: not yet 15K
        {14999.5f,m,false,Effect::Slash,"MILESTONE 15K"},                 // shows 15000, rounding half to even
      };
      for(const auto &p:pulls){play(p.rpm);check(p,Milestones::name(mode));}
      // Hidden pages never celebrate, milestone or not.
      tournamentMode=true;play(16000);require(!bestEffect.badge(),"tournament hides milestones");tournamentMode=false;
      ++cases;
    }
  }
  std::cout<<(failures?"FAIL":"PASS")<<": "<<cases<<" production pending/finish/NVS/draw scenarios, optical="<<completedTrace.durationMs<<"ms/context="<<completedTrace.points[completedTrace.count-1].ms-completedTrace.points[0].ms<<"ms, 178432us writes, 24ms draw, next-frame write, 700ms stall, manual A, new pull, wrap, hidden/invalid/timeout; failures="<<failures<<'\n';
  return failures?1:0;
}
