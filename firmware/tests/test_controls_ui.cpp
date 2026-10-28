// Bounds checks for the actual compact renderers. Device fonts are inspected
// after flashing; these deliberately conservative host metrics catch overflow.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <array>
#include <iostream>
#include <string>
#include <vector>
static constexpr uint16_t TFT_BLACK=0,TFT_WHITE=1,TFT_DARKGREY=2,TFT_CYAN=3,TFT_ORANGE=4;
static constexpr int top_center=0,top_left=1,top_right=2;
namespace fonts {static int Font4=4,Font2=2;}
namespace lgfx {
struct LGFXBase {
  unsigned strings=0,footers=0,cycleHints=0;
  uint16_t ink=0;
  int datum=top_center;
  std::vector<int> valueEdges;
  std::vector<int> valueFonts;
  std::vector<std::array<int,4>> spans; // left, right, y and font of every string
  std::vector<std::string> texts;
  bool drew(const char *text)const{return std::find(texts.begin(),texts.end(),text)!=texts.end();}
  int width(){return 135;}
  void setTextDatum(int d){datum=d;}void setTextColor(uint16_t color,uint16_t){ink=color;}
  void fillRect(int x,int y,int w,int h,uint16_t){assert(x>=0 && y>=0 && x+w<=135 && y+h<=240);}
  void drawRoundRect(int x,int y,int w,int h,int,uint16_t){fillRect(x,y,w,h,0);}
  int textWidth(const char *text,const int *font){return int(std::strlen(text))*(*font==4?18:8);}
  // Font 2 is proportional: 8 px a glyph, but 5 for i, l, space and narrow
  // punctuation. Every glyph's estimate is at or above M5GFX Font2's own
  // width: "String wear check" draws 110 px and estimates 127, "Tournament"
  // 69 and 80. check_ui_render pins the real pixels.
  static int font2Width(const char *text) {
    int width=0;
    for(const char *c=text;*c;++c)width+=std::strchr("il .,:;'!|",*c)?5:8;
    return width;
  }
  void drawString(const char *text,int x,int y,int font) {
    const int glyph=font==4?18:6,height=font==4?26:font==2?16:8;
    const int width=font==2?font2Width(text):int(std::strlen(text))*glyph;
    const int left=datum==top_left?x:datum==top_right?x-width:x-width/2;
    const int right=datum==top_left?x+width:datum==top_right?x:x+(width+1)/2;
    assert(left>=0 && right<=135 && y>=0 && y+height<=240);
    spans.push_back({left,right,y,font});texts.push_back(text);
    ++strings;if(y==231)++footers;
    if(!std::strcmp(text,"B next option ->")){assert(y==139 && ink==0x39e7);++cycleHints;}
    else if(y>=86 && y<=95){valueEdges.push_back(left);valueEdges.push_back(right);valueFonts.push_back(font);}
  }
};
}
#include "controls_ui.h"
int main() {
  using namespace Controls;unsigned renders=0;
  for(unsigned row=0;row<MENU_COUNT;++row){lgfx::LGFXBase g;ControlsUI::list(g,"Menu",ControlsUI::MENU,MENU_COUNT,row);assert(g.footers==1);++renders;}
  for(unsigned row=0;row<4;++row){lgfx::LGFXBase g;ControlsUI::list(g,"History",ControlsUI::HISTORY,4,row);assert(g.footers==1);++renders;}
  uint8_t values[]={10,0,100,1,0,0,0,2,2,0,1};
  for(unsigned row=0;row<=SETTING_COUNT;++row){lgfx::LGFXBase g;State state;state.setting=row;ControlsUI::settings(g,state,values);assert(g.footers==1);++renders;}
  // Lowest and highest draft for each setting. A named array keeps the values
  // alive for the loop; a conditional initializer_list would dangle.
  const unsigned extremes[][2]={{1,10},{0,1},{10,100},{0,1},{0,1},{0,Appearance::THEMES-1},{0,Appearance::EFFECTS-1},{0,2},{0,2},{0,1},{0,1}};
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
  // The Celebrate editor summarizes the milestone table: the defaults, one
  // row, and the widest thresholds the table allows, all inside the screen.
  {
    using Milestones::Row;using Appearance::Effect;
    Milestones::Config defaults,one,wide;
    const Row single[]={{12500,Effect::Slash}};assert(one.set(Milestones::Mode::Both,single,1)==Milestones::Problem::None);
    Row rows[Milestones::MAX_ROWS];for(unsigned i=0;i<Milestones::MAX_ROWS;++i)rows[i]={uint16_t(10001+1818*i),Effect::Orbit};
    assert(wide.set(Milestones::Mode::Milestones,rows,Milestones::MAX_ROWS)==Milestones::Problem::None);
    for(const Milestones::Config *table:{(const Milestones::Config *)nullptr,(const Milestones::Config *)&defaults,(const Milestones::Config *)&one,(const Milestones::Config *)&wide})for(unsigned mode=0;mode<Milestones::MODES;++mode) {
      ControlsUI::milestones=table;
      State state;state.setting=unsigned(Setting::Celebrate);state.draft=mode;values[state.setting]=mode;
      lgfx::LGFXBase row;ControlsUI::settings(row,state,values);assert(row.footers==1);++renders;
      lgfx::LGFXBase editor;ControlsUI::editor(editor,state,false,false);
      assert(editor.footers==1 && editor.cycleHints==1 && editor.valueEdges.size()==2);++renders;
    }
    ControlsUI::milestones=nullptr;
  }
  for(bool start:{false,true}){lgfx::LGFXBase g;ControlsUI::session(g,start);assert(g.footers==1);++renders;}
  {
    WearCheck::Session wear;
    const WearCheck::Tilt tilts[]={
      {false,0.0f,0.0f,0.0f},
      {true,0.8f,0.05f,-0.02f},
      {true,18.5f,0.8f,0.6f},
      {true,87.9f,0.0f,1.0f},
      {true,88.4f,0.0f,-1.0f}
    };
    for(const auto &t:tilts){lgfx::LGFXBase g;ControlsUI::wearCheck(g,wear,t,t);assert(g.footers==1);++renders;}
    // On target the label is the pull's step (0.20.4): PULL OUT only while
    // the detector would measure (WAIT in its rearm second), HOLD with the
    // string out, then LET GO.
    for(bool detectorReady:{true,false}) {
      lgfx::LGFXBase g;ControlsUI::wearCheck(g,wear,tilts[1],tilts[1],detectorReady);assert(g.footers==1);++renders;
      assert(g.drew("PULL OUT")==detectorReady && g.drew("WAIT")==!detectorReady && g.drew("Pull, hold, let go"));
    }
    {lgfx::LGFXBase g;ControlsUI::wearCheck(g,wear,tilts[2],tilts[2],false);assert(g.drew("TILT TO 0 DEG") && !g.drew("WAIT"));++renders;}
    {
      WearCheck::Session steps;uint64_t t=1000000;uint32_t turns=0;
      steps.observeOptics(t,turns,true);
      steps.observeOptics(t+=WearCheck::STILL_US,turns+=WearCheck::RUN_TURNS,true);
      for(bool detectorReady:{true,false}) {
        lgfx::LGFXBase g;ControlsUI::wearCheck(g,steps,tilts[1],tilts[1],detectorReady);++renders;
        assert(g.drew("HOLD") && !g.drew("WAIT") && !g.drew("PULL OUT"));
      }
      steps.observeOptics(t+=WearCheck::HOLD_US,turns,true);
      {lgfx::LGFXBase g;ControlsUI::wearCheck(g,steps,tilts[1],tilts[1],true);assert(g.drew("LET GO"));++renders;}
      {lgfx::LGFXBase g;ControlsUI::wearCheck(g,steps,tilts[2],tilts[2],true);assert(g.drew("TILT TO 0 DEG") && !g.drew("LET GO"));++renders;}
      // Marks after LET GO that never become a result: not measured.
      steps.observeOptics(t+=WearCheck::STILL_US,turns+=WearCheck::RUN_TURNS,true);
      assert(steps.observeOptics(t+=WearCheck::MISSED_US,turns,true)==WearCheck::Outcome::Missed);
      {lgfx::LGFXBase g;ControlsUI::wearCheck(g,steps,tilts[1],tilts[1],true);assert(g.drew("Retry: not measured") && g.drew("PULL OUT"));++renders;}
      steps.observeOptics(t+=WearCheck::STILL_US,turns+=WearCheck::RUN_TURNS,true);
      steps.observeOptics(t+=WearCheck::STILL_US,turns,false);
      assert(steps.observeOptics(t+=20000,turns+=WearCheck::RUN_TURNS,false)==WearCheck::Outcome::EarlyRelease);
      {lgfx::LGFXBase g;ControlsUI::wearCheck(g,steps,tilts[1],tilts[1],false);assert(g.drew("Retry: hold longer") && g.drew("WAIT"));++renders;}
    }
    uint32_t launches=0;
    // Rejections and stage transitions through all 3 stages and summary.
    wear.observePull(++launches,false,0,tilts[1]);
    {lgfx::LGFXBase g;ControlsUI::wearCheck(g,wear,tilts[1],tilts[1]);assert(g.footers==1);++renders;}
    wear.observePull(++launches,true,3200,tilts[0]);
    {lgfx::LGFXBase g;ControlsUI::wearCheck(g,wear,tilts[0],tilts[0]);assert(g.footers==1);++renders;}
    wear.observePull(++launches,true,3200,tilts[2]);
    {lgfx::LGFXBase g;ControlsUI::wearCheck(g,wear,tilts[2],tilts[2]);assert(g.footers==1);++renders;}
    for(unsigned i=0;i<3;++i){wear.observePull(++launches,true,3250+i*10,tilts[1]);lgfx::LGFXBase g;ControlsUI::wearCheck(g,wear,tilts[3],tilts[3]);assert(g.footers==1);++renders;}
    wear.observePull(++launches,true,2029,tilts[1]);
    {lgfx::LGFXBase g;ControlsUI::wearCheck(g,wear,tilts[1],tilts[1]);assert(g.footers==1);++renders;}
    wear.observePull(++launches,true,2029,tilts[3]);
    wear.observePull(++launches,true,2029,tilts[4]);
    {lgfx::LGFXBase g;ControlsUI::wearCheck(g,wear,tilts[4],tilts[4]);assert(g.footers==1);++renders;}
    wear.observePull(++launches,true,2040,tilts[3]);
    wear.observePull(++launches,true,2020,tilts[3]);
    for(unsigned i=0;i<3;++i){wear.observePull(++launches,true,3700+i*15,tilts[4]);lgfx::LGFXBase g;ControlsUI::wearCheck(g,wear,tilts[4],tilts[4]);assert(g.footers==1);++renders;}
    assert(wear.complete());
    // Every line of the result stays inside its 8..127 card, with five-digit pulls too.
    WearCheck::Session fast;uint32_t n=0;
    for(unsigned i=0;i<3;++i)fast.observePull(++n,true,12345+i*1111,tilts[1]);
    for(unsigned i=0;i<3;++i)fast.observePull(++n,true,29876-i*1111,tilts[3]);
    for(unsigned i=0;i<3;++i)fast.observePull(++n,true,10001+i,tilts[4]);
    assert(fast.complete());
    for(const auto *done:{&wear,&fast}) {
      lgfx::LGFXBase g;ControlsUI::wearCheck(g,*done,tilts[4],tilts[4]);assert(g.footers==1);++renders;
      // Font 1 (GLCD) is exactly 6 px a glyph; Font 2 widths here are only an upper bound.
      for(const auto &span:g.spans)if(span[3]==1 && span[2]>=31 && span[2]<213)assert(span[0]>=10 && span[1]<=125);
    }
  }
  for(const char *label:{"Enter tournament","Exit tournament","Cancel change","Back"})for(unsigned elapsed:{0u,999u,1000u,10000u}) {
    lgfx::LGFXBase g;ControlsUI::hold(g,label,elapsed,1000);assert(g.footers==1);++renders;
  }
  std::cout<<"PASS: "<<renders<<" actual menu/editor/confirmation/hold renders fit135x240; one-line footers, conservative glyph bounds\n";
}
