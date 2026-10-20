#pragma once
// Hardware-in-the-loop command set. Test builds only (LAUNCHLAB_HIL=1); see
// firmware/tools/hil/README.md. Included once by the sketch, after its own
// state and helpers, so the commands drive the real code paths (commitSetting,
// the IMU gate) rather than copies of them.
//
// Lines start with '!' and end with a newline. Replies start with HIL_.
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <nvs.h>
#include "esp_random.h"
#include "esp_system.h"
#include "hil_optical.h"
#include "hil_imu.h"
#include "usb_tx.h"
#include "hil_resync.h"

namespace Hil {
static constexpr size_t RX_BUFFER=8192,UPLOAD_CHUNK=4096;
inline uint32_t bootId=0;
inline uint32_t uploads=0,uploadErrors=0;
// After any failed upload, input is swallowed until "!sync" or 1 s of quiet.
inline HilResync resync;
inline bool usbIdle=false; // Let the sensor-idle pause run while USB is attached.
inline std::atomic<uint32_t> gateHolds{0},gateHeldMs{0},gateActive{0};

// ---- Reply accounting. The USB TX path drops output it cannot drain within
// its 10 ms timeout (a short write) or, once it has marked the host gone,
// overwrites its ring. A host that misses a reply reads HIL_STATUS: commands
// and last say whether the command arrived; tx_short counts replies cut short.
inline uint32_t commands=0,txShort=0;
inline char lastCommand[12]="none";
inline void reply(const char *format,...) __attribute__((format(printf,1,2)));
inline void reply(const char *format,...) {
  va_list args,copy;va_start(args,format);va_copy(copy,args);
  const int length=vsnprintf(nullptr,0,format,copy);va_end(copy);
  const size_t sent=Serial.vprintf(format,args);va_end(args);
  if(length>0 && sent<size_t(length))++txShort;
}
inline void replyLine(const char *text){reply("%s\n",text);}

// ---- NVS write faults (linker --wrap of the Preferences setters).
struct NvsFaults {
  uint32_t fail=0,slow=0,slowMs=0,writes=0,failed=0,slowed=0;
  char failKey[16]={},slowKey[16]={};
  // A pull armed to start from inside the next matching write: the loop task
  // that would read a USB !pull is itself busy saving.
  bool armed=false;int32_t armDelayMs=0;char armKey[16]={};
};
inline NvsFaults nvs;
inline bool keyMatches(const char *prefix,const char *key){return !prefix[0] || (key && !strncmp(key,prefix,strlen(prefix)));}
// Returns true when this write must fail. Called on the writing task.
inline bool nvsWrite(const char *key,size_t bytes) {
  ++nvs.writes;
  if(nvs.armed && keyMatches(nvs.armKey,key)) {
    nvs.armed=false;
    const int64_t start=esp_timer_get_time()+int64_t(nvs.armDelayMs)*1000;
    pullStartUs=start;++pullCount;
    Hil::reply("HIL_PULL n=%lu start_us=%lld trigger=nvs key=%s delay_ms=%ld\n",(unsigned long)pullCount.load(),
      (long long)start,key?key:"",(long)nvs.armDelayMs);
  }
  if(nvs.slow && keyMatches(nvs.slowKey,key)) {
    --nvs.slow;++nvs.slowed;Hil::reply("HIL_FAULT nvs_slow key=%s ms=%lu\n",key?key:"",(unsigned long)nvs.slowMs);
    delay(nvs.slowMs);
  }
  if(nvs.fail && keyMatches(nvs.failKey,key)) {
    --nvs.fail;++nvs.failed;Hil::reply("HIL_FAULT nvs_fail key=%s bytes=%u\n",key?key:"",unsigned(bytes));return true;
  }
  Hil::reply("HIL_NVS key=%s bytes=%u\n",key?key:"",unsigned(bytes));
  return false;
}

inline const char *resetName(esp_reset_reason_t r) {
  switch(r) {
    case ESP_RST_POWERON:return "poweron";case ESP_RST_SW:return "software";case ESP_RST_PANIC:return "panic";
    case ESP_RST_INT_WDT:return "int_wdt";case ESP_RST_TASK_WDT:return "task_wdt";case ESP_RST_WDT:return "wdt";
    case ESP_RST_DEEPSLEEP:return "deepsleep";case ESP_RST_BROWNOUT:return "brownout";case ESP_RST_USB:return "usb";
    case ESP_RST_JTAG:return "jtag";case ESP_RST_EXT:return "external";default:return "other";
  }
}
inline uint32_t fnv(const uint8_t *p,size_t n,uint32_t h=2166136261u){for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}

// Binary upload with per-chunk acknowledgement. HWCDC drops bytes when its RX
// queue is full, so the host sends one chunk (<= RX_BUFFER/2) per ACK.
inline bool receive(uint8_t *out,size_t bytes,uint32_t expected) {
  // A partial USB packet can sit in the TX buffer until more output follows:
  // flush every line the host waits on before it may send.
  Hil::reply("HIL_UP_READY bytes=%u chunk=%u\n",unsigned(bytes),unsigned(UPLOAD_CHUNK));UsbTx::drain(100);
  size_t got=0;uint32_t hash=2166136261u;
  while(got<bytes) {
    const size_t want=std::min(UPLOAD_CHUNK,bytes-got);size_t chunk=0;
    const uint32_t started=millis();
    while(chunk<want && millis()-started<3000) {
      const int available=Serial.available();
      if(available<=0){delay(1);continue;}
      chunk+=Serial.read(out+got+chunk,std::min(size_t(available),want-chunk));
    }
    if(chunk<want) {
      resync.begin(millis());
      Hil::reply("HIL_ERR up timeout received=%u\n",unsigned(got+chunk));++uploadErrors;return false;
    }
    hash=fnv(out+got,chunk,hash);got+=chunk;
    Hil::reply("HIL_ACK %u\n",unsigned(got));UsbTx::drain(100);
  }
  if(hash!=expected) {
    resync.begin(millis());
    Hil::reply("HIL_ERR up checksum got=%lu expected=%lu\n",(unsigned long)hash,(unsigned long)expected);++uploadErrors;return false;
  }
  ++uploads;return true;
}

inline void status() {
  auto &o=optical();auto &m=imu();auto &f=m.faults;auto &c=m.counters;float a[3],g[3];m.base(a,g);
  Hil::reply("HIL_STATUS boot_id=%08lx reset=%s uptime_ms=%lu optical=%s idle=%.0f/%.1f buffer=%lu keys=%u replaced=%llu cursor=%lld resyncs=%lu pulls=%lu pull_start_us=%lld commands=%lu last=%s tx_short=%lu\n",
    (unsigned long)bootId,resetName(esp_reset_reason()),(unsigned long)millis(),Optical::name(o.mode()),o.baseline(),o.idleNoise(),
    (unsigned long)o.bufferLength(),o.keyCount(),(unsigned long long)o.replaced(),(long long)o.cursor(),(unsigned long)o.resyncs(),
    (unsigned long)pullCount.load(),(long long)pullStartUs.load(),(unsigned long)commands,lastCommand,(unsigned long)txShort);
  Hil::reply("HIL_IMU mode=%s base_g=%.3f,%.3f,%.3f base_dps=%.1f,%.1f,%.1f profile=%lu offset_ms=%ld frames=%lu activations=%lu flushes=%lu length_fails=%lu data_fails=%lu corrupts=%lu garbles=%lu overflows=%lu natural_overflows=%lu oversizes=%lu stalls=%lu skips=%lu pending_faults=%lu,%lu,%lu,%lu,%lu,%lu,%lu jitter_us=%lu sensor_jitter_us=%lu drift_ppm=%ld freeze=%lu\n",
    Imu::name(m.mode()),a[0],a[1],a[2],g[0],g[1],g[2],(unsigned long)m.profileLength(),(long)m.offsetMs(),
    (unsigned long)c.frames.load(),(unsigned long)c.activations.load(),(unsigned long)c.flushes.load(),(unsigned long)c.lengthFails.load(),
    (unsigned long)c.dataFails.load(),(unsigned long)c.corrupts.load(),(unsigned long)c.garbles.load(),(unsigned long)c.overflows.load(),
    (unsigned long)c.naturalOverflows.load(),(unsigned long)c.oversizes.load(),(unsigned long)c.stalls.load(),(unsigned long)c.skips.load(),
    (unsigned long)f.lengthFail.load(),(unsigned long)f.dataFail.load(),(unsigned long)f.corrupt.load(),(unsigned long)f.garble.load(),
    (unsigned long)f.overflow.load(),(unsigned long)f.oversize.load(),(unsigned long)f.stall.load(),
    (unsigned long)f.jitterUs.load(),(unsigned long)f.sensorJitterUs.load(),(long)f.driftPpm.load(),(unsigned long)f.freeze.load());
  Hil::reply("HIL_FAULTS nvs_writes=%lu nvs_failed=%lu nvs_slowed=%lu nvs_pending_fail=%lu nvs_pending_slow=%lu gate_holds=%lu gate_held_ms=%lu gate_active=%lu uploads=%lu upload_errors=%lu\n",
    (unsigned long)nvs.writes,(unsigned long)nvs.failed,(unsigned long)nvs.slowed,(unsigned long)nvs.fail,(unsigned long)nvs.slow,
    (unsigned long)gateHolds.load(),(unsigned long)gateHeldMs.load(),(unsigned long)gateActive.load(),(unsigned long)uploads,(unsigned long)uploadErrors);
}
}

