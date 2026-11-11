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
namespace fonts {static int Font0=0,Font4=4,Font2=2;}
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
  std::vector<std::string> texts;
  bool drew(const char *text)const{return std::find(texts.begin(),texts.end(),text)!=texts.end();}
  int width(){return 135;}
  void setTextDatum(int d){datum=d;}void setTextColor(uint16_t color,uint16_t){ink=color;}
  void fillRect(int x,int y,int w,int h,uint16_t){assert(x>=0 && y>=0 && x+w<=135 && y+h<=240);}
  void drawRoundRect(int x,int y,int w,int h,int,uint16_t){fillRect(x,y,w,h,0);}
  static int font4Width(const char *text){int width=0;for(const char *p=text;*p;++p)width+=(*p>='0' && *p<='9')?14:*p==':'?7:*p==' '?5:*p=='R'?17:*p=='P'?16:*p=='M'?21:18;return width;}
  int textWidth(const char *text,const int *font){return *font==4?font4Width(text):int(std::strlen(text))*8;}
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
    const int width=font==4?font4Width(text):font==2?font2Width(text):int(std::strlen(text))*glyph;
    const int left=datum==top_left?x:datum==top_right?x-width:x-width/2;
    const int right=datum==top_left?x+width:datum==top_right?x:x+(width+1)/2;
    assert(left>=0 && right<=135 && y>=0 && y+height<=240);
    if(y==80 && font==4)timerInk=ink;
    spans.push_back({left,right,y,font});texts.push_back(text);
    ++strings;if(y==231)++footers;
    if(!std::strcmp(text,"B next option ->")){assert(y==139 && ink==0x39e7);++cycleHints;}
    else if(y>=86 && y<=95){valueEdges.push_back(left);valueEdges.push_back(right);valueFonts.push_back(font);}
  }
};
}

