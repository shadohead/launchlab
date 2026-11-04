#!/usr/bin/env python3
"""Fault-test production speaker callbacks and PM1 shutdown wake guards."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
speaker = (root / "firmware/LaunchLabMini/best_effect_speaker.h").read_text()
speaker = speaker[speaker.index("class BestEffectSpeaker {"):]
shake = (root / "firmware/LaunchLabMini/shake_wake.h").read_text()


def member(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor]


# arm's BMI upload can exceed the idle timeout: wake just before its PM1
# sequence. Preserve the audited power-hold and wake-register operations.
arm = member(shake, "bool arm()")
assert arm.index("pm1Idle().ensureAwake()") < arm.index("pm.setGPIOMode(")
assert "masked(0x07,0x60,0x20)" in arm
assert "masked(0x18,0x18,0x10)" in arm
# The green LED (PM1 LED_EN) stays off for the shake standby (0.20.6).
assert "pm.setLedEnLevel(false)" in arm and "setLedEnLevel(true)" not in arm
constants = "\n".join(line for line in shake.splitlines() if "SYSTEM_IRQ_STATUS=" in line or "USB_RECHECK_US=" in line)
assert constants.count("static constexpr") == 2
prefix = r'''
#define PM1_IDLE_NATIVE
#include "pm1_idle.h"
#include "es8311_power.h"
#include "power_status.h"
#include "best_effect_sound.h"
#include "sound_sync.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>
static unsigned busReads=0,pauses=0;
static bool asleep=false,dead=false;
static std::vector<std::pair<uint8_t,uint8_t>> pmWrites;
struct Bus {
  bool read(uint8_t,uint8_t *data,size_t size){
    ++busReads;if(dead)return false;if(asleep){asleep=false;return false;}
    for(size_t i=0;i<size;++i)data[i]=0;return true;
  }
  bool write(uint8_t reg,uint8_t value){if(dead)return false;pmWrites.push_back({reg,value});return true;}
  void pause(){++pauses;}
};
static PM1Idle<Bus> &pm1Idle(){static Bus bus;static PM1Idle<Bus> idle(bus);return idle;}
static std::vector<void *> allocations;
static constexpr unsigned MALLOC_CAP_SPIRAM=1;
static void *heap_caps_malloc(size_t n,unsigned){auto p=std::malloc(n);allocations.push_back(p);return p;}
static void heap_caps_free(void *p){for(auto &a:allocations)if(a==p)a=nullptr;std::free(p);}
static int64_t esp_timer_get_time(){return 0;}
struct StickS3MotionAcquisition {
  bool owned=false,available=true;unsigned locks=0,unlocks=0;
  bool lock(){if(!available)return false;assert(!owned);owned=true;++locks;return true;}
  void unlock(){assert(owned);owned=false;++unlocks;}
};
static StickS3MotionAcquisition imu;
struct Speaker {
  struct Config {int task_pinned_core=0;bool stereo=true;};
  unsigned begins=0,ends=0;bool active=false,asyncStop=false;
  Config config(){return {};}
  void config(Config){}
  bool isEnabled(){return true;}
  bool begin(){assert(imu.owned && !asleep && !dead);++begins;return true;}
  void end(){assert(imu.owned && !asleep && !dead);++ends;active=false;}
  bool isPlaying(){return active;}
  bool isPlaying(uint8_t){return active;}
  void setVolume(unsigned){}
  void setChannelVolume(uint8_t,uint8_t){}
  bool playRaw(const int16_t *,size_t,unsigned,bool,unsigned,unsigned,bool){active=true;return true;}
  void stop(){if(!asyncStop)active=false;}  // M5's task clears the channels later
  void stop(uint8_t){stop();}
};
struct PM1 {unsigned offs=0;bool offOK=true;
  bool powerOff(){assert(!asleep && !dead);++offs;return offOK;}
};
// The codec's power-down rides the speaker's own IMU/I2C gate.
struct InternalI2C {unsigned codecWrites=0;
  bool writeRegister8(uint8_t address,uint8_t,uint8_t,uint32_t){assert(address==0x18 && imu.owned);if(dead)return false;++codecWrites;return true;}
};
static struct {struct Speaker Speaker;struct {PM1 M5pm1;} Power;InternalI2C In_I2C;} M5;
// The music streamer's stand-ins (best_effect_speaker.h defines them for
// the device above the class this harness takes): a host lock, no task.
#include "music_stream.h"
#include <mutex>
struct HostLock {std::mutex m;struct Hold {std::mutex &m;explicit Hold(HostLock &l):m(l.m){m.lock();}~Hold(){m.unlock();}};};
using Music=MusicStream<HostLock>;
using TaskHandle_t=void *;
static void xTaskNotifyGive(TaskHandle_t){}
struct MusicChannel:MusicOutput {
  unsigned queued() override{return 0;}
  bool queue(const int16_t *,size_t) override{return true;}
  void volume(uint8_t) override{}
  void stop() override{}
};

static struct {template<class... T>void printf(const char*,T...){};} Serial;
static constexpr unsigned ESP_SLEEP_WAKEUP_ALL=0,GPIO_NUM_13=13,ESP_OK=0;
static unsigned sleeps=0,delayCalls=0;
static void esp_sleep_disable_wakeup_source(unsigned){}
static unsigned esp_sleep_enable_ext0_wakeup(unsigned,unsigned){return ESP_OK;}
static uint64_t timerWakeUs=0;
static unsigned esp_sleep_enable_timer_wakeup(uint64_t us){timerWakeUs=us;return ESP_OK;}
static void delay(unsigned){++delayCalls;}
static void esp_deep_sleep_start(){++sleeps;}
'''
tests = r'''
int main(){
  BestEffectSpeaker sound(imu);sound.configure();
  assert(sound.prepare(Appearance::Effect::Orbit));
  asleep=true;
  assert(sound.play(Appearance::Effect::Orbit,Appearance::Sound::Low));
  assert(M5.Speaker.begins==1 && pauses==1 && !imu.owned && sound.powered());
  sound.stop();asleep=true;
  const auto oldReads=busReads;sound.tick(true);
  assert(busReads==oldReads && M5.Speaker.ends==0); // No bus access mid-pull.
  sound.tick(false);
  assert(M5.Speaker.ends==1 && pauses==2 && !sound.powered() && !imu.owned);
  assert(M5.In_I2C.codecWrites==sizeof(Es8311Power::DOWN)/sizeof(Es8311Power::DOWN[0]));
  dead=true;
  assert(!sound.play(Appearance::Effect::Orbit,Appearance::Sound::Low));
  assert(M5.Speaker.begins==1 && !sound.powered() && !imu.owned);
  dead=false;assert(sound.play(Appearance::Effect::Orbit,Appearance::Sound::Low));
  sound.stop();dead=true;sound.tick(false);
  assert(sound.powered() && M5.Speaker.ends==1 && !imu.owned);
  dead=false;asleep=true;sound.tick(false);
  assert(!sound.powered() && M5.Speaker.ends==2 && !imu.owned);
  // Sleep while a sound plays: stop() is asynchronous on the device, so
  // shutdown() powers the speaker and codec down without waiting for it.
  assert(sound.play(Appearance::Effect::Orbit,Appearance::Sound::Low) && sound.powered());
  M5.Speaker.asyncStop=true;const unsigned codecBefore=M5.In_I2C.codecWrites;
  sound.tick(false);assert(sound.powered() && M5.Speaker.ends==2);  // tick() waits for silence
  sound.shutdown();
  assert(!sound.powered() && M5.Speaker.ends==3 && !imu.owned && M5.In_I2C.codecWrites>codecBefore);
  sound.shutdown();assert(M5.Speaker.ends==3);  // nothing left to power down
  M5.Speaker.asyncStop=false;
  ShakeSleep wake;dead=true;
  assert(!wake.sleep());assert(M5.Power.M5pm1.offs==0 && sleeps==0);
  dead=false;asleep=true;M5.Power.M5pm1.offOK=false;pmWrites.clear();
  assert(!wake.sleep());assert(M5.Power.M5pm1.offs==1 && sleeps==0);
  // A refused power-off leaves the unit awake: 5VIN removal is masked again.
  assert(!pmWrites.empty() && pmWrites.back().first==0x44 && (pmWrites.back().second&0x02));
  M5.Power.M5pm1.offOK=true;asleep=true;pmWrites.clear();
  assert(!wake.sleep());assert(M5.Power.M5pm1.offs==2 && sleeps==1 && delayCalls==1);
  // Before the power-off: 5VIN removal unmasked (0x44 bit 1 clear) after its
  // status is cleared, so unplugging wakes a USB-retained standby; the timer
  // re-check is armed too.
  bool cleared=false,unmasked=false;
  for(auto w:pmWrites){if(w.first==0x41 && w.second==0)cleared=true;if(w.first==0x44 && !(w.second&0x02))unmasked=cleared;}
  assert(cleared && unmasked && timerWakeUs==ShakeSleep::USB_RECHECK_US);
  // With a computer attached there is no timer: the IRQ alone.
  timerWakeUs=0;M5.Power.M5pm1.offOK=true;assert(!wake.sleep(0) && sleeps==2 && !timerWakeUs);
  assert(imu.locks==imu.unlocks);
  for(auto p:allocations)std::free(p);
  std::cout<<"PM1 production speaker and shutdown fault guards passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix="pm1-idle-") as tmp:
    path = Path(tmp)
    source = path / "check.cpp"
    source.write_text(prefix + speaker + "\nclass ShakeSleep {public:\n" + constants + "\n" + member(shake, "bool sleep(") + "\n" +
                      member(shake, "static bool deepSleepWakes(") + "\n};\n" + tests)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(root / "firmware/LaunchLabMini"), str(source), "-o", str(path / "check")], check=True)
    subprocess.run([str(path / "check")], check=True)
