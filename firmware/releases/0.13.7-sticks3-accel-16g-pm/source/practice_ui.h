#pragma once
#include "practice_history.h"
#include "practice_views.h"

namespace PracticeUI {
static constexpr unsigned ROWS=5,PULL_WINDOW=30,SESSION_WINDOW=12;
inline unsigned window(View view){return unsigned(view)>=unsigned(View::Battery)?0:view==View::Recent?ROWS:view==View::Pulls?PULL_WINDOW:SESSION_WINDOW;}
inline unsigned count(const PracticeHistory &h,View view){return unsigned(view)>=unsigned(View::Battery)?0:view==View::Sessions?h.sessions():h.size();}
inline void draw(lgfx::LGFXBase &g,const PracticeHistory &h,View mode,unsigned offset,bool demo,uint16_t accent=TFT_CYAN) {
  g.setTextDatum(top_center);g.setTextColor(demo?TFT_ORANGE:TFT_WHITE,TFT_BLACK);
  g.drawString(demo?"DEMO":mode==View::Recent?"Launch history":mode==View::Pulls?"Pull trend":"Session trend",g.width()/2,8,2);
  g.setTextColor(TFT_WHITE,TFT_BLACK);
  const unsigned total=count(h,mode);
  if(offset>=total)offset=0;
  const unsigned n=std::min(window(mode),total-offset);
  char line[64];
  if(!total) {
    g.drawString("No launches yet",g.width()/2,66,2);
    g.drawString("Make a few pulls",g.width()/2,98,1);
    g.drawString("RPM from each pull",g.width()/2,118,1);
  } else if(mode==View::Recent) {
    const auto *session=h.session();
    snprintf(line,sizeof(line),"S%lu  %lu pulls",(unsigned long)session->number,(unsigned long)session->count);
    g.drawString(line,g.width()/2,34,2);
    snprintf(line,sizeof(line),"Avg %.0f RPM",session->mean());g.drawString(line,g.width()/2,57,2);
    snprintf(line,sizeof(line),"Best %.0f",session->best);g.setTextColor(accent,TFT_BLACK);g.drawString(line,g.width()/2,80,1);
    g.setTextDatum(top_left);g.setTextColor(TFT_DARKGREY,TFT_BLACK);g.drawString("Pull    RPM   Session",5,99,1);
    for(unsigned i=0;i<n;++i) {
      const auto &r=*h.recent(offset+i);
      g.setTextColor(i?TFT_WHITE:accent,TFT_BLACK);
      snprintf(line,sizeof(line),"#%lu",(unsigned long)r.number);g.drawString(line,5,115+i*17,1);
      snprintf(line,sizeof(line),"%.0f",r.rpm);g.drawString(line,47,115+i*17,1);
      snprintf(line,sizeof(line),"S%lu",(unsigned long)r.session);g.drawString(line,95,115+i*17,1);
    }
    g.setTextDatum(top_center);g.setTextColor(TFT_DARKGREY,TFT_BLACK);
    snprintf(line,sizeof(line),"%u-%u of %u",offset+1,offset+n,total);g.drawString(line,g.width()/2,204,1);
  } else {
    const bool sessions=mode==View::Sessions;
    const float mean=sessions?h.session(offset)->mean():h.recentMean(offset,std::min(5u,n));
    snprintf(line,sizeof(line),"%.0f",mean);g.drawString(line,g.width()/2,34,4);
    if(sessions)snprintf(line,sizeof(line),"Session avg RPM");
    else snprintf(line,sizeof(line),"Last %u avg RPM",std::min(5u,n));
    g.drawString(line,g.width()/2,63,1);
    if(sessions) {
      const auto &s=*h.session(offset);
      snprintf(line,sizeof(line),"S%lu  n=%lu  best %.0f",(unsigned long)s.number,(unsigned long)s.count,s.best);
    } else {
      const auto &r=*h.recent(offset);snprintf(line,sizeof(line),"Latest #%lu: %.0f",(unsigned long)r.number,r.rpm);
    }
    g.drawString(line,g.width()/2,82,1);
    // Time scale is practice order. Equal horizontal spacing does not claim
    // elapsed days or minutes, and session means are one point per session.
    float values[PULL_WINDOW]={};uint32_t numbers[PULL_WINDOW]={};
    float lo=INFINITY,hi=0;
    for(unsigned i=0;i<n;++i) {
      const unsigned index=offset+n-1-i;
      values[i]=sessions?h.session(index)->mean():h.recent(index)->rpm;
      numbers[i]=sessions?h.session(index)->number:h.recent(index)->number;
      lo=std::min(lo,values[i]);hi=std::max(hi,values[i]);
    }
    const float pad=std::max(200.0f,(hi-lo)*0.15f);
    lo=std::max(0.0f,lo-pad);hi+=pad;
    const int left=35,right=127,top=108,bottom=179;
    auto px=[&](unsigned i){return n<=1?(left+right)/2:left+int(i*(right-left)/(n-1));};
    auto py=[&](float value){return bottom-int((value-lo)/(hi-lo)*(bottom-top));};
    g.setTextDatum(top_left);g.setTextColor(TFT_DARKGREY,TFT_BLACK);
    snprintf(line,sizeof(line),"%.1fk",hi/1000);g.drawString(line,1,top-2,1);
    snprintf(line,sizeof(line),"%.1fk",lo/1000);g.drawString(line,1,bottom-6,1);
    g.drawFastVLine(left-2,top,bottom-top+1,TFT_DARKGREY);
    g.drawFastHLine(left-2,bottom,right-left+3,TFT_DARKGREY);
    for(unsigned i=0;i<n;++i) {
      const uint16_t color=sessions?accent:TFT_DARKGREY;
      if(sessions && i)g.drawLine(px(i-1),py(values[i-1]),px(i),py(values[i]),color);
      g.fillCircle(px(i),py(values[i]),sessions?2:1,color);
    }
    if(!sessions && n>=5) {
      // Only complete trailing five-pull averages; no padded early windows.
      float previous=0;
      for(unsigned i=4;i<n;++i) {
        float average=0;for(unsigned j=i-4;j<=i;++j)average+=values[j]/5;
        if(i>4)g.drawLine(px(i-1),py(previous),px(i),py(average),accent);
        g.fillCircle(px(i),py(average),1,accent);previous=average;
      }
    }
    snprintf(line,sizeof(line),"%s%lu",sessions?"S":"#",(unsigned long)numbers[0]);g.drawString(line,left,186,1);
    g.setTextDatum(top_right);snprintf(line,sizeof(line),"%s%lu",sessions?"S":"#",(unsigned long)numbers[n-1]);g.drawString(line,right,186,1);
    g.setTextDatum(top_center);g.setTextColor(accent,TFT_BLACK);
    if((sessions && n>=4) || (!sessions && n>=10)) {
      const unsigned k=sessions?2:5;float first=0,last=0;
      for(unsigned i=0;i<k;++i){first+=values[i]/k;last+=values[n-k+i]/k;}
      snprintf(line,sizeof(line),"First/last %u: %+.0f%%",k,100*(last-first)/first);
    } else snprintf(line,sizeof(line),sessions?"Need 4 sessions":"10 pulls for change");
    g.drawString(line,g.width()/2,204,1);
  }
  g.setTextDatum(top_center);g.setTextColor(TFT_DARKGREY,TFT_BLACK);
  g.drawString("Hold B Back",g.width()/2,219,1);
  g.drawString("A Done  B Older",g.width()/2,231,1);
}
}
