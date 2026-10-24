#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
// Host only: the engine counts each primitive's calls and steps for the validator's cost model.
#ifndef LLFX_COUNTS
#define LLFX_COUNTS
#endif
#include "fx_effect.h"

// Compiles an effect's source text into an LLFX file (fx_effect.h) for the
// host tools, the tests and the web editor's WebAssembly build. Not part of
// the firmware: the device only loads and validates compiled files.
//
// Source, one statement per line (# starts a comment):
//
//   effect "Orbit"          name, 1-14 printable ASCII
//   uid 0x4c4c000000000001  stable identity across revisions
//   revision 1
//   duration 5000           ms, 1000-6000
//   readout 1500            ms the readout primitives may run (<= 2200, <= 3/4 of the duration)
//   sound 1700              ms of sound (0-2800)
//   const SPIN = 1.05       compile-time numbers and colours: rgb(255,214,110)
//   frame { ... }           runs once a frame; its variables are seen by later blocks
//   repeat 18 { ... }       runs 18 times a frame with i = 0..17 and n = 18
//
// The sound (docs/FIRMWARE_LOADABLE_EFFECTS.md, "Sound"): native voices,
// notes timed to the sample, and a control block run every 2 ms.
//
//   voice hum = tone(cubic: .3)                 a voice; settings are constants
//   voice ping[8] = tone(octave: .25, attack: .002, decay: 16, length: .32)
//   voice hiss = noise(seed: 0x27d4eb2f, filter: band, k: .18, k2: .04)
//   note(ping, at: .1, level: .2, hz: 2093)     a note: ping[k] plays it, or a free voice of ping
//   notes 18 { at = .1 + rnd(i, 1)  note(ping, at: at, level: .16, hz: 3136) }
//                                               notes from a loop worked out when compiling
//   sound { hum(hz: 240 + 1500*t, gain: .3*smooth(t/.12)) }
//                                               every 2 ms: t, i (the step); sets voices' parameters
//
// A tone: wave sine|square|triangle|saw, hz (or bend: the notes' frequency
// times this), octave, cubic. Noise: seed. Both: gain, filter low|band|high
// with k (and k2 for band), trigger, attack, decay (per second), length. A
// voice played by notes or a trigger is silent until then and decays from
// each; any other sounds at its gain throughout.
//
// Statements: `name = expression`, `exit condition` (ends this run of the
// block) and a primitive call such as `plot(x, y, colour, k)`. Expressions
// have numbers and colours, + - * / %, comparisons (1 or 0), && || !
// (on 0/1 values), c ? a : b (both sides are computed), and sin cos sqrt
// exp abs floor fract round clamp01 smooth easeout min max mix rnd noise
// rgb color. Built-in values: t (s), dur, W, H, ax0 ay0 ax1 ay1 acx acy
// (the readout's box and centre), i, n, bottom, frame (t*20), and the
// colours accent and white.
namespace FxCompiler {
using namespace LoadableFx;

struct Result {
  bool ok=false;std::string error;int line=0;
  std::vector<uint8_t> bytes;
  std::vector<std::string> blocks;  // per block: "count x length"
};

namespace detail {
enum class Tok {End,Newline,Number,Ident,String,Punct};
struct Token {Tok kind;std::string text;double number=0;int line=0;};

inline std::vector<Token> lex(const std::string &s,std::string &error,int &errorLine) {
  std::vector<Token> out;int line=1;size_t i=0;int depth=0;
  while(i<s.size()) {
    const char c=s[i];
    if(c=='#'){while(i<s.size() && s[i]!='\n')++i;continue;}
    if(c=='\n'){if(!depth)out.push_back({Tok::Newline,"\n",0,line});++line;++i;continue;}
    if(c==' ' || c=='\t' || c=='\r'){++i;continue;}
    if(std::isdigit(static_cast<unsigned char>(c)) || (c=='.' && i+1<s.size() && std::isdigit(static_cast<unsigned char>(s[i+1])))) {
      size_t j=i;
      if(c=='0' && j+1<s.size() && (s[j+1]=='x' || s[j+1]=='X')) {
        j+=2;while(j<s.size() && std::isxdigit(static_cast<unsigned char>(s[j])))++j;
      } else {
        while(j<s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j]=='.'))++j;
        if(j<s.size() && (s[j]=='e' || s[j]=='E')){++j;if(j<s.size() && (s[j]=='+' || s[j]=='-'))++j;while(j<s.size() && std::isdigit(static_cast<unsigned char>(s[j])))++j;}
      }
      Token t{Tok::Number,s.substr(i,j-i),0,line};
      t.number=t.text.size()>1 && (t.text[1]=='x' || t.text[1]=='X')?double(std::strtoull(t.text.c_str(),nullptr,16)):std::strtod(t.text.c_str(),nullptr);
      out.push_back(t);i=j;continue;
    }
    if(std::isalpha(static_cast<unsigned char>(c)) || c=='_') {
      size_t j=i;while(j<s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j]=='_'))++j;
      out.push_back({Tok::Ident,s.substr(i,j-i),0,line});i=j;continue;
    }
    if(c=='"') {
      size_t j=i+1;while(j<s.size() && s[j]!='"' && s[j]!='\n')++j;
      if(j>=s.size() || s[j]!='"'){error="unterminated string";errorLine=line;return {};}
      // Names travel in share codes from strangers: plain printable ASCII only (no control, bidi or other Unicode).
      for(size_t k=i+1;k<j;++k)if(static_cast<unsigned char>(s[k])<0x20 || static_cast<unsigned char>(s[k])>0x7e){error="only plain letters, digits, spaces and punctuation in quotes";errorLine=line;return {};}
      out.push_back({Tok::String,s.substr(i+1,j-i-1),0,line});i=j+1;continue;
    }
    static const char *two[]={"<=",">=","==","!=","&&","||"};
    bool matched=false;
    for(const char *t:two)if(s.compare(i,2,t)==0){out.push_back({Tok::Punct,t,0,line});i+=2;matched=true;break;}
    if(matched)continue;
    if(std::strchr("+-*/%(){},=<>!?:;[]",c)) {
      if(c=='(')++depth;
      if(c==')' && depth)--depth;
      out.push_back({Tok::Punct,std::string(1,c),0,line});++i;continue;
    }
    error=std::string("unexpected character '")+c+"'";errorLine=line;return {};
  }
  out.push_back({Tok::End,"",0,line});
  return out;
}

struct Node {
  enum Kind {Number,Name,Unary,Binary,Ternary,Call} kind;
  std::string text;double number=0;int line=0;
  std::vector<std::unique_ptr<Node>> kids;
};
using NodeP=std::unique_ptr<Node>;

struct Value {bool color=false,constant=false;float c[3]={0,0,0};int reg=-1;};

// args: F number, C colour, all given in order. params, when set, names
// them and gives defaults ("name:T=default", the default a number, a
// built-in name or an earlier parameter's name; none means required), so a
// call may name its arguments after any positional ones.
struct Signature {const char *name;uint8_t prim;const char *args;const char *params=nullptr;};
inline const Signature *signature(const std::string &name) {
  static const Signature table[]={
    {"clip",CLIP,"F"},{"plot",PLOT,"FFCF"},{"dot",DOT,"FFCF"},{"vdot",VDOT,"FFCF"},{"spark",SPARK,"FFCFF"},
    {"twinkle",TWINKLE,"FFCFF"},{"star",STAR,"FFFC"},{"line",LINE,"FFFFCF"},{"fline",FLINE,"FFFFCFF"},
    {"segment",SEGMENT,"FFFFCF"},{"ellipse",ELLIPSE,"FFFFCFFFF"},{"glow",GLOW,"FFFFCF"},{"bolt",BOLT,"FFFFFFF"},
    {"rimbolt",RIMBOLT,"FFFFFCCFF"},{"fire",FIRE,"FFF"},{"prime",PRIME,"FF"},{"glint",GLINT,"FF"},
    {"shiftrow",SHIFTROW,"FF"},{"shade",SHADE,"CFF"},{"cut",CUT,"FFFF"},{"heatline",HEATLINE,"FFFFCF"},{"blade",BLADE,"FFFFFCF"},
    {"emit",EMIT,"FFFFFFFFFFFFFFFFFFCCFFFFFFFF","n:F,salt:F=0,birth:F=0,birthSpan:F=0,life:F=1,lifeSpan:F=0,x:F,y:F,spreadX:F=0,spreadY:F=0,"
      "dir:F=0,dirSpread:F=3.14159265,speed:F=0,speedSpan:F=0,drag:F=0,gravity:F=0,swayAmp:F=0,swayFreq:F=0,color:C,endColor:C=color,"
      "k:F=1,fadeIn:F=0,fadeOut:F=0,flicker:F=0,style:F=0,trail:F=0,time:F=t,ring:F=0"},
    {"stroke",STROKE,"FFFFFFFFFFFFFFFFFFFFFFFFCCFFFFFFFCFF","x0:F=0,x1:F=191,y:F,slope:F=0,a1:F=0,f1:F=0,p1:F=0,a2:F=0,f2:F=0,p2:F=0,a3:F=0,f3:F=0,p3:F=0,"
      "bumpX:F=0,bumpH:F=0,bumpW:F=1,ringA:F=0,ringF:F=0,ringP:F=0,noise:F=0,noiseScale:F=0,noisePhase:F=0,noiseSalt:F=0,gate:F=0,"
      "color:C,endColor:C=color,k:F=1,endK:F=k,shimmer:F=0,shimmerFreq:F=0,shimmerPhase:F=0,width:F=0,smooth:F=0,bodyColor:C=color,bodyK:F=0,bodyRows:F=0"},
    {"trail",TRAIL,"FFFFFFFCCFFFFFFFCF","cx:F,cy:F,rx:F,ry:F,head:F,sweep:F,n:F,color:C,endColor:C=color,k:F=1,tilt:F=0,tailScale:F=1,falloff:F=2,edge:F=0,"
      "rot:F=0,width:F=0,bodyColor:C=color,tailK:F=1"},
    {"stamp",STAMP,"FFFFFCCCFFF","sprite:F,x:F,y:F,scale:F=1,flip:F=0,color:C,color2:C=color,color3:C=color,k:F=1,k2:F=k,k3:F=k2"},
    {"aura",AURA,"FCCFFFF","shells:F,color:C,outer:C=color,k:F=1,phase:F=0,climb:F=0,dither:F=0"},
    {"disc",DISC,"FFFFCFFFCFFFF","cx:F,cy:F,rx:F,ry:F,color:C,k:F=1,dither:F=16,top:F=-1000,rim:C=color,rimK:F=0,noise:F=0,noiseScale:F=0,noisePhase:F=0"},
    {"runes",RUNES,"FFFFFCFF","x:F,y:F,dx:F,dy:F,n:F,color:C,k:F=1,salt:F=0"},
    {"puff",PUFF,"FFFF","x:F,y:F,r:F,k:F=1"},
    {"cloud",CLOUD,"CCFFF","color:C,rim:C=color,bodyK:F=.6,density:F=5,top:F=-1000"},
    {"arc",ARC,"FFFFCFF","cx:F,cy:F,rx:F,ry:F,color:C,k:F=1,split:F=0"},
    {"heatring",HEATRING,"FFFFCF","cx:F,cy:F,r0:F,r1:F,color:C,k:F=1"},
    {"vessel",VESSEL,"FFFFFFCCFFF","cx:F,top:F,bottom:F,topW:F,bottomW:F,surface:F,color:C,deep:C=color,k:F=1,band:F=3.5,phase:F=0"},
    {"rays",RAYS,"FFFFFFFFFFCFFFFF","cx:F,cy:F,r:F,n:F,base:F,vary:F=0,scale:F=1,low:F=1,high:F=low,lift:F=0,color:C,k:F=1,endK:F=k,phase:F=0,salt:F=0,offset:F=0"}};
  for(const auto &s:table)if(name==s.name)return &s;
  return nullptr;
}

