// Bounds checks for the actual compact renderers. Device fonts are inspected
// after flashing; these deliberately conservative host metrics catch overflow.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <vector>
static constexpr uint16_t TFT_BLACK=0,TFT_WHITE=1,TFT_DARKGREY=2,TFT_CYAN=3,TFT_ORANGE=4;
static constexpr int top_center=0;
namespace fonts {static int Font4=4,Font2=2;}
namespace lgfx {
struct LGFXBase {
  unsigned strings=0,footers=0,cycleHints=0;
  uint16_t ink=0;
  std::vector<int> valueEdges;
  std::vector<int> valueFonts;
  int width(){return 135;}
  void setTextDatum(int){}void setTextColor(uint16_t color,uint16_t){ink=color;}
  void fillRect(int x,int y,int w,int h,uint16_t){assert(x>=0 && y>=0 && x+w<=135 && y+h<=240);}
  void drawRoundRect(int x,int y,int w,int h,int,uint16_t){fillRect(x,y,w,h,0);}
  int textWidth(const char *text,const int *font){return int(std::strlen(text))*(*font==4?18:8);}
  void drawString(const char *text,int x,int y,int font) {
    const int glyph=font==4?18:font==2?8:6,height=font==4?26:font==2?16:8;
    const int width=int(std::strlen(text))*glyph;
    assert(x-width/2>=0 && x+(width+1)/2<=135 && y>=0 && y+height<=240);
    ++strings;if(y==231)++footers;
    if(!std::strcmp(text,"B next option ->")){assert(y==139 && ink==0x39e7);++cycleHints;}
    else if(y>=86 && y<=95){valueEdges.push_back(x-width/2);valueEdges.push_back(x+(width+1)/2);valueFonts.push_back(font);}
  }
};
}
#include "controls_ui.h"
int main() {
  using namespace Controls;unsigned renders=0;
  for(unsigned row=0;row<6;++row){lgfx::LGFXBase g;ControlsUI::list(g,"Menu",ControlsUI::MENU,6,row);assert(g.footers==1);++renders;}
  for(unsigned row=0;row<4;++row){lgfx::LGFXBase g;ControlsUI::list(g,"History",ControlsUI::HISTORY,4,row);assert(g.footers==1);++renders;}
  uint8_t values[]={10,0,100,1,0,0,0,2,0};
  for(unsigned row=0;row<=SETTING_COUNT;++row){lgfx::LGFXBase g;State state;state.setting=row;ControlsUI::settings(g,state,values);assert(g.footers==1);++renders;}
  // Lowest and highest draft for each setting. A named array keeps the values
  // alive for the loop; a conditional initializer_list would dangle.
  const unsigned extremes[][2]={{1,10},{0,1},{10,100},{0,1},{0,1},{0,Appearance::THEMES-1},{0,6},{0,2},{0,1}};
  static_assert(sizeof(extremes)/sizeof(extremes[0])>=EDITABLE_SETTING_COUNT,"one range per editable setting");
  for(unsigned row=0;row<EDITABLE_SETTING_COUNT;++row)for(unsigned value:extremes[row]) {
    lgfx::LGFXBase g;State state;state.setting=row;state.draft=value;
    ControlsUI::editor(g,state,true,row==1 || row==4);
    assert(g.footers==1 && g.cycleHints==1);
    assert(g.valueEdges.size()==2 && g.valueEdges[0]>=8 && g.valueEdges[1]<=127);++renders;
  }
  WipeConfirmation wipe;wipe.begin(1000);
  for(unsigned stage=0;stage<3;++stage) {
    wipe.tick(1001+stage,false,false);
    for(bool failed:{false,true})for(bool blocked:{false,true}) {
      lgfx::LGFXBase g;ControlsUI::wipe(g,wipe,1001+stage,failed,blocked,false);assert(g.footers==1);++renders;
    }
    if(stage<2)assert(wipe.confirm(1001+stage));
  }
  wipe.tick(1010,false,false);wipe.tick(1011,true,false);
  for(unsigned ms=1012;ms<4011;ms+=100) {
    assert(!wipe.tick(ms,true,false));lgfx::LGFXBase g;ControlsUI::wipe(g,wipe,ms,false,false,false);assert(g.footers==1);++renders;
  }
  wipe.cancel();
  for(bool complete:{false,true}){lgfx::LGFXBase g;ControlsUI::wipe(g,wipe,5000,true,true,complete);assert(g.footers==1);++renders;}
  // Every named palette value stays visible in its Settings row and editor.
  for(unsigned theme=0;theme<Appearance::THEMES;++theme) {
    State state;state.setting=unsigned(Setting::Theme);state.draft=theme;values[state.setting]=theme;
    lgfx::LGFXBase row;ControlsUI::settings(row,state,values);assert(row.footers==1);++renders;
    lgfx::LGFXBase editor;ControlsUI::editor(editor,state,false,false);
    assert(editor.footers==1 && editor.cycleHints==1 && editor.valueEdges.size()==2);
    assert(editor.valueFonts.size()==1 && editor.valueFonts[0]==2);
    assert(editor.valueEdges[0]>=8 && editor.valueEdges[1]<=127);++renders;
  }
  // Custom theme names (up to 14 characters) keep the built-ins' size when
  // they fit and drop to the small font when not, inside the card either way.
  for(const char *name:{"Dragoon Storm","WWWWWWWWWWWWWW","A"}) {
    static const char *shown;shown=name;
    ControlsUI::themeLabel=[](Appearance::Theme t){return Appearance::custom(t)?shown:Appearance::label(t);};
    State state;state.setting=unsigned(Setting::Theme);state.draft=uint8_t(Appearance::THEMES);values[state.setting]=state.draft;
    lgfx::LGFXBase row;ControlsUI::settings(row,state,values);assert(row.footers==1);++renders;
    lgfx::LGFXBase editor;ControlsUI::editor(editor,state,false,false);
    assert(editor.valueFonts.size()==1 && editor.valueFonts[0]==(std::strlen(name)*8<=119?2:1));
    assert(editor.valueEdges[0]>=8 && editor.valueEdges[1]<=127);++renders;
  }
  ControlsUI::themeLabel=nullptr;values[unsigned(Setting::Theme)]=0;
  for(bool start:{false,true}){lgfx::LGFXBase g;ControlsUI::session(g,start);assert(g.footers==1);++renders;}
  for(const char *label:{"Enter tournament","Exit tournament","Cancel change","Back"})for(unsigned elapsed:{0u,999u,1000u,10000u}) {
    lgfx::LGFXBase g;ControlsUI::hold(g,label,elapsed,1000);assert(g.footers==1);++renders;
  }
  std::cout<<"PASS: "<<renders<<" actual menu/editor/confirmation/hold renders fit135x240; one-line footers, conservative glyph bounds\n";
}