extern "C" {
esp_err_t __real_nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t length);
esp_err_t __real_nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value);
esp_err_t __real_nvs_set_u32(nvs_handle_t handle,const char *key,uint32_t value);
esp_err_t __wrap_nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t length) {
  return Hil::nvsWrite(key,length)?ESP_ERR_NVS_NOT_ENOUGH_SPACE:__real_nvs_set_blob(handle,key,value,length);
}
esp_err_t __wrap_nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value) {
  return Hil::nvsWrite(key,1)?ESP_ERR_NVS_NOT_ENOUGH_SPACE:__real_nvs_set_u8(handle,key,value);
}
esp_err_t __wrap_nvs_set_u32(nvs_handle_t handle,const char *key,uint32_t value) {
  return Hil::nvsWrite(key,4)?ESP_ERR_NVS_NOT_ENOUGH_SPACE:__real_nvs_set_u32(handle,key,value);
}
}

// Holds the shared IMU/I2C gate from another task, as a long FIFO catch-up
// read or PM1 transfer would. Exercises the checkpoint veto and FIFO overflow.
static void hilGateTask(void *arg) {
  const uint32_t ms=uint32_t(uintptr_t(arg));
  while(!imuAcquisition.lock()){}
  ++Hil::gateActive;const uint32_t started=millis();
  vTaskDelay(pdMS_TO_TICKS(ms));
  imuAcquisition.unlock();
  Hil::gateHeldMs=millis()-started;--Hil::gateActive;++Hil::gateHolds;
  vTaskDelete(nullptr);
}