// Where each constant lives: the pool, its first MAX_CONSTS entries in
// registers. A first pass weighs every use by how often it runs a frame,
// so the busiest constants get the registers.
struct Plan {std::vector<float> pool;std::map<std::string,int> index;std::map<std::string,std::pair<std::vector<float>,double>> usage;};
inline std::string constKey(const float *v,int n){return std::string(reinterpret_cast<const char *>(v),size_t(n)*4);}

class Compiler {
public:
  Compiler(std::vector<Token> tokens,Plan &plan,bool planning):tok_(std::move(tokens)),plan_(plan),planning_(planning){}
  Result run() {
    Result r;
    try {
      parseFile();
      r.bytes=assemble();
      for(const auto &b:blocks_)r.blocks.push_back(std::to_string(b.count)+" x "+std::to_string(b.length));
      r.ok=true;
    } catch(const Failure &f){r.error=f.message;r.line=f.line;}
    return r;
  }

private:
  struct Failure {std::string message;int line;};
  std::vector<Token> tok_;size_t at_=0;
  std::string name_;uint64_t uid_=0;uint16_t revision_=1,duration_=0,readout_=0,sound_=0;uint8_t fallback_=Program::NO_FALLBACK;
  std::vector<float> consts_;std::vector<uint8_t> code_,args_;std::vector<Block> blocks_;
  std::vector<uint8_t> soundCode_;bool soundBlock_=false;
  // Engine level 2 (pulse, wave, fm and drum voices, stepped noise, a song):
  // the file says level 2 only when it uses one, so others play on 0.19.
  bool level2_=false;
  // The song (SONG section body), built when its block is compiled.
  std::vector<uint8_t> song_;
  // The sound's voices (arrays expanded), their names and the notes.
  struct VoiceDecl {
    std::string name;int line=0;uint8_t type=TONE_VOICE,wave=WAVE_SINE,filter=NO_FILTER;uint32_t seed=0,datum=0;
    float value[VOICE_PARAMS]={};bool given[VOICE_PARAMS]={};int reg[VOICE_PARAMS]={-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    bool hzNamed=false,bendNamed=false,pitched=false,played=false;
  };
  static_assert(VOICE_PARAMS==10,"one initial register per parameter");
  std::vector<VoiceDecl> voices_;
  struct VoiceRef {int first=0,count=1;bool array=false;};
  std::map<std::string,VoiceRef> voiceNames_;
  struct Note {uint32_t sample=0;int first=0,count=1;bool allocate=false;float level=1,hz=0;int line=0;size_t order=0;};
  std::vector<Note> notes_;
  // Names a notes block's loop knows when compiling (i, n and its own).
  std::map<std::string,Value> score_;
  struct SpriteData {int w=0,h=0;std::vector<uint8_t> px;};std::vector<SpriteData> sprites_;
  std::vector<std::vector<float>> tables_;
  std::map<std::string,Value> constants_,globals_,locals_;
  bool inFrame_=false;int globalTop_=FIRST_TEMP,localTop_=FIRST_TEMP,sp_=FIRST_TEMP;
  // Code goes to out_: a repeat block's run-once prologue or its body.
  std::vector<uint8_t> *out_=&code_;
  // While a repeat block's body is generated: which names vary between
  // runs, where invariant subexpressions go, and the registers holding
  // them, allocated downward from hoistTop_.
  const std::map<std::string,bool> *vary_=nullptr;std::vector<uint8_t> *prologue_=nullptr;
  std::map<std::string,Value> hoisted_,cse_;int hoistTop_=FIRST_CONST;
  Plan &plan_;bool planning_;double weight_=1;

  [[noreturn]] void fail(const std::string &m,int line=-1){throw Failure{m,line<0?peek().line:line};}
  const Token &peek(size_t ahead=0)const{return tok_[std::min(at_+ahead,tok_.size()-1)];}
  const Token &next(){const Token &t=peek();if(at_<tok_.size()-1)++at_;return t;}
  bool punct(const char *p){if(peek().kind==Tok::Punct && peek().text==p){next();return true;}return false;}
  void expect(const char *p){if(!punct(p))fail(std::string("expected '")+p+"'");}
  void skipNewlines(){while(peek().kind==Tok::Newline || (peek().kind==Tok::Punct && peek().text==";"))next();}
  void endStatement(){if(peek().kind==Tok::Newline || peek().kind==Tok::End || punct(";")){if(peek().kind==Tok::Newline)next();return;}if(peek().kind==Tok::Punct && peek().text=="}")return;fail("expected end of line");}
  // A header number: a whole number 0-65535 (the header's fields are 16 bits; their ranges are the loader's to check).
  uint16_t integer() {
    const Token &t=next();if(t.kind!=Tok::Number)fail("expected a number",t.line);
    if(!(t.number>=0 && t.number<=65535) || t.number!=std::floor(t.number))fail("expected a whole number 0-65535",t.line);
    return uint16_t(t.number);
  }

  void parseFile() {
    for(;;) {
      skipNewlines();
      const Token &t=peek();
      if(t.kind==Tok::End)break;
      if(t.kind!=Tok::Ident)fail("expected a directive or a block");
      const std::string word=next().text;
      if(word=="effect") {
        const Token &s=next();if(s.kind!=Tok::String)fail("expected the name in quotes");
        // What the device shows (LoadableFx::load checks the same): 1-14 printable ASCII, no space at either end.
        if(s.text.empty() || s.text.size()>LoadableFx::MAX_NAME)fail("the name must be 1 to "+std::to_string(LoadableFx::MAX_NAME)+" characters",s.line);
        if(s.text.front()==' ' || s.text.back()==' ')fail("the name cannot start or end with a space",s.line);
        name_=s.text;endStatement();
      }
      else if(word=="uid"){const Token &u=next();if(u.kind!=Tok::Number)fail("expected a uid");uid_=std::strtoull(u.text.c_str(),nullptr,0);endStatement();}
      else if(word=="revision"){revision_=integer();endStatement();}
      else if(word=="fallback") {
        // The built-in that plays when this effect cannot, by its name.
        const Token &s=next();if(s.kind!=Tok::String)fail("expected the fallback effect's name in quotes",s.line);
        int found=-1;
        for(unsigned e=0;e<Appearance::EFFECTS && found<0;++e)if(Appearance::drawn(e) && s.text==Appearance::label(Appearance::Effect(e)))found=int(e);
        if(found<0)fail("unknown fallback effect '"+s.text+"'",s.line);
        fallback_=uint8_t(found);endStatement();
      }
      else if(word=="duration"){duration_=integer();endStatement();}
      else if(word=="readout"){readout_=integer();endStatement();}
      else if(word=="sound" && peek().kind==Tok::Punct && peek().text=="{")soundBlock();
      else if(word=="sound"){sound_=integer();endStatement();}
      else if(word=="const") {
        const Token &n=next();if(n.kind!=Tok::Ident)fail("expected a constant name");
        expect("=");
        NodeP e=expression();
        const Value v=gen(*e);
        if(!v.constant)fail("a const must be known when compiling",n.line);
        constants_[n.text]=v;endStatement();
      }
      else if(word=="sprite") {
        // sprite NAME ("..12.." "..21..") : rows of . 1 2 3, one string each.
        const Token &n=next();if(n.kind!=Tok::Ident)fail("expected a sprite name");
        expect("(");
        SpriteData sp;
        while(peek().kind==Tok::String) {
          const std::string row=next().text;
          if(sp.w && int(row.size())!=sp.w)fail("sprite rows differ in length",n.line);
          sp.w=int(row.size());
          for(char c:row){if(c!='.' && (c<'1' || c>'3'))fail("sprite pixels are . 1 2 3",n.line);sp.px.push_back(uint8_t(c=='.'?0:c-'0'));}
          ++sp.h;
        }
        expect(")");
        if(!sp.w || !sp.h || sp.w>int(MAX_SPRITE_SIDE) || sp.h>int(MAX_SPRITE_SIDE))fail("a sprite is 1-64 pixels each way",n.line);
        if(sprites_.size()>=MAX_SPRITES)fail("too many sprites",n.line);
        constants_[n.text]=number(float(sprites_.size()));sprites_.push_back(sp);endStatement();
      }
      else if(word=="voice")voiceDecl(t.line);
      else if(word=="note" && peek().kind==Tok::Punct && peek().text=="("){NoteCall c=noteCall(t.line);endStatement();playNote(c);}
      else if(word=="notes")notesBlock();
      else if(word=="song")songBlock(t.line);
      else if(word=="frame"){block(1,true);}
      else if(word=="repeat") {
        NodeP e=expression();const Value v=gen(*e);
        if(!v.constant || v.color || !(v.c[0]>=1 && v.c[0]<=MAX_COUNT) || v.c[0]!=float(int(v.c[0])))fail("repeat needs a whole number 1-512");
        block(unsigned(v.c[0]),false);
      }
      else fail("unknown directive '"+word+"'",t.line);
    }
    if(name_.empty())fail("missing effect name",1);
    if(!duration_)fail("missing duration",1);
    if(blocks_.empty())fail("no blocks",1);
  }

  struct Stmt {
    enum Kind {Assign,Exit,Draw,Voice} kind;std::string name;const Signature *sig=nullptr;
    std::vector<NodeP> exprs;int line=0;
    VoiceRef voice;std::vector<std::string> params;  // Voice: which voices, and the parameters exprs set
  };
  Stmt statement() {
    const Token &t=peek();Stmt st;st.line=t.line;
    if(t.kind!=Tok::Ident)fail("expected a statement");
    if(t.text=="exit"){next();st.kind=Stmt::Exit;st.exprs.push_back(expression());endStatement();return st;}
    // voice(param: value, ...), voices[k](...): sets voices' parameters (the sound block only).
    if(voiceNames_.count(t.text) && peek(1).kind==Tok::Punct && (peek(1).text=="(" || peek(1).text=="[")) {
      st.kind=Stmt::Voice;st.voice=voiceRef(true,t.line);
      expect("(");
      if(!punct(")")) {
        do {
          const Token &p=next();
          if(p.kind!=Tok::Ident || !punct(":"))fail("a voice's parameters are named: hum(gain: .5)",p.line);
          if(std::find(st.params.begin(),st.params.end(),p.text)!=st.params.end())fail("'"+p.text+"' given twice",p.line);
          st.params.push_back(p.text);st.exprs.push_back(expression());
        } while(punct(","));
        expect(")");
      }
      endStatement();return st;
    }
    if(peek(1).kind==Tok::Punct && peek(1).text=="(") {
      st.kind=Stmt::Draw;st.sig=signature(t.text);
      if(!st.sig)fail("unknown primitive '"+t.text+"'");
      next();next();
      const size_t want=std::strlen(st.sig->args);
      if(!st.sig->params) {
        for(size_t k=0;k<want;++k){if(k)expect(",");st.exprs.push_back(expression());}
        expect(")");endStatement();return st;
      }
      // Positional arguments, then named ones; the rest take their defaults.
      struct Param {std::string name,def;bool required;};
      std::vector<Param> params;
      {
        const std::string spec=st.sig->params;size_t at=0;
        while(at<spec.size()) {
          size_t end=spec.find(',',at);if(end==std::string::npos)end=spec.size();
          const std::string item=spec.substr(at,end-at);const size_t colon=item.find(':'),eq=item.find('=');
          params.push_back({item.substr(0,colon),eq==std::string::npos?"":item.substr(eq+1),eq==std::string::npos});
          at=end+1;
        }
      }
      st.exprs.resize(params.size());
      bool named=false;size_t position=0;
      if(!punct(")")) {
        do {
          if(peek().kind==Tok::Ident && peek(1).kind==Tok::Punct && peek(1).text==":") {
            const std::string name=next().text;next();named=true;
            size_t k=0;while(k<params.size() && params[k].name!=name)++k;
            if(k==params.size())fail(std::string(st.sig->name)+" has no argument '"+name+"'");
            if(st.exprs[k])fail("argument '"+name+"' given twice");
            st.exprs[k]=expression();
          } else {
            if(named)fail("positional arguments come before named ones");
            if(position>=params.size())fail(std::string("too many arguments to ")+st.sig->name);
            st.exprs[position++]=expression();
          }
        } while(punct(","));
        expect(")");
      }
      for(size_t k=0;k<params.size();++k) {
        if(st.exprs[k])continue;
        if(params[k].required)fail(std::string(st.sig->name)+" needs '"+params[k].name+"'",st.line);
        const std::string &d=params[k].def;
        size_t ref=0;while(ref<k && params[ref].name!=d)++ref;
        if(ref<k)st.exprs[k]=clone(*st.exprs[ref]);
        else if(std::isdigit(static_cast<unsigned char>(d[0])) || d[0]=='-' || d[0]=='.'){st.exprs[k]=make(Node::Number,d,st.line);st.exprs[k]->number=std::strtod(d.c_str(),nullptr);}
        else st.exprs[k]=make(Node::Name,d,st.line);
      }
      endStatement();return st;
    }
    st.kind=Stmt::Assign;st.name=next().text;
    expect("=");
    if(isBuiltin(st.name) || constants_.count(st.name))fail("cannot assign to '"+st.name+"'");
    st.exprs.push_back(expression());endStatement();return st;
  }

