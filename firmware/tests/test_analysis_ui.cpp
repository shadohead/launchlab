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
namespace fonts {static int Font0=0,Font4=4,Font2=2,Font6=6;}
namespace lgfx {
struct LGFXBase {
  std::vector<std::array<int,5>> lines;
  void drawLine(int x0,int y0,int x1,int y1,uint16_t ink){lines.push_back({x0,y0,x1,y1,ink});}
  std::vector<std::array<int,3>> pixels;
  void drawPixel(int x,int y,uint16_t color){pixels.push_back({x,y,color});}
  void fillTriangle(int,int,int,int,int,int,uint16_t){}
  void drawTriangle(int,int,int,int,int,int,uint16_t){}
  std::vector<std::array<int,2>> loader;
  unsigned grayDots=0;
  void fillCircle(int x,int y,int radius,uint16_t color){if(y==185 && radius==3)loader.push_back({x,color});if(radius==1 && color==0x7bef)++grayDots;}
  void drawCircle(int x,int y,int r,uint16_t){assert(x-r>=0 && x+r<135 && y-r>=0 && y+r<240);++circles;}
  unsigned circles=0;
  void drawRect(int x,int y,int w,int h,uint16_t c){fillRect(x,y,w,h,c);}
  void drawFastHLine(int x,int y,int w,uint16_t c){fillRect(x,y,w,1,c);}
  void drawFastVLine(int x,int y,int h,uint16_t c){fillRect(x,y,1,h,c);}
  unsigned strings=0,footers=0,cycleHints=0;
  uint16_t ink=0,timerInk=0;
  int datum=top_center;
  std::vector<int> valueEdges;
  std::vector<int> valueFonts;
  std::vector<std::array<int,4>> spans; // left, right, y and font of every string
  std::vector<uint16_t> textColors;
  std::vector<std::string> texts;
  bool drew(const char *text)const{return std::find(texts.begin(),texts.end(),text)!=texts.end();}
  int width()const{return 135;}
  int height()const{return 240;}
  void setTextDatum(int d){datum=d;}void setTextColor(uint16_t color,uint16_t){ink=color;}
  std::vector<std::array<int,3>> dots; // 2x2 squares: leader and forecast dots
  void fillRect(int x,int y,int w,int h,uint16_t c){
    assert(x>=0 && y>=0 && x+w<=135 && y+h<=240);
    if(w==2 && h==2)dots.push_back({x,y,c});
    // The footer clears its background after the axes have been drawn.
    if(y==229)for(const auto &s:spans)assert(s[2]+(s[3]==6?48:s[3]==4?26:s[3]==2?16:8)<=y);
  }
  void drawRoundRect(int x,int y,int w,int h,int,uint16_t){fillRect(x,y,w,h,0);}
  void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){drawRoundRect(x,y,w,h,r,c);}
  void fillScreen(uint16_t c){fillRect(0,0,135,240,c);}
  static int font4Width(const char *text){int width=0;for(const char *p=text;*p;++p)width+=(*p>='0' && *p<='9')?14:*p==':'?7:*p==' '?5:*p=='R'?17:*p=='P'?16:*p=='M'?21:18;return width;}
  static int font6Width(const char *text){int width=0;for(const char *p=text;*p;++p)width+=*p==':'?12:27;return width;}
  int textWidth(const char *text,const int *font){return *font==4?font4Width(text):*font==6?font6Width(text):*font==2?font2Width(text):int(std::strlen(text))*8;}
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
    const int glyph=font==4?18:6,height=font==6?48:font==4?26:font==2?16:8;
    const int width=font==6?font6Width(text):font==4?font4Width(text):font==2?font2Width(text):int(std::strlen(text))*glyph;
    const int left=datum==top_left?x:datum==top_right?x-width:x-width/2;
    const int right=datum==top_left?x+width:datum==top_right?x:x+(width+1)/2;
    assert(left>=0 && right<=135 && y>=0 && y+height<=240);
    if(font==6)timerInk=ink;
    spans.push_back({left,right,y,font});texts.push_back(text);textColors.push_back(ink);
    ++strings;if(y==231)++footers;
    if(!std::strcmp(text,"B next option ->")){assert(y==139 && ink==0x39e7);++cycleHints;}
    else if(y>=86 && y<=95){valueEdges.push_back(left);valueEdges.push_back(right);valueFonts.push_back(font);}
  }
};
}

