#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "controls_ui.h"
#include "rival_book.h"
#include "screens_ui.h"

// Battles (docs/FIRMWARE_BATTLES.md): the cards a battle puts on the main
// screen and the Battles pages under Menu. Pure drawing from the record, so
// the host renderer draws exactly what the device does.
namespace BattleUI {
// Counts are shown up to this; a stick does not get there, and the layouts hold to it.
inline unsigned shown(unsigned long value){return unsigned(value<9999?value:9999);}
// The largest of Font 2 and Font 1 in which `text` fits `width`.
inline int fit(lgfx::LGFXBase &g,const char *text,int width){return g.textWidth(text,&fonts::Font2)<=width?2:1;}
// "VS Kai" within `width`; a name too long for that goes without the VS, and
// in the small font if it must.
inline void title(lgfx::LGFXBase &g,const Palette::Colors &c,const Battle::Rival &r,int y,int width) {
  char name[Battle::NAME_CHARS+1],line[Battle::NAME_CHARS+4];Battle::displayName(r,name,sizeof(name));
  snprintf(line,sizeof(line),"VS %s",name);
  g.setTextDatum(top_center);g.setTextColor(c.accent,c.background);
  const char *text=g.textWidth(line,&fonts::Font2)<=width?line:name;
  const int font=fit(g,text,width);
  g.drawString(text,g.width()/2,font==2?y:y+4,font);
}
// A player's way to the end of the match: a dot for each point scored, a ring
// for each still to score, as many as the match is long, centered on `cx`
// within `width`. Points past the length (a 3-point finish at 2 of 4) fill it.
inline void pips(lgfx::LGFXBase &g,int cx,int y,unsigned scored,unsigned of,int width,uint16_t ink,uint16_t ring) {
  if(!of)return;
  // The largest dots that fit, the gaps shrinking first: seven still read as dots.
  static constexpr struct {int8_t d,gap;} SIZES[]={{10,5},{9,4},{8,3},{7,2},{6,2},{6,1},{5,1},{4,1}};
  int d=0,gap=0;
  for(const auto &s:SIZES)if(int(of)*s.d+int(of-1)*s.gap<=width){d=s.d;gap=s.gap;break;}
  if(!d) {
    // Too long a match for dots (only a later firmware asks for one): a bar.
    const int x=cx-width/2,filled=int((std::min(scored,of)*unsigned(width-2))/of);
    g.drawRoundRect(x,y+1,width,8,3,ring);if(filled)g.fillRoundRect(x+1,y+2,filled,6,2,ink);
    return;
  }
  const int span=int(of)*d+int(of-1)*gap,r=d/2;
  for(unsigned i=0;i<of;++i) {
    const int x=cx-span/2+int(i)*(d+gap)+r;
    if(i<scored)g.fillCircle(x,y+r,r,ink);
    else g.drawCircle(x,y+r,r,ring);
  }
}
// The match as two rows, this stick's above the rival's: the name of the side,
// its dots, its points.
inline void scoreRows(lgfx::LGFXBase &g,const Palette::Colors &c,int y,unsigned me,unsigned them,unsigned of) {
  const int labelX=8,numberX=g.width()-8,dotsLeft=44,dotsRight=numberX-22;
  char n[8];
  // Each row centred on its dots: Font 1 is 8 pixels high, Font 2 16.
  g.setTextDatum(top_left);g.setTextColor(c.muted,c.background);
  g.drawString("YOU",labelX,y+1,1);g.drawString("RIVAL",labelX,y+23,1);
  pips(g,(dotsLeft+dotsRight)/2,y,me,of,dotsRight-dotsLeft,c.accent,c.muted);
  pips(g,(dotsLeft+dotsRight)/2,y+22,them,of,dotsRight-dotsLeft,c.text,c.muted);
  g.setTextDatum(top_right);
  snprintf(n,sizeof(n),"%u",me);g.setTextColor(c.accent,c.background);g.drawString(n,numberX,y-3,2);
  snprintf(n,sizeof(n),"%u",them);g.setTextColor(c.text,c.background);g.drawString(n,numberX,y+19,2);
  g.setTextDatum(top_center);
}

// A full-screen card's title keeps clear of the corner marks (Bluetooth top
// left, the charging battery top right).
inline void cardTitle(lgfx::LGFXBase &g,const Palette::Colors &c,const Battle::Rival &r){title(g,c,r,8,g.width()-36);}

// The main screen while a battle is open: the status line and the level stay
// where they are (the next launch needs both), and the two launches take the
// place of the RPM readout, the faster one in the readout's color.
inline void versus(lgfx::LGFXBase &g,const Palette::Colors &c,const char *status,ScreensUI::Tone statusTone,const Battle::Rival &r) {
  const int cx=g.width()/2,left=g.width()/4,right=g.width()-g.width()/4,column=g.width()/2-6;
  g.setTextDatum(top_center);
  g.setTextColor(ScreensUI::tone(c,statusTone),c.background);
  g.drawString(status,cx,8,2);
  title(g,c,r,30,g.width()-4);
  char mine[8],theirs[8];snprintf(mine,sizeof(mine),"%u",unsigned(r.myRpm));snprintf(theirs,sizeof(theirs),"%u",unsigned(r.theirRpm));
  const int font=g.textWidth(mine,&fonts::Font4)<=column && g.textWidth(theirs,&fonts::Font4)<=column?4:2;
  g.setTextColor(r.myRpm>=r.theirRpm?c.number:c.muted,c.background);g.drawString(mine,left,font==4?52:57,font);
  g.setTextColor(r.theirRpm>=r.myRpm?c.number:c.muted,c.background);g.drawString(theirs,right,font==4?52:57,font);
  g.setTextColor(c.muted,c.background);
  g.drawString("YOU",left,81,1);g.drawString("RIVAL",right,81,1);
  // The match under each launch: this stick's dots in the accent, the rival's in text.
  // Narrower than the columns, so the two sides stay apart.
  pips(g,left,93,r.matchMe,r.matchTo,column-6,c.accent,c.muted);
  pips(g,right,93,r.matchThem,r.matchTo,column-6,c.text,c.muted);
  char line[24];
  if(r.matchPoint()){g.setTextColor(c.alert,c.background);g.drawString("MATCH POINT",cx,106,1);}
  else if(!r.matchMe && !r.matchThem){snprintf(line,sizeof(line),"First to %u",unsigned(r.matchTo));g.setTextColor(c.muted,c.background);g.drawString(line,cx,106,1);}
}

// Entering the result: the four finishes, B moving down them and A sending.
inline void picker(lgfx::LGFXBase &g,const Palette::Colors &c,const Battle::Rival &r,Battle::Outcome outcome,Battle::Finish finish) {
  const int cx=g.width()/2;
  cardTitle(g,c,r);
  const bool won=outcome==Battle::Outcome::Won;
  g.setTextColor(won?c.ready:c.warning,c.background);g.drawString(won?"WON":"LOST",cx,34,4);
  g.setTextColor(c.muted,c.background);g.drawString(won?"How did you win?":"How did you lose?",cx,66,1);
  for(unsigned i=0;i<Battle::FINISHES;++i) {
    const auto f=Battle::Finish(i);const int y=86+int(i)*27;
    char line[20];snprintf(line,sizeof(line),"%s  +%u",Battle::label(f),Battle::points(f));
    if(f==finish)g.drawRoundRect(12,y-4,g.width()-24,25,4,c.accent);
    g.setTextColor(f==finish?c.accent:c.text,c.background);g.drawString(line,cx,y,2);
  }
  g.setTextColor(c.muted,c.background);g.drawString("Hold B: back",cx,202,1);
  ControlsUI::footer(g,"A Send  B Next");
}

// How far the result has got: both sticks hold it, this one is still telling
// it, or it waits for the next battle with this rival.
enum class Told:uint8_t {Agreed,Telling,Later};
inline void result(lgfx::LGFXBase &g,const Palette::Colors &c,const Battle::Rival &r,Told told) {
  const int cx=g.width()/2;char line[32];
  cardTitle(g,c,r);
  const auto o=r.claim.outcome;const unsigned scored=Battle::points(r.claim.finish);
  const bool won=o==Battle::Outcome::Won;
  if(r.claim.scored()) {
    g.setTextColor(won?c.ready:c.warning,c.background);g.drawString(won?"WIN":"LOSS",cx,34,4);
    // The finish and what it scored, for whichever side scored it.
    snprintf(line,sizeof(line),won?"%s  +%u":"%s, rival +%u",Battle::label(r.claim.finish),scored);
    g.setTextColor(won?c.accent:c.muted,c.background);g.drawString(line,cx,68,2);
  } else {
    // Both players entered a win, or neither has it any more.
    const bool both=o==Battle::Outcome::Void;
    g.setTextColor(c.alert,c.background);g.drawString(both?"NO RESULT":"OPEN",cx,both?40:34,both?2:4);
    g.setTextColor(c.muted,c.background);g.drawString(both?"Both entered a win":"Not entered yet",cx,68,1);
  }
  // The match after this battle: the final score when it ended the match.
  const bool ended=r.matchEnded();
  scoreRows(g,c,98,ended?r.finalMe():r.matchMe,ended?r.finalThem():r.matchThem,r.matchTo);
  if(ended) {
    const int w=g.width()-24,y=140;
    g.fillRoundRect(12,y,w,24,6,won?c.accent:c.warning);
    g.setTextColor(c.background,won?c.accent:c.warning);g.drawString(won?"MATCH WON":"MATCH LOST",cx,y+4,2);
  } else if(r.matchPoint()) {
    g.setTextColor(c.alert,c.background);g.drawString("MATCH POINT",cx,148,1);
  }
  // Where the result has got, quietly: a tick once both sticks hold it.
  const char *where=told==Told::Agreed?"On both sticks":told==Told::Telling?"Sending...":"Sends at next battle";
  const int y=198,width=int(strlen(where))*6+(told==Told::Agreed?12:0),x=cx-width/2;
  if(told==Told::Agreed) {
    g.drawLine(x,y+4,x+2,y+6,c.ready);g.drawLine(x+2,y+6,x+7,y+1,c.ready);
    g.drawLine(x,y+5,x+2,y+7,c.ready);g.drawLine(x+2,y+7,x+7,y+2,c.ready);
  }
  g.setTextDatum(top_left);g.setTextColor(told==Told::Agreed?c.ready:c.muted,c.background);
  g.drawString(where,x+(told==Told::Agreed?12:0),y,1);
  g.setTextDatum(top_center);
  ControlsUI::footer(g,"A OK  B Change");
}

// Menu, Battles: Auto battles, the match length, then each rival with its
// record, then Back, six rows to a page.
static constexpr unsigned PAGE_ROWS=6,MATCH_ROW=1,FIRST_RIVAL_ROW=2;
inline void battles(lgfx::LGFXBase &g,const Battle::Book &book,unsigned row,bool bluetoothOn,bool on,uint8_t matchTo) {
  const auto &c=ControlsUI::colors;
  ControlsUI::heading(g,"Battles");
  const int cx=g.width()/2;char line[40];
  const unsigned rivals=book.count(),rows=rivals+FIRST_RIVAL_ROW+1;
  if(row>=rows)row=0;
  // Font 1 is 6 pixels a character: the wide spacing while it fits.
  snprintf(line,sizeof(line),"W %u  L %u  %u pts",shown(book.totalWins()),shown(book.totalLosses()),shown(book.totalPoints()));
  if(int(strlen(line))*6>g.width()-2)
    snprintf(line,sizeof(line),"W %u L %u %u pts",shown(book.totalWins()),shown(book.totalLosses()),shown(book.totalPoints()));
  g.setTextColor(c.muted,c.background);g.drawString(line,cx,28,1);
  const unsigned first=row/PAGE_ROWS*PAGE_ROWS;
  for(unsigned i=first;i<rows && i<first+PAGE_ROWS;++i) {
    const int y=45+int(i-first)*30;
    char label[Battle::NAME_CHARS+1];const char *detail=nullptr;char record[24];
    if(i==0){snprintf(label,sizeof(label),"Auto battles");detail=!bluetoothOn?"Needs Bluetooth":on?"On":"Off";}
    else if(i==MATCH_ROW){snprintf(label,sizeof(label),"Match");snprintf(record,sizeof(record),"First to %u",unsigned(matchTo));detail=record;}
    else if(i<FIRST_RIVAL_ROW+rivals) {
      const Battle::Rival &r=*book.recent(i-FIRST_RIVAL_ROW);Battle::displayName(r,label,sizeof(label));
      snprintf(record,sizeof(record),"%u - %u",unsigned(r.wins),unsigned(r.losses));detail=record;
    } else snprintf(label,sizeof(label),"Back");
    // A row of one line sits in the middle of its space.
    if(i==row)g.drawRoundRect(4,detail?y-3:y,g.width()-8,detail?30:24,4,c.accent);
    g.setTextColor(i==row?c.accent:c.text,c.background);
    const int font=fit(g,label,g.width()-12);
    g.drawString(label,cx,(detail?y:y+4)+(font==2?0:4),font);
    if(detail)g.drawString(detail,cx,y+18,1);
  }
  if(!rivals) {
    g.setTextColor(c.muted,c.background);
    g.drawString("Launch together with",cx,154,1);g.drawString("another LaunchLab:",cx,167,1);g.drawString("it finds your rival",cx,180,1);
  }
  ControlsUI::footer(g,row==0?(!bluetoothOn?"A Bluetooth  B Next":on?"A Turn off  B Next":"A Turn on  B Next"):
    row==MATCH_ROW?(matchTo==Battle::MATCH_LONG?"A To 4  B Next":"A To 7  B Next"):row<FIRST_RIVAL_ROW+rivals?"A Open  B Next":"A Back  B Next");
}

// One rival's record.
inline void rival(lgfx::LGFXBase &g,const Battle::Rival &r) {
  const auto &c=ControlsUI::colors;
  const int cx=g.width()/2;char line[40],name[Battle::NAME_CHARS+1];
  Battle::displayName(r,name,sizeof(name));
  g.setTextDatum(top_center);g.setTextColor(c.text,c.background);
  g.drawString(name,cx,fit(g,name,g.width()-4)==2?8:12,fit(g,name,g.width()-4));
  auto row=[&](int y,const char *text,uint16_t color,int font){g.setTextColor(color,c.background);g.drawString(text,cx,y,font);};
  snprintf(line,sizeof(line),"Won %u  Lost %u",unsigned(r.wins),unsigned(r.losses));row(34,line,c.accent,fit(g,line,g.width()-4));
  snprintf(line,sizeof(line),"Points %u - %u",unsigned(r.pointsFor),unsigned(r.pointsAgainst));row(56,line,c.text,fit(g,line,g.width()-4));
  snprintf(line,sizeof(line),"Matches %u - %u",unsigned(r.matchWins),unsigned(r.matchLosses));row(78,line,c.text,fit(g,line,g.width()-4));
  snprintf(line,sizeof(line),"First to %u: %u - %u",unsigned(r.matchTo),unsigned(r.matchMe),unsigned(r.matchThem));row(100,line,c.text,fit(g,line,g.width()-4));
  row(126,"Wins by finish",c.muted,1);
  // Two to a line, each in its own half.
  g.setTextColor(c.text,c.background);
  for(unsigned f=0;f<Battle::FINISHES;++f) {
    snprintf(line,sizeof(line),"%s %u",Battle::label(Battle::Finish(f)),shown(r.winsBy[f]));
    g.drawString(line,f&1?g.width()-g.width()/4:g.width()/4,139+int(f/2)*13,1);
  }
  row(172,"Faster launch",c.muted,1);
  snprintf(line,sizeof(line),"%u of %u",unsigned(r.duelWins),unsigned(r.duels));row(185,line,c.text,1);
  snprintf(line,sizeof(line),"Last %u v %u rpm",unsigned(r.myRpm),unsigned(r.theirRpm));row(205,line,c.muted,1);
  ControlsUI::footer(g,"A Back");
}
}
