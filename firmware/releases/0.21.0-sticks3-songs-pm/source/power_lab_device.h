#pragma once
// Battery lab on the device: test builds only (LAUNCHLAB_POWER_LAB=1); see
// power_lab.h and docs/battery/POWER_LAB.md. Included once by the sketch,
// after its own state and helpers, so the knobs drive the real code paths.
//
// Lines start with '~' and end with a newline. Replies start with LAB_.
//   ~lab                      status
//   ~lab start <unix_s> <segment_s> [charge0] <profile>[;<profile>...]
//   ~lab stop                 an End record, then shipped behaviour
//   ~lab dump                 the header and every record
//   ~lab charge <0|1>         PM1 battery charging (PWR_CFG bit 0), at once
// A profile is power_lab.h's knob text, for example "b30 a1".
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_pm.h"
#include "esp_heap_caps.h"
#include "soc/spi_reg.h"

// The app1 partition as the lab log's flash, partition-relative. Never the
// partition this image runs from.
struct App1Flash {
  const esp_partition_t *partition=nullptr;
  bool begin() {
    partition=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_OTA_1,nullptr);
    if(partition && partition==esp_ota_get_running_partition())partition=nullptr;
    return partition && partition->size>=PowerLab::LOG_BYTES;
  }
  bool read(uint32_t at,void *out,size_t n){return partition && esp_partition_read(partition,at,out,n)==ESP_OK;}
  bool write(uint32_t at,const void *data,size_t n){return partition && esp_partition_write(partition,at,data,n)==ESP_OK;}
  bool erase(uint32_t at,size_t n){return partition && esp_partition_erase_range(partition,at,n)==ESP_OK;}
};

namespace Lab {
inline App1Flash flash;
inline PowerLab::Log<App1Flash> log(flash);
inline bool flashOk=false;
inline uint32_t baseSeconds=0,baseMs=0,lastRecordMs=0,lastPauseTryMs=0;
inline uint8_t boots=0,profile=255;
inline bool firstRecord=false;
inline esp_pm_lock_handle_t cpuLock=nullptr;
inline bool cpuHeld=false;
inline long long lastMinUs=-1;inline int64_t lastMinAt=0;
inline char line[160];inline size_t lineLength=0;inline bool inLine=false;
inline uint32_t seconds(uint32_t now){return baseSeconds+(now-baseMs)/1000;}
}

// The display bus's clock: IDF clocks the StickS3's ST7789 SPI host from
// the PLL, which it stops while the CPU runs from the 40 MHz crystal, so the
// 40 MHz knob moves the bus to the crystal first (M5GFX re-derives its
// divider from the source on every transaction: still 40 MHz writes).
static bool labDisplayOnCrystal(bool crystal) {
  auto *bus=M5.Display.getPanel()?M5.Display.getPanel()->getBus():nullptr;
  if(!bus || bus->busType()!=lgfx::bus_type_t::bus_spi)return false;
  const int host=static_cast<lgfx::Bus_SPI *>(bus)->config().spi_host;
  M5.Display.waitDMA();
  if(crystal)REG_CLR_BIT(SPI_CLK_GATE_REG(host+1),SPI_MST_CLK_SEL);
  else REG_SET_BIT(SPI_CLK_GATE_REG(host+1),SPI_MST_CLK_SEL);
  return true;
}
// The CPU's top frequency, 80 or 40 MHz (knob c2), and what has to move
// with it: the ADC's timing (re-timed by its owner, a sample gap), the display
// bus's clock and the codec (I2S has no clock without the PLL: no sounds).
static bool labCpuMhz(int mhz) {
  if(!CpuPower::active() || CpuPower::maxMhz()==mhz)return CpuPower::active();
  bool ok=true;
  if(mhz==40){soundPlayer.blocked=true;soundPlayer.shutdown();ok=labDisplayOnCrystal(true);}
  ok=ok && CpuPower::setMaxMhz(mhz);
  if(mhz==80){labDisplayOnCrystal(false);soundPlayer.blocked=false;}
  const bool retimed=acquisition.request(Action::Retime,0);
  Serial.printf("LAB_CPU max_mhz=%d ok=%d adc_retimed=%d\n",CpuPower::maxMhz(),ok,retimed);
  return ok;
}
// The savers (power_savers.h) and the IMU poll period a profile selects; the
// default knobs give the shipped settings.
static void labSavers(const PowerLab::Knobs &k) {
  PowerSavers::Settings v;
  if(k.dimS){v.dimMs=uint32_t(k.dimS)*1000;v.dimIgnoresMotion=true;}
  v.dimBacklight=k.dimLevel;
  if(k.restS)v.restMs=uint32_t(k.restS)*1000;
  v.changedRowsOnly=!k.push;
  PowerSavers::settings=v;
  imuAcquisition.setPollMs(k.imuMs?k.imuMs:StickS3MotionAcquisition::POLL_MS);
}

