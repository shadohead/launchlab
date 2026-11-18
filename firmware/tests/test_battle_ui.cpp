// The battle cards and the Battles pages (battle_ui.h) with the largest
// values a record can hold and the widest names: nothing leaves the screen
// and no two lines of text share pixels. Device fonts are pinned by
// check_ui_render; these deliberately conservative host metrics catch overflow.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
static constexpr int top_center=0,top_left=1,top_right=2;
namespace fonts {static int Font6=6,Font4=4,Font2=2;}
namespace lgfx {
struct LGFXBase {
  struct Text {int left,right,top,bottom;std::string value;};
  std::vector<Text> texts;unsigned footers=0;int datum=top_center;
  bool drew(const char *text)const{for(const auto &t:texts)if(t.value==text)return true;return false;}
  int width()const{return 135;}
  void setTextDatum(int d){datum=d;}void setTextColor(uint16_t,uint16_t){}
  void fillScreen(uint16_t){}
  void fillRect(int x,int y,int w,int h,uint16_t){assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=135 && y+h<=240);}
  void fillRoundRect(int x,int y,int w,int h,int,uint16_t){fillRect(x,y,w,h,0);}
  void drawRoundRect(int x,int y,int w,int h,int,uint16_t){fillRect(x,y,w,h,0);}
  void drawCircle(int,int,int,uint16_t){}void fillCircle(int,int,int,uint16_t){}
  void drawFastHLine(int,int,int,uint16_t){}void drawFastVLine(int,int,int,uint16_t){}
  void drawLine(int x0,int y0,int x1,int y1,uint16_t){assert(std::min(x0,x1)>=0 && std::max(x0,x1)<135 && std::min(y0,y1)>=0 && std::max(y0,y1)<240);}
  // At or above M5GFX's own widths: Font 1 is 6 px a glyph; Font 2 is
  // proportional, 8 at most and 5 for the narrow ones; Font 4's digits and
  // capitals are 14 to 18.
  static int measure(const char *text,int font) {
    if(font==2){int w=0;for(const char *c=text;*c;++c)w+=std::strchr("il .,:;'!|",*c)?5:8;return w;}
    return int(std::strlen(text))*(font==6?30:font==4?18:6);
  }
  int textWidth(const char *text,const int *font)const{return measure(text,*font);}
  void drawString(const char *text,int x,int y,int font) {
    const int w=measure(text,font),h=font==6?48:font==4?26:font==2?16:8;
    const int left=datum==top_left?x:datum==top_right?x-w:x-w/2,right=left+w;
    if(left<0 || right>135 || y<0 || y+h>240)std::fprintf(stderr,"off screen: \"%s\" font %d at %d..%d, %d..%d\n",text,font,left,right,y,y+h);
    assert(left>=0 && right<=135 && y>=0 && y+h<=240);
    for(const auto &t:texts) {
      const bool overlap=left<t.right && t.left<right && y<t.bottom && t.top<y+h;
      if(overlap)std::fprintf(stderr,"\"%s\" overlaps \"%s\"\n",text,t.value.c_str());
      assert(!overlap);
    }
    texts.push_back({left,right,y,y+h,text});
    if(y==231)++footers;
  }
};
}
#include "battle_ui.h"

using namespace Battle;

// A rival at the limits of its record, under `name`, one point from either
// player winning a match of `to`.
static Book extreme(const char *name,uint8_t to=MATCH_SHORT,uint32_t id=0xd5d2ffff) {
  Book::Image image;Book empty;empty.encode(image);
  uint8_t *p=image.data();p[5]=1;put32(p+12,1);put32(p+16,0xffffffffu);put32(p+20,0xffffffffu);
  uint8_t *q=p+Book::HEADER;put32(q,id);std::memcpy(q+4,name,std::strlen(name));
  for(unsigned at=20;at<44;at+=2)put16(q+at,65535);
  const uint8_t last=uint8_t(to-1);
  q[44]=last;q[45]=last;put16(q+46,7);put16(q+48,9);q[50]=Claim{Outcome::Won,Finish::Xtreme,15}.byte();q[51]=last;q[52]=last;
  put16(q+53,65535);put16(q+55,65535);put32(q+57,1);q[61]=to;
  put32(p+Book::IMAGE_BYTES-8,0xffffffffu);
  uint32_t h=2166136261u;for(size_t i=0;i<Book::IMAGE_BYTES-4;++i){h^=p[i];h*=16777619u;}
  put32(p+Book::IMAGE_BYTES-4,h);
  Book book;assert(book.decode(image.data(),image.size()));return book;
}