  static void split(NodeP e,std::vector<NodeP> &out) {
    if(e->kind==Node::Binary && e->text=="||"){split(std::move(e->kids[0]),out);split(std::move(e->kids[1]),out);}
    else out.push_back(std::move(e));
  }
  template<class F> static void reads(const Node &e,F &&f){if(e.kind==Node::Name)f(e.text);for(const auto &k:e.kids)reads(*k,f);}
  // A canonical text of an expression, for sharing its value.
  static std::string key(const Node &e) {
    if(e.kind==Node::Number)return e.text;
    if(e.kind==Node::Name)return e.text;
    std::string k="("+e.text;for(const auto &c:e.kids)k+=" "+key(*c);return k+")";
  }
  // Whether an expression reads i or a value that changes from run to run.
  static bool varying(const Node &e,const std::map<std::string,bool> &vary) {
    if(e.kind==Node::Name){if(e.text=="i")return true;auto v=vary.find(e.text);return v!=vary.end() && v->second;}
    for(const auto &k:e.kids)if(varying(*k,vary))return true;
    return false;
  }

  // A repeat block's statements that compute the same value in every run
  // (no i, no per-run variable, assigned once) are hoisted into a run-once
  // block before it, and exits on such values ahead of the first draw
  // become the block's guard: the engine then skips the whole block.
  void block(unsigned count,bool frame) {
    expect("{");
    std::vector<Stmt> body;
    for(;;) {
      skipNewlines();
      if(punct("}"))break;
      if(peek().kind==Tok::End)fail("missing '}'");
      Stmt st=statement();
      if(st.kind==Stmt::Voice)fail("only the sound block sets voices",st.line);
      // `exit a || b` is `exit a` then `exit b`, so an invariant part can become the guard.
      if(st.kind==Stmt::Exit) {
        std::vector<NodeP> parts;split(std::move(st.exprs[0]),parts);
        for(auto &part:parts){Stmt e;e.kind=Stmt::Exit;e.line=st.line;e.exprs.push_back(std::move(part));body.push_back(std::move(e));}
      } else body.push_back(std::move(st));
    }
    inFrame_=frame;locals_.clear();localTop_=globalTop_;sp_=localTop_;
    std::vector<bool> hoist(body.size(),false),guard(body.size(),false);
    std::map<std::string,bool> finalVary;
    if(!frame) {
      std::map<std::string,int> assigned;
      for(const auto &st:body)if(st.kind==Stmt::Assign)++assigned[st.name];
      std::map<std::string,bool> vary;  // per local: varies between runs
      std::map<std::string,bool> seen;
      for(const auto &st:body)if(st.kind==Stmt::Assign)vary[st.name]=true;  // until shown otherwise
      // A local read before its assignment holds the previous run's value: never hoisted.
      std::map<std::string,bool> early;
      {
        std::map<std::string,bool> done;
        for(const auto &st:body) {
          for(const auto &e:st.exprs)reads(*e,[&](const std::string &n){if(assigned.count(n) && !done.count(n))early[n]=true;});
          if(st.kind==Stmt::Assign)done[st.name]=true;
        }
      }
      bool drawn=false;
      for(size_t k=0;k<body.size();++k) {
        const Stmt &st=body[k];
        if(st.kind==Stmt::Draw){drawn=true;continue;}
        // A local read before its assignment holds the previous run's value.
        std::map<std::string,bool> now=vary;
        for(auto &v:now)if(!seen.count(v.first))v.second=true;
        const bool v=varying(*st.exprs[0],now);
        if(st.kind==Stmt::Exit){guard[k]=!v && !drawn;continue;}
        if(globals_.count(st.name))continue;
        seen[st.name]=true;
        if(!v && assigned[st.name]==1 && !early.count(st.name)){vary[st.name]=false;hoist[k]=true;}
      }
      finalVary=vary;
    }
    int guardReg=-1;
    std::vector<uint8_t> pro,body_;
    hoisted_.clear();hoistTop_=FIRST_CONST;
    declare(body);
    out_=&pro;weight_=1;
    for(size_t k=0;k<body.size();++k)if(hoist[k]){cse_.clear();genStatement(body[k]);sp_=localTop_;}
    for(size_t k=0;k<body.size();++k)if(guard[k]) {
      cse_.clear();
      const Value c=gen(*body[k].exprs[0]);
      if(c.color)fail("exit needs a number",body[k].line);
      if(guardReg<0){guardReg=localTop_++;sp_=localTop_;move(guardReg,c);}
      else{Value g;g.reg=guardReg;const Value m=op2(MAX,g,c,guardReg);if(m.constant || m.reg!=guardReg)move(guardReg,m);}
      sp_=localTop_;
    }
    out_=&body_;
    if(!frame){vary_=&finalVary;prologue_=&pro;}
    weight_=count;
    for(size_t k=0;k<body.size();++k)if(!hoist[k] && !guard[k]){cse_.clear();genStatement(body[k]);sp_=localTop_;}
    vary_=nullptr;prologue_=nullptr;out_=&code_;cse_.clear();weight_=1;
    if(body_.empty())body_={NOP,0,0,0};
    if(!pro.empty()) {
      blocks_.push_back({uint16_t(code_.size()/4),uint16_t(pro.size()/4),1,0});
      code_.insert(code_.end(),pro.begin(),pro.end());
    }
    blocks_.push_back({uint16_t(code_.size()/4),uint16_t(body_.size()/4),uint16_t(count),uint16_t(guardReg>=0?0x8000|guardReg:0)});
    code_.insert(code_.end(),body_.begin(),body_.end());
    if(code_.size()/4>MAX_CODE)fail("program too long");
    if(frame)globalTop_=localTop_;
    inFrame_=false;
  }

