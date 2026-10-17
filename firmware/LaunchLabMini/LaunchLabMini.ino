#include <M5Unified.h>
#include <Preferences.h>
#include <memory>
#include "acquisition.h"
#include "level_indicator.h"
#include "practice_store.h"
#include "practice_ui.h"
#include "power_status.h"
#include "sensor_idle.h"
#include "cpu_power.h"
#ifndef LAUNCHLAB_MOTION_ADC_GATE
#define LAUNCHLAB_MOTION_ADC_GATE 0
#endif
#include "usb_awake.h"
#include <HWCDC.h>
#include "power_diagnostics.h"
#include <sys/time.h>
#include <esp32-hal-cpu.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <nvs.h>
#include "shake_wake.h"
#include "wake_ui_state.h"
#include "rpm_estimator_setting.h"
#include "display_flip_setting.h"
#include "display_orientation.h"
#include "sensor_profile_setting.h"
#include "mount_orientation_setting.h"
#include "controls_ui.h"
#include "motion_acquisition.h"
#include "i2c_bus_recovery.h"
#include "motion_store.h"
#include "motion_ui.h"
#include "launch_feedback.h"
#include "appearance_store.h"
#include "best_effect_ui.h"
#include "best_effect_speaker.h"
#include "device_backup.h"
#include "nvs_budget.h"
#include "device_wipe_nvs.h"
#include "wipe_confirmation.h"

// loop() runs on an 8 KB task. GCC inlines static helpers called once, and the
// frames of inlined bodies add up: 0.11.0's loop frame reached 6.9 KB and
// overflowed on the first status print. Keep heavy or rarely used helpers as
// real calls so each one uses stack only while it runs.
#define LOOP_HELPER __attribute__((noinline))
#if LAUNCHLAB_CPU_PM
static constexpr char VERSION[] = "0.13.8-sticks3-fifo-timing-pm";
#else
static constexpr char VERSION[] = "0.13.8-sticks3-fifo-timing";
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
static constexpr uint32_t HISTORY_SAVE_RETRY_MS=5000;
static uint32_t lastAcceptedAt=0,historyPendingSince=0;
static bool screenReady=false;
static bool diagnostics=false,extPower=false,stress=false,dirty=true;
static uint32_t lastDraw=0,lastStatus=0,lastRevision=0,maxDrawUs=0,maxEffectUs=0,uiStalls=0;
static uint32_t lastImuPoll=0,maxImuPollUs=0;
static StickS3Level::Reading drawnLevel;
static InactivityTimer inactivity;
static uint64_t lastActivityMotionUs=0;
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
static uint8_t brightnessPercent=40;
static bool displayFlipped=false;
static RpmEstimator::Mode rpmEstimator=RpmEstimator::DEFAULT_MODE;
static StickS3SensorProfile::Mode sensorProfile=StickS3SensorProfile::DEFAULT_MODE;
static MountOrientation::Mode mountMode=MountOrientation::Mode::Top;
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
// A celebration sound that cannot start within this long after its effect is dropped.
static constexpr uint32_t SOUND_LATE_MS=300;
static void requestSound(Appearance::Effect effect,Appearance::Sound level,bool celebration);
static Appearance::Effect celebrationEffect();static Appearance::Effect soundTarget();
static constexpr uint32_t MOTION_INIT_RETRY_MS=5000,MOTION_DATA_TIMEOUT_MS=250;
static uint32_t motionInitAttempts=0,lastMotionInit=0;
static bool motionInitPaired=false,motionInitSuccess=false;
static MotionStore motionStore;
static DeviceBackup::Receiver restoreRx;
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
  motionInitSuccess=motionInitPaired && imuAcquisition.setMount(mountMode) && imuAcquisition.begin();
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

LOOP_HELPER static void retryMotion(uint32_t now,const Snapshot &s) {
  if(imuAcquisition.running() || motionPending || uint32_t(now-lastMotionInit)<MOTION_INIT_RETRY_MS ||
     (s.phase!=AnalogTachometer::Phase::Ready && s.phase!=AnalogTachometer::Phase::Paused))return;
  startMotion();dirty=true;
}

static uint8_t backlightValue(){return (unsigned(brightnessPercent)*255+50)/100;}
// During a pull the preview can be deferred. Sensor coordinates must follow
// the LCD orientation that was actually applied, rather than an unseen draft.
static bool visibleDisplayFlipped(){return DisplayOrientation::flipForRotation(M5.Display.getRotation());}
static Appearance::Theme visibleTheme(){return controls.page==Controls::Page::Edit && controls.setting==5?Appearance::Theme(controls.draft):appearance.theme;}
static uint16_t themeAccent(){return Appearance::accent(visibleTheme());}
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
  if(ok)powerLogDirty=false;return ok;
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
    HistoryCheckpoint::practiceIdle(now,lastAcceptedAt,HISTORY_COALESCE_MS,motionPending,launchFeedback.shown(),s.phase)){
    // Record the attempt first: a vetoed checkpoint must not repeat the IMU
    // quiet wait on every UI tick. The dirty log stays pending.
    lastPowerSaveTry=now;
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
  Serial.printf("HEAP_STATUS internal_free=%u internal_largest=%u internal_min_free=%u psram_free=%u psram_largest=%u canvas=%s canvas_bytes=%u\n",
    unsigned(heap_caps_get_free_size(INTERNAL)),unsigned(heap_caps_get_largest_free_block(INTERNAL)),
    unsigned(heap_caps_get_minimum_free_size(INTERNAL)),unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
    unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
    !canvas?"none":esp_ptr_internal(canvas)?"internal":"psram",unsigned(screenReady?screen.bufferLength():0));
}

