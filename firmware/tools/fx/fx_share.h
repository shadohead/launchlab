#pragma once
// Share codes for loaded effects (docs/FIRMWARE_LOADABLE_EFFECTS.md,
// "Sharing"): "LLFX1:" and the base64url (no padding) of the zlib stream of
// the packed source. The packed source is the source's tokens with comments
// and spaces dropped and each built-in word one byte; pack() and unpack()
// are here, zlib is the platform's (fxc links zlib, the website uses the
// browser's CompressionStream). A code is always compiled and validated
// after unpacking, never trusted.
#include "fx_compiler.h"
#include <string>
#include <vector>

namespace FxShare {

constexpr const char *PREFIX="LLFX1:";
// Limits on what a code may unpack to: far above any effect (the largest
// idiomatic file's source is under 8 KB), so a hostile code cannot grow
// without bound.
constexpr size_t MAX_PACKED=32768,MAX_SOURCE=65536;

// Version 1's words, one byte each when packed: 0x80+i for the first 127,
// 0xFF then 0x80+(i-127) after. Frozen: a later version only appends, so
// every version-1 code unpacks the same forever (test_fx_share pins them).
inline const std::vector<std::string> &words() {
  static const std::vector<std::string> list={
    // the language
    "effect","uid","revision","duration","readout","sound","const","frame","repeat","exit","sprite","out","fallback",
    "rgb","color","mix","table","min","max","abs","floor","fract","sin","cos","sqrt","exp","clamp01","smooth","easeout",
    "rnd","noise","red","green","blue","round","dur","ax0","ay0","ax1","ay1","acx","acy","bottom","accent","white",
    // primitives
    "clip","plot","dot","vdot","spark","twinkle","star","line","fline","segment","ellipse","glow","bolt","rimbolt","fire","prime",
    "glint","shiftrow","shade","cut","heatline","blade","emit","stroke","trail","stamp","aura","disc","runes","puff","cloud","arc",
    "heatring","vessel","rays",
    // their parameter names
    "salt","birth","birthSpan","life","lifeSpan","spreadX","spreadY","dir","dirSpread","speed","speedSpan","drag","gravity","swayAmp",
    "swayFreq","endColor","fadeIn","fadeOut","flicker","style","time","ring","x0","x1","slope","a1","f1","p1","a2","f2","p2","a3","f3","p3",
    "bumpX","bumpH","bumpW","ringA","ringF","ringP","noiseScale","noisePhase","noiseSalt","gate","endK","shimmer","shimmerFreq",
    "shimmerPhase","width","bodyColor","bodyK","bodyRows","cx","cy","rx","ry","head","sweep","tilt","tailScale","falloff","edge","rot",
    "tailK","scale","flip","color2","color3","k2","k3","shells","outer","phase","climb","dither","top","rim","rimK","dx","dy","density",
    "split","r0","r1","topW","bottomW","surface","deep","band","base","vary","low","high","lift","offset",
    // names authors use most
    "PI","age","side","seed","len","fade","level","angle","span","grow","cycle","END",
    // sound: voices, notes and their parameters (appended before 0.19 shipped)
    "voice","note","notes","tone","filter","wave","sine","square","triangle","saw","none",
    "gain","hz","bend","octave","cubic","trigger","attack","decay","length","at"};
  return list;
}

namespace detail {
inline bool wordChar(char c){return std::isalnum(static_cast<unsigned char>(c)) || c=='_' || c=='.';}
inline int wordIndex(const std::string &w) {
  const auto &list=words();
  for(size_t i=0;i<list.size();++i)if(list[i]==w)return int(i);
  return -1;
}
}

// The packed source, or an error with its line.
struct Packed {bool ok=false;std::string error;int line=0;std::string bytes;};

inline Packed pack(const std::string &source) {
  Packed r;
  std::string error;int line=0;
  const auto tokens=FxCompiler::detail::lex(source,error,line);
  if(tokens.empty()){r.error=error;r.line=line;return r;}
  using FxCompiler::detail::Tok;
  std::string &out=r.bytes;
  // Whether the last thing written ends in a name or number character, so a
  // following name or number needs a space to stay apart.
  bool wordy=false;std::string lastPunct;
  for(const auto &t:tokens) {
    switch(t.kind) {
      case Tok::End:break;
      case Tok::Newline:
        if(!out.empty() && out.back()!='\n')out+='\n';
        wordy=false;lastPunct.clear();break;
      case Tok::Ident: {
        const int w=detail::wordIndex(t.text);
        if(w>=0) {
          if(w<127)out+=char(0x80+w);else {out+=char(0xFF);out+=char(0x80+(w-127));}
          wordy=false;
        } else {
          if(wordy)out+=' ';
          out+=t.text;wordy=true;
        }
        lastPunct.clear();break;
      }
      case Tok::Number:
        if(wordy)out+=' ';
        out+=t.text;wordy=true;lastPunct.clear();break;
      case Tok::String:
        out+='"';out+=t.text;out+='"';wordy=false;lastPunct.clear();break;
      case Tok::Punct: {
        // Two operators that would read as one (`<` `=`) keep a space.
        static const char *two[]={"<=",">=","==","!=","&&","||"};
        if(!lastPunct.empty())for(const char *p:two)if(lastPunct.back()==p[0] && t.text[0]==p[1]){out+=' ';break;}
        out+=t.text;wordy=false;lastPunct=t.text;break;
      }
    }
  }
  while(!out.empty() && out.back()=='\n')out.pop_back();
  if(out.size()>MAX_PACKED){r.error="effect too large to share";return r;}
  r.ok=true;return r;
}

namespace detail {
using FxCompiler::detail::Tok;using FxCompiler::detail::Token;
inline bool binaryOp(const Token &t) {
  if(t.kind!=Tok::Punct)return false;
  static const char *ops[]={"=","+","-","<",">","<=",">=","==","!=","&&","||","?"};
  for(const char *o:ops)if(t.text==o)return true;
  return false;
}
// A minus or plus is unary after nothing, an operator, a comma or an opening bracket.
inline bool unaryOp(const Token *before,const Token &t) {
  return (t.text=="-" || t.text=="+") && t.kind==Tok::Punct && (!before || (before->kind==Tok::Punct && before->text!=")"));
}
// Whether the formatter puts a space between prev and t (prev2 before prev).
inline bool spaceBefore(const Token *prev2,const Token *prev,const Token &t,int parens) {
  const bool pWord=prev->kind!=Tok::Punct,tWord=t.kind!=Tok::Punct;
  if(t.kind==Tok::Punct && (t.text==")" || t.text==","))return false;
  if(prev->kind==Tok::Punct && prev->text=="(")return false;
  if(prev->kind==Tok::Punct && (prev->text=="," || prev->text==":" || prev->text=="{"))return true;
  if(t.kind==Tok::Punct && t.text=="{")return true;
  if(t.kind==Tok::Punct && t.text==":") {
    // A named argument (`name: value`) takes no space before its colon; a ?: does.
    return !(parens>0 && prev->kind==Tok::Ident && prev2 && prev2->kind==Tok::Punct && (prev2->text=="(" || prev2->text==","));
  }
  if(binaryOp(*prev) && !unaryOp(prev2,*prev))return true;
  if(binaryOp(t))return !unaryOp(prev,t);
  if(t.kind==Tok::Punct && t.text=="!")return pWord || prev->text==")";
  if(pWord && tWord)return true;
  if(t.kind==Tok::Punct && t.text=="(")return prev->kind==Tok::Ident && prev2 && prev2->kind==Tok::Ident && prev2->text=="sprite";
  if(prev->kind==Tok::Punct && prev->text==")" && tWord)return true;
  return false;
}
}

// The source, formatted for reading (two-space indents, spaces round the
// operators that take them), or an error.
struct Unpacked {bool ok=false;std::string error;std::string source;};

inline Unpacked unpack(const std::string &packed) {
  Unpacked r;
  if(packed.size()>MAX_PACKED){r.error="code too large";return r;}
  // Words back to text: a minified source.
  std::string text;const auto &list=words();
  for(size_t i=0;i<packed.size();++i) {
    const unsigned char c=static_cast<unsigned char>(packed[i]);
    if(c>=0x80) {
      size_t w;
      if(c==0xFF){if(i+1>=packed.size() || static_cast<unsigned char>(packed[i+1])<0x80){r.error="damaged code";return r;}w=127+(static_cast<unsigned char>(packed[++i])-0x80);}
      else w=c-0x80;
      if(w>=list.size()){r.error="code from a newer version";return r;}
      if(!text.empty() && detail::wordChar(text.back()))text+=' ';
      text+=list[w];
      if(i+1<packed.size() && detail::wordChar(packed[i+1]))text+=' ';
      continue;
    }
    if(c!='\n' && c!=' ' && (c<0x20 || c==0x7F)){r.error="damaged code";return r;}
    text+=char(c);
    if(text.size()>MAX_SOURCE){r.error="code too large";return r;}
  }
  // Then print it again from its tokens.
  std::string error;int line=0;
  const auto tokens=FxCompiler::detail::lex(text,error,line);
  if(tokens.empty()){r.error="damaged code";return r;}
  using FxCompiler::detail::Tok;
  std::string &out=r.source;int depth=0,parens=0;bool lineStart=true;
  const FxCompiler::detail::Token *prev=nullptr,*prev2=nullptr;
  for(size_t k=0;k<tokens.size();++k) {
    const auto &t=tokens[k];
    if(t.kind==Tok::End)break;
    if(t.kind==Tok::Newline){out+='\n';lineStart=true;prev2=prev=nullptr;continue;}
    if(t.kind==Tok::Punct && t.text=="}")depth=std::max(0,depth-1);
    if(lineStart){out.append(size_t(2*depth),' ');lineStart=false;}
    else if(prev && detail::spaceBefore(prev2,prev,t,parens))out+=' ';
    if(t.kind==Tok::String){out+='"';out+=t.text;out+='"';}
    else out+=t.text;
    if(t.kind==Tok::Punct) {
      if(t.text=="{")++depth;
      else if(t.text=="(")++parens;
      else if(t.text==")" && parens)--parens;
    }
    prev2=prev;prev=&t;
    if(out.size()>MAX_SOURCE){r.error="code too large";r.source.clear();return r;}
  }
  if(!out.empty() && out.back()!='\n')out+='\n';
  r.ok=true;return r;
}

// base64url without padding.
inline std::string base64url(const std::vector<uint8_t> &bytes) {
  static const char *A="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;out.reserve((bytes.size()*4+2)/3);
  for(size_t i=0;i<bytes.size();i+=3) {
    const uint32_t v=uint32_t(bytes[i])<<16|(i+1<bytes.size()?uint32_t(bytes[i+1])<<8:0)|(i+2<bytes.size()?bytes[i+2]:0);
    out+=A[v>>18];out+=A[(v>>12)&63];
    if(i+1<bytes.size())out+=A[(v>>6)&63];
    if(i+2<bytes.size())out+=A[v&63];
  }
  return out;
}
// The bytes, or false for a character outside base64url or an impossible length.
inline bool fromBase64url(const std::string &text,std::vector<uint8_t> &out) {
  out.clear();uint32_t acc=0;int bits=0;
  if(text.size()%4==1)return false;
  for(const char c:text) {
    int v;
    if(c>='A' && c<='Z')v=c-'A';else if(c>='a' && c<='z')v=c-'a'+26;else if(c>='0' && c<='9')v=c-'0'+52;
    else if(c=='-')v=62;else if(c=='_')v=63;else return false;
    acc=acc<<6|uint32_t(v);bits+=6;
    if(bits>=8){bits-=8;out.push_back(uint8_t(acc>>bits));acc&=(1u<<bits)-1;}
  }
  return acc==0;  // the unused low bits of the last character must be zero
}

// The code's payload (still zlib-compressed) from whatever was pasted: a
// code, a line holding one, or a link whose fragment is one. Line breaks
// inside it (as a chat app may wrap it) are ignored; a space ends it.
struct Payload {bool ok=false;std::string error;std::vector<uint8_t> zlib;};
inline Payload payload(const std::string &pasted) {
  Payload r;
  const size_t at=pasted.find(PREFIX);
  if(at==std::string::npos){r.error=pasted.find("LLFX")!=std::string::npos?"code from a newer version":"not an effect code";return r;}
  std::string body;
  for(size_t i=at+std::char_traits<char>::length(PREFIX);i<pasted.size();++i) {
    const char c=pasted[i];
    if(c=='\n' || c=='\r')continue;
    if(!(std::isalnum(static_cast<unsigned char>(c)) || c=='-' || c=='_'))break;  // the code ends at anything else
    body+=c;
  }
  if(body.empty() || !fromBase64url(body,r.zlib)){r.error="damaged code";return r;}
  r.ok=true;return r;
}
inline std::string code(const std::vector<uint8_t> &zlib){return PREFIX+base64url(zlib);}

}  // namespace FxShare
