#!/usr/bin/env python3
"""Run the sketch's actual list replies: BLE Get options and the USB `M` and `L` lists.

BleCommands::options must return for every Best effect, including a saved
one the effect set no longer offers (effect_set.h keeps it selected), and
list each choice once: through 0.20.10 its cycle never came back to such an
effect and froze the loop task. Every `data=` field must carry the whole
record: `M` printed only the last of the four base64 lines of its 232-byte
milestone record.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "LaunchLabMini/LaunchLabMini.ino"


def function(source, name, result):
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
#include "controls.h"
#include "device_backup.h"
#include "effect_set.h"
#include "milestones.h"
#include "theme_library.h"
#include <cassert>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>
#define LOOP_HELPER
struct SerialFake {
  std::vector<std::string> lines;
  void println(const char *s){lines.emplace_back(s);}
  void printf(const char *f,...){char b[2048];va_list a;va_start(a,f);std::vsnprintf(b,sizeof(b),f,a);va_end(a);lines.emplace_back(b);}
}Serial;
static Appearance::Config appearance;
static EffectSet::Config effectSet;
static Themes::Library themeLibrary;
static Milestones::Config milestones;
static struct {bool ready()const{return true;}bool damaged()const{return false;}unsigned neededEntries(const Milestones::Config &)const{return 65;}}milestoneStore;
static struct {int status()const{return 0;}unsigned neededEntries()const{return 4;}}themeStore;
namespace ThemeStore {inline const char *name(int){return "ok";}}
static bool nvsAvailable(unsigned &n){n=150;return true;}
static const char *milestoneStoreName(){return "ok";}
static const char *effectSetFields(){return " effect_set=0";}
static const Appearance::Effect *milestonePicks(){static Appearance::Effect p[Milestones::MAX_ROWS]={};return p;}
static void milestoneReply(){}
static void themeReply(){}
// Labels are the renderer's business (test_controls_ui); here only their framing.
namespace ControlsUI {inline void protocolValue(char *out,unsigned length,unsigned,unsigned v){std::snprintf(out,length,"choice %u",v);}}
'''

TESTS = r'''
static volatile unsigned asked=0;
static void hung(int){char m[96];const int n=std::snprintf(m,sizeof(m),"FAIL: BleCommands::options did not return for Best effect %u\n",asked);
  if(write(2,m,size_t(n))){}_exit(1);}
using Appearance::Effect;
// Get options for Best effect: the values in order; each record is value, length, label.
static std::vector<unsigned> bestOptions(Effect current,size_t capacity=512) {
  appearance.effect=current;asked=unsigned(current);uint8_t out[512];alarm(5);
  const size_t n=options(unsigned(Controls::Setting::BestEffect),out,capacity);alarm(0);
  assert(n<=capacity);std::vector<unsigned> values;
  for(size_t at=0;at<n;at+=2+out[at+1]) {
    char label[24];ControlsUI::protocolValue(label,sizeof(label),unsigned(Controls::Setting::BestEffect),out[at]);
    assert(at+2+out[at+1]<=n && out[at+1]==std::strlen(label) && !std::memcmp(out+at+2,label,out[at+1]));
    values.push_back(out[at]);
  }
  return values;
}
static std::vector<unsigned> ids(std::initializer_list<Effect> e){std::vector<unsigned> v;for(Effect x:e)v.push_back(unsigned(x));return v;}
static std::string field(const std::string &line,const char *name) {
  const std::string key=std::string(" ")+name+"=";const size_t at=line.find(key);assert(at!=std::string::npos);
  const size_t from=at+key.size();return line.substr(from,line.find(' ',from)-from);
}
static std::vector<uint8_t> decoded(const std::string &text) {
  std::vector<uint8_t> out(text.size());size_t n=0;
  assert(DeviceBackup::base64Decode(text.data(),text.size(),out.data(),out.size(),n));out.resize(n);return out;
}
int main() {
  std::signal(SIGALRM,hung);
  using EffectSet::flag;
  const EffectSet::Mask mine=flag(Effect::Slash)|flag(Effect::Crimson)|flag(Effect::Ravage);
  assert(effectSet.set(mine)==EffectSet::Problem::None);
  // An offered effect, Random and Off: the cycle from it, once round.
  assert(bestOptions(Effect::Slash)==ids({Effect::Slash,Effect::Crimson,Effect::Ravage,Effect::Off,Effect::Random}));
  assert(bestOptions(Effect::Random)==ids({Effect::Random,Effect::Slash,Effect::Crimson,Effect::Ravage,Effect::Off}));
  assert(bestOptions(Effect::Off)==ids({Effect::Off,Effect::Random,Effect::Slash,Effect::Crimson,Effect::Ravage}));
  // A saved effect outside the set: listed first, then every offered choice once.
  assert(bestOptions(Effect::Tidal)==ids({Effect::Tidal,Effect::Crimson,Effect::Ravage,Effect::Off,Effect::Random,Effect::Slash}));
  assert(bestOptions(Effect::Orbit)==ids({Effect::Orbit,Effect::Slash,Effect::Crimson,Effect::Ravage,Effect::Off,Effect::Random}));
  // Every stored value as the current choice, with the default and a one-effect set:
  // it returns, starts with that value and lists no value twice.
  for(EffectSet::Mask m:{EffectSet::DEFAULT_MASK,EffectSet::ALL,flag(Effect::Bloom)}) {
    assert(effectSet.set(m)==EffectSet::Problem::None);
    for(unsigned v=0;v<Appearance::EFFECTS;++v) {
      const auto got=bestOptions(Effect(v));assert(!got.empty() && got[0]==v);
      for(size_t i=0;i<got.size();++i)for(size_t j=i+1;j<got.size();++j)assert(got[i]!=got[j]);
    }
  }
  // A short reply buffer only shortens the list.
  assert(effectSet.set(mine)==EffectSet::Problem::None);
  assert(bestOptions(Effect::Tidal,20).size()<6);
  std::cout<<"Get options: Best effect lists each choice once, from any current effect, offered or not\n";
  // `M`: data= is the whole v3 record (232 bytes, four 96-character base64 lines).
  Milestones::Config::Image image;milestones.encode(image);static_assert(Milestones::Config::IMAGE_BYTES>72,"a record of several base64 lines");
  Serial.lines.clear();listMilestones();
  assert(Serial.lines.front().rfind("MILESTONE_LIST version=3 ",0)==0 && Serial.lines.back()=="MILESTONE_LIST_END");
  const auto record=decoded(field(Serial.lines.front(),"data"));
  assert(record.size()==image.size() && std::equal(record.begin(),record.end(),image.begin()));
  Milestones::Config back;assert(back.decode(record.data(),record.size()));
  // `L`: each slot's data= is its whole 32-byte record.
  Serial.lines.clear();listThemes();
  unsigned slots=0;
  for(const auto &line:Serial.lines)if(line.rfind("THEME_SLOT ",0)==0) {
    const auto raw=decoded(field(line,"data"));
    assert(raw.size()==Themes::SLOT_BYTES && !std::memcmp(raw.data(),themeLibrary.raw(slots),raw.size()));++slots;
  }
  assert(slots==Themes::SLOTS);
  std::cout<<"PASS: actual BleCommands::options returns every Best effect choice once; M and L data= decode to the whole records\n";
}
'''


def main():
    source = SOURCE.read_text()
    bodies = "\n".join([function(source, "putOption", "size_t"), function(source, "options", "size_t"),
                        function(source, "listMilestones", "void"), function(source, "listThemes", "void")])
    with tempfile.TemporaryDirectory(prefix="launchlab-listing-") as temp:
        cpp = Path(temp) / "test.cpp"
        cpp.write_text(PREFIX + bodies + TESTS)
        exe = cpp.with_suffix("")
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O1", "-g",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                        *("-I" + str(ROOT / d) for d in ("tests/fakes", "LaunchLabRpm", "LaunchLabMini")), str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


main()
