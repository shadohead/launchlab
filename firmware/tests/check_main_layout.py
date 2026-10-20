#!/usr/bin/env python3
"""Exercise extracted main-screen layout with synthetic states and geometry.

This host contract test uses conservative font envelopes, not raster pixels.
It extracts the production status and drawing functions.
"""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "firmware/LaunchLabMini/LaunchLabMini.ino").read_text()
acquisition = (root / "firmware/LaunchLabMini/acquisition.h").read_text()

def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for cursor in range(opening, len(text)):
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        if depth == 0:
            return text[start:cursor + 1]
    raise RuntimeError("Unbalanced production function")

start = source.index("    const bool sessionNotice=")
end = source.index("\n  }\n  if(!tournamentMode", start)
main = source[start:end]
prefix = r'''
#include "appearance.h"
#include "analog_tachometer.h"
#include "level_indicator.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>
static constexpr uint16_t TFT_BLACK=0,TFT_WHITE=0xffff,TFT_DARKGREY=0x7bef,TFT_ORANGE=0xfd20,TFT_GREEN=0x07e0;
static constexpr int top_center=0,top_left=1;
namespace fonts {static int Font6=6,Font4=4,Font2=2;}
namespace lgfx {
class LGFXBase {
public:
  struct Text {std::string value; int x,y,font; uint16_t color;};
  std::vector<Text> texts;
  std::vector<std::pair<int,int>> bubbles;
  uint16_t color=0;
  int width()const{return 135;}
  void setTextDatum(int){}
  void setTextColor(uint16_t c,uint16_t){color=c;}
  int textWidth(const char *s,const int *font)const{return int(std::strlen(s))*(*font==6?30:*font==4?20:6);}
  void drawString(const char *s,int x,int y,int font){
    const int w=textWidth(s,&font),h=font==6?48:font==4?26:16;
    assert(x-w/2>=0 && x+(w+1)/2<=135 && y>=0 && y+h<=240);
    texts.push_back({s,x,y,font,color});
  }
  void drawCircle(int x,int y,int r,uint16_t){assert(x-r>=0 && x+r<135 && y-r>=0 && y+r<240);}
  void drawFastHLine(int x,int y,int w,uint16_t){assert(x>=0 && x+w<=135 && y>=0 && y<240);}
  void drawFastVLine(int x,int y,int h,uint16_t){assert(x>=0 && x<135 && y>=0 && y+h<=240);}
  void fillCircle(int x,int y,int r,uint16_t){drawCircle(x,y,r,0);bubbles.emplace_back(x,y);}
};
}
#include "palette.h"
#include "theme_library.h"
#include "screens_ui.h"
static uint32_t hostNow=2000,newSessionAt=0;
static uint32_t millis(){return hostNow;}
static StickS3Level level;static StickS3Level::Reading drawnLevel;
static bool flip=false;
static Appearance::Theme theme=Appearance::Theme::Classic;
static bool visibleDisplayFlipped(){return flip;}
static Appearance::Theme visibleTheme(){return theme;}
static uint16_t themeAccent(){return Appearance::accent(theme);}
static Palette::Colors ui(){return Themes::builtIn(theme);}
struct Snapshot {
  bool ready=true,suspended=false,resting=false,valid=false,signalFault=false;
  float resultRpm=0;
  AnalogTachometer::Phase phase=AnalogTachometer::Phase::Ready;
'''
prefix += function(acquisition, "const char *stateName() const") + "\n};\n"
prefix += function(source, "static void drawLiveLevel(")
prefix += "\nstatic void drawMain(lgfx::LGFXBase &view,const Snapshot &s){const int cx=67;const Palette::Colors colors=ui();" + main + "\n}\n"
tests = r'''
int main(){
  unsigned cases=0;
  for(unsigned state=0;state<9;++state)for(unsigned rpm=0;rpm<3;++rpm)
  for(bool reversed:{false,true})for(unsigned palette=0;palette<Appearance::THEMES;++palette)
  for(unsigned feed=0;feed<5;++feed)for(unsigned notice:{0u,1000u,1500u}){
    Snapshot s;s.suspended=state==0 || state==8;s.resting=state==8;s.ready=state!=1;s.signalFault=state==2;
    s.phase=state==3?AnalogTachometer::Phase::Paused:state==4?AnalogTachometer::Phase::Settling:
      state==6?AnalogTachometer::Phase::Launch:state==7?AnalogTachometer::Phase::Hold:AnalogTachometer::Phase::Ready;
    s.valid=rpm!=0;s.resultRpm=rpm==1?8123:99999;
    flip=reversed;theme=Appearance::Theme(palette);newSessionAt=notice?hostNow-notice:0;level={};
    if(feed==0)level.feed(0,0,1,hostNow);
    if(feed==1)level.feed(.5f,0,std::sqrt(.75f),hostNow);
    if(feed==2)level.feed(.35355339f,.35355339f,std::sqrt(.75f),hostNow);
    if(feed==3)level.feed(0,0,0,hostNow);
    if(feed==4)level.feed(0,0,1,hostNow-StickS3Level::STALE_MS-1);
    lgfx::LGFXBase g;drawMain(g,s);
    assert(g.texts.size()==4);
    const bool shown=notice && notice<1500 && state==5;
    assert(g.texts[0].value==(shown?"New session ready":s.stateName()));
    assert(g.texts[0].y==8 && g.texts[0].font==2);
    assert(g.texts[1].value==(rpm==0?"--":rpm==1?"8123":"99999") && g.texts[1].y==47);
    assert(g.texts[1].font==(rpm==2?4:6));
    assert(g.texts[1].color==(palette==0?TFT_WHITE:Appearance::accent(theme)));
    assert(g.texts[2].value=="RPM" && g.texts[2].y==99 && g.texts[2].font==2);
    assert(g.texts[3].y==207 && g.texts[3].font==2);
    assert(g.bubbles.size()==(feed<3?1u:0u));
    if(drawnLevel.valid())assert(std::hypot(drawnLevel.right*32,drawnLevel.down*32)+5<=38.001f);
    ++cases;
  }
  std::cout<<"PASS: "<<cases<<" extracted production main-screen layout states, small status, notice gating, RPM fallback/accent, enlarged level bounds, both flips and all ten themes\n";
}
'''
with tempfile.TemporaryDirectory(prefix="launchlab-main-layout-") as directory:
    build=Path(directory); cpp=build/"test.cpp"; exe=build/"test"
    cpp.write_text(prefix+tests)
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++17","-O1","-g","-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer","-I"+str(root/"firmware/tests/fakes"),
                    "-I"+str(root/"firmware/LaunchLabMini"),"-I"+str(root/"firmware/LaunchLabRpm"),
                    str(cpp),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