  // ---- the sound ----
  // Level 2's voices name some parameters for what they do: a pulse's duty,
  // FM's ratio and index (the octave and cubic slots) and a drum's rate (hz).
  static int voiceParam(const std::string &n,bool &bend) {
    static const std::pair<const char *,int> names[]={{"gain",GAIN},{"hz",HZ},{"bend",HZ},{"octave",OCTAVE},{"cubic",CUBIC},{"k",K1},{"k2",K2},
      {"trigger",TRIGGER},{"attack",ATTACK},{"decay",DECAY},{"length",LENGTH},{"duty",CUBIC},{"ratio",OCTAVE},{"index",CUBIC},{"rate",HZ}};
    bend=n=="bend";
    for(const auto &p:names)if(n==p.first)return p.second;
    return -1;
  }
  // A parameter a voice's kind does not have.
  void checkParam(const VoiceDecl &v,int param,bool bend,const std::string &name,int line) {
    const char *kind=v.type==TONE_VOICE?"tone":v.type==NOISE_VOICE?"noise":v.type==PULSE_VOICE?"pulse":v.type==WAVETABLE_VOICE?"wave":v.type==FM_VOICE?"fm":"drum";
    auto refuse=[&]{fail(std::string("a ")+kind+" voice has no "+name,line);};
    const bool special=name=="duty" || name=="ratio" || name=="index" || name=="rate";
    if(v.type==TONE_VOICE && special)refuse();
    if(v.type==NOISE_VOICE) {
      if(special || param==OCTAVE || param==CUBIC)refuse();
      if(param==HZ && (v.wave==NOISE_WHITE || bend))fail(v.wave==NOISE_WHITE?"a noise voice has no "+name+" (stepped noise has: noise(step: 4000))":"a noise voice has no bend",line);
    }
    if(v.type==PULSE_VOICE && (name=="octave" || name=="cubic" || name=="ratio" || name=="index" || name=="rate"))refuse();
    if(v.type==WAVETABLE_VOICE && (param==OCTAVE || param==CUBIC || name=="rate"))refuse();
    if(v.type==FM_VOICE && (name=="octave" || name=="cubic" || name=="duty" || name=="rate"))refuse();
    if(v.type==DRUM_VOICE && (param==OCTAVE || param==CUBIC || name=="hz" || bend))refuse();
    VoiceDecl &w=const_cast<VoiceDecl &>(v);(bend?w.bendNamed:w.hzNamed)|=param==HZ && v.type!=DRUM_VOICE && v.type!=NOISE_VOICE;
  }
  float constantNumber(const Node &e,const char *what) {
    const Value v=gen(e);
    if(!v.constant || v.color)fail(std::string(what)+" must be a number known when compiling",e.line);
    return v.c[0];
  }
  // voice NAME = tone(...) | noise(...), or voice NAME[N] = ... for N alike.
  void voiceDecl(int line) {
    const Token &n=next();if(n.kind!=Tok::Ident)fail("expected the voice's name",line);
    const std::string name=n.text;
    if(voiceNames_.count(name) || constants_.count(name) || isBuiltin(name))fail("'"+name+"' is already a name",line);
    int count=1;bool array=false;
    if(punct("[")) {
      NodeP e=expression();const float c=constantNumber(*e,"a voice array's size");expect("]");
      if(!(c>=1 && c<=MAX_VOICES) || c!=float(int(c)))fail("a voice array holds 1-"+std::to_string(MAX_VOICES)+" voices",line);
      count=int(c);array=true;
    }
    expect("=");
    const Token &kind=next();
    static const std::pair<const char *,uint8_t> kinds[]={{"tone",TONE_VOICE},{"noise",NOISE_VOICE},{"pulse",PULSE_VOICE},{"wave",WAVETABLE_VOICE},{"fm",FM_VOICE},{"drum",DRUM_VOICE}};
    VoiceDecl v;v.name=name;v.line=line;v.type=0;
    for(const auto &k:kinds)if(kind.kind==Tok::Ident && kind.text==k.first)v.type=k.second;
    if(!v.type)fail("a voice is tone(...), noise(...), pulse(...), wave(...), fm(...) or drum(...)",line);
    if(v.type>=PULSE_VOICE)level2_=true;
    if(v.type==NOISE_VOICE)v.seed=0x9e3779b9u;
    expect("(");
    // drum(kick, ...): the bank's drum by name first.
    if(v.type==DRUM_VOICE) {
      const Token &d=next();int found=-1;
      for(unsigned k=0;k<Drums::COUNT;++k)if(d.kind==Tok::Ident && d.text==Drums::BANK[k].name)found=int(k);
      if(found<0) {
        std::string names;for(unsigned k=0;k<Drums::COUNT;++k)names+=std::string(k?", ":"")+Drums::BANK[k].name;
        fail("drum(...) names a drum first: "+names,d.line);
      }
      v.datum=uint32_t(found);
    }
    bool points=false;
    const bool closed=punct(")");
    if(!closed && v.type==DRUM_VOICE)expect(",");
    if(!closed) {
      do {
        const Token &p=next();
        if(p.kind!=Tok::Ident || !punct(":"))fail("a voice's settings are named: tone(hz: 440, gain: .5)",p.line);
        const std::string param=p.text;
        if(param=="wave" || param=="filter") {
          const Token &w=next();
          static const char *waves[]={"sine","square","triangle","saw"},*filters[]={"none","low","band","high"};
          const char *const *list=param=="wave"?waves:filters;int found=-1;
          for(int k=0;k<4;++k)if(w.text==list[k])found=k;
          if(w.kind!=Tok::Ident || found<0)fail(param=="wave"?"wave is sine, square, triangle or saw":"filter is none, low, band or high",w.line);
          if(param=="wave"){if(v.type!=TONE_VOICE)fail("only a tone voice has a wave",w.line);v.wave=uint8_t(found);}
          else v.filter=uint8_t(found);
          continue;
        }
        if(param=="points") {
          // wave(points: [a, b, ...]): one cycle, read round with interpolation.
          if(v.type!=WAVETABLE_VOICE)fail("only a wave voice has points",p.line);
          expect("[");std::vector<float> values;
          do{NodeP e=expression();const float c=constantNumber(*e,"a wave's point");if(!(c>=-1 && c<=1))fail("a wave's points are -1 to 1",p.line);values.push_back(c);}while(punct(","));
          expect("]");
          if(values.size()<2 || values.size()>256)fail("a wave has 2-256 points",p.line);
          size_t id=0;while(id<tables_.size() && tables_[id]!=values)++id;
          if(id==tables_.size()){if(tables_.size()>=MAX_TABLES)fail("too many tables",p.line);tables_.push_back(values);}
          v.datum=uint32_t(id);points=true;continue;
        }
        if(param=="step") {
          // noise(step: 4000): a new value 4000 times a second, held between.
          if(v.type!=NOISE_VOICE)fail("only a noise voice steps",p.line);
          NodeP e=expression();const float c=constantNumber(*e,"a noise's step rate");
          if(!(c>0 && c<8000))fail("a noise's step is 0-7999 Hz",p.line);
          if(v.given[HZ])fail("'step' given twice",p.line);
          v.wave=NOISE_STEPPED;v.given[HZ]=true;v.value[HZ]=c;level2_=true;continue;
        }
        NodeP e=expression();
        if(param=="seed") {
          // Written out, read exactly (a float would round a seed above 2^24).
          if(v.type!=NOISE_VOICE)fail("only a noise voice has a seed",p.line);
          const std::string &text=e->text;
          const bool hex=text.size()>2 && text[0]=='0' && (text[1]=='x' || text[1]=='X');
          const bool whole=e->kind==Node::Number && (hex?text.find_first_not_of("0123456789abcdefABCDEF",2)==std::string::npos:text.find_first_not_of("0123456789")==std::string::npos);
          const unsigned long long s=whole && text.size()<=18?std::strtoull(text.c_str(),nullptr,0):0;
          if(!s || s>0xffffffffull)fail("a seed is a whole number 1-4294967295 written out, such as 0x27d4eb2f",p.line);
          v.seed=uint32_t(s);
          continue;
        }
        bool bend=false;const int k=voiceParam(param,bend);
        if(k<0)fail("a voice has no setting '"+param+"'",p.line);
        checkParam(v,k,bend,param,p.line);
        if(v.given[k])fail("'"+param+"' given twice",p.line);
        v.given[k]=true;v.value[k]=constantNumber(*e,"a voice's setting");
      } while(punct(","));
      expect(")");
    }
    endStatement();
    if(v.type==WAVETABLE_VOICE && !points)fail("a wave voice needs its points: wave(points: [0, 1, 0, -1])",line);
    if(voices_.size()+size_t(count)>MAX_VOICES)fail("at most "+std::to_string(MAX_VOICES)+" voices",line);
    voiceNames_[name]={int(voices_.size()),count,array};
    for(int k=0;k<count;++k){VoiceDecl one=v;if(array)one.name=name+"["+std::to_string(k)+"]";voices_.push_back(one);}
  }
  // A voice, or a voice of an array (name[k], k known when compiling), or a
  // whole array where `whole` is allowed.
  VoiceRef voiceRef(bool whole,int line) {
    const Token &n=next();
    auto found=voiceNames_.find(n.text);
    if(n.kind!=Tok::Ident || found==voiceNames_.end())fail("unknown voice '"+n.text+"'",line);
    VoiceRef r=found->second;
    if(punct("[")) {
      if(!r.array)fail("'"+n.text+"' is not an array of voices",line);
      NodeP e=expression();const float k=constantNumber(*e,"a voice's index");expect("]");
      if(!(k>=0 && k<r.count) || k!=float(int(k)))fail("'"+n.text+"' has voices 0-"+std::to_string(r.count-1),line);
      r.first+=int(k);r.count=1;r.array=false;
    } else if(r.array && !whole)fail("which voice of '"+n.text+"'? name[k]",line);
    return r;
  }
  // note(voice, at: s, level: l, hz: f): positional in that order, then named.
  // The voice is resolved when the note is played: its index may use a notes block's i.
  struct NoteCall {std::string voice;NodeP index,at,level,hz;int line=0;};
  NoteCall noteCall(int line) {
    expect("(");
    NoteCall c;c.line=line;
    const Token &n=next();
    if(n.kind!=Tok::Ident || !voiceNames_.count(n.text))fail("unknown voice '"+n.text+"'",line);
    c.voice=n.text;
    if(punct("[")){c.index=expression();expect("]");}
    static const char *order[]={"at","level","hz"};NodeP *slot[]={&c.at,&c.level,&c.hz};
    size_t position=0;bool named=false;
    while(punct(",")) {
      if(peek().kind==Tok::Ident && peek(1).kind==Tok::Punct && peek(1).text==":") {
        const std::string name=next().text;next();named=true;
        size_t k=0;while(k<3 && name!=order[k])++k;
        if(k==3)fail("note has no argument '"+name+"'");
        if(*slot[k])fail("argument '"+name+"' given twice");
        *slot[k]=expression();
      } else {
        if(named)fail("positional arguments come before named ones");
        if(position>=3)fail("too many arguments to note");
        *slot[position++]=expression();
      }
    }
    expect(")");
    if(!c.at)fail("note needs 'at'",line);
    return c;
  }
  // Works a note out (every argument known when compiling) and keeps it.
  void playNote(const NoteCall &c) {
    cse_.clear();
    const float at=constantNumber(*c.at,"a note's time");
    const float level=c.level?constantNumber(*c.level,"a note's level"):1.0f,hz=c.hz?constantNumber(*c.hz,"a note's frequency"):0.0f;
    if(!(at>=0 && at<=60))fail("a note's time is 0-60 s",c.line);
    if(!(level>=0 && level<=8))fail("a note's level is 0-8",c.line);
    if(!(hz>=0 && hz<8000))fail("a note's frequency is 0-7999 Hz (0 keeps the voice's)",c.line);
    VoiceRef ref=voiceNames_.at(c.voice);
    if(c.index) {
      if(!ref.array)fail("'"+c.voice+"' is not an array of voices",c.line);
      const float k=constantNumber(*c.index,"a voice's index");
      if(!(k>=0 && k<ref.count) || k!=float(int(k)))fail("'"+c.voice+"' has voices 0-"+std::to_string(ref.count-1),c.line);
      ref.first+=int(k);ref.count=1;ref.array=false;
    }
    if(hz>0)for(int k=0;k<ref.count;++k){VoiceDecl &v=voices_[size_t(ref.first+k)];if(v.type==NOISE_VOICE)fail("a noise voice has no frequency",c.line);}
    // The first sample at or after `at` on the synth's clock, as built-in sounds start.
    uint32_t sample=uint32_t(double(at)*SOUND_RATE);
    while(sample>0 && Sound::timeOf(sample-1)>=at)--sample;
    while(Sound::timeOf(sample)<at)++sample;
    if(notes_.size()>=MAX_SOUND_EVENTS)fail("at most "+std::to_string(MAX_SOUND_EVENTS)+" notes",c.line);
    Note n;n.sample=sample;n.first=ref.first;n.count=ref.count;n.allocate=ref.array;n.level=level;n.hz=hz;n.line=c.line;n.order=notes_.size();
    notes_.push_back(n);
  }
  // notes N { name = value ... note(...) }: run N times when compiling, i = 0..N-1.
  void notesBlock() {
    NodeP e=expression();const float count=constantNumber(*e,"notes' count");
    if(!(count>=1 && count<=MAX_SOUND_EVENTS) || count!=float(int(count)))fail("notes needs a whole number 1-"+std::to_string(MAX_SOUND_EVENTS));
    expect("{");
    struct Item {std::string name;NodeP value;NoteCall note;bool isNote=false,isExit=false;int line=0;};
    std::vector<Item> items;
    for(;;) {
      skipNewlines();
      if(punct("}"))break;
      if(peek().kind==Tok::End)fail("missing '}'");
      Item it;it.line=peek().line;
      if(peek().kind==Tok::Ident && peek().text=="note" && peek(1).kind==Tok::Punct && peek(1).text=="("){next();it.note=noteCall(it.line);it.isNote=true;}
      else if(peek().kind==Tok::Ident && peek().text=="exit"){next();it.value=expression();it.isExit=true;}
      else {
        const Token &n=next();if(n.kind!=Tok::Ident)fail("expected a name or note(...)");
        if(n.text=="i" || n.text=="n" || isBuiltin(n.text) || constants_.count(n.text))fail("cannot assign to '"+n.text+"'");
        expect("=");it.name=n.text;it.value=expression();
      }
      endStatement();items.push_back(std::move(it));
    }
    for(int i=0;i<int(count);++i) {
      score_.clear();score_["i"]=number(float(i));score_["n"]=number(count);
      for(const auto &it:items) {
        if(it.isNote){playNote(it.note);continue;}
        if(it.isExit){cse_.clear();if(constantNumber(*it.value,"an exit in notes")!=0)break;continue;}
        cse_.clear();score_[it.name]=number(constantNumber(*it.value,"a value in notes"));
      }
    }
    score_.clear();
  }
  // song(tempo: 140, rows: 4) { pattern NAME { CHANNEL: "row row ..." ... } ... order NAME NAME ... }
  // Each channel string holds one token a row: "." nothing, a note (c4,
  // f#3, bb5), "off", a voice's name (play it; a drum or noise voice needs
  // no note), voice:note (switch and play), vN (volume 1-16 for what
  // follows), joined by + in one row (v12+e5). A channel named after a
  // voice starts on it; any other starts on the first voice it names.
  struct SongChannel {std::string name;int voice=-1;};
  void songBlock(int line) {
    if(!song_.empty())fail("a file has one song",line);
    if(voices_.empty())fail("declare a song's voices first: voice lead = pulse(duty: .25)",line);
    level2_=true;
    float tempo=120,rows=4;
    expect("(");
    if(!punct(")")) {
      do {
        const Token &p=next();if(p.kind!=Tok::Ident || !punct(":"))fail("song's settings are named: song(tempo: 140, rows: 4)",p.line);
        NodeP e=expression();const float c=constantNumber(*e,"a song's setting");
        if(p.text=="tempo")tempo=c;else if(p.text=="rows")rows=c;else fail("song has no setting '"+p.text+"'",p.line);
      } while(punct(","));
      expect(")");
    }
    if(!(rows>=1 && rows<=16) || rows!=float(int(rows)))fail("a song's rows a beat are 1-16",line);
    const double rowSamples=double(SOUND_RATE)*60/(double(tempo)*rows);
    if(!(tempo>0) || !(rowSamples>=MIN_ROW_SAMPLES && rowSamples<=MAX_ROW_SAMPLES))fail("this tempo and rows make a row "+std::to_string(int(rowSamples))+" samples; 400-16000 fit",line);
    expect("{");
    std::vector<SongChannel> channels;std::map<std::string,int> patternIds;
    struct Pattern {int rows=0;std::vector<std::vector<uint8_t>> streams;};
    std::vector<Pattern> patterns;std::vector<int> order;
    auto channelOf=[&](const std::string &n){
      for(size_t c=0;c<channels.size();++c)if(channels[c].name==n)return int(c);
      if(channels.size()>=MAX_CHANNELS)fail("a song has at most "+std::to_string(MAX_CHANNELS)+" channels",line);
      SongChannel ch;ch.name=n;auto f=voiceNames_.find(n);
      if(f!=voiceNames_.end()){if(f->second.array)fail("a song plays single voices, not the array '"+n+"'",line);ch.voice=f->second.first;}
      channels.push_back(ch);return int(channels.size()-1);
    };
    auto voiceIndex=[&](const std::string &n,int l){
      auto f=voiceNames_.find(n);
      if(f==voiceNames_.end())return -1;
      if(f->second.array)fail("a song plays single voices, not the array '"+n+"'",l);
      return f->second.first;
    };
    for(;;) {
      skipNewlines();
      if(punct("}"))break;
      const Token &w=next();
      if(w.kind!=Tok::Ident)fail("a song holds patterns and an order",w.line);
      if(w.text=="pattern") {
        const Token &n=next();if(n.kind!=Tok::Ident)fail("expected the pattern's name",n.line);
        if(patternIds.count(n.text))fail("pattern '"+n.text+"' twice",n.line);
        if(patterns.size()>=MAX_PATTERNS)fail("a song has at most "+std::to_string(MAX_PATTERNS)+" patterns",n.line);
        expect("{");
        Pattern pt;std::vector<std::pair<int,std::vector<std::vector<std::string>>>> lines;
        for(;;) {
          skipNewlines();
          if(punct("}"))break;
          const Token &c=next();if(c.kind!=Tok::Ident || !punct(":"))fail("a pattern's lines are channel: \"row row ...\"",c.line);
          const Token &s=next();if(s.kind!=Tok::String)fail("a channel's rows are in quotes",s.line);
          const int ch=channelOf(c.text);
          for(const auto &l:lines)if(l.first==ch)fail("channel '"+c.text+"' twice in pattern '"+n.text+"'",c.line);
          std::vector<std::vector<std::string>> rowTokens;std::string word;
          auto flush=[&]{if(word.empty())return;std::vector<std::string> parts;size_t at=0;
            while(at<=word.size()){const size_t plus=word.find('+',at);parts.push_back(word.substr(at,plus==std::string::npos?std::string::npos:plus-at));if(plus==std::string::npos)break;at=plus+1;}
            rowTokens.push_back(parts);word.clear();};
          for(char ch2:s.text){if(ch2==' ' || ch2=='\t' || ch2=='|')flush();else word+=ch2;}
          flush();
          if(rowTokens.empty())fail("channel '"+c.text+"' has no rows",c.line);
          if(rowTokens.size()>255)fail("a pattern has at most 255 rows",c.line);
          pt.rows=std::max(pt.rows,int(rowTokens.size()));
          lines.push_back({ch,rowTokens});
          endStatement();
          // The channel's first voice: its own name, else the first voice it names.
          if(channels[size_t(ch)].voice<0)
            for(const auto &row:rowTokens)for(const auto &part:row) {
              const std::string v=part.substr(0,part.find(':'));const int k=voiceIndex(v,c.line);
              if(k>=0 && channels[size_t(ch)].voice<0)channels[size_t(ch)].voice=k;
            }
        }
        if(!pt.rows)fail("pattern '"+n.text+"' is empty",n.line);
        // Encode each channel: a row's events, then waits over the rows after.
        pt.streams.assign(MAX_CHANNELS,{});
        for(const auto &l:lines) {
          std::vector<uint8_t> &out=pt.streams[size_t(l.first)];unsigned pending=0;
          SongChannel &ch=channels[size_t(l.first)];
          if(ch.voice<0)fail("channel '"+ch.name+"' names no voice to play",n.line);
          for(const auto &row:l.second) {
            std::vector<uint8_t> events;
            for(const std::string &part:row) {
              if(part==".")continue;
              if(part=="off"){events.push_back(SONG_OFF);continue;}
              // vN is a volume unless a voice of that name was declared (a declared name wins).
              if(part.size()>=2 && part[0]=='v' && std::isdigit(uint8_t(part[1])) && voiceIndex(part.substr(0,part.find(':')),n.line)<0) {
                const int q=std::atoi(part.c_str()+1);if(q<1 || q>16 || part.find_first_not_of("0123456789",1)!=std::string::npos)fail("a volume is v1-v16",n.line);
                events.push_back(uint8_t(SONG_VOLUME|(q-1)));continue;
              }
              const size_t colon=part.find(':');const std::string head=part.substr(0,colon);
              const int vk=voiceIndex(head,n.line);
              int noteNumber=-1;
              if(vk>=0) {
                events.push_back(uint8_t(SONG_VOICE|vk));
                VoiceDecl &vd=voices_[size_t(vk)];
                if(colon!=std::string::npos)noteNumber=noteOf(part.substr(colon+1),n.line);
                else if(vd.type==DRUM_VOICE || vd.type==NOISE_VOICE)noteNumber=60;  // unpitched: the name plays it
              } else noteNumber=noteOf(part,n.line);
              if(noteNumber>=0)events.push_back(uint8_t(noteNumber));
            }
            if(!events.empty()) {
              while(pending){const unsigned w2=std::min(pending,63u);out.push_back(uint8_t(SONG_WAIT|w2));pending-=w2;}
              out.insert(out.end(),events.begin(),events.end());
            }
            ++pending;
          }
        }
        patternIds[n.text]=int(patterns.size());patterns.push_back(pt);
      } else if(w.text=="order") {
        while(peek().kind==Tok::Ident) {
          const Token &n=next();auto f=patternIds.find(n.text);
          if(f==patternIds.end())fail("unknown pattern '"+n.text+"' (patterns come before the order)",n.line);
          order.push_back(f->second);
        }
        endStatement();
      } else fail("a song holds patterns and an order, not '"+w.text+"'",w.line);
    }
    if(patterns.empty())fail("a song needs a pattern",line);
    if(order.empty())fail("a song needs its order: order verse verse chorus",line);
    if(order.size()>MAX_ORDER)fail("a song's order has at most "+std::to_string(MAX_ORDER)+" entries",line);
    double total=0;for(int k:order)total+=double(patterns[size_t(k)].rows)*std::round(rowSamples);
    // As long as a celebration at most (0.20.1); 0.20.0 allowed 120 s.
    if(total>double(SONG_PLAY_MS)*SOUND_RATE/1000) {
      char seconds[16];std::snprintf(seconds,sizeof(seconds),"%.1f",total/SOUND_RATE);
      fail("a song plays at most "+std::to_string(SONG_PLAY_MS/1000)+" s, as long as a celebration; this one runs "+seconds+" s",line);
    }
    // The voices a song plays are triggered by its notes, and pitched if they can be.
    for(const SongChannel &ch:channels)(void)ch;
    for(const Pattern &pt:patterns)for(size_t c=0;c<channels.size();++c) {
      int voice=channels[c].voice;
      for(uint8_t e:pt.streams[c]) {
        if((e&0xF0)==SONG_VOICE)voice=e&15;
        else if(e<SONG_WAIT){VoiceDecl &vd=voices_[size_t(voice)];vd.played=true;if(vd.type!=DRUM_VOICE && vd.type!=NOISE_VOICE)vd.pitched=true;}
      }
    }
    song_.assign(SONG_HEAD,0);
    song_[0]=SONG_VERSION;song_[1]=uint8_t(channels.size());song_[2]=uint8_t(patterns.size());
    put16(song_.data()+4,uint16_t(std::lround(rowSamples)));put16(song_.data()+6,uint16_t(order.size()));
    for(const SongChannel &ch:channels)song_.push_back(uint8_t(ch.voice));
    for(int k:order)song_.push_back(uint8_t(k));
    for(const Pattern &pt:patterns) {
      song_.push_back(uint8_t(pt.rows));
      for(size_t c=0;c<channels.size();++c) {
        const auto &s=pt.streams[c];uint8_t n2[2];put16(n2,uint16_t(s.size()));
        song_.insert(song_.end(),n2,n2+2);song_.insert(song_.end(),s.begin(),s.end());
      }
    }
    if(song_.size()>8192)fail("a song is at most 8 KB compiled; this one "+std::to_string(song_.size())+" bytes",line);
  }
  // A note's name: c d e f g a b, then # or b, then an octave -1 to 9 (c4 is middle C, 60).
  int noteOf(const std::string &s,int line) {
    static const int PITCH[7]={9,11,0,2,4,5,7};  // a b c d e f g
    if(s.empty() || s[0]<'a' || s[0]>'g')fail("unknown token '"+s+"' (a note, a voice, ., off or v1-v16)",line);
    int n=PITCH[s[0]-'a'];size_t at=1;
    if(at<s.size() && s[at]=='#'){++n;++at;}else if(at<s.size() && s[at]=='b' && at+1<s.size()){--n;++at;}
    const std::string octave=s.substr(at);
    if(octave.empty() || (octave!="-1" && (octave.size()!=1 || !std::isdigit(uint8_t(octave[0])))))fail("a note is a letter, # or b, and an octave -1 to 9: '"+s+"'",line);
    const int midi=(std::atoi(octave.c_str())+1)*12+n;
    if(midi<0 || midi>127)fail("note '"+s+"' is outside 0-127",line);
    return midi;
  }
  // In the sound block: voice(param: value, ...) sets a voice's parameters
  // (an array's: all of them) every step.
  void voiceSet(const Stmt &st) {
    for(size_t k=0;k<st.params.size();++k) {
      bool bend=false;const int param=voiceParam(st.params[k],bend);
      if(param<0)fail("a voice has no parameter '"+st.params[k]+"'",st.line);
      // The parameter's registers first (a new one shared by the voices that
      // have none yet), so the value is computed straight into the first.
      int shared=-1;std::vector<int> regs;
      for(int i=0;i<st.voice.count;++i) {
        VoiceDecl &d=voices_[size_t(st.voice.first+i)];
        checkParam(d,param,bend,st.params[k],st.line);
        if(d.given[param])fail("'"+st.params[k]+"' of '"+d.name+"' is set both where it is declared and here",st.line);
        if(d.reg[param]<0) {
          if(shared<0){shared=localTop_++;sp_=localTop_;if(localTop_>hoistTop_)fail("too many variables",st.line);}
          d.reg[param]=shared;
        }
        if(std::find(regs.begin(),regs.end(),d.reg[param])==regs.end())regs.push_back(d.reg[param]);
      }
      const Value v=gen(*st.exprs[k],regs[0]);
      if(v.color)fail("a voice's '"+st.params[k]+"' is a number",st.line);
      for(int r:regs)if(v.constant || v.reg!=r)move(r,v);
      sp_=localTop_;
    }
  }
  // The sound block runs every 2 ms in its own registers, so it sees the
  // constants but none of the frame's variables; it sets voices.
  void soundBlock() {
    expect("{");
    if(soundBlock_)fail("a second sound block");
    if(voices_.empty())fail("declare the sound's voices before its block");
    soundBlock_=true;
    std::vector<Stmt> body;
    for(;;) {
      skipNewlines();
      if(punct("}"))break;
      if(peek().kind==Tok::End)fail("missing '}'");
      body.push_back(statement());
      if(body.back().kind==Stmt::Draw)fail("a sound block cannot draw",body.back().line);
      if(body.back().kind==Stmt::Assign && body.back().name=="out")fail("a sound block sets voices, not out: voice(gain: ..., hz: ...)",body.back().line);
    }
    const auto savedGlobals=globals_;const int savedTop=globalTop_;
    globals_.clear();locals_.clear();localTop_=FIRST_TEMP;sp_=localTop_;
    hoistTop_=FIRST_CONST;declare(body);
    out_=&soundCode_;weight_=double(MAX_SOUND_MS)*SOUND_RATE/1000/SOUND_STEP;
    for(const auto &st:body){cse_.clear();if(st.kind==Stmt::Voice)voiceSet(st);else genStatement(st);sp_=localTop_;}
    out_=&code_;weight_=1;cse_.clear();
    if(soundCode_.size()/4>MAX_SOUND_CODE)fail("a sound block runs at most "+std::to_string(MAX_SOUND_CODE)+" instructions; this one "+std::to_string(soundCode_.size()/4));
    globals_=savedGlobals;globalTop_=savedTop;locals_.clear();localTop_=globalTop_;sp_=localTop_;
  }
  // A constant a voice reads: always one of the constant registers (the
  // busiest constants of all, by weight).
  int voiceConstant(float v) {
    const std::string k=constKey(&v,1);
    if(planning_){auto &u=plan_.usage[k];u.first.assign(&v,&v+1);u.second+=1e15;return FIRST_CONST;}
    const int at=plan_.index.at(k);
    if(at>=int(MAX_CONSTS))fail("too many different voice settings",0);
    return int(FIRST_CONST)+at;
  }
  // The SOUND section, or empty without voices.
  std::vector<uint8_t> soundSection(uint16_t soundStart) {
    if(voices_.empty())return {};
    if(!sound_ && song_.empty())fail("a sound with voices needs its length: sound 1700 (ms)",voices_[0].line);
    const uint32_t samples=uint32_t(sound_)*SOUND_RATE/1000;
    // Notes in time order (then source order); a note to a whole array goes
    // to its first voice free by then (its last note ended: its length), else
    // to the one that started longest ago.
    std::vector<Note> notes=notes_;
    std::stable_sort(notes.begin(),notes.end(),[](const Note &a,const Note &b){return a.sample<b.sample;});
    std::vector<uint32_t> busy(voices_.size(),0),started(voices_.size(),0);std::vector<bool> used(voices_.size(),false);
    for(Note &n:notes) {
      if(n.sample>=samples)fail("a note after the sound's end ("+std::to_string(sound_)+" ms)",n.line);
      if(n.allocate) {
        int pick=-1;
        for(int k=0;k<n.count && pick<0;++k)if(!used[size_t(n.first+k)] || busy[size_t(n.first+k)]<=n.sample)pick=n.first+k;
        if(pick<0){pick=n.first;for(int k=1;k<n.count;++k)if(started[size_t(n.first+k)]<started[size_t(pick)])pick=n.first+k;}
        n.first=pick;n.count=1;n.allocate=false;
      }
      VoiceDecl &v=voices_[size_t(n.first)];
      const bool fixedLength=v.reg[LENGTH]<0 && v.given[LENGTH] && v.value[LENGTH]>0;
      used[size_t(n.first)]=true;started[size_t(n.first)]=n.sample;
      busy[size_t(n.first)]=fixedLength?n.sample+uint32_t(v.value[LENGTH]*SOUND_RATE+.5f):UINT32_MAX;
      v.played=true;v.pitched=v.pitched || n.hz>0;
    }
    std::vector<uint8_t> body(SOUND_HEAD,0);
    body[0]=SOUND_VERSION;body[1]=uint8_t(voices_.size());body[2]=SOUND_STEP_SHIFT;body[3]=1;
    put16(body.data()+4,soundStart);put16(body.data()+6,uint16_t(soundCode_.size()/4));put16(body.data()+8,uint16_t(notes.size()));
    for(VoiceDecl &v:voices_) {
      const bool triggered=v.played || v.reg[TRIGGER]>=0 || (v.given[TRIGGER] && v.value[TRIGGER]>0);
      if(!triggered && (v.given[ATTACK] || v.given[DECAY] || v.given[LENGTH] || v.reg[ATTACK]>=0 || v.reg[DECAY]>=0 || v.reg[LENGTH]>=0))
        fail("voice '"+v.name+"' has an envelope but no note or trigger plays it",v.line);
      if(v.pitched && v.hzNamed)fail("voice '"+v.name+"' plays notes' frequencies: give it bend (a factor), not hz",v.line);
      if(!v.pitched && v.bendNamed)fail("voice '"+v.name+"' has bend but no note gives it a frequency",v.line);
      uint8_t r[VOICE_BYTES]={};
      r[0]=v.type;r[1]=v.wave;r[2]=v.filter;r[3]=uint8_t((triggered?ENVELOPE:0)|(v.pitched?NOTE_PITCH:0));
      put32(r+4,v.type==NOISE_VOICE?v.seed:v.type==WAVETABLE_VOICE || v.type==DRUM_VOICE?v.datum:0);
      for(unsigned k=0;k<VOICE_PARAMS;++k) {
        if(v.reg[k]>=0){r[8+k]=uint8_t(v.reg[k]);continue;}
        // Defaults: gain 1, a note's frequency as is, a pulse's duty .5, FM's ratio and index 1, a drum's rate 1.
        const float def=k==GAIN?1.0f:(k==HZ && (v.pitched || v.type==DRUM_VOICE))?1.0f:(k==CUBIC && v.type==PULSE_VOICE)?.5f:
                        ((k==OCTAVE || k==CUBIC) && v.type==FM_VOICE)?1.0f:0.0f;
        r[8+k]=uint8_t(voiceConstant(v.given[k]?v.value[k]:def));
      }
      body.insert(body.end(),r,r+VOICE_BYTES);
    }
    for(const Note &n:notes) {
      uint8_t e[EVENT_BYTES]={};uint32_t bits;
      put32(e,n.sample);e[4]=uint8_t(n.first);e[5]=NOTE;
      std::memcpy(&bits,&n.level,4);put32(e+8,bits);std::memcpy(&bits,&n.hz,4);put32(e+12,bits);
      body.insert(body.end(),e,e+EVENT_BYTES);
    }
    return body;
  }