// NVS entries; a history save needs one rewrite's worth free, and a negative
// headroom means the next history save would fail for lack of space.
static bool nvsAvailable(unsigned &available){
  nvs_stats_t stats{};if(nvs_get_stats(nullptr,&stats)!=ESP_OK)return false;
  available=unsigned(stats.available_entries);return true;
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
  Serial.printf("RPM_STATUS state=%s valid=%d peak_rpm=%.1f live_peak=%.1f launches=%lu edges=%lu weak_rejected=%lu slow_rejected=%lu noise=%.1f amplitude=%.1f rearm_ms=%lu candidate_rpm=%.1f contrast=%.1f hysteresis=%.1f shape_rejected=%lu inconsistent=%lu rearm_extensions=%lu detector_us=%llu recorder_s=%lu min_mark=%.0f edge_fraction=%.2f\n",
    s.stateName(),s.valid,s.resultRpm,s.peakRpm,(unsigned long)s.launches,(unsigned long)s.edges,
    (unsigned long)s.weak,(unsigned long)s.slow,s.noise,s.amplitude,(unsigned long)s.rearmMs,
    s.candidateRpm,s.contrast,s.hysteresis,(unsigned long)s.shape,(unsigned long)s.inconsistent,
    (unsigned long)s.rearmExtensions,(unsigned long long)s.detectorUs,
    acquisition.storage()?60ul:0ul,StickS3SensorProfile::minMark(s.sensorProfile),StickS3SensorProfile::EDGE_SWING_FRACTION);
  Serial.printf("SENSOR_PROFILE_STATUS selected=%s min_mark=%.0f saved_setting=%s\n",
    StickS3SensorProfile::name(s.sensorProfile),StickS3SensorProfile::minMark(s.sensorProfile),StickS3SensorProfile::name(sensorProfile));
  Serial.printf("RPM_ESTIMATORS selected=%s units=rpm three_turn_peak_rpm=%.1f single_turn_peak_rpm=%.1f confirmation_turns=3 max_rpm=30000 single_updates=%lu single_rejected=%lu\n",
    RpmEstimator::name(s.estimator),s.sustainedPeakRpm,s.singlePeakRpm,
    (unsigned long)s.singlePeakUpdates,(unsigned long)s.singlePeakRejected);
  Serial.printf("TOURNAMENT_STATUS enabled=%d display=%s persistent=wake\n",tournamentMode,tournamentViewName());
  soundPlayer.status(appearance.sound);
  Serial.printf("UI_STATUS page=%u menu=%u history=%u setting=%u draft=%u recording=%s\n",
    unsigned(controls.page),controls.menu,controls.history,controls.setting,controls.draft,
    s.resting?"idle_paused":LAUNCHLAB_MOTION_ADC_GATE?"motion_gated":"always");
  Serial.printf("PERF acquisition_core=0 ui_core=%d processing_pct=%.2f max_batch_us=%lu frame_budget_us=5120 max_read_gap_us=%lu stack_free=%lu invalid_frames=%lu lost_events=%lu max_draw_us=%lu max_effect_us=%lu ui_stalls=%lu stress=%d heap_free=%lu psram_free=%lu\n",
    xPortGetCoreID(),s.processingPercent,(unsigned long)s.maxBatchUs,(unsigned long)s.maxReadGapUs,
    (unsigned long)s.stackFree,(unsigned long)s.invalidFrames,(unsigned long)s.lostEvents,
    (unsigned long)maxDrawUs,(unsigned long)maxEffectUs,(unsigned long)uiStalls,stress,
    (unsigned long)ESP.getFreeHeap(),(unsigned long)ESP.getFreePsram());
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
    (unsigned long)practice.generation(),practiceStore.pending(practice),practiceStore.ready(),
    (unsigned long)saveErrors,(unsigned long)saveCount,(unsigned long)maxSaveUs,
    diagnostics?"signal":historyPage?"history":"live",unsigned(historyView),historyOffset,demo,
    (unsigned long)uxTaskGetStackHighWaterMark(nullptr));
  const bool pendingHistory=practiceStore.pending(practice);
  const uint32_t pendingMs=pendingHistory && historyPendingSince?now-historyPendingSince:0;
  const uint32_t quietMs=now-lastAcceptedAt;
  const uint32_t flushDue=quietMs>=HISTORY_COALESCE_MS?0u:HISTORY_COALESCE_MS-quietMs;
  const uint32_t retryRemaining=saveError && now-lastSaveTry<HISTORY_SAVE_RETRY_MS?HISTORY_SAVE_RETRY_MS-(now-lastSaveTry):0;
  Serial.printf("HISTORY_PENDING unsaved_pulls=%lu pending_ms=%lu flush_due_ms=%lu coalesce_ms=%lu max_pending_ms=none retry_remaining_ms=%lu\n",
    (unsigned long)practiceStore.pendingCount(practice),(unsigned long)pendingMs,
    (unsigned long)(pendingHistory?flushDue:0),(unsigned long)HISTORY_COALESCE_MS,(unsigned long)retryRemaining);
  Serial.printf("POWER_STATUS auto_off_s=%lu wake=shake sleep=imu_only sensor_rail=L2_off_in_battery_standby idle_s=%lu off_in_s=%lu battery_valid=%d battery_mv=%u battery_pct=%d charging=%s max_battery_poll_us=%lu auto_off_inhibited=%d\n",
    (unsigned long)(inactivity.timeout()/1000),(unsigned long)(inactivity.elapsed(now)/1000),(unsigned long)((inactivity.remaining(now)+999)/1000),
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
  Serial.printf("IMU_STARTUP_STATUS attempts=%lu last_try_ms=%lu last_paired=%d last_success=%d retry_ms=%lu\n",
    (unsigned long)motionInitAttempts,(unsigned long)lastMotionInit,motionInitPaired,motionInitSuccess,
    (unsigned long)MOTION_INIT_RETRY_MS);
  Serial.printf("MOTION_STATUS imu_core=0 imu_task=%d paired_samples=%lu accel_samples=%lu max_gap_us=%lu gaps=%lu max_poll_us=%lu stack_free=%lu gate_misses=%lu latest=%lu quality=%s pending=%d ref=%lu ref_rpm=%.1f ref_store_ready=%d ref_load_error=%d dropped=%lu demo=%d feedback=%d\n",
    imuAcquisition.running(),(unsigned long)imu.pairedSamples,(unsigned long)imu.accelSamples,
    (unsigned long)imu.maxGapUs,(unsigned long)imu.gaps,(unsigned long)imu.maxPollUs,(unsigned long)imu.stackFree,(unsigned long)imu.gateMisses,
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
  const int cx=g.width()/2;
  g.setTextDatum(top_center);g.setTextColor(TFT_WHITE,TFT_BLACK);
  g.drawString("Battery",cx,8,2);
  char line[32];
  const bool valid=battery.valid(now);
  if(valid)snprintf(line,sizeof(line),"%d%%",battery.percent);else snprintf(line,sizeof(line),"--");
  g.setTextColor(valid && battery.percent<=20?TFT_ORANGE:themeAccent(),TFT_BLACK);
  g.drawString(line,cx,43,4);
  g.drawRoundRect(29,96,74,25,3,TFT_DARKGREY);g.fillRect(103,103,4,11,TFT_DARKGREY);
  if(valid)g.fillRect(33,100,66*battery.percent/100,17,battery.percent<=20?TFT_ORANGE:themeAccent());
  g.setTextColor(TFT_WHITE,TFT_BLACK);
  if(valid)snprintf(line,sizeof(line),"%.2f V",battery.millivolts/1000.0);else snprintf(line,sizeof(line),"Reading unavailable");
  g.drawString(line,cx,133,2);
  g.drawString(!battery.chargeKnown?"Charge status unknown":battery.charging?"Charging":"Not charging",cx,161,1);
  g.setTextColor(TFT_DARKGREY,TFT_BLACK);
  ControlsUI::footer(g,"A Done");
}

static void drawSettings(lgfx::LGFXBase &g) {
  const uint8_t values[]={inactivity.minutes(),uint8_t(rpmEstimator),brightnessPercent,uint8_t(displayFlipped),uint8_t(sensorProfile),uint8_t(appearance.theme),uint8_t(appearance.effect),uint8_t(appearance.sound),uint8_t(mountMode)};
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
    const int cy=165,radius=38;
    view.setTextColor(TFT_DARKGREY,TFT_BLACK);
    view.drawCircle(cx,cy,radius,TFT_DARKGREY);
    view.drawFastHLine(cx-radius+3,cy,2*radius-5,TFT_DARKGREY);
    view.drawFastVLine(cx,cy-radius+3,2*radius-5,TFT_DARKGREY);
    view.drawCircle(cx,cy,6,TFT_DARKGREY);
    drawnLevel=StickS3Level::forDisplay(level.reading(millis()),visibleDisplayFlipped());
    if(drawnLevel.valid()) {
      const auto color=drawnLevel.degrees<=2?themeAccent():TFT_ORANGE;
      view.fillCircle(cx+lroundf(drawnLevel.right*32),cy+lroundf(drawnLevel.down*32),5,color);
      char angle[20];snprintf(angle,sizeof(angle),"%.1f deg",drawnLevel.degrees);
      view.setTextColor(color,TFT_BLACK);view.drawString(angle,cx,207,2);
    } else {
      view.drawString("--",cx,207,2);
    }
}

LOOP_HELPER static void draw(const Snapshot &s) {
  const int64_t started=esp_timer_get_time();
  ControlsUI::accent=themeAccent();
  const uint32_t replayFrameElapsed=launchFeedback.replayElapsed(millis());
  const bool replayWasStarted=launchFeedback.replayStarted();
  auto &view=screenReady?static_cast<lgfx::LGFXBase &>(screen):static_cast<lgfx::LGFXBase &>(M5.Display);
  if(!screenReady){acquisition.displayBoundary(true);M5.Display.startWrite();}
  view.fillScreen(TFT_BLACK);
  const int cx=view.width()/2;
  view.setTextDatum(top_center);
  view.setTextColor(TFT_WHITE,TFT_BLACK);
  if(!tournamentMode && diagnostics) {
    if(newSessionAt && millis()-newSessionAt<1500)view.drawString("New session ready",cx,8,2);
    view.setTextColor(s.ready && !s.signalFault?TFT_GREEN:TFT_ORANGE,TFT_BLACK);
    view.drawString(s.stateName(),cx,32,2);
  }
  view.setTextColor(TFT_WHITE,TFT_BLACK);
  if(tournamentMode) {
    if(!s.ready || s.signalFault) {
      view.setTextColor(TFT_ORANGE,TFT_BLACK);
      view.drawString("Check sensor",cx,92,2);
    } else if(tournamentView==TournamentView::RecordingOnly) {
      // A static card keeps launch results out of the competitor's view.
      view.setTextColor(TFT_RED,TFT_BLACK);
      view.drawString("RECORDING",cx,84,2);
      view.drawString("ONLY",cx,108,4);
    } else {
      char rpm[16];
      if(s.valid)snprintf(rpm,sizeof(rpm),"%.0f",s.resultRpm);
      else snprintf(rpm,sizeof(rpm),"--");
      const int rpmFont=view.textWidth(rpm,&fonts::Font6)<=view.width()-8?6:4;
      view.drawString(rpm,cx,86,rpmFont);
      view.drawString("RPM",cx,137,2);
    }
    if(int32_t(tournamentHintUntil-millis())>0)ControlsUI::footer(view,"B View  Hold A Exit");
  } else if(diagnostics) {
    view.setTextDatum(top_left);
    char line[64];
    snprintf(line,sizeof(line),"AO G%u: %lu",s.pin,(unsigned long)s.last);
    view.drawString(line,8,64,2);
    snprintf(line,sizeof(line),"Range %lu-%lu",(unsigned long)s.minimum,(unsigned long)s.maximum);
    view.drawString(line,8,93,2);
    snprintf(line,sizeof(line),"%.0f samples/s",s.rate);
    view.drawString(line,8,122,2);
  } else if(historyPage) {
    view.fillScreen(TFT_BLACK);
    switch(controls.page) {
      case Controls::Page::Menu:ControlsUI::list(view,"Menu",ControlsUI::MENU,6,controls.menu);break;
      case Controls::Page::HistoryMenu:ControlsUI::list(view,"History",ControlsUI::HISTORY,4,controls.history);break;
      case Controls::Page::Settings:drawSettings(view);break;
      case Controls::Page::Edit:ControlsUI::editor(view,controls,settingsSaveError,
        (controls.setting==1 && controls.draft!=uint8_t(rpmEstimator)) ||
        (controls.setting==4 && controls.draft!=uint8_t(sensorProfile)));
        // The previewed effect frames the value text inside its card.
        if(screenReady && controls.setting==6 && editorEffect.badge()) {
          const int64_t effectStarted=esp_timer_get_time();BestEffectUI::Renderer::Anchor card;
          card.top=72;card.bottom=128;card.left=8;card.right=view.width()-9;
          bestEffectRenderer.draw({static_cast<uint16_t *>(screen.getBuffer()),screen.width(),screen.height()},
            editorEffect.effect(),editorEffect.elapsed(millis()),themeAccent(),card);
          maxEffectUs=std::max(maxEffectUs,uint32_t(esp_timer_get_time()-effectStarted));
        }
        break;
      case Controls::Page::NewSession:ControlsUI::session(view,controls.startSession);break;
      case Controls::Page::Battery:drawBattery(view,millis());break;
      case Controls::Page::Wipe:ControlsUI::wipe(view,wipeConfirmation,millis(),wipeFailed,wipeBlocked,wipeComplete);break;
      default:PracticeUI::draw(view,demo?demoPractice:practice,historyView,historyOffset,demo,themeAccent());break;
    }
  } else if(launchFeedback.shown()) {
    view.fillScreen(TFT_BLACK);
    MotionUI::feedback(view,motionDemo?demoMotion:latestMotion,
      motionPending && !motionDemo,motionDemo,MotionReplay::playbackProgress(motionDemo?demoMotion:latestMotion,replayFrameElapsed),visibleDisplayFlipped(),themeAccent(),!motionDemo && bestEffect.badge());
    ControlsUI::footer(view,"A Main  B Menu");
    // Direct-draw fallback stays functional without reading the hardware LCD.
    if(screenReady && !motionDemo && !motionPending && bestEffect.badge()) {
      const int64_t effectStarted=esp_timer_get_time();
      bestEffectRenderer.draw({static_cast<uint16_t *>(screen.getBuffer()),screen.width(),screen.height()},
        bestEffect.effect(),bestEffect.elapsed(millis()),themeAccent());
      maxEffectUs=std::max(maxEffectUs,uint32_t(esp_timer_get_time()-effectStarted));
    }
  } else {
    const bool sessionNotice=newSessionAt && millis()-newSessionAt<1500 &&
      s.ready && !s.signalFault && !s.suspended && s.phase==AnalogTachometer::Phase::Ready;
    const char *status=sessionNotice?"New session ready":s.stateName();
    view.setTextColor(sessionNotice?TFT_WHITE:s.ready && !s.signalFault?TFT_GREEN:TFT_ORANGE,TFT_BLACK);
    view.drawString(status,cx,8,2);
    char rpm[16];
    if(s.valid)snprintf(rpm,sizeof(rpm),"%.0f",s.resultRpm);
    else snprintf(rpm,sizeof(rpm),"--");
    view.setTextDatum(top_center);
    // Keep large values inside the narrow portrait width as well.
    const int rpmFont=view.textWidth(rpm,&fonts::Font6)<=view.width()-8?6:4;
    view.setTextColor(visibleTheme()==Appearance::Theme::Classic?TFT_WHITE:themeAccent(),TFT_BLACK);
    view.drawString(rpm,cx,47,rpmFont);
    view.drawString("RPM",cx,99,2);
    drawLiveLevel(view,cx);
  }
  if(!tournamentMode && (diagnostics || (!historyPage && !launchFeedback.shown()))) {
    view.setTextDatum(top_center);view.setTextColor(TFT_DARKGREY,TFT_BLACK);
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
  acquisition.displayBoundary(false);
  if(!tournamentMode && !diagnostics && !historyPage && launchFeedback.shown() && !motionDemo && !motionPending)
    bestEffect.presented(millis());
  // The sound starts with the effect's first presented frame.
  if(controls.page==Controls::Page::Edit && controls.setting==6)editorEffect.presented(millis());
  if(bestEffect.takeCue()) {
    requestSound(bestEffect.effect(),appearance.sound,true);
    // Keep this pick's clip ready until its first frame has started playback.
    // Only a presented celebration consumes Random's next pick.
    if(appearance.effect==Appearance::Effect::Random)upcomingEffect=Appearance::pick(esp_random(),bestEffect.effect());
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
    if(sample.fusedUs && sample.fusedUs!=lastLevelSampleUs) {
      lastLevelSampleUs=sample.fusedUs;
      if(sample.orientationReady)level.feed(sample.gravity[0],sample.gravity[1],sample.gravity[2],uint32_t(sample.fusedUs/1000));
      else level.feed(0,0,0,uint32_t(sample.fusedUs/1000));
    }
    return;
  }
  if(!M5.Imu.isEnabled())return;
  const int64_t started=esp_timer_get_time();
  // update's returned accel bit establishes freshness; cached data is not a new sample.
  if(M5.Imu.update() & m5::IMU_Class::sensor_mask_accel) {
    const auto data=M5.Imu.getImuData();
    const auto &range=accelRange();
    const float native[3]={range.toG(data.accel.x),range.toG(data.accel.y),range.toG(data.accel.z)};float mapped[3];
    MountOrientation::mapVector(mountMode,native,mapped);
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
  const bool celebrate=beat && !tournamentMode && !historyPage && !diagnostics;
  bestEffect.queue(celebrate,latestMotion.number,celebrate?celebrationEffect():Appearance::Effect::Off);
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
  imuAcquisition.capture(motionStart,motionEnd,latestMotion.rpm,latestMotion.number,latestMotion);
  motionPending=false;dirty=true;
  const auto state=acquisition.snapshot();
  if(state.phase==AnalogTachometer::Phase::Launch || !launchFeedback.shown() || tournamentMode || historyPage || diagnostics || motionDemo)bestEffect.clear();
  else bestEffect.captured(latestMotion.number);
  if(launchFeedback.shown())launchFeedback.show(millis());
  Serial.printf("MAIN_FEEDBACK shown=%d reason=result t_ms=%lu\n",launchFeedback.shown(),(unsigned long)millis());
  Serial.printf("MOTION_RESULT launch=%lu quality=%s points=%u duration_ms=%lu rpm=%.1f stationary_bias=%d saturated=%d peak_accel_g=%.2f accel_range_g=%.0f peak_gyro_dps=%.0f gyro_range_dps=%.0f\n",
    (unsigned long)latestMotion.number,LaunchMotion::name(latestMotion.quality),latestMotion.count,
    (unsigned long)latestMotion.durationMs,latestMotion.rpm,latestMotion.stationaryBias,latestMotion.saturated,
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
  if(!Appearance::drawn(unsigned(upcomingEffect)))upcomingEffect=Appearance::pick(esp_random(),Appearance::Effect::Off);
  return upcomingEffect;
}
LOOP_HELPER static Appearance::Effect celebrationEffect() {
  if(appearance.effect!=Appearance::Effect::Random)return appearance.effect;
  return upcoming();
}
// Keep a queued recap's clip intact until its first presented frame takes the
// sound cue, including USB previews of an effect other than the saved one.
LOOP_HELPER static Appearance::Effect soundTarget(){
  if(bestEffect.badge() && !bestEffect.started())return bestEffect.effect();
  return appearance.effect==Appearance::Effect::Random?upcoming():appearance.effect;
}
// Plays now if the sound is rendered, otherwise once the idle slices finish it.
LOOP_HELPER static void requestSound(Appearance::Effect effect,Appearance::Sound level,bool celebration) {
  soundRequest={};
  if(level==Appearance::Sound::Off){soundPlayer.stop();celebrationSound=soundPreview=false;return;}
  soundRequest.active=true;soundRequest.celebration=celebration;soundRequest.effect=effect;soundRequest.level=level;
  if(!soundPlayer.prepared(effect))return;
  const bool started=soundPlayer.play(effect,level);
  (celebration?celebrationSound:soundPreview)=started;soundRequest.active=false;
}
// Each B in the Sound editor plays the current effect at the draft level.
LOOP_HELPER static void previewSound(){requestSound(soundTarget(),Appearance::Sound(controls.draft),false);}
// Each B in the Best effect editor, and opening it, plays the draft around the
// value card (with its sound when Sound is on); Random shows a random pick.
LOOP_HELPER static void previewEffect() {
  static Appearance::Effect lastShown=Appearance::Effect::Off;
  const auto choice=Appearance::Effect(controls.draft);
  editorEffect.clear();dirty=true;
  if(!Appearance::drawn(unsigned(choice)) && choice!=Appearance::Effect::Random){soundPlayer.stop();soundPreview=false;soundRequest={};return;}
  lastShown=choice==Appearance::Effect::Random?Appearance::pick(esp_random(),lastShown):choice;
  editorEffect.queue(true,1,lastShown);editorEffect.captured(1);
}
static void cancelSettingEdit() {
  if(controls.page==Controls::Page::Wipe)cancelWipe();
  if(controls.page==Controls::Page::Edit)controls.page=Controls::Page::Settings;
  restorePreview=true;settingsSaveError=false;previewSetting();
}
static void commitSetting() {
  const auto s=acquisition.snapshot();
  if(motionPending || !acquisition.request(Action::IdleCheckpointBegin,s.launches)) {
    settingsSaveError=true;dirty=true;return;
  }
  const uint8_t value=controls.draft;
  bool saved=false;
  switch(Controls::Setting(controls.setting)) {
    case Controls::Setting::Sleep:
      saved=prefs.putUChar("sleep_min",value)==1;
      if(saved)inactivity.setMinutes(value);
      break;
    case Controls::Setting::Rpm: {
      const auto next=RpmEstimator::Mode(value);
      saved=next==rpmEstimator || RpmEstimatorSetting::save(prefs,next);
      if(saved && next!=rpmEstimator && !acquisition.request(Action::Estimator,value)) {
        saved=false;
        if(!RpmEstimatorSetting::save(prefs,rpmEstimator))Serial.println("RPM_SETTING_ERROR preference_rollback_failed");
      }
      if(saved && next!=rpmEstimator){rpmEstimator=next;practice.newSession();launchFeedback.clear();}
      break;
    }
    case Controls::Setting::Brightness:
      saved=prefs.putUChar("bright_pct",value)==1;
      if(saved)brightnessPercent=value;
      break;
    case Controls::Setting::Flip:
      saved=DisplayFlipSetting::save(prefs,bool(value));
      if(saved)displayFlipped=value;
      break;
    case Controls::Setting::Sensor: {
      if(!StickS3SensorProfile::valid(value))break;
      const auto next=StickS3SensorProfile::Mode(value);
      saved=next==sensorProfile || SensorProfileSetting::save(prefs,next);
      if(next!=sensorProfile && (!saved || !acquisition.request(Action::SensorProfile,value))) {
        saved=false;
        if(!SensorProfileSetting::save(prefs,sensorProfile))Serial.println("SENSOR_SETTING_ERROR preference_rollback_failed");
      }
      if(saved && next!=sensorProfile){sensorProfile=next;practice.newSession();launchFeedback.clear();}
      break;
    }
    case Controls::Setting::Mount: {
      if(!MountOrientation::valid(value))break;
      const auto next=MountOrientation::Mode(value);
      saved=next==mountMode || MountOrientationSetting::save(prefs,next);
      if(next!=mountMode && (!saved || !imuAcquisition.setMount(next))) {
        saved=false;
        if(!MountOrientationSetting::save(prefs,mountMode))Serial.println("MOUNT_SETTING_ERROR preference_rollback_failed");
      }
      if(saved && next!=mountMode) {
        mountMode=next;level=StickS3Level{};drawnLevel={};lastLevelSampleUs=0;
        launchFeedback.clear();
      }
      break;
    }
    case Controls::Setting::Theme:case Controls::Setting::BestEffect:case Controls::Setting::Sound: {
      auto next=appearance;
      if(!next.select(controls.setting==5?Appearance::Theme(value):appearance.theme,
                      controls.setting==6?Appearance::Effect(value):appearance.effect,
                      controls.setting==7?Appearance::Sound(value):appearance.sound))break;
      saved=appearanceStore.save(next);if(saved)appearance=next;
      break;
    }
    case Controls::Setting::WipeAll:break; // Erasure never uses Apply or draft values.
  }
  settingsSaveError=!saved;
  if(saved){controls.page=Controls::Page::Settings;restorePreview=true;previewSetting();}
  if(!acquisition.request(Action::CheckpointEnd))Serial.println("SETTING_ERROR acquisition_resume");
  Serial.printf("SETTING_APPLY row=%u value=%u saved=%d\n",controls.setting,value,saved);
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
      controls.draft=controls.setting==0?inactivity.minutes():controls.setting==1?uint8_t(rpmEstimator):controls.setting==2?brightnessPercent:
        controls.setting==3?uint8_t(displayFlipped):controls.setting==4?uint8_t(sensorProfile):
        controls.setting==5?uint8_t(appearance.theme):controls.setting==6?uint8_t(appearance.effect):
        controls.setting==7?uint8_t(appearance.sound):uint8_t(mountMode);
      if(controls.setting==6)previewEffect();
      break;
    case Controls::Effect::Preview:settingsSaveError=false;previewSetting();
      if(controls.setting==6)previewEffect();
      if(controls.setting==7)previewSound();
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
  Serial.println("WIPE_RESTART");Serial.flush();delay(150);ESP.restart();
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
LOOP_HELPER static void saveHistory(uint32_t now,const Snapshot &s) {
  const bool history=practiceStore.pending(practice) && practiceStore.ready();
  const bool best=bestStore.pending(personalBests) && bestStore.ready();
  // Failed writes must leave enough uninterrupted quiet for detector rearm.
  const uint32_t retryMs=saveError?HISTORY_SAVE_RETRY_MS:1000;
  if((!history && !best) || now-lastSaveTry<retryMs ||
     !HistoryCheckpoint::practiceIdle(now,lastAcceptedAt,HISTORY_COALESCE_MS,motionPending,launchFeedback.shown(),s.phase))return;
  // Coalesce a rapid practice bout instead of resetting fusion after each pull.
  // Pending RAM history is flushed once the newest accepted pull has been idle.
  // No maximum-age override: a long practice bout stays in RAM until quiet.
  // Automatic sleep still forces a verified flush before entering standby.
  lastSaveTry=now;
  if(!acquisition.request(Action::HistoryIdleCheckpointBegin,s.launches))return;
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
  while(acquisition.takeEvent(event))if(practice.accept(event.launches,event.valid,event.resultRpm,now)){prepareMotion(event);logLaunchMetrics(event);newPull=true;}
  if(newPull) {
    inactivity.touch(now);dirty=true;
    if(!acquisition.request(Action::CheckpointEnd))acquisition.request(Action::Pin,s.pin);
    return ShutdownFlush::NewPull;
  }
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

LOOP_HELPER static void autoPowerOff(uint32_t now,const Snapshot &s) {
  if(wipeBlocked || controls.page==Controls::Page::Wipe)return;
  if(usbAwake.inhibited(now))return;
  if(motionPending || !inactivity.due(now,s.phase==AnalogTachometer::Phase::Launch) ||
     (lastShutdownTry && now-lastShutdownTry<60000))return;
  // Recheck immediately at the timer boundary; periodic polling alone could
  // leave a just-attached computer unnoticed for up to 250ms.
  pollUsbAwake(now,s,true);
  if(usbAwake.inhibited(now) || !inactivity.due(now,s.phase==AnalogTachometer::Phase::Launch))return;
  lastShutdownTry=now;
  // A failed save retries after 60 s; a new pull restarts the full timeout.
  const auto flushed=flushForShutdown(now,s);
  if(flushed==ShutdownFlush::NewPull)lastShutdownTry=0;
  if(flushed!=ShutdownFlush::Ready)return;
  // A host may attach while the forced history flush is blocking.
  pollUsbAwake(millis(),acquisition.snapshot(),true);
  if(usbAwake.inhibited(millis())) {
    if(!acquisition.request(Action::CheckpointEnd))acquisition.request(Action::Pin,s.pin);
    return;
  }
  Serial.printf("POWER_OFF reason=inactivity idle_ms=%lu records=%u saved=1\n",
    (unsigned long)inactivity.elapsed(now),practice.size());
  cancelSettingEdit(); // Wake into the settings list, never an unfinished edit.
  Serial.flush();
  if(imuAcquisition.lock()) {
    bool usbCancelled=false;
    if(shakeWake.arm()) {
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
        Serial.printf("SHAKE_SLEEP timeout_s=%lu sensor_rail=L2_off_on_battery wake=imu_motion mode=imu_only\n",(unsigned long)(inactivity.timeout()/1000));Serial.flush();
        // Flash/USB flushing above can outlast the earlier host check. This
        // last O(1) SOF read is safe while holding the IMU mutex; do not call
        // pollUsbAwake here because it can attempt that mutex again.
        if(HWCDC::isPlugged()) {
          usbCancelled=true;usbAwake.sampleHost(millis(),true);usbAutoOffInhibited=true;
          inactivity.touch(millis());lastShutdownTry=0;
          --powerLog.sleeps;powerLog.next=previousPowerNext;powerLog.count=previousPowerCount;
          powerLog.records[previousPowerNext]=previousPowerRecord;powerLogDirty=true;
          Serial.println("POWER_OFF_DEFERRED usb_host_attached_before_sleep");
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
    const bool extRestored=pm1Idle().run([](){M5.Power.setExtOutput(extPower);return true;});
    imuAcquisition.unlock();
    if(!extRestored)Serial.println("EXT_POWER_NOTICE restore_wake_failed");
    Serial.printf("POWER_OFF_DEFERRED reason=%s restored=%d retry_s=%u\n",usbCancelled?"usb_host_attached":"shake_sleep_failed",restored,usbCancelled?0:60);
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
  M5.Display.setBrightness(0);M5.Display.sleep();Serial.flush();
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
LOOP_HELPER static void exportBackup() {
  auto contents=std::unique_ptr<DeviceBackup::Contents>(new(std::nothrow) DeviceBackup::Contents);
  auto buffer=std::unique_ptr<DeviceBackup::Buffer>(new(std::nothrow) DeviceBackup::Buffer);
  if(!contents || !buffer){Serial.println("BACKUP_ERROR out_of_memory");return;}
  contents->practice=practice;contents->look=appearance;contents->bests=personalBests;
  contents->hasMotion=referenceMotion.valid();if(contents->hasMotion)contents->motion=referenceMotion;
  contents->settings={inactivity.minutes(),brightnessPercent,rpmEstimator,sensorProfile,displayFlipped,mountMode};
  const size_t n=DeviceBackup::encode(*contents,VERSION,*buffer);
  if(!n){Serial.println("BACKUP_ERROR out_of_memory");return;}
  Serial.printf("BACKUP_BEGIN format=%lu bytes=%u records=%u sessions=%u motion=%d runtime=%s\n",
    (unsigned long)DeviceBackup::FORMAT,unsigned(n),practice.size(),practice.sessions(),contents->hasMotion,VERSION);
  DeviceBackup::base64Lines(buffer->data(),n,96,[](const char *line){Serial.print("BACKUP_DATA ");Serial.println(line);});
  Serial.println("BACKUP_END");
}
// Validate the whole backup first, write every record inside one ADC-owned
// idle checkpoint with readback, then restart so each store loads normally.
// Restored records supersede whatever generation the device holds.
LOOP_HELPER static void finishRestore(const Snapshot &s) {
  auto contents=std::unique_ptr<DeviceBackup::Contents>(new(std::nothrow) DeviceBackup::Contents);
  auto buffer=std::unique_ptr<DeviceBackup::Buffer>(new(std::nothrow) DeviceBackup::Buffer);
  if(!contents || !buffer){Serial.println("RESTORE_ERROR reason=out_of_memory");return;}
  size_t n=0;
  if(!restoreRx.take(buffer->data(),buffer->size(),n) || !DeviceBackup::decode(buffer->data(),n,*contents)) {
    Serial.println("RESTORE_ERROR reason=invalid_backup written=0");return;
  }
  Serial.printf("RESTORE_VALID records=%u sessions=%u motion=%d from=%s\n",contents->practice.size(),
    contents->practice.sessions(),contents->hasMotion,contents->runtime);
  if(motionPending || s.phase==AnalogTachometer::Phase::Launch || !acquisition.request(Action::IdleCheckpointBegin,s.launches)) {
    Serial.println("RESTORE_ERROR reason=busy written=0");return;
  }
  contents->practice.supersede(practice.generation());
  contents->look.revision=appearance.revision+1;contents->bests.revision=personalBests.revision+1;
  const auto &settings=contents->settings;
  const bool saved=practiceStore.save(contents->practice) && appearanceStore.save(contents->look) &&
    bestStore.save(contents->bests) && (!contents->hasMotion || motionStore.save(contents->motion)) &&
    prefs.putUChar("sleep_min",settings.sleepMinutes)==1 && prefs.putUChar("bright_pct",settings.brightness)==1 &&
    RpmEstimatorSetting::save(prefs,settings.rpmMethod) && SensorProfileSetting::save(prefs,settings.sensorProfile) &&
    DisplayFlipSetting::save(prefs,settings.flipped) && MountOrientationSetting::save(prefs,settings.mount);
  if(!saved) {
    if(!acquisition.request(Action::CheckpointEnd))Serial.println("RESTORE_ERROR ADC checkpoint resume failed");
    // Earlier records may already hold the restored values; restoring again is safe.
    Serial.println("RESTORE_ERROR reason=write_failed written=partial");dirty=true;return;
  }
  heapStatus();
  Serial.println("RESTORE_OK restarting=1");Serial.flush();
  delay(200);ESP.restart();
}
LOOP_HELPER static void pollRestore(const Snapshot &s) {
  const uint32_t now=millis();
  if(restoreRx.timedOut(now)){restoreRx.cancel();Serial.println("RESTORE_ERROR reason=timeout written=0");return;}
  // A transfer owns the USB input until it ends; bounded per loop like commands.
  for(unsigned n=0;n<256 && restoreRx.active() && Serial.available();++n) {
    inactivity.touch(now);
    switch(restoreRx.feed(Serial.read(),now)) {
      case DeviceBackup::Receiver::Event::Chunk:Serial.printf("RESTORE_CHUNK chars=%u\n",restoreRx.received());break;
      case DeviceBackup::Receiver::Event::Complete:finishRestore(s);break;
      case DeviceBackup::Receiver::Event::Error:Serial.printf("RESTORE_ERROR reason=%s written=0\n",restoreRx.error());break;
      case DeviceBackup::Receiver::Event::None:break;
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

void setup() {
  const auto wakeCause=esp_sleep_get_wakeup_cause();
  const int64_t sleepEnded=wallUs();
  Serial.begin(115200);
  Serial.setTxTimeoutMs(10);
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
  powerLogReady=powerPrefs.begin("ll-power",false);
  PowerDiagnostics::fresh(powerLog);
  if(powerLogReady && powerPrefs.isKey("log")){
    PowerDiagnostics::Image loaded;
    if(powerPrefs.getBytesLength("log")==sizeof(loaded) && powerPrefs.getBytes("log",&loaded,sizeof(loaded))==sizeof(loaded)
      && PowerDiagnostics::valid(loaded))powerLog=loaded;
    else {powerLogReady=false;Serial.println("POWER_LOG_NOTICE invalid_existing_log_retained");}
  }
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
  if(!RpmEstimatorSetting::load(prefs,rpmEstimator))Serial.println("RPM_SETTING_NOTICE invalid_record_retained_using_three_turn_default");
  if(!SensorProfileSetting::load(prefs,sensorProfile))Serial.println("SENSOR_SETTING_NOTICE invalid_record_retained_using_standard_default");
  const uint8_t sleepMinutes=prefs.getUChar("sleep_min",InactivityTimer::DEFAULT_MINUTES);
  if(!inactivity.setMinutes(sleepMinutes))Serial.println("SLEEP_SETTING_NOTICE invalid_saved_value_using_3_min");
  const uint8_t savedBrightness=prefs.getUChar("bright_pct",40);
  if(savedBrightness>=10 && savedBrightness<=100 && savedBrightness%10==0)brightnessPercent=savedBrightness;
  else Serial.println("BRIGHTNESS_SETTING_NOTICE invalid_saved_value_using_40_pct");
  if(!DisplayFlipSetting::load(prefs,displayFlipped))Serial.println("DISPLAY_SETTING_NOTICE invalid_record_retained_using_flip_off_default");
  if(!MountOrientationSetting::load(prefs,mountMode))Serial.println("MOUNT_SETTING_NOTICE invalid_record_retained_using_top_default");
  if(!appearanceStore.begin(appearance,"ll-appearance","cfg0","cfg1"))Serial.println("APPEARANCE_NOTICE invalid_record_retained_using_defaults");
  if(!bestStore.begin(personalBests,"ll-bests","best0","best1"))Serial.println("BEST_NOTICE invalid_record_retained_celebration_disabled");
  uint8_t pin=prefs.getUChar("ao_pin",1);
  if(pin<1 || pin>10)pin=1;
  extPower=prefs.getBool("ext5v",false);
  saveError=!practiceStore.begin(practice);
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
  if(!acquisition.begin(pin,rpmEstimator,sensorProfile))Serial.println("ADC_ERROR acquisition_task_allocation");
  pollBattery(millis(),acquisition.snapshot());
  powerRecord(wakingFromStandby?PowerDiagnostics::Wake:PowerDiagnostics::Boot,loggedWake);
  powerLogTick=lastPowerSample=millis();
  Serial.printf("POWER_WAKE esp_cause=%d pm1_flags_valid=%d pm1_flags=0x%02x pending_standby=%d\n",int(wakeCause),pm1WakeValid,pm1WakeFlags,wakingFromStandby);
  // Use hardware wake evidence; the optional diagnostic log can fail to save
  // and must not determine the battery policy.
  inactivity.begin(millis(),(pm1WakeValid && (pm1WakeFlags&0x20)) || wakeCause==ESP_SLEEP_WAKEUP_EXT0);
  Serial.printf("BOOT %s ppr=1 min_rpm=1000 min_mark=%.0f edge_fraction=%.2f sample_hz=50000 task_core=0\n",VERSION,
    StickS3SensorProfile::minMark(sensorProfile),StickS3SensorProfile::EDGE_SWING_FRACTION);
  Serial.printf("RPM_SETTING_BOOT selected=%s default=three_turn_peak legacy_history=retained_metric_not_encoded\n",RpmEstimator::name(rpmEstimator));
  Serial.println("READY commands: p status, P power log, C CPU power profiling, a/c tap A, h tap B, o hold A, b hold B, d diagnostics, n new session, u history export, K backup export, R backup restore, e motion export, q synthetic recap, E<0-4> best effect preview, t synthetic history, y/z storage check, v screen, w recorder export, x ext5V, g<N> GPIO, s stress, j UI stall");
}

// A pull, leaving the recap or closing the Sound editor silences at once;
// the codec powers down once silent and no pull is being measured.
LOOP_HELPER static void tickSound(const Snapshot &s) {
  const bool measuring=s.phase==AnalogTachometer::Phase::Launch;
  const bool celebrating=bestEffect.badge(),editing=controls.page==Controls::Page::Edit && (controls.setting==6 || controls.setting==7);
  if(editorEffect.badge() && !(controls.page==Controls::Page::Edit && controls.setting==6)){editorEffect.clear();dirty=true;}
  if(measuring || (celebrationSound && !celebrating) || (soundPreview && !editing)) {
    soundPlayer.stop();celebrationSound=soundPreview=false;
  }
  if(soundRequest.active && (measuring || (soundRequest.celebration?!celebrating || bestEffect.elapsed(millis())>SOUND_LATE_MS:!editing)))
    soundRequest.active=false;
  if(!soundPlayer.playing())celebrationSound=soundPreview=false;
  soundPlayer.tick(measuring || motionPending);
  if(measuring || motionPending)return;
  // Render a slice per idle pass (1/64 s of audio, a few ms): a waiting
  // request first, else the chosen effect, ready before the next best or the
  // first preview while the Sound editor is open.
  constexpr size_t SLICE=BestEffectSound::RATE/64;
  if(soundRequest.active) {
    if(soundPlayer.prepare(soundRequest.effect,SLICE))requestSound(soundRequest.effect,soundRequest.level,soundRequest.celebration);
    return;
  }
  const bool wanted=appearance.sound!=Appearance::Sound::Off || editing;
  if(wanted && !soundPlayer.prepared(soundTarget()))soundPlayer.prepare(soundTarget(),SLICE);
}

// USB only: replay the last recap with a celebration to check an effect on the
// device. Bests, history and settings are unchanged; max_effect_us and
// max_draw_us restart so PERF reports this preview's slowest pass and frame.
LOOP_HELPER static void previewBestEffect(Appearance::Effect effect) {
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

LOOP_HELPER static void pollCommands(const Snapshot &s) {
  static char command[8]={};static uint8_t used=0;static bool effectPreview=false;
  if(controls.page==Controls::Page::Wipe || wipeBlocked) {
    restoreRx.cancel();used=0;effectPreview=false;
  } else if(restoreRx.active())pollRestore(s);
  // Bound incoming work; an unending USB stream cannot starve button/draw work.
  for(unsigned n=0;n<32 && !restoreRx.active() && Serial.available();++n) {
    const char c=Serial.read();
    // Wipe confirmations and the final hold belong to physical buttons.
    // Read-only status and cancellation cannot advance the confirmation.
    if(controls.page==Controls::Page::Wipe || wipeBlocked) {
      if(c=='p')status();else if(c=='b' && !wipeBlocked)cancelWipe();
      continue;
    }
    // Status polling is observational; other explicit USB actions count as use.
    if(c!='p' && c!='P' && c!='C' && c!='\n' && c!='\r')inactivity.touch(millis());
    if(effectPreview) {
      // E<digit> previews that effect; E<newline> the saved one.
      effectPreview=false;const unsigned digit=unsigned(c-'0');
      if(Appearance::drawn(digit)){previewBestEffect(Appearance::Effect(digit));continue;}
      previewBestEffect(celebrationEffect());if(c=='\n' || c=='\r')continue;
    }
    if(used || c=='g') {
      if(c=='\n' || c=='\r') {
        char *end=nullptr;const long pin=strtol(command+1,&end,10);
        if(end!=command+1 && *end==0 && pin>=1 && pin<=10) {
          if(acquisition.request(Action::Pin,uint8_t(pin)))prefs.putUChar("ao_pin",pin);
          else Serial.println("COMMAND_ERROR gpio_init");
          dirty=true;
        } else Serial.println("COMMAND_ERROR gpio_must_be_1_to_10");
        used=0;command[0]=0;
      } else if(used<sizeof(command)-1){command[used++]=c;command[used]=0;}
      else {used=0;command[0]=0;Serial.println("COMMAND_ERROR too_long");}
    } else if(c=='p')status();
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
    else if(c=='E')effectPreview=true;
    else if(c=='q') {
      if(tournamentMode){Serial.println("RECAP_DEMO_ERROR leave_tournament_first");continue;}
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
    else if(c=='R'){restoreRx.start(millis());Serial.printf("RESTORE_READY max_chars=%u\n",DeviceBackup::TEXT_BYTES);}
    else if(c=='t')previewHistory();
    else if(c=='y')checkStorage();
    else if(c=='z')checkMotionStorage();
    else if(c=='v')exportScreen();
    else if(c=='w')exportSignal();
    else if(c=='x') {
      // Explicit command only. No automatic voltage/power changes in sampling.
      if(!imuAcquisition.lock()){Serial.println("COMMAND_ERROR power_busy");continue;}
      const bool nextExtPower=!extPower;
      const bool changed=pm1Idle().run([&](){M5.Power.setExtOutput(nextExtPower);return true;});
      imuAcquisition.unlock();
      if(changed){extPower=nextExtPower;prefs.putBool("ext5v",extPower);}
      else Serial.println("EXT_POWER_NOTICE toggle_wake_failed");
      acquisition.request(Action::Reset);dirty=true;status();
    } else if(c=='s'){stress=!stress;diagnostics=true;dirty=true;}
    else if(c=='j'){++uiStalls;delay(200);}
  }
}

#if LAUNCHLAB_MOTION_ADC_GATE
LOOP_HELPER static void tickSensorIdle(uint32_t now,Snapshot &s) {
  const auto imu=imuAcquisition.snapshot();
  const bool blocked=usbAwake.inhibited(now) || inactivity.unusedShake() || wipeBlocked ||
    controls.page!=Controls::Page::Main || historyPage || diagnostics || tournamentMode ||
    motionPending || launchFeedback.shown() || practiceStore.pending(practice) ||
    bestStore.pending(personalBests) || !imuAcquisition.running() || Serial.available();
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
      if(practice.accept(event.launches,event.valid,event.resultRpm,millis())) {
        prepareMotion(event);logLaunchMetrics(event);
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
  if(launchFeedback.tick(now)){dirty=true;motionDemo=false;Serial.printf("MAIN_FEEDBACK shown=0 reason=timeout t_ms=%lu\n",(unsigned long)now);}
  if(!launchFeedback.shown())bestEffect.clear();
  if(s.phase==AnalogTachometer::Phase::Launch)inactivity.touch(now);
  finishMotion();
  retryMotion(now,s);
  saveHistory(now,s);s=acquisition.snapshot();
  pollBattery(now,s);
  tickPowerLog(now,s);
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