LOOP_HELPER static void hilBoot() {
  Hil::bootId=esp_random();
  Hil::reply("HIL_BOOT boot_id=%08lx reset=%s runtime=%s psram_free=%u\n",(unsigned long)Hil::bootId,
    Hil::resetName(esp_reset_reason()),VERSION,unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

LOOP_HELPER static void hilCommand(char *line) {
  using Hil::optical;using Hil::imu;
  char *save=nullptr;
  const char *cmd=strtok_r(line," ",&save);
  if(!cmd){Hil::replyLine("HIL_ERR empty");return;}
  auto arg=[&]()->const char *{return strtok_r(nullptr," ",&save);};
  auto num=[&](double fallback)->double{const char *t=arg();return t?strtod(t,nullptr):fallback;};
  if(!strcmp(cmd,"hil")){Hil::status();return;}
  // Resynchronization point after a failed upload (also a no-op when idle).
  if(!strcmp(cmd,"sync")){Hil::reply("HIL_SYNC reason=idle discarded=0\n");return;}
  if(!strcmp(cmd,"reset")) {
    Hil::pullStartUs=0;optical().setMode(Hil::Optical::Mode::Pass);imu().setMode(Hil::Imu::Mode::Pass);
    imu().faults.clear();Hil::nvs.fail=Hil::nvs.slow=0;Hil::nvs.armed=false;Hil::usbIdle=false;
    Hil::replyLine("HIL_OK reset");return;
  }
  if(!strcmp(cmd,"opt")) {
    const char *mode=arg();using M=Hil::Optical::Mode;
    if(!mode){Hil::replyLine("HIL_ERR opt mode");return;}
    if(!strcmp(mode,"pass"))optical().setMode(M::Pass);
    else if(!strcmp(mode,"idle"))optical().setMode(M::Idle);
    else if(!strcmp(mode,"buf"))optical().setMode(M::Buffer);
    else if(!strcmp(mode,"gen"))optical().setMode(M::Generator);
    else {Hil::replyLine("HIL_ERR opt mode");return;}
    Hil::reply("HIL_OK opt %s\n",Hil::Optical::name(optical().mode()));return;
  }
  if(!strcmp(cmd,"idle")) {
    const float baseline=num(2000),noise=num(2);optical().setIdle(baseline,noise);
    Hil::reply("HIL_OK idle baseline=%.1f noise=%.2f\n",baseline,noise);return;
  }
  if(!strcmp(cmd,"gen")) {
    const char *what=arg();
    // Reprogramming cancels a scheduled pull, which could otherwise play the new keys at once.
    if(what && !strcmp(what,"clear")){Hil::pullStartUs=0;optical().clearKeys();Hil::replyLine("HIL_OK gen clear");return;}
    if(what && !strcmp(what,"duty")){const float d=num(.3);optical().setDuty(d);Hil::reply("HIL_OK gen duty=%.3f\n",d);return;}
    if(what && !strcmp(what,"key")) {
      Hil::Optical::Key k;k.ms=int32_t(num(0));k.rpm=num(0);k.depth=num(0);k.noise=num(0);
      if(!optical().addKey(k)){Hil::replyLine("HIL_ERR gen key order_or_capacity");return;}
      Hil::reply("HIL_OK gen key=%u\n",optical().keyCount());return;
    }
    Hil::replyLine("HIL_ERR gen");return;
  }
  if(!strcmp(cmd,"imu")) {
    const char *mode=arg();using M=Hil::Imu::Mode;
    if(mode && !strcmp(mode,"pass"))imu().setMode(M::Pass);
    else if(mode && !strcmp(mode,"const"))imu().setMode(M::Constant);
    else if(mode && !strcmp(mode,"prof"))imu().setMode(M::Profile);
    else {Hil::replyLine("HIL_ERR imu mode");return;}
    Hil::reply("HIL_OK imu %s\n",Hil::Imu::name(imu().mode()));return;
  }
  if(!strcmp(cmd,"const")) {
    float a[3],g[3];for(auto &v:a)v=num(0);for(auto &v:g)v=num(0);
    const float noiseG=num(0),noiseDps=num(0);imu().setConstant(a,g,noiseG,noiseDps);
    Hil::reply("HIL_OK const a=%.3f,%.3f,%.3f g=%.1f,%.1f,%.1f noise=%.4f,%.3f\n",a[0],a[1],a[2],g[0],g[1],g[2],noiseG,noiseDps);return;
  }
  if(!strcmp(cmd,"offset")){const int32_t ms=int32_t(num(0));imu().setOffset(ms);Hil::reply("HIL_OK offset ms=%ld\n",(long)ms);return;}
  if(!strcmp(cmd,"up")) {
    const char *kind=arg();const uint32_t count=uint32_t(num(0)),expected=uint32_t(strtoul(arg()?:"0",nullptr,10));
    if(kind && !strcmp(kind,"opt")) {
      if(!count || count>Hil::Optical::BUFFER_CAPACITY){Hil::resync.begin(millis());Hil::replyLine("HIL_ERR up size");return;}
      Hil::pullStartUs=0;
      if(optical().mode()==Hil::Optical::Mode::Buffer)optical().setMode(Hil::Optical::Mode::Idle);
      delay(30); // Any batch reading the old buffer has finished.
      uint16_t *buffer=optical().buffer();
      if(!buffer){Hil::resync.begin(millis());Hil::replyLine("HIL_ERR up no_psram");return;}
      optical().setBufferLength(0);
      if(!Hil::receive(reinterpret_cast<uint8_t *>(buffer),count*2,expected))return;
      optical().setBufferLength(count);
      Hil::reply("HIL_OK up opt samples=%lu\n",(unsigned long)count);return;
    }
    if(kind && !strcmp(kind,"imu")) {
      if(!count || count>Hil::Imu::PROFILE_CAPACITY){Hil::resync.begin(millis());Hil::replyLine("HIL_ERR up size");return;}
      Hil::pullStartUs=0;
      if(imu().mode()==Hil::Imu::Mode::Profile)imu().setMode(Hil::Imu::Mode::Constant);
      delay(30);
      auto *frames=imu().profile();
      if(!frames){Hil::resync.begin(millis());Hil::replyLine("HIL_ERR up no_psram");return;}
      imu().setProfile(0,imu().offsetMs());
      if(!Hil::receive(reinterpret_cast<uint8_t *>(frames),count*sizeof(Hil::Imu::Frame),expected))return;
      imu().setProfile(count,imu().offsetMs());
      Hil::reply("HIL_OK up imu frames=%lu\n",(unsigned long)count);return;
    }
    Hil::resync.begin(millis());Hil::replyLine("HIL_ERR up kind");return;
  }
  if(!strcmp(cmd,"pull")) {
    // Optical sample 0 and IMU profile frame 0 (+offset) share one start time.
    const int32_t lead=int32_t(num(50));
    const int64_t start=esp_timer_get_time()+int64_t(std::max<int32_t>(lead,10))*1000;
    Hil::pullStartUs=start;++Hil::pullCount;
    Hil::reply("HIL_PULL n=%lu start_us=%lld optical=%s imu=%s\n",(unsigned long)Hil::pullCount.load(),(long long)start,
      Hil::Optical::name(optical().mode()),Hil::Imu::name(imu().mode()));return;
  }
  if(!strcmp(cmd,"fault")) {
    const char *what=arg();auto &f=imu().faults;
    if(!what){Hil::replyLine("HIL_ERR fault");return;}
    const uint32_t value=uint32_t(num(1));
    if(!strcmp(what,"clear"))f.clear();
    else if(!strcmp(what,"lenfail"))f.lengthFail+=value;
    else if(!strcmp(what,"datafail"))f.dataFail+=value;
    else if(!strcmp(what,"corrupt"))f.corrupt+=value;
    else if(!strcmp(what,"garble"))f.garble+=value;
    else if(!strcmp(what,"overflow"))f.overflow+=value;
    else if(!strcmp(what,"oversize"))f.oversize+=value;
    else if(!strcmp(what,"stall")){f.stallMs=value;f.stall+=uint32_t(num(1));}
    else if(!strcmp(what,"skip"))f.skip+=value;
    else if(!strcmp(what,"jitter"))f.jitterUs=value;
    else if(!strcmp(what,"sensorjitter"))f.sensorJitterUs=value;
    else if(!strcmp(what,"drift"))f.driftPpm=int32_t(value);
    else if(!strcmp(what,"freeze"))f.freeze=value;
    else {Hil::replyLine("HIL_ERR fault name");return;}
    Hil::reply("HIL_OK fault %s %lu\n",what,(unsigned long)value);return;
  }
  if(!strcmp(cmd,"nvs")) {
    const char *what=arg();
    if(what && !strcmp(what,"clear")){Hil::nvs.fail=Hil::nvs.slow=0;Hil::replyLine("HIL_OK nvs clear");return;}
    if(what && !strcmp(what,"fail")) {
      Hil::nvs.fail=uint32_t(num(1));const char *key=arg();snprintf(Hil::nvs.failKey,sizeof(Hil::nvs.failKey),"%s",key?key:"");
      Hil::reply("HIL_OK nvs fail=%lu key=%s\n",(unsigned long)Hil::nvs.fail,Hil::nvs.failKey);return;
    }
    if(what && !strcmp(what,"slow")) {
      Hil::nvs.slowMs=uint32_t(num(200));Hil::nvs.slow=uint32_t(num(1));const char *key=arg();
      snprintf(Hil::nvs.slowKey,sizeof(Hil::nvs.slowKey),"%s",key?key:"");
      Hil::reply("HIL_OK nvs slow=%lu ms=%lu key=%s\n",(unsigned long)Hil::nvs.slow,(unsigned long)Hil::nvs.slowMs,Hil::nvs.slowKey);return;
    }
    Hil::replyLine("HIL_ERR nvs");return;
  }
  if(!strcmp(cmd,"arm")) {
    // !arm <key_prefix> <delay_ms>: start the programmed pull from inside the
    // next matching NVS write, delay_ms later (may be negative: already running).
    const char *key=arg();Hil::nvs.armDelayMs=int32_t(num(0));
    snprintf(Hil::nvs.armKey,sizeof(Hil::nvs.armKey),"%s",key && strcmp(key,"*")?key:"");Hil::nvs.armed=true;
    Hil::reply("HIL_OK arm key=%s delay_ms=%ld\n",Hil::nvs.armKey,(long)Hil::nvs.armDelayMs);return;
  }
  if(!strcmp(cmd,"gate")) {
    const uint32_t ms=uint32_t(num(300));
    const bool ok=xTaskCreatePinnedToCore(hilGateTask,"hil_gate",3072,reinterpret_cast<void *>(uintptr_t(ms)),2,nullptr,1)==pdPASS;
    Hil::reply("HIL_OK gate ms=%lu started=%d\n",(unsigned long)ms,ok);return;
  }
  if(!strcmp(cmd,"set")) {
    // The real Apply path: checkpoint, NVS write, resume, error state.
    const unsigned row=unsigned(num(-1)),value=unsigned(num(-1));
    if(row>=Controls::EDITABLE_SETTING_COUNT || value>255){Hil::replyLine("HIL_ERR set row");return;}
    controls.page=Controls::Page::Edit;controls.setting=row;controls.draft=uint8_t(value);settingsSaveError=false;
    const uint32_t started=millis();
    commitSetting();
    Hil::reply("HIL_SET row=%u value=%u error=%d page=%u ms=%lu\n",row,value,settingsSaveError,unsigned(controls.page),(unsigned long)(millis()-started));
    return;
  }
  if(!strcmp(cmd,"usbidle")){Hil::usbIdle=num(1)!=0;Hil::reply("HIL_OK usbidle %d\n",Hil::usbIdle);return;}
  if(!strcmp(cmd,"powerlog")) {
    // The next loop pass runs the real 5-minute power-log checkpoint.
    powerRecord(PowerDiagnostics::Sample);lastPowerSaveTry=millis()-300000;
    Hil::reply("HIL_OK powerlog dirty=%d ready=%d\n",powerLogDirty,powerLogReady);return;
  }
  if(!strcmp(cmd,"reboot")) {
    // A restart through the normal boot path (the custom theme scenario).
    Hil::replyLine("HIL_REBOOT");UsbTx::drain(200);delay(100);ESP.restart();return;
  }
  if(!strcmp(cmd,"themecorrupt")) {
    // Damages the stored custom themes record, inside an ADC checkpoint as
    // every flash write is; the next boot must fall back to the default.
    const auto s=acquisition.snapshot();bool ok=false;
    if(acquisition.request(Action::IdleCheckpointBegin,s.launches)) {
      Preferences store;uint8_t junk[Themes::Library::IMAGE_BYTES];std::memset(junk,0x5a,sizeof(junk));
      ok=store.begin(ThemeStore::SPACE,false) && store.putBytes(ThemeStore::KEY,junk,sizeof(junk))==sizeof(junk);store.end();
      acquisition.request(Action::CheckpointEnd);
    }
    Hil::reply("HIL_OK themecorrupt ok=%d\n",ok);return;
  }
  if(!strcmp(cmd,"home")) {
    if(controls.page==Controls::Page::Edit)cancelSettingEdit();
    controls.page=Controls::Page::Main;syncNavigation();
    Hil::replyLine("HIL_OK home");return;
  }
  Hil::reply("HIL_ERR unknown %s\n",cmd);
}

// Feeds one USB character; returns true when it belonged to a HIL line.
LOOP_HELPER static bool hilFeed(char c) {
  static char line[128];static uint8_t used=0;static bool active=false;
  // Chunk data still streaming after a failed upload: never let it reach
  // this parser or the production one.
  switch(Hil::resync.feed(c,millis())) {
    case HilResync::Result::Swallowed:return true;
    case HilResync::Result::Synced:
      active=false;Hil::reply("HIL_SYNC reason=sync discarded=%lu\n",(unsigned long)Hil::resync.discarded);return true;
    case HilResync::Result::Quiet:
      active=false;Hil::reply("HIL_SYNC reason=quiet discarded=%lu\n",(unsigned long)Hil::resync.discarded);break;
    case HilResync::Result::Pass:break;
  }
  if(!active) {
    if(c!='!')return false;
    active=true;used=0;return true;
  }
  if(c=='\r')return true;
  if(c=='\n') {
    line[used]=0;active=false;
    char word[sizeof(Hil::lastCommand)]={};std::sscanf(line,"%11s",word);
    hilCommand(line);UsbTx::drain(100);
    // Recorded after the command so its own HIL_STATUS shows the previous one.
    ++Hil::commands;std::memcpy(Hil::lastCommand,word,sizeof(word));
    return true;
  }
  if(used<sizeof(line)-1)line[used++]=c;
  else {active=false;Hil::replyLine("HIL_ERR too_long");}
  return true;
}