  // Every variable a block assigns gets its registers before any code, so
  // no temporary ever shares a register with one: a variable read before its
  // assignment then holds the previous run's (or sample's) value.
  void declare(const std::vector<Stmt> &body) {
    for(const auto &st:body) {
      if(st.kind!=Stmt::Assign || lookupVar(st.name))continue;
      const bool color=isColor(*st.exprs[0]);
      Value v;v.color=color;v.reg=localTop_;localTop_+=color?3:1;sp_=localTop_;
      if(localTop_>hoistTop_)fail("too many variables",st.line);
      (inFrame_?globals_:locals_)[st.name]=v;
    }
  }

  void genStatement(const Stmt &st) {
    if(st.kind==Stmt::Exit) {
      const Value v=gen(*st.exprs[0]);
      if(v.color)fail("exit needs a number",st.line);
      if(v.constant){if(v.c[0]!=0)emit(EXIT,0,uint8_t(materialize(v)),0);}
      else emit(EXIT,0,uint8_t(v.reg),0);
      return;
    }
    if(st.kind==Stmt::Draw) {
      std::vector<uint8_t> list;
      for(size_t k=0;k<st.exprs.size();++k) {
        const Value v=gen(*st.exprs[k]);
        const bool color=st.sig->args[k]=='C';
        // The k-th argument of the signature, counting a colour as one.
        if(v.color!=color)fail(std::string("argument ")+std::to_string(k+1)+" of "+st.sig->name+" must be a "+(color?"colour":"number"),st.line);
        const int reg=materialize(v);
        for(int c=0;c<(color?3:1);++c)list.push_back(uint8_t(reg+c));
      }
      emitDraw(st.sig->prim,list);
      return;
    }
    const Node &e=*st.exprs[0];
    // A new variable gets its registers before the expression's temps.
    Value *var=lookupVar(st.name);
    if(!var) {
      const bool color=isColor(e);
      Value v;v.color=color;v.reg=localTop_;localTop_+=color?3:1;sp_=localTop_;
      if(localTop_>hoistTop_)fail("too many variables",st.line);
      (inFrame_?globals_:locals_)[st.name]=v;var=lookupVar(st.name);
    }
    const Value target=*var;
    const Value v=gen(e,target.reg);
    if(v.color!=target.color)fail("'"+st.name+"' changes between number and colour",st.line);
    if(v.constant || v.reg!=target.reg)move(target.reg,v);
  }

