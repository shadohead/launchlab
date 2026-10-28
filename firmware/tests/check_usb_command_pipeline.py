#!/usr/bin/env python3
"""Run the sketch's actual pollCommands/runUsbCommand against USB byte streams.

Every action a USB letter can take is a counting fake. Random streams (with
runs of the dangerous letters inside bursts) must take none of the
state-changing ones, while the beywatch updater's K / R + base64 + '.' flow,
status polling, the repo's tools and, in a HIL build, the rig's 'p' then
'!hil' polling keep working. See usb_command_guard.h.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "LaunchLabMini/LaunchLabMini.ino"


def function(source, name, result="void"):
    signature = "static " + result + " " + name + "("
    start = source.index(signature)
    while source.index(";", start) < source.index("{", start):
        start = source.index(signature, start + 1)
    opening = source.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PREFIX = r'''
#include "usb_command_guard.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#define LOOP_HELPER
static uint32_t clockMs=1000;
static uint32_t millis(){return clockMs;}
static std::map<std::string,unsigned> acted; // every action a letter took
static void act(const char *what){++acted[what];}
static UsbCommandGuard usbCommands;
static void delay(unsigned ms){clockMs+=ms;act("stall");}
struct SerialFake {
  std::deque<char> input;std::vector<std::string> output;
  int available()const{return int(input.size());}
  char read(){const char c=input.front();input.pop_front();return c;}
  void println(const char *s){output.emplace_back(s);}
  template<class... T>void printf(const char *f,T...){output.emplace_back(f);}
}Serial;
namespace Controls {enum class Page:uint8_t{Main,History,Settings,Edit,Wipe,Battery,Tournament};}
static struct {Controls::Page page=Controls::Page::Main;}controls;
static bool wipeBlocked=false,dirty=false,diagnostics=false,tournamentMode=false,historyPage=false,motionDemo=false;
static bool stress=false,extPower=false,powerKeyRequested=false;
static uint32_t powerKeyRequestedAt=0,uiStalls=0;
struct Snapshot {uint32_t launches=3;};
enum class Action{Pin,Reset,IdleCheckpointBegin,CheckpointEnd};
static struct {bool request(Action a,uint32_t=0){act(a==Action::Pin?"pin":a==Action::Reset?"reset":"checkpoint");return true;}}acquisition;
namespace SettingsSave {enum Item:uint16_t{Pin=1,ExtPower=2};}
static struct {void mark(uint16_t){act("setting");}}settingsSave;
static struct {void touch(uint32_t t){if(t+1000<millis())act("sleep");}uint32_t timeout()const{return 180000;}}inactivity;
static struct {bool lock(){return true;}void unlock(){}bool quiet(uint32_t){act("imu_quiet");return true;}}imuAcquisition;
static struct {bool arm(){return true;}bool restore(){return true;}}shakeWake;
static void noteMotionRestoreFailure(){}
struct IdleFake {template<class F>bool run(F fn){return fn();}};
static IdleFake &pm1Idle(){static IdleFake idle;return idle;}
static struct {struct {void setExtOutput(bool){act("ext_power");}}Power;}M5;
// Restore: the real receiver takes base64 lines and '.'; any other byte ends it.
static struct Receiver {
  bool receiving=false;std::string text;unsigned received=0,errors=0;
  bool active()const{return receiving;}void cancel(){receiving=false;}
  void start(uint32_t){receiving=true;text.clear();act("restore_start");}
}restoreRx;
static void startRestore(){restoreRx.start(0);}
// Theme requests: the same framing as a restore.
namespace Themes {struct Request {static constexpr unsigned BYTES=44;};}
static struct ThemeReceiver {
  bool receiving=false;std::string text;unsigned received=0,errors=0;
  bool active()const{return receiving;}void cancel(){receiving=false;}
  void start(uint32_t){receiving=true;text.clear();act("theme_start");}
}themeRx;
static void themeReply(){}
static void listThemes(){act("theme_list");}
static void pollThemeRequest(const Snapshot &) {
  while(themeRx.active() && Serial.available()) {
    const char c=Serial.read();
    const bool b64=(c>='A' && c<='Z')||(c>='a' && c<='z')||(c>='0' && c<='9')||c=='+'||c=='/'||c=='=';
    if(c=='.'){themeRx.receiving=false;++themeRx.received;continue;}
    if(b64 || c=='\n'){themeRx.text+=c;continue;}
    themeRx.receiving=false;++themeRx.errors;
  }
}
// Milestone requests: the same framing again.
namespace Milestones {struct Request {static constexpr unsigned BYTES=40;};}
static struct MilestoneReceiver {
  bool receiving=false;std::string text;unsigned received=0,errors=0;
  bool active()const{return receiving;}void cancel(){receiving=false;}
  void start(uint32_t){receiving=true;text.clear();act("milestone_start");}
}milestoneRx;
static void milestoneReply(){}
static void listMilestones(){act("milestone_list");}
static void pollMilestoneRequest(const Snapshot &) {
  while(milestoneRx.active() && Serial.available()) {
    const char c=Serial.read();
    const bool b64=(c>='A' && c<='Z')||(c>='a' && c<='z')||(c>='0' && c<='9')||c=='+'||c=='/'||c=='=';
    if(c=='.'){milestoneRx.receiving=false;++milestoneRx.received;continue;}
    if(b64 || c=='\n'){milestoneRx.text+=c;continue;}
    milestoneRx.receiving=false;++milestoneRx.errors;
  }
}
// Loaded-effect requests: the same framing again.
static struct EffectReceiver {
  bool receiving=false;std::string text;unsigned received=0,errors=0;
  bool active()const{return receiving;}void cancel(){receiving=false;}
}effectRx;
static void listEffects(){act("effect_list");}
static void startEffectRequest(){effectRx.receiving=true;effectRx.text.clear();act("effect_start");}
static void pollEffectRequest(const Snapshot &) {
  while(effectRx.active() && Serial.available()) {
    const char c=Serial.read();
    const bool b64=(c>='A' && c<='Z')||(c>='a' && c<='z')||(c>='0' && c<='9')||c=='+'||c=='/'||c=='=';
    if(c=='.'){effectRx.receiving=false;++effectRx.received;continue;}
    if(b64 || c=='\n'){effectRx.text+=c;continue;}
    effectRx.receiving=false;++effectRx.errors;
  }
}
static void pollRestore(const Snapshot &) {
  while(restoreRx.active() && Serial.available()) {
    const char c=Serial.read();
    const bool b64=(c>='A' && c<='Z')||(c>='a' && c<='z')||(c>='0' && c<='9')||c=='+'||c=='/'||c=='=';
    if(c=='.'){restoreRx.receiving=false;++restoreRx.received;continue;}
    if(b64 || c=='\n'){restoreRx.text+=c;continue;}
    restoreRx.receiving=false;++restoreRx.errors;
  }
}
namespace DeviceBackup {constexpr unsigned TEXT_BYTES=5248;}
namespace Appearance {enum class Effect:uint8_t{Orbit};inline bool drawn(unsigned d){return d<6;}inline bool celebrates(unsigned d){return drawn(d);}}
static Appearance::Effect celebrationEffect(){return Appearance::Effect::Orbit;}
static void previewBestEffect(Appearance::Effect){act("effect");}
struct Motion {};static Motion demoMotion;
namespace MotionUI {inline void makeDemo(Motion&,bool){act("demo");}}
struct LaunchFeedback {enum class Replay {Twice,Loop};};
namespace MotionReplay {template<typename T>static uint32_t playbackDurationMs(const T &){return 0;}}
static struct {template<typename... T>void show(T...){}}launchFeedback;
// Exports print for tens of milliseconds while input keeps queuing.
static void status(){act("status");clockMs+=40;}static void exportPowerLog(){act("power_log");clockMs+=60;}
static void exportCpuPower(){act("cpu_power");clockMs+=40;}
static void exportBackup(){act("backup");clockMs+=60;}static void cancelWipe(){act("cancel_wipe");}
static void buttonA(){act("button");}static void holdA(){act("button");}static void holdB(){act("button");}
static void noteWearTap(const char *){}
static void toggleHistory(){act("history");}static void nextView(){act("view");}static void exportMotion(){act("export");}
static void cancelSettingEdit(){}static void syncNavigation(){}static void newSession(){act("session");}
static void exportHistory(){act("export");}static void previewHistory(){act("view");}
static void checkStorage(){act("storage");}static void checkMotionStorage(){act("storage");}
static void exportScreen(){act("screen");}static void exportSignal(){act("export");}
#if LAUNCHLAB_HIL
// The HIL line parser: consumes "!..." lines and records them.
static std::vector<std::string> hilLines;
static bool hilFeed(char c){static bool on=false;static std::string line;
  if(!on){if(c!='!')return false;on=true;line.clear();return true;}
  if(c=='\n'){on=false;hilLines.push_back(line);return true;}line+=c;return true;}
#endif
'''

TESTS = r'''
// The loop: a pollCommands pass reading up to 32 queued bytes, then 5 ms
// plus a redraw of up to 30 ms (max_draw_us reached 28 ms on the M5).
static uint32_t loopRng=11;
static void pass(){pollCommands(Snapshot{});loopRng=loopRng*1664525u+1013904223u;clockMs+=5+(loopRng>>24)%31;}
static void send(const std::string &bytes){for(char c:bytes)Serial.input.push_back(c);while(Serial.available())pass();}
static void idle(unsigned ms){for(unsigned t=0;t<ms;t+=5)pass();}
static void reset(){acted.clear();Serial.input.clear();Serial.output.clear();restoreRx={};themeRx={};milestoneRx={};effectRx={};idle(100);}
static unsigned harmful() {
  unsigned n=0;
  for(auto &[what,count]:acted)
    if(what!="status" && what!="power_log" && what!="cpu_power" && what!="backup" && what!="restore_start")n+=count;
  return n;
}
int main() {
  // Random bursts at USB speed with runs of every dangerous letter inside.
  uint32_t state=2026101702u;auto next=[&]{state=state*1664525u+1013904223u;return uint8_t(state>>24);};
  for(unsigned trial=0;trial<24;++trial) {
    reset();
    for(unsigned burst=0;burst<12;++burst) {
      std::string bytes;
      if(burst%3==1)bytes+="OSxgiaodhceQEqbnutyzvwjsLTMU"[next()%28];
      bytes+=char(0x80|next());
      for(unsigned i=0;i<2048;++i) {
        const uint8_t r=next();
        if(r<10)bytes+=std::string("OOSSxxnnggaa\n")+"OSxgiaodhceQEqbnutyzvwjsLTMU"[next()%28]+"\n";
        else if(r<18)bytes+=std::string(1,"PpKC"[r%4])+"OSxgiaodhceQEqbnutyzvwjsLTMU"[next()%28]+"\n";
        else bytes+=char(next());
      }
      if(burst%3==2)bytes+=std::string("\n")+"OSxgiaodhceQEqbnutyzvwjsLTMU"[next()%28];
      send(bytes);idle(60+next());
    }
    if(harmful()){for(auto &[w,n]:acted)std::cerr<<w<<"="<<n<<"\n";}
    assert(!harmful());
  }
  std::cout<<"random streams: no power-off, sleep, setting, session, button or export action\n";
  // The beywatch updater: K, then R + base64 lines + '.'.
  reset();send("K");idle(100);assert(acted["backup"]==1 && !harmful());
  send("R");send("QUJDRA==\nRUZHSA==\n.\n");idle(50);
  assert(acted["restore_start"]==1 && restoreRx.received==1 && restoreRx.text=="QUJDRA==\nRUZHSA==\n" && !harmful());
  // The theme editor: L lists, T takes one framed request, each sent standalone.
  reset();send("L\n");idle(50);assert(acted["theme_list"]==1);
  reset();send("T\n");idle(50);assert(acted["theme_start"]==1 && themeRx.active());
  send("TExRAQEA\n.\n");idle(50);assert(!themeRx.active() && themeRx.received==1 && themeRx.text=="TExRAQEA\n" && !themeRx.errors);
  // Inside a burst neither runs.
  reset();send("xxLxxTxx");idle(50);assert(!acted.count("theme_list") && !acted.count("theme_start"));
  // The milestone editor: M lists, U takes one framed request, the same way.
  reset();send("M\n");idle(50);assert(acted["milestone_list"]==1);
  reset();send("U\n");idle(50);assert(acted["milestone_start"]==1 && milestoneRx.active());
  send("TExVAQAA\n.\n");idle(50);assert(!milestoneRx.active() && milestoneRx.received==1 && milestoneRx.text=="TExVAQAA\n" && !milestoneRx.errors);
  reset();send("xxMxxUxx");idle(50);assert(!acted.count("milestone_list") && !acted.count("milestone_start"));
  // Loaded effects: F lists, X takes one framed request, the same way.
  reset();send("F\n");idle(50);assert(acted["effect_list"]==1);
  reset();send("X\n");idle(50);assert(acted["effect_start"]==1 && effectRx.active());
  send("TExYAQAA\n.\n");idle(50);assert(!effectRx.active() && effectRx.received==1 && effectRx.text=="TExYAQAA\n" && !effectRx.errors);
  reset();send("xxFxxXxx");idle(50);assert(!acted.count("effect_list") && !acted.count("effect_start"));
  std::cout<<"updater: K backup and R restore lines intact\n";
  // Tools: a bare letter after a pause, scripted lines, typed keys.
  reset();send("w");idle(30);assert(acted["export"]==1);
  reset();send("h\nh\n");assert(acted["history"]==2);
  reset();send("q\np\n");assert(acted["demo"]==1 && acted["status"]==1);
  reset();send("g");idle(300);send("7\n");assert(acted["pin"]==1 && acted["setting"]==1);
  reset();send("qp");idle(30);assert(!acted["demo"] && acted["status"]==1);
  std::cout<<"tools: bare, scripted, typed; glued letters run only the status letter\n";
#if LAUNCHLAB_HIL
  reset();send("p!hil\n");assert(acted["status"]==1 && hilLines.back()=="hil");
  reset();send("!home\n");send("v");idle(30);assert(acted["screen"]==1 && hilLines.back()=="home");
  std::cout<<"HIL rig: 'p' then '!hil' polled; a command right after a HIL line runs\n";
#endif
  std::cout<<"PASS: actual pollCommands rejects stray bytes and keeps the updater, tools"
#if LAUNCHLAB_HIL
    " and HIL rig"
#endif
    " working\n";
}
'''


def main():
    source = SOURCE.read_text()
    bodies = function(source, "runUsbCommand") + "\n" + function(source, "pollCommands")
    with tempfile.TemporaryDirectory(prefix="launchlab-usb-commands-") as temp:
        for hil in (0, 1):
            cpp = Path(temp) / f"test{hil}.cpp"
            cpp.write_text(f"#define LAUNCHLAB_HIL {hil}\n" + PREFIX + bodies + TESTS)
            exe = cpp.with_suffix("")
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                            "-I" + str(ROOT / "LaunchLabMini"), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


main()
