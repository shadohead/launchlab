#include <M5Unified.h>
#include <Preferences.h>
#include <memory>
#include "acquisition.h"
#include "level_indicator.h"
#include "practice_store.h"
#include "history_log.h"
#include "spiffs_flash.h"
#include "loaded_effects.h"
#include "fx_transfer.h"
#if LAUNCHLAB_HIL
// Test builds: the NVS write faults (hil.h) also slow, fail or arm the history log writes.
namespace Hil{inline bool nvsWrite(const char *key,size_t bytes);}
#endif
#include "practice_ui.h"
#include "power_status.h"
#include "sensor_idle.h"
#include "cpu_power.h"
#ifndef LAUNCHLAB_MOTION_ADC_GATE
#define LAUNCHLAB_MOTION_ADC_GATE 0
#endif
#include "usb_awake.h"
#include <HWCDC.h>
#include "usb_tx.h"
#include "usb_command_guard.h"
#include "power_diagnostics.h"
#include <sys/time.h>
#include <esp32-hal-cpu.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <nvs.h>
#include "shake_wake.h"
#include "wake_ui_state.h"
#include "rpm_estimator_setting.h"
#include "brightness.h"
#include "display_flip_setting.h"
#include "shake_wake_setting.h"
#include "display_orientation.h"
#include "sensor_profile_setting.h"
#include "start_mark_reference.h"
#include "mount_orientation_setting.h"
#include "controls_ui.h"
#include "motion_acquisition.h"
#include "i2c_bus_recovery.h"
#include "motion_store.h"
#include "motion_ui.h"
#include "launch_feedback.h"
#include "appearance_store.h"
#include "milestones.h"
#include "milestone_store.h"
#include "effect_set.h"
#include "best_effect_ui.h"
#include "screens_ui.h"
#include "theme_store.h"
#include "best_effect_speaker.h"
#include "device_backup.h"
#include "nvs_budget.h"
#include "device_wipe_nvs.h"
#include "wipe_confirmation.h"
#include "settings_save.h"

// loop() runs on an 8 KB task. GCC inlines static helpers called once, and the
// frames of inlined bodies add up: 0.11.0's loop frame reached 6.9 KB and
// overflowed on the first status print. Keep heavy or rarely used helpers as
// real calls so each one uses stack only while it runs.
#define LOOP_HELPER __attribute__((noinline))
#if LAUNCHLAB_HIL
// Test-only hardware-in-the-loop build; never a release. See tools/hil.
static constexpr char VERSION[] = "0.20.3-sticks3-songs-hil-pm";
#elif LAUNCHLAB_CPU_PM
static constexpr char VERSION[] = "0.20.3-sticks3-songs-pm";
#else
static constexpr char VERSION[] = "0.20.3-sticks3-songs";
#endif
static_assert(sizeof(VERSION)<=DeviceBackup::RUNTIME_BYTES,"Runtime must fit the USB backup header");
#ifndef LAUNCHLAB_CPU_MHZ
#define LAUNCHLAB_CPU_MHZ 80
#endif
static_assert(LAUNCHLAB_CPU_MHZ==80 || LAUNCHLAB_CPU_MHZ==160 || LAUNCHLAB_CPU_MHZ==240,
  "CPU policy must retain the 80 MHz peripheral clock");
static_assert(!LAUNCHLAB_CPU_PM || LAUNCHLAB_CPU_MHZ==80,"Experimental DFS uses a maximum of 80 MHz");
static constexpr uint32_t ANALOG_SAMPLE_HZ = StickS3Acquisition::SAMPLE_HZ;
#include "raw_transfer.h"
static StickS3Acquisition acquisition;
static Preferences prefs;
static StickS3Level level;
static M5Canvas screen(&M5.Display);
static PracticeHistory practice,demoPractice;
static PracticeStore practiceStore;
static Appearance::Config appearance;
static Appearance::Bests personalBests;
static AppearanceStore<Appearance::Config> appearanceStore;
static AppearanceStore<Appearance::Bests> bestStore;
// Automatic saves (history, bests, the power log) go to the history log in
// the spiffs partition (history_log.h): program-only writes the ADC rides,
// no checkpoint and no NVS while the device is in use. NVS keeps 0.15-0.17's
// records as the mirror written at sleep, power-off and restore. Without the
// log (another layout found there), they go to NVS in a checkpoint as before.
static SpiffsFlash spiffsFlash;static HistoryLog::Store<SpiffsFlash> historyLog;
// Loaded effects (loaded_effects.h, docs/FIRMWARE_LOADABLE_EFFECTS.md): the
// store in the same partition and the engine that plays them, allocated at
// boot after the ADC has its DMA memory (internal heap), the playing file in
// PSRAM. Null when either allocation failed: loaded IDs then play their fallbacks.
static LoadedEffects<SpiffsFlash> *loadedEffects=nullptr;
// USB `X`: the request's base64 text and its bytes, in PSRAM, allocated at the first `X`.
static FxTransfer::Receiver effectRx;static char *effectRxText=nullptr;static uint8_t *effectRxBytes=nullptr;
static int64_t effectGuardStart=0;
static uint32_t loggedHistory=0,loggedBests=0;
static uint32_t powerMirror=0;  // checksum of the power log NVS holds
// The user's custom themes (theme IDs THEMES..), beside the appearance record.
static Themes::Library themeLibrary;
static ThemeStore themeStore;
// The Celebrate mode and the RPM milestone table, one record beside them. A
// damaged record shows the defaults and is replaced by the next change.
static Milestones::Config milestones;
static MilestoneStore milestoneStore;
// Each row's next effect, picked from its pool ahead of the pull that plays
// it, so its sound is rendered in time; picked again after it plays (never
// the same twice in a row when the pool has more) and when the table changes.
static Appearance::Effect milestoneNext[Milestones::MAX_ROWS];
static Milestones::Config milestonePicked;static bool milestonePicksReady=false;
// The effect set: the effects Best effect offers and Random draws from, a
// record beside them. A damaged record offers the default set and is
// replaced by the next change.
static EffectSet::Config effectSet;
static AppearanceStore<EffectSet::Config> effectSetStore;
static Appearance::Celebration bestEffect;
// The Best effect editor previews the draft around its value card.
static Appearance::Celebration editorEffect;
// Random's next pick, chosen ahead of the next best so its sound can render.
static Appearance::Effect upcomingEffect=Appearance::Effect::Off;
static BestEffectUI::Renderer bestEffectRenderer;
static bool effectWasMoving=false;
static PracticeUI::View historyView=PracticeUI::View::Recent;
static bool historyPage=false,demo=false,saveError=false;
static bool tournamentMode=false;
enum class TournamentView:uint8_t { RecordingOnly, Rpm };
static TournamentView tournamentView=TournamentView::RecordingOnly;
static Controls::State controls;
static Controls::Gesture gestureA,gestureB;
static WipeConfirmation wipeConfirmation;
static bool wipeFailed=false,wipeBlocked=false,wipeComplete=false;
static bool wipeCheckpointHeld=false,wipeBootRecovery=false,wipeRestartRequested=false;
static bool restorePreview=false;
static uint32_t tournamentHintUntil=0;

static unsigned historyOffset=0;
static uint32_t lastSaveTry=0,saveCount=0,saveErrors=0,maxSaveUs=0,newSessionAt=0;
static constexpr uint32_t HISTORY_COALESCE_MS=10000;
static constexpr uint32_t CELEBRATION_HOLD_MS=1000;
static constexpr uint32_t HISTORY_SAVE_RETRY_MS=5000;
static uint32_t lastAcceptedAt=0,historyPendingSince=0;
static bool screenReady=false;
static bool diagnostics=false,extPower=false,stress=false,dirty=true;
static uint32_t lastDraw=0,lastStatus=0,lastRevision=0,maxDrawUs=0,maxEffectUs=0,uiStalls=0;
#if LAUNCHLAB_HIL
// HIL only: each recap effect pass since the last E preview (elapsed ms, us),
// listed by `!effectframes`, to see where in an effect its time goes.
static struct {uint16_t elapsed[128],us[128];unsigned count=0;} effectFrames;
// The loaded-effects spike (hil_fx.h): draws a previewing file instead.
static bool hilFxDraw(uint16_t *pixels,int width,int height,uint32_t elapsed,uint16_t accent,uint16_t background);
#ifdef BEST_EFFECT_PROFILE
// A profiling debug build only: each pass's cycles per renderer stage, for
// `!effectstages`.
static struct {uint16_t elapsed[64];uint32_t us[64],cycles[64][BestEffectUI::Renderer::STAGES];unsigned count=0;} effectStages;
#endif
#endif
static uint32_t lastImuPoll=0,maxImuPollUs=0;
static StickS3Level::Reading drawnLevel;
static InactivityTimer inactivity;
static uint64_t lastActivityMotionUs=0;
// USB letters a stray byte stream sent outside a standalone command.
static UsbCommandGuard usbCommands;
// Automatic checkpoints prefer a launcher the IMU shows set down.
static HistoryCheckpoint::Stillness imuStill;
static SensorIdle sensorIdle;
static UsbHostAwake usbAwake;
static bool usbAutoOffInhibited=false;
static StickS3ShakeWake shakeWake;
// A side-key click is latched by the G13 edge ISR and acted on by the loop.
static volatile bool powerKeyIrq=false;
static bool powerKeyRequested=false;
static uint32_t powerKeyRequestedAt=0,lastPowerKeyRead=0;
static constexpr uint32_t POWER_KEY_WAIT_MS=10000;
static bool settingsSaveError=false;
static SettingsSave settingsSave;
// A stopped, non-resting ADC owner outlasting this is a lost checkpoint reply.
static constexpr uint32_t ORPHAN_CHECKPOINT_MS=1000;
static bool orphanSeen=false;static uint32_t orphanSince=0;
static uint8_t brightnessPercent=Brightness::DEFAULT_PERCENT;
static bool displayFlipped=false;
// Shake to wake (0.20.2, shake_wake_setting.h): whether an inactivity sleep arms the IMU wake.
static bool shakeToWake=ShakeWakeSetting::DEFAULT_ON;
static RpmEstimator::Mode rpmEstimator=RpmEstimator::DEFAULT_MODE;
static StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
static MountOrientation::Mode mountMode=MountOrientation::Mode::Top;
// The frame level data and the IMU owner actually use. A new mount choice is
// switched to inside its save checkpoint: switching clears the IMU ring, which
// must never happen during a pull or its capture.
static MountOrientation::Mode liveMount=MountOrientation::Mode::Top;
static StickS3Battery battery;
static uint32_t lastBatteryPoll=0,maxBatteryPollUs=0,lastShutdownTry=0;
static bool batteryPolled=false;
static bool powerConfigValid=false;
static uint8_t powerConfig=0;
static Preferences powerPrefs;
static PowerDiagnostics::Image powerLog;
static bool powerLogReady=false,powerLogDirty=false;
static uint32_t powerLogTick=0,lastPowerSample=0,lastPowerSaveTry=0;
struct SleepReceipt {uint32_t magic;int64_t atUs;};
RTC_DATA_ATTR static SleepReceipt sleepReceipt={};
static uint8_t pm1WakeFlags=0;
static bool pm1WakeValid=false;
static StickS3MotionAcquisition imuAcquisition;
static BestEffectSpeaker soundPlayer(imuAcquisition);
// Which sound is playing: a celebration stops when its recap goes, a Settings
// preview when its editor closes. A requested sound that is not rendered yet
// waits for the idle slices rather than stalling the UI loop to render it.
static bool celebrationSound=false,soundPreview=false;
static struct {bool active=false,celebration=false;Appearance::Effect effect=Appearance::Effect::Off;Appearance::Sound level=Appearance::Sound::Off;} soundRequest;
// From reading an effect frame's elapsed time to that frame on the glass
// (effect draw and push), measured each frame: SoundSync lines sound up with it.
static int64_t effectReadUs=0;static uint32_t presentLagUs=15000;
// The last sound start: how far its effect's clock moved, or the samples it
// skipped to join late (SOUND_SYNC).
static int32_t soundShiftMs=0;static uint32_t soundSkipped=0;
// A celebration sound that cannot start within this long after its effect is dropped.
static constexpr uint32_t SOUND_LATE_MS=300;
static void requestSound(Appearance::Effect effect,Appearance::Sound level,bool celebration);static void startSound(bool late);
static const char *milestoneStoreName();static const char *effectSetFields();
static Appearance::Effect celebrationEffect();static Appearance::Effect soundTarget();
static const Appearance::Effect *milestonePicks();static void nextMilestonePick(uint16_t rpm,Appearance::Effect played);
static constexpr size_t USB_TX_BUFFER_BYTES=8192;
// HWCDC drops bytes once its RX queue is full, and a tool's request line
// arrives in one burst. Every build uses this size: the HIL build alone once
// had a large queue, which hid that production's 256-byte one lost the end of
// every v3 milestone request (312 characters), so no save reached 0.18.0.
static constexpr size_t USB_RX_BUFFER_BYTES=8192;
static constexpr uint32_t MOTION_INIT_RETRY_MS=5000,MOTION_DATA_TIMEOUT_MS=250;
// The 1024-sample ring (10 s at 100 Hz) still holds a pull for this long.
static constexpr uint64_t MOTION_CAPTURE_RETRY_US=3000000;
static uint32_t motionCaptureRetries=0,motionRestoreFailures=0;
static bool motionRestoreNeeded=false;
static uint32_t motionInitAttempts=0,lastMotionInit=0;
static bool motionInitPaired=false,motionInitSuccess=false;
static MotionStore motionStore;
// A restore's base64 text, in PSRAM (allocated at the first R): a backup
// with loaded effects can carry up to 32 of them. Decoded in place.
static DeviceBackup::BufferReceiver restoreRx;static char *restoreText=nullptr;
// One framed custom theme request (USB `T`).
static DeviceBackup::LineReceiver<(Themes::Request::BYTES+2)/3*4> themeRx;
// One framed milestone request (USB `U`).
static DeviceBackup::LineReceiver<(Milestones::Request::BYTES+2)/3*4> milestoneRx;
static LaunchMotion::Trace latestMotion,referenceMotion,demoMotion;
static bool motionPending=false,referenceError=false,motionDemo=false;
static uint64_t motionStart=0,motionEnd=0,lastLevelSampleUs=0;
static uint32_t motionDrops=0;
static I2cBusRecovery::Result i2cRecovery=I2cBusRecovery::Result::Idle;static unsigned i2cRecoveryPulses=0;
static bool i2cSdaWas=true,i2cSclWas=true;
static LaunchFeedback launchFeedback;
static AnalogTachometer::Phase lastPhase=AnalogTachometer::Phase::Paused;
static bool lastFault=false;
using Action=StickS3Acquisition::Action;
using Snapshot=StickS3Acquisition::Snapshot;
enum class ShutdownFlush:uint8_t {Busy,NewPull,SaveFailed,Ready};

static bool motionSensorPaired() {
  // isEnabled only establishes driver detection. Require an actual new paired
  // read before starting the fusion owner; cached data and rest are not tests.
  M5.Imu.setCalibration(0,0,0);
  const uint32_t started=millis();
  do {
    const unsigned fresh=M5.Imu.update();
    const unsigned both=m5::IMU_Class::sensor_mask_accel|m5::IMU_Class::sensor_mask_gyro;
    if((fresh&both)==both) {
      const auto data=M5.Imu.getImuData();
      if(std::isfinite(data.accel.x) && std::isfinite(data.accel.y) && std::isfinite(data.accel.z) &&
         std::isfinite(data.gyro.x) && std::isfinite(data.gyro.y) && std::isfinite(data.gyro.z))return true;
    }
    delay(5);
  } while(uint32_t(millis()-started)<MOTION_DATA_TIMEOUT_MS);
  return false;
}

static bool startMotion() {
  if(imuAcquisition.running())return true;
  // No IMU owner exists here. Main-thread power/level calls are serialized;
  // only the missing IMU is reinitialized, not M5 or optical acquisition.
  ++motionInitAttempts;lastMotionInit=millis();
  // Every M5 begin resets the BMI270 range and FIFO; both must follow detection.
  motionInitPaired=M5.Imu.isEnabled() && configureMotionSensor() && motionSensorPaired();
  if(!motionInitPaired) {
    const bool detected=M5.Imu.begin(&M5.In_I2C,m5::board_t::board_M5StickS3);
    motionInitPaired=detected && configureMotionSensor() && motionSensorPaired();
  }
  // Nothing is captured before the owner starts, so it can take the chosen mount.
  const bool mounted=motionInitPaired && imuAcquisition.setMount(mountMode);
  if(mounted)liveMount=mountMode;
  motionInitSuccess=mounted && imuAcquisition.begin();
  Serial.printf("IMU_STARTUP attempt=%lu enabled=%d type=%d paired=%d task=%d elapsed_ms=%lu\n",
    (unsigned long)motionInitAttempts,M5.Imu.isEnabled(),int(M5.Imu.getType()),motionInitPaired,
    imuAcquisition.running(),(unsigned long)(millis()-lastMotionInit));
  return motionInitSuccess;
}

// Every flash write runs inside an ADC checkpoint (DMA cannot survive the
// cache stall either). The same boundary keeps IMU transfers out of the stall.
// If no quiet window can be established the checkpoint is vetoed; every
// caller already treats a refused checkpoint as busy and retries later.
static bool quietImuForFlash(Action action,bool done,bool ok) {
  const bool writes=action==Action::IdleCheckpointBegin || action==Action::ShutdownBegin ||
    action==Action::HistoryIdleCheckpointBegin || action==Action::CheckpointBegin;
  if(writes && !done)return imuAcquisition.quiet();
  if((writes && !ok) || action==Action::CheckpointEnd)imuAcquisition.endQuiet();
  return true;
}

// A cancelled standby re-applies M5's measurement configuration and the FIFO.
// If that fails the IMU task keeps running on an unconfigured sensor, so the
// task existing is not proof of tilt data: retry the restore until it works.
static void noteMotionRestoreFailure(){motionRestoreNeeded=true;++motionRestoreFailures;lastMotionInit=millis();}
LOOP_HELPER static void retryMotion(uint32_t now,const Snapshot &s) {
  if(motionPending || uint32_t(now-lastMotionInit)<MOTION_INIT_RETRY_MS ||
     (s.phase!=AnalogTachometer::Phase::Ready && s.phase!=AnalogTachometer::Phase::Paused))return;
  if(!imuAcquisition.running()){startMotion();dirty=true;return;}
  if(!motionRestoreNeeded || !imuAcquisition.lock())return;
  lastMotionInit=now;
  const bool restored=shakeWake.restore();imuAcquisition.unlock();
  if(restored)motionRestoreNeeded=false;else ++motionRestoreFailures;
  Serial.printf("IMU_RESTORE_RETRY ok=%d failures=%lu\n",restored,(unsigned long)motionRestoreFailures);dirty=true;
}

static uint8_t backlightValue(){return (unsigned(brightnessPercent)*255+50)/100;}
// During a pull the preview can be deferred. Sensor coordinates must follow
// the LCD orientation that was actually applied, rather than an unseen draft.
static bool visibleDisplayFlipped(){return DisplayOrientation::flipForRotation(M5.Display.getRotation());}
static Appearance::Theme visibleTheme(){return controls.page==Controls::Page::Edit && controls.setting==5?Appearance::Theme(controls.draft):appearance.theme;}
// The colors on screen: the Theme editor's draft while it is open. A custom
// theme that is missing or invalid shows the default.
static Palette::Colors ui(){return themeLibrary.colors(visibleTheme());}
static void cancelSettingEdit();
static void beginWipe();
static void cancelWipe();
static bool saveWakeNavigation(){return prefs.putUInt("wake_nav",Controls::encodeWake(controls,uint8_t(tournamentView)))==sizeof(uint32_t);}
static void syncNavigation() {
  tournamentMode=controls.page==Controls::Page::Tournament;
  historyPage=controls.page!=Controls::Page::Main && !tournamentMode;
  if(controls.page==Controls::Page::History)historyView=PracticeUI::View(controls.history);
  else if(controls.page==Controls::Page::Battery)historyView=PracticeUI::View::Battery;
  else if(controls.page==Controls::Page::Settings || controls.page==Controls::Page::Edit || controls.page==Controls::Page::Wipe)historyView=PracticeUI::View::Settings;
  dirty=true;
}
static int64_t wallUs(){timeval t;gettimeofday(&t,nullptr);return int64_t(t.tv_sec)*1000000+t.tv_usec;}
static uint8_t powerFlags(){return (battery.charging?PowerDiagnostics::Charging:0)
  | (battery.chargeKnown?PowerDiagnostics::ChargeKnown:0) | (powerConfigValid && (powerConfig&0x10)?PowerDiagnostics::Led:0)
  | (extPower?PowerDiagnostics::Boost:0) | (tournamentMode?PowerDiagnostics::Tournament:0) | PowerDiagnostics::SensorRail;}
static void powerRecord(uint8_t event,uint8_t wake=0){PowerDiagnostics::append(powerLog,event,battery.voltageOk?battery.millivolts:0,
  powerFlags(),M5.Display.getBrightness(),ESP.getCpuFreqMHz(),wake);powerLogDirty=true;}
static bool savePowerLog(){
  // Bound retries after an NVS failure; repeated checkpoints keep the detector
  // in Settling and prevent subsequent pulls from being measured.
  lastPowerSaveTry=millis();
  if(!powerLogReady)return false;powerLog.checksum=PowerDiagnostics::hash(powerLog);
  const bool ok=powerPrefs.putBytes("log",&powerLog,sizeof(powerLog))==sizeof(powerLog);
  if(ok){powerLogDirty=false;powerMirror=powerLog.checksum;}return ok;
}
LOOP_HELPER static void exportPowerLog(){
  Serial.printf("POWER_LOG schema=1 boots=%lu power_button_wakes=%lu shake_wakes=%lu other_wakes=%lu sleeps=%lu awake_s=%llu known_sleep_s=%llu unknown_sleep_intervals=%lu charging_awake_s=%llu led_awake_s=%llu boost_awake_s=%llu display_awake_s=%llu records=%lu storage_ready=%d unsaved=%d current_ma=unavailable\n",
    (unsigned long)powerLog.boots,(unsigned long)powerLog.powerButtonWakes,(unsigned long)powerLog.shakeWakes,
    (unsigned long)powerLog.otherWakes,(unsigned long)powerLog.sleeps,powerLog.awakeMs/1000,powerLog.sleepMs/1000,(unsigned long)powerLog.unknownSleepIntervals,
    powerLog.chargingMs/1000,powerLog.ledMs/1000,powerLog.boostMs/1000,powerLog.displayMs/1000,
    (unsigned long)powerLog.count,powerLogReady,powerLogDirty);
  for(unsigned i=0;i<powerLog.count;++i){const auto &r=powerLog.records[(powerLog.next+PowerDiagnostics::CAPACITY-powerLog.count+i)%PowerDiagnostics::CAPACITY];
    Serial.printf("POWER_RECORD tracked_s=%llu boot=%lu event=%u battery_mv=%u flags=0x%02x backlight=%u cpu_mhz=%u wake=%u\n",
      r.elapsedSeconds,(unsigned long)r.boot,r.event,r.batteryMv,r.flags,r.brightness,r.cpuMhz,r.wake);}
}
LOOP_HELPER static void tickPowerLog(uint32_t now,const Snapshot &s){
  PowerDiagnostics::account(powerLog,now-powerLogTick,powerFlags(),M5.Display.getBrightness());powerLogTick=now;
  if(now-lastPowerSample>=60000){lastPowerSample=now;powerRecord(PowerDiagnostics::Sample);}
  // A resting owner already holds its checkpoint. Keep optional diagnostics
  // in RAM until motion resumes it or final shutdown flushes the log.
  if(!s.resting && powerLogReady && powerLogDirty && now-lastPowerSaveTry>=300000 &&
    HistoryCheckpoint::practiceIdle(now,lastAcceptedAt,
      HistoryCheckpoint::automaticQuietMs(now,imuStill,now-lastPowerSaveTry-300000,HISTORY_COALESCE_MS),
      motionPending,launchFeedback.shown(),s.phase)){
    // Record the attempt first: a vetoed checkpoint must not repeat the IMU
    // quiet wait on every UI tick. The dirty log stays pending.
    lastPowerSaveTry=now;
    if(historyLog.isOpen()) {
      // To the log, beside history: no checkpoint (sleep and power-off write NVS).
      powerLog.checksum=PowerDiagnostics::hash(powerLog);
#if LAUNCHLAB_HIL
      if(Hil::nvsWrite("log",sizeof(powerLog)))return;
#endif
      if(historyLog.savePower(powerLog,powerMirror))powerLogDirty=false;else Serial.println("POWER_LOG_NOTICE log_save_failed");
      return;
    }
    if(!acquisition.request(Action::IdleCheckpointBegin,s.launches))return;
    if(!savePowerLog())Serial.println("POWER_LOG_NOTICE checkpoint_failed");acquisition.request(Action::CheckpointEnd);
  }
}


static const char *tournamentViewName() {
  switch(tournamentView) {
    case TournamentView::RecordingOnly:return "recording_only";
    default:return "rpm";
  }
}

// Internal RAM headroom for task stacks, drivers and the UI canvas. The
// minimum is the low-water mark since boot, so it covers backup and restore.
LOOP_HELPER static void heapStatus() {
  constexpr uint32_t INTERNAL=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  const void *canvas=screenReady?screen.getBuffer():nullptr;
  // DMA-capable internal RAM: the ADC's continuous driver, the speaker's I2S
  // ring and USB need it; a build once boot-looped when the ADC found none.
  constexpr uint32_t DMA=MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL;
  Serial.printf("HEAP_STATUS internal_free=%u internal_largest=%u internal_min_free=%u dma_free=%u dma_largest=%u dma_min_free=%u psram_free=%u psram_largest=%u canvas=%s canvas_bytes=%u\n",
    unsigned(heap_caps_get_free_size(INTERNAL)),unsigned(heap_caps_get_largest_free_block(INTERNAL)),
    unsigned(heap_caps_get_minimum_free_size(INTERNAL)),unsigned(heap_caps_get_free_size(DMA)),
    unsigned(heap_caps_get_largest_free_block(DMA)),unsigned(heap_caps_get_minimum_free_size(DMA)),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
    !canvas?"none":esp_ptr_internal(canvas)?"internal":"psram",unsigned(screenReady?screen.bufferLength():0));
}