  Value *lookupVar(const std::string &n){auto l=locals_.find(n);if(l!=locals_.end())return &l->second;auto g=globals_.find(n);return g!=globals_.end()?&g->second:nullptr;}
  static bool isBuiltin(const std::string &n){return builtinReg(n)>=0 || n=="accent" || n=="white";}
  static int builtinReg(const std::string &n) {
    static const std::pair<const char *,int> names[]={{"t",T},{"dur",DUR},{"W",W},{"H",H},{"ax0",AX0},{"ay0",AY0},{"ax1",AX1},{"ay1",AY1},
      {"acx",ACX},{"acy",ACY},{"i",I},{"n",N},{"bottom",BOTTOM},{"frame",FRAME}};
    for(const auto &p:names)if(n==p.first)return p.second;
    return -1;
  }
  // Whether an expression is a colour, without generating it.
  bool isColor(const Node &e) {
    switch(e.kind) {
      case Node::Number:return false;
      case Node::Name:{
        if(e.text=="accent" || e.text=="white")return true;
        auto c=constants_.find(e.text);if(c!=constants_.end())return c->second.color;
        Value *v=lookupVar(e.text);return v && v->color;
      }
      case Node::Ternary:return isColor(*e.kids[1]);
      case Node::Call:return e.text=="rgb" || e.text=="color" || (e.text=="mix" && e.kids.size()==3 && isColor(*e.kids[0])) || (e.text=="table" && e.kids.size()>=2 && isColor(*e.kids[1]));
      default:return false;
    }
  }