// Applies one profile's knobs that need an action; the rest are read where
// the shipped code decides (tickDisplayPower, tickSensorIdle, the draw gate).
static void labApply(uint8_t index) {
  const auto &k=Lab::log.header().profiles[index];
  Lab::profile=index;Lab::knobs=k;
  if(CpuPower::active()) {
    if(!Lab::cpuLock)esp_pm_lock_create(ESP_PM_APB_FREQ_MAX,0,"ll_lab",&Lab::cpuLock);
    if(Lab::cpuLock && k.cpu==1 && !Lab::cpuHeld){esp_pm_lock_acquire(Lab::cpuLock);Lab::cpuHeld=true;}
    if(Lab::cpuLock && k.cpu!=1 && Lab::cpuHeld){esp_pm_lock_release(Lab::cpuLock);Lab::cpuHeld=false;}
    labCpuMhz(k.cpu==2?40:80);
  }
  labSavers(k);
  char text[64];PowerLab::format(k,text,sizeof(text));
  Serial.printf("LAB_PROFILE index=%u knobs=\"%s\" t_s=%lu\n",index,text,(unsigned long)Lab::seconds(millis()));
  dirty=true;
}
static void labRelease() {
  Lab::running=false;Lab::profile=255;Lab::knobs=PowerLab::Knobs();
  if(Lab::cpuHeld){esp_pm_lock_release(Lab::cpuLock);Lab::cpuHeld=false;}
  if(CpuPower::active())labCpuMhz(80);
  labSavers(Lab::knobs);
  if(Lab::displayAsleep){M5.Display.wakeup();frameRows.invalidate();Lab::displayAsleep=false;}
  dirty=true;
}
static bool labCharge(bool on) {
  if(!imuAcquisition.lock())return false;
  const bool ok=pm1Idle().masked(0x06,0x01,on?0x01:0x00);
  imuAcquisition.unlock();
  return ok;
}