int main() {
  const Palette::Colors c=Themes::builtIn(Themes::FALLBACK);
  ControlsUI::colors=c;
  unsigned renders=0;
  const char *names[]={"","K","Kai","LL-3C11","SixteenCharsLong","WWWWWWWWWWWWWWWW","MMMM MMMM MMMM M","iiiiiiiiiiiiiiii","w w w w w w w w "};
  for(const char *name:names) {
    // Fresh: one launch together, nothing entered. Then at the limits.
    Book fresh;fresh.begin(0xd5d23c11,1,2,6420,5980);if(name[0])assert(fresh.rename(0xd5d23c11,name));
    const Book full=extreme(name),longest=extreme(name,MATCH_MAX);
    for(const Book *book:{static_cast<const Book *>(&fresh),&full,&longest}) {
      const Rival &r=*book->recent(0);
      for(const char *status:{"Ready","Check sensor","New session ready","Settling","Measuring"}) {
        lgfx::LGFXBase g;BattleUI::versus(g,c,status,ScreensUI::Tone::Ready,r);
        assert(g.drew("YOU") && g.drew("RIVAL"));++renders;
      }
      for(unsigned o=1;o<=2;++o)for(unsigned f=0;f<FINISHES;++f) {
        lgfx::LGFXBase g;BattleUI::picker(g,c,r,Outcome(o),Finish(f));
        assert(g.footers==1 && g.drew(o==1?"WON":"LOST") && g.drew("Xtreme  +3"));++renders;
      }
      for(auto told:{BattleUI::Told::Agreed,BattleUI::Told::Telling,BattleUI::Told::Later}) {
        lgfx::LGFXBase g;BattleUI::result(g,c,r,told);assert(g.footers==1);++renders;
      }
      {lgfx::LGFXBase g;BattleUI::rival(g,r);assert(g.footers==1 && g.drew("Wins by finish"));++renders;}
    }
    // Every result a battle can have, from every match score it can start at,
    // in a match of either length.
    for(unsigned to:{MATCH_SHORT,MATCH_LONG})
    for(unsigned me=0;me<to;++me)for(unsigned them=0;them<to;++them)for(unsigned o=0;o<4;++o)for(unsigned f=0;f<FINISHES;++f) {
      Book book;book.begin(7,1,2,6000,6100,uint8_t(to));if(name[0])book.rename(7,name);
      // Up to the starting score: Spin finishes each way, then the battle.
      for(unsigned i=0;i<me;++i){book.claim(7,Outcome::Won,Finish::Spin);book.begin(7,uint16_t(10+i),uint16_t(20+i),1,1);}
      for(unsigned i=0;i<them;++i){book.claim(7,Outcome::Lost,Finish::Spin);book.begin(7,uint16_t(30+i),uint16_t(40+i),1,1);}
      const Rival &r=*book.find(7);
      assert(r.matchMe==me && r.matchThem==them && r.matchTo==to);
      if(o){Claim next;next.rev=1;next.outcome=Outcome(o);next.finish=o==3?Finish::Spin:Finish(f);assert(book.merge(7,r.theirTag,next));}
      lgfx::LGFXBase g;BattleUI::result(g,c,r,BattleUI::Told::Agreed);
      assert(g.footers==1 && g.drew(o==1?"WIN":o==2?"LOSS":o==3?"NO RESULT":"OPEN"));
      const bool ended=o==1?me+points(Finish(f))>=to:o==2 && them+points(Finish(f))>=to;
      assert(r.matchEnded()==ended);
      // The score after it, each side on its own row, and the match's end or its last point.
      char mine[8],theirs[8];
      snprintf(mine,sizeof(mine),"%u",o==1?me+points(Finish(f)):me);snprintf(theirs,sizeof(theirs),"%u",o==2?them+points(Finish(f)):them);
      assert(g.drew("YOU") && g.drew("RIVAL") && g.drew(mine) && g.drew(theirs));
      if(ended)assert(g.drew(o==1?"MATCH WON":"MATCH LOST") && !g.drew("MATCH POINT"));
      else assert(g.drew("MATCH POINT")==r.matchPoint() && !g.drew("MATCH WON") && !g.drew("MATCH LOST"));
      ++renders;
    }
  }
  // The Battles page: every row of an empty, a part-filled and a full book,
  // with Bluetooth and Auto battles each way.
  for(unsigned rivals:{0u,1u,5u,6u,Book::CAPACITY}) {
    Book book;
    const char *names[]={"Kai","WWWWWWWWWWWWWWWW","","SixteenCharsLong"};
    for(uint32_t i=1;i<=rivals;++i) {
      book.begin(i,uint16_t(i),uint16_t(i),6000,5000);
      if(names[i%4][0])book.rename(i,names[i%4]);
      book.claim(i,i&1?Outcome::Won:Outcome::Lost,Finish(i%FINISHES));
    }
    for(unsigned row=0;row<rivals+3;++row)for(bool bluetooth:{false,true})for(bool on:{false,true})for(uint8_t to:{MATCH_SHORT,MATCH_LONG}) {
      lgfx::LGFXBase g;BattleUI::battles(g,book,row,bluetooth,on,to);
      assert(g.footers==1 && g.drew("Battles"));
      // Six rows to a page: the selected row is always on it.
      if(row==0)assert(g.drew("Auto battles") && g.drew(!bluetooth?"Needs Bluetooth":on?"On":"Off"));
      if(row==1)assert(g.drew("Match") && g.drew(to==MATCH_LONG?"First to 7":"First to 4") && g.drew(to==MATCH_LONG?"A To 4  B Next":"A To 7  B Next"));
      if(row==rivals+2)assert(g.drew("Back") && g.drew("A Back  B Next"));
      if(!rivals)assert(g.drew("Launch together with"));
      ++renders;
    }
    // A row past the end (a rival replaced since) draws the first page.
    {lgfx::LGFXBase g;BattleUI::battles(g,book,rivals+5,true,true,MATCH_SHORT);assert(g.drew("Auto battles"));++renders;}
  }
  // The totals line holds whatever the totals are.
  {const Book full=extreme("Kai");lgfx::LGFXBase g;BattleUI::battles(g,full,0,true,true,MATCH_SHORT);assert(g.drew("W 9999 L 9999 9999 pts"));++renders;}
  std::cout<<"PASS: "<<renders<<" battle cards and Battles pages stay on screen with no two lines sharing pixels, at the record's limits and with the widest names\n";
}