  // ---- parsing ----
  static NodeP clone(const Node &n){NodeP c(new Node{n.kind,n.text,n.number,n.line,{}});for(const auto &k:n.kids)c->kids.push_back(clone(*k));return c;}
  NodeP make(Node::Kind k,const std::string &text,int line){NodeP n(new Node{k,text,0,line,{}});return n;}
  NodeP expression() {
    NodeP c=orExpr();
    if(punct("?")) {
      NodeP a=expression();expect(":");NodeP b=expression();
      NodeP n=make(Node::Ternary,"?",c->line);n->kids.push_back(std::move(c));n->kids.push_back(std::move(a));n->kids.push_back(std::move(b));return n;
    }
    return c;
  }
  NodeP binaryChain(NodeP (Compiler::*sub)(),std::initializer_list<const char *> ops) {
    NodeP l=(this->*sub)();
    for(;;) {
      bool found=false;
      for(const char *o:ops)if(peek().kind==Tok::Punct && peek().text==o) {
        const int line=next().line;NodeP r=(this->*sub)();
        NodeP n=make(Node::Binary,o,line);n->kids.push_back(std::move(l));n->kids.push_back(std::move(r));l=std::move(n);found=true;break;
      }
      if(!found)return l;
    }
  }
  NodeP orExpr(){return binaryChain(&Compiler::andExpr,{"||"});}
  NodeP andExpr(){return binaryChain(&Compiler::compare,{"&&"});}
  NodeP compare(){return binaryChain(&Compiler::sum,{"<","<=",">",">=","==","!="});}
  NodeP sum(){return binaryChain(&Compiler::product,{"+","-"});}
  NodeP product(){return binaryChain(&Compiler::unary,{"*","/","%"});}
  NodeP unary() {
    if(peek().kind==Tok::Punct && (peek().text=="-" || peek().text=="!")) {
      const Token &t=next();NodeP n=make(Node::Unary,t.text,t.line);n->kids.push_back(unary());return n;
    }
    return primary();
  }
  NodeP primary() {
    const Token &t=next();
    if(t.kind==Tok::Number){NodeP n=make(Node::Number,t.text,t.line);n->number=t.number;return n;}
    if(t.kind==Tok::Punct && t.text=="("){NodeP e=expression();expect(")");return e;}
    if(t.kind==Tok::Ident) {
      if(punct("(")) {
        NodeP n=make(Node::Call,t.text,t.line);
        if(!punct(")")){do n->kids.push_back(expression());while(punct(","));expect(")");}
        return n;
      }
      return make(Node::Name,t.text,t.line);
    }
    fail("expected a value",t.line);
  }

  // ---- code generation ----
  void emit(uint8_t op,int d,int a,int b) {
    if((code_.size()+out_->size())/4>=MAX_CODE)fail("program too long");
    out_->push_back(op);out_->push_back(uint8_t(d));out_->push_back(uint8_t(a));out_->push_back(uint8_t(b));
  }
  void emitDraw(uint8_t prim,const std::vector<uint8_t> &list) {
    // Identical argument lists are shared.
    size_t offset=args_.size();
    for(size_t o=0;o+list.size()<=args_.size();++o)if(std::equal(list.begin(),list.end(),args_.begin()+long(o))){offset=o;break;}
    if(offset==args_.size())args_.insert(args_.end(),list.begin(),list.end());
    if(args_.size()>MAX_ARGS)fail("too many primitive arguments");
    emit(DRAW,prim,int(offset&255),int(offset>>8));
  }
  // A register holding an expression's intermediate value (not a variable,
  // built-in, constant or hoisted value).
  bool isTemp(int r)const{return r>=localTop_ && r<hoistTop_;}
  void forget(int r){for(auto i=cse_.begin();i!=cse_.end();)if(i->second.reg==r || (i->second.color && r>=i->second.reg && r<i->second.reg+3))i=cse_.erase(i);else ++i;}
  int temp(int n) {
    const int r=sp_;sp_+=n;
    if(sp_>hoistTop_)fail("expression too complex (out of registers)");
    return r;
  }
  int constReg(float v){return constant(&v,1);}
  int constColor(const float *c){return constant(c,3);}
  int constant(const float *v,int n) {
    const std::string k=constKey(v,n);
    if(planning_) {
      auto &u=plan_.usage[k];u.first.assign(v,v+n);u.second+=weight_;
      return FIRST_CONST;
    }
    const int at=plan_.index.at(k);
    if(at+n<=int(MAX_CONSTS))return int(FIRST_CONST)+at;
    // Outside the registers: loaded once a frame in a repeat block's
    // prologue, or where it is used.
    auto load=[&](int dst){for(int c=0;c<n;++c)emit(LDK,dst+c,(at+c)&255,(at+c)>>8);};
    if(vary_) {
      const std::string hk="#"+k;
      auto h=hoisted_.find(hk);if(h!=hoisted_.end())return h->second.reg;
      hoistTop_-=n;if(hoistTop_<sp_ || hoistTop_<localTop_)fail("expression too complex (out of registers)");
      std::vector<uint8_t> *const saved=out_;out_=prologue_;load(hoistTop_);out_=saved;
      Value r;r.reg=hoistTop_;r.color=n==3;hoisted_[hk]=r;return hoistTop_;
    }
    const int dst=temp(n);load(dst);return dst;
  }
  int materialize(const Value &v){if(!v.constant)return v.reg;return v.color?constColor(v.c):constReg(v.c[0]);}
  void move(int dst,const Value &v){const int src=materialize(v);for(int c=0;c<(v.color?3:1);++c)emit(MOV,dst+c,src+c,0);}
  static Value number(float v){Value r;r.constant=true;r.c[0]=v;return r;}
  // The destination: the hint when it is safe to write early, else a temp.
  int dest(int hint,int n,std::initializer_list<int> reads) {
    if(hint>=0){bool clash=false;for(int r:reads)for(int c=0;c<n;++c)for(int k=0;k<3;++k)if(r>=0 && r+k==hint+c)clash=true;if(!clash)return hint;}
    return temp(n);
  }
  Value op2(uint8_t op,const Value &a,const Value &b,int hint) {
    if(a.color || b.color)fail("this operator needs numbers");
    if(a.constant && b.constant)return number(Math::apply(op,a.c[0],b.c[0],0));
    const int ra=materialize(a),rb=materialize(b),d=hint>=0?hint:temp(1);
    emit(op,d,ra,rb);Value r;r.reg=d;return r;
  }
  Value op1(uint8_t op,const Value &a,int hint) {
    if(a.color)fail("this function needs a number");
    if(a.constant)return number(Math::apply(op,a.c[0],0,0));
    const int d=hint>=0?hint:temp(1);emit(op,d,a.reg,0);Value r;r.reg=d;return r;
  }
  // d = op(a,b) with d first loaded with `k`: MIX, SEL, NOISE.
  Value op3(uint8_t op,const Value &k,const Value &a,const Value &b,int hint) {
    if(k.constant && a.constant && b.constant)return number(Math::apply(op,a.c[0],b.c[0],k.c[0]));
    const int ra=materialize(a),rb=materialize(b),rk=materialize(k);
    // A condition or weight just computed into a temporary is overwritten
    // in place: no copy.
    if(hint<0 && !k.constant && isTemp(rk) && rk!=ra && rk!=rb) {
      forget(rk);emit(op,rk,ra,rb);Value r;r.reg=rk;return r;
    }
    const int d=dest(hint,1,{ra,rb});
    if(rk!=d)emit(MOV,d,rk,0);
    emit(op,d,ra,rb);Value r;r.reg=d;return r;
  }

  // Shares values: within a statement, a repeated subexpression is
  // computed once; in a repeat block, one that is the same in every run is
  // computed once a frame in the block's prologue.
  Value gen(const Node &e,int hint=-1) {
    if(e.kind==Node::Number || e.kind==Node::Name)return generate(e,hint);
    const std::string k=key(e);
    if(vary_ && !varying(e,*vary_)) {
      auto h=hoisted_.find(k);if(h!=hoisted_.end())return h->second;
      std::vector<uint8_t> *const savedOut=out_;const int savedSp=sp_;const auto *savedVary=vary_;
      const int n=isColor(e)?3:1;hoistTop_-=n;
      if(hoistTop_<sp_ || hoistTop_<localTop_)fail("expression too complex (out of registers)");
      const double savedWeight=weight_;
      out_=prologue_;vary_=nullptr;sp_=localTop_;weight_=1;
      Value v=generate(e,hoistTop_);
      weight_=savedWeight;
      if(v.constant)hoistTop_+=n;
      else if(v.reg!=hoistTop_){move(hoistTop_,v);v.reg=hoistTop_;}
      out_=savedOut;sp_=savedSp;vary_=savedVary;
      hoisted_[k]=v;return v;
    }
    auto c=cse_.find(k);if(c!=cse_.end())return c->second;
    const Value v=generate(e,hint);
    if(!v.constant && v.reg!=hint)cse_[k]=v;
    return v;
  }
  Value generate(const Node &e,int hint) {
    switch(e.kind) {
      case Node::Number:if(!(std::fabs(e.number)<=3.4e38))fail("number out of range",e.line);return number(float(e.number));
      case Node::Name: {
        if(auto s=score_.find(e.text);s!=score_.end())return s->second;
        const int b=builtinReg(e.text);
        if(b>=0){Value v;v.reg=b;return v;}
        if(e.text=="accent" || e.text=="white"){Value v;v.color=true;v.reg=e.text=="accent"?ACCENT_R:WHITE_R;
          if(e.text=="white"){v.constant=true;v.c[0]=31;v.c[1]=63;v.c[2]=31;v.reg=-1;}return v;}
        auto c=constants_.find(e.text);if(c!=constants_.end())return c->second;
        if(Value *v=lookupVar(e.text))return *v;
        fail("unknown name '"+e.text+"'",e.line);
      }
      case Node::Unary: {
        const Value a=gen(*e.kids[0]);
        if(e.text=="-")return op1(NEG,a,hint);
        return op2(EQ,a,number(0),hint);
      }
      case Node::Binary: {
        const Value a=gen(*e.kids[0]),b=gen(*e.kids[1]);
        const std::string &o=e.text;
        if(o=="+")return op2(ADD,a,b,hint);
        if(o=="-")return op2(SUB,a,b,hint);
        if(o=="*")return op2(MUL,a,b,hint);
        if(o=="/")return op2(DIV,a,b,hint);
        if(o=="%")return op2(MOD,a,b,hint);
        if(o=="<")return op2(LT,a,b,hint);
        if(o==">")return op2(LT,b,a,hint);
        if(o=="<=")return op2(LE,a,b,hint);
        if(o==">=")return op2(LE,b,a,hint);
        if(o=="==")return op2(EQ,a,b,hint);
        if(o=="!="){const Value eq=op2(EQ,a,b,-1);return op2(SUB,number(1),eq,hint);}
        if(o=="&&")return op2(MUL,a,b,hint);
        if(o=="||")return op2(MAX,a,b,hint);
        fail("unknown operator "+o,e.line);
      }
      case Node::Ternary: {
        const Value c=gen(*e.kids[0]);
        if(c.color)fail("a condition must be a number",e.line);
        if(c.constant)return gen(*e.kids[c.c[0]!=0?1:2],hint);
        const Value a=gen(*e.kids[1]),b=gen(*e.kids[2]);
        if(a.color!=b.color)fail("both sides of ?: must be numbers or both colours",e.line);
        if(!a.color)return op3(SEL,c,a,b,hint);
        const int ra=materialize(a),rb=materialize(b),d=dest(hint,3,{ra,rb,c.reg});
        if(d!=c.reg)emit(MOV,d,c.reg,0);
        emit(CSEL,d,ra,rb);
        Value r;r.color=true;r.reg=d;return r;
      }
      case Node::Call:return call(e,hint);
    }
    fail("bad expression",e.line);
  }

