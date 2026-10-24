// The effect compiler and validator on the command line.
//   fxc compile IN.fx OUT.llfx     compile (refuses what the device would refuse)
//   fxc check IN.fx|IN.llfx...     validate: rules and the frame budget on the host fixture
//   fxc dis IN.fx|IN.llfx          list the blocks and instructions
//   fxc header OUT.h NAME IN.fx... a C++ header of compiled files (the HIL spike's)
//   fxc share IN.fx                the effect's share code (LLFX1:...)
//   fxc unshare CODE|FILE          the source a share code holds (a code, or a file or link containing one)
// Build: c++ -std=c++17 -O2 -ffp-contract=off -I../../LaunchLabMini -I. fxc.cpp -lz -o fxc
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include "fx_compiler.h"
#include "fx_validate.h"
#include "fx_codes.h"

static std::vector<uint8_t> read(const std::string &path) {
  std::ifstream in(path,std::ios::binary);
  if(!in){std::fprintf(stderr,"%s: cannot read\n",path.c_str());std::exit(2);}
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
  if(bytes.size()>=4 && !std::memcmp(bytes.data(),"LLFX",4))return bytes;
  const auto r=FxCompiler::compile(std::string(bytes.begin(),bytes.end()));
  if(!r.ok){std::fprintf(stderr,"%s:%d: %s\n",path.c_str(),r.line,r.error.c_str());std::exit(1);}
  return r.bytes;
}
static void dis(const std::vector<uint8_t> &f) {
  static const char *ops[]={"nop","mov","add","sub","mul","div","min","max","abs","floor","fract","sin","cos","sqrt","exp","clamp01","smooth","easeout",
    "mix","lt","le","eq","sel","rnd","noise","mod","madd","neg","cmix","exit","draw","ldk","ldx","csel"};
  static const char *prims[]={"clip","plot","dot","vdot","spark","twinkle","star","line","fline","segment","ellipse","glow","bolt","rimbolt","fire","prime",
    "glint","shiftrow","shade","cut","heatline","blade","emit","stroke","trail","stamp","aura","disc","runes","puff","cloud","arc","heatring","vessel","rays"};
  static_assert(sizeof(ops)/sizeof(*ops)==LoadableFx::OPS && sizeof(prims)/sizeof(*prims)==LoadableFx::PRIMS,"one name each");
  LoadableFx::Program p;const auto e=LoadableFx::load(f.data(),f.size(),p);
  if(e!=LoadableFx::Error::None){std::printf("refused: %s\n",LoadableFx::label(e));return;}
  auto reg=[&](int r){static char b[4][24];static int k;k=(k+1)&3;
    if(r>=int(LoadableFx::FIRST_CONST))std::snprintf(b[k],24,"%g",p.consts[r-LoadableFx::FIRST_CONST]);else std::snprintf(b[k],24,"r%d",r);return b[k];};
  std::printf("%s: %zu bytes, %u blocks, at most %u instructions and %u draws a frame\n",p.name,f.size(),p.blockCount,p.maxOps,p.maxDraws);
  for(unsigned b=0;b<p.blockCount;++b) {
    const auto &k=p.blocks[b];
    std::printf("block %u: %u x %u%s\n",b,k.count,k.length,k.flags?" (guarded)":"");
    for(unsigned pc=k.start;pc<k.start+k.length;++pc) {
      const uint8_t *in=p.code+4*pc;
      if(in[0]==LoadableFx::DRAW) {
        const unsigned at=in[2]|in[3]<<8;std::printf("  draw %s",prims[in[1]]);
        for(unsigned j=0;j<LoadableFx::PRIM_ARGS[in[1]];++j)std::printf(" %s",reg(p.args[at+j]));
        std::printf("\n");
      } else if(in[0]==LoadableFx::LDK)std::printf("  ldk r%d %g\n",in[1],p.constant(in[2]|in[3]<<8));
      else std::printf("  %s r%d %s %s\n",ops[in[0]],in[1],reg(in[2]),reg(in[3]));
    }
  }
  if(!p.hasSound)return;
  // The sound: its voices with the registers they read, the notes, and the control block.
  static const char *types[]={"","tone","noise"},*waves[]={"sine","square","triangle","saw"},*filters[]={"","low","band","high"},
    *params[]={"gain","hz","octave","cubic","k","k2","trigger","attack","decay","length"};
  std::printf("sound: %u ms, %u voices, %u notes, a control block of %u instructions a step\n",p.soundMs,p.soundVoices,p.soundEvents,p.soundLength);
  for(unsigned v=0;v<p.soundVoices;++v) {
    const uint8_t *r=p.soundVoice+size_t(v)*LoadableFx::VOICE_BYTES;
    std::printf("voice %u: %s",v,types[r[0]]);
    if(r[0]==LoadableFx::TONE_VOICE)std::printf(" %s",waves[r[1]]);else std::printf(" seed 0x%08x",LoadableFx::get32(r+4));
    if(r[2])std::printf(" %s-pass",filters[r[2]]);
    if(r[3]&LoadableFx::ENVELOPE)std::printf(" envelope");
    if(r[3]&LoadableFx::NOTE_PITCH)std::printf(" note-pitch");
    for(unsigned k=0;k<LoadableFx::VOICE_PARAMS;++k)std::printf(" %s=%s",params[k],reg(r[8+k]));
    std::printf("\n");
  }
  for(unsigned e=0;e<p.soundEvents;++e) {
    const uint8_t *r=p.soundEvent+size_t(e)*LoadableFx::EVENT_BYTES;float level,hz;const uint32_t lb=LoadableFx::get32(r+8),hb=LoadableFx::get32(r+12);
    std::memcpy(&level,&lb,4);std::memcpy(&hz,&hb,4);
    std::printf("note at sample %u: voice %u level %g hz %g\n",LoadableFx::get32(r),r[4],level,hz);
  }
  for(unsigned pc=p.soundStart;pc<p.soundStart+p.soundLength;++pc) {
    const uint8_t *in=p.code+4*pc;
    if(in[0]==LoadableFx::LDK)std::printf("  ldk r%d %g\n",in[1],p.constant(in[2]|in[3]<<8));
    else std::printf("  %s r%d %s %s\n",ops[in[0]],in[1],reg(in[2]),reg(in[3]));
  }
}
int main(int argc,char **argv) {
  const std::string cmd=argc>1?argv[1]:"";
  if(cmd=="compile" && argc==4) {
    const auto f=read(argv[2]);std::ofstream(argv[3],std::ios::binary).write(reinterpret_cast<const char *>(f.data()),long(f.size()));
    std::printf("%s: %zu bytes\n",argv[3],f.size());return 0;
  }
  if(cmd=="check" && argc>=3) {
    int failed=0;
    for(int i=2;i<argc;++i) {
      const auto r=FxValidate::check(read(argv[i]),{FxValidate::fixture(),FxValidate::fixture(0x0841)});
      std::printf("%s: %s, worst frame %.0f us modelled at %u ms, %u instructions at most (bound %u), %u frames\n",argv[i],
                  r.ok?"ok":r.problem.c_str(),r.worstUs,r.worstMs,r.worstOps,r.maxOps,r.frames);
      if(r.songLoad)std::printf("  song: %s of core 0 at its busiest, at %u ms (budget %s)\n",FxValidate::percent(r.songLoad).c_str(),r.songWorstMs,
                                FxValidate::percent(LoadableFx::SONG_BUDGET).c_str());
      failed+=!r.ok;
    }
    return failed?1:0;
  }
  if(cmd=="dis" && argc==3){dis(read(argv[2]));return 0;}
  if(cmd=="header" && argc>=5) {
    std::ofstream out(argv[2]);const std::string name=argv[3];
    out<<"#pragma once\n#include <cstddef>\n#include <cstdint>\n// Generated by firmware/tools/fx/fxc header; do not edit. Compiled LLFX files.\n";
    out<<"namespace "<<name<<" {\nstruct File {const char *name;const uint8_t *bytes;size_t size;};\n";
    for(int i=4;i<argc;++i) {
      const auto f=read(argv[i]);
      out<<"alignas(4) inline const uint8_t FILE"<<i-4<<"[]={";
      for(size_t k=0;k<f.size();++k)out<<(k%24?"":"\n  ")<<unsigned(f[k])<<",";
      out<<"};\n";
    }
    out<<"inline const File FILES[]={";
    // Named by the path below effects/ (idiom/orbit), else the file's base name.
    for(int i=4;i<argc;++i){std::string base=argv[i];const size_t at=base.find("effects/");
      base=at!=std::string::npos?base.substr(at+8):base.substr(base.find_last_of('/')+1);base=base.substr(0,base.find('.'));
      out<<"{\""<<base<<"\",FILE"<<i-4<<",sizeof(FILE"<<i-4<<")},";}
    out<<"};\nconstexpr unsigned COUNT="<<argc-4<<";\n}\n";
    return 0;
  }
  if(cmd=="share" && argc==3) {
    std::ifstream in(argv[2]);std::stringstream text;text<<in.rdbuf();
    if(!in && text.str().empty()){std::fprintf(stderr,"%s: cannot read\n",argv[2]);return 2;}
    const auto made=FxCodes::make(text.str());
    if(!made.ok){std::fprintf(stderr,"%s:%d: %s\n",argv[2],made.line,made.error.c_str());return 1;}
    std::printf("%s\n",made.code.c_str());
    return 0;
  }
  if(cmd=="unshare" && argc==3) {
    std::string pasted=argv[2];
    if(pasted.find(FxShare::PREFIX)==std::string::npos){std::ifstream in(argv[2]);std::stringstream text;text<<in.rdbuf();pasted=text.str();}
    const auto opened=FxCodes::open(pasted);
    if(!opened.ok){std::fprintf(stderr,"%s\n",opened.error.c_str());return 1;}
    std::fputs(opened.source.c_str(),stdout);
    return 0;
  }
  std::fprintf(stderr,"usage: fxc compile IN OUT | check IN... | dis IN | header OUT.h NAME IN... | share IN | unshare CODE\n");return 2;
}
