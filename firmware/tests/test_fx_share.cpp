// Share codes (tools/fx/fx_share.h and fx_codes.h, docs/FIRMWARE_LOADABLE_EFFECTS.md,
// "Sharing"): every effect file packs and unpacks to source that compiles
// to the same bytes, and goes to a code and back however it is pasted;
// version 1's words never change; base64url and the code's framing
// round-trip; damaged, truncated, oversized and hostile codes are refused
// or, when they unpack, only ever reach the compiler, never crash it.
#include "../tools/fx/fx_codes.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

static std::string readFile(const std::string &path) {
  std::ifstream in(path);std::stringstream text;text<<in.rdbuf();
  assert(in.good() || !text.str().empty());
  return text.str();
}

int main() {
  // Every effect in the repository: the packed source unpacks to source
  // that compiles to exactly the same file, and is much smaller.
  const char *files[]={"orbit","flames","sparkles","shockwave","slash","rupture","worst","opbench",
                       "idiom/orbit","idiom/sparkles","idiom/slash","idiom/rupture","idiom/tidal","idiom/crimson","idiom/bladework",
                       "idiom/overdrive","idiom/summon","idiom/boundless017","idiom/sunblade","idiom/waterleaf"};
  size_t sourceBytes=0,packedBytes=0;
  for(const char *name:files) {
    const std::string source=readFile(std::string("firmware/tools/fx/effects/")+name+".fx");
    const auto compiled=FxCompiler::compile(source);assert(compiled.ok);
    const auto packed=FxShare::pack(source);
    if(!packed.ok)std::cerr<<name<<": "<<packed.error<<"\n";
    assert(packed.ok);
    const auto unpacked=FxShare::unpack(packed.bytes);
    if(!unpacked.ok)std::cerr<<name<<": "<<unpacked.error<<"\n";
    assert(unpacked.ok);
    const auto again=FxCompiler::compile(unpacked.source);
    if(!again.ok || again.bytes!=compiled.bytes)std::cerr<<name<<": unpacked source compiles differently ("<<again.error<<" line "<<again.line<<")\n"<<unpacked.source;
    assert(again.ok && again.bytes==compiled.bytes);
    // Packing the unpacked source gives the same bytes: a code is stable however often it is shared.
    assert(FxShare::pack(unpacked.source).bytes==packed.bytes);
    sourceBytes+=source.size();packedBytes+=packed.bytes.size();
  }
  std::printf("20 effects: %zu bytes of source pack to %zu\n",sourceBytes,packedBytes);
  assert(packedBytes*2<sourceBytes);

  // A whole code: made from source, opened from a line, a link or an .llfx
  // file's text, giving the packed bytes back and source that compiles to
  // the same file; a code shorter by a character, or with one changed, is
  // refused or (padding bits) the same effect, never some other effect.
  {
    uint32_t seed=1234567u;auto next=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    unsigned refused=0;
    for(const char *name:files) {
      const std::string source=readFile(std::string("firmware/tools/fx/effects/")+name+".fx");
      const auto made=FxCodes::make(source);assert(made.ok && made.code.rfind(FxShare::PREFIX,0)==0);
      assert(made.packed==FxShare::pack(source).bytes);
      for(const std::string &pasted:{made.code,"try this "+made.code+" !","https://launchlab.example/effects#"+made.code,made.code+"\n"}) {
        const auto opened=FxCodes::open(pasted);assert(opened.ok && opened.packed==made.packed);
        assert(FxCompiler::compile(opened.source).bytes==FxCompiler::compile(source).bytes);
      }
      for(unsigned k=0;k<40;++k) {
        std::string bad=made.code;
        if(k%4==1)bad.pop_back();
        else if(k%4==3)bad.resize(bad.size()-4);  // still base64: a zlib stream that stops short
        else {const size_t at=6+next()%(bad.size()-6);const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
              char c;do c=alphabet[next()%64];while(c==bad[at]);bad[at]=c;}
        const auto opened=FxCodes::open(bad);
        // A change zlib cannot see falls in the padding bits after deflate's
        // last block: it opens as the same effect, never as another.
        if(opened.ok)assert(opened.packed==made.packed);
        else {++refused;assert(opened.error=="damaged code" || opened.error=="code from a newer version");}
      }
    }
    std::printf("codes: %u of 800 damaged codes refused\n",refused);
    assert(refused>=780);
    // Source that does not compile makes no code, and says where.
    const auto bad=FxCodes::make("effect \"A\"\nduration 2000\nnope(1)\n");assert(!bad.ok && bad.line==3 && bad.code.empty());
    // A code that inflates past the largest packed source is refused, not inflated without bound.
    std::string big(FxShare::MAX_PACKED+100,'a');
    uLongf size=compressBound(uLong(big.size()));std::vector<uint8_t> z(size);
    assert(compress2(z.data(),&size,reinterpret_cast<const Bytef *>(big.data()),uLong(big.size()),9)==Z_OK);z.resize(size);
    assert(FxShare::code(z).size()<200 && FxCodes::open(FxShare::code(z)).error=="code too large");
    // A zip bomb: 16 MB of zeros is a code of about 22,000 characters, and
    // opening it inflates into the fixed 32 KB buffer and stops.
    {
      std::vector<uint8_t> zeros(16u<<20,0);uLongf n=compressBound(uLong(zeros.size()));std::vector<uint8_t> bomb(n);
      assert(compress2(bomb.data(),&n,zeros.data(),uLong(zeros.size()),9)==Z_OK);bomb.resize(n);
      const std::string code=FxShare::code(bomb);
      assert(code.size()<30000 && FxCodes::open(code).error=="code too large");
    }
    // Whole codes from strangers: random text after the prefix, random zlib
    // streams, and real codes cut, spliced and mutated are refused or open
    // to source the compiler then judges, never a crash or an unbounded inflate.
    {
      uint32_t seed=362436069u;auto next=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
      const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
      std::vector<std::string> codes;
      for(const char *name:files)codes.push_back(FxCodes::make(readFile(std::string("firmware/tools/fx/effects/")+name+".fx")).code);
      unsigned refused=0,opened=0;
      for(unsigned k=0;k<6000;++k) {
        std::string code;
        switch(k%4) {
          case 0:{code=FxShare::PREFIX;const size_t n=next()%3000;for(size_t i=0;i<n;++i)code+=alphabet[next()%64];break;}
          case 1:{std::string raw;const size_t n=next()%2000;for(size_t i=0;i<n;++i)raw+=char(next()%256);
                  uLongf z=compressBound(uLong(raw.size()));std::vector<uint8_t> out(z);compress2(out.data(),&z,reinterpret_cast<const Bytef *>(raw.data()),uLong(raw.size()),1+next()%9);
                  out.resize(z);code=FxShare::code(out);break;}
          case 2:{const auto &a=codes[next()%codes.size()],&b=codes[next()%codes.size()];code=a.substr(0,6+next()%(a.size()-6))+b.substr(6+next()%(b.size()-6));break;}
          default:{code=codes[next()%codes.size()];for(unsigned e=0,edits=1+next()%6;e<edits;++e)code[6+next()%(code.size()-6)]=alphabet[next()%64];}
        }
        const auto o=FxCodes::open(code);
        if(!o.ok){++refused;continue;}
        ++opened;assert(o.packed.size()<=FxShare::MAX_PACKED && o.source.size()<=FxShare::MAX_SOURCE);
        const auto c=FxCompiler::compile(o.source);
        if(c.ok){LoadableFx::Program p;assert(LoadableFx::load(c.bytes.data(),c.bytes.size(),p)==LoadableFx::Error::None);}
      }
      std::printf("code fuzz: %u refused, %u opened\n",refused,opened);
      assert(refused && opened);
    }
    // A slot's packed source, read back from a device, opens like a code.
    const auto made=FxCodes::make(readFile("firmware/tools/fx/effects/idiom/tidal.fx"));
    const auto back=FxCodes::unpacked(made.packed);assert(back.ok && FxShare::pack(back.source).bytes==made.packed);
    assert(FxCodes::unpacked(std::string("\xFF")).error=="damaged code");
  }

  // The unpacked source reads like hand-written source.
  {
    const auto u=FxShare::unpack(FxShare::pack("effect \"A\"\nduration 2000\nrepeat 3 {\n  exit !(i > 1) || t < .5\n"
      "  x = -i*2 + acx\n  emit(4, salt: 7, x: x, y: t > 1 ? -3 : ay1, color: mix(accent, white, .5))\n}\n").bytes);
    assert(u.ok);
    const std::string want="effect \"A\"\nduration 2000\nrepeat 3 {\n  exit !(i > 1) || t < .5\n  x = -i*2 + acx\n"
      "  emit(4, salt: 7, x: x, y: t > 1 ? -3 : ay1, color: mix(accent, white, .5))\n}\n";
    if(u.source!=want)std::cerr<<u.source;
    assert(u.source==want);
  }

  // Version 1's words are frozen: these are the first, the last and the count.
  {
    const auto &w=FxShare::words();
    assert(w.size()==229 && w.size()<=254 && w.front()=="effect" && w[45]=="clip" && w[186]=="END" && w[207]=="at");
    // 0.20's music words, appended after "at" in this order.
    const char *music[]={"pulse","fm","drum","duty","ratio","index","rate","step","points","song","pattern","order","tempo","rows",
                         "kick","snare","hat","openhat","clap","tomlow","tomhigh"};
    for(size_t i=0;i<sizeof(music)/sizeof(*music);++i)assert(w[208+i]==music[i]);
    assert(w.back()=="tomhigh");
    for(size_t i=0;i<w.size();++i)for(size_t j=i+1;j<w.size();++j)assert(w[i]!=w[j]);
    // Every primitive the compiler knows has a word: a new one is appended in a new version.
    for(const char *p:{"clip","plot","emit","stroke","trail","stamp","aura","disc","runes","puff","cloud","arc","heatring","vessel","rays",
                       "voice","note","notes","tone","noise","gain","hz","attack","decay"})
      assert(FxShare::detail::wordIndex(p)>=0);
    // A word past the 127th is two bytes.
    const auto two=FxShare::pack("effect \"A\"\nduration 2000\nframe {\n  END = 1\n}\n");
    assert(two.ok && two.bytes.find(char(0xFF))!=std::string::npos);
  }

  // Songs share like any effect: the code unpacks to source that compiles to the identical file.
  {
    for(const char *name:{"victory_lap","night_spin","ripcord"}) {
      std::ifstream in(std::string("firmware/tools/fx/effects/music/")+name+".fx");std::stringstream text;text<<in.rdbuf();
      const auto compiled=FxCompiler::compile(text.str());assert(compiled.ok);
      const auto made=FxCodes::make(text.str());assert(made.ok);const std::string &code=made.code;
      const auto back=FxCodes::open(code);assert(back.ok);
      const auto again=FxCompiler::compile(back.source);assert(again.ok && again.bytes==compiled.bytes);
      std::printf("music %s: code %zu characters\n",name,code.size());
      assert(code.size()<2600);
    }
  }

  // base64url round-trips every length, and refuses what is not base64url.
  {
    for(size_t n=0;n<70;++n) {
      std::vector<uint8_t> bytes(n);for(size_t i=0;i<n;++i)bytes[i]=uint8_t(i*37+n);
      std::vector<uint8_t> back;assert(FxShare::fromBase64url(FxShare::base64url(bytes),back) && back==bytes);
    }
    std::vector<uint8_t> out;
    assert(!FxShare::fromBase64url("ab+c",out) && !FxShare::fromBase64url("abcde",out) && !FxShare::fromBase64url("ab=",out));
    assert(!FxShare::fromBase64url("AB",out));  // unused bits set: not a canonical encoding
  }

  // The code's framing: found in a line or a link, line breaks inside it
  // ignored, a space ending it, anything else refused with the message the web shows.
  {
    const std::vector<uint8_t> z={0x78,0x9c,1,2,3,250,251};
    const std::string code=FxShare::code(z);
    assert(code.rfind("LLFX1:",0)==0);
    for(const std::string &pasted:{code,"look: "+code+" nice","https://launchlab.example/effects#"+code,
                                   code.substr(0,9)+"\r\n"+code.substr(9)}) {
      const auto p=FxShare::payload(pasted);assert(p.ok && p.zlib==z);
    }
    assert(FxShare::payload("hello").error=="not an effect code");
    assert(FxShare::payload("LLFX2:abcd").error=="code from a newer version");
    assert(FxShare::payload("LLFX1:").error=="damaged code");
  }

  // Damaged packed bytes: control characters, a cut two-byte word and a
  // word from a later version are refused; size is bounded.
  {
    assert(FxShare::unpack(std::string("frame\x01")).error=="damaged code");
    assert(FxShare::unpack(std::string("\xFF")).error=="damaged code");
    assert(FxShare::unpack(std::string("\xFF\xFE")).error=="code from a newer version");
    assert(FxShare::unpack(std::string(FxShare::MAX_PACKED+1,'a')).error=="code too large");
  }

  // Fuzzing: random and mutated packed bytes either are refused or unpack
  // to source the compiler handles (refusing it or not), never a crash.
  {
    uint32_t seed=2463534242u;auto next=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    std::vector<std::string> corpus;
    for(const char *name:files)corpus.push_back(FxShare::pack(readFile(std::string("firmware/tools/fx/effects/")+name+".fx")).bytes);
    unsigned refused=0,compiled=0,rejected=0;
    for(unsigned k=0;k<6000;++k) {
      std::string bytes;
      if(k%3==0){const size_t n=next()%600;for(size_t i=0;i<n;++i)bytes+=char(next()%256);}
      else {
        bytes=corpus[next()%corpus.size()];
        const unsigned edits=1+next()%8;
        for(unsigned e=0;e<edits && !bytes.empty();++e) {
          const size_t at=next()%bytes.size();
          switch(next()%3){case 0:bytes[at]=char(next()%256);break;case 1:bytes.erase(at,1+next()%20);break;default:bytes.insert(at,1,char(next()%256));}
        }
      }
      const auto u=FxShare::unpack(bytes);
      if(!u.ok){++refused;continue;}
      const auto c=FxCompiler::compile(u.source);
      if(c.ok){++compiled;LoadableFx::Program p;assert(LoadableFx::load(c.bytes.data(),c.bytes.size(),p)==LoadableFx::Error::None);}
      else ++rejected;
    }
    std::printf("fuzz: %u refused by unpack, %u refused by the compiler, %u compiled and loaded\n",refused,rejected,compiled);
    assert(refused && rejected && compiled);
  }
  // The compiler takes source from strangers on the website: lines dropped,
  // repeated and swapped, and numbers replaced by extremes, compile or are
  // refused, and whatever compiles loads and passes the loader's checks.
  {
    uint32_t seed=88172645u;auto next=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;};
    const char *extremes[]={"0","1e39","-1e39","65536","4294967296","1e-45","0x7fffffff","512","513","-1","3.4e38","1e308",".0000001"};
    unsigned ok=0,refused=0;
    for(unsigned k=0;k<4000;++k) {
      std::vector<std::string> lines;
      {std::stringstream in(readFile(std::string("firmware/tools/fx/effects/")+files[next()%20]+".fx"));std::string l;while(std::getline(in,l))lines.push_back(l);}
      const unsigned edits=1+next()%6;
      for(unsigned e=0;e<edits && !lines.empty();++e) {
        const size_t at=next()%lines.size();
        switch(next()%4) {
          case 0:lines.erase(lines.begin()+long(at));break;
          case 1:lines.insert(lines.begin()+long(next()%lines.size()),lines[at]);break;
          case 2:std::swap(lines[at],lines[next()%lines.size()]);break;
          default: {
            std::string &l=lines[at];size_t d=l.find_first_of("0123456789");
            if(d!=std::string::npos){size_t end=d;while(end<l.size() && (std::isalnum(static_cast<unsigned char>(l[end])) || l[end]=='.'))++end;l.replace(d,end-d,extremes[next()%13]);}
          }
        }
      }
      std::string source;for(const auto &l:lines)source+=l+"\n";
      const auto c=FxCompiler::compile(source);
      if(c.ok){++ok;LoadableFx::Program p;assert(LoadableFx::load(c.bytes.data(),c.bytes.size(),p)==LoadableFx::Error::None);}
      else ++refused;
    }
    std::printf("source fuzz: %u compiled, %u refused\n",ok,refused);
    assert(ok && refused);
  }
  std::puts("test_fx_share: ok");
}