#include "analysis_ui.h"
#include "analysis_result_ui.h"
void checkOverlap(const lgfx::LGFXBase &g){
  for(unsigned i=0;i<g.spans.size();++i)for(unsigned j=i+1;j<g.spans.size();++j){auto a=g.spans[i],b=g.spans[j];int ah=a[3]==4?26:a[3]==2?16:8,bh=b[3]==4?26:b[3]==2?16:8;assert(a[1]<=b[0] || b[1]<=a[0] || a[2]+ah<=b[2] || b[2]+bh<=a[2]);}
}
int main(){
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
      assert(details.drew("Estimated") && details.drew("Battle Stamina") && details.drew("63.0s") && details.drew("7K - 6K") && details.drew("10.0s"));checkOverlap(details);
      session.nextResult();lgfx::LGFXBase more;AnalysisResultUI::draw(more,session,&summary,false,false,palette);
      assert(more.drew("1K - 0K") && !more.drew("7K - 6K"));checkOverlap(more);
    }
    {
      fits.acousticStart=15;fits.lastTime=45;
      input->samples[0]={20000,6000,false,false};
      input->samples[1]={50000,12000,false,true};
      lgfx::LGFXBase main;
      auto palette=Palette::builtIn(0x07ff,true);
      AnalysisUI::fittedPlot(main,palette,fits,*input,&curve,100);
      int lastX=26;bool accentSeen=false;
      for(auto &line:main.lines){
        assert(line[0]==lastX);
        const bool measured=line[0]>=42 && line[2]<=73;
        if(line[0]!=line[2])assert(line[4]==(measured?palette.accent:Palette::DARKGREY));
        accentSeen|=line[4]==palette.accent;lastX=line[2];
      }
      assert(accentSeen && lastX==130 && main.pixels[0][2]==palette.accent && main.pixels[1][2]==Palette::DARKGREY);
      lgfx::LGFXBase inconclusive;
      AnalysisUI::fittedPlot(inconclusive,palette,fits,*input,nullptr,100);
      assert(inconclusive.lines.empty());
    }
    delete input;
  }

  AnalogTachometer::Curve curve;
  const auto colors=Palette::builtIn(0x07ff,true);LaunchMotion::Trace motion;AnalysisMode::Session session;MicTrace::StoredTrace trace;
  for(auto mic:{MicView::Mic::Starting,MicView::Mic::On,MicView::Mic::Failed,MicView::Mic::NoMemory}){
    lgfx::LGFXBase g;AnalysisUI::draw(g,session,0,nullptr,mic,motion,false,false,colors,curve);assert(g.drew("Analysis") && g.drew("READY"));checkOverlap(g);
  }
  session.begin(1,true,15000,1000000);trace.begin(1,15000);MicTrace::Step step;step.launch=1;step.rpm=9000;trace.add(step);
  for(bool stopped:{false,true}){if(stopped)session.stop(83456000);lgfx::LGFXBase g;AnalysisUI::draw(g,session,83456000,&trace,MicView::Mic::On,motion,false,false,colors,curve);assert(g.drew("01:22") && g.drew("82s") && g.circles==3);checkOverlap(g);}
  {lgfx::LGFXBase timed;session.begin(2,true,6000,1000000);session.tick(301000000);AnalysisUI::draw(timed,session,999000000,nullptr,MicView::Mic::On,motion,false,false,colors,curve);assert(timed.drew("05:00") && timed.drew("300s") && timed.drew("Last --:--:---"));checkOverlap(timed);}
  // The clock alone advances the graph through a total absence of microphone readings.
  session.begin(3,true,6000,1000000);
  for(unsigned seconds:{11u,37u,121u}){lgfx::LGFXBase gap;AnalysisUI::draw(gap,session,1000000ULL+seconds*1000000ULL,nullptr,MicView::Mic::On,motion,false,false,colors,curve);char label[16];snprintf(label,sizeof(label),"%us",seconds);assert(gap.drew(label));checkOverlap(gap);}
  // Static content is retained, but time, palette, launch and sensor-state changes invalidate correctly.
  AnalysisUI::Cache cache;AnalysisMode::Session cached;lgfx::LGFXBase canvas;
  auto paint=[&](uint64_t at,const Palette::Colors &palette,const char *state="READY",bool ready=true){
    canvas.texts.clear();canvas.spans.clear();canvas.circles=0;canvas.setTextDatum(top_right);
    AnalysisUI::draw(canvas,cached,at,nullptr,MicView::Mic::On,motion,false,false,palette,curve,state,ready,&cache);
  };
  paint(0,colors);assert(canvas.drew("READY") && canvas.circles==3);
  paint(100000,colors);assert(!canvas.drew("READY") && !canvas.drew("00:00") && canvas.circles==0);
  paint(200000,colors,"WAIT",false);assert(canvas.drew("WAIT") && canvas.circles==3);
  cached.begin(1,true,6000,1000000);paint(1000000,colors);assert(canvas.drew("6000 RPM"));
  paint(1100000,colors);assert(!canvas.drew("6000 RPM") && !canvas.drew("00:00"));
  paint(2000000,colors);assert(canvas.drew("00:01") && canvas.circles==0);checkOverlap(canvas);
  auto changed=colors;changed.number=0xf800;paint(2100000,changed);assert(canvas.drew("6000 RPM") && canvas.circles==3);
  cached.toggleReplay(2100000);paint(2200000,changed);assert(!cache.valid);
  cached.toggleReplay(2300000);paint(2300000,changed);assert(canvas.circles==3);
  cache.invalidate();paint(2400000,changed);assert(canvas.drew("Analysis"));
  assert(canvas.timerInk==changed.number);
  cached.stop(2450000);paint(2450000,changed);assert(canvas.drew("00:01") && canvas.timerInk==changed.muted && canvas.circles==0);
  cached.begin(2,true,6000,3000000);paint(3000000,changed);assert(canvas.drew("00:00") && canvas.timerInk==changed.number);
  cached.tick(303000000);paint(303000000,changed);assert(canvas.drew("05:00") && canvas.timerInk==changed.muted);
  session.toggleReplay(83456000);lgfx::LGFXBase replay;AnalysisUI::draw(replay,session,84456000,&trace,MicView::Mic::On,motion,false,false,colors,curve);assert(!replay.drew("Analysis"));
  uint16_t values[8]={},rejected[8]={};MicTrace::Trace marked(values,8,rejected);
  marked.begin(12,6000);MicTrace::Step accepted;accepted.launch=12;accepted.rpm=5900;marked.add(accepted);
  auto bad=accepted;bad.index=2;bad.rpm=0;bad.rejectedRpm=12000;marked.add(bad);
  assert(marked.heard()==1 && marked.latestRpm()==5900 && marked.lastSeconds()==MicTrace::Trace::seconds(0));
  assert(marked.rejectedRpm(1)==0 && marked.rejectedRpm(2)==12000 && marked.peakRpm()==5900);
  AnalysisMode::Session markedSession;markedSession.begin(12,true,6000,1000000);
  lgfx::LGFXBase markedGraph;AnalysisUI::draw(markedGraph,markedSession,2000000,&marked,MicView::Mic::On,motion,false,false,colors,curve);assert(markedGraph.grayDots==1 && markedGraph.drew("Predicted finish --:--"));
  lgfx::LGFXBase normalGraph;MicView::draw(normalGraph,&marked,MicView::Mic::On,colors);assert(normalGraph.grayDots==0);
  markedSession.stop(2000000);
  for(unsigned phase=0;phase<3;++phase){
    lgfx::LGFXBase loading;
    AnalysisUI::draw(loading,markedSession,3000000+phase*250000,&marked,MicView::Mic::On,motion,false,false,colors,curve,
                     "READY",true,nullptr,nullptr,nullptr,nullptr,true);
    assert(loading.drew("Computing Best Fit") && loading.drew("RPM Curve. . .") && !loading.drew("Stopped"));
    checkOverlap(loading);
    assert(loading.loader.size()==3);
    for(unsigned i=0;i<3;++i)assert(loading.loader[i][1]==(i==phase?colors.accent:colors.muted));
  }
  const char *titles[]={"Coulomb-Viscous Model","Coulomb-Quadratic","Riccati Model","Power-Law Damping"};
  Decay::Results ranked;Decay::Curve winner;winner.end=10;
  auto samples=new Decay::Session;
  for(int model=0;model<4;++model){
    ranked.best=model;
    lgfx::LGFXBase graph;
    AnalysisUI::draw(graph,markedSession,3000000,&marked,MicView::Mic::On,motion,false,false,colors,curve,
                     "READY",true,nullptr,&ranked,samples,&winner,false);
    assert(graph.drew(titles[model]));
    if(model==1 || model==3)assert(graph.drew("Model"));
    checkOverlap(graph);
    AnalysisMode::Session live;live.begin(13,true,6000,1000000);
    // A long physical estimate must not set the right edge of a live preview.
    winner.end=600;
    for(unsigned i=0;i<Decay::CURVE_POINTS;++i)winner.rpm[i]=6000-10*winner.seconds(i);
    ranked.validationPossible=true;ranked.acousticStart=1;ranked.lastTime=45;
    for(auto &m:ranked.models){
      m.valid=m.converged=m.validationConverged=m.validation.valid=m.stopValid=true;
      m.params.a=.04f;m.validation.rmse=30;m.stopSeconds=83.4f;
    }
    ranked.models[model].stopValid=model!=0;
    lgfx::LGFXBase prediction;
    AnalysisUI::draw(prediction,live,51000000,nullptr,MicView::Mic::On,motion,false,false,colors,curve,
                     "READY",true,nullptr,&ranked,samples,&winner,false);
    assert(prediction.drew(model==0?"Predicted finish --:--":"Predicted finish 01:23") && prediction.drew("Press B when"));
    assert(prediction.drew("60s") && !prediction.drew("600s"));
    assert(!prediction.lines.empty() && prediction.lines.back()[2]==130);
    for(const auto &line:prediction.lines)assert(line[0]>=26 && line[2]<=130);
    checkOverlap(prediction);
  }
  delete samples;
  marked.freeze();bad.index=3;marked.add(bad);assert(marked.count()==3);
  marked.begin(13,7000);assert(marked.count()==0 && marked.rejectedPeakRpm()==0);
  accepted.launch=13;accepted.index=2;marked.add(accepted);assert(marked.rejectedRpm(2)==0);
  lgfx::LGFXBase g;MicView::draw(g,&trace,MicView::Mic::On,colors);
  std::cout<<"PASS: Analysis and existing microphone view text/plot bounds\n";
}