// NVS entries; a history save needs one rewrite's worth free, and a negative
// headroom means the next history save would fail for lack of space.
static bool nvsAvailable(unsigned &available){
  nvs_stats_t stats{};if(nvs_get_stats(nullptr,&stats)!=ESP_OK)return false;
  available=unsigned(stats.available_entries);return true;
}

// The start floor while a sound plays (start_mark_reference.h, 0.20.1): this
// unit's recent confirmed pulls' weakest start marks, for its sensor profile.
static StartMarkReference::Reference startMarks;
static bool startMarksDirty=false;
static constexpr const char *START_MARKS_KEY="start_marks";
static void loadStartMarks() {
  StartMarkReference::Image image;const size_t n=prefs.getBytesLength(START_MARKS_KEY);
  if(n!=StartMarkReference::IMAGE_BYTES || prefs.getBytes(START_MARKS_KEY,image,n)!=n ||
     !startMarks.decode(image,unsigned(n)) || startMarks.profile!=uint8_t(sensorProfile))startMarks.clear(uint8_t(sensorProfile));
}
// A confirmed pull's weakest start mark joins the reference of its profile.
static void noteStartMark(const Snapshot &e) {
  if(!e.valid || e.startMark<=0)return;
  if(startMarks.profile!=uint8_t(e.sensorProfile))startMarks.clear(uint8_t(e.sensorProfile));
  startMarks.add(e.startMark);startMarksDirty=true;
}
// Saved with the NVS mirror at sleep and power-off, never while the unit is
// in use. A first save takes NVS room, so it keeps the reserve for optional
// records; an empty reference (a new profile, after a wipe) removes the key.
static void saveStartMarks() {
  if(!startMarksDirty)return;
  if(!startMarks.count) {
    if(!prefs.isKey(START_MARKS_KEY) || prefs.remove(START_MARKS_KEY))startMarksDirty=false;
    return;
  }
  const unsigned own=NvsBudget::blobEntries(StartMarkReference::IMAGE_BYTES);
  const unsigned needed=NvsBudget::optionalNeeded(own,prefs.isKey(START_MARKS_KEY)?0:own,NvsBudget::blobEntries(PracticeHistory::IMAGE_BYTES));
  unsigned available=0;
  if(!nvsAvailable(available) || available<needed){Serial.printf("START_MARKS_NOTICE refused nvs_space available=%u needed=%u\n",available,needed);return;}
  StartMarkReference::Image image;startMarks.encode(image);
  if(prefs.putBytes(START_MARKS_KEY,image,sizeof(image))==sizeof(image))startMarksDirty=false;
  else Serial.println("START_MARKS_ERROR save_failed");
}
// Each loop pass: while any sound plays, a launch starts only on marks half
// as strong as this unit's pulls (at most 400 counts); otherwise as ever.
static void updateStartFloor() {
  static float last=-1;
  // A reference kept for another profile (a restore can change it) does not apply.
  const float minMark=StickS3SensorProfile::minMark(sensorProfile);
  const float floor=!soundPlayer.playing()?0:startMarks.profile==uint8_t(sensorProfile)?startMarks.floor(minMark):minMark;
  if(floor!=last){acquisition.setStartFloor(floor);last=floor;}
}
LOOP_HELPER static void nvsStatus() {
  nvs_stats_t stats{};const esp_err_t error=nvs_get_stats(nullptr,&stats);
  Serial.printf("NVS_STATUS ok=%d used_entries=%u available_entries=%u total_entries=%u namespaces=%u history_save_entries=%u history_headroom=%d\n",
    error==ESP_OK,unsigned(stats.used_entries),unsigned(stats.available_entries),unsigned(stats.total_entries),
    unsigned(stats.namespace_count),NvsBudget::blobEntries(PracticeHistory::IMAGE_BYTES),
    NvsBudget::headroom(unsigned(stats.available_entries),PracticeHistory::IMAGE_BYTES));
}

LOOP_HELPER static void status() {
  const Snapshot s=acquisition.snapshot();
  // Least free stack the loop task has had since boot; 0.11.0 overflowed its 8 KB.
  Serial.printf("STACK loop_min_free_bytes=%u\n",unsigned(uxTaskGetStackHighWaterMark(nullptr)));
  Serial.printf("STATUS v=%s board=StickS3 board_id=%d gpio=%u adc_ready=%d suspended=%d ext5v=%d width=%d height=%d rotation=%u samples=%llu delivered_sps=%.0f mean=%.1f min=%lu max=%lu last=%lu overflow_events=%lu read_errors=%lu adc_error=%s\n",
    VERSION,int(M5.getBoard()),s.pin,s.ready,s.suspended,extPower,M5.Display.width(),M5.Display.height(),
    unsigned(M5.Display.getRotation()),
    (unsigned long long)s.samples,s.rate,s.mean,(unsigned long)s.minimum,(unsigned long)s.maximum,
    (unsigned long)s.last,(unsigned long)s.overflows,(unsigned long)s.readErrors,esp_err_to_name(s.error));
  Serial.printf("RPM_STATUS state=%s valid=%d peak_rpm=%.1f live_peak=%.1f launches=%lu edges=%lu weak_rejected=%lu slow_rejected=%lu noise=%.1f amplitude=%.1f rearm_ms=%lu candidate_rpm=%.1f contrast=%.1f hysteresis=%.1f shape_rejected=%lu inconsistent=%lu rearm_extensions=%lu detector_us=%llu recorder_s=%lu min_mark=%.0f edge_fraction=%.2f start_floor=%.0f start_mark=%.0f sound_held=%lu ref_pulls=%u ref_median=%.0f\n",
    s.stateName(),s.valid,s.resultRpm,s.peakRpm,(unsigned long)s.launches,(unsigned long)s.edges,
    (unsigned long)s.weak,(unsigned long)s.slow,s.noise,s.amplitude,(unsigned long)s.rearmMs,
    s.candidateRpm,s.contrast,s.hysteresis,(unsigned long)s.shape,(unsigned long)s.inconsistent,
    (unsigned long)s.rearmExtensions,(unsigned long long)s.detectorUs,
    acquisition.storage()?60ul:0ul,StickS3SensorProfile::minMark(s.sensorProfile),StickS3SensorProfile::EDGE_SWING_FRACTION,
    s.startFloor,s.startMark,(unsigned long)s.soundHeld,unsigned(startMarks.count),startMarks.median());
  Serial.printf("SENSOR_PROFILE_STATUS selected=%s min_mark=%.0f saved_setting=%s\n",
    StickS3SensorProfile::name(s.sensorProfile),StickS3SensorProfile::minMark(s.sensorProfile),StickS3SensorProfile::name(sensorProfile));
  Serial.printf("RPM_ESTIMATORS selected=%s units=rpm three_turn_peak_rpm=%.1f single_turn_peak_rpm=%.1f confirmation_turns=3 max_rpm=30000 single_updates=%lu single_rejected=%lu\n",
    RpmEstimator::name(s.estimator),s.sustainedPeakRpm,s.singlePeakRpm,
    (unsigned long)s.singlePeakUpdates,(unsigned long)s.singlePeakRejected);
  Serial.printf("SETTINGS_SAVE_STATUS pending=0x%03x failed=%d saves=%lu errors=%lu refusals=%lu\n",unsigned(settingsSave.pending()),
    settingsSave.failed(),(unsigned long)settingsSave.saves(),(unsigned long)settingsSave.errors(),(unsigned long)settingsSave.refusals());
  Serial.printf("TOURNAMENT_STATUS enabled=%d display=%s persistent=wake\n",tournamentMode,tournamentViewName());
  Serial.printf("MILESTONE_STATUS mode=%s rows=%u revision=%lu store=%s\n",Milestones::name(milestones.mode()),milestones.count(),
    (unsigned long)milestones.revision,milestoneStoreName());
  soundPlayer.status(appearance.sound);
  Serial.printf("SOUND_SYNC present_lag_us=%lu output_us=%lu clock_shift_ms=%ld skipped_samples=%lu\n",(unsigned long)presentLagUs,
    (unsigned long)soundPlayer.outputUs(),(long)soundShiftMs,(unsigned long)soundSkipped);
  Serial.printf("UI_STATUS page=%u menu=%u history=%u setting=%u draft=%u recording=%s\n",
    unsigned(controls.page),controls.menu,controls.history,controls.setting,controls.draft,
    s.resting?"idle_paused":LAUNCHLAB_MOTION_ADC_GATE?"motion_gated":"always");
  Serial.printf("PERF acquisition_core=0 ui_core=%d processing_pct=%.2f max_batch_us=%lu frame_budget_us=%lu max_read_gap_us=%lu stack_free=%lu invalid_frames=%lu lost_events=%lu adc_stalls=%lu max_draw_us=%lu max_effect_us=%lu ui_stalls=%lu stress=%d heap_free=%lu psram_free=%lu usb_drain_timeouts=%lu usb_rejected_commands=%lu\n",
    xPortGetCoreID(),s.processingPercent,(unsigned long)s.maxBatchUs,
    (unsigned long)(uint64_t(StickS3Acquisition::FRAME_BYTES/SOC_ADC_DIGI_RESULT_BYTES)*1000000/StickS3Acquisition::SAMPLE_HZ),(unsigned long)s.maxReadGapUs,
    (unsigned long)s.stackFree,(unsigned long)s.invalidFrames,(unsigned long)s.lostEvents,(unsigned long)s.stalls,
    (unsigned long)maxDrawUs,(unsigned long)maxEffectUs,(unsigned long)uiStalls,stress,
    (unsigned long)ESP.getFreeHeap(),(unsigned long)ESP.getFreePsram(),(unsigned long)UsbTx::drainTimeouts,(unsigned long)usbCommands.rejected);
  heapStatus();nvsStatus();
  const auto tilt=level.reading(millis());const float *acc=level.lastAccel();
  Serial.printf("LEVEL_STATUS imu_ready=%d imu_type=%d state=%s degrees=%.2f accel_g=%.3f,%.3f,%.3f samples=%lu rejected=%lu max_poll_us=%lu draw_buffer=%d\n",
    M5.Imu.isEnabled(),int(M5.Imu.getType()),tilt.name(),tilt.degrees,acc[0],acc[1],acc[2],
    (unsigned long)level.samples(),(unsigned long)level.rejected(),(unsigned long)maxImuPollUs,screenReady);
  const auto *session=practice.session();
  const uint32_t now=millis();
  Serial.printf("HISTORY_STATUS records=%u sessions=%u session=%lu session_pulls=%lu session_avg=%.1f generation=%lu pending=%d store_ready=%d save_errors=%lu saves=%lu max_save_us=%lu page=%s view=%u offset=%u demo=%d ui_stack_free=%lu\n",
    practice.size(),practice.sessions(),session?(unsigned long)session->number:0ul,
    session?(unsigned long)session->count:0ul,session?session->mean():0,
    (unsigned long)practice.generation(),historyUnsaved(),practiceStore.ready() || historyLog.isOpen(),
    (unsigned long)saveErrors,(unsigned long)saveCount,(unsigned long)maxSaveUs,
    diagnostics?"signal":historyPage?"history":"live",unsigned(historyView),historyOffset,demo,
    (unsigned long)uxTaskGetStackHighWaterMark(nullptr));
  const bool pendingHistory=historyUnsaved();
  const uint32_t pendingMs=pendingHistory && historyPendingSince?now-historyPendingSince:0;
  const uint32_t quietMs=now-lastAcceptedAt;
  const uint32_t coalesceMs=HistoryCheckpoint::automaticQuietMs(now,imuStill,pendingMs,HISTORY_COALESCE_MS);
  const uint32_t flushDue=quietMs>=coalesceMs?0u:coalesceMs-quietMs;
  const uint32_t retryRemaining=saveError && now-lastSaveTry<HISTORY_SAVE_RETRY_MS?HISTORY_SAVE_RETRY_MS-(now-lastSaveTry):0;
  Serial.printf("HISTORY_PENDING unsaved_pulls=%lu pending_ms=%lu flush_due_ms=%lu coalesce_ms=%lu max_pending_ms=%lu retry_remaining_ms=%lu\n",
    (unsigned long)(historyLog.isOpen()?practice.generation()-loggedHistory:practiceStore.pendingCount(practice)),(unsigned long)pendingMs,
    (unsigned long)(pendingHistory?flushDue:0),(unsigned long)coalesceMs,(unsigned long)HistoryCheckpoint::MAX_PENDING_MS,(unsigned long)retryRemaining);
  Serial.printf("POWER_STATUS auto_off_s=%lu shake_wake=%d wake=%s sleep=%s sensor_rail=L2_off_in_battery_standby idle_s=%lu off_in_s=%lu battery_valid=%d battery_mv=%u battery_pct=%d charging=%s max_battery_poll_us=%lu auto_off_inhibited=%d\n",
    (unsigned long)(inactivity.timeout()/1000),shakeToWake,shakeToWake?"shake":"side_key",shakeToWake?"imu_only":"power_off",(unsigned long)(inactivity.elapsed(now)/1000),(unsigned long)((inactivity.remaining(now)+999)/1000),
    battery.valid(now),unsigned(battery.millivolts),battery.valid(now)?battery.percent:-1,
    battery.chargeName(),(unsigned long)maxBatteryPollUs,usbAwake.inhibited(now));
  Serial.printf("USB_POWER host_sof=%d host_seen=%d auto_off_inhibited=%d reason=%s power_known=%d last_power_read_ok=%d source=0x%02x host_poll_ms=%lu power_poll_ms=%lu\n",
    usbAwake.sof(),usbAwake.remembered(),usbAwake.inhibited(now),usbAwake.reason(now),usbAwake.powerKnown(now),usbAwake.lastPowerReadOk(),usbAwake.powerBits(),
    (unsigned long)UsbHostAwake::HOST_POLL_MS,(unsigned long)UsbHostAwake::POWER_POLL_MS);
  Serial.printf("POWER_DETAIL uptime_s=%lu cpu_mhz=%lu backlight=%u pm1_cfg_valid=%d pm1_cfg=0x%02x led_enabled=%d current_measurement=unavailable\n",
    (unsigned long)(now/1000),(unsigned long)ESP.getCpuFreqMHz(),unsigned(M5.Display.getBrightness()),
    powerConfigValid,powerConfig,powerConfigValid?int(bool(powerConfig&0x10)):-1);
  Serial.printf("POWER_KEY single_click=%s pending=%d clear_failures=%lu\n",
    powerKey().configured()?"firmware_power_off":"pm1_reset",powerKeyRequested,(unsigned long)powerKey().clearFailures());
  Serial.printf("CPU_PM requested=%d enabled=%d min_mhz=%u max_mhz=80 light_sleep=0 error=%s\n",
    LAUNCHLAB_CPU_PM,CpuPower::active(),CpuPower::active()?40u:80u,CpuPower::errorName());
  Serial.printf("POWER_POLICY effective_timeout_s=%lu unused_shake=%d dim_after_ms=%lu level_poll_ms=%lu ui_poll_ms=%lu battery_estimate_mv=%u battery_estimate=voltage_median5\n",
    (unsigned long)(inactivity.activeTimeout()/1000),inactivity.unusedShake(),
    (unsigned long)IdlePowerPolicy::DIM_MS,(unsigned long)IdlePowerPolicy::LEVEL_POLL_MS,
    (unsigned long)IdlePowerPolicy::UI_POLL_MS,unsigned(battery.estimateMillivolts));
  // PM1 statistics are written under the shared gate. Do not block status
  // waiting on the IMU owner or sample them concurrently with a PM1 operation.
  if(imuAcquisition.lock()) {
    const auto pm=pm1Idle().stats();imuAcquisition.unlock();
    Serial.printf("PM1_IDLE_STATUS configured=%d seconds=%u fallback_verified=%d wake_retries=%lu recovered_reads=%lu read_failures=%lu write_failures=%lu operation_failures=%lu config_failures=%lu\n",
      pm.configured,unsigned(pm.seconds),pm.fallbackVerified,(unsigned long)pm.wakeRetries,
      (unsigned long)pm.recoveredReads,(unsigned long)pm.readFailures,(unsigned long)pm.writeFailures,
      (unsigned long)pm.operationFailures,(unsigned long)pm.configFailures);
  }
  const auto imu=imuAcquisition.snapshot();
  const auto &activity=imu.activity;
  Serial.printf("SENSOR_IDLE enabled=%d resting=%d sensor_power=on_shared_rail adc_pause_after_ms=%lu pauses=%lu resumes=%lu experimental=1\n",
    LAUNCHLAB_MOTION_ADC_GATE,s.resting,(unsigned long)SensorIdle::IDLE_MS,
    (unsigned long)sensorIdle.pauses(),(unsigned long)sensorIdle.resumes());
  Serial.printf("MOTION_ACTIVITY valid=%d fresh=%d moving=%d gyro_dps=%.3f accel_change_g=%.4f gyro_on_dps=%.2f accel_on_g=%.3f confirm_ms=%lu idle_ms=%lu unused_shake=%d off_in_s=%lu\n",
    activity.valid,activity.fresh(esp_timer_get_time()),activity.moving,activity.gyroDps,activity.accelChangeG,
    MotionActivity::GYRO_ON,MotionActivity::ACCEL_ON,(unsigned long)(MotionActivity::CONFIRM_US/1000),
    (unsigned long)inactivity.elapsed(now),inactivity.unusedShake(),(unsigned long)((inactivity.remaining(now)+999)/1000));
  Serial.printf("MOUNT_STATUS selected=%s imu_frame=%s accel_gyro_frame=launcher raw_imu_frame=device display_flip=%d\n",
    MountOrientation::label(mountMode),MountOrientation::label(imu.mount),displayFlipped);
  Serial.printf("IMU_RESTORE_STATUS needed=%d failures=%lu capture_retries=%lu\n",motionRestoreNeeded,(unsigned long)motionRestoreFailures,(unsigned long)motionCaptureRetries);
  Serial.printf("IMU_STARTUP_STATUS attempts=%lu last_try_ms=%lu last_paired=%d last_success=%d retry_ms=%lu\n",
    (unsigned long)motionInitAttempts,(unsigned long)lastMotionInit,motionInitPaired,motionInitSuccess,
    (unsigned long)MOTION_INIT_RETRY_MS);
  Serial.printf("MOTION_STATUS imu_core=0 imu_task=%d paired_samples=%lu accel_samples=%lu max_gap_us=%lu gaps=%lu max_poll_us=%lu stack_free=%lu gate_misses=%lu late_polls=%lu missed_polls=%lu latest=%lu quality=%s pending=%d ref=%lu ref_rpm=%.1f ref_store_ready=%d ref_load_error=%d dropped=%lu demo=%d feedback=%d\n",
    imuAcquisition.running(),(unsigned long)imu.pairedSamples,(unsigned long)imu.accelSamples,
    (unsigned long)imu.maxGapUs,(unsigned long)imu.gaps,(unsigned long)imu.maxPollUs,(unsigned long)imu.stackFree,(unsigned long)imu.gateMisses,(unsigned long)imu.latePolls,(unsigned long)imu.missedPolls,
    (unsigned long)latestMotion.number,LaunchMotion::name(latestMotion.quality),motionPending,
    (unsigned long)referenceMotion.number,referenceMotion.rpm,motionStore.ready(),referenceError,
    (unsigned long)motionDrops,motionDemo,launchFeedback.shown());
  Serial.printf("ORIENTATION_STATUS algorithm=VQF6D position=disabled absolute_heading=disabled ready=%d rest=%d updates=%lu resets=%lu max_fusion_us=%lu bias_dps=%.4f,%.4f,%.4f bias_sigma_dps=%.4f\n",
    imu.orientationReady,imu.rest,(unsigned long)imu.fusionSamples,(unsigned long)imu.fusionResets,
    (unsigned long)imu.maxFusionUs,imu.bias[0],imu.bias[1],imu.bias[2],imu.biasSigma);
  Serial.printf("I2C_RECOVERY bus=internal result=%s pulses=%u sda_was=%d scl_was=%d\n",I2cBusRecovery::name(i2cRecovery),i2cRecoveryPulses,i2cSdaWas,i2cSclWas);
  Serial.printf("IMU_FIFO_STATUS reads=%lu errors=%lu skipped_frames=%lu anchors=%lu max_bytes=%lu period_us=%.1f gyro_samples=%lu accel_samples=%lu flash_quiet_polls=%lu length_errors=%lu data_errors=%lu parse_errors=%lu oversize=%lu overflows=%lu stalls=%lu last_stall_us=%lu last_stall_ms=%lu last_error_bytes=%lu last_error_offset=%lu last_error_ms=%lu last_quiet_ms=%lu..%lu\n",
    (unsigned long)imu.fifoReads,(unsigned long)imu.fifoErrors,(unsigned long)imu.fifoSkipped,(unsigned long)imu.fifoAnchors,
    (unsigned long)imu.fifoMaxBytes,imu.fifoPeriodUs,(unsigned long)imu.pairedSamples,(unsigned long)imu.accelSamples,(unsigned long)imu.quietPolls,
    (unsigned long)imu.fifoLengthErrors,(unsigned long)imu.fifoDataErrors,(unsigned long)imu.fifoParseErrors,(unsigned long)imu.fifoOversize,(unsigned long)imu.fifoOverflows,
    (unsigned long)imu.fifoStalls,(unsigned long)imu.lastStallUs,(unsigned long)imu.lastStallMs,
    (unsigned long)imu.lastErrorBytes,(unsigned long)imu.lastErrorOffset,(unsigned long)imu.lastErrorMs,
    (unsigned long)imuAcquisition.quietBeganMs(),(unsigned long)imuAcquisition.quietEndedMs());
  Serial.printf("MOTION_CLIP accel_range_g=%.0f gyro_range_dps=%.0f samples=%lu max_run=%lu last_run=%lu last_ms=%lu last_axes=0x%02lx\n",
    accelRange().fullScaleG(),StickS3AccelRange::GYRO_FULL_SCALE_DPS,(unsigned long)imu.clipSamples,(unsigned long)imu.maxClipRun,
    (unsigned long)imu.lastClipRun,(unsigned long)imu.lastClipMs,(unsigned long)imu.lastClipAxes);
  for(unsigned i=imu.gapNext>StickS3MotionAcquisition::Snapshot::GAPS?imu.gapNext-StickS3MotionAcquisition::Snapshot::GAPS:0;i<imu.gapNext;++i) {
    const auto &g=imu.gapLog[i%StickS3MotionAcquisition::Snapshot::GAPS];
    Serial.printf("MOTION_GAP n=%u at_ms=%lu gap_us=%lu polls=%lu empty_polls=%lu max_period_us=%lu max_lock_us=%lu max_poll_us=%lu max_ring_us=%lu\n",
      i+1,(unsigned long)g.atMs,(unsigned long)g.gapUs,(unsigned long)g.polls,(unsigned long)g.emptyPolls,
      (unsigned long)g.periodUs,(unsigned long)g.lockUs,(unsigned long)g.pollUs,(unsigned long)g.ringUs);
  }
}

// No serial reader is needed: isPlugged() uses IDF's SOF monitor, not CDC I/O.
LOOP_HELPER static void pollUsbAwake(uint32_t now,const Snapshot &s,bool force=false) {
  if(!force && !usbAwake.hostPollDue(now))return;
  usbAwake.sampleHost(now,HWCDC::isPlugged());
  if(usbAwake.powerPollDue(now) && s.phase!=AnalogTachometer::Phase::Launch) {
    uint8_t source=0;bool ok=false;
    if(imuAcquisition.lock()) {
      // Checked PWR_SRC read: wrappers that return zero also hide I2C errors.
      ok=pm1Idle().read(0x04,&source,1);
      imuAcquisition.unlock();
    }
    usbAwake.samplePower(now,ok,source);
  }
  const bool inhibited=usbAwake.inhibited(now);
  // A confirmed computer connection establishes use, even if the initial
  // optimistic SOF already inhibited shutdown on the preceding poll.
  if(inhibited && usbAwake.remembered() && usbAwake.powerKnown(now) &&
     (usbAwake.powerBits()&UsbHostAwake::VIN_MASK) && inactivity.unusedShake())inactivity.touch(now);
  if(inhibited!=usbAutoOffInhibited) {
    usbAutoOffInhibited=inhibited;
    // A disconnect starts a full ordinary battery timeout, not an overdue
    // timer accumulated during a long computer session. Touch only on edges.
    // A transient, unconfirmed boot SOF must not turn an accidental shake
    // wake into a full practice timeout. Confirmed detach was promoted above.
    if(!inactivity.unusedShake())inactivity.touch(now);
    lastShutdownTry=0;
    Serial.printf("USB_AUTO_OFF inhibited=%d reason=%s sof=%d host_seen=%d power_known=%d source=0x%02x\n",
      inhibited,usbAwake.reason(now),usbAwake.sof(),usbAwake.remembered(),usbAwake.powerKnown(now),usbAwake.powerBits());
  }
}

LOOP_HELPER static void pollBattery(uint32_t now,const Snapshot &s) {
  if(s.phase==AnalogTachometer::Phase::Launch || (batteryPolled && now-lastBatteryPoll<5000))return;
  if(!imuAcquisition.lock())return;
  batteryPolled=true;lastBatteryPoll=now;
  const int64_t started=esp_timer_get_time();
  uint16_t mv=0;uint8_t bits=0;
  bool voltageRead=false,chargeRead=false;
  if(pm1Idle().ensureAwake()) {
    voltageRead=M5.Power.M5pm1.getBatteryVoltage(&mv);
    chargeRead=M5.Power.M5pm1.getGPIOInputBits(&bits);
  }
  powerConfigValid=pm1Idle().read(0x06,&powerConfig,1);
  imuAcquisition.unlock();
  battery.update(mv,voltageRead,chargeRead,bits,now);
  maxBatteryPollUs=std::max(maxBatteryPollUs,uint32_t(esp_timer_get_time()-started));
  if(historyPage && historyView==PracticeUI::View::Battery)dirty=true;
}

