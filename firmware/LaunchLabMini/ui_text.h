#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The words the screen draws, by key, so the UI can be shown in more than one
// language (I18N). English is the reference: every key has its English text
// here, the tests and HIL run in English, and a language without a key falls
// back to it. Only screen text goes through this table: Serial, HIL, BLE
// payloads, NVS keys and backups keep their own fixed English names (for
// example MountOrientation::label(), Acquisition's stateName() and
// ControlsUI::protocolValue()), so a language never changes a protocol.
//
// Each text keeps the width and the font the English one is drawn in: a
// translation has to fit the same place (Font 1 is 6 px a glyph; the body
// keeps x 10..125, the footer the whole 135 px).
//
// The tables live in flash: constexpr arrays of const pointers to literals.
namespace UiText {

// X(key, English text). Grouped by screen; printf formats keep their
// arguments in the order the comment names.
#define LAUNCHLAB_UI_TEXT(X) \
  /* Shared words and footers */ \
  X(Back,"Back") X(On,"On") X(Off,"Off") X(Cancel,"Cancel") X(Menu,"Menu") \
  X(FooterOpenNext,"A Open  B Next") X(FooterBackNext,"A Back  B Next") X(FooterEditNext,"A Edit  B Next") \
  X(FooterChooseNext,"A Choose  B Next") X(FooterApplyNext,"A Apply  B Next") X(FooterBack,"A Back") X(FooterDone,"A Done") \
  X(FooterTurnOffNext,"A Turn off  B Next") X(FooterTurnOnNext,"A Turn on  B Next") \
  X(FooterViewExit,"B View  Hold A Exit") X(FooterMainMenu,"A Main  B Menu") \
  X(FooterWonLost,"A Won  B Lost") X(FooterRecapMenu,"A Recap  B Menu") \
  X(HoldEnterTournament,"Enter tournament") X(HoldExitTournament,"Exit tournament") \
  X(HoldSkipBattle,"Skip battle") X(HoldOk,"OK") X(HoldEnterAnalysis,"Enter analysis") X(HoldExitAnalysis,"Exit analysis") \
  X(HoldCancelChange,"Cancel change") \
  /* Menu rows (Controls::MenuRow) */ \
  X(MenuHistory,"History") X(MenuNewSession,"New session") X(MenuTournament,"Tournament") X(MenuBattles,"Battles") \
  X(MenuWearCheck,"String wear check") X(MenuAnalysis,"Analysis") X(MenuDevice,"Device") X(MenuBluetooth,"Bluetooth") \
  X(MenuSettings,"Settings") \
  /* History menu rows */ \
  X(HistoryLaunches,"Launches") X(HistoryPullTrend,"Pull trend") X(HistorySessionTrend,"Session trend") \
  /* Settings rows (Controls::Setting) */ \
  X(SettingSleep,"Sleep after") X(SettingRpm,"RPM method") X(SettingBrightness,"Brightness") X(SettingFlip,"Flip display") \
  X(SettingSensor,"Sensor profile") X(SettingTheme,"Theme") X(SettingBestEffect,"Best effect") X(SettingCelebrate,"Celebrate") \
  X(SettingSound,"Sound") X(SettingMount,"Mounting") X(SettingShakeWake,"Shake to wake") X(SettingWipeAll,"Wipe all") \
  X(SettingAbout,"About") X(SettingBatteryIcon,"Battery icon") X(SettingBluetooth,"Bluetooth") X(SettingMinRpm,"Min RPM") X(SettingLanguage,"Language") \
  /* Settings values */ \
  X(ValueMinutes,"%u min") /* minutes */ \
  X(ValueRpmThreeTurn,"3-turn average") X(ValueRpmOneTurn,"1-turn peak") \
  X(ValueSensorTcrt,"TCRT5000") X(ValueSensorStandard,"QRE / Standard") \
  X(ValueRandom,"Random") \
  X(ValueCelebrateBest,"New best") X(ValueCelebrateMilestones,"Milestones") X(ValueCelebrateBoth,"Both") \
  X(ValueSoundLow,"Low") X(ValueSoundHigh,"High") \
  X(ValueMountTop,"Top") X(ValueMountSidecar,"Sidecar") \
  X(ValueWipeAll,"Erase device data") X(ValueFirmware,"Firmware %.*s") /* release length, runtime */ \
  /* Settings list and editor */ \
  X(SettingsNextMounting,"B Next: mounting") X(SettingsNextAppearance,"B Next: appearance") \
  X(EditHoldCancels,"Hold B cancels") X(EditNextOption,"B next option ->") \
  X(EditNewSession1,"Next pull starts") X(EditNewSession2,"a new session") \
  X(EditBest1,"Each new personal") X(EditBest2,"best celebrates") \
  X(EditOneMilestone,"1 milestone, %s") /* RPM */ X(EditSteps,"%u steps %s-%s") /* steps, low RPM, high RPM */ \
  X(EditAndBests,"and new bests") X(EditTableOnline,"Edit table online") \
  X(EditMinRpm1,"Slower pulls and") X(EditMinRpm2,"rewinds not saved") \
  X(EditMount1,"Align level + replay") X(EditMount2,"to your mount") \
  X(EditFailed,"Could not apply") \
  /* New session */ \
  X(SessionKeep,"Keep past launches") X(SessionStart,"Start") \
  X(Restarting,"Saving, restarting") \
  /* Bluetooth */ \
  X(BluetoothOnExperimental,"On, experimental") X(BluetoothAppearsAs,"Appears as") X(BluetoothRadioOff,"Radio off") \
  X(BluetoothPhoneConnected,"Phone connected") X(BluetoothNoPhone,"No phone connected") \
  X(BluetoothPairedOne,"%d phone paired") X(BluetoothPairedOther,"%d phones paired") /* count */ \
  X(BluetoothBeybladeX,"Beyblade X") X(BluetoothForget,"Forget phones") \
  X(PassBluetoothOff,"Bluetooth is off") X(PassTournament,"Tournament: no RPM") X(PassAppConnected,"App connected") \
  X(PassWaiting,"Waiting for app") X(PassButton,"PASS") X(PassPressed,"pressed") X(PassHoldA,"hold A") \
  X(PassRegister1,"To register a pass,") X(PassRegister2,"hold A when asked") \
  X(PassToSync,"To sync: %u") X(PassLastSync,"Last sync: %u") /* launches */ X(PassFooter,"Hold A Button  B Back") \
  X(ForgetAgain,"Pair again afterwards") X(ForgetConfirm,"Forget") \
  X(EnableExperimental,"Experimental") X(EnableTested,"Still being tested.") X(EnableBattery1,"Uses more battery") \
  X(EnableBattery2,"while it is on.") X(EnableTurnOn,"Turn on") \
  X(PairTitle,"Pair phone?") X(PairCheck,"Check it on the phone") X(PairSame,"Same number on phone?") \
  X(PairReject,"B reject") X(PairConfirmReject,"A confirm, B reject") \
  /* Device and About */ \
  X(DeviceBattery,"Battery") X(DeviceNoReading,"Reading unavailable") X(DeviceChargeUnknown,"Charge status unknown") \
  X(DeviceCharging,"Charging") X(DeviceNotCharging,"Not charging") X(DeviceFlash,"Flash") \
  X(DeviceTotal,"Total %.2f MiB") X(DeviceTotalUnknown,"Total unavailable") \
  X(DeviceApp,"App %.2f/%.2f MiB") /* app, slot */ X(DeviceAppUnknown,"App unavailable") \
  X(DeviceAppFree,"App free %.2f MiB") X(DeviceAppFreeUnknown,"App free unavailable") X(DeviceAppSlot,"App = firmware slot") \
  X(AboutFirmware,"Firmware") X(AboutDeviceId,"Device ID") X(AboutPulls,"Pulls saved") \
  /* Wipe all */ \
  X(WipeErased,"Data erased") X(WipeRelease,"Release buttons") X(WipeToRestart,"to restart") X(WipeRestartAfter,"Restart after release") \
  X(WipeIncomplete,"Wipe incomplete") X(WipeReview,"Review and retry") X(WipeToFinish,"to finish wiping") \
  X(WipeFooterReview,"A Review  B Restart") X(WipeStep,"Step %u of 3") /* step */ \
  X(WipeErase,"Erase this device?") X(WipeLaunches,"All launches") X(WipeBests,"Personal bests") \
  X(WipeTilts,"Tilt recordings") X(WipeSettings,"All settings") X(WipeFooterContinue,"A Continue  B Cancel") \
  X(WipeUndone,"This can't be undone.") X(WipeBackUp,"Back up first") X(WipeEverything,"Erase everything?") \
  X(WipeFooterConfirm,"A Confirm  B Cancel") X(WipeHold,"Hold A for 3 sec") X(WipeReleaseStop,"Release to stop") \
  X(WipeBCancels,"B cancels") X(WipeFooterHold,"Hold A  B Cancel") X(WipeRetry,"Wipe needs retry") X(WipeIdle,"Try again when idle") \
  /* String wear check */ \
  X(WearTopMount,"Top mount") X(WearSidecarMount,"Sidecar mount") X(WearSidecarInstall,"Sidecar install?") X(WearTopInstall,"Top install?") \
  X(WearHealth,"AVG RETRACT %u%%") /* percent */ X(WearRetraction,"AVG RETRACTION") X(WearMedian,"RPM  (med %ld)") /* RPM */ \
  X(WearLevel,"Level") X(WearDown,"Down") X(WearUp,"Up") \
  X(WearStage,"%s: %ld RPM") X(WearStageWide,"%s: %ld") /* stage, RPM */ X(WearFooterDone,"A Again  Hold B Back") \
  X(WearRetryLevel,"Retry: hold 0-12 deg") X(WearRetryVertical,"Retry: hold 90 deg") X(WearRetryFlip,"Retry: flip 180 deg") \
  X(WearRetryImu,"Retry: wait for IMU") X(WearRetryLonger,"Retry: hold longer") X(WearRetryMissed,"Retry: not measured") \
  X(WearLast,"Last: %ld RPM") /* RPM */ X(WearIntro,"Pull, hold, let go") X(WearDegrees,"%.1f deg") X(WearNoDegrees,"--.- deg") \
  X(WearSettingsMount,"Settings > Mounting") X(WearFooter,"A Reset  Hold B Back") \
  /* the other screens */ \
  X(MainNewSession,"New session ready") \
  X(MainCheckSensor,"Check sensor") \
  X(MainRecording,"RECORDING") \
  X(MainOnly,"ONLY") \
  X(StateResting,"RESTING") \
  X(StateExport,"EXPORT") \
  X(StateAdcError,"ADC ERROR") \
  X(StateCheckSensor,"CHECK SENSOR") \
  X(StatePaused,"PAUSED") \
  X(StateStarting,"STARTING") \
  X(StateReady,"READY") \
  X(StateMeasuring,"MEASURING") \
  X(StateRearm,"REARM") \
  X(PracticeHistory,"Launch history") \
  X(PracticeNone,"No launches yet") \
  X(PracticeMake,"Make a few pulls") \
  X(PracticeEach,"RPM from each pull") \
  X(PracticeSession,"S%lu  %lu pulls") /* session, pulls */ \
  X(PracticeAvg,"Avg %.0f RPM") /* RPM */ \
  X(PracticeBest,"Best %.0f") /* RPM */ \
  X(PracticeColumns,"Pull    RPM   Session") \
  X(PracticePage,"%u-%u of %u") /* first, last, total */ \
  X(PracticeSessionAvg,"Session avg RPM") \
  X(PracticeLastAvg,"Last %u avg RPM") /* count */ \
  X(PracticeSessionLine,"S%lu  n=%lu  best %.0f") /* session, pulls, RPM */ \
  X(PracticeLatest,"Latest #%lu: %.0f") /* pull, RPM */ \
  X(PracticeChange,"First/last %u: %+.0f%%") /* count, percent */ \
  X(PracticeNeedSessions,"Need 4 sessions") \
  X(PracticeNeedPulls,"10 pulls for change") \
  X(PracticeHoldBack,"Hold B Back") \
  X(PracticeFooter,"A Done  B Older") \
  X(TiltWarming,"Tilt warming up") \
  X(TiltGap,"Tilt incomplete") \
  X(TiltClipped,"Tilt out of range") \
  X(TiltTooLong,"Pull too long") \
  X(TiltNone,"No tilt data") \
  X(RecapNoCurve,"No RPM curve") \
  X(RecapLast,"Last launch") \
  X(RecapTiltApprox,"Tilt approx.") \
  X(RecapTiltReplay,"Tilt replay") \
  X(RecapCapturing,"Capturing") \
  X(RecapNewBest,"NEW BEST") \
  X(RecapMilestone,"MILESTONE %s") /* RPM */ \
  X(EffectMissing,"Missing effect") \
  X(MicQuiet,"quiet") \
  X(MicLoud,"too loud") \
  X(MicNoise,"noise") \
  X(MicLocking,"locking on") \
  X(MicNoTone,"no clear tone") \
  X(AnalysisFooterStop,"A Back  B Stop") \
  X(AnalysisFooterDetails,"A Back  B Details") \
  X(AnalysisTitle,"Stamina Analysis") \
  X(AnalysisLaunch,"Launch") \
  X(AnalysisToStart,"Launch to start") \
  X(AnalysisNow,"Now") \
  X(AnalysisLast,"Last") \
  X(AnalysisSaving,"Save pending") \
  X(AnalysisWait,"Please wait") \
  X(AnalysisTimeLimit,"Time limit") \
  X(AnalysisInterrupted,"Interrupted") \
  X(AnalysisNoStop,"No stop recorded") \
  X(AnalysisFitting,"Fitting curve") \
  X(AnalysisNoCurve,"Curve unavailable") \
  X(AnalysisEstimated,"Estimated curve") \
  X(AnalysisFitted,"Curve fitted") \
  X(AnalysisMicFailed,"Mic unavailable") \
  X(AnalysisNoMemory,"Not enough memory") \
  X(AnalysisMicStarting,"Mic warming up") \
  X(AnalysisFinish,"Finish") \
  X(AnalysisExperimental,"EXPERIMENTAL") \
  X(AnalysisRetry,"B Retry") \
  X(AnalysisReplayStop,"A Replay  B Stop") \
  X(AnalysisReplayDetails,"A Replay  B Details") \
  X(AnalysisHoldExit,"Hold B Exit") \
  X(AnalysisDetails,"Details") \
  X(AnalysisSpinTime,"Spin time") \
  X(AnalysisRecorded,"Recorded") \
  X(AnalysisDecay,"Decay") \
  X(AnalysisRevs,"Revs") \
  X(AnalysisStamina,"Stamina") \
  X(AnalysisTime,"Time") \
  X(AnalysisFitFailed,"Fit unavailable") \
  X(AnalysisNoBands,"No complete bands") \
  X(AnalysisNoModel,"No winning model") \
  X(AnalysisFooterMore,"A Back  B More") \
  X(BattleYou,"YOU") \
  X(BattleRival,"RIVAL") \
  X(BattleMatchPoint,"MATCH POINT") \
  X(BattleFirstTo,"First to %u") /* points */ \
  X(BattleWon,"WON") \
  X(BattleLost,"LOST") \
  X(BattleHowWin,"How did you win?") \
  X(BattleHowLose,"How did you lose?") \
  X(BattleHoldBack,"Hold B: back") \
  X(BattleFooterSend,"A Send  B Next") \
  X(BattleWin,"WIN") \
  X(BattleLoss,"LOSS") \
  X(BattleRivalScored,"%s, rival +%u") /* finish, points */ \
  X(BattleNoResult,"NO RESULT") \
  X(BattleOpen,"OPEN") \
  X(BattleBothWon,"Both entered a win") \
  X(BattleNotEntered,"Not entered yet") \
  X(BattleMatchWon,"MATCH WON") \
  X(BattleMatchLost,"MATCH LOST") \
  X(BattleAgreed,"On both sticks") \
  X(BattleSending,"Sending...") \
  X(BattleLater,"Sends at next battle") \
  X(BattleFooterChange,"A OK  B Change") \
  X(BattleTotals,"W %u  L %u  %u pts") /* wins, losses, points */ \
  X(BattleTotalsTight,"W %u L %u %u pts") /* wins, losses, points */ \
  X(BattleAuto,"Auto battles") \
  X(BattleNeedsBluetooth,"Needs Bluetooth") \
  X(BattleMatch,"Match") \
  X(BattleTogether1,"Launch together with") \
  X(BattleTogether2,"another LaunchLab:") \
  X(BattleTogether3,"it finds your rival") \
  X(BattleFooterBluetooth,"A Bluetooth  B Next") \
  X(BattleFooterTo4,"A To 4  B Next") \
  X(BattleFooterTo7,"A To 7  B Next") \
  X(BattleWonLost,"Won %u  Lost %u") /* wins, losses */ \
  X(BattlePoints,"Points %u - %u") /* for, against */ \
  X(BattleMatches,"Matches %u - %u") /* won, lost */ \
  X(BattleMatchScore,"First to %u: %u - %u") /* points, me, rival */ \
  X(BattleByFinish,"Wins by finish") \
  X(BattleFaster,"Faster launch") \
  X(BattleOf,"%u of %u") /* wins, duels */ \
  X(BattleLastRpm,"Last %u v %u rpm") /* mine, theirs */ \
  X(BannerLeft,"Phone left") \
  X(WearStageLevel,"1/3 Level") \
  X(WearStageDown,"2/3 Pull Down") \
  X(WearStageUp,"3/3 Pull Up") \
  X(WearPromptLevel,"Keep level") \
  X(WearPromptDown,"Pull down 90") \
  X(WearPromptUp,"Pull up 90") \
  X(WearComplete,"Complete") \
  X(WearPullOut,"PULL OUT") \
  X(WearTiltLevel,"TILT TO 0 DEG") \
  X(WearTiltVertical,"TILT TO 90 DEG") \
  X(WearFlip,"FLIP 180 DEG") \
  X(WearWaitImu,"WAIT FOR IMU") \
  X(WearHold,"HOLD") \
  X(WearLetGo,"LET GO") \
  X(WearWait,"WAIT")

enum class Text:uint16_t {
#define LAUNCHLAB_UI_TEXT_KEY(key,english) key,
  LAUNCHLAB_UI_TEXT(LAUNCHLAB_UI_TEXT_KEY)
#undef LAUNCHLAB_UI_TEXT_KEY
  COUNT
};
static constexpr unsigned COUNT=unsigned(Text::COUNT);
using Table=std::array<const char *,COUNT>;

static constexpr Table ENGLISH={
#define LAUNCHLAB_UI_TEXT_ENGLISH(key,english) english,
  LAUNCHLAB_UI_TEXT(LAUNCHLAB_UI_TEXT_ENGLISH)
#undef LAUNCHLAB_UI_TEXT_ENGLISH
};

// A translation: key and text (UTF-8). A language lists only the keys it
// translates; the others stay nullptr and show English.
struct Entry {Text key;const char *text;};
// Not constexpr: reaching it while a table is built is a compile error.
inline void keyTwiceOrOutOfRange(){}
template<size_t N> constexpr Table build(const Entry (&entries)[N]) {
  Table table{};
  for(const Entry &e:entries) {
    if(unsigned(e.key)>=COUNT || table[unsigned(e.key)])keyTwiceOrOutOfRange();
    table[unsigned(e.key)]=e.text;
  }
  return table;
}
}
#include "ui_text_de.h"
#include "ui_text_it.h"
namespace UiText {

// Languages by stable id (a saved setting will keep these numbers).
enum class Lang:uint8_t {English,German,Italian};
static constexpr unsigned LANGS=3;
// Each language by its own name (the Language setting's values).
static constexpr const char *NAMES[LANGS]={"English","Deutsch","Italiano"};
inline const char *name(Lang lang){return unsigned(lang)<LANGS?NAMES[unsigned(lang)]:NAMES[0];}
static constexpr const Table *TABLES[LANGS]={&ENGLISH,&GERMAN,&ITALIAN};

#ifndef LAUNCHLAB_UI_LANG
#define LAUNCHLAB_UI_LANG 0
#endif
// The language the screen is drawn in; English until a setting chooses another.
inline Lang &current(){static Lang lang=Lang(LAUNCHLAB_UI_LANG);return lang;}
inline const char *t(Text key,Lang lang) {
  const unsigned k=unsigned(key),l=unsigned(lang);
  const char *text=k<COUNT && l<LANGS?(*TABLES[l])[k]:nullptr;
  return text?text:k<COUNT?ENGLISH[k]:"";
}
inline const char *t(Text key){return t(key,current());}
// The English text of a translation this table returned (the same pointer),
// or nullptr for any other text (a formatted line, a name).
inline const char *english(const char *text) {
  const unsigned l=unsigned(current());
  if(!text || l>=LANGS)return nullptr;
  for(unsigned k=0;k<COUNT;++k)if((*TABLES[l])[k]==text)return ENGLISH[k];
  return nullptr;
}
// The current language's text for an English text equal to a table entry
// (by its letters), or nullptr. Screens that draw a fixed English word that
// also goes out on the serial port or BLE (a detector state, a wear check
// stage, a battle finish) pass it as it is; ui_draw.h looks it up here.
inline const char *local(const char *text) {
  if(!text || current()==Lang::English)return nullptr;
  for(unsigned k=0;k<COUNT;++k)if(!std::strcmp(ENGLISH[k],text))return t(Text(k));
  return nullptr;
}

// Glyphs. The tables are UTF-8; the fonts are not. Font 1 (GLCD, M5GFX
// Font0) has the 256 code page 437 glyphs, so Latin letters with accents
// are drawn as they are: each goes to M5GFX as the UTF-8 of its CP437 code,
// which M5GFX's UTF-8 decoder turns back into that glyph. Without cp437()
// (never set here) M5GFX draws a code at 176 or above one glyph further on,
// so those (ß) are passed one lower. Fonts 2, 4 and 6 have ASCII alone: there
// a letter is drawn as its base letter (`ascii`), and ui_draw.h puts the two
// dots over the umlauts (`dots`); other accents are left off (é: e), ß is
// "ss". ASCII text passes unchanged, so English draws exactly as before.
// mark: what ui_draw.h puts over the base letter in Fonts 2 and 4 (0 none,
// DOTS an umlaut, GRAVE, ACUTE).
enum Mark:uint8_t {NONE=0,DOTS=1,GRAVE=2,ACUTE=3};
struct Glyph {uint16_t unicode;uint8_t cp437;const char *ascii;uint8_t mark;};
static constexpr Glyph GLYPHS[]={
  {0xC4,0x8E,"A",true},{0xD6,0x99,"O",true},{0xDC,0x9A,"U",true},{0xE4,0x84,"a",true},{0xF6,0x94,"o",true},{0xFC,0x81,"u",true},
  {0xDF,0xE1,"ss",false},
  {0xE0,0x85,"a",GRAVE},{0xE1,0xA0,"a",ACUTE},{0xE2,0x83,"a",false},{0xE7,0x87,"c",false},{0xE8,0x8A,"e",GRAVE},{0xE9,0x82,"e",ACUTE},
  {0xEA,0x88,"e",false},{0xEC,0x8D,"i",GRAVE},{0xED,0xA1,"i",ACUTE},{0xEE,0x8C,"i",false},{0xF1,0xA4,"n",false},{0xF2,0x95,"o",GRAVE},
  {0xF3,0xA2,"o",ACUTE},{0xF4,0x93,"o",false},{0xF9,0x97,"u",GRAVE},{0xFA,0xA3,"u",ACUTE},{0xFB,0x96,"u",false},{0xC9,0x90,"E",false},
  {0xC7,0x80,"C",false},{0xD1,0xA5,"N",false},{0xEB,0x89,"e",false},{0xEF,0x8B,"i",false},{0xB0,0xF8,"`",false}};
// The glyph at s (UTF-8), its length into len; nullptr for ASCII or a
// character the table lacks.
inline const Glyph *glyphAt(const char *text,int &len) {
  const uint8_t *s=reinterpret_cast<const uint8_t *>(text);
  if(*s<0x80){len=1;return nullptr;}
  uint32_t u='?';len=1;
  if((*s&0xE0)==0xC0 && (s[1]&0xC0)==0x80){u=uint32_t(*s&0x1F)<<6|(s[1]&0x3F);len=2;}
  else while((s[len]&0xC0)==0x80)++len;
  for(const Glyph &x:GLYPHS)if(x.unicode==u)return &x;
  return nullptr;
}
inline bool ascii(const char *text){for(;*text;++text)if(uint8_t(*text)&0x80)return false;return true;}
// `text` as `font` can draw it, into out (always terminated). A character
// neither font knows is drawn as '?'.
inline const char *glyphs(const char *text,int font,char *out,size_t size) {
  size_t n=0;
  auto put=[&](uint8_t c){if(n+1<size)out[n++]=char(c);};
  for(const char *s=text;*s;) {
    if(uint8_t(*s)<0x80){put(uint8_t(*s++));continue;}
    int len;const Glyph *g=glyphAt(s,len);s+=len;
    if(!g){put('?');continue;}
    if(font==1) {
      const uint8_t code=g->cp437>=176?g->cp437-1:g->cp437;
      if(n+2<size){put(uint8_t(0xC0|code>>6));put(uint8_t(0x80|(code&0x3F)));}
    }
    else for(const char *a=g->ascii;*a;++a)put(uint8_t(*a));
  }
  out[n]=0;
  return out;
}
// Glyphs in a UTF-8 text (what Font 1 draws: 6 px each).
inline int glyphCount(const char *text){int n=0;for(;*text;++text)n+=(uint8_t(*text)&0xC0)!=0x80;return n;}
}
