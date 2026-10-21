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
prefix = r'''
#define PM1_IDLE_NATIVE
#include "pm1_idle.h"
#include "best_effect_sound.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>
static unsigned busReads=0,pauses=0;
static bool asleep=false,dead=false;
struct Bus {
  bool read(uint8_t,uint8_t *data,size_t size){
    ++busReads;if(dead)return false;if(asleep){asleep=false;return false;}
    for(size_t i=0;i<size;++i)data[i]=0;return true;
  }
  bool write(uint8_t,uint8_t){return !dead;}
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
  struct Config {int task_pinned_core=0;};
  unsigned begins=0,ends=0;bool active=false;
  Config config(){return {};}
  void config(Config){}
  bool isEnabled(){return true;}
  bool begin(){assert(imu.owned && !asleep && !dead);++begins;return true;}
  void end(){assert(imu.owned && !asleep && !dead);++ends;}
  bool isPlaying(){return active;}
  void setVolume(unsigned){}
  bool playRaw(const int16_t *,size_t,unsigned,bool,unsigned,unsigned,bool){active=true;return true;}
  void stop(){active=false;}
};
struct PM1 {unsigned offs=0;bool offOK=true;
  bool powerOff(){assert(!asleep && !dead);++offs;return offOK;}
};
static struct {struct Speaker Speaker;struct {PM1 M5pm1;} Power;} M5;
static struct {template<class... T>void printf(const char*,T...){};} Serial;
static constexpr unsigned ESP_SLEEP_WAKEUP_ALL=0,GPIO_NUM_13=13,ESP_OK=0;
static unsigned sleeps=0,delayCalls=0;
static void esp_sleep_disable_wakeup_source(unsigned){}
static unsigned esp_sleep_enable_ext0_wakeup(unsigned,unsigned){return ESP_OK;}
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
  dead=true;
  assert(!sound.play(Appearance::Effect::Orbit,Appearance::Sound::Low));
  assert(M5.Speaker.begins==1 && !sound.powered() && !imu.owned);
  dead=false;assert(sound.play(Appearance::Effect::Orbit,Appearance::Sound::Low));
  sound.stop();dead=true;sound.tick(false);
  assert(sound.powered() && M5.Speaker.ends==1 && !imu.owned);
  dead=false;asleep=true;sound.tick(false);
  assert(!sound.powered() && M5.Speaker.ends==2 && !imu.owned);
  ShakeSleep wake;dead=true;
  assert(!wake.sleep());assert(M5.Power.M5pm1.offs==0 && sleeps==0);
  dead=false;asleep=true;M5.Power.M5pm1.offOK=false;
  assert(!wake.sleep());assert(M5.Power.M5pm1.offs==1 && sleeps==0);
  M5.Power.M5pm1.offOK=true;asleep=true;
  assert(!wake.sleep());assert(M5.Power.M5pm1.offs==2 && sleeps==1 && delayCalls==1);
  assert(imu.locks==imu.unlocks);
  for(auto p:allocations)std::free(p);
  std::cout<<"PM1 production speaker and shutdown fault guards passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix="pm1-idle-") as tmp:
    path = Path(tmp)
    source = path / "check.cpp"
    source.write_text(prefix + speaker + "\nclass ShakeSleep {public:\n" + member(shake, "bool sleep()") + "\n};\n" + tests)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(root / "firmware/LaunchLabMini"), str(source), "-o", str(path / "check")], check=True)
    subprocess.run([str(path / "check")], check=True)