LOOP_HELPER static void tickDisplayPower(uint32_t now,const Snapshot &s) {
  // Preserve the electrical conditions during a measured pull. A pull while
  // dimmed restores its feedback brightness only after measurement completes.
  if(s.phase==AnalogTachometer::Phase::Launch ||
     (s.phase==AnalogTachometer::Phase::Ready && !s.opticalIdle))return;
  const uint8_t selected=controls.page==Controls::Page::Edit && controls.setting==2?
    controls.draft:brightnessPercent;
  const uint8_t percent=IdlePowerPolicy::brightness(selected,inactivity.elapsed(now),
    usbAwake.inhibited(now),launchFeedback.shown());
  const uint8_t target=(unsigned(percent)*255+50)/100;
  if(M5.Display.getBrightness()==target)return;
  acquisition.displayBoundary(true);M5.Display.setBrightness(target);acquisition.displayBoundary(false);
  Serial.printf("DISPLAY_POWER selected_pct=%u actual_pct=%u idle_ms=%lu dimmed=%d\n",
    selected,percent,(unsigned long)inactivity.elapsed(now),percent<selected);
}

static void drawBattery(lgfx::LGFXBase &g,uint32_t now) {
  ControlsUI::battery(g,battery.valid(now),battery.percent,battery.millivolts,battery.chargeKnown,battery.charging);
}

// The applied value of each editable Settings row, in Controls::Setting order.
static uint8_t settingValue(unsigned row) {
  using S=Controls::Setting;
  switch(S(row)) {
    case S::Sleep:return inactivity.minutes();
    case S::Rpm:return uint8_t(rpmEstimator);
    case S::Brightness:return brightnessPercent;
    case S::Flip:return uint8_t(displayFlipped);
    case S::Sensor:return uint8_t(sensorProfile);
    case S::Theme:return uint8_t(themeLibrary.effective(appearance.theme));
    case S::BestEffect:return uint8_t(appearance.effect);
    case S::Celebrate:return uint8_t(milestones.mode());
    case S::Sound:return uint8_t(appearance.sound);
    case S::Mount:return uint8_t(mountMode);
    case S::ShakeWake:return uint8_t(shakeToWake);
    case S::WipeAll:break;
  }
  return 0;
}
static void drawSettings(lgfx::LGFXBase &g) {
  uint8_t values[Controls::EDITABLE_SETTING_COUNT];
  for(unsigned i=0;i<Controls::EDITABLE_SETTING_COUNT;++i)values[i]=settingValue(i);
  ControlsUI::settings(g,controls,values);
}

LOOP_HELPER static void exportSignal() {
  if(!acquisition.storage() || !acquisition.request(Action::Suspend)) {
    Serial.println("RAW_ERROR acquisition_suspend_failed");return;
  }
  const Snapshot s=acquisition.snapshot();
  if(s.ringCount) {
    Serial.printf("FLIGHT_EXPORT samples=%lu end_detector_us=%llu state=%s launches=%lu gpio=%u display_blanking=0\n",
      (unsigned long)s.ringCount,(unsigned long long)s.detectorUs,s.stateName(),(unsigned long)s.launches,s.pin);
    opticalTransferRaw(acquisition.storage(),StickS3Acquisition::RING_SAMPLES,s.ringFirst,s.ringCount,
      int64_t(s.ringCount)*1000000/ANALOG_SAMPLE_HZ,0,0,true,ESP_OK,false);
  } else Serial.println("RAW_ERROR no_flight_record");
  if(!acquisition.request(Action::Resume))Serial.println("ADC_ERROR resume_failed");
  dirty=true;
}

static void drawLiveLevel(lgfx::LGFXBase &view,int cx) {
  drawnLevel=StickS3Level::forDisplay(level.reading(millis()),visibleDisplayFlipped());
  ScreensUI::level(view,ui(),cx,drawnLevel);
}

// The recap's label: NEW BEST for a personal best, otherwise the milestone
// the pull reached; null when the recap celebrates nothing.
static const char *celebrationBadge() {
  static char text[24];
  if(motionDemo || !bestEffect.badge())return nullptr;
  if(bestEffect.best())return "NEW BEST";
  Milestones::milestoneText(text,sizeof(text),bestEffect.milestone());return text;
}
LOOP_HELPER static void draw(const Snapshot &s) {
  const int64_t started=esp_timer_get_time();
  const Palette::Colors colors=ui();
  ControlsUI::colors=colors;ControlsUI::themeLabel=[](Appearance::Theme t){return themeLibrary.label(t);};ControlsUI::milestones=&milestones;
  const uint32_t replayFrameElapsed=launchFeedback.replayElapsed(millis());
  const bool replayWasStarted=launchFeedback.replayStarted();
  auto &view=screenReady?static_cast<lgfx::LGFXBase &>(screen):static_cast<lgfx::LGFXBase &>(M5.Display);
  if(!screenReady){acquisition.displayBoundary(true);M5.Display.startWrite();}
  view.fillScreen(colors.background);
  const int cx=view.width()/2;
  view.setTextDatum(top_center);
  view.setTextColor(colors.text,colors.background);
  if(!tournamentMode && diagnostics)
    ScreensUI::diagnosticsHeader(view,colors,newSessionAt && millis()-newSessionAt<1500,s.stateName(),s.ready && !s.signalFault);
  view.setTextColor(colors.text,colors.background);
  if(tournamentMode) {
    ScreensUI::tournament(view,colors,s.ready && !s.signalFault,tournamentView==TournamentView::RecordingOnly,s.valid,s.resultRpm);
    if(int32_t(tournamentHintUntil-millis())>0)ControlsUI::footer(view,"B View  Hold A Exit");
  } else if(diagnostics) {
    ScreensUI::diagnostics(view,s.pin,s.last,s.minimum,s.maximum,s.rate);
  } else if(historyPage) {
    view.fillScreen(colors.background);
    switch(controls.page) {
      case Controls::Page::Menu:ControlsUI::list(view,"Menu",ControlsUI::MENU,Controls::MENU_COUNT,controls.menu);break;
      case Controls::Page::HistoryMenu:ControlsUI::list(view,"History",ControlsUI::HISTORY,4,controls.history);break;
      case Controls::Page::Settings:drawSettings(view);break;
      case Controls::Page::Edit:ControlsUI::editor(view,controls,settingsSaveError,
        (controls.setting==1 && controls.draft!=uint8_t(rpmEstimator)) ||
        (controls.setting==4 && controls.draft!=uint8_t(sensorProfile)));
        // The previewed effect frames the value text inside its card.
        if(screenReady && controls.setting==unsigned(Controls::Setting::BestEffect) && editorEffect.badge()) {
          const int64_t effectStarted=esp_timer_get_time();effectReadUs=effectStarted;BestEffectUI::Renderer::Anchor card;
          card.top=72;card.bottom=128;card.left=8;card.right=view.width()-9;
          drawEffect(editorEffect.effect(),editorEffect.elapsed(millis()),card,colors);
          maxEffectUs=std::max(maxEffectUs,uint32_t(esp_timer_get_time()-effectStarted));
        }
        break;
      case Controls::Page::NewSession:ControlsUI::session(view,controls.startSession);break;
      case Controls::Page::Battery:drawBattery(view,millis());break;
      case Controls::Page::Wipe:ControlsUI::wipe(view,wipeConfirmation,millis(),wipeFailed,wipeBlocked,wipeComplete);break;
      case Controls::Page::WearCheck: {
        const auto rawTilt=level.reading(millis());
        drawnLevel=StickS3Level::forDisplay(rawTilt,visibleDisplayFlipped());
        ControlsUI::wearCheck(view,controls.wear,WearCheck::Tilt(rawTilt),WearCheck::Tilt(drawnLevel),
          s.ready && !s.signalFault && !s.suspended && s.phase==AnalogTachometer::Phase::Ready);
        break;
      }
      default:PracticeUI::draw(view,demo?demoPractice:practice,historyView,historyOffset,demo,colors);break;
    }
  } else if(launchFeedback.shown()) {
    view.fillScreen(colors.background);
    MotionUI::feedback(view,motionDemo?demoMotion:latestMotion,
      motionPending && !motionDemo,motionDemo,MotionReplay::playbackProgress(motionDemo?demoMotion:latestMotion,replayFrameElapsed),visibleDisplayFlipped(),colors,celebrationBadge());
    ControlsUI::footer(view,"A Main  B Menu");
    // Direct-draw fallback stays functional without reading the hardware LCD.
    if(screenReady && !motionDemo && !motionPending && bestEffect.badge()) {
      const int64_t effectStarted=esp_timer_get_time();effectReadUs=effectStarted;
#if LAUNCHLAB_HIL
      if(!hilFxDraw(static_cast<uint16_t *>(screen.getBuffer()),screen.width(),screen.height(),bestEffect.elapsed(millis()),colors.accent,colors.background))
#endif
      drawEffect(bestEffect.effect(),bestEffect.elapsed(millis()),BestEffectUI::Renderer::Anchor(),colors);
      maxEffectUs=std::max(maxEffectUs,uint32_t(esp_timer_get_time()-effectStarted));
#if LAUNCHLAB_HIL
      if(effectFrames.count<128 && bestEffect.moving(millis())){
        effectFrames.elapsed[effectFrames.count]=uint16_t(bestEffect.elapsed(millis()));
        effectFrames.us[effectFrames.count++]=uint16_t(std::min<int64_t>(65535,esp_timer_get_time()-effectStarted));}
#ifdef BEST_EFFECT_PROFILE
      if(effectStages.count<64 && bestEffect.moving(millis())) {
        const unsigned n=effectStages.count++;effectStages.elapsed[n]=uint16_t(bestEffect.elapsed(millis()));
        effectStages.us[n]=uint32_t(esp_timer_get_time()-effectStarted);
        std::memcpy(effectStages.cycles[n],bestEffectRenderer.stage,sizeof(effectStages.cycles[n]));
      }
#endif
#endif
    }
  } else {
    const bool sessionNotice=newSessionAt && millis()-newSessionAt<1500 &&
      s.ready && !s.signalFault && !s.suspended && s.phase==AnalogTachometer::Phase::Ready;
    const char *status=sessionNotice?"New session ready":s.stateName();
    ScreensUI::live(view,colors,status,sessionNotice?ScreensUI::Tone::Text:s.ready && !s.signalFault?ScreensUI::Tone::Ready:ScreensUI::Tone::Warning,
      s.valid,s.resultRpm);
    drawLiveLevel(view,cx);
  }
  if(!tournamentMode && (diagnostics || (!historyPage && !launchFeedback.shown()))) {
    view.setTextDatum(top_center);view.setTextColor(colors.muted,colors.background);
    ControlsUI::footer(view,"A Recap  B Menu");
  }
  const uint32_t now=millis();
  if(M5.BtnA.isPressed() && !gestureA.consumed && (tournamentMode || (controls.page==Controls::Page::Main && !launchFeedback.shown())))
    ControlsUI::hold(view,tournamentMode?"Exit tournament":"Enter tournament",now-M5.BtnA.lastChange(),1000);
  else if(M5.BtnB.isPressed() && !gestureB.consumed && (controls.page!=Controls::Page::Main || launchFeedback.shown()) && !tournamentMode)
    ControlsUI::hold(view,controls.page==Controls::Page::Edit?"Cancel change":"Back",now-M5.BtnB.lastChange(),1000);
  // An internal canvas is pushed by DMA, which endWrite does not wait for.
  // Wait so the transfer stays inside the boundary marks and the next frame
  // cannot overwrite pixels still being sent.
  if(screenReady){acquisition.displayBoundary(true);M5.Display.startWrite();screen.pushSprite(0,0);M5.Display.waitDMA();}
  M5.Display.endWrite();
  if(effectReadUs){presentLagUs=uint32_t(esp_timer_get_time()-effectReadUs);effectReadUs=0;}
  acquisition.displayBoundary(false);
  if(!tournamentMode && !diagnostics && !historyPage && launchFeedback.shown() && !motionDemo && !motionPending)
    bestEffect.presented(millis());
  // The sound starts with the effect's first presented frame. The editor's
  // preview holds its first frame until its sound is rendered, so the two
  // start together.
  if(controls.page==Controls::Page::Edit && controls.setting==unsigned(Controls::Setting::BestEffect) &&
     (appearance.sound==Appearance::Sound::Off || !editorEffect.badge() || soundPlayer.prepared(editorEffect.effect())))editorEffect.presented(millis());
  if(bestEffect.takeCue()) {
    Serial.printf("CELEBRATION_START launch=%lu effect=%s milestone=%u best=%d\n",(unsigned long)latestMotion.number,
      Appearance::label(bestEffect.effect()),unsigned(bestEffect.milestone()),bestEffect.best());
    requestSound(bestEffect.effect(),appearance.sound,true);
    // Keep this pick's clip ready until its first frame has started playback.
    // Only a presented Best effect consumes Random's next pick; a milestone
    // plays its own effect.
    if(appearance.effect==Appearance::Effect::Random && !bestEffect.milestone())upcomingEffect=EffectSet::pick(esp_random(),bestEffect.effect(),effectSet.mask());
    // A milestone uses up its row's pick.
    if(bestEffect.milestone())nextMilestonePick(bestEffect.milestone(),bestEffect.effect());
  }
  if(editorEffect.takeCue())requestSound(editorEffect.effect(),appearance.sound,false);
  launchFeedback.replayPresented(millis(),!tournamentMode && !diagnostics && !historyPage &&
    (!motionPending || motionDemo) && (motionDemo?demoMotion:latestMotion).valid() && (motionDemo?demoMotion:latestMotion).fused,replayFrameElapsed);
  if(!motionDemo && !replayWasStarted && launchFeedback.replayStarted())
    Serial.printf("RECAP_PRESENT launch=%lu after_end_ms=%lu draw_us=%lu\n",(unsigned long)latestMotion.number,
      (unsigned long)((esp_timer_get_time()-motionEnd)/1000),(unsigned long)(esp_timer_get_time()-started));
  maxDrawUs=std::max(maxDrawUs,uint32_t(esp_timer_get_time()-started));
}

LOOP_HELPER static void pollLevel(uint32_t now) {
  // Fusion remains continuous on its own task. The UI needs fresh level data
  // only at 50 Hz, rather than copying the full snapshot on every loop pass.
  if(now-lastImuPoll<IdlePowerPolicy::LEVEL_POLL_MS)return;
  lastImuPoll=now;
  if(imuAcquisition.running()) {
    const auto sample=imuAcquisition.snapshot();
    const uint64_t clockUs=esp_timer_get_time();
    if(sample.activity.fresh(clockUs) && sample.activity.lastMotionUs>lastActivityMotionUs &&
       clockUs>=sample.activity.lastMotionUs && clockUs-sample.activity.lastMotionUs<=MotionActivity::FRESH_US) {
      lastActivityMotionUs=sample.activity.lastMotionUs;inactivity.motion(now);
    }
    imuStill.observe(now,sample.activity.fresh(clockUs) && !sample.activity.moving);
    if(sample.fusedUs && sample.fusedUs!=lastLevelSampleUs) {
      lastLevelSampleUs=sample.fusedUs;
      if(sample.orientationReady)level.feed(sample.gravity[0],sample.gravity[1],sample.gravity[2],uint32_t(sample.fusedUs/1000));
      else level.feed(0,0,0,uint32_t(sample.fusedUs/1000));
    }
    return;
  }
  imuStill.observe(now,false);
  if(!M5.Imu.isEnabled())return;
  const int64_t started=esp_timer_get_time();
  // update's returned accel bit establishes freshness; cached data is not a new sample.
  if(M5.Imu.update() & m5::IMU_Class::sensor_mask_accel) {
    const auto data=M5.Imu.getImuData();
    const auto &range=accelRange();
    const float native[3]={range.toG(data.accel.x),range.toG(data.accel.y),range.toG(data.accel.z)};float mapped[3];
    MountOrientation::mapVector(liveMount,native,mapped);
    level.feed(mapped[0],mapped[1],mapped[2],millis());
  }
  maxImuPollUs=std::max(maxImuPollUs,uint32_t(esp_timer_get_time()-started));
}

LOOP_HELPER static void exportScreen() {
  if(!screenReady){Serial.println("SNAP_ERROR no_draw_buffer");return;}
  // Export the last displayed frame, including its frozen level during a pull.
  const unsigned width=screen.width(),height=screen.height();
  Serial.printf("SNAP %u %u %u\n",width,height,width*height*2);
  uint8_t row[240*2];
  for(unsigned y=0;y<height;++y) {
    for(unsigned x=0;x<width;++x) {
      const uint16_t pixel=screen.readPixel(x,y);row[x*2]=pixel>>8;row[x*2+1]=pixel;
    }
    Serial.write(row,width*2);
  }
}

static void toggleFeedback() {
  if(tournamentMode)return;
  bestEffect.clear();
  historyPage=diagnostics=motionDemo=demo=false;
  launchFeedback.toggle(millis(),latestMotion.number!=0);dirty=true;
  Serial.printf("MAIN_FEEDBACK shown=%d reason=button t_ms=%lu\n",launchFeedback.shown(),(unsigned long)millis());
}

