#!/usr/bin/env python3
"""Execute production display-power/battery polling against guarded fakes."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "firmware/LaunchLabMini/LaunchLabMini.ino").read_text()


def function(name):
    match = re.search(r"(?:LOOP_HELPER )?static [^\n{;]+\b" + name + r"\([^;]*?\)\s*\{", source)
    assert match, f"Production function missing: {name}"
    opening = source.index("{", match.start())
    depth, cursor = 1, opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[match.start():cursor]


setup = source[source.index("void setup() {"):source.index("LOOP_HELPER static void tickSound(")]
assert setup.index("cfg.pmic_button=false;") < setup.index("M5.begin(cfg);")
wake = re.search(r"inactivity\.begin\(([^;]+)\);", setup)
assert wake and "wakingFromStandby" not in wake.group(1)
assert "pm1WakeValid" in wake.group(1) and "pm1WakeFlags&0x20" in wake.group(1)
assert "wakeCause==ESP_SLEEP_WAKEUP_EXT0" in wake.group(1)

prefix = r'''
#define PM1_IDLE_NATIVE
#define LOOP_HELPER
#include "pm1_idle.h"
#include "power_status.h"
#include "power_savers.h"
#include "usb_awake.h"
#include "analog_tachometer.h"
#include <cassert>
#include <iostream>
#include <vector>
static uint64_t clockUs=0;
static int64_t esp_timer_get_time(){return int64_t(clockUs);}
static bool owned=false,mutexAvailable=true;
static unsigned locks=0,unlocks=0;
struct Imu {
  bool lock(){++locks;if(!mutexAvailable)return false;assert(!owned);owned=true;return true;}
  void unlock(){assert(owned);owned=false;++unlocks;}
} imuAcquisition;
struct Bus {
  bool asleep=false,dead=false,voltageOK=true,chargeOK=true,configOK=true;
  uint16_t voltage=3900;uint8_t gpio=1,config=0x06;
  unsigned reads=0,pauses=0;
  std::vector<uint8_t> registers;
  bool read(uint8_t reg,uint8_t *data,size_t n){
    assert(owned);++reads;registers.push_back(reg);clockUs+=10;
    if(dead)return false;
    if(asleep){asleep=false;return false;}
    if(reg==0x09){assert(n==1);*data=2;return true;}
    if(reg==0x22){assert(n==2);if(!voltageOK)return false;data[0]=uint8_t(voltage);data[1]=uint8_t(voltage>>8);return true;}
    if(reg==0x12){assert(n==1);if(!chargeOK)return false;*data=gpio;return true;}
    assert(reg==0x06 && n==1);if(!configOK)return false;*data=config;return true;
  }
  bool write(uint8_t,uint8_t){assert(false);return false;}
  void pause(){assert(owned);++pauses;clockUs+=1000;}
} bus;
static PM1Idle<Bus> idle(bus);
static PM1Idle<Bus> &pm1Idle(){return idle;}
struct PM1 {
  unsigned voltageReads=0,chargeReads=0;
  bool getBatteryVoltage(uint16_t *mv){++voltageReads;uint8_t bytes[2]={};
    if(!bus.read(0x22,bytes,2))return false;*mv=uint16_t(bytes[0]|uint16_t(bytes[1])<<8);return true;}
  bool getGPIOInputBits(uint8_t *bits){++chargeReads;return bus.read(0x12,bits,1);}
};
static bool boundary=false;
struct Acquisition {
  unsigned boundaries=0;
  void displayBoundary(bool active){assert(active!=boundary);boundary=active;++boundaries;}
} acquisition;
struct M5Fake {
  struct DisplayFake {
    uint8_t brightness=102;unsigned writes=0;
    uint8_t getBrightness(){return brightness;}
    void setBrightness(uint8_t value){assert(boundary);brightness=value;++writes;}
  } Display;
  struct {PM1 M5pm1;} Power;
} M5;
static struct {template<class... T>void printf(const char*,T...){};} Serial;
namespace Controls {enum class Page {Main,Edit};struct State {Page page=Page::Main;unsigned setting=0;uint8_t draft=0;};}
namespace PracticeUI {enum class View {Recent,Battery};}
static Controls::State controls;
static PracticeUI::View historyView=PracticeUI::View::Recent;
static bool historyPage=false,dirty=false;
static uint8_t brightnessPercent=40,powerConfig=0;
static bool powerConfigValid=false,batteryPolled=false;
static uint32_t lastBatteryPoll=0,maxBatteryPollUs=0;
static StickS3Battery battery;
static InactivityTimer inactivity;
static UsbHostAwake usbAwake;
static struct {bool visible=false;bool shown(){return visible;}} launchFeedback;
struct Snapshot {AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;bool opticalIdle=true;};
// The corner battery as drawn (the sketch's cornerBattery): its fill, on pages that show it.
struct Corner {int fill=0;bool operator!=(const Corner &o)const{return fill!=o.fill;}};
static bool cornerShown=false;static Corner drawnCornerBattery;
static Corner cornerBattery(uint32_t now){Corner c;if(cornerShown && battery.valid(now))c.fill=battery.percent;return c;}
'''
tests = r'''
static void at(uint32_t ms){clockUs=uint64_t(ms)*1000;}
static void reset(){
  clockUs=0;owned=boundary=false;mutexAvailable=true;locks=unlocks=0;
  bus=Bus{};acquisition=Acquisition{};M5=M5Fake{};controls={};
  historyPage=dirty=powerConfigValid=batteryPolled=false;
  historyView=PracticeUI::View::Recent;brightnessPercent=40;powerConfig=0;
  lastBatteryPoll=maxBatteryPollUs=0;battery=StickS3Battery{};
  inactivity=InactivityTimer{};inactivity.begin(0);usbAwake=UsbHostAwake{};launchFeedback.visible=false;
  cornerShown=false;drawnCornerBattery={};
  PowerSavers::settings=PowerSavers::Settings{};
}
static Snapshot state;
static void display(uint32_t ms){at(ms);tickDisplayPower(ms,state);assert(!boundary);}
static void poll(uint32_t ms){at(ms);pollBattery(ms,state);assert(!owned);}
int main(){
  reset();display(29999);assert(!M5.Display.writes);
  display(30000);assert(M5.Display.brightness==26 && M5.Display.writes==1 && acquisition.boundaries==2);
  display(31000);assert(M5.Display.writes==1); // Do not keep writing PWM while dim.
  inactivity.touch(31000);display(31000);assert(M5.Display.brightness==102 && M5.Display.writes==2);
  display(31001);assert(M5.Display.writes==2); // A button/activity restores once.
  display(61000);assert(M5.Display.brightness==26);
  inactivity.touch(61000);display(61000);assert(M5.Display.brightness==102);
  // Shipped: movement keeps the screen at its selected brightness.
  reset();inactivity.motion(20000);display(30000);assert(!M5.Display.writes);
  display(50000);assert(M5.Display.brightness==26);
  std::cout<<"PASS: 30-second dim and button/accepted-activity restoration; no repeated brightness writes\n";
  // The battery savers (power_savers.h): a dim that only use undoes, sooner,
  // and to a lower backlight; movement no longer holds it bright.
  reset();PowerSavers::settings.dimMs=10000;PowerSavers::settings.dimIgnoresMotion=true;PowerSavers::settings.dimBacklight=5;
  inactivity.motion(9000);display(9999);assert(!M5.Display.writes);
  display(10000);assert(M5.Display.brightness==5 && M5.Display.writes==1);
  inactivity.motion(12000);display(12000);assert(M5.Display.brightness==5 && M5.Display.writes==1);
  inactivity.touch(13000);display(13000);assert(M5.Display.brightness==102 && M5.Display.writes==2);
  display(23000);assert(M5.Display.brightness==5);
  launchFeedback.visible=true;display(24000);assert(M5.Display.brightness==102);  // the recap stays bright
  std::cout<<"PASS: battery savers: dim after 10 s without use while moving, to a raw backlight of 5, use restores\n";

  reset();display(30000);usbAwake.sampleHost(30000,true);display(30000);
  assert(M5.Display.brightness==102);usbAwake.sampleHost(30250,true);usbAwake.samplePower(30250,true,1);
  usbAwake.sampleHost(30500,false);display(30500);assert(M5.Display.brightness==102);
  usbAwake.samplePower(31000,true,0);display(31000);assert(M5.Display.brightness==26);
  launchFeedback.visible=true;display(31000);assert(M5.Display.brightness==102);
  launchFeedback.visible=false;display(31000);assert(M5.Display.brightness==26);
  std::cout<<"PASS: USB host/suspend and retained pull feedback restore selected brightness\n";

  reset();brightnessPercent=5;M5.Display.brightness=13;display(30000);
  assert(M5.Display.brightness==13 && !M5.Display.writes); // Never raise a lower selection.
  controls.page=Controls::Page::Edit;controls.setting=2;controls.draft=80;
  inactivity.touch(30000);display(30000);assert(M5.Display.brightness==204);
  controls.setting=1;display(30000);assert(M5.Display.brightness==13);
  std::cout<<"PASS: low brightness preserved; brightness editor draft applies only to its own setting\n";

  reset();state.phase=AnalogTachometer::Phase::Launch;display(30000);
  assert(!M5.Display.writes && !acquisition.boundaries);
  state.phase=AnalogTachometer::Phase::Ready;state.opticalIdle=false;display(30000);
  assert(!M5.Display.writes && !acquisition.boundaries);
  state.opticalIdle=true;display(30000);assert(M5.Display.brightness==26);
  state.phase=AnalogTachometer::Phase::Launch;launchFeedback.visible=true;display(30001);
  assert(M5.Display.brightness==26 && M5.Display.writes==1);
  state.phase=AnalogTachometer::Phase::Ready;state.opticalIdle=false;display(30001);
  assert(M5.Display.brightness==26 && M5.Display.writes==1);
  state.opticalIdle=true;display(30001);assert(M5.Display.brightness==102 && M5.Display.writes==2);
  std::cout<<"PASS: launch and active Ready optical input defer all brightness boundaries/writes\n";

  reset();state={};historyPage=true;historyView=PracticeUI::View::Battery;bus.asleep=true;poll(0);
  assert(batteryPolled && lastBatteryPoll==0 && battery.valid(0) && battery.percent==75);
  assert(locks==1 && unlocks==1 && bus.pauses==1);
  assert(bus.registers==std::vector<uint8_t>({0x09,0x09,0x22,0x12,0x06}));
  assert(M5.Power.M5pm1.voltageReads==1 && M5.Power.M5pm1.chargeReads==1);
  assert(powerConfigValid && powerConfig==0x06 && dirty && maxBatteryPollUs>=1000);
  dirty=false;poll(4999);assert(locks==1 && bus.reads==5 && !dirty);
  state.phase=AnalogTachometer::Phase::Launch;poll(5000);assert(locks==1 && bus.reads==5);
  state.phase=AnalogTachometer::Phase::Ready;poll(6000);
  assert(locks==2 && unlocks==2 && bus.reads==9 && dirty);
  std::cout<<"PASS: battery voltage/charge/config serialized under one IMU gate; bounded wake retry; launch/5-second polls defer\n";

  // The corner battery (Battery icon, 0.20.8): a reading that changes what it
  // draws redraws the page; the same drawing, or a page without it, does not.
  reset();state={};cornerShown=true;poll(0);assert(dirty);
  drawnCornerBattery=cornerBattery(0);dirty=false;poll(5000);assert(!dirty);
  bus.dead=true;poll(10000);assert(dirty && !cornerBattery(10000).fill);
  reset();state={};poll(0);assert(battery.valid(0) && !dirty);
  std::cout<<"PASS: a battery reading redraws the page only when the corner battery changes\n";

  reset();state={};mutexAvailable=false;poll(0);
  assert(!batteryPolled && !bus.reads && locks==1 && !unlocks);
  mutexAvailable=true;poll(5);assert(battery.valid(5) && batteryPolled && locks==2 && unlocks==1);
  assert(battery.valid(15005) && !battery.valid(15006));
  bus.dead=true;poll(16000);
  assert(!battery.valid(16000) && battery.percent==-1 && !battery.chargeKnown && !powerConfigValid);
  assert(bus.reads==8 && bus.pauses==2); // Awake and config each stop after two reads.
  assert(M5.Power.M5pm1.voltageReads==1 && M5.Power.M5pm1.chargeReads==1);
  bus.dead=false;bus.voltage=3700;poll(21000);
  assert(battery.valid(21000) && battery.estimateMillivolts==3700 && battery.percent==50 && powerConfigValid);
  assert(locks==4 && unlocks==3);
  std::cout<<"PASS: mutex miss and stale/error battery availability recover; persistent PM1 faults are bounded\n";

  reset();state={};bus.voltageOK=false;bus.chargeOK=false;bus.configOK=false;poll(0);
  assert(!battery.valid(0) && battery.percent==-1 && !battery.chargeKnown && !powerConfigValid);
  assert(locks==unlocks && bus.reads==5 && bus.pauses==1);
  bus.voltageOK=bus.chargeOK=bus.configOK=true;bus.voltage=3300;bus.gpio=0;poll(5000);
  assert(battery.valid(5000) && battery.percent==0 && battery.charging && powerConfigValid);
  std::cout<<"PASS: direct voltage/charge/config errors show unavailable then recover; critical voltage never smoothed away\n";
  std::cout<<"PASS: production setup disables unused PMIC polling; fresh hardware wake controls shake grace independently of diagnostic logs\n";
}
'''
with tempfile.TemporaryDirectory(prefix="idle-power-pipeline-") as tmp:
    path = Path(tmp)
    cpp = path / "pipeline.cpp"
    cpp.write_text(prefix + function("pollBattery") + "\n" + function("tickDisplayPower") + tests)
    exe = path / "pipeline"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I" + str(root / "firmware/LaunchLabMini"), "-I" + str(root / "firmware/LaunchLabRpm"), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