  Value call(const Node &e,int hint) {
    const std::string &f=e.text;const size_t n=e.kids.size();
    auto arity=[&](size_t want){if(n!=want)fail(f+" takes "+std::to_string(want)+" arguments",e.line);};
    static const std::pair<const char *,uint8_t> unary[]={{"sin",SIN},{"cos",COS},{"sqrt",SQRT},{"exp",EXP},{"abs",ABS},{"floor",FLOOR},
      {"fract",FRACT},{"clamp01",CLAMP01},{"smooth",SMOOTH},{"easeout",EASEOUT}};
    for(const auto &u:unary)if(f==u.first){arity(1);return op1(u.second,gen(*e.kids[0]),hint);}
    if(f=="round"){arity(1);const Value a=gen(*e.kids[0]);const Value h=op2(ADD,a,number(.5f),-1);return op1(FLOOR,h,hint);}
    if(f=="min" || f=="max") {
      if(n<2)fail(f+" takes two or more arguments",e.line);
      Value v=gen(*e.kids[0]);
      for(size_t k=1;k<n;++k)v=op2(f=="min"?MIN:MAX,v,gen(*e.kids[k]),k+1==n?hint:-1);
      return v;
    }
    if(f=="rnd"){arity(2);return op2(RND,gen(*e.kids[0]),gen(*e.kids[1]),hint);}
    if(f=="noise"){arity(3);const Value x=gen(*e.kids[0]),y=gen(*e.kids[1]),s=gen(*e.kids[2]);return op3(NOISE,s,x,y,hint);}
    if(f=="rgb") {
      arity(3);Value v;v.color=v.constant=true;
      for(int k=0;k<3;++k){const Value c=gen(*e.kids[size_t(k)]);if(!c.constant || c.color || !(c.c[0]>=0 && c.c[0]<=255))fail("rgb takes three constant numbers 0-255",e.line);
        const int ch=int(c.c[0]);v.c[k]=float(ch*(k==1?63:31))/255.0f;}
      return v;
    }
    if(f=="color") {
      arity(3);Value ch[3];bool all=true;
      for(int k=0;k<3;++k){ch[k]=gen(*e.kids[size_t(k)]);if(ch[k].color)fail("color takes three numbers",e.line);all=all && ch[k].constant;}
      if(all){Value v;v.color=v.constant=true;for(int k=0;k<3;++k)v.c[k]=ch[k].c[0];return v;}
      int rs[3];for(int k=0;k<3;++k)rs[k]=materialize(ch[k]);
      const int d=dest(hint,3,{rs[0],rs[1],rs[2]});
      for(int k=0;k<3;++k)emit(MOV,d+k,rs[k],0);
      Value v;v.color=true;v.reg=d;return v;
    }
    if(f=="table") {
      // table(i, v0, v1, ...): entry i (a whole number, clamped) of constant
      // numbers, or of constant colours (a table per channel).
      if(n<2)fail("table takes an index and one or more numbers",e.line);
      const Value index=gen(*e.kids[0]);if(index.color)fail("a table index is a number",e.line);
      std::vector<Value> entries;
      for(size_t k=1;k<n;++k){const Value v=gen(*e.kids[k]);if(!v.constant)fail("table entries are constant numbers or colours",e.line);entries.push_back(v);}
      const bool colors=entries[0].color;
      for(const Value &v:entries)if(v.color!=colors)fail("a table's entries are all numbers or all colours",e.line);
      if(index.constant)return entries[size_t(Math::toInt(index.c[0],0,int(entries.size())-1))];
      auto lookup=[&](int channel,int d) {
        std::vector<float> values;for(const Value &v:entries)values.push_back(v.c[channel]);
        size_t id=0;while(id<tables_.size() && tables_[id]!=values)++id;
        if(id==tables_.size()){if(tables_.size()>=MAX_TABLES)fail("too many tables",e.line);tables_.push_back(values);}
        emit(LDX,d,materialize(index),int(id));
      };
      if(!colors){const int d=hint>=0?hint:temp(1);lookup(0,d);Value r;r.reg=d;return r;}
      const int ri=materialize(index),d=dest(hint,3,{ri});(void)ri;
      for(int c=0;c<3;++c)lookup(c,d+c);
      Value r;r.color=true;r.reg=d;return r;
    }
    if(f=="red" || f=="green" || f=="blue") {
      arity(1);const Value c=gen(*e.kids[0]);
      if(!c.color)fail(f+" takes a colour",e.line);
      const int k=f=="red"?0:f=="green"?1:2;
      if(c.constant)return number(c.c[k]);
      Value v;v.reg=c.reg+k;return v;
    }
    if(f=="mix") {
      arity(3);
      const Value a=gen(*e.kids[0]),b=gen(*e.kids[1]),k=gen(*e.kids[2]);
      if(k.color)fail("mix's third argument is a number",e.line);
      if(a.color!=b.color)fail("mix takes two numbers or two colours",e.line);
      if(!a.color)return op3(MIX,k,a,b,hint);
      if(a.constant && b.constant && k.constant) {
        Value v;v.color=v.constant=true;
        for(int c=0;c<3;++c)v.c[c]=Math::apply(MIX,a.c[c],b.c[c],k.c[0]);
        return v;
      }
      const int ra=materialize(a),rb=materialize(b),rk=materialize(k),d=dest(hint,3,{ra,rb});
      if(rk!=d)emit(MOV,d,rk,0);
      emit(CMIX,d,ra,rb);
      Value v;v.color=true;v.reg=d;return v;
    }
    fail("unknown function '"+f+"'",e.line);
  }

  static constexpr uint8_t PAD=10;  // an optional section the engine skips
  std::vector<uint8_t> assemble() {
    const size_t soundStart=code_.size()/4;
    code_.insert(code_.end(),soundCode_.begin(),soundCode_.end());
    if(code_.size()/4>MAX_CODE)fail("program too long",0);
    std::vector<uint8_t> f(HEADER_BYTES,0);
    put32(f.data(),MAGIC);f[4]=FORMAT;f[5]=level2_?2:1;
    put32(f.data()+8,uint32_t(uid_));put32(f.data()+12,uint32_t(uid_>>32));
    put16(f.data()+16,revision_);put16(f.data()+18,duration_);put16(f.data()+20,readout_);put16(f.data()+22,sound_);
    auto section=[&](uint8_t type,const uint8_t *body,size_t length,uint8_t flags=0) {
      if(length>0xffff)fail("section too large",0);
      f.push_back(type);f.push_back(flags);f.push_back(uint8_t(length));f.push_back(uint8_t(length>>8));
      f.insert(f.end(),body,body+length);
    };
    section(NAME,reinterpret_cast<const uint8_t *>(name_.data()),name_.size());
    if(fallback_!=Program::NO_FALLBACK){const uint8_t meta[2]={1,fallback_};section(META,meta,sizeof(meta));}
    // Tables follow the planned constants, each contiguous.
    std::vector<float> pool=plan_.pool;std::vector<uint8_t> tableBody{uint8_t(tables_.size())};
    for(const auto &tb:tables_) {
      uint8_t e[4];put16(e,uint16_t(pool.size()));put16(e+2,uint16_t(tb.size()));tableBody.insert(tableBody.end(),e,e+4);
      pool.insert(pool.end(),tb.begin(),tb.end());
    }
    if(pool.size()>MAX_POOL)fail("too many constants",0);
    std::vector<uint8_t> c(pool.size()*4);
    for(size_t i=0;i<pool.size();++i){uint32_t bits;std::memcpy(&bits,&pool[i],4);put32(c.data()+4*i,bits);}
    if(!c.empty())section(CONSTS,c.data(),c.size());
    // The code body starts on a 4-byte boundary (an optional pad section before it).
    if((f.size()+4)%4)section(PAD,std::vector<uint8_t>(4-(f.size()+4)%4,0).data(),4-(f.size()+4)%4);
    section(CODE,code_.data(),code_.size());
    if(!args_.empty())section(ARGS,args_.data(),args_.size());
    std::vector<uint8_t> b(blocks_.size()*8);
    for(size_t i=0;i<blocks_.size();++i){put16(b.data()+8*i,blocks_[i].start);put16(b.data()+8*i+2,blocks_[i].length);put16(b.data()+8*i+4,blocks_[i].count);put16(b.data()+8*i+6,blocks_[i].flags);}
    section(BLOCKS,b.data(),b.size());
    if(!tables_.empty())section(TABLES,tableBody.data(),tableBody.size());
    if(!sprites_.empty()) {
      std::vector<uint8_t> body{uint8_t(sprites_.size())};
      for(const auto &sp:sprites_) {
        body.push_back(uint8_t(sp.w));body.push_back(uint8_t(sp.h));
        std::vector<uint8_t> packed((sp.px.size()+3)/4,0);
        for(size_t i=0;i<sp.px.size();++i)packed[i/4]|=uint8_t(sp.px[i]<<((i%4)*2));
        body.insert(body.end(),packed.begin(),packed.end());
      }
      section(SPRITES,body.data(),body.size());
    }
    {
      const auto body=soundSection(uint16_t(soundCode_.empty()?0:soundStart));
      if(!body.empty())section(SOUND,body.data(),body.size());
    }
    if(!song_.empty())section(SONG,song_.data(),song_.size(),1);  // required: a 0.19 engine must not ignore it
    put32(f.data()+24,uint32_t(f.size()));
    const uint32_t crc=crc32(f.data(),f.size());  // the CRC field is still zero
    put32(f.data()+28,crc);
    return f;
  }
};
}

inline Result compile(const std::string &source) {
  Result r;int line=0;
  auto tokens=detail::lex(source,r.error,line);
  if(tokens.empty()){r.line=line;return r;}
  detail::Plan plan;
  r=detail::Compiler(tokens,plan,true).run();
  if(!r.ok)return r;
  {
    // The busiest constants first, into the registers; the rest after.
    std::vector<std::pair<std::string,std::pair<std::vector<float>,double>>> items(plan.usage.begin(),plan.usage.end());
    std::stable_sort(items.begin(),items.end(),[](const auto &a,const auto &b){return a.second.second>b.second.second;});
    std::vector<bool> placed(items.size(),false);
    for(size_t k=0;k<items.size();++k) {
      const auto &v=items[k].second.first;
      if(plan.pool.size()+v.size()>LoadableFx::MAX_CONSTS)continue;
      plan.index[items[k].first]=int(plan.pool.size());plan.pool.insert(plan.pool.end(),v.begin(),v.end());placed[k]=true;
    }
    bool overflow=false;for(bool p:placed)overflow=overflow || !p;
    if(overflow)plan.pool.resize(LoadableFx::MAX_CONSTS,0.0f);
    for(size_t k=0;k<items.size();++k)if(!placed[k]) {
      const auto &v=items[k].second.first;
      plan.index[items[k].first]=int(plan.pool.size());plan.pool.insert(plan.pool.end(),v.begin(),v.end());
    }
    if(plan.pool.size()>LoadableFx::MAX_POOL){r.ok=false;r.error="too many constants";return r;}
  }
  r=detail::Compiler(std::move(tokens),plan,false).run();
  if(r.ok) {
    Program p;const Error e=load(r.bytes.data(),r.bytes.size(),p);
    if(e!=Error::None){r.ok=false;r.error=std::string("compiled file refused: ")+label(e);}
  }
  return r;
}
}
