#pragma once
// Drawing a UiText text in the screen's language, for every UI header
// (templates, so the host tests' stand-ins for lgfx::LGFXBase work too).
//
// UiDraw::text(g,s,x,y,font[,room]) draws where g.drawString(s,x,y,font)
// would (same datum). English draws exactly that. Another language draws its
// letters as the font can (UiText::glyphs): Font 1 has the umlauts itself,
// Fonts 2 and 4 draw the base letter and put the two dots over it (the
// height and size of their own i dot). Wider than `room` (default: the
// screen less 4 px each side), the text goes one font smaller (4, then 2,
// then 1, centred on the same line), then English, and last is cut with a
// '.'; each of those counts in `fallbacks`, which the tests keep at zero.
#include <cstring>
#include <cstdint>
#include "ui_text.h"

namespace UiDraw {
static unsigned fallbacks=0;
struct Overflow {const char *text=nullptr;int font=0,width=0,room=0;};
static Overflow lastOverflow;  // the last text that did not fit, for the tests to name
inline int fontHeight(int font){return font==4?26:font==2?16:font==6?48:8;}

// Width as M5GFX draws it at text size 1: the advance of each glyph, from
// M5GFX's own width tables (Font16.h, Font32rle.h; 0x60 is the degree sign).
// Font 1 is 6 px a glyph. Measured here, not by the display, so every host
// test's stand-in for the display measures the same.
static constexpr uint8_t WIDTH2[96]={6,3,4,9,8,9,9,3,7,7,8,6,3,6,5,7,8,8,8,8,8,8,8,8,8,8,3,3,6,6,6,8,
  9,8,8,8,8,8,8,8,8,4,8,8,7,10,8,8,8,8,8,8,8,8,8,10,8,8,8,4,7,4,7,9,5,7,7,7,7,7,6,7,7,4,5,6,4,8,7,8,7,8,6,6,5,7,8,8,6,7,7,5,3,5,8,6};
static constexpr uint8_t WIDTH4[96]={5,8,8,19,14,21,17,6,8,8,12,10,7,8,7,8,14,14,14,14,14,14,14,14,14,14,7,7,14,9,14,13,
  25,16,17,18,18,16,15,19,18,6,13,17,13,21,18,19,16,19,17,16,14,18,15,23,15,16,16,9,13,9,12,13,9,14,15,13,15,14,8,15,15,6,6,12,6,22,15,15,15,15,8,12,7,14,12,18,13,13,12,13,13,13,15,1};
template<class G> int width(G &,const char *text,int font) {
  if(font==1)return 6*UiText::glyphCount(text);
  int w=0;
  for(const unsigned char *c=reinterpret_cast<const unsigned char *>(text);*c;++c)
    if(*c>=32 && *c<128)w+=(font==4?WIDTH4:WIDTH2)[*c-32];
  return w;
}

// The two dots of an umlaut over its base letter, in the text colour: x
// within the letter's cell (both dots) and the row of the upper dot, from
// the font's own glyphs (Font 2: 1 px like its i dot, 1 px clear of the
// letter; Font 4: 2x2 like its i dot; its capitals reach row 1, so their
// dots sit just above the cell).
struct Dots {char base;int8_t left,right,row;};
static constexpr Dots DOTS2[]={{'a',1,4,4},{'o',2,4,4},{'u',1,4,4},{'A',2,4,1},{'O',2,4,1},{'U',1,5,1}};
static constexpr Dots DOTS4[]={{'a',3,9,2},{'o',4,9,2},{'u',4,9,2},{'A',4,10,-3},{'O',6,11,-3},{'U',5,11,-3}};
// A grave or an acute over a lowercase vowel: two steps, each a pixel in
// Font 2 and 2x2 in Font 4, in the rows the umlaut dots use, the upper step
// at `left` (grave) or one step right of it (acute). Over i (which keeps its
// own dot) and capitals none is drawn.
struct Accent {char base;int8_t left,row;};
static constexpr Accent ACCENTS2[]={{'a',2,3},{'e',2,3},{'o',2,3},{'u',2,3}};
static constexpr Accent ACCENTS4[]={{'a',5,1},{'e',5,1},{'o',5,1},{'u',5,1}};
template<class G> auto dots(G &g,const char *text,const char *shown,int x,int y,int font,int)
    ->decltype(g.getTextStyle().fore_rgb888,int(g.getTextDatum()),void()) {
  if(font!=2 && font!=4)return;
  const int datum=int(g.getTextDatum()),total=width(g,shown,font),size=font==4?2:1;
  int left=x-((datum&3)==1?total/2:(datum&3)==2?total:0);
  const int vertical=datum&12;
  y-=vertical==4?fontHeight(font)/2:vertical==8?fontHeight(font):vertical==12?(font==4?19:13):0;
  const uint32_t ink=g.getTextStyle().fore_rgb888;
  char prefix[64];size_t n=0;
  for(const char *s=text;*s && n+3<sizeof(prefix);) {
    int len;const UiText::Glyph *glyph=UiText::glyphAt(s,len);s+=len;
    if(!glyph || !glyph->mark){
      if(glyph)for(const char *a=glyph->ascii;*a;++a)prefix[n++]=*a;else prefix[n++]=len==1?s[-1]:'?';
      continue;
    }
    prefix[n]=0;
    const int at=left+width(g,prefix,font);
    prefix[n++]=glyph->ascii[0];
    if(glyph->mark==UiText::DOTS) {
      for(const Dots &d:font==4?DOTS4:DOTS2)if(d.base==glyph->ascii[0] && y+d.row>=0) {
        g.fillRect(at+d.left,y+d.row,size,size,ink);g.fillRect(at+d.right,y+d.row,size,size,ink);
      }
    }
    else for(const Accent &a:font==4?ACCENTS4:ACCENTS2)if(a.base==glyph->ascii[0]) {
      const bool grave=glyph->mark==UiText::GRAVE;
      g.fillRect(at+a.left+(grave?0:size),y+a.row,size,size,ink);g.fillRect(at+a.left+(grave?size:0),y+a.row+size,size,size,ink);
    }
  }
}
template<class G> void dots(G &,const char *,const char *,int,int,int,long){}

template<class G> void text(G &g,const char *s,int x,int y,int font,int room=0) {
  if(UiText::current()==UiText::Lang::English || (font!=1 && font!=2 && font!=4)){g.drawString(s,x,y,font);return;}
  // An English text the table knows is drawn in the language. Other ASCII
  // text the table does not know (a number, a name) draws as in English,
  // where it fits as it is laid out.
  if(const char *local=UiText::local(s))s=local;
  else if(UiText::ascii(s) && !UiText::english(s)){g.drawString(s,x,y,font);return;}
  if(room<=0)room=g.width()-8;
  char shown[64];
  for(int f=font;f;f=f==4?2:f==2?1:0) {
    UiText::glyphs(s,f,shown,sizeof(shown));
    const int w=width(g,shown,f);
    if(f==font && w>room)lastOverflow={s,font,w,room};
    if(w<=room) {
      if(f!=font)++fallbacks;
      const int top=y+(fontHeight(font)-fontHeight(f))/2;
      g.drawString(shown,x,top,f);dots(g,s,shown,x,top,f,0);return;
    }
  }
  ++fallbacks;
  if(const char *english=UiText::english(s)){g.drawString(english,x,y,font);return;}
  // Cut in Font 1: drop whole glyphs (a CP437 letter is two bytes) until the
  // text and its '.' fit.
  UiText::glyphs(s,1,shown,sizeof(shown));
  size_t n=std::strlen(shown);
  while(n && 6*(UiText::glyphCount(shown)+1)>room) {
    do --n; while(n && (uint8_t(shown[n])&0xC0)==0x80);
    shown[n]=0;
  }
  if(n+1<sizeof(shown)){shown[n]='.';shown[n+1]=0;}
  g.drawString(shown,x,y+(fontHeight(font)-8)/2,1);
}
}
