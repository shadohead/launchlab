#!/usr/bin/env python3
"""The production recheckUsbStandby(): the first thing setup() runs after a
timer wake out of a USB-retained standby (0.20.6, shake_wake.h). With 5 V
still there it deep-sleeps again before anything starts; unplugged, or with
PM1 unreadable, the boot goes on to finish the power-off on battery. Every
path releases the internal I2C bus that M5.begin() then takes."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
sketch = (root / "firmware/LaunchLabMini/LaunchLabMini.ino").read_text()


def function(source, name):
    match = re.search(r"(?:LOOP_HELPER )?static [^\n{;]+\b" + name + r"\([^;]*?\)\s*\{", source)
    if not match:
        raise RuntimeError(f"Production function missing: {name}")
    cursor = source.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[match.start():cursor]


# The re-check must come before anything else in setup().
setup = sketch[sketch.index("void setup() {"):]
assert setup.index("recheckUsbStandby(wakeCause);") < setup.index("Serial.") < setup.index("M5.begin(cfg);")

prefix = r'''
#include "power_status.h"
#include "usb_awake.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
enum esp_sleep_wakeup_cause_t {ESP_SLEEP_WAKEUP_UNDEFINED=0,ESP_SLEEP_WAKEUP_EXT0=2,ESP_SLEEP_WAKEUP_TIMER=4};
enum {I2C_NUM_1=1};
enum {GPIO_NUM_47=47,GPIO_NUM_48=48};
struct Asleep {};  // esp_deep_sleep_start() never returns
static unsigned sleeps=0,delays=0;
static void esp_deep_sleep_start(){++sleeps;throw Asleep{};}
static void delay(unsigned){++delays;}
struct InternalI2C {
  bool begun=false,dead=false;unsigned begins=0,releases=0,reads=0;int failFirst=0;uint8_t source=0x05,mask=0x3D;
  bool begin(int port,int sda,int scl){assert(port==I2C_NUM_1 && sda==47 && scl==48);begun=true;++begins;return true;}
  bool release(){begun=false;++releases;return true;}
  bool readRegister(uint8_t address,uint8_t reg,uint8_t *out,size_t n,uint32_t){
    assert(begun && address==0x6E && n==1);++reads;
    if(dead)return false;
    if(failFirst>0){--failFirst;return false;}  // an idle PM1's first transaction
    *out=reg==0x04?source:mask;return true;
  }
};
static struct {InternalI2C In_I2C;} M5;
struct StickS3ShakeWake {
  static constexpr uint8_t SYSTEM_IRQ_MASK=0x44;
  static constexpr uint64_t USB_RECHECK_US=15ULL*60*1000000;
  static inline uint64_t armedUs=0;
  static bool deepSleepWakes(uint64_t us){armedUs=us;return true;}
};
static UsbStandby usbStandby{};
static bool finishStandby=false;
static struct {bool ran=false,read=false;uint8_t source=0,mask=0xff;} standbyRecheck;
static uint64_t standbyRecheckUs(){return StickS3ShakeWake::USB_RECHECK_US;}
'''
tests = r'''
static void reset(){M5.In_I2C=InternalI2C{};usbStandby={};finishStandby=false;standbyRecheck={};sleeps=delays=0;StickS3ShakeWake::armedUs=0;}
static bool run(esp_sleep_wakeup_cause_t cause){try{recheckUsbStandby(cause);return false;}catch(Asleep&){return true;}}
int main(){
  // No standby receipt, or not a timer wake: nothing happens here.
  reset();assert(!run(ESP_SLEEP_WAKEUP_TIMER) && !M5.In_I2C.begins && !finishStandby);
  reset();usbStandby={UsbStandby::MAGIC,false};
  assert(!run(ESP_SLEEP_WAKEUP_EXT0) && !run(ESP_SLEEP_WAKEUP_UNDEFINED) && !M5.In_I2C.begins && !finishStandby);
  // Still on USB: straight back to sleep, re-check armed, bus released first.
  reset();usbStandby={UsbStandby::MAGIC,true};
  assert(run(ESP_SLEEP_WAKEUP_TIMER) && sleeps==1 && !finishStandby && M5.In_I2C.releases==1 && !M5.In_I2C.begun);
  assert(StickS3ShakeWake::armedUs==StickS3ShakeWake::USB_RECHECK_US && standbyRecheck.ran && standbyRecheck.read && standbyRecheck.mask==0x3D);
  // An idle PM1 fails its first read: retried, still asleep again.
  reset();usbStandby={UsbStandby::MAGIC,false};M5.In_I2C.failFirst=1;
  assert(run(ESP_SLEEP_WAKEUP_TIMER) && M5.In_I2C.reads==3 && delays==1 && !finishStandby);
  // Unplugged: boot on, to finish the power-off on battery.
  reset();usbStandby={UsbStandby::MAGIC,false};M5.In_I2C.source=0x04;
  assert(!run(ESP_SLEEP_WAKEUP_TIMER) && finishStandby && !sleeps && !M5.In_I2C.begun && standbyRecheck.source==0x04);
  // PM1 unreadable: boot too (setup() re-checks 5 V before finishing).
  reset();usbStandby={UsbStandby::MAGIC,false};M5.In_I2C.dead=true;
  assert(!run(ESP_SLEEP_WAKEUP_TIMER) && finishStandby && !sleeps && M5.In_I2C.reads==3 && !M5.In_I2C.begun && !standbyRecheck.read);
  std::cout<<"PASS: production recheckUsbStandby: USB re-sleeps at once, unplugged or unreadable boots to finish, the bus always released\n";
}
'''
with tempfile.TemporaryDirectory(prefix="usb-standby-") as tmp:
    path = Path(tmp)
    source = path / "check.cpp"
    body = function(sketch, "recheckUsbStandby")
    # HIL builds only: never part of this production check.
    body = re.sub(r"#if LAUNCHLAB_HIL.*?#endif\n", "", body, flags=re.S)
    source.write_text(prefix + body + tests)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I" + str(root / "firmware/LaunchLabMini"), str(source), "-o", str(path / "check")], check=True)
    subprocess.run([str(path / "check")], check=True)