#include "analysis_ui.h"
#include "analysis_result_ui.h"
void checkOverlap(const lgfx::LGFXBase &g){
  for(unsigned i=0;i<g.spans.size();++i)for(unsigned j=i+1;j<g.spans.size();++j){auto a=g.spans[i],b=g.spans[j];int ah=a[3]==6?48:a[3]==4?26:a[3]==2?16:8,bh=b[3]==6?48:b[3]==4?26:b[3]==2?16:8;assert(a[1]<=b[0] || b[1]<=a[0] || a[2]+ah<=b[2] || b[2]+bh<=a[2]);}
}
int main(){
  {
    lgfx::LGFXBase graph;const auto palette=Palette::builtIn(0x07ff,true);
    AnalysisUI::timeMarker(graph,palette.text,26,130,164,207,60,156);
    assert(graph.pixels.size()==44);
    for(unsigned i=0;i<graph.pixels.size();++i){
      assert(graph.pixels[i][0]==66+int(i%2) && graph.pixels[i][1]==165+int(i/4)*4+int(i/2%2));
      assert(graph.pixels[i][2]==palette.text);
    }
    graph.pixels.clear();
    AnalysisUI::timeMarker(graph,palette.warning,26,130,164,207,NAN,156);
    AnalysisUI::timeMarker(graph,palette.warning,26,130,164,207,160,156);
    assert(graph.pixels.empty());
    auto light=palette;light.background=Palette::WHITE;light.text=Palette::BLACK;light.warning=0x001f;
    AnalysisUI::timeMarkers(graph,light,26,130,164,207,60,166,156);
    assert(graph.pixels.size()==88);
    for(unsigned i=0;i<44;++i)
      assert(graph.pixels[i][0]==124+int(i%2) && graph.pixels[i][2]==light.warning);
    for(unsigned i=44;i<88;++i)
      assert(graph.pixels[i][0]==64+int(i%2) && graph.pixels[i][2]==light.text);
    graph.pixels.clear();
    AnalysisUI::timeMarker(graph,palette.text,26,130,164,207,156,156);
    for(const auto &point:graph.pixels)assert(point[0]>=129 && point[0]<=130);
    assert(AnalysisUI::graphSpan(60,true,156)==166);
    assert(AnalysisUI::graphSpan(155,true,156)==166);
    assert(AnalysisUI::graphSpan(156,true,156)==166);
    assert(AnalysisUI::graphSpan(157,true,156)==167);
    for(float missing:{NAN,INFINITY,-1.f,0.f})assert(AnalysisUI::graphSpan(60,true,missing)==70);
    assert(AnalysisUI::graphSpan(60,false,156)==60);
  }
  {
    auto input=new Decay::Session;Decay::Results fits;fits.launcherRpm=15000;fits.initialRpm=9000;fits.acousticStart=.2f;
    fits.lastTime=45;fits.actualStop=80;fits.actualStopValid=true;fits.sampleCount=30;fits.validationPossible=true;fits.best=1;
    input->samples[0]={200,9000,false};input->samples[1]={1000,12000,true};input->count=2;
    Decay::Curve curve;curve.end=100;for(auto &v:curve.rpm)v=7000;
    {
      AnalysisMode::Session session;session.begin(1,true,7300,1000000);session.stop(74000000);
      Decay::Curve spin;spin.end=73;
      for(unsigned i=0;i<Decay::CURVE_POINTS;++i)spin.rpm[i]=7300-100*spin.seconds(i);
      auto summary=AnalysisDetails::summarize(spin,7300,73);
      assert(summary.complete && summary.firstBand==7 && summary.count==7);
      for(unsigned i=0;i<7;++i)assert(std::fabs(summary.seconds[i]-10)<.001f);
      assert(std::fabs(summary.battleStaminaSeconds-63)<.01f && std::fabs(summary.revolutions-7300*73/120.f)<.01f);
      auto partial=AnalysisDetails::summarize(spin,7300,36.5f);
      assert(partial.complete && partial.count==3 && !std::isfinite(partial.battleStaminaSeconds));
      auto palette=Palette::builtIn(0x07ff,true);session.nextResult();
      lgfx::LGFXBase details;AnalysisResultUI::draw(details,session,&summary,false,false,palette);
      assert(details.drew("Stamina") && details.drew("~63.0 s") && details.drew("7-6K") && details.drew("10.0 s") && !details.drew("Estimated curve"));checkOverlap(details);
      session.nextResult();lgfx::LGFXBase more;AnalysisResultUI::draw(more,session,&summary,false,false,palette);
      assert(more.drew("1-0K") && !more.drew("7-6K"));checkOverlap(more);
      summary.endpointEstimate=true;
      lgfx::LGFXBase estimate;AnalysisResultUI::draw(estimate,session,&summary,false,false,palette);
      assert(estimate.drew("Estimated curve") && estimate.drew("A Back  B More"));checkOverlap(estimate);
      summary.count=3;
      lgfx::LGFXBase shortEstimate;AnalysisResultUI::draw(shortEstimate,session,&summary,false,false,palette);
      assert(shortEstimate.drew("Estimated curve") && shortEstimate.drew("A Back"));checkOverlap(shortEstimate);
    }
    {
      fits.acousticStart=15;fits.lastTime=45;
      input->samples[0]={20000,6000,false,false};
      input->samples[1]={50000,12000,false,true};
      lgfx::LGFXBase main;
      auto palette=Palette::builtIn(0x07ff,true);
      AnalysisUI::fittedPlot(main,palette,fits,input,&curve,100);
      int lastX=26;bool accentSeen=false;
      for(auto &line:main.lines){
        assert(line[0]==lastX);
        const bool measured=line[0]>=42 && line[2]<=73;
        if(line[0]!=line[2])assert(line[4]==(measured?palette.accent:Palette::DARKGREY));
        accentSeen|=line[4]==palette.accent;lastX=line[2];
      }
      assert(accentSeen && lastX==130 && main.pixels[0][2]==palette.accent && main.pixels[1][2]==Palette::DARKGREY);
      lgfx::LGFXBase inconclusive;
      AnalysisUI::fittedPlot(inconclusive,palette,fits,input,nullptr,100);
      assert(inconclusive.lines.empty());
    }
    delete input;
  }

  AnalogTachometer::Curve curve;
  const auto colors=Palette::builtIn(0x07ff,true);LaunchMotion::Trace motion;AnalysisMode::Session session;MicTrace::StoredTrace trace;
  for(auto mic:{MicView::Mic::Starting,MicView::Mic::On,MicView::Mic::Failed,MicView::Mic::NoMemory}){
    lgfx::LGFXBase g;AnalysisUI::draw(g,session,0,nullptr,mic,motion,false,false,colors,curve);assert(g.drew("Stamina Analysis") && g.drew("READY") && g.drew("Hold B Exit") && !g.drew("A Replay  B Details"));checkOverlap(g);
  }
  session.begin(1,true,15000,1000000);trace.begin(1,15000);MicTrace::Step step;step.launch=1;step.rpm=9000;trace.add(step);
  for(bool stopped:{false,true}){if(stopped)session.stop(83456000);lgfx::LGFXBase g;AnalysisUI::draw(g,session,83456000,&trace,MicView::Mic::On,motion,false,false,colors,curve);assert(g.drew("1:22") && g.drew(stopped?"82s":"92s") && g.circles==0);checkOverlap(g);} // No stopwatch dial
  {lgfx::LGFXBase timed;session.begin(2,true,6000,1000000);session.tick(301000000);AnalysisUI::draw(timed,session,999000000,nullptr,MicView::Mic::On,motion,false,false,colors,curve);assert(timed.drew("5:00") && timed.drew("300s") && timed.drew("Time limit") && timed.drew("No stop recorded"));checkOverlap(timed);}
  {lgfx::LGFXBase partial;session.begin(7,true,6000,1000000);session.interrupt(21000000);AnalysisUI::draw(partial,session,22000000,nullptr,MicView::Mic::On,motion,false,false,colors,curve);assert(partial.drew("Interrupted") && partial.drew("No stop recorded"));checkOverlap(partial);}
  // Without readings, the population estimate plus padding sets the horizon until elapsed time passes it.
  session.begin(3,true,6000,1000000);
  for(unsigned seconds:{11u,37u,121u}){lgfx::LGFXBase gap;AnalysisUI::draw(gap,session,1000000ULL+seconds*1000000ULL,nullptr,MicView::Mic::On,motion,false,false,colors,curve,"READY",true,nullptr,nullptr,nullptr,nullptr,false,94.131f);char label[16];snprintf(label,sizeof(label),"%us",seconds<94?104:seconds+10);assert(gap.drew(label));checkOverlap(gap);}
  // Static content is retained, but time, palette, launch and sensor-state changes invalidate correctly.
  AnalysisUI::Cache cache;AnalysisMode::Session cached;lgfx::LGFXBase canvas;
  auto paint=[&](uint64_t at,const Palette::Colors &palette,const char *state="READY",bool ready=true){
    canvas.texts.clear();canvas.spans.clear();canvas.circles=0;canvas.setTextDatum(top_right);
    AnalysisUI::draw(canvas,cached,at,nullptr,MicView::Mic::On,motion,false,false,palette,curve,state,ready,&cache);
  };
  paint(0,colors);assert(canvas.drew("READY") && canvas.drew("Stamina Analysis"));
  paint(100000,colors);assert(!canvas.drew("READY") && !canvas.drew("0:00") && !canvas.drew("Stamina Analysis"));
  paint(200000,colors,"WAIT",false);assert(canvas.drew("WAIT") && canvas.drew("Stamina Analysis"));
  cached.begin(1,true,6000,1000000);paint(1000000,colors);assert(canvas.drew("6000 RPM"));
  paint(1100000,colors);assert(!canvas.drew("6000 RPM") && !canvas.drew("0:00"));
  paint(2000000,colors);assert(canvas.drew("0:01") && !canvas.drew("Stamina Analysis"));checkOverlap(canvas);
  auto changed=colors;changed.number=0xf800;paint(2100000,changed);assert(canvas.drew("6000 RPM") && canvas.drew("Stamina Analysis"));
  cached.toggleReplay(2100000);paint(2200000,changed);assert(!cache.valid);
  cached.toggleReplay(2300000);paint(2300000,changed);assert(canvas.drew("Stamina Analysis"));
  cache.invalidate();paint(2400000,changed);assert(canvas.drew("Stamina Analysis"));
  assert(canvas.timerInk==changed.number);
  // Stopping redraws the labels; the clock keeps full strength: it is the result.
  cached.stop(2450000);paint(2450000,changed);assert(canvas.drew("0:01") && canvas.drew("Curve unavailable") && canvas.timerInk==changed.number);
  cached.begin(2,true,6000,3000000);paint(3000000,changed);assert(canvas.drew("0:00") && canvas.timerInk==changed.number);
  cached.tick(303000000);paint(303000000,changed);assert(canvas.drew("5:00") && canvas.drew("Time limit") && canvas.timerInk==changed.number);
  session.toggleReplay(83456000);lgfx::LGFXBase replay;AnalysisUI::draw(replay,session,84456000,&trace,MicView::Mic::On,motion,false,false,colors,curve);assert(!replay.drew("Stamina Analysis"));
  lgfx::LGFXBase saving;
  AnalysisUI::draw(saving,session,84456000,&trace,MicView::Mic::On,motion,false,false,colors,curve,
                   "READY",true,nullptr,nullptr,nullptr,nullptr,false,NAN,true);
  assert(saving.drew("Stamina Analysis") && saving.drew("Save pending") && saving.drew("Please wait") && saving.drew("B Retry"));checkOverlap(saving);
  for(unsigned i=0;i<saving.texts.size();++i)
    if(saving.texts[i]=="0s")assert(saving.textColors[i]==colors.muted);
  uint16_t values[8]={},rejected[8]={};MicTrace::Trace marked(values,8,rejected);
  marked.begin(12,6000);MicTrace::Step accepted;accepted.launch=12;accepted.rpm=5900;marked.add(accepted);
  auto bad=accepted;bad.index=2;bad.rpm=0;bad.rejectedRpm=12000;marked.add(bad);
  assert(marked.heard()==1 && marked.latestRpm()==5900 && marked.lastSeconds()==MicTrace::Trace::seconds(0));
  assert(marked.rejectedRpm(1)==0 && marked.rejectedRpm(2)==12000 && marked.peakRpm()==5900);
  AnalysisMode::Session markedSession;markedSession.begin(12,true,6000,1000000);
  lgfx::LGFXBase markedGraph;AnalysisUI::draw(markedGraph,markedSession,2000000,&marked,MicView::Mic::On,motion,false,false,colors,curve);assert(markedGraph.grayDots==1 && markedGraph.drew("Finish") && markedGraph.drew("~") && !markedGraph.drew("-:--"));
  lgfx::LGFXBase normalGraph;MicView::draw(normalGraph,&marked,MicView::Mic::On,colors);assert(normalGraph.grayDots==0);
  lgfx::LGFXBase fallback;
  AnalysisUI::draw(fallback,markedSession,31000000,&marked,MicView::Mic::On,motion,false,false,colors,curve,"READY",true,nullptr,nullptr,nullptr,nullptr,false,38.f);
  assert(fallback.drew("~0:38") && fallback.drew("48s"));
  // No model names on screen: one row of 2x2 dots on the caption line says the
  // model is working. Before the first forecast it grows with elapsed time
  // (muted, led by one accent dot); once forecasting, one accent dot with a
  // muted tail steps one dot every 250 ms over a faint row.
  auto computeRow=[](const lgfx::LGFXBase &f){std::vector<std::array<int,2>> row;for(const auto &d:f.dots)if(d[1]==AnalysisUI::CAPTION_Y+3)row.push_back({d[0],d[2]});return row;};
  auto accentAt=[&](const lgfx::LGFXBase &f){int at=-1;const auto row=computeRow(f);
    for(unsigned i=0;i<row.size();++i){assert(row[i][0]==AnalysisUI::computeLeft(f)+int(i)*AnalysisUI::COMPUTE_PITCH);if(row[i][1]==colors.accent){assert(at<0);at=int(i);}}return at;};
  {const auto gathering=computeRow(markedGraph); // 1 s of 10: 2 muted dots, then the accent
   assert(gathering.size()==3 && gathering[0][1]==colors.muted && gathering[1][1]==colors.muted && gathering[2][1]==colors.accent);}
  {lgfx::LGFXBase nine;AnalysisUI::draw(nine,markedSession,10000000,&marked,MicView::Mic::On,motion,false,false,colors,curve);
   const auto row=computeRow(nine);assert(row.size()==23 && row.back()[1]==colors.accent);}
  assert(!fallback.drew("Early mic estimate") && computeRow(fallback).size()==AnalysisUI::COMPUTE_DOTS);
  {const int at=accentAt(fallback);assert(at>=0);
   lgfx::LGFXBase next;AnalysisUI::draw(next,markedSession,31000000+AnalysisUI::MODEL_STEP_US,&marked,MicView::Mic::On,motion,false,false,colors,curve,"READY",true,nullptr,nullptr,nullptr,nullptr,false,38.f);
   assert(accentAt(next)==(at+1)%AnalysisUI::COMPUTE_DOTS);
   const auto row=computeRow(fallback);
   for(unsigned i=0;i<row.size();++i)assert(row[i][1]==(int(i)==at?colors.accent:int(i)==(at+AnalysisUI::COMPUTE_DOTS-1)%AnalysisUI::COMPUTE_DOTS?colors.muted:colors.faint));}
  lgfx::LGFXBase before,atTen,pending;
  AnalysisUI::draw(before,markedSession,10999000,nullptr,MicView::Mic::Failed,motion,false,false,colors,curve);
  AnalysisUI::draw(atTen,markedSession,11000000,nullptr,MicView::Mic::Failed,motion,false,false,colors,curve,"READY",true,nullptr,nullptr,nullptr,nullptr,false,94.131f);
  // Without the mic the caption names the problem instead; nothing animates.
  assert(!before.drew("Finish") && atTen.drew("~1:34") && atTen.drew("Mic unavailable") && computeRow(atTen).empty());
  AnalysisUI::draw(pending,markedSession,32000000,&marked,MicView::Mic::On,motion,false,false,colors,curve,
                   "READY",true,nullptr,nullptr,nullptr,nullptr,true,88.f);
  assert(!pending.drew("Fixed Riccati") && pending.drew("~1:28") && pending.drew("98s") && accentAt(pending)>=0);
  // The RPM row: the latest heard reading, cyan as the readings on the graph.
  for(unsigned i=0;i<pending.texts.size();++i)if(pending.texts[i]=="5900 RPM")assert(pending.textColors[i]==colors.accent);
  assert(pending.drew("Now") && pending.drew("5900 RPM") && atTen.drew("- RPM"));
  // Every ten seconds the caption row gives way for three seconds to a word
  // decoded from glyphs: letters settle left to right in muted; until then
  // cycling glyphs in accent. No dots while the word shows.
  {auto word=[](const lgfx::LGFXBase &f,uint16_t &ink,bool &mixed){std::string w;ink=0;mixed=false;
     for(unsigned i=0;i<f.texts.size();++i)if(f.spans[i][2]==AnalysisUI::CAPTION_Y && f.spans[i][3]==1 && f.texts[i].size()==1){
       if(!w.empty() && f.textColors[i]!=ink)mixed=true;ink=f.textColors[i];w+=f.texts[i];}
     return w;};
   const uint64_t wordAt=AnalysisUI::WORD_CYCLE_US*3-AnalysisUI::WORD_SHOW_US;
   lgfx::LGFXBase start,settled,dots;
   for(auto [frame,at]:{std::pair{&start,wordAt},std::pair{&settled,wordAt+AnalysisUI::WORD_SHOW_US-1},std::pair{&dots,wordAt-1}})
     AnalysisUI::draw(*frame,markedSession,at,&marked,MicView::Mic::On,motion,false,false,colors,curve,
                      "READY",true,nullptr,nullptr,nullptr,nullptr,false,88.f);
   uint16_t ink;bool mixed;
   const auto done=word(settled,ink,mixed);
   assert((done=="ANALYZING" || done=="LISTENING" || done=="CALCULATING") && ink==colors.muted && !mixed);
   const auto first=word(start,ink,mixed);
   assert(first.size()==done.size() && first!=done && ink==colors.accent && !mixed);
   assert(computeRow(start).empty() && computeRow(settled).empty() && computeRow(dots).size()==AnalysisUI::COMPUTE_DOTS);
   assert(word(dots,ink,mixed).empty());
   checkOverlap(start);checkOverlap(settled);}
  // While the mic feeds the model, trails of 2x2 dots fall between the now line
  // and the finish line, on the markers' 4 px rows; they move every 100 ms.
  {auto rainDots=[](const lgfx::LGFXBase &f){std::vector<std::array<int,3>> r;
     for(const auto &d:f.dots)if(d[1]>AnalysisUI::GRAPH_TOP && d[1]<AnalysisUI::GRAPH_BOTTOM)r.push_back(d);return r;};
   lgfx::LGFXBase a,b,off;
   for(auto [frame,at,m]:{std::tuple{&a,uint64_t(32000000),MicView::Mic::On},std::tuple{&b,uint64_t(32000000+AnalysisUI::RAIN_STEP_US),MicView::Mic::On},
                          std::tuple{&off,uint64_t(32000000),MicView::Mic::Failed}})
     AnalysisUI::draw(*frame,markedSession,at,&marked,m,motion,false,false,colors,curve,
                      "READY",true,nullptr,nullptr,nullptr,nullptr,false,88.f);
   const float span=AnalysisUI::graphSpan(31.f,true,88.f);
   const int nowX=AnalysisUI::markerX(26,130,31.f,span),finishX=AnalysisUI::markerX(26,130,88.f,span);
   const auto drops=rainDots(a);
   assert(!drops.empty() && rainDots(off).empty() && rainDots(b)!=drops);
   for(const auto &d:drops){
     assert(d[0]>nowX+1 && d[0]+2<=finishX && (d[1]-AnalysisUI::GRAPH_TOP-1)%4==0 && (d[0]-26)%AnalysisUI::RAIN_PITCH==0);
     assert(d[2]==colors.accent || d[2]==colors.muted || d[2]==colors.faint);
   }
   checkOverlap(a);}
  // "~" marks the Finish value as the current estimate. With the sprite cache a
  // new value decodes into place: only its changed digits cycle, muted, and
  // settle left to right within VALUE_DECODE_US.
  {AnalysisUI::Cache cache;
   auto frame=[&](lgfx::LGFXBase &f,uint64_t at,float finish){
     AnalysisUI::draw(f,markedSession,at,&marked,MicView::Mic::On,motion,false,false,colors,curve,
                      "READY",true,&cache,nullptr,nullptr,nullptr,false,finish);};
   lgfx::LGFXBase appear,shown,change,changed;
   frame(appear,32000000,88.f);
   assert(!appear.drew("~1:28") && appear.drew("~") && appear.drew(":"));
   frame(shown,32000000+AnalysisUI::VALUE_DECODE_US,88.f);
   assert(shown.drew("~1:28"));
   frame(change,32500000,89.f);
   assert(!change.drew("~1:29") && change.drew("~") && change.drew("1") && change.drew("2"));
   unsigned sawTwo=0;
   for(unsigned i=0;i+1<change.texts.size();++i)
     if(change.texts[i]=="2" && change.spans[i][2]==AnalysisUI::FINISH_Y){++sawTwo;
       assert(change.textColors[i]==colors.warning && change.textColors[i+1]==colors.muted);
       assert(change.texts[i+1].size()==1 && change.texts[i+1][0]>='0' && change.texts[i+1][0]<='9');
     }
   assert(sawTwo==1);checkOverlap(change);
   frame(changed,32500000+AnalysisUI::VALUE_DECODE_US,89.f);
   assert(changed.drew("~1:29"));}
  markedSession.stop(2000000);
  for(unsigned phase=0;phase<3;++phase){
    lgfx::LGFXBase loading;
    AnalysisUI::draw(loading,markedSession,3000000+phase*250000,&marked,MicView::Mic::On,motion,false,false,colors,curve,
                     "READY",true,nullptr,nullptr,nullptr,nullptr,true);
    assert(loading.drew("Fitting curve") && !loading.drew("Stopped"));
    checkOverlap(loading);
    // Fitting after the stop shows the same row, stepping on the 250 ms redraws.
    assert(loading.loader.empty() && accentAt(loading)==int((3000000/AnalysisUI::MODEL_STEP_US+phase)%AnalysisUI::COMPUTE_DOTS));
  }
  Decay::Results ranked;
  ranked.best=int(Decay::Model::Riccati);
  Decay::Curve winner;
  winner.end=83.4f;
  auto samples=new Decay::Session;
  for(bool fixed:{false,true}){
    ranked.fixedRiccati=fixed;
    lgfx::LGFXBase graph;
    AnalysisUI::draw(graph,markedSession,3000000,&marked,MicView::Mic::On,motion,false,false,colors,curve,
                     "READY",true,nullptr,&ranked,samples,&winner,false);
    assert(graph.drew("Curve fitted") && !graph.drew("Fixed Riccati fit") && !graph.drew("Free Riccati fit"));
    checkOverlap(graph);
  }
  AnalysisMode::Session live;live.begin(13,true,6000,1000000);
  ranked.fixedRiccati=ranked.endpointEstimate=true;
  ranked.launcherRpm=7000;ranked.acousticStart=ranked.lastTime=0;
  for(unsigned i=0;i<Decay::CURVE_POINTS;++i)winner.rpm[i]=7000*(1-winner.seconds(i)/winner.end);
  for(bool copiedInput:{false,true}){
    lgfx::LGFXBase graph;
    AnalysisUI::draw(graph,markedSession,3000000,&marked,MicView::Mic::On,motion,false,false,colors,curve,
                     "READY",true,nullptr,&ranked,copiedInput?samples:nullptr,&winner,false);
    assert(graph.drew("Estimated curve") && !graph.drew("Stopped"));
    for(const auto &line:graph.lines)assert(line[4]==Palette::DARKGREY);
    for(unsigned i=0;i+1<graph.pixels.size();++i)assert(graph.pixels[i][2]==Palette::DARKGREY);
    assert(graph.pixels.back()[2]==colors.background); // Hollow IR anchor marker.
    checkOverlap(graph);
  }
  ranked.endpointEstimate=false;
  ranked.fixedRiccati=true;ranked.acousticStart=1;ranked.lastTime=45;
  for(unsigned i=0;i<Decay::CURVE_POINTS;++i)winner.rpm[i]=6000-10*winner.seconds(i);
  lgfx::LGFXBase prediction;
  AnalysisUI::draw(prediction,live,51000000,nullptr,MicView::Mic::On,motion,false,false,colors,curve,
                   "READY",true,nullptr,&ranked,samples,&winner,false,83.f);
  assert(prediction.drew("~1:23") && prediction.drew("A Replay  B Stop"));
  assert(prediction.drew("93s") && !prediction.drew("60s"));
  // Measured up to the last reading (45 s) as a line; the forecast past it as
  // accent dots every 5 px.
  assert(!prediction.lines.empty() && prediction.lines.back()[2]<100);
  auto forecastDots=[](const lgfx::LGFXBase &f){std::vector<int> xs;for(const auto &d:f.dots)if(d[1]>AnalysisUI::GRAPH_TOP){assert(d[2]==Palette::builtIn(0x07ff,true).accent);xs.push_back(d[0]);}return xs;};
  const auto ahead=forecastDots(prediction);
  assert(ahead.size()>=5 && ahead.front()>=prediction.lines.back()[2]);
  // The active finish can be well beyond the raw fit. Only its projected
  // tail moves, and the last dot falls within one pitch of the finish marker.
  lgfx::LGFXBase adjusted;
  AnalysisUI::draw(adjusted,live,51000000,nullptr,MicView::Mic::On,motion,false,false,colors,curve,
                   "READY",true,nullptr,&ranked,samples,&winner,false,120.f);
  const auto adjustedDots=forecastDots(adjusted);
  const int endX=AnalysisUI::markerX(26,130,120.f,130.f);
  assert(adjusted.drew("~2:00") && adjusted.drew("130s") && !adjustedDots.empty());
  assert(adjustedDots.back()<=endX && endX-adjustedDots.back()<=AnalysisUI::FORECAST_PITCH);
  for(unsigned i=1;i<ahead.size();++i)assert(ahead[i]-ahead[i-1]==AnalysisUI::FORECAST_PITCH);
  {lgfx::LGFXBase next;AnalysisUI::draw(next,live,51000000+AnalysisUI::MODEL_STEP_US,nullptr,MicView::Mic::On,motion,false,false,colors,curve,
                   "READY",true,nullptr,&ranked,samples,&winner,false,83.f);
   assert(forecastDots(next)==ahead);} // The forecast holds still: the caption row is the one animation.
  for(const auto &line:prediction.lines)assert(line[0]>=26 && line[2]<=130);
  checkOverlap(prediction);
  for(const auto *frame:{&markedGraph,&fallback,&atTen,&pending,&prediction})
    for(unsigned i=0;i<frame->texts.size();++i){
      // The Finish value is orange, as the dotted finish line; "-:--" waits muted.
      if(frame->texts[i]=="Finish" && i+1<frame->texts.size())
        assert(frame->textColors[i+1]==(frame->texts[i+1]=="-:--"?colors.muted:colors.warning));
      assert(frame->texts[i]!="Fixed Riccati" && frame->texts[i]!="Early mic estimate" && frame->texts[i]!="Population estimate");
    }
  // Fit and raw views share the displayed forecast, including during a refit.
  // After passing that forecast, keep its marker while the axis follows the clock.
  const auto finishInk=colors.warning;
  for(bool fitted:{false,true})for(bool busy:{false,true})for(float finish:{600.f,40.f}){
    winner.end=finish;
    lgfx::LGFXBase frame;
    AnalysisUI::draw(frame,live,51000000,nullptr,MicView::Mic::On,motion,false,false,colors,curve,
                     "READY",true,nullptr,fitted?&ranked:nullptr,fitted?samples:nullptr,fitted?&winner:nullptr,busy,finish);
    assert(frame.drew(finish>50?"610s":"60s") && !frame.drew("50s"));checkOverlap(frame);
    unsigned currentDots=0,finishDots=0;
    for(const auto &point:frame.pixels){
      if(point[2]==colors.text){
        const int x=finish>50?35:113;
        assert(point[0]>=x && point[0]<=x+1 && point[1]>AnalysisUI::GRAPH_TOP && point[1]<AnalysisUI::GRAPH_BOTTOM);++currentDots;
      } else if(point[2]==finishInk){
        const int x=finish>50?128:95;
        assert(point[0]>=x && point[0]<=x+1 && point[1]>AnalysisUI::GRAPH_TOP && point[1]<AnalysisUI::GRAPH_BOTTOM);++finishDots;
      }
    }
    assert(currentDots==58 && finishDots==58); // 2x2 dots every 4 rows over the 58-row graph
  }
  live.stop(51000000);
  lgfx::LGFXBase finalFrame;
  AnalysisUI::draw(finalFrame,live,52000000,nullptr,MicView::Mic::On,motion,false,false,colors,curve,
                   "READY",true,nullptr,&ranked,samples,&winner,false);
  assert(finalFrame.drew("50s") && !finalFrame.drew("600s"));checkOverlap(finalFrame);
  for(const auto &point:finalFrame.pixels)assert(point[2]!=finishInk && point[2]!=colors.text);
  delete samples;
  marked.freeze();bad.index=3;marked.add(bad);assert(marked.count()==3);
  marked.begin(13,7000);assert(marked.count()==0 && marked.rejectedPeakRpm()==0);
  accepted.launch=13;accepted.index=2;marked.add(accepted);assert(marked.rejectedRpm(2)==0);
  lgfx::LGFXBase g;MicView::draw(g,&trace,MicView::Mic::On,colors);
  std::cout<<"PASS: Analysis and existing microphone view text/plot bounds\n";
}