static void buttonA();
static void buttonB();
LOOP_HELPER static void toggleHistory(){buttonB();}
LOOP_HELPER static void nextView(){buttonA();}
LOOP_HELPER static void prepareMotion(const Snapshot &event) {
  lastAcceptedAt=millis();if(!historyPendingSince)historyPendingSince=lastAcceptedAt;
  if(motionPending)++motionDrops;
  latestMotion={};latestMotion.number=practice.recent()->number;latestMotion.rpm=event.resultRpm;
  const bool beat=bestStore.ready() && personalBests.observe(unsigned(event.sensorProfile),unsigned(event.estimator),event.valid,event.resultRpm);
  // The selected estimator's RPM is the pull's RPM, for milestones as for bests.
  const bool visible=!tournamentMode && !historyPage && !diagnostics;
  const auto decision=visible?Milestones::decide(milestones,event.valid,event.resultRpm,beat,
    beat && milestones.mode()!=Milestones::Mode::Milestones?celebrationEffect():Appearance::Effect::Off,milestonePicks()):Milestones::Decision{};
  bestEffect.queue(decision.show,latestMotion.number,decision.effect,decision.best,decision.milestone);
  Serial.printf("CELEBRATION launch=%lu rpm=%.1f shown_rpm=%ld valid=%d mode=%s best=%d milestone=%u effect=%s celebrate=%d\n",(unsigned long)latestMotion.number,
    event.resultRpm,Milestones::shown(event.resultRpm),event.valid,Milestones::name(milestones.mode()),beat,unsigned(decision.milestone),
    decision.show?Appearance::playedLabel(decision.effect):"none",decision.show);
  motionStart=event.burstStartHostUs;motionEnd=event.burstEndHostUs;motionPending=true;
  // Update the recording regardless of navigation, without stealing an edit
  // or revealing results in tournament. Main/recap alone follows new pulls.
  if(!tournamentMode && !historyPage && !diagnostics) {
    motionDemo=demo=false;launchFeedback.show(millis());
  }
  dirty=true;
}
LOOP_HELPER static void finishMotion() {
  if(!motionPending)return;
  const uint64_t now=esp_timer_get_time();const auto imu=imuAcquisition.snapshot();
  if(!LaunchMotion::captureReady(now,motionEnd,imu.fusedUs))return;
  // A failed ring copy (allocation or a busy ring gate) is transient: retry
  // while the samples are still buffered instead of losing the replay.
  if(!imuAcquisition.capture(motionStart,motionEnd,latestMotion.rpm,latestMotion.number,latestMotion) &&
     now<motionEnd+MOTION_CAPTURE_RETRY_US){++motionCaptureRetries;return;}
  motionPending=false;dirty=true;
  const auto state=acquisition.snapshot();
  if(state.phase==AnalogTachometer::Phase::Launch || !launchFeedback.shown() || tournamentMode || historyPage || diagnostics || motionDemo)bestEffect.clear();
  else bestEffect.captured(latestMotion.number);
  if(launchFeedback.shown())launchFeedback.show(millis());
  Serial.printf("MAIN_FEEDBACK shown=%d reason=result t_ms=%lu\n",launchFeedback.shown(),(unsigned long)millis());
  Serial.printf("MOTION_RESULT launch=%lu quality=%s points=%u duration_ms=%lu rpm=%.1f stationary_bias=%d saturated=%d bridged=%d capture_retries=%lu peak_accel_g=%.2f accel_range_g=%.0f peak_gyro_dps=%.0f gyro_range_dps=%.0f\n",
    (unsigned long)latestMotion.number,LaunchMotion::name(latestMotion.quality),latestMotion.count,
    (unsigned long)latestMotion.durationMs,latestMotion.rpm,latestMotion.stationaryBias,latestMotion.saturated,latestMotion.bridged,(unsigned long)motionCaptureRetries,
    latestMotion.peakAccelG,accelRange().fullScaleG(),latestMotion.peakGyroDps,StickS3AccelRange::GYRO_FULL_SCALE_DPS);
  if(latestMotion.issueMs!=LaunchMotion::Trace::NO_ISSUE)
    Serial.printf("MOTION_ISSUE launch=%lu quality=%s at_ms=%ld pull_ms=%lu\n",(unsigned long)latestMotion.number,
      LaunchMotion::name(latestMotion.quality),(long)latestMotion.issueMs,(unsigned long)((motionEnd-motionStart)/1000));
  Serial.printf("MOTION_TIMING launch=%lu ready_after_end_ms=%lu pre_ms=%d post_ms=%d\n",
    (unsigned long)latestMotion.number,(unsigned long)((now-motionEnd)/1000),
    latestMotion.count?-int(latestMotion.points[0].ms):0,
    latestMotion.count?int(latestMotion.points[latestMotion.count-1].ms)-int(latestMotion.durationMs):0);
  Serial.printf("RECAP_LEVEL launch=%lu start_valid=%d start_xy=%.4f,%.4f end_valid=%d end_xy=%.4f,%.4f\n",
    (unsigned long)latestMotion.number,latestMotion.startLevel.valid(),latestMotion.startLevel.right,latestMotion.startLevel.down,
    latestMotion.endLevel.valid(),latestMotion.endLevel.right,latestMotion.endLevel.down);
}
LOOP_HELPER static void exportMotion() {
  for(const auto *t:{&referenceMotion,&latestMotion}) {
    const char *role=t==&referenceMotion?"reference":"latest";
    Serial.printf("MOTION_EXPORT role=%s number=%lu rpm=%.3f quality=%s count=%u duration_ms=%lu stationary_bias=%d fused=%d\n",role,
      (unsigned long)t->number,t->rpm,LaunchMotion::name(t->quality),t->count,(unsigned long)t->durationMs,t->stationaryBias,t->fused);
    if(t->valid())for(unsigned i=0;i<t->count;++i) {
      const auto &p=t->points[i];Serial.printf("MOTION_POINT role=%s t_ms=%d accel_change_g=%.3f,%.3f,%.3f gyro_dps=%.1f,%.1f,%.1f q=%.5f,%.5f,%.5f,%.5f\n",
        role,p.ms,p.a[0]*.001f,p.a[1]*.001f,p.a[2]*.001f,p.g[0]*.1f,p.g[1]*.1f,p.g[2]*.1f,
        p.q[0]/16384.0f,p.q[1]/16384.0f,p.q[2]/16384.0f,p.q[3]/16384.0f);
    }
    auto replay=std::unique_ptr<MotionReplay::Replay>(new(std::nothrow) MotionReplay::Replay);
    if(replay && replay->build(*t)) {
      Serial.printf("REPLAY_EXPORT role=%s number=%lu mode=rotation_only position=disabled absolute_heading=disabled count=%u\n",
        role,(unsigned long)t->number,MotionReplay::Replay::COUNT);
      for(unsigned i=0;i<MotionReplay::Replay::COUNT;++i) {
        const auto &q=replay->pose[i];Serial.printf("REPLAY_POINT role=%s t_ms=%d gravity_aligned_q=%.5f,%.5f,%.5f,%.5f\n",role,
          replay->ms[i],q.w,q.x,q.y,q.z);
      }
    }
  }
}
LOOP_HELPER static void newSession() {
  bestEffect.clear();
  practice.newSession();newSessionAt=millis();demo=false;diagnostics=false;historyPage=false;dirty=true;
  Serial.println("HISTORY_SESSION next accepted launch starts a new session");
}
LOOP_HELPER static void previewSetting() {
  const auto s=acquisition.snapshot();
  if(s.phase==AnalogTachometer::Phase::Launch){restorePreview=true;return;}
  if(restorePreview) {
    M5.Display.setBrightness(backlightValue());M5.Display.setRotation(DisplayOrientation::rotation(displayFlipped));
    restorePreview=false;
  }
  if(controls.page==Controls::Page::Edit) {
    if(controls.setting==2)M5.Display.setBrightness((unsigned(controls.draft)*255+50)/100);
    if(controls.setting==3)M5.Display.setRotation(DisplayOrientation::rotation(bool(controls.draft)));
  }
}
// Random's upcoming pick stays available until a celebration is presented.
LOOP_HELPER static Appearance::Effect upcoming() {
  if(!Appearance::celebrates(unsigned(upcomingEffect)))upcomingEffect=EffectSet::pick(esp_random(),Appearance::Effect::Off,effectSet.mask());
  return upcomingEffect;
}
LOOP_HELPER static const Appearance::Effect *milestonePicks() {
  if(!milestonePicksReady || !milestonePicked.same(milestones)) {
    for(unsigned i=0;i<milestones.count();++i)milestoneNext[i]=Milestones::pick(Milestones::playable(milestones.row(i).pool),esp_random(),Appearance::Effect::Off);
    milestonePicked=milestones;milestonePicksReady=true;
  }
  return milestoneNext;
}
LOOP_HELPER static void nextMilestonePick(uint16_t rpm,Appearance::Effect played) {
  milestonePicks();
  for(unsigned i=0;i<milestones.count();++i)
    if(milestones.row(i).rpm==rpm)milestoneNext[i]=Milestones::pick(Milestones::playable(milestones.row(i).pool),esp_random(),played);
}
LOOP_HELPER static Appearance::Effect celebrationEffect() {
  // A loaded Best effect that cannot play here (deleted, damaged) plays its fallback.
  if(appearance.effect!=Appearance::Effect::Random)return Appearance::resolve(appearance.effect);
  return upcoming();
}
// Keep a queued recap's clip intact until its first presented frame takes the
// sound cue, including USB previews of an effect other than the saved one.
LOOP_HELPER static Appearance::Effect soundTarget(){
  if(bestEffect.badge() && !bestEffect.started())return bestEffect.effect();
  if(editorEffect.badge() && !editorEffect.started())return editorEffect.effect();
  return appearance.effect==Appearance::Effect::Random?upcoming():appearance.effect;
}
// Plays now if the sound is rendered, otherwise once the idle slices finish it.
LOOP_HELPER static void requestSound(Appearance::Effect effect,Appearance::Sound level,bool celebration) {
  soundRequest={};
  if(level==Appearance::Sound::Off){soundPlayer.stop();celebrationSound=soundPreview=false;return;}
  soundRequest.active=true;soundRequest.celebration=celebration;soundRequest.effect=effect;soundRequest.level=level;
  if(soundPlayer.prepared(effect))startSound(false);
}
// Starts the requested sound in step with its effect (SoundSync). At the
// effect's cue the effect's clock moves to where the sound will be heard; a
// sound ready only later (`late`) starts at the sample matching the frame
// then on the glass.
LOOP_HELPER static void startSound(bool late) {
  auto &shown=soundRequest.celebration?bestEffect:editorEffect;
  const bool synced=shown.started() && shown.effect()==soundRequest.effect;
  size_t skip=0;
  if(late && synced && soundPlayer.power())
    skip=SoundSync::skipSamples(millis(),soundPlayer.nextOutputUs(),presentLagUs,shown.startedAt(),BestEffectSound::RATE);
  const bool started=soundPlayer.play(soundRequest.effect,soundRequest.level,skip);
  if(started){soundSkipped=uint32_t(skip);soundShiftMs=0;}
  if(started && synced && !late) {
    const uint32_t at=SoundSync::clockStart(millis(),soundPlayer.outputUs(),presentLagUs);
    soundShiftMs=int32_t(at-shown.startedAt());shown.align(at);
  }
  (soundRequest.celebration?celebrationSound:soundPreview)=started;soundRequest.active=false;
}
// Each B in the Sound editor plays the current effect at the draft level.
LOOP_HELPER static void previewSound(){requestSound(soundTarget(),Appearance::Sound(controls.draft),false);}
// Each B in the Best effect editor, and opening it, plays the draft around the
// value card (with its sound when Sound is on); Random shows a random pick.
LOOP_HELPER static void previewEffect() {
  static Appearance::Effect lastShown=Appearance::Effect::Off;
  const auto choice=Appearance::Effect(controls.draft);
  // A new preview silences the last one: its first frame must not hold under
  // the old sound while its own renders.
  editorEffect.clear();dirty=true;
  if(soundPreview || soundRequest.active){soundPlayer.stop();soundPreview=false;soundRequest={};}
  if(!Appearance::celebrates(unsigned(choice)) && choice!=Appearance::Effect::Random){soundPlayer.stop();soundPreview=false;soundRequest={};return;}
  lastShown=choice==Appearance::Effect::Random?EffectSet::pick(esp_random(),lastShown,effectSet.mask()):choice;
  editorEffect.queue(true,1,lastShown);editorEffect.captured(1);
}
static void cancelSettingEdit() {
  if(controls.page==Controls::Page::Wipe)cancelWipe();
  if(controls.page==Controls::Page::Edit)controls.page=Controls::Page::Settings;
  restorePreview=true;settingsSaveError=false;previewSetting();
}
// Runs inside a checkpoint the caller holds, ordinary or shutdown: the
// detector's estimator and sensor profile can only change there, and the IMU
// frame only outside a pull or capture. A cancelled power-off then leaves the
// live state matching the records. Whatever fails stays pending.
static void writeSettings() {
  const uint16_t items=settingsSave.pending();
  if(!items)return;
  uint16_t saved=0;
  const auto live=acquisition.snapshot();
  if(items&SettingsSave::Rpm) {
    bool current=live.estimator==rpmEstimator;
    // A metric change starts a new session at the moment the detector switches.
    if(!current && acquisition.request(Action::Estimator,uint8_t(rpmEstimator))){current=true;practice.newSession();launchFeedback.clear();}
    if(current && RpmEstimatorSetting::save(prefs,rpmEstimator))saved|=SettingsSave::Rpm;
  }
  if(items&SettingsSave::Sensor) {
    bool current=live.sensorProfile==sensorProfile;
    if(!current && acquisition.request(Action::SensorProfile,uint8_t(sensorProfile))) {
      current=true;practice.newSession();launchFeedback.clear();
      // Another sensor's marks: its own reference starts empty.
      startMarks.clear(uint8_t(sensorProfile));startMarksDirty=true;saveStartMarks();
    }
    if(current && SensorProfileSetting::save(prefs,sensorProfile))saved|=SettingsSave::Sensor;
  }
  if((items&SettingsSave::Sleep) && prefs.putUChar("sleep_min",inactivity.minutes())==1)saved|=SettingsSave::Sleep;
  if((items&SettingsSave::Brightness) && prefs.putUChar(Brightness::KEY,brightnessPercent)==1)saved|=SettingsSave::Brightness;
  if((items&SettingsSave::Flip) && DisplayFlipSetting::save(prefs,displayFlipped))saved|=SettingsSave::Flip;
  if((items&SettingsSave::ShakeWake) && ShakeWakeSetting::save(prefs,shakeToWake))saved|=SettingsSave::ShakeWake;
  if(items&SettingsSave::Mount) {
    bool current=liveMount==mountMode;
    if(!current && imuAcquisition.setMount(mountMode)) {
      // Level restarts in the new frame; a recap drawn in the old one goes.
      current=true;liveMount=mountMode;level=StickS3Level{};drawnLevel={};lastLevelSampleUs=0;
      launchFeedback.clear();
    }
    if(current && MountOrientationSetting::save(prefs,mountMode))saved|=SettingsSave::Mount;
  }
  if((items&SettingsSave::Appearance) && appearanceStore.saveViewed(appearance))saved|=SettingsSave::Appearance;
  if((items&SettingsSave::Milestones) && milestoneStore.save(milestones))saved|=SettingsSave::Milestones;
  if((items&SettingsSave::Pin) && prefs.putUChar("ao_pin",live.pin)==1)saved|=SettingsSave::Pin;
  if((items&SettingsSave::ExtPower) && prefs.putBool("ext5v",extPower)==1)saved|=SettingsSave::ExtPower;
  settingsSave.wrote(saved);dirty=true;
  Serial.printf("SETTINGS_SAVE items=0x%03x saved=0x%03x\n",unsigned(items),unsigned(saved));
}
// Pending settings reach flash in the first checkpoint the optical owner
// grants: right away after Apply in the usual case, otherwise as soon as the
// signal is quiet again. Never during a pull or a motion capture.
LOOP_HELPER static void saveSettings(uint32_t now,const Snapshot &s,bool immediate=false) {
  if(!settingsSave.pending() || (!immediate && !settingsSave.due(now)) || motionPending ||
     !SettingsSave::ownerIdle(s.ready,s.suspended,s.opticalIdle,s.signalFault))return;
  // Record the attempt first: a vetoed checkpoint must not repeat the IMU
  // quiet wait on every UI tick.
  settingsSave.attempted(now);
  if(!acquisition.request(Action::IdleCheckpointBegin,s.launches)){settingsSave.refused();return;}
  writeSettings();
  if(!acquisition.request(Action::CheckpointEnd))Serial.println("SETTING_ERROR acquisition_resume");
}
// Apply takes the choice at once and queues its record for flash. It never
// waits for an idle instant: pressing A can itself jiggle the launcher across
// a mark, and that used to refuse Apply after Apply with "Try again when idle".
static void commitSetting() {
  const uint8_t value=controls.draft;
  bool applied=true,changed=false;
  switch(Controls::Setting(controls.setting)) {
    case Controls::Setting::Sleep:
      changed=value!=inactivity.minutes();applied=inactivity.setMinutes(value);break;
    // The estimator, sensor profile and mount switch inside the save checkpoint.
    case Controls::Setting::Rpm:
      applied=RpmEstimator::valid(value);changed=applied && RpmEstimator::Mode(value)!=rpmEstimator;
      if(changed)rpmEstimator=RpmEstimator::Mode(value);
      break;
    case Controls::Setting::Brightness:changed=value!=brightnessPercent;brightnessPercent=value;break;
    case Controls::Setting::Flip:changed=bool(value)!=displayFlipped;displayFlipped=value;break;
    case Controls::Setting::ShakeWake:changed=bool(value)!=shakeToWake;shakeToWake=value;break;
    case Controls::Setting::Sensor:
      applied=StickS3SensorProfile::valid(value);changed=applied && StickS3SensorProfile::Mode(value)!=sensorProfile;
      if(changed)sensorProfile=StickS3SensorProfile::Mode(value);
      break;
    case Controls::Setting::Mount:
      applied=MountOrientation::valid(value);changed=applied && MountOrientation::Mode(value)!=mountMode;
      if(changed)mountMode=MountOrientation::Mode(value);
      break;
    case Controls::Setting::Theme:case Controls::Setting::BestEffect:case Controls::Setting::Sound: {
      // An unreadable stored look cannot be replaced; keep showing it rather
      // than a choice that would silently revert at the next boot.
      using S=Controls::Setting;const auto row=S(controls.setting);
      auto next=appearance;
      applied=appearanceStore.ready() && (row!=S::Theme || themeLibrary.available(Appearance::Theme(value))) &&
        next.select(row==S::Theme?Appearance::Theme(value):appearance.theme,
                      row==S::BestEffect?Appearance::Effect(value):appearance.effect,
                      row==S::Sound?Appearance::Sound(value):appearance.sound);
      changed=applied && next.generation()!=appearance.generation();
      if(changed)appearance=next;
      break;
    }
    case Controls::Setting::Celebrate: {
      auto next=milestones;
      applied=milestoneStore.ready() && next.select(Milestones::Mode(value));
      changed=applied && next.generation()!=milestones.generation();
      if(changed)milestones=next;
      break;
    }
    case Controls::Setting::WipeAll:applied=false;break; // Erasure never uses Apply or draft values.
  }
  settingsSaveError=!applied;
  Serial.printf("SETTING_APPLY row=%u value=%u applied=%d changed=%d\n",controls.setting,value,applied,changed);
  if(applied) {
    if(changed)settingsSave.mark(SettingsSave::item(controls.setting));
    controls.page=Controls::Page::Settings;restorePreview=true;previewSetting();
    saveSettings(millis(),acquisition.snapshot(),true);
  }
  syncNavigation();
}
LOOP_HELPER static void logLaunchMetrics(const Snapshot &s) {
  const auto *record=practice.recent();if(!record)return;
  Serial.printf("LAUNCH_METRICS number=%lu session=%lu selected_estimator=%s selected_rpm=%.1f three_turn_peak_rpm=%.1f single_turn_peak_rpm=%.1f units=rpm\n",
    (unsigned long)record->number,(unsigned long)record->session,RpmEstimator::name(s.estimator),
    s.resultRpm,s.sustainedPeakRpm,s.singlePeakRpm);
}
static void browseOlder() {
  if(tournamentMode || !historyPage)return;
  const auto &h=demo?demoPractice:practice;
  const unsigned total=PracticeUI::count(h,historyView),step=PracticeUI::window(historyView);
  historyOffset=historyOffset+step<total?historyOffset+step:0;dirty=true;
}
static void navigate(Controls::Effect effect,Controls::Page previous) {
  if(previous==Controls::Page::Wipe && controls.page!=previous)wipeConfirmation.cancel();
  if(previous!=controls.page || effect==Controls::Effect::Recap || effect==Controls::Effect::Tournament)bestEffect.clear();
  if(previous==Controls::Page::Main && controls.page!=previous)launchFeedback.clear();
  if(previous!=controls.page){diagnostics=false;motionDemo=false;demo=false;historyOffset=0;}
  switch(effect) {
    case Controls::Effect::Recap:toggleFeedback();break;
    case Controls::Effect::BeginEdit:
      settingsSaveError=false;
      controls.draft=settingValue(controls.setting);
      if(controls.setting==unsigned(Controls::Setting::BestEffect))previewEffect();
      break;
    case Controls::Effect::Preview:settingsSaveError=false;previewSetting();
      if(controls.setting==unsigned(Controls::Setting::BestEffect))previewEffect();
      if(controls.setting==unsigned(Controls::Setting::Sound))previewSound();
      break;
    case Controls::Effect::Apply:commitSetting();break;
    case Controls::Effect::CancelEdit:cancelSettingEdit();break;
    case Controls::Effect::Older:browseOlder();break;
    case Controls::Effect::NewSession:newSession();break;
    case Controls::Effect::Tournament:
      if(previous==Controls::Page::Tournament && controls.page==previous)
        tournamentView=TournamentView((unsigned(tournamentView)+1)%2);
      else if(controls.page==Controls::Page::Tournament)tournamentView=TournamentView::RecordingOnly;
      launchFeedback.clear();tournamentHintUntil=millis()+2500;
      Serial.printf("TOURNAMENT_CHANGE enabled=%d display=%s\n",controls.page==Controls::Page::Tournament,tournamentViewName());
      break;
    case Controls::Effect::BeginWipe:beginWipe();break;
    case Controls::Effect::ConfirmWipe:if(wipeConfirmation.confirm(millis()))dirty=true;break;
    case Controls::Effect::CancelWipe:cancelWipe();break;
    case Controls::Effect::None:break;
  }
  syncNavigation();
}
static void buttonA(){const auto p=controls.page;const auto e=controls.tapA();navigate(e,p);}
static void buttonB(){const auto p=controls.page;const auto e=controls.tapB();navigate(e,p);}
LOOP_HELPER static void holdA(){const auto p=controls.page;const auto e=controls.holdA(launchFeedback.shown());navigate(e,p);}
LOOP_HELPER static void holdB(){const auto p=controls.page;const auto e=controls.holdB(launchFeedback.shown());navigate(e,p);}
static void beginWipe() {
  restoreRx.cancel();
  wipeFailed=false;wipeConfirmation.begin(millis());
  controls.page=Controls::Page::Wipe;controls.setting=unsigned(Controls::Setting::WipeAll);
  gestureA.consumed=gestureB.consumed=true;
  syncNavigation();
}
static void cancelWipe() {
  wipeConfirmation.cancel();
  // After an erase attempt, old RAM must never be saved back into storage.
  controls.page=wipeBlocked?Controls::Page::Wipe:Controls::Page::Settings;
  controls.setting=unsigned(Controls::Setting::WipeAll);
  gestureA.consumed=gestureB.consumed=true;
  syncNavigation();
}
LOOP_HELPER static void restartAfterWipe() {
  if(wipeRestartRequested)return;
  wipeRestartRequested=true;sleepReceipt.magic=0;
  Serial.println("WIPE_RESTART");UsbTx::drain(200);delay(150);ESP.restart();
}
LOOP_HELPER static void commitWipe(const Snapshot &s) {
  // Only pollWipe's fresh physical three-second hold calls this function.
  // An active capture or newly started optical burst cannot cross the erase.
  if(!wipeCheckpointHeld && !wipeBootRecovery) {
    if(motionPending || !acquisition.request(Action::IdleCheckpointBegin,s.launches)) {
      wipeFailed=true;wipeConfirmation.begin(millis());dirty=true;
      Serial.println("WIPE_DEFERRED wait_until_idle");return;
    }
    wipeCheckpointHeld=true;
  }
  wipeBlocked=true; // Set before the first write; retained on every failure.
  soundPlayer.stop();soundRequest={};celebrationSound=soundPreview=false;
  bestEffect.clear();editorEffect.clear();launchFeedback.clear();
  restoreRx.cancel();
  startMarks.clear(uint8_t(sensorProfile));startMarksDirty=false;  // the wipe takes its key too
  DeviceWipe::NvsStore store;
  wipeComplete=DeviceWipe::begin(store)==DeviceWipe::Result::Complete;
  wipeFailed=!wipeComplete;wipeConfirmation.cancel();dirty=true;
  Serial.println(wipeComplete?"WIPE_OK release_buttons_to_restart":"WIPE_ERROR retry_or_restart_no_normal_saves");
  // Keep the ADC checkpoint held. Success restarts on button release; a
  // partial wipe may only retry/reboot, never resume ordinary RAM saves.
}
LOOP_HELPER static void pollWipe(uint32_t now,bool buttonsFresh,const Snapshot &s) {
  if(controls.page!=Controls::Page::Wipe)return;
  const bool aDown=M5.BtnA.isPressed(),bDown=M5.BtnB.isPressed();
  if(wipeComplete) {
    if(buttonsFresh && !aDown && !bDown)restartAfterWipe();
    return;
  }
  const bool clickedA=buttonsFresh && gestureA.click(M5.BtnA.wasPressed(),M5.BtnA.wasHold(),M5.BtnA.wasClicked());
  const bool clickedB=buttonsFresh && gestureB.click(M5.BtnB.wasPressed(),M5.BtnB.wasHold(),M5.BtnB.wasClicked());
  // Launches defer screen redraws. Do not accept an unseen warning or count a
  // hold while the display is frozen; require a new press after measurement.
  const bool measuring=s.phase==AnalogTachometer::Phase::Launch;
  if(measuring)gestureA.consumed=true;
  const bool wasConfirming=wipeConfirmation.active();
  if(wipeConfirmation.tick(now,aDown,bDown,buttonsFresh && !measuring)) {commitWipe(s);return;}
  if(!wipeConfirmation.active()) {
    if(wasConfirming){cancelWipe();return;}
    if(!wipeBlocked){cancelWipe();return;}
    if(clickedA)beginWipe();
    else if(clickedB)restartAfterWipe();
    return;
  }
  if(clickedA && !measuring)buttonA();
  else if(clickedB)cancelWipe();
  if(now-lastDraw>=50)dirty=true;
}
LOOP_HELPER static bool recoverWipe() {
  DeviceWipe::NvsStore store;
  const auto state=DeviceWipe::inspect(store);
  if(state==DeviceWipe::State::Clean)return true;
  if(state==DeviceWipe::State::Pending && DeviceWipe::resume(store)==DeviceWipe::Result::Complete) {
    sleepReceipt.magic=0;Serial.println("WIPE_BOOT completed_pending_erase");return true;
  }
  // No old settings/history are loaded and no acquisition task is started.
  // An invalid marker is not permission to erase; the physical flow must run.
  wipeBlocked=wipeBootRecovery=wipeFailed=true;
  controls.page=Controls::Page::Wipe;controls.setting=unsigned(Controls::Setting::WipeAll);
  M5.Display.setRotation(DisplayOrientation::rotation(false));M5.Display.setBrightness(102);M5.Display.setTextSize(1);
  M5.BtnA.setHoldThresh(1000);M5.BtnB.setHoldThresh(1000);
  syncNavigation();Serial.println("WIPE_BOOT_ERROR normal_load_and_saves_blocked");return false;
}
LOOP_HELPER static void previewHistory() {
  bestEffect.clear();
  demo=!demo;diagnostics=false;historyPage=demo;historyView=PracticeUI::View::Recent;historyOffset=0;
  if(demo && !demoPractice.size()) {
    uint32_t count=0;
    for(unsigned session=0;session<18;++session) {
      demoPractice.newSession();
      for(unsigned i=0;i<12;++i) {
        const float rpm=4200+session*130+float(int(i%5)-2)*260+i*35;
        demoPractice.accept(++count,true,rpm,session*900000+i*20000);
      }
    }
  }
  Serial.printf("HISTORY_DEMO enabled=%d synthetic display only; saved history unchanged\n",demo);dirty=true;
}
LOOP_HELPER static void exportHistory() {
  Serial.printf("HISTORY_EXPORT records=%u sessions=%u metric=peak_rpm order=oldest_first\n",practice.size(),practice.sessions());
  for(unsigned i=practice.size();i>0;--i) {
    const auto &r=*practice.recent(i-1);
    Serial.printf("HISTORY_LAUNCH number=%lu session=%lu rpm=%.3f session_seconds=%lu\n",
      (unsigned long)r.number,(unsigned long)r.session,r.rpm,(unsigned long)r.seconds);
  }
  for(unsigned i=practice.sessions();i>0;--i) {
    const auto &s=*practice.session(i-1);
    Serial.printf("HISTORY_SESSION number=%lu pulls=%lu average_rpm=%.3f best_rpm=%.3f seconds=%lu\n",
      (unsigned long)s.number,(unsigned long)s.count,s.mean(),s.best,(unsigned long)s.seconds);
  }
}
// History and bests newer than their saved copy: the log's when it is open, else NVS's.
static bool historyUnsaved(){return historyLog.isOpen()?practice.generation()!=loggedHistory:practiceStore.pending(practice);}
static bool bestsUnsaved(){return historyLog.isOpen()?personalBests.generation()!=loggedBests:bestStore.pending(personalBests);}
static bool logHistory() {
#if LAUNCHLAB_HIL
  if(Hil::nvsWrite("history",PracticeHistory::IMAGE_BYTES))return false;
#endif
  if(!historyLog.saveHistory(practice,practiceStore.savedGeneration()))return false;
  loggedHistory=practice.generation();return true;
}
static bool logBests() {
#if LAUNCHLAB_HIL
  if(Hil::nvsWrite("best",32))return false;
#endif
  if(!historyLog.saveBests(personalBests,bestStore.savedGeneration()))return false;
  loggedBests=personalBests.generation();return true;
}
// At boot, before sampling starts: the first boot of 0.18 formats the
// partition (blank, erased or another firmware's bytes) in under a second.
static void openHistoryLog() {
  if(!spiffsFlash.begin()){Serial.println("HISTORY_LOG_BOOT state=no_partition");return;}
  const int64_t started=esp_timer_get_time();
  using Open=HistoryLog::Store<SpiffsFlash>::Log::Open;
  const auto opened=historyLog.open(spiffsFlash);
  Serial.printf("HISTORY_LOG_BOOT state=%s ready_sectors=%u us=%lld\n",opened==Open::Ready?"ready":opened==Open::Formatted?"formatted":
    opened==Open::Unsupported?"unsupported_layout":"failed",historyLog.isOpen()?historyLog.log().readyCount():0u,
    (long long)(esp_timer_get_time()-started));
}
// A loaded effect's own sound, for the speaker: rendered by the store's
// engine from its own copy of the file.
struct LoadedSoundSource:BestEffectSpeaker::LoadedSounds {
  bool sound(unsigned effect,uint32_t &key,size_t &samples) override{return loadedEffects && loadedEffects->soundOf(effect,key,samples);}
  bool begin(unsigned effect) override{return loadedEffects && loadedEffects->beginSound(effect);}
  size_t run(int16_t *out,size_t count) override{return loadedEffects?loadedEffects->renderSound(out,count):0;}
  bool song(unsigned effect) override{return loadedEffects && loadedEffects->songOf(effect);}
  bool readSong(unsigned effect,const uint8_t *&file,size_t &bytes) override{return loadedEffects && loadedEffects->readSong(effect,file,bytes);}
};
static LoadedSoundSource loadedSounds;
// Songs (engine level 2) stream from a task on core 0 at priority 1, below
// the optical ADC owner and the IMU, so it renders only in core 0's idle
// time; it sleeps until a song starts. Its file copy, renderer and ring
// are in PSRAM.
static Music *music=nullptr;
static constexpr uint32_t MUSIC_STACK=3584;
static void musicTask(void *) {
  // A block a step, then the sleep step() asks for (at least 1 ms, keeping
  // the task under Music::DUTY_PERCENT of core 0); with nothing to render it
  // waits for play() to wake it.
  for(;;) {
    const uint32_t ms=music->step([]{return uint32_t(esp_timer_get_time());});
    if(ms)vTaskDelay(pdMS_TO_TICKS(ms));
    else ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(250));
  }
}
LOOP_HELPER static void startMusic() {
  void *space=heap_caps_malloc(sizeof(Music::Space),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  void *memory=heap_caps_malloc(sizeof(Music),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  TaskHandle_t task=nullptr;
  if(space && memory) {
    music=new(memory) Music(new(space) Music::Space());
    // Internal RAM: 2,016 bytes at its deepest over the three example songs
    // (every voice type; HIL songs run, 2026-10-23), plus a 1.5 KB margin.
    if(xTaskCreatePinnedToCore(musicTask,"music",MUSIC_STACK,nullptr,1,&task,0)==pdPASS){soundPlayer.attachMusic(music,task);return;}
  }
  heap_caps_free(space);heap_caps_free(memory);music=nullptr;
  Serial.println("MUSIC_BOOT state=no_memory");
}
// After the ADC has its DMA memory: the runtime, the file buffers and the
// sound renderer from PSRAM; the playing file's hot 4 KB from the internal
// heap on the first added-effect play, so a unit without added effects keeps
// all its DMA-capable memory (0.19 first took 10.5 KB of it at boot).
// The store reads every installed effect once (a few ms each).
static void openLoadedEffects() {
  if(!spiffsFlash.partition){Serial.println("FX_STORE_BOOT state=no_partition");return;}
  using Loaded=LoadedEffects<SpiffsFlash>;
  void *memory=heap_caps_malloc(sizeof(Loaded),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  auto *file=static_cast<uint8_t *>(heap_caps_malloc(Loaded::Store::MAX_PAYLOAD,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!memory || !file){heap_caps_free(memory);heap_caps_free(file);Serial.println("FX_STORE_BOOT state=no_memory");return;}
  // The sound's file copy and renderer go to PSRAM too; without them loaded
  // effects sound as their stock fallbacks.
  auto *soundFile=static_cast<uint8_t *>(heap_caps_malloc(Loaded::Store::MAX_PAYLOAD,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  void *soundMemory=heap_caps_malloc(sizeof(Loaded::SoundSpace),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  Loaded::SoundSpace *sound=soundMemory?new(soundMemory) Loaded::SoundSpace():nullptr;
  if(!soundFile || !sound){heap_caps_free(soundFile);heap_caps_free(soundMemory);soundFile=nullptr;sound=nullptr;Serial.println("FX_STORE_BOOT sound=no_memory");}
  const Loaded::HotMemory hot=[]{return static_cast<uint8_t *>(heap_caps_malloc(LoadableFx::HOT_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT));};
  loadedEffects=new(memory) Loaded(bestEffectRenderer,file,Loaded::Store::MAX_PAYLOAD,hot,soundFile,sound);
  soundPlayer.attach(&loadedSounds);
  startMusic();
  const int64_t started=esp_timer_get_time();
  using Open=Loaded::Store::Open;
  const auto opened=loadedEffects->begin(spiffsFlash);
  Serial.printf("FX_STORE_BOOT state=%s installed=%u us=%lld\n",opened==Open::Ready?"ready":opened==Open::Unformatted?"unformatted":
    opened==Open::Unsupported?"unsupported_layout":"failed",loadedEffects->store().installed(),(long long)(esp_timer_get_time()-started));
}
// The 9 ms guard of a loaded effect's pass, timed from effectGuardStart.
static bool effectOverBudget(void *){return esp_timer_get_time()-effectGuardStart>9000;}
// A celebration's frame: a loaded effect through the engine, held to 9 ms a
// pass by the guard and given up for the clean recap after three stopped
// frames in a play (FX_OVERRUN); a loaded effect that cannot play here draws
// its stock fallback; a built-in through its renderer.
LOOP_HELPER static void drawEffect(Appearance::Effect e,uint32_t elapsed,const BestEffectUI::Renderer::Anchor &anchor,const Palette::Colors &colors) {
  const BestEffectUI::Surface surface{static_cast<uint16_t *>(screen.getBuffer()),screen.width(),screen.height()};
  if(Appearance::loaded(unsigned(e))) {
    LoadableFx::Guard guard;guard.expired=effectOverBudget;effectGuardStart=esp_timer_get_time();
    if(loadedEffects && loadedEffects->draw(surface,unsigned(e),elapsed,colors.accent,anchor,colors.background,guard)) {
      if(loadedEffects->overran())Serial.printf("FX_OVERRUN id=%u elapsed=%lu\n",unsigned(e),(unsigned long)elapsed);
      return;
    }
    e=Appearance::stockFallback(e);
  }
  bestEffectRenderer.draw(surface,e,elapsed,colors.accent,anchor,colors.background);
}
// At most one sector erase (14-30 ms, the ADC rides it) a second while
// nothing is being measured, so a save never has to erase.
LOOP_HELPER static void tickHistoryLog(uint32_t now,const Snapshot &s) {
  static uint32_t lastErase=0;
  if(!historyLog.isOpen() || now-lastErase<1000 || s.suspended || motionPending ||
     (s.phase!=AnalogTachometer::Phase::Ready && s.phase!=AnalogTachometer::Phase::Paused))return;
  lastErase=now;historyLog.readyAhead();
}
LOOP_HELPER static void saveHistory(uint32_t now,const Snapshot &s) {
  const bool logged=historyLog.isOpen();
  const bool history=logged?historyUnsaved():practiceStore.pending(practice) && practiceStore.ready();
  const bool best=logged?bestsUnsaved():bestStore.pending(personalBests) && bestStore.ready();
  // Failed writes must leave enough uninterrupted quiet for detector rearm.
  const uint32_t retryMs=saveError?HISTORY_SAVE_RETRY_MS:1000;
  // A sensor fault (signal held on the low rail) keeps the detector in Hold
  // until the signal returns, and nothing can be measured meanwhile, as when
  // Paused. Flush through an idle checkpoint rather than keep pulls in RAM.
  const bool fault=s.signalFault && s.phase==AnalogTachometer::Phase::Hold;
  if((!history && !best) || now-lastSaveTry<retryMs ||
     !HistoryCheckpoint::practiceIdle(now,lastAcceptedAt,
       fault?HISTORY_COALESCE_MS:HistoryCheckpoint::automaticQuietMs(now,imuStill,historyPendingSince?now-historyPendingSince:0,
         HISTORY_COALESCE_MS),motionPending,launchFeedback.shown(),
       fault?AnalogTachometer::Phase::Paused:s.phase))return;
  // Coalesce a rapid practice bout instead of resetting fusion after each pull.
  // Pending RAM history is flushed once the newest accepted pull has been idle.
  // No maximum-age override: a long practice bout stays in RAM until quiet.
  // Automatic sleep still forces a verified flush before entering standby.
  lastSaveTry=now;
  if(logged) {
    // Program-only writes: the ADC keeps sampling, so a pull that starts now
    // is measured however long the write takes.
    const int64_t started=esp_timer_get_time();
    const bool saved=(!history || logHistory()) && (!best || logBests());
    maxSaveUs=std::max(maxSaveUs,uint32_t(esp_timer_get_time()-started));
    if(saved){++saveCount;saveError=false;historyPendingSince=0;}else {++saveErrors;saveError=true;}
    lastSaveTry=millis();
    Serial.printf("HISTORY_SAVE ok=%d generation=%lu duration_us=%lu store=log\n",saved,
      (unsigned long)practice.generation(),(unsigned long)(esp_timer_get_time()-started));
    dirty=true;return;
  }
  if(!acquisition.request(fault?Action::IdleCheckpointBegin:Action::HistoryIdleCheckpointBegin,s.launches))return;
  const int64_t started=esp_timer_get_time();
  const bool saved=(!history || practiceStore.save(practice)) && (!best || bestStore.save(personalBests));
  maxSaveUs=std::max(maxSaveUs,uint32_t(esp_timer_get_time()-started));
  if(saved){++saveCount;saveError=false;historyPendingSince=0;}else {++saveErrors;saveError=true;}
  if(!acquisition.request(Action::CheckpointEnd))Serial.println("HISTORY_ERROR ADC checkpoint resume failed");
  lastSaveTry=millis();
  Serial.printf("HISTORY_SAVE ok=%d generation=%lu duration_us=%lu\n",saved,
    (unsigned long)practice.generation(),(unsigned long)(esp_timer_get_time()-started));
  dirty=true;
}

// Stops sampling, accepts queued pulls and saves history/bests before power
// down. Only Ready leaves acquisition stopped; otherwise sampling continues
// and the caller retries. A new pull is recorded but restarts the decision.
LOOP_HELPER static ShutdownFlush flushForShutdown(uint32_t now,const Snapshot &s) {
  if(!acquisition.request(Action::ShutdownBegin,s.launches))return ShutdownFlush::Busy;
  // Finish any queued accepted result before saving; the owner is now stopped.
  Snapshot event;
  bool newPull=false;
  while(acquisition.takeEvent(event))if(practice.accept(event.launches,event.valid,event.resultRpm,now)){prepareMotion(event);logLaunchMetrics(event);noteStartMark(event);newPull=true;}
  if(newPull) {
    inactivity.touch(now);dirty=true;
    if(!acquisition.request(Action::CheckpointEnd))acquisition.request(Action::Pin,s.pin);
    return ShutdownFlush::NewPull;
  }
  // Settings never hold up power-off: a failed write is logged, stays pending
  // and is retried if the device stays on.
  writeSettings();
  saveStartMarks();
  const bool historyFailed=practiceStore.pending(practice) && (!practiceStore.ready() || !practiceStore.save(practice));
  if(historyFailed || (bestStore.pending(personalBests) && (!bestStore.ready() || !bestStore.save(personalBests)))) {
    saveError=true;++saveErrors;dirty=true;
    Serial.printf("POWER_OFF_DEFERRED %s_save_failed\n",historyFailed?"history":"best");
    if(!acquisition.request(Action::CheckpointEnd))acquisition.request(Action::Pin,s.pin);
    lastSaveTry=millis();
    return ShutdownFlush::SaveFailed;
  }
  historyPendingSince=0;saveError=false;
  return ShutdownFlush::Ready;
}

#if LAUNCHLAB_HIL
// !sleepdry: the wake state the sleep set up, read back where it would have
// powered off (the caller holds the IMU/I2C gate): PM1's wake enables (0x18,
// GPIO4 = the IMU's interrupt), its hold (0x07, bit 5 holds the IMU supply)
// and the IMU's power control (bit 2 accelerometer, bit 1 gyroscope).
static void hilSleepDryReport(const SleepPlan &plan) {
  uint8_t wake=0xff,hold=0xff,imu=0xff;
  const bool ok=pm1Idle().read(0x18,&wake,1) && pm1Idle().read(0x07,&hold,1) && M5.In_I2C.readRegister(0x68,0x7D,&imu,1,400000);
  Serial.printf("SLEEP_DRY_RUN ok=%d shake_wake=%d arm_imu=%d wake=%s mode=%s pm1_wake=0x%02x pm1_hold=0x%02x imu_power=0x%02x imu_wake_armed=%d\n",
    ok,shakeToWake,plan.armImu,plan.wake,plan.mode,wake,hold,imu,ok && (wake&0x18)==0x10 && (hold&0x60)==0x20);
}
#endif
LOOP_HELPER static void autoPowerOff(uint32_t now,const Snapshot &s) {
  if(wipeBlocked || controls.page==Controls::Page::Wipe)return;
#if LAUNCHLAB_HIL
  if(!Hil::allowSleep.load())return;
  const bool dry=Hil::sleepDry.load();  // The rig's host is attached: never really sleep.
#else
  constexpr bool dry=false;
#endif
  if(usbAwake.inhibited(now) && !dry)return;
  if(motionPending || !inactivity.due(now,s.phase==AnalogTachometer::Phase::Launch) ||
     (lastShutdownTry && now-lastShutdownTry<60000))return;
  // Recheck immediately at the timer boundary; periodic polling alone could
  // leave a just-attached computer unnoticed for up to 250ms.
  pollUsbAwake(now,s,true);
  if((usbAwake.inhibited(now) && !dry) || !inactivity.due(now,s.phase==AnalogTachometer::Phase::Launch))return;
  lastShutdownTry=now;
  // A failed save retries after 60 s; a new pull restarts the full timeout.
  const auto flushed=flushForShutdown(now,s);
  if(flushed==ShutdownFlush::NewPull)lastShutdownTry=0;
  if(flushed!=ShutdownFlush::Ready)return;
  // A host may attach while the forced history flush is blocking.
  pollUsbAwake(millis(),acquisition.snapshot(),true);
  if(usbAwake.inhibited(millis()) && !dry) {
    if(!acquisition.request(Action::CheckpointEnd))acquisition.request(Action::Pin,s.pin);
    return;
  }
  Serial.printf("POWER_OFF reason=inactivity idle_ms=%lu records=%u saved=1\n",
    (unsigned long)inactivity.elapsed(now),practice.size());
  cancelSettingEdit(); // Wake into the settings list, never an unfinished edit.
  UsbTx::drain(200);
  if(imuAcquisition.lock()) {
    bool usbCancelled=false;
    // Shake to wake Off sleeps as the side key's power-off does: no IMU arm.
    const auto plan=sleepPlan(shakeToWake);
    if(!plan.armImu || shakeWake.arm()) {
      const WakeUi::State state{tournamentMode,historyPage && !demo && !diagnostics,
        uint8_t(tournamentView),uint8_t(historyView)};
      if(prefs.putUInt("wake_ui",WakeUi::encode(state))==sizeof(uint32_t) && saveWakeNavigation()) {
        // Keep the overwritten ring entry so a USB-cancelled attempt is not
        // counted or exported as an actual standby.
        const uint32_t previousPowerNext=powerLog.next,previousPowerCount=powerLog.count;
        const auto previousPowerRecord=powerLog.records[previousPowerNext];
        ++powerLog.sleeps;powerLog.pendingSleep=1;powerRecord(PowerDiagnostics::Sleep);
        if(!savePowerLog())Serial.println("POWER_LOG_NOTICE sleep_checkpoint_failed");
        sleepReceipt={PowerDiagnostics::MAGIC,wallUs()};
        M5.Display.setBrightness(0);M5.Display.sleep();
        Serial.printf("SHAKE_SLEEP timeout_s=%lu sensor_rail=L2_off_on_battery wake=%s mode=%s shake_wake=%d\n",(unsigned long)(inactivity.timeout()/1000),plan.wake,plan.mode,shakeToWake);UsbTx::drain(200);
        // Flash/USB flushing above can outlast the earlier host check. This
        // last O(1) SOF read is safe while holding the IMU mutex; do not call
        // pollUsbAwake here because it can attempt that mutex again.
        if(dry || HWCDC::isPlugged()) {
          usbCancelled=true;inactivity.touch(millis());lastShutdownTry=0;
          if(!dry){usbAwake.sampleHost(millis(),true);usbAutoOffInhibited=true;}
          --powerLog.sleeps;powerLog.next=previousPowerNext;powerLog.count=previousPowerCount;
          powerLog.records[previousPowerNext]=previousPowerRecord;powerLogDirty=true;
#if LAUNCHLAB_HIL
          if(dry)hilSleepDryReport(plan);
#endif
          Serial.println(dry?"POWER_OFF_DEFERRED sleep_dry_run":"POWER_OFF_DEFERRED usb_host_attached_before_sleep");
        } else shakeWake.sleep();
        sleepReceipt.magic=0;powerLog.pendingSleep=0;
        savePowerLog();
        M5.Display.wakeup();M5.Display.setBrightness(backlightValue());
        if(!prefs.remove("wake_ui"))Serial.println("WAKE_UI_NOTICE failed_sleep_marker_clear");
        prefs.remove("wake_nav");
      } else {
        prefs.remove("wake_ui");prefs.remove("wake_nav");
        Serial.println("POWER_OFF_DEFERRED wake_mode_save_failed");
      }
    }
    const bool restored=shakeWake.restore();
    if(!restored)noteMotionRestoreFailure();
    const bool extRestored=pm1Idle().run([](){M5.Power.setExtOutput(extPower);return true;});
    imuAcquisition.unlock();
    if(!extRestored)Serial.println("EXT_POWER_NOTICE restore_wake_failed");
    Serial.printf("POWER_OFF_DEFERRED reason=%s restored=%d retry_s=%u\n",dry?"sleep_dry_run":usbCancelled?"usb_host_attached":"shake_sleep_failed",restored,usbCancelled?0:60);
  } else Serial.println("POWER_OFF_DEFERRED imu_bus_busy");
  if(!acquisition.request(Action::CheckpointEnd))acquisition.request(Action::Pin,s.pin);
}

// The IRQ edge is latched by the ISR. The line level is rechecked at most
// once a second in case an edge was missed, so a stuck line cannot keep PM1
// out of its idle sleep with constant I2C reads.
static void IRAM_ATTR onPowerKeyIrq(){powerKeyIrq=true;}
LOOP_HELPER static void pollPowerKey(uint32_t now) {
  if(!powerKeyIrq && (digitalRead(POWER_KEY_IRQ_PIN) || now-lastPowerKeyRead<1000))return;
  if(!imuAcquisition.lock())return;
  powerKeyIrq=false;lastPowerKeyRead=now;
  const bool click=powerKey().takeClick();
  imuAcquisition.unlock();
  if(!click || powerKeyRequested)return;
  powerKeyRequested=true;powerKeyRequestedAt=now;inactivity.touch(now);
  Serial.println("POWER_KEY click=1 action=power_off");
}

// A single click switches off like PM1's own double click, after saving.
// A measured burst or motion capture finishes first. Manual power-off stays
// manual: shake wake is not armed and the side key turns the unit back on.
LOOP_HELPER static void powerKeyOff(uint32_t now,const Snapshot &s) {
  if(!powerKeyRequested)return;
  if(wipeBlocked){powerKeyRequested=false;Serial.println("POWER_KEY_IGNORED wipe_in_progress");return;}
  if(now-powerKeyRequestedAt>=POWER_KEY_WAIT_MS) {
    powerKeyRequested=false;Serial.println("POWER_KEY_OFF_DEFERRED acquisition_busy");return;
  }
  if(motionPending)return;
  const auto flushed=flushForShutdown(now,s);
  if(flushed==ShutdownFlush::Busy || flushed==ShutdownFlush::NewPull)return;
  powerKeyRequested=false;
  if(flushed==ShutdownFlush::SaveFailed)return; // Stay on; the save error is shown.
  if(controls.page==Controls::Page::Wipe)cancelWipe();
  cancelSettingEdit();
  Serial.printf("POWER_OFF reason=power_key records=%u saved=1\n",practice.size());
  ++powerLog.sleeps;powerLog.pendingSleep=1;powerRecord(PowerDiagnostics::Sleep);
  if(!savePowerLog())Serial.println("POWER_LOG_NOTICE sleep_checkpoint_failed");
  sleepReceipt={PowerDiagnostics::MAGIC,wallUs()};
  M5.Display.setBrightness(0);M5.Display.sleep();UsbTx::drain(200);
  // PM1 removes battery power. USB can keep the ESP32 powered, so it deep
  // sleeps until the next click asserts the IRQ line; the IMU is not armed.
  if(imuAcquisition.lock()){shakeWake.sleep();imuAcquisition.unlock();}
  sleepReceipt.magic=0;powerLog.pendingSleep=0;savePowerLog();
  M5.Display.wakeup();M5.Display.setBrightness(backlightValue());dirty=true;
  Serial.println("POWER_KEY_OFF_FAILED pm1_power_off_refused");
  if(!acquisition.request(Action::CheckpointEnd))acquisition.request(Action::Pin,s.pin);
}

// USB backup: everything a player would miss, read from RAM so pulls not yet
// flushed are included. Lines are BACKUP_DATA base64; the host checks the
// checksum inside the container.
// The installed loaded effects' files and sources, read into one PSRAM block
// the backup points at (format 7); the block is the caller's to free.
static uint8_t *readLoadedForBackup(DeviceBackup::Contents &c) {
  c.loadedCount=0;
  if(!loadedEffects || !loadedEffects->store().installed())return nullptr;
  const auto &store=loadedEffects->store();size_t total=0;
  for(unsigned slot=0;slot<Appearance::LOADED;++slot)if(loadedEffects->info(slot).playable)total+=store.entry(slot).fileBytes+store.entry(slot).sourceBytes;
  auto *block=static_cast<uint8_t *>(heap_caps_malloc(std::max<size_t>(total,1),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!block)return nullptr;
  size_t at=0;
  for(unsigned slot=0;slot<Appearance::LOADED;++slot) {
    if(!loadedEffects->info(slot).playable)continue;  // a damaged file stays behind
    const auto &e=store.entry(slot);size_t file=0,source=0;
    if(!store.readFile(slot,block+at,e.fileBytes,file) || !store.readSource(slot,block+at+file,e.sourceBytes,source))continue;
    c.loaded[c.loadedCount++]={uint8_t(slot),block+at,source?block+at+file:nullptr,uint16_t(file),uint16_t(source)};at+=file+source;
  }
  return block;
}
LOOP_HELPER static void exportBackup() {
  auto contents=std::unique_ptr<DeviceBackup::Contents>(new(std::nothrow) DeviceBackup::Contents);
  if(!contents){Serial.println("BACKUP_ERROR out_of_memory");return;}
  contents->practice=practice;contents->look=appearance;contents->bests=personalBests;
  contents->hasMotion=referenceMotion.valid();if(contents->hasMotion)contents->motion=referenceMotion;
  contents->settings={inactivity.minutes(),brightnessPercent,rpmEstimator,sensorProfile,displayFlipped,mountMode,shakeToWake};
  contents->hasThemes=!themeLibrary.empty();contents->themes=themeLibrary;
  contents->hasMilestones=!milestones.isDefault();contents->milestones=milestones;
  contents->hasEffectSet=!effectSet.isDefault();contents->effectSet=effectSet;
  uint8_t *files=readLoadedForBackup(*contents);
  const size_t capacity=DeviceBackup::encodedBound(*contents);
  auto *buffer=static_cast<uint8_t *>(heap_caps_malloc(capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  const size_t n=buffer?DeviceBackup::encode(*contents,VERSION,buffer,capacity):0;
  if(!n){heap_caps_free(buffer);heap_caps_free(files);Serial.println("BACKUP_ERROR out_of_memory");return;}
  Serial.printf("BACKUP_BEGIN format=%lu bytes=%u records=%u sessions=%u motion=%d themes=%d milestones=%d effect_set=%d effects=%u runtime=%s\n",
    (unsigned long)contents->format(),unsigned(n),practice.size(),practice.sessions(),contents->hasMotion,contents->hasThemes,contents->hasMilestones,
    contents->hasEffectSet,contents->loadedCount,VERSION);
  DeviceBackup::base64Lines(buffer,n,96,[](const char *line){Serial.print("BACKUP_DATA ");Serial.println(line);});
  Serial.println("BACKUP_END");
  heap_caps_free(buffer);heap_caps_free(files);
}
LOOP_HELPER static void startRestore() {
  if(!restoreText)restoreText=static_cast<char *>(heap_caps_malloc(DeviceBackup::LOADED_TEXT_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!restoreText){Serial.println("RESTORE_ERROR reason=out_of_memory written=0");return;}
  restoreRx.start(restoreText,DeviceBackup::LOADED_TEXT_BYTES,millis());
  Serial.printf("RESTORE_READY max_chars=%u\n",unsigned(DeviceBackup::LOADED_TEXT_BYTES));
}
// A backup's loaded effects replace the device's (inside the restore's
// checkpoint): the region formatted if it never was, then each file into the
// slot the backup had it in. A backup without them leaves the device's.
static bool restoreLoaded(const DeviceBackup::Contents &c) {
  if(!c.loadedCount)return true;
  if(!loadedEffects)return false;
  auto &store=loadedEffects->store();
  if(!store.isOpen()){bool failed=false;while(!store.prepareStep(failed) && !failed){}if(failed)return false;}
  using Result=LoadedEffects<SpiffsFlash>::Store::Result;
  if(loadedEffects->clear()!=Result::Ok)return false;
  for(unsigned i=0;i<c.loadedCount;++i) {
    const auto &e=c.loaded[i];
    if(loadedEffects->restore(e.slot,e.file,e.fileBytes,e.source,e.sourceBytes)!=Result::Ok)return false;
  }
  return true;
}
// Validate the whole backup first, write every record inside one ADC-owned
// idle checkpoint with readback, then restart so each store loads normally.
// Restored records supersede whatever generation the device holds.
LOOP_HELPER static void finishRestore(const Snapshot &s) {
  auto contents=std::unique_ptr<DeviceBackup::Contents>(new(std::nothrow) DeviceBackup::Contents);
  if(!contents){Serial.println("RESTORE_ERROR reason=out_of_memory");return;}
  // Decoded in place: base64's bytes never overtake its text.
  size_t n=0;uint8_t *bytes=reinterpret_cast<uint8_t *>(restoreText);
  if(!restoreRx.take(bytes,DeviceBackup::LOADED_TEXT_BYTES,n) || !DeviceBackup::decode(bytes,n,*contents)) {
    Serial.println("RESTORE_ERROR reason=invalid_backup written=0");return;
  }
  Serial.printf("RESTORE_VALID records=%u sessions=%u motion=%d themes=%d milestones=%d effect_set=%d effects=%u from=%s\n",contents->practice.size(),
    contents->practice.sessions(),contents->hasMotion,contents->hasThemes,contents->hasMilestones,contents->hasEffectSet,contents->loadedCount,contents->runtime);
  if(motionPending || s.phase==AnalogTachometer::Phase::Launch || !acquisition.request(Action::IdleCheckpointBegin,s.launches)) {
    Serial.println("RESTORE_ERROR reason=busy written=0");return;
  }
  contents->practice.supersede(practice.generation());
  contents->look.revision=appearance.revision+1;contents->bests.revision=personalBests.revision+1;
  contents->themes.revision=themeLibrary.revision+1;contents->milestones.revision=milestones.revision+1;
  contents->effectSet.revision=effectSet.revision+1;
  const auto &settings=contents->settings;
  // The effects first: records may name their slots.
  const bool saved=restoreLoaded(*contents) && practiceStore.save(contents->practice) && appearanceStore.saveViewed(contents->look) &&
    bestStore.save(contents->bests) && (!contents->hasMotion || motionStore.save(contents->motion)) &&
    prefs.putUChar("sleep_min",settings.sleepMinutes)==1 && prefs.putUChar(Brightness::KEY,settings.brightness)==1 &&
    RpmEstimatorSetting::save(prefs,settings.rpmMethod) && SensorProfileSetting::save(prefs,settings.sensorProfile) &&
    DisplayFlipSetting::save(prefs,settings.flipped) && MountOrientationSetting::save(prefs,settings.mount) &&
    ShakeWakeSetting::save(prefs,settings.shakeWake) &&
    (!contents->hasThemes || themeStore.save(contents->themes,true)) &&
    (!contents->hasMilestones || milestoneStore.save(contents->milestones)) &&
    (!contents->hasEffectSet || effectSetStore.saveViewed(contents->effectSet));
  // The log gets the restored records too, mirrored to what NVS now holds,
  // so the next boot loads them whichever copy it picks.
  if(saved && historyLog.isOpen() && !(historyLog.saveHistory(contents->practice,contents->practice.generation()) &&
     historyLog.saveBests(contents->bests,contents->bests.generation())))Serial.println("RESTORE_NOTICE history_log_write_failed");
  if(!saved) {
    if(!acquisition.request(Action::CheckpointEnd))Serial.println("RESTORE_ERROR ADC checkpoint resume failed");
    // Earlier records may already hold the restored values; restoring again is safe.
    Serial.println("RESTORE_ERROR reason=write_failed written=partial");dirty=true;return;
  }
  heapStatus();
  Serial.println("RESTORE_OK restarting=1");UsbTx::drain(200);
  delay(200);ESP.restart();
}
LOOP_HELPER static void pollRestore(const Snapshot &s) {
  const uint32_t now=millis();
  if(restoreRx.timedOut(now)){restoreRx.cancel();Serial.println("RESTORE_ERROR reason=timeout written=0");return;}
  // A transfer owns the USB input until it ends; bounded per loop like commands.
  for(unsigned n=0;n<256 && restoreRx.active() && Serial.available();++n) {
    inactivity.touch(now);
    switch(restoreRx.feed(Serial.read(),now)) {
      case DeviceBackup::TransferEvent::Chunk:Serial.printf("RESTORE_CHUNK chars=%u\n",unsigned(restoreRx.received()));break;
      case DeviceBackup::TransferEvent::Complete:finishRestore(s);break;
      case DeviceBackup::TransferEvent::Error:usbCommands.refused(now);Serial.printf("RESTORE_ERROR reason=%s written=0\n",restoreRx.error());break;
      case DeviceBackup::TransferEvent::None:break;
    }
  }
}

// ---- USB custom themes ------------------------------------------------------
// `L` lists the slots; `T` takes one framed, checksummed request (put, delete,
// select or clear) as a base64 line and a "." line, like a restore. Replies
// are drained at once: a short reply can otherwise wait in the USB ring.
static void themeReply(){UsbTx::drain(100);}
LOOP_HELPER static void listThemes() {
  unsigned available=0;const bool known=nvsAvailable(available);
  // colors=any: any colors are accepted (0.14 and 0.15 refused light or low-contrast themes).
  Serial.printf("THEME_LIST slots=%u selected=%u showing=%u store=%s revision=%lu nvs_available=%d nvs_needed=%u colors=any\n",
    Themes::SLOTS,unsigned(appearance.theme),unsigned(themeLibrary.effective(appearance.theme)),ThemeStore::name(themeStore.status()),
    (unsigned long)themeLibrary.revision,known?int(available):-1,themeStore.neededEntries());
  for(unsigned i=0;i<Themes::SLOTS;++i) {
    char data[(Themes::SLOT_BYTES+2)/3*4+1]={};
    DeviceBackup::base64Lines(themeLibrary.raw(i),Themes::SLOT_BYTES,96,[&](const char *line){strncpy(data,line,sizeof(data)-1);});
    Palette::Custom theme;const bool valid=themeLibrary.get(i,theme);
    Serial.printf("THEME_SLOT slot=%u theme=%u state=%s data=%s name=%s\n",i,unsigned(Themes::slotTheme(i)),
      Themes::stateName(themeLibrary.state(i)),data,valid?theme.name:"");
  }
  Serial.println("THEME_LIST_END");themeReply();
}
// A selection the library no longer has returns to the default; an open
// Theme editor closes, since its draft may be gone.
static void themesChanged() {
  controls.customThemes=themeLibrary.validMask();
  if(controls.page==Controls::Page::Edit && controls.setting==unsigned(Controls::Setting::Theme))cancelSettingEdit();
  if(!themeLibrary.available(appearance.theme)) {
    auto next=appearance;
    if(appearanceStore.ready() && next.select(Themes::FALLBACK,appearance.effect,appearance.sound)){appearance=next;settingsSave.mark(SettingsSave::Appearance);}
  }
  syncNavigation();
}
LOOP_HELPER static void selectTheme(Appearance::Theme theme,const Snapshot &s) {
  auto next=appearance;
  if(!themeLibrary.available(theme)){Serial.printf("THEME_ERROR op=select reason=unavailable theme=%u written=0\n",unsigned(theme));themeReply();return;}
  if(!appearanceStore.ready() || !next.select(theme,appearance.effect,appearance.sound)) {
    Serial.println("THEME_ERROR op=select reason=appearance_unavailable written=0");themeReply();return;
  }
  if(next.generation()!=appearance.generation()){appearance=next;settingsSave.mark(SettingsSave::Appearance);}
  if(controls.page==Controls::Page::Edit && controls.setting==unsigned(Controls::Setting::Theme))cancelSettingEdit();
  syncNavigation();
  // Saved with the other settings: at once when the owner is idle, otherwise
  // at its next checkpoint.
  saveSettings(millis(),s,true);
  Serial.printf("THEME_OK op=select theme=%u pending=%d\n",unsigned(theme),bool(settingsSave.pending()&SettingsSave::Appearance));themeReply();
}
LOOP_HELPER static void finishThemeRequest(const Snapshot &s) {
  uint8_t bytes[Themes::Request::BYTES];size_t n=0;
  auto request=std::unique_ptr<Themes::Request>(new(std::nothrow) Themes::Request);
  auto next=std::unique_ptr<Themes::Library>(new(std::nothrow) Themes::Library);
  if(!request || !next){Serial.println("THEME_ERROR reason=out_of_memory written=0");themeReply();return;}
  if(!themeRx.take(bytes,sizeof(bytes),n) || !request->decode(bytes,n)) {
    Serial.println("THEME_ERROR reason=invalid_request written=0");themeReply();return;
  }
  using Op=Themes::Request::Op;
  const char *op=Themes::Request::name(request->op);const unsigned slot=request->argument;
  if(request->op==Op::Select){selectTheme(Appearance::Theme(request->argument),s);return;}
  auto fail=[&](const char *reason){Serial.printf("THEME_ERROR op=%s slot=%u reason=%s written=0\n",op,slot,reason);themeReply();};
  if(request->op!=Op::Clear && slot>=Themes::SLOTS)return fail("bad_slot");
  if(request->op==Op::Put && !request->theme.valid())return fail(Palette::problemName(request->theme.check()));
  // Colors are never refused; a put reports what the editor warns about.
  char warnings[Palette::ADVICE_TEXT]="none";
  if(request->op==Op::Put)request->theme.advice().format(warnings,sizeof(warnings));
  if(themeStore.status()==ThemeStore::Status::Unavailable)return fail("storage_unavailable");
  // An unreadable record is kept until it is cleared on purpose.
  if(request->op!=Op::Clear && themeStore.status()==ThemeStore::Status::Unreadable)return fail("store_unreadable");
  *next=themeLibrary;
  if(request->op==Op::Put)next->put(slot,request->theme);
  else if(request->op==Op::Delete)next->remove(slot);
  else *next=Themes::Library{};
  if(request->op!=Op::Clear && *next==themeLibrary) {
    Serial.printf("THEME_OK op=%s slot=%u revision=%lu unchanged=1 warnings=%s\n",op,slot,(unsigned long)themeLibrary.revision,warnings);themeReply();return;
  }
  unsigned available=0;
  if(request->op==Op::Put && (!nvsAvailable(available) || available<themeStore.neededEntries())) {
    Serial.printf("THEME_ERROR op=put slot=%u reason=nvs_space available_entries=%u needed_entries=%u written=0\n",slot,available,themeStore.neededEntries());
    themeReply();return;
  }
  if(motionPending || s.phase==AnalogTachometer::Phase::Launch || !acquisition.request(Action::IdleCheckpointBegin,s.launches))return fail("busy");
  const bool ok=request->op==Op::Clear?themeStore.clear():themeStore.save(*next);
  if(!acquisition.request(Action::CheckpointEnd))Serial.println("THEME_NOTICE adc_checkpoint_resume_failed");
  if(!ok){Serial.printf("THEME_ERROR op=%s slot=%u reason=write_failed written=unknown\n",op,slot);themeReply();dirty=true;return;}
  themeLibrary=*next;themesChanged();
  Serial.printf("THEME_OK op=%s slot=%u revision=%lu state=%s warnings=%s\n",op,slot,(unsigned long)themeLibrary.revision,
    request->op==Op::Clear?"empty":Themes::stateName(themeLibrary.state(slot)),warnings);themeReply();
  // A selection that fell back is saved like any other setting.
  saveSettings(millis(),acquisition.snapshot(),true);dirty=true;
}
LOOP_HELPER static void pollThemeRequest(const Snapshot &s) {
  const uint32_t now=millis();
  if(themeRx.timedOut(now)){themeRx.cancel();Serial.println("THEME_ERROR reason=timeout written=0");themeReply();return;}
  for(unsigned n=0;n<256 && themeRx.active() && Serial.available();++n) {
    inactivity.touch(now);
    switch(themeRx.feed(Serial.read(),now)) {
      case DeviceBackup::Receiver::Event::Chunk:Serial.printf("THEME_CHUNK chars=%u\n",themeRx.received());themeReply();break;
      case DeviceBackup::Receiver::Event::Complete:finishThemeRequest(s);break;
      case DeviceBackup::Receiver::Event::Error:usbCommands.refused(now);Serial.printf("THEME_ERROR reason=%s written=0\n",themeRx.error());themeReply();break;
      case DeviceBackup::Receiver::Event::None:break;
    }
  }
}

// ---- USB milestones -----------------------------------------------------------
// `M` lists the Celebrate mode and the table; `U` takes one framed,
// checksummed request (set or reset) like a theme request. A request is
// checked whole and saved inside an idle checkpoint, or nothing changes.
static void milestoneReply(){UsbTx::drain(100);}
static const char *milestoneStoreName(){return !milestoneStore.ready()?"unavailable":milestoneStore.damaged()?"damaged":"ok";}
// An upload must leave room for a history rewrite in the 20 KB NVS.
LOOP_HELPER static void listMilestones() {
  unsigned available=0;const bool known=nvsAvailable(available);
  Milestones::Config::Image image;milestones.encode(image);
  char data[(Milestones::Config::IMAGE_BYTES+2)/3*4+1]={};
  DeviceBackup::base64Lines(image.data(),image.size(),96,[&](const char *line){strncpy(data,line,sizeof(data)-1);});
  Serial.printf("MILESTONE_LIST version=3 mode=%s rows=%u max_rows=%u min_rpm=%u max_rpm=%u effects=%u revision=%lu store=%s default=%d nvs_available=%d nvs_needed=%u data=%s%s\n",
    Milestones::name(milestones.mode()),milestones.count(),Milestones::MAX_ROWS,unsigned(Milestones::MIN_RPM),unsigned(Milestones::MAX_RPM),Appearance::EFFECTS,
    (unsigned long)milestones.revision,milestoneStoreName(),milestones.isDefault(),known?int(available):-1,milestoneStore.neededEntries(milestones),data,effectSetFields());
  // effect/effect_id name the pool's first effect, as 0.15 listed its one.
  const auto *picks=milestonePicks();
  for(unsigned i=0;i<milestones.count();++i) {
    // A pool of every drawn effect names about 600 characters.
    const auto &r=milestones.row(i);static char names[16*Appearance::EFFECTS];size_t used=0;names[0]=0;
    for(unsigned e=0;e<Appearance::EFFECTS && used<sizeof(names);++e)if(r.has(Appearance::Effect(e)))
      used+=snprintf(names+used,sizeof(names)-used,"%s%s",used?"+":"",Appearance::label(Appearance::Effect(e)));
    char pool[33];r.pool.hex(pool);
    Serial.printf("MILESTONE_ROW index=%u rpm=%u effect=%s effect_id=%u pool=%s effects=%s next=%s\n",i,unsigned(r.rpm),
      Appearance::label(r.first()),unsigned(r.first()),pool,names,Appearance::label(picks[i]));
  }
  Serial.println("MILESTONE_LIST_END");milestoneReply();
}
LOOP_HELPER static void finishMilestoneRequest(const Snapshot &s) {
  uint8_t bytes[Milestones::Request::BYTES];size_t n=0;
  Milestones::Request request;Milestones::Problem problem=Milestones::Problem::None;unsigned row=0;
  if(!milestoneRx.take(bytes,sizeof(bytes),n) || !request.decode(bytes,n,problem,row)) {
    Serial.println("MILESTONE_ERROR reason=invalid_request written=0");milestoneReply();return;
  }
  const char *op=Milestones::Request::name(request.op);
  if(problem!=Milestones::Problem::None) {
    Serial.printf("MILESTONE_ERROR op=%s reason=invalid_table problem=%s row=%u written=0\n",op,Milestones::problemName(problem),row);
    milestoneReply();return;
  }
  auto fail=[&](const char *reason){Serial.printf("MILESTONE_ERROR op=%s reason=%s written=0\n",op,reason);milestoneReply();};
  if(!milestoneStore.ready())return fail("storage_unavailable");
  auto next=milestones;
  if(request.op==Milestones::Request::Op::Reset)next.reset();
  else next.set(request.mode,request.rows,request.count);
  // A damaged record is replaced even by the table it already shows.
  if(next.generation()==milestones.generation() && !milestoneStore.damaged()) {
    Serial.printf("MILESTONE_OK op=%s revision=%lu unchanged=1\n",op,(unsigned long)milestones.revision);milestoneReply();return;
  }
  if(next.generation()==milestones.generation())++next.revision;
  unsigned available=0;
  const unsigned needed=milestoneStore.neededEntries(next);
  if(!nvsAvailable(available) || available<needed) {
    Serial.printf("MILESTONE_ERROR op=%s reason=nvs_space available_entries=%u needed_entries=%u written=0\n",op,available,needed);
    milestoneReply();return;
  }
  if(motionPending || s.phase==AnalogTachometer::Phase::Launch || !acquisition.request(Action::IdleCheckpointBegin,s.launches))return fail("busy");
  const bool ok=milestoneStore.save(next);
  if(!acquisition.request(Action::CheckpointEnd))Serial.println("MILESTONE_NOTICE adc_checkpoint_resume_failed");
  if(!ok){Serial.printf("MILESTONE_ERROR op=%s reason=write_failed written=unknown\n",op);milestoneReply();dirty=true;return;}
  milestones=next;
  // A Celebrate editor open on the device shows a draft of the old mode.
  if(controls.page==Controls::Page::Edit && controls.setting==unsigned(Controls::Setting::Celebrate))cancelSettingEdit();
  syncNavigation();
  Serial.printf("MILESTONE_OK op=%s revision=%lu mode=%s rows=%u\n",op,(unsigned long)milestones.revision,
    Milestones::name(milestones.mode()),milestones.count());milestoneReply();
}
// ---- USB loaded effects -----------------------------------------------------
// `F` lists the store and every slot; `X` takes one framed, checksummed
// request (fx_transfer.h: put, delete, clear, or source to read a slot's
// packed source back), checked whole and carried out while nothing is
// measured, or nothing changes. The first install formats
// the effect region (docs/FIRMWARE_FLASH_LAYOUT.md), a sector at a time.
static void effectReply(){UsbTx::drain(100);}
static constexpr size_t EFFECT_REQUEST_BYTES=FxTransfer::Request::HEADER+LoadedEffects<SpiffsFlash>::Store::MAX_PAYLOAD+FxTransfer::Request::TRAILER;
static constexpr unsigned EFFECT_REQUEST_CHARS=unsigned((EFFECT_REQUEST_BYTES+2)/3*4);
static const char *effectStoreName() {
  if(!loadedEffects)return "unavailable";
  return loadedEffects->store().isOpen()?"ready":"unformatted";
}
// Which records name loaded effect `id`, as FX_SLOT and a delete's reply list them.
static const char *effectReferences(unsigned id) {
  static char text[32];text[0]=0;
  bool rows=false;for(unsigned i=0;i<milestones.count();++i)rows=rows || milestones.row(i).has(Appearance::Effect(id));
  snprintf(text,sizeof(text),"%s%s%s%s%s",appearance.effect==Appearance::Effect(id)?"best":"",
    appearance.effect==Appearance::Effect(id) && (effectSet.mask().has(id) || rows)?",":"",effectSet.mask().has(id)?"set":"",
    effectSet.mask().has(id) && rows?",":"",rows?"milestones":"");
  return text[0]?text:"none";
}
static bool effectReferenced(unsigned slot){return strcmp(effectReferences(Appearance::FIRST_LOADED+slot),"none")!=0;}
LOOP_HELPER static void listEffects() {
  const auto *store=loadedEffects?&loadedEffects->store():nullptr;
  Serial.printf("FX_LIST version=1 engine=%u code=1 store=%s limit=%u installed=%u free_slots=%u first_id=%u\n",unsigned(LoadableFx::ENGINE),
    effectStoreName(),unsigned(Appearance::LOADED),store?store->installed():0u,store?store->freeSlots():0u,Appearance::FIRST_LOADED);
  for(unsigned slot=0;loadedEffects && slot<Appearance::LOADED;++slot) {
    const auto &i=loadedEffects->info(slot);const auto &e=store->entry(slot);const uint8_t fallback=store->fallback(slot);
    if(!i.installed && fallback==LoadableFx::Program::NO_FALLBACK)continue;
    const unsigned id=Appearance::FIRST_LOADED+slot;
    const char *state=!i.installed?"deleted":i.playable?"ok":i.engine>LoadableFx::ENGINE?"needs_engine":"damaged";
    // The name last: it may hold spaces.
    Serial.printf("FX_SLOT slot=%u id=%u state=%s uid=%016llx revision=%u bytes=%u source_bytes=%u duration_ms=%lu fallback=%s references=%s name=%s\n",
      slot,id,state,(unsigned long long)(i.installed?e.uid:0),unsigned(i.revision),unsigned(e.fileBytes),unsigned(e.sourceBytes),(unsigned long)i.duration,
      fallback!=LoadableFx::Program::NO_FALLBACK?Appearance::label(Appearance::Effect(fallback)):"Orbit",effectReferences(id),i.installed?i.name:"");
  }
  Serial.println("FX_LIST_END");effectReply();
}
LOOP_HELPER static void startEffectRequest() {
  if(!effectRxText)effectRxText=static_cast<char *>(heap_caps_malloc(EFFECT_REQUEST_CHARS,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!effectRxBytes)effectRxBytes=static_cast<uint8_t *>(heap_caps_malloc(EFFECT_REQUEST_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!loadedEffects || !effectRxText || !effectRxBytes){Serial.println("FX_ERROR reason=storage_unavailable written=0");effectReply();return;}
  effectRx.start(effectRxText,EFFECT_REQUEST_CHARS,millis());
  Serial.printf("FX_READY max_chars=%u\n",EFFECT_REQUEST_CHARS);effectReply();
}
// After an install or a delete: Random, the editors and the milestone picks see the change.
static void effectsChanged() {
  if(!Appearance::celebrates(unsigned(upcomingEffect)))upcomingEffect=Appearance::Effect::Off;
  milestonePicksReady=false;
  if(controls.page==Controls::Page::Edit && controls.setting==unsigned(Controls::Setting::BestEffect))cancelSettingEdit();
  syncNavigation();dirty=true;
}
LOOP_HELPER static void finishEffectRequest(const Snapshot &s) {
  size_t n=0;FxTransfer::Request request;
  if(!effectRx.take(effectRxBytes,EFFECT_REQUEST_BYTES,n) || !request.decode(effectRxBytes,n)){Serial.println("FX_ERROR reason=invalid_request written=0");effectReply();return;}
  const char *op=FxTransfer::Request::name(request.op);
  auto fail=[&](const char *reason){Serial.printf("FX_ERROR op=%s reason=%s written=0\n",op,reason);effectReply();};
  if(motionPending || s.phase==AnalogTachometer::Phase::Launch)return fail("busy");
  using Result=LoadedEffects<SpiffsFlash>::Store::Result;
  auto &store=loadedEffects->store();
  if(request.op==FxTransfer::Request::Op::Put) {
    LoadableFx::Program p;const auto loaded=LoadableFx::load(request.file,request.fileBytes,p,false);
    if(loaded==LoadableFx::Error::Engine)return fail("engine");
    if(loaded!=LoadableFx::Error::None){Serial.printf("FX_ERROR op=put reason=invalid_file problem=%s written=0\n",LoadableFx::label(loaded));effectReply();return;}
    if(LoadableFx::hotBytes(p)>LoadableFx::HOT_BYTES)return fail("too_large_for_ram");
    if(!store.isOpen()) {
      // Once per unit: erase the region a sector at a time (14-30 ms each, the ADC rides them).
      const int64_t started=esp_timer_get_time();bool failed=false;
      while(!store.prepareStep(failed) && !failed)vTaskDelay(1);
      if(failed)return fail("storage_unavailable");
      Serial.printf("FX_NOTICE store_formatted us=%lld\n",(long long)(esp_timer_get_time()-started));
    }
    unsigned slot=0;
    const Result r=loadedEffects->install(request.file,request.fileBytes,request.source,request.sourceBytes,effectReferenced,slot);
    if(r!=Result::Ok) {
      if(r==Result::WriteFailed){Serial.println("FX_ERROR op=put reason=write_failed written=unknown");effectReply();effectsChanged();return;}
      return fail(r==Result::Full?"slots_full":r==Result::TooLarge?"too_large":r==Result::Engine?"engine":r==Result::Invalid?"invalid_file":"storage_unavailable");
    }
    effectsChanged();
    Serial.printf("FX_OK op=put slot=%u id=%u uid=%016llx revision=%u installed=%u free_slots=%u name=%s\n",slot,Appearance::FIRST_LOADED+slot,
      (unsigned long long)p.uid,unsigned(p.revision),store.installed(),store.freeSlots(),p.name);
    effectReply();return;
  }
  if(!store.isOpen())return fail("not_found");
  if(request.op==FxTransfer::Request::Op::Source) {
    // The request is decoded and holds no file: its buffer takes the source.
    const unsigned slot=request.slot;const auto &e=store.entry(slot);size_t n=0;
    if(!e.used)return fail("not_found");
    if(!store.readSource(slot,effectRxBytes,EFFECT_REQUEST_BYTES,n))return fail("read_failed");
    Serial.printf("FX_SOURCE slot=%u id=%u uid=%016llx bytes=%u\n",slot,Appearance::FIRST_LOADED+slot,(unsigned long long)e.uid,unsigned(n));
    DeviceBackup::base64Lines(effectRxBytes,n,96,[](const char *line){Serial.println(line);effectReply();});
    Serial.printf("FX_OK op=source slot=%u bytes=%u\n",slot,unsigned(n));effectReply();return;
  }
  if(request.op==FxTransfer::Request::Op::Delete) {
    const unsigned id=Appearance::FIRST_LOADED+request.slot;const char *references=effectReferences(id);
    static char kept[32];strncpy(kept,references,sizeof(kept)-1);kept[sizeof(kept)-1]=0;
    const Result r=loadedEffects->remove(request.slot);
    if(r==Result::NotFound)return fail("not_found");
    if(r!=Result::Ok){Serial.println("FX_ERROR op=delete reason=write_failed written=unknown");effectReply();effectsChanged();return;}
    effectsChanged();
    Serial.printf("FX_OK op=delete slot=%u id=%u references=%s installed=%u\n",unsigned(request.slot),id,kept,store.installed());effectReply();return;
  }
  const Result r=loadedEffects->clear();effectsChanged();
  if(r!=Result::Ok){Serial.println("FX_ERROR op=clear reason=write_failed written=unknown");effectReply();return;}
  Serial.printf("FX_OK op=clear installed=%u\n",store.installed());effectReply();
}
LOOP_HELPER static void pollEffectRequest(const Snapshot &s) {
  const uint32_t now=millis();
  if(effectRx.timedOut(now)){effectRx.cancel();Serial.println("FX_ERROR reason=timeout written=0");effectReply();return;}
  for(unsigned n=0;n<256 && effectRx.active() && Serial.available();++n) {
    inactivity.touch(now);
    switch(effectRx.feed(Serial.read(),now)) {
      case FxTransfer::Event::Chunk:Serial.printf("FX_CHUNK chars=%u\n",effectRx.received());effectReply();break;
      case FxTransfer::Event::Complete:finishEffectRequest(s);break;
      case FxTransfer::Event::Error:usbCommands.refused(now);Serial.printf("FX_ERROR reason=%s written=0\n",effectRx.error());effectReply();break;
      case FxTransfer::Event::None:break;
    }
  }
}

// ---- USB effect set -----------------------------------------------------------
// The milestone protocol carries it: `M` adds effect_set=, effect_set_store=
// and effect_set_default= to MILESTONE_LIST, and `U` op 3 (set_effects,
// effect_set.h) replaces the set, checked whole and saved inside an idle
// checkpoint, or nothing changes. Replies are MILESTONE_OK / MILESTONE_ERROR.
static const char *effectSetStoreName(){return !effectSetStore.ready()?"unavailable":effectSetStore.damaged()?"damaged":"ok";}
// An upload must leave room for a history rewrite in the 20 KB NVS, and keep
// the reserve when it fills an empty slot.
static constexpr unsigned EFFECT_SET_NEEDED_ENTRIES=NvsBudget::blobEntries(EffectSet::Config::IMAGE_BYTES)+
  NvsBudget::blobEntries(PracticeHistory::IMAGE_BYTES);
static unsigned effectSetNeededEntries() {
  const unsigned own=NvsBudget::blobEntries(EffectSet::Config::IMAGE_BYTES);
  return NvsBudget::optionalNeeded(own,effectSetStore.bothSlots()?0:own,NvsBudget::blobEntries(PracticeHistory::IMAGE_BYTES));
}
// MILESTONE_LIST's effect set fields, each after a space.
// loaded= is the mask of added effects that play here (IDs 96-127): the ones a
// table or the set may name; older sites ignore it.
static const char *effectSetFields() {
  static char text[136];char mask[33],loaded[33];effectSet.mask().hex(mask);(EffectSet::celebrating()&EffectSet::LOADED).hex(loaded);
  snprintf(text,sizeof(text)," effect_set=%s effect_set_store=%s effect_set_default=%d loaded=%s",mask,effectSetStoreName(),effectSet.isDefault(),loaded);
  return text;
}
// A new set reaches the Best effect editor and Random's next pick at once.
static void effectSetChanged() {
  controls.offeredEffects=effectSet.mask();
  if(!effectSet.offers(upcomingEffect))upcomingEffect=Appearance::Effect::Off;
  // A Best effect editor open on the device cycles the old set.
  if(controls.page==Controls::Page::Edit && controls.setting==unsigned(Controls::Setting::BestEffect))cancelSettingEdit();
  syncNavigation();dirty=true;
}
// Takes a completed `U` transfer that is a set_effects request; false leaves
// it to finishMilestoneRequest.
LOOP_HELPER static bool finishEffectSetRequest(const Snapshot &s) {
  uint8_t bytes[EffectSet::Request::BYTES];size_t n=0;
  if(!milestoneRx.take(bytes,sizeof(bytes),n) || !EffectSet::Request::claims(bytes,n))return false;
  EffectSet::Mask mask;EffectSet::Problem problem=EffectSet::Problem::None;unsigned effect=0;
  if(!EffectSet::Request::decode(bytes,n,mask,problem,effect)) {
    Serial.println("MILESTONE_ERROR reason=invalid_request written=0");milestoneReply();return true;
  }
  const char *op=EffectSet::Request::NAME;
  if(problem!=EffectSet::Problem::None) {
    Serial.printf("MILESTONE_ERROR op=%s reason=invalid_effects problem=%s effect=%u written=0\n",op,EffectSet::problemName(problem),effect);
    milestoneReply();return true;
  }
  auto fail=[&](const char *reason){Serial.printf("MILESTONE_ERROR op=%s reason=%s written=0\n",op,reason);milestoneReply();return true;};
  if(!effectSetStore.ready())return fail("storage_unavailable");
  auto next=effectSet;next.set(mask);
  // A damaged record is replaced even by the set it already shows.
  if(next.generation()==effectSet.generation() && !effectSetStore.damaged()) {
    Serial.printf("MILESTONE_OK op=%s revision=%lu unchanged=1\n",op,(unsigned long)effectSet.revision);milestoneReply();return true;
  }
  if(next.generation()==effectSet.generation())++next.revision;
  unsigned available=0;
  const unsigned needed=effectSetNeededEntries();
  if(!nvsAvailable(available) || available<needed) {
    Serial.printf("MILESTONE_ERROR op=%s reason=nvs_space available_entries=%u needed_entries=%u written=0\n",op,available,needed);
    milestoneReply();return true;
  }
  if(motionPending || s.phase==AnalogTachometer::Phase::Launch || !acquisition.request(Action::IdleCheckpointBegin,s.launches))return fail("busy");
  const bool ok=effectSetStore.saveViewed(next);
  if(!acquisition.request(Action::CheckpointEnd))Serial.println("MILESTONE_NOTICE adc_checkpoint_resume_failed");
  if(!ok){Serial.printf("MILESTONE_ERROR op=%s reason=write_failed written=unknown\n",op);milestoneReply();dirty=true;return true;}
  effectSet=next;effectSetChanged();
  char hex[33];effectSet.mask().hex(hex);
  Serial.printf("MILESTONE_OK op=%s revision=%lu effect_set=%s effects=%u\n",op,(unsigned long)effectSet.revision,hex,effectSet.count());milestoneReply();
  return true;
}

LOOP_HELPER static void pollMilestoneRequest(const Snapshot &s) {
  const uint32_t now=millis();
  if(milestoneRx.timedOut(now)){milestoneRx.cancel();Serial.println("MILESTONE_ERROR reason=timeout written=0");milestoneReply();return;}
  for(unsigned n=0;n<256 && milestoneRx.active() && Serial.available();++n) {
    inactivity.touch(now);
    switch(milestoneRx.feed(Serial.read(),now)) {
      case DeviceBackup::TransferEvent::Chunk:Serial.printf("MILESTONE_CHUNK chars=%u\n",milestoneRx.received());milestoneReply();break;
      case DeviceBackup::TransferEvent::Complete:if(!finishEffectSetRequest(s))finishMilestoneRequest(s);break;
      case DeviceBackup::TransferEvent::Error:usbCommands.refused(now);Serial.printf("MILESTONE_ERROR reason=%s written=0\n",milestoneRx.error());milestoneReply();break;
      case DeviceBackup::TransferEvent::None:break;
    }
  }
}

LOOP_HELPER static void checkStorage() {
  // Explicit USB bench diagnostic. Scratch measurements never enter practice.
  const auto state=acquisition.snapshot();
  if(motionPending || !acquisition.request(Action::IdleCheckpointBegin,state.launches)) {
    Serial.println("HISTORY_CHECK_ERROR wait until capture finishes");return;
  }
  Preferences scratch;scratch.begin("ll-prac-qa",false);
  // Only this diagnostic owns these scratch keys; recover an interrupted bench.
  if(scratch.isKey("history0"))scratch.remove("history0");
  if(scratch.isKey("history1"))scratch.remove("history1");
  bool ok=!scratch.isKey("history0") && !scratch.isKey("history1");
  const bool clean=ok;
  // Two scratch slots plus a rewrite in flight do not fit beside real history
  // in the 20 KB NVS; check first rather than fill it until a write fails.
  unsigned available=0;const unsigned needed=NvsBudget::alternatingStoreEntries(PracticeHistory::IMAGE_BYTES);
  const bool room=nvsAvailable(available) && available>=needed;
  if(!room)Serial.printf("HISTORY_CHECK_SKIPPED reason=nvs_space available_entries=%u needed_entries=%u written=0\n",available,needed);
  ok=ok && room;
  auto h=std::unique_ptr<PracticeHistory>(new(std::nothrow) PracticeHistory);
  auto store=std::unique_ptr<PracticeStore>(new(std::nothrow) PracticeStore);
  uint32_t maximum=0;unsigned writes=0;
  ok=ok && h && store && store->begin(*h,"ll-prac-qa");
  for(unsigned i=1;ok && i<=36;++i) {
    if(i%6==1)h->newSession();
    h->accept(i,true,5000+i*30,i*20000);
    const int64_t started=esp_timer_get_time();ok=store->save(*h);
    if(ok)++writes;
    maximum=std::max(maximum,uint32_t(esp_timer_get_time()-started));
    auto restored=std::unique_ptr<PracticeHistory>(new(std::nothrow) PracticeHistory);
    auto reader=std::unique_ptr<PracticeStore>(new(std::nothrow) PracticeStore);
    ok=ok && restored && reader && reader->begin(*restored,"ll-prac-qa") &&
      restored->generation()==h->generation() && restored->size()==h->size() &&
      restored->session()->mean()==h->session()->mean() && restored->recent()->rpm==h->recent()->rpm;
  }
  if(clean) {
    if(scratch.isKey("history0"))ok=scratch.remove("history0") && ok;
    if(scratch.isKey("history1"))ok=scratch.remove("history1") && ok;
    ok=ok && !scratch.isKey("history0") && !scratch.isKey("history1");
  }
  const bool resumed=acquisition.request(Action::CheckpointEnd);
  Serial.printf("HISTORY_CHECK ok=%d writes=%u max_write_us=%lu scratch_clean=%d resumed=%d actual_records=%u nvs_available_entries=%u nvs_needed_entries=%u\n",
    ok,writes,(unsigned long)maximum,clean,resumed,practice.size(),available,needed);dirty=true;
}

LOOP_HELPER static void checkMotionStorage() {
  const auto state=acquisition.snapshot();
  if(motionPending || !acquisition.request(Action::IdleCheckpointBegin,state.launches)) {
    Serial.println("MOTION_CHECK_ERROR wait until capture finishes");return;
  }
  Preferences scratch;scratch.begin("ll-mot-qa",false);
  for(const char *key:{"ref0","ref1"})if(scratch.isKey(key))scratch.remove(key);
  const bool clean=!scratch.isKey("ref0") && !scratch.isKey("ref1");
  unsigned available=0;const unsigned needed=NvsBudget::alternatingStoreEntries(LaunchMotion::IMAGE_BYTES);
  const bool room=nvsAvailable(available) && available>=needed;
  if(!room)Serial.printf("MOTION_CHECK_SKIPPED reason=nvs_space available_entries=%u needed_entries=%u written=0\n",available,needed);
  auto t=std::unique_ptr<LaunchMotion::Trace>(new(std::nothrow) LaunchMotion::Trace);
  auto restored=std::unique_ptr<LaunchMotion::Trace>(new(std::nothrow) LaunchMotion::Trace);
  auto store=std::unique_ptr<MotionStore>(new(std::nothrow) MotionStore);
  bool ok=clean && room && t && restored && store && store->begin(*restored,"ll-mot-qa");
  unsigned writes=0;uint32_t maximum=0;
  if(t)MotionUI::makeDemo(*t,true);
  for(unsigned i=1;ok && i<=16;++i) {
    t->number=i;t->rpm=5000+i*50;const int64_t started=esp_timer_get_time();ok=store->save(*t);
    if(ok)++writes;maximum=std::max(maximum,uint32_t(esp_timer_get_time()-started));
    auto reader=std::unique_ptr<MotionStore>(new(std::nothrow) MotionStore);
    ok=ok && reader && reader->begin(*restored,"ll-mot-qa") && restored->number==i && restored->rpm==t->rpm;
  }
  bool cleaned=clean;
  if(clean)for(const char *key:{"ref0","ref1"})if(scratch.isKey(key))cleaned=scratch.remove(key) && cleaned;
  cleaned=cleaned && !scratch.isKey("ref0") && !scratch.isKey("ref1");
  const bool resumed=acquisition.request(Action::CheckpointEnd);
  Serial.printf("MOTION_CHECK ok=%d writes=%u bytes=%u max_write_us=%lu scratch_clean=%d resumed=%d actual_records=%u actual_ref=%lu nvs_available_entries=%u nvs_needed_entries=%u\n",
    ok,writes,unsigned(LaunchMotion::IMAGE_BYTES),(unsigned long)maximum,cleaned,resumed,practice.size(),(unsigned long)referenceMotion.number,available,needed);
  dirty=true;
}

// Every checkpoint begins and ends inside one call; only the wipe flow keeps
// one, and the loop stops before here while it does. A stopped owner that is
// not resting and outlasts a loop pass therefore means a Begin whose reply was
// lost, or an End that never arrived. Release it, or the ADC would stay
// stopped and no later pull would be measured.
LOOP_HELPER static void releaseOrphanCheckpoint(uint32_t now,const Snapshot &s) {
  if(!s.suspended || s.resting || wipeCheckpointHeld){orphanSeen=false;return;}
  if(!orphanSeen){orphanSeen=true;orphanSince=now;return;}
  if(uint32_t(now-orphanSince)<ORPHAN_CHECKPOINT_MS)return;
  orphanSeen=false;
  const bool resumed=acquisition.request(Action::CheckpointEnd) || acquisition.request(Action::Pin,s.pin);
  Serial.printf("ADC_NOTICE orphaned_checkpoint_released resumed=%d\n",resumed);dirty=true;
}

#if LAUNCHLAB_HIL
#include "hil.h"
#endif
// The longest request lines (a v3 milestone request, a theme request, a
// restore line) fit the RX queue twice over.
static_assert(USB_RX_BUFFER_BYTES>=2*((Milestones::Request::BYTES+2)/3*4+2) &&
              USB_RX_BUFFER_BYTES>=2*((Themes::Request::BYTES+2)/3*4+2) && USB_RX_BUFFER_BYTES>=2*(96+2),
              "a request line must fit the USB RX queue");
void setup() {
  const auto wakeCause=esp_sleep_get_wakeup_cause();
  const int64_t sleepEnded=wallUs();
  Serial.setRxBufferSize(USB_RX_BUFFER_BYTES); // Before begin.
  // The core's 256-byte USB TX ring cannot hold one status block, so with the
  // 10 ms timeout below whole lines were dropped while the host was reading
  // (truncated status, a failed backup checksum). Must be set before begin.
  Serial.setTxBufferSize(USB_TX_BUFFER_BYTES);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(10);
  UsbTx::calibrate(); // The TX ring is empty: its free size marks "drained".
  // Set once before either acquisition task starts. Keep ADC, USB and SPI
  // peripheral clocks unchanged; do not switch frequency during a launch.
  const bool clockSet=setCpuFrequencyMhz(LAUNCHLAB_CPU_MHZ);
  const bool clockOk=clockSet && getCpuFrequencyMhz()==LAUNCHLAB_CPU_MHZ && getApbFrequency()==80000000;
  if(!clockOk)setCpuFrequencyMhz(240);
  Serial.printf("CPU_POLICY requested_mhz=%u actual_mhz=%lu apb_hz=%lu fallback=%d\n",
    unsigned(LAUNCHLAB_CPU_MHZ),(unsigned long)getCpuFrequencyMhz(),(unsigned long)getApbFrequency(),!clockOk);
  // Free a bus left held by a slave interrupted mid-read before any I2C use.
  i2cRecovery=I2cBusRecovery::recoverStickS3Internal(i2cRecoveryPulses,i2cSdaWas,i2cSclWas);
  Serial.printf("I2C_RECOVERY bus=internal result=%s pulses=%u sda_was=%d scl_was=%d\n",I2cBusRecovery::name(i2cRecovery),i2cRecoveryPulses,i2cSdaWas,i2cSclWas);
  CpuPower::begin();
  CpuPower::Guard startupClock(CpuPower::Ui);
  auto cfg=M5.config();
  cfg.fallback_board=m5::board_t::board_M5StickS3;
  // internal_spk only configures the speaker; it stays unpowered until a sound plays.
  cfg.internal_mic=cfg.internal_rtc=false;cfg.internal_spk=cfg.internal_imu=true;
  cfg.external_imu=cfg.external_rtc=false;cfg.output_power=false;
  // This UI uses A/B only. M5.update otherwise polls the unused BtnPWR IRQ
  // via PM1 every 4 ms, adding I2C traffic and preventing PM1 idle timeout.
  // PM1's physical power-key/reset operation remains a hardware function.
  cfg.pmic_button=false;
  M5.begin(cfg);
  soundPlayer.configure();
  M5.Ex_I2C.release();
  pm1WakeValid=pm1Idle().read(0x05,&pm1WakeFlags,1);
  if(!pm1Idle().run([](){return M5.Power.M5pm1.clearWakeSource();}))Serial.println("PM1_NOTICE wake_clear_failed");
  if(!shakeWake.clear())Serial.println("SHAKE_WAKE_NOTICE startup_disarm_failed");
  pinMode(POWER_KEY_IRQ_PIN,INPUT_PULLUP);
  attachInterrupt(POWER_KEY_IRQ_PIN,onPowerKeyIrq,FALLING);
  // The value found at boot shows whether PM1 kept SINGLE_RST_DIS through power-off.
  uint8_t bootKeyConfig=0;
  const bool bootKeyConfigRead=pm1Idle().read(StickS3PowerKey::BUTTON_CONFIG,&bootKeyConfig,1);
  const bool keyConfigured=powerKey().configure();
  Serial.printf("POWER_KEY_BOOT btn_cfg_valid=%d btn_cfg=0x%02x configured=%d\n",bootKeyConfigRead,bootKeyConfig,keyConfigured);
  if(!recoverWipe())return;
  prefs.begin("launchlab-mini",false);
  openHistoryLog();
  powerLogReady=powerPrefs.begin("ll-power",false);
  PowerDiagnostics::fresh(powerLog);
  bool powerInNvs=false;
  if(powerLogReady && powerPrefs.isKey("log")){
    PowerDiagnostics::Image loaded;
    if(powerPrefs.getBytesLength("log")==sizeof(loaded) && powerPrefs.getBytes("log",&loaded,sizeof(loaded))==sizeof(loaded)
      && PowerDiagnostics::valid(loaded)){powerLog=loaded;powerInNvs=true;powerMirror=loaded.checksum;}
    else {powerLogReady=false;Serial.println("POWER_LOG_NOTICE invalid_existing_log_retained");}
  }
  {std::unique_ptr<PowerDiagnostics::Image> logged(new(std::nothrow) PowerDiagnostics::Image);uint32_t mirrored=0;
   const bool inLog=logged && historyLog.isOpen() && historyLog.loadPower(*logged,mirrored);
   if(HistoryLog::pick(inLog,mirrored,powerInNvs,powerMirror)==HistoryLog::Source::Log)powerLog=*logged;}
  ++powerLog.boots;
  const bool wakingFromStandby=powerLog.pendingSleep;
  uint8_t loggedWake=pm1WakeValid?pm1WakeFlags:0;
  if(wakingFromStandby && ((pm1WakeValid && (pm1WakeFlags&0x20)) || wakeCause==ESP_SLEEP_WAKEUP_EXT0))++powerLog.shakeWakes;
  else if(pm1WakeValid && (pm1WakeFlags&0x0c))++powerLog.powerButtonWakes;
  else ++powerLog.otherWakes;
  if(wakingFromStandby) {
    if(wakeCause!=ESP_SLEEP_WAKEUP_UNDEFINED && sleepReceipt.magic==PowerDiagnostics::MAGIC
      && sleepEnded>=sleepReceipt.atUs && sleepEnded-sleepReceipt.atUs<366LL*24*3600*1000000)
      powerLog.sleepMs+=uint64_t(sleepEnded-sleepReceipt.atUs)/1000;
    else ++powerLog.unknownSleepIntervals; // PM1 battery shutdown loses ESP32 RTC time.
  }
  powerLog.pendingSleep=0;sleepReceipt.magic=0;
  bool restoredNavigation=false;
  if(prefs.isKey("wake_nav")) {
    Controls::State restored;uint8_t restoredView=0;
    const bool valid=Controls::decodeWake(prefs.getUInt("wake_nav",0),restored,restoredView);
    const bool consumed=prefs.remove("wake_nav");
    if(valid && consumed){controls=restored;tournamentView=TournamentView(restoredView);syncNavigation();restoredNavigation=true;}
    Serial.printf("WAKE_NAV_RESTORE valid=%d consumed=%d page=%u view=%u\n",valid,consumed,unsigned(controls.page),unsigned(tournamentView));
  }
  if(prefs.isKey("wake_ui")) {
    WakeUi::State state;
    const bool valid=WakeUi::decode(prefs.getUInt("wake_ui",0),uint8_t(PracticeUI::View::Settings),state);
    const bool consumed=prefs.remove("wake_ui");
    if(valid && consumed && !restoredNavigation) {
      tournamentMode=state.tournament;tournamentView=TournamentView(state.tournamentView);
      historyPage=state.history;historyView=PracticeUI::restoredView(state.historyView);
      controls.page=state.tournament?Controls::Page::Tournament:!state.history?Controls::Page::Main:
        historyView==PracticeUI::View::Battery?Controls::Page::Battery:
        historyView==PracticeUI::View::Settings?Controls::Page::Settings:Controls::Page::History;
      if(controls.page==Controls::Page::History)controls.history=uint8_t(historyView);
      syncNavigation();
    }
    Serial.printf("WAKE_UI_RESTORE valid=%d consumed=%d tournament=%d display=%s history=%d page=%u\n",
      valid,consumed,tournamentMode,tournamentViewName(),historyPage,unsigned(historyView));
  }
  if(!RpmEstimatorSetting::load(prefs,rpmEstimator))
    Serial.printf("RPM_SETTING_NOTICE invalid_record_retained_using_default=%s\n",RpmEstimator::name(RpmEstimator::DEFAULT_MODE));
  if(!SensorProfileSetting::load(prefs,sensorProfile))Serial.println("SENSOR_SETTING_NOTICE invalid_record_retained_using_standard_default");
  loadStartMarks();
  const uint8_t sleepMinutes=prefs.getUChar("sleep_min",InactivityTimer::DEFAULT_MINUTES);
  if(!inactivity.setMinutes(sleepMinutes)) {
    inactivity.setMinutes(InactivityTimer::DEFAULT_MINUTES);
    Serial.printf("SLEEP_SETTING_NOTICE invalid_saved_value_using_%u_min\n",unsigned(InactivityTimer::DEFAULT_MINUTES));
  }
  if(!Brightness::load(prefs,brightnessPercent))
    Serial.printf("BRIGHTNESS_SETTING_NOTICE invalid_saved_value_using_default_pct=%u\n",unsigned(Brightness::DEFAULT_PERCENT));
  if(!ShakeWakeSetting::load(prefs,shakeToWake))Serial.println("SHAKE_WAKE_SETTING_NOTICE invalid_saved_value_retained_using_on");
  if(!DisplayFlipSetting::load(prefs,displayFlipped))Serial.println("DISPLAY_SETTING_NOTICE invalid_record_retained_using_flip_off_default");
  if(!MountOrientationSetting::load(prefs,mountMode))Serial.println("MOUNT_SETTING_NOTICE invalid_record_retained_using_top_default");
  liveMount=mountMode;
  if(!appearanceStore.begin(appearance,"ll-appearance","cfg0","cfg1"))Serial.println("APPEARANCE_NOTICE invalid_record_retained_using_defaults");
  const bool bestsInNvs=bestStore.begin(personalBests,"ll-bests","best0","best1") && bestStore.present();
  if(!bestStore.ready() || bestStore.damaged())Serial.println("BEST_NOTICE invalid_record_retained_celebration_disabled");
  {Appearance::Bests logged;uint32_t mirrored=0;const bool inLog=historyLog.isOpen() && historyLog.loadBests(logged,mirrored);
   const auto from=HistoryLog::pick(inLog,mirrored,bestsInNvs,personalBests.generation());
   if(from==HistoryLog::Source::Log)personalBests=logged;
   loggedBests=from==HistoryLog::Source::Log?personalBests.generation():~personalBests.generation();}
  if(themeStore.begin(themeLibrary)==ThemeStore::Status::Unreadable)Serial.println("THEME_NOTICE invalid_record_retained_using_default_colors");
  // 0.17's slots, a packed record for a wider table, else 0.15's carried over (milestone_store.h).
  milestoneStore.begin(milestones);
  if(!milestoneStore.ready())Serial.println("MILESTONE_NOTICE storage_unavailable_using_defaults");
  else if(milestoneStore.damaged())Serial.println("MILESTONE_NOTICE invalid_record_using_defaults");
  else if(milestoneStore.source()==MilestoneStore::Source::Legacy)Serial.println("MILESTONE_NOTICE imported_v1_table");
  if(!effectSetStore.begin(effectSet,"ll-appearance","eset0","eset1",true))
    Serial.println(effectSetStore.damaged()?"EFFECT_SET_NOTICE invalid_record_using_defaults":"EFFECT_SET_NOTICE storage_unavailable_using_defaults");
  controls.customThemes=themeLibrary.validMask();controls.offeredEffects=effectSet.mask();
  if(!themeLibrary.available(appearance.theme))Serial.printf("THEME_NOTICE selected_theme_unavailable theme=%u showing=%u\n",unsigned(appearance.theme),unsigned(Themes::FALLBACK));
  uint8_t pin=prefs.getUChar("ao_pin",1);
  if(pin<1 || pin>10)pin=1;
  extPower=prefs.getBool("ext5v",false);
  saveError=!practiceStore.begin(practice);
  // Newest wins between the log and NVS (history_log.h); a copy only in NVS is logged at the next save.
  if(historyLog.isOpen()) {
    std::unique_ptr<PracticeHistory> logged(new(std::nothrow) PracticeHistory);uint32_t mirrored=0;
    const bool inLog=logged && historyLog.loadHistory(*logged,mirrored);
    const auto from=HistoryLog::pick(inLog,mirrored,practiceStore.found(),practiceStore.savedGeneration());
    if(from==HistoryLog::Source::Log){practice=*logged;saveError=false;}
    loggedHistory=from==HistoryLog::Source::Log?practice.generation():~practice.generation();
    Serial.printf("HISTORY_LOG_LOAD from=%s log_generation=%lu mirrored=%lu nvs_generation=%lu nvs_found=%d\n",
      from==HistoryLog::Source::Log?"log":from==HistoryLog::Source::Nvs?"nvs":"none",inLog?(unsigned long)logged->generation():0ul,
      (unsigned long)mirrored,(unsigned long)practiceStore.savedGeneration(),practiceStore.found());
  }
  // Reclaim the orphaned RF calibration blob found in ll-practice; the history
  // save it would otherwise crowd out matters far more. Boot only, before ADC.
  if(practiceStore.hasOrphan())Serial.printf("NVS_RECLAIM key=ll-practice/%s removed=%d\n",PracticeStore::ORPHAN_KEY,practiceStore.removeOrphan());
  practice.newSession(); // Boot starts a fresh metric/session boundary; old scalar history stays intact.
  referenceError=!motionStore.begin(referenceMotion);
  if(const auto *last=practice.recent()) {
    latestMotion.number=last->number;latestMotion.rpm=last->rpm;
    if(referenceMotion.valid() && referenceMotion.fused && referenceMotion.number==last->number)latestMotion=referenceMotion;
  }
  if(!pm1Idle().run([](){M5.Power.setExtOutput(extPower);return true;}))Serial.println("EXT_POWER_NOTICE startup_wake_failed");
  const bool idleConfigured=pm1Idle().configure();
  Serial.printf("PM1_IDLE_CONFIG ok=%d seconds=%u fallback_verified=%d\n",idleConfigured,
    unsigned(pm1Idle().stats().seconds),pm1Idle().stats().fallbackVerified);
  M5.Display.setRotation(DisplayOrientation::rotation(displayFlipped));M5.Display.setBrightness(backlightValue());M5.Display.setTextSize(1);
  // Internal RAM first: at 80 MHz a PSRAM canvas made a recap frame about 8 ms
  // slower to compose and push. M5Canvas defaults to PSRAM, which stays the
  // fallback if 64.8 KB of internal RAM is not free.
  screen.setColorDepth(16);screen.setPsram(false);
  screenReady=screen.createSprite(M5.Display.width(),M5.Display.height())!=nullptr;
  if(!screenReady){screen.setPsram(true);screenReady=screen.createSprite(M5.Display.width(),M5.Display.height())!=nullptr;}
  if(!screenReady)Serial.println("DISPLAY_NOTICE direct_draw_fallback");
  if(saveError)Serial.println("HISTORY_ERROR storage unavailable; existing bytes retained");
  M5.BtnA.setHoldThresh(1000);M5.BtnB.setHoldThresh(1000);
  for(unsigned attempt=0;attempt<3 && !startMotion();++attempt)if(attempt<2)delay(100);
  if(!imuAcquisition.running())Serial.println("MOTION_NOTICE IMU startup failed; automatic idle retry enabled");
  acquisition.setRequestHook(quietImuForFlash);
  // What the ADC's continuous driver finds when it allocates (HEAP_BOOT).
  constexpr uint32_t DMA_HEAP=MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL;
  const unsigned dmaFreeBeforeAdc=unsigned(heap_caps_get_free_size(DMA_HEAP)),dmaLargestBeforeAdc=unsigned(heap_caps_get_largest_free_block(DMA_HEAP));
  if(!acquisition.begin(pin,rpmEstimator,sensorProfile))Serial.println("ADC_ERROR acquisition_task_allocation");
  openLoadedEffects();
  pollBattery(millis(),acquisition.snapshot());
  powerRecord(wakingFromStandby?PowerDiagnostics::Wake:PowerDiagnostics::Boot,loggedWake);
  powerLogTick=lastPowerSample=millis();
  Serial.printf("POWER_WAKE esp_cause=%d pm1_flags_valid=%d pm1_flags=0x%02x pending_standby=%d\n",int(wakeCause),pm1WakeValid,pm1WakeFlags,wakingFromStandby);
  // Use hardware wake evidence; the optional diagnostic log can fail to save
  // and must not determine the battery policy.
  inactivity.begin(millis(),(pm1WakeValid && (pm1WakeFlags&0x20)) || wakeCause==ESP_SLEEP_WAKEUP_EXT0);
  Serial.printf("HEAP_BOOT before_adc_dma_free=%u before_adc_dma_largest=%u dma_free=%u dma_largest=%u internal_free=%u usb_rx_bytes=%u\n",
    dmaFreeBeforeAdc,dmaLargestBeforeAdc,unsigned(heap_caps_get_free_size(DMA_HEAP)),unsigned(heap_caps_get_largest_free_block(DMA_HEAP)),
    unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(USB_RX_BUFFER_BYTES));
  Serial.printf("BOOT %s ppr=1 min_rpm=1000 min_mark=%.0f edge_fraction=%.2f sample_hz=50000 task_core=0\n",VERSION,
    StickS3SensorProfile::minMark(sensorProfile),StickS3SensorProfile::EDGE_SWING_FRACTION);
  Serial.printf("RPM_SETTING_BOOT selected=%s default=%s legacy_history=retained_metric_not_encoded\n",RpmEstimator::name(rpmEstimator),
    RpmEstimator::name(RpmEstimator::DEFAULT_MODE));
  Serial.println("READY commands: p status, P power log, C CPU power profiling, a/c tap A, h tap B, o hold A, b hold B, d diagnostics, n new session, u history export, K backup export, R backup restore, L custom themes, T theme request, M milestones, U milestone request, F loaded effects, X loaded-effect request, e motion export, q synthetic recap, E<0-4,7> best effect preview, t synthetic history, y/z storage check, v screen, w recorder export, x ext5V, g<N> GPIO, s stress, j UI stall");
#if LAUNCHLAB_HIL
  hilBoot();
#endif
}

// A pull, leaving the recap or closing the Sound editor silences at once;
// the codec powers down once silent and no pull is being measured.
LOOP_HELPER static void tickSound(const Snapshot &s) {
  const bool measuring=s.phase==AnalogTachometer::Phase::Launch;
  using S=Controls::Setting;
  const bool effectEditor=controls.page==Controls::Page::Edit && controls.setting==unsigned(S::BestEffect);
  const bool celebrating=bestEffect.badge(),editing=effectEditor || (controls.page==Controls::Page::Edit && controls.setting==unsigned(S::Sound));
  if(editorEffect.badge() && !effectEditor){editorEffect.clear();dirty=true;}
  // A pull fades a song out within 20 ms and stops other sounds at once; a
  // song plays on past its effect's frames.
  if(measuring){soundPlayer.fadeOut();celebrationSound=soundPreview=false;}
  else if((celebrationSound && !celebrating && !soundPlayer.streaming()) || (soundPreview && !editing)) {
    soundPlayer.stop();celebrationSound=soundPreview=false;
  }
  if(soundRequest.active && (measuring || (soundRequest.celebration?!celebrating || bestEffect.elapsed(millis())>SOUND_LATE_MS:!editing)))
    soundRequest.active=false;
  if(!soundPlayer.playing())celebrationSound=soundPreview=false;
  soundPlayer.tick(measuring || motionPending);
  updateStartFloor();
  if(measuring || motionPending)return;
  // Render a slice per idle pass (1/64 s of audio, a few ms): a waiting
  // request first, else the chosen effect, ready before the next best or the
  // first preview while the Sound editor is open, then every effect the
  // milestone table can play, since a milestone is known only once its pull ends.
  constexpr size_t SLICE=BestEffectSound::RATE/64;
  if(soundRequest.active) {
    if(soundPlayer.prepare(soundRequest.effect,SLICE))startSound(true);
    return;
  }
  const bool wanted=appearance.sound!=Appearance::Sound::Off || editing;
  if(!wanted)return;
  // The editor's preview holds its first frame for its sound: render all of
  // it now (about 0.3 s on the device) rather than a slice per pass.
  const bool waiting=editorEffect.badge() && !editorEffect.started();
  if(!soundPlayer.prepared(soundTarget())){soundPlayer.prepare(soundTarget(),waiting?SIZE_MAX:SLICE);return;}
  if(appearance.sound==Appearance::Sound::Off || !Milestones::milestones(milestones.mode()))return;
  const auto *picks=milestonePicks();
  for(unsigned i=0;i<milestones.count();++i) {
    const auto effect=picks[i];
    if(!soundPlayer.prepared(effect)){soundPlayer.prepare(effect,SLICE);return;}
  }
}

// USB only: replay the last recap with a celebration to check an effect on the
// device. Bests, history and settings are unchanged; max_effect_us and
// max_draw_us restart so PERF reports this preview's slowest pass and frame.
LOOP_HELPER static void previewBestEffect(Appearance::Effect effect) {
#if LAUNCHLAB_HIL
  HilFx::playing=-1;
#endif
  if(tournamentMode || motionPending || !latestMotion.number){Serial.println("EFFECT_PREVIEW_ERROR no_recap");return;}
  if(controls.page==Controls::Page::Edit)cancelSettingEdit();
  controls.page=Controls::Page::Main;syncNavigation();
  historyPage=diagnostics=motionDemo=demo=false;
  // A preview may pick an effect other than the saved one: render its sound
  // here, before the recap and its timing start, rather than miss its cue.
  uint32_t renderUs=0;
  if(appearance.sound!=Appearance::Sound::Off && !soundPlayer.prepared(effect)) {
    soundPlayer.stop();const int64_t started=esp_timer_get_time();
    soundPlayer.prepare(effect);renderUs=uint32_t(esp_timer_get_time()-started);
  }
  launchFeedback.show(millis());
  bestEffect.queue(true,latestMotion.number,effect);bestEffect.captured(latestMotion.number);
  maxEffectUs=maxDrawUs=0;dirty=true;
#if LAUNCHLAB_HIL
  effectFrames.count=0;
#ifdef BEST_EFFECT_PROFILE
  effectStages.count=0;
#endif
#endif
  Serial.printf("EFFECT_PREVIEW effect=%s duration_ms=%lu launch=%lu sound_render_us=%lu bests_unchanged=1\n",Appearance::label(effect),
    (unsigned long)Appearance::duration(effect),(unsigned long)latestMotion.number,(unsigned long)renderUs);
}

// USB commands, bounded per pass. A real call: its inlined handlers once made
// up most of loop()'s frame.
LOOP_HELPER static void exportCpuPower() {
#if LAUNCHLAB_CPU_PM
  // IDF's stdout is UART, whereas this device uses native USB. Buffer its
  // profiling report so a read-only USB command can retrieve idle mode times.
  constexpr size_t bytes=8192;
  auto buffer=std::unique_ptr<char[]>(new(std::nothrow) char[bytes]);
  FILE *stream=buffer?fmemopen(buffer.get(),bytes,"w"):nullptr;
  if(!stream){Serial.println("CPU_PM_TRACE error=no_buffer");return;}
  const int result=CpuPower::dump(stream);fflush(stream);
  const long length=ftell(stream);fclose(stream);
  Serial.printf("CPU_PM_TRACE begin enabled=%d error=%s\n",CpuPower::active(),esp_err_to_name(result));
  if(length>0)Serial.write(reinterpret_cast<const uint8_t*>(buffer.get()),std::min<size_t>(size_t(length),bytes-1));
  Serial.println("CPU_PM_TRACE end");
#else
  Serial.println("CPU_PM_TRACE enabled=0");
#endif
}

// One accepted USB command (usb_command_guard.h): a letter, "g<pin>" or
// "E[<digit>]".
LOOP_HELPER static void runUsbCommand(const char *command,const Snapshot &s) {
  const char c=command[0];
  if(c=='g') {
    char *end=nullptr;const long pin=strtol(command+1,&end,10);
    if(end!=command+1 && *end==0 && pin>=1 && pin<=10) {
      if(acquisition.request(Action::Pin,uint8_t(pin)))settingsSave.mark(SettingsSave::Pin);
      else Serial.println("COMMAND_ERROR gpio_init");
      dirty=true;
    } else Serial.println("COMMAND_ERROR gpio_must_be_1_to_10");
    return;
  }
  if(c=='E') {
    // E<id> previews that effect (its stored value, one to three digits: a
    // loaded effect is 96-127); E alone the saved one.
    const unsigned id=command[1]?unsigned(strtoul(command+1,nullptr,10)):255u;
    previewBestEffect(Appearance::celebrates(id)?Appearance::Effect(id):celebrationEffect());
    return;
  }
  if(c=='p')status();
  else if(c=='P')exportPowerLog();
  else if(c=='C')exportCpuPower();
  else if(c=='S'){
    inactivity.touch(millis()-inactivity.timeout());
    Serial.println("POWER_SLEEP_TEST requested=1 normal_save_and_sleep_path=1");
  }
  else if(c=='O'){
    powerKeyRequested=true;powerKeyRequestedAt=millis();
    Serial.println("POWER_KEY_TEST requested=1 normal_save_and_power_off_path=1");
  }
  else if(c=='i') {
    if(acquisition.request(Action::IdleCheckpointBegin,s.launches)) {
      if(imuAcquisition.lock()) {
        const bool armed=shakeWake.arm(),restored=shakeWake.restore();
        if(!restored)noteMotionRestoreFailure();
        const bool extRestored=pm1Idle().run([](){M5.Power.setExtOutput(extPower);return true;});
        imuAcquisition.unlock();
        if(!extRestored)Serial.println("EXT_POWER_NOTICE test_restore_wake_failed");
        Serial.printf("SHAKE_WAKE_CHECK armed=%d restored=%d sleep=0\n",armed,restored);
      }
      acquisition.request(Action::CheckpointEnd);
    }
  }
  else if(c=='a')buttonA();
  else if(c=='o')holdA();
  else if(c=='d'){diagnostics=!diagnostics;dirty=true;}
  else if(c=='h')toggleHistory();
  else if(c=='c')nextView();
  else if(c=='e')exportMotion();
  else if(c=='Q'){const bool ok=imuAcquisition.quiet(300);Serial.printf("IMU_QUIET_TEST ms=300 ok=%d flash_write=0\n",ok);}
  else if(c=='q') {
    if(tournamentMode){Serial.println("RECAP_DEMO_ERROR leave_tournament_first");return;}
    if(controls.page==Controls::Page::Edit)cancelSettingEdit();
    controls.page=Controls::Page::Main;syncNavigation();
    historyPage=diagnostics=false;motionDemo=true;
    MotionUI::makeDemo(demoMotion,false);
    launchFeedback.show(millis());dirty=true;
    Serial.println("RECAP_DEMO synthetic only; real launches and reference unchanged");
  }
  else if(c=='b')holdB();
  else if(c=='n')newSession();
  else if(c=='u')exportHistory();
  else if(c=='K')exportBackup();
  else if(c=='R')startRestore();
  else if(c=='L')listThemes();
  else if(c=='T'){themeRx.start(millis());Serial.printf("THEME_READY max_chars=%u\n",unsigned((Themes::Request::BYTES+2)/3*4));themeReply();}
  else if(c=='M')listMilestones();
  else if(c=='F')listEffects();
  else if(c=='X')startEffectRequest();
  else if(c=='U'){milestoneRx.start(millis());Serial.printf("MILESTONE_READY max_chars=%u\n",unsigned((Milestones::Request::BYTES+2)/3*4));milestoneReply();}
  else if(c=='t')previewHistory();
  else if(c=='y')checkStorage();
  else if(c=='z')checkMotionStorage();
  else if(c=='v')exportScreen();
  else if(c=='w')exportSignal();
  else if(c=='x') {
    // Explicit command only. No automatic voltage/power changes in sampling.
    if(!imuAcquisition.lock()){Serial.println("COMMAND_ERROR power_busy");return;}
    const bool nextExtPower=!extPower;
    const bool changed=pm1Idle().run([&](){M5.Power.setExtOutput(nextExtPower);return true;});
    imuAcquisition.unlock();
    if(changed){extPower=nextExtPower;settingsSave.mark(SettingsSave::ExtPower);}
    else Serial.println("EXT_POWER_NOTICE toggle_wake_failed");
    acquisition.request(Action::Reset);dirty=true;status();
  } else if(c=='s'){stress=!stress;diagnostics=true;dirty=true;}
  else if(c=='j'){++uiStalls;delay(200);}
}

LOOP_HELPER static void pollCommands(const Snapshot &s) {
  // Stray bytes (an upload gone wrong, a buggy updater, line noise) must not
  // run state-changing letters: see usb_command_guard.h.
  auto &guard=usbCommands;char command[UsbCommandGuard::MAX+1];
  if(controls.page==Controls::Page::Wipe || wipeBlocked) {
    restoreRx.cancel();themeRx.cancel();milestoneRx.cancel();effectRx.cancel();guard.cancel();
  } else {
    if(restoreRx.active())pollRestore(s);
    if(themeRx.active())pollThemeRequest(s);
    if(milestoneRx.active())pollMilestoneRequest(s);
    if(effectRx.active())pollEffectRequest(s);
    // Quiet is the receive queue seen empty, not a slow pass.
    if(!Serial.available())guard.idle(millis());
    if(guard.due(millis(),command))runUsbCommand(command,s);
  }
  // Bound incoming work; an unending USB stream cannot starve button/draw work.
  for(unsigned n=0;n<32 && !restoreRx.active() && !themeRx.active() && !milestoneRx.active() && !effectRx.active() && Serial.available();++n) {
    const char c=Serial.read();const uint32_t now=millis();
    // Wipe confirmations and the final hold belong to physical buttons.
    // Read-only status and cancellation cannot advance the confirmation.
    if(controls.page==Controls::Page::Wipe || wipeBlocked) {
      if(c=='p')status();else if(c=='b' && !wipeBlocked)cancelWipe();
      continue;
    }
    if(guard.drop(c,now))continue;  // the rest of a refused transfer line
#if LAUNCHLAB_HIL
    if(hilFeed(c)){guard.consumed(c,now);continue;}
#endif
    // Status polling is observational; other explicit USB actions count as use.
    if(c!='p' && c!='P' && c!='C' && c!='\n' && c!='\r')inactivity.touch(now);
    if(guard.due(now,command))runUsbCommand(command,s);
    if(guard.feed(c,now,command))runUsbCommand(command,s);
    if(controls.page==Controls::Page::Wipe || wipeBlocked)guard.cancel();
  }
  if(!Serial.available())guard.idle(millis());
}

#if LAUNCHLAB_MOTION_ADC_GATE
LOOP_HELPER static void tickSensorIdle(uint32_t now,Snapshot &s) {
  const auto imu=imuAcquisition.snapshot();
#if LAUNCHLAB_HIL
  const bool usbBlocks=usbAwake.inhibited(now) && !Hil::usbIdle; // Rest with the rig attached.
#else
  const bool usbBlocks=usbAwake.inhibited(now);
#endif
  const bool blocked=usbBlocks || inactivity.unusedShake() || wipeBlocked ||
    controls.page!=Controls::Page::Main || historyPage || diagnostics || tournamentMode ||
    motionPending || launchFeedback.shown() || historyUnsaved() ||
    bestsUnsaved() || settingsSave.pending() || !imuAcquisition.running() || Serial.available();
  const bool opticalIdle=s.ready && !s.suspended && !s.signalFault && s.opticalIdle &&
    s.phase==AnalogTachometer::Phase::Ready;
  const auto action=sensorIdle.decide(now,esp_timer_get_time(),imu.activity,inactivity.elapsed(now),
    s.resting,opticalIdle,blocked);
  if(action==SensorIdle::Action::None)return;
  const bool pause=action==SensorIdle::Action::Pause;
  const bool ok=acquisition.request(pause?Action::IdlePause:Action::IdleResume,s.launches);
  sensorIdle.attempted(now,action,ok);s=acquisition.snapshot();dirty=true;
  Serial.printf("SENSOR_IDLE_CHANGE pause=%d ok=%d resting=%d state=%s sensor_power=on_shared_rail\n",
    pause,ok,s.resting,s.stateName());
}
#endif

void loop() {
  CpuPower::Guard clock(CpuPower::Ui);
  bool buttonsFresh=false;
  if(imuAcquisition.lock()){M5.update();imuAcquisition.unlock();buttonsFresh=true;}
  Snapshot s=acquisition.snapshot();
  if(buttonsFresh && (M5.BtnA.isPressed() || M5.BtnB.isPressed() || M5.BtnA.wasReleased() || M5.BtnB.wasReleased()))
    inactivity.touch(millis());
  if(wipeBlocked) {
    pollWipe(millis(),buttonsFresh,s);
    tickDisplayPower(millis(),s);
    if(dirty){draw(s);lastDraw=millis();dirty=false;}
    clock.release();delay(IdlePowerPolicy::UI_POLL_MS);return;
  }
  pollLevel(millis());
  if(controls.page==Controls::Page::WearCheck) {
    const auto rawTilt=level.reading(millis());
    controls.wear.observeLevel(rawTilt,s.phase==AnalogTachometer::Phase::Launch);
    if(!controls.wear.complete() && uint32_t(millis()-lastDraw)>=100) {
      const auto tilt=StickS3Level::forDisplay(rawTilt,visibleDisplayFlipped());
      if(tilt.state!=drawnLevel.state || (tilt.valid() &&
        (std::fabs(tilt.degrees-drawnLevel.degrees)>=0.15f ||
         std::fabs(tilt.right-drawnLevel.right)>=0.02f ||
         std::fabs(tilt.down-drawnLevel.down)>=0.02f)))dirty=true;
    }
  }
#if LAUNCHLAB_MOTION_ADC_GATE
  tickSensorIdle(millis(),s);
#endif
  if(controls.page==Controls::Page::Wipe)pollWipe(millis(),buttonsFresh,s);
  else if(buttonsFresh) {
    const bool clickedA=gestureA.click(M5.BtnA.wasPressed(),M5.BtnA.wasHold(),M5.BtnA.wasClicked());
    const bool clickedB=gestureB.click(M5.BtnB.wasPressed(),M5.BtnB.wasHold(),M5.BtnB.wasClicked());
    if(M5.BtnA.wasHold())holdA();else if(clickedA)buttonA();
    if(M5.BtnB.wasHold())holdB();else if(clickedB)buttonB();
  }
  if(wipeBlocked)return;
  pollCommands(s);
  pollPowerKey(millis());
  Snapshot event;
  while(acquisition.takeEvent(event)) {
    if(event.revision!=lastRevision) {
      lastRevision=event.revision;
      Serial.printf("LAUNCH_RESULT valid=%d peak_rpm=%.1f revs=%lu amplitude=%.1f start_noise=%.1f launches=%lu end=%s t_us=%llu\n",
        event.valid,event.resultRpm,(unsigned long)event.revolutions,event.amplitude,event.startNoise,
        (unsigned long)event.launches,event.endName(),(unsigned long long)event.detectorUs);
      if(controls.page==Controls::Page::WearCheck) {
        const auto stageBefore=controls.wear.stage();
        const auto outcome=controls.wear.observePull(event.launches,event.valid,event.resultRpm,level.reading(millis()));
        if(outcome!=WearCheck::Outcome::None) {
          inactivity.touch(millis());
          Serial.printf("WEAR_CHECK_PULL stage=%s pull=%u/%u outcome=%s rpm=%.1f tilt_deg=%.2f\n",
            WearCheck::stageName(stageBefore),
            stageBefore<WearCheck::Stage::Complete?controls.wear.stageCount(unsigned(stageBefore)):WearCheck::PULLS_PER_STAGE,
            WearCheck::PULLS_PER_STAGE,WearCheck::outcomeName(outcome),event.resultRpm,controls.wear.lastTiltDegrees());
          if(outcome==WearCheck::Outcome::Completed) {
            Serial.printf("WEAR_CHECK_RESULT rpm=%.1f median_rpm=%.1f level_rpm=%.1f pull_down_rpm=%.1f pull_up_rpm=%.1f pulls=%u tolerance_deg=%.0f baseline_rpm=%.1f\n",
              controls.wear.overallMean(),controls.wear.overallMedianMean(),
              controls.wear.stageMean(0),controls.wear.stageMean(1),controls.wear.stageMean(2),
              controls.wear.totalAccepted(),WearCheck::TOLERANCE_DEG,WearCheck::FRESH_BASELINE_RPM);
          }
        }
      } else if(practice.accept(event.launches,event.valid,event.resultRpm,millis())) {
        prepareMotion(event);logLaunchMetrics(event);noteStartMark(event);
        inactivity.touch(millis());
        Serial.printf("HISTORY_ADDED number=%lu session=%lu rpm=%.1f\n",(unsigned long)practice.recent()->number,
          (unsigned long)practice.recent()->session,practice.recent()->rpm);
      }
      dirty=true;
    }
    if(event.phase!=lastPhase || event.signalFault!=lastFault) {
      lastPhase=event.phase;lastFault=event.signalFault;dirty=true;
      Serial.printf("RPM_STATE state=%s t_us=%llu contrast=%.1f noise=%.1f\n",event.stateName(),
        (unsigned long long)event.detectorUs,event.contrast,event.noise);
    }
  }
  s=acquisition.snapshot();
  const uint32_t now=millis();
  if(s.phase==AnalogTachometer::Phase::Launch || motionDemo || tournamentMode || historyPage || diagnostics){
    if(bestEffect.badge()){bestEffect.clear();dirty=true;}else bestEffect.clear();
  }
  // A celebration runs about as long as the recap (5 s) and starts with its
  // first frame: the recap stays until the effect has ended and the readout
  // has shown clean for CELEBRATION_HOLD_MS.
  const bool celebrationHolds=bestEffect.badge() && bestEffect.started() &&
    bestEffect.elapsed(now)<Appearance::duration(bestEffect.effect())+CELEBRATION_HOLD_MS;
  if(!celebrationHolds && launchFeedback.tick(now)){dirty=true;motionDemo=false;Serial.printf("MAIN_FEEDBACK shown=0 reason=timeout t_ms=%lu\n",(unsigned long)now);}
  if(!launchFeedback.shown())bestEffect.clear();
  if(s.phase==AnalogTachometer::Phase::Launch)inactivity.touch(now);
  finishMotion();
  retryMotion(now,s);
  saveHistory(now,s);s=acquisition.snapshot();
  saveSettings(now,s);s=acquisition.snapshot();
  releaseOrphanCheckpoint(now,s);
  pollBattery(now,s);
  tickPowerLog(now,s);
  tickHistoryLog(now,s);
  pollUsbAwake(now,s);
  tickSound(s);
#if LAUNCHLAB_MOTION_ADC_GATE
  tickSensorIdle(millis(),s);
#endif
  if(restorePreview)previewSetting();
  powerKeyOff(now,s);
  autoPowerOff(now,s);
  tickDisplayPower(millis(),acquisition.snapshot());
  if(newSessionAt && now-newSessionAt>=1500){newSessionAt=0;dirty=true;}
  if(stress && now-lastDraw>=50)dirty=true;
  if((M5.BtnA.isPressed() || M5.BtnB.isPressed()) && now-lastDraw>=50)dirty=true;
  if(tournamentHintUntil && int32_t(now-tournamentHintUntil)>=0){tournamentHintUntil=0;dirty=true;}
  if(diagnostics && now-lastDraw>=500)dirty=true;
  if(historyPage && historyView==PracticeUI::View::Battery && now-lastDraw>=1000)dirty=true;
  const bool effectMoving=bestEffect.moving(now) || editorEffect.moving(now);
  if((effectMoving || effectWasMoving) && now-lastDraw>=50)dirty=true;
  // One final clean frame removes the decoration; the NEW BEST label stays.
  if(dirty && (stress || s.phase!=AnalogTachometer::Phase::Launch))effectWasMoving=effectMoving;
  if(!tournamentMode && !diagnostics && !historyPage && launchFeedback.shown() && !motionPending &&
     MotionReplay::playbackNeedsFrame(motionDemo?demoMotion:latestMotion,launchFeedback.replayElapsed(now),launchFeedback.lastReplayElapsed()) && now-lastDraw>=50)dirty=true;
  if(!tournamentMode && !diagnostics && !historyPage && !launchFeedback.shown() && now-lastDraw>=100) {
    const auto tilt=StickS3Level::forDisplay(level.reading(now),visibleDisplayFlipped());
    if(tilt.state!=drawnLevel.state || (tilt.valid() &&
      (std::fabs(tilt.degrees-drawnLevel.degrees)>=0.15f ||
       std::fabs(tilt.right-drawnLevel.right)>=0.02f || std::fabs(tilt.down-drawnLevel.down)>=0.02f)))dirty=true;
  }
  // Avoid electrical display changes through an ordinary measured burst.
  // Stress mode intentionally exercises the screen even during a burst.
  if(dirty && (stress || s.phase!=AnalogTachometer::Phase::Launch)) {
    draw(s);lastDraw=millis();dirty=false;
  }
  if(Serial && now-lastStatus>=2000){lastStatus=now;status();}
  // ADC and IMU owners retain their own clocks and priorities. Avoid waking
  // this UI task 1,000 times/second merely to inspect unchanged snapshots.
  clock.release();delay(IdlePowerPolicy::UI_POLL_MS);
}