// The share of time below 80 MHz since the previous call, from IDF's PM
// profiling (the same report as the C command); 255 when unavailable.
static uint8_t labCpuLowPct() {
#if LAUNCHLAB_CPU_PM
  constexpr size_t BYTES=8192;
  static char *buffer=static_cast<char *>(heap_caps_malloc(BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!buffer)return 255;
  FILE *stream=fmemopen(buffer,BYTES,"w");
  if(!stream)return 255;
  CpuPower::dump(stream);fputc(0,stream);fclose(stream);buffer[BYTES-1]=0;
  const long long us=PowerLab::minFrequencyUs(buffer);const int64_t at=esp_timer_get_time();
  uint8_t pct=255;
  if(us>=0 && Lab::lastMinUs>=0 && at>Lab::lastMinAt)
    pct=uint8_t(std::min<long long>(100,(us-Lab::lastMinUs)*100/(at-Lab::lastMinAt)));
  Lab::lastMinUs=us;Lab::lastMinAt=at;
  return pct;
#else
  return 255;
#endif
}

static void labRecord(uint32_t now,uint8_t extraFlags=0) {
  PowerLab::Record r;
  r.seconds=Lab::seconds(now);r.profile=Lab::profile==255?0:Lab::profile;
  uint32_t sum=0;unsigned reads=0;uint16_t lo=UINT16_MAX,hi=0,vin=0;uint8_t bits=0;bool chargeRead=false;uint8_t source=0;
  if(imuAcquisition.lock()) {
    if(pm1Idle().ensureAwake()) {
      for(unsigned i=0;i<4;++i) {
        uint16_t mv=0;
        if(M5.Power.M5pm1.getBatteryVoltage(&mv) && mv>=2500 && mv<=4400){sum+=mv;++reads;lo=std::min(lo,mv);hi=std::max(hi,mv);}
      }
      vin=M5.Power.M5pm1.getVBUSVoltage();
      chargeRead=M5.Power.M5pm1.getGPIOInputBits(&bits);
      pm1Idle().read(0x04,&source,1);
    }
    imuAcquisition.unlock();
  }
  const auto s=acquisition.snapshot();
  r.batteryMv=reads?uint16_t(sum/reads):0;r.minMv=reads?lo:0;r.maxMv=hi;r.vinMv=vin;
  r.flags=uint8_t((chargeRead && !(bits&1)?PowerLab::Charging:0) | (chargeRead?PowerLab::ChargeKnown:0) |
    ((source&1)?PowerLab::Vin:0) | (usbAwake.sof()?PowerLab::UsbHost:0) |
    (s.ready && !s.resting && !s.suspended?PowerLab::Sampling:0) | (!Lab::displayAsleep && M5.Display.getBrightness()?PowerLab::DisplayOn:0) |
    (Lab::firstRecord?PowerLab::Booted:0) | extraFlags);
  r.backlight=M5.Display.getBrightness();r.cpuLowPct=labCpuLowPct();
  const float t=temperatureRead();r.tempC=std::isfinite(t)?int8_t(std::max(-127.0f,std::min(127.0f,t))):-128;
  r.boots=Lab::boots;r.frames=uint16_t(std::min<uint32_t>(Lab::frames,UINT16_MAX));r.loops=Lab::loops;r.rows=Lab::rows;
  Lab::frames=Lab::rows=Lab::loops=0;Lab::firstRecord=false;Lab::lastRecordMs=now;
  const bool ok=Lab::log.append(r);
  if(Serial)Serial.printf("LAB_LIVE t_s=%lu profile=%u mv=%u min_mv=%u max_mv=%u vin_mv=%u flags=0x%02x backlight=%u cpu_low_pct=%u temp_c=%d frames=%u rows=%lu loops=%lu saved=%d records=%lu\n",
    (unsigned long)r.seconds,r.profile,r.batteryMv,r.minMv,r.maxMv,r.vinMv,r.flags,r.backlight,r.cpuLowPct,r.tempC,r.frames,
    (unsigned long)r.rows,(unsigned long)r.loops,ok,(unsigned long)Lab::log.count());
}

static void labBoot() {
  Lab::flashOk=Lab::flash.begin();
  const bool open=Lab::flashOk && Lab::log.begin();
  bool charge=true;
  if(open) {
    PowerLab::Record last;
    const bool resumed=Lab::log.last(last);
    Lab::baseSeconds=resumed?last.seconds+PowerLab::RECORD_S:0;Lab::boots=resumed?uint8_t(last.boots+1):0;
    Lab::baseMs=Lab::lastRecordMs=millis();Lab::running=true;Lab::firstRecord=true;
    charge=!(Lab::log.header().flags&PowerLab::ChargeOff);
    labApply(Lab::log.header().profileAt(Lab::baseSeconds));
  }
  // A lab build always leaves PM1 charging on, except while a run asks otherwise.
  const bool chargeSet=labCharge(charge);
  Serial.printf("LAB_BOOT flash=%d run=%d run_id=%lu records=%lu boots=%u charge=%d charge_set=%d\n",Lab::flashOk,open,
    (unsigned long)Lab::log.header().runId,(unsigned long)Lab::log.count(),Lab::boots,charge,chargeSet);
}

static void labTick(uint32_t now) {
  ++Lab::loops;
  if(!Lab::running)return;
  const uint8_t wanted=Lab::log.header().profileAt(Lab::seconds(now));
  if(wanted!=Lab::profile)labApply(wanted);
  if(now-Lab::lastRecordMs>=PowerLab::RECORD_S*1000)labRecord(now);
  if(!Lab::log.open()){Serial.println("LAB_NOTICE log_closed");labRelease();}
}

static void labStatus() {
  PowerLab::Record last;const bool haveLast=Lab::log.last(last);
  Serial.printf("LAB_STATUS flash=%d running=%d run_id=%lu profiles=%u segment_s=%u flags=0x%02x records=%lu slots=%lu t_s=%lu profile=%u last_mv=%u boots=%u\n",
    Lab::flashOk,Lab::running,(unsigned long)Lab::log.header().runId,Lab::log.header().count,Lab::log.header().segmentS,
    Lab::log.header().flags,(unsigned long)Lab::log.count(),(unsigned long)Lab::log.slots(),
    (unsigned long)(Lab::running?Lab::seconds(millis()):0),Lab::profile,haveLast?last.batteryMv:0,Lab::boots);
  for(unsigned i=0;i<Lab::log.header().count && Lab::log.hasRun();++i) {
    char text[64];PowerLab::format(Lab::log.header().profiles[i],text,sizeof(text));
    Serial.printf("LAB_PROFILE_DEF index=%u knobs=\"%s\"\n",i,text);
  }
}

static void labDump() {
  labStatus();
  uint32_t slot=0,n=0;PowerLab::Record r;
  while(Lab::log.next(slot,r)) {
    Serial.printf("LAB_RECORD t_s=%lu profile=%u mv=%u min_mv=%u max_mv=%u vin_mv=%u flags=0x%02x backlight=%u cpu_low_pct=%u temp_c=%d frames=%u rows=%lu loops=%lu boots=%u\n",
      (unsigned long)r.seconds,r.profile,r.batteryMv,r.minMv,r.maxMv,r.vinMv,r.flags,r.backlight,r.cpuLowPct,r.tempC,r.frames,
      (unsigned long)r.rows,(unsigned long)r.loops,r.boots);
    if(++n%32==0)UsbTx::drain(200);
  }
  UsbTx::drain(200);
  Serial.printf("LAB_DUMP_END records=%lu\n",(unsigned long)n);
}

static void labCommand(char *text) {
  char *save=nullptr;const char *word=strtok_r(text," ",&save);
  if(!word || strcmp(word,"lab")){Serial.println("LAB_ERR unknown");return;}
  const char *cmd=strtok_r(nullptr," ",&save);
  if(!cmd || !strcmp(cmd,"status")){labStatus();return;}
  if(!strcmp(cmd,"dump")){labDump();return;}
  if(!strcmp(cmd,"charge")) {
    const char *v=strtok_r(nullptr," ",&save);
    if(!v || (strcmp(v,"0") && strcmp(v,"1"))){Serial.println("LAB_ERR charge 0|1");return;}
    uint8_t cfg=0;const bool ok=labCharge(*v=='1');
    if(imuAcquisition.lock()){pm1Idle().read(0x06,&cfg,1);imuAcquisition.unlock();}
    Serial.printf("LAB_CHARGE on=%d ok=%d pm1_cfg=0x%02x\n",*v=='1',ok,cfg);return;
  }
  if(!strcmp(cmd,"stop")) {
    if(Lab::running)labRecord(millis(),PowerLab::End);
    labRelease();labCharge(true);
    Serial.printf("LAB_STOPPED records=%lu\n",(unsigned long)Lab::log.count());return;
  }
  if(!strcmp(cmd,"start")) {
    PowerLab::Header h;
    const char *id=strtok_r(nullptr," ",&save),*segment=strtok_r(nullptr," ",&save);
    char *rest=save;
    if(!id || !segment || !rest){Serial.println("LAB_ERR start <unix_s> <segment_s> [charge0] <profile>[;...]");return;}
    h.runId=uint32_t(strtoul(id,nullptr,10));h.segmentS=uint16_t(strtoul(segment,nullptr,10));
    while(*rest==' ')++rest;
    if(!strncmp(rest,"charge0",7) && (rest[7]==' ' || !rest[7])){h.flags|=PowerLab::ChargeOff;rest+=7;}
    h.count=0;
    for(char *p=rest,*next=nullptr;p && h.count<PowerLab::MAX_PROFILES;p=next) {
      next=strchr(p,';');if(next)*next++=0;
      if(!PowerLab::parse(p,h.profiles[h.count])){Serial.printf("LAB_ERR profile \"%s\"\n",p);return;}
      ++h.count;
    }
    if(!h.count)h.count=1;
    if(!Lab::flashOk || !h.valid()){Serial.println("LAB_ERR header");return;}
    labRelease();
    if(!Lab::log.start(h)){Serial.println("LAB_ERR flash");return;}
    Lab::baseSeconds=0;Lab::boots=0;Lab::baseMs=millis();Lab::lastRecordMs=Lab::baseMs-PowerLab::RECORD_S*1000;
    Lab::running=true;Lab::firstRecord=true;Lab::frames=Lab::rows=Lab::loops=0;Lab::lastMinUs=-1;
    labCharge(!(h.flags&PowerLab::ChargeOff));
    labApply(0);
    Serial.printf("LAB_STARTED run_id=%lu profiles=%u segment_s=%u charge=%d\n",(unsigned long)h.runId,h.count,h.segmentS,!(h.flags&PowerLab::ChargeOff));
    return;
  }
  Serial.println("LAB_ERR unknown");
}

// A byte for the lab parser; true when it belongs to a '~' line.
static bool labFeed(char c) {
  if(!Lab::inLine) {
    if(c!='~')return false;
    Lab::inLine=true;Lab::lineLength=0;return true;
  }
  if(c=='\n' || c=='\r') {
    Lab::line[Lab::lineLength]=0;Lab::inLine=false;
    if(Lab::lineLength)labCommand(Lab::line);
    return true;
  }
  if(Lab::lineLength<sizeof(Lab::line)-1)Lab::line[Lab::lineLength++]=c;
  return true;
}
