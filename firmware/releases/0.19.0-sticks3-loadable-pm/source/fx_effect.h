#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
// HIL builds can time each primitive (the spike's bench, LLFX_PROFILE); the
// engine is one class for the whole build, so it is decided here.
#if defined(LAUNCHLAB_HIL) && LAUNCHLAB_HIL && !defined(LLFX_PROFILE)
#define LLFX_PROFILE
#endif
#if defined(LLFX_PROFILE)
#if defined(ESP_PLATFORM)
#include "esp_timer.h"
#else
#include <chrono>
#endif
#endif
#include "best_effect_ui.h"
#include "best_effect_sound.h"  // the synth's sine table, which the sound's tones read

// Loaded celebration effects (docs/FIRMWARE_LOADABLE_EFFECTS.md): an effect
// is data, an "LLFX" file, played by this engine with the built-in effects'
// own primitives (BestEffectUI::Renderer), so a loaded effect keeps their
// rules: light lands only on background pixels outside the UI halo, the
// readout is touched only by the readout primitives and only early, and
// every frame is a pure function of elapsed time with fixed seeds.
//
// A file holds blocks of straight-line bytecode. A block runs `count` times
// a frame (once per particle, ring, row...), each run computing numbers
// with a register machine and handing them to native primitives (DRAW),
// which do the per-pixel work. There are no jumps backwards and no loops,
// so a frame runs at most sum(count * length) instructions: the bound is
// read off the file. load() refuses anything malformed before a frame is
// drawn; draw() clamps every argument a primitive receives, so even a
// valid file with wild numbers cannot index outside the canvas, divide by
// zero or convert an out-of-range float. A Guard stops a frame between two
// draw calls when its time is up, so a slow file degrades, never stalls.
// Engine arithmetic is never contracted into fused multiply-adds, so a frame
// is the same on the device, the host and the browser's WebAssembly build.
#if defined(__clang__) || !defined(__GNUC__)
#define LLFX_EXACT
#else
#define LLFX_EXACT __attribute__((optimize("fp-contract=off")))
#endif
// The interpreter's loop runs from internal RAM on the device, and its
// tables (dispatch labels, sine, smoothstep) live there too: a table read
// from flash cost about 5.6 us on the device when the cache missed
// (Boundless, 2026-10-21).
#if defined(ARDUINO)
#define LLFX_HOT IRAM_ATTR
#define LLFX_RAM DRAM_ATTR
#else
#define LLFX_HOT
#define LLFX_RAM
#endif
namespace LoadableFx {
constexpr uint32_t MAGIC=0x58464c4c;  // "LLFX"
constexpr uint8_t FORMAT=1;           // container layout
constexpr uint8_t ENGINE=1;           // opcodes and primitives this engine plays
constexpr size_t HEADER_BYTES=32,MAX_FILE=16384;
// Constants: the first MAX_CONSTS of the pool are registers FIRST_CONST..255,
// the rest are read with LDK.
constexpr unsigned REGS=256,FIRST_TEMP=32,FIRST_CONST=160,MAX_CONSTS=REGS-FIRST_CONST,MAX_POOL=1024;
constexpr unsigned MAX_CODE=4096,MAX_ARGS=4096,MAX_BLOCKS=32,MAX_COUNT=512,MAX_NAME=14;
constexpr uint32_t MIN_DURATION=1000,MAX_DURATION=6000,MAX_READOUT_MS=2200,MAX_SOUND_MS=2800;
// Sound (SOUND section, version 2; docs/FIRMWARE_LOADABLE_EFFECTS.md,
// "Sound"): mono 16-bit at SOUND_RATE (as BestEffectSound), rendered by
// native voices whose parameters a control block of at most MAX_SOUND_CODE
// instructions sets once a step of SOUND_STEP samples (2 ms), plus notes
// timed to the sample. Version 1 (the prototype's per-sample block) is not
// played.
constexpr uint32_t SOUND_RATE=16000;constexpr unsigned MAX_SOUND_CODE=128;
constexpr uint8_t SOUND_VERSION=2,SOUND_STEP_SHIFT=5;constexpr unsigned SOUND_STEP=1u<<SOUND_STEP_SHIFT;
constexpr unsigned MAX_VOICES=16,MAX_SOUND_EVENTS=512,SOUND_HEAD=12,VOICE_BYTES=20,EVENT_BYTES=16;
// Voice types this engine plays. A later engine adds types (and their own
// data) and a file that uses one needs that engine's level; this one refuses
// any type, waveform, filter, flag or event kind it does not know (Error::Engine).
enum VoiceType:uint8_t {TONE_VOICE=1,NOISE_VOICE=2,VOICE_TYPES};
enum Wave:uint8_t {WAVE_SINE,WAVE_SQUARE,WAVE_TRIANGLE,WAVE_SAW,WAVES};
enum Filter:uint8_t {NO_FILTER,LOWPASS,BANDPASS,HIGHPASS,FILTERS};
// ENVELOPE: silent until a note or trigger, then decaying; else the gain alone.
// NOTE_PITCH: a tone's frequency is its last note's times the hz parameter.
enum VoiceFlag:uint8_t {ENVELOPE=1,NOTE_PITCH=2,VOICE_FLAGS=3};
// Every voice reads these parameters from registers after each control step.
enum VoiceParam:uint8_t {GAIN,HZ,OCTAVE,CUBIC,K1,K2,TRIGGER,ATTACK,DECAY,LENGTH,VOICE_PARAMS};
enum EventKind:uint8_t {NOTE=1,EVENT_KINDS};
// Static caps on one frame: instructions run and primitives drawn.
constexpr uint32_t MAX_FRAME_OPS=24000,MAX_FRAME_DRAWS=3000;

// Read-only registers set by the engine each frame (and per instance: I).
enum Builtin:uint8_t {T=0,DUR,W,H,AX0,AY0,AX1,AY1,ACX,ACY,I,N,BOTTOM,ACCENT_R,ACCENT_G,ACCENT_B,WHITE_R,WHITE_G,WHITE_B,FRAME,BUILTINS};

// Instructions are four bytes: op, d, a, b. d is written (a temp register,
// FIRST_TEMP..FIRST_CONST-1); a and b are read (any register).
enum Op:uint8_t {
  NOP,MOV,ADD,SUB,MUL,DIV,MIN,MAX,ABS,FLOOR,FRACT,SIN,COS,SQRT,EXP,CLAMP01,SMOOTH,EASEOUT,
  MIX,     // d = a+(b-a)*d
  LT,LE,EQ,// d = a<b, a<=b, a==b (1 or 0)
  SEL,     // d = d!=0 ? a : b
  RND,     // d = rnd(uint(a),uint(b)) in [0,1)
  NOISE,   // d = value noise(int(a),int(b),uint(d)) in 0..255, coordinates in Q8 cells
  MOD,     // d = a-b*floor(a/b), 0 when b is 0
  MADD,    // d = a*b+d
  NEG,     // d = -a
  CMIX,    // d..d+2 = a..a+2 + (b..b+2 - a..a+2)*d (colour mix; d holds k)
  EXIT,    // ends this instance (no more draws) when a != 0
  DRAW,    // primitive d with its arguments at ARGS offset a|b<<8
  LDK,     // d = constant pool[a|b<<8]
  LDX,     // d = table b's entry int(a), clamped to the table (TABLES section)
  CSEL,    // d..d+2 = d!=0 ? a..a+2 : b..b+2 (colour select)
  OPS};
inline bool writes(uint8_t op){return op!=NOP && op!=EXIT && op!=DRAW && op<OPS;}

// Primitives, with their argument counts (a colour is three registers, r g b
// in RGB565 channel units: 0..31, 0..63, 0..31). Readout primitives change
// the RPM readout's own pixels and run only before the file's readout end.
enum Prim:uint8_t {
  CLIP,      // bottom row light may reach: y
  PLOT,      // x y C k
  DOT,       // x y C k: sub-pixel point over four pixels
  VDOT,      // x y C k: sub-pixel in y over two pixels
  SPARK,     // x y C k radius (<= 3.5)
  TWINKLE,   // x y C k arm (0..4)
  STAR,      // x y size C
  LINE,      // x0 y0 x1 y1 C k: one plot per step
  FLINE,     // x0 y0 x1 y1 C k0 k1: fixed point, weight ramping, off-screen points skipped
  SEGMENT,   // x0 y0 x1 y1 C k: white core with a coloured glow beside it
  ELLIPSE,   // cx cy rx ry C k points tilt squash: (cx+rx*cos a, cy+(ry*sin a)*squash), weight k*((1-tilt)+tilt*sin a)
  GLOW,      // cx cy rx ry C k: filled soft ellipse (rx <= 80, ry <= 60)
  BOLT,      // x0 y0 x1 y1 seed k depth: midpoint-displaced lightning (depth <= 5)
  RIMBOLT,   // x0 y0 x1 y1 seed Crim Couter k depth: black lightning drawn as two lit rims
  FIRE,      // base height scroll: rising noise flames along the width
  PRIME,     // y0 y1: build the UI mask of these rows (at most 16) before a long run of light
  GLINT,     // start length: the sheen over the header's glyphs (readout)
  SHIFTROW,  // y d: move one readout row sideways (readout)
  SHADE,     // C k lighten: pull the readout toward C, never below 3:1 (readout)
  CUT,       // along apart heat reach: Slash's cut through the readout (readout)
  HEATLINE,  // x0 y0 x1 y1 C k: brighten glyph pixels a line crosses (readout)
  BLADE,     // x0 x1 cx cy slope C k: pull every pixel on y = cy-(x-cx)*slope toward C, within 6 rows of the readout (readout)
  // Loops the effects repeat most, native: one call draws what took one
  // bytecode run per particle, column, curve point or sprite row.
  EMIT,      // n salt birth0 birthSpan life0 lifeSpan x0 y0 spreadX spreadY dir dirSpread speed0 speedSpan drag gravity swayAmp swayFreq
             //   Cbirth Cdeath k fadeIn fadeOut flicker style trail time shape: up to 256 seeded particles in closed-form motion
  STROKE,    // x0 x1 y0 slope a1 f1 p1 a2 f2 p2 a3 f3 p3 bumpX bumpH bumpW ringA ringF ringP noiseAmp noiseScale noisePhase noiseSalt gate
             //   Cstart Cend k0 k1 shimmer shimmerFreq shimmerPhase width smooth Cbody bodyK bodyRows:
             //   one point per column on a line plus three sine waves, a bump with rings spreading from it and value noise
  TRAIL,     // cx cy rx ry head sweep n Chead Ctail k tilt tailScale falloff edge rot width Cbody tailK:
             //   n points along a (rotated) ellipse arc, head to tail, thickening inward by width (an arc band, a crescent)
  STAMP,     // sprite x y scale flip C1 C2 C3 k1 k2 k3: a sprite of the file's SPRITES section, light in three colours and weights
  AURA,      // shells Cinner Couter k phase climb dither: glowing shells grown out of the UI halo round the readout
  DISC,      // cx cy rx ry C k dither top Crim rimK noise noiseScale noisePhase: a filled ellipse body, ordered-dithered, with a rim
  RUNES,     // x y dx dy n C k salt: a row of n tiny glyphs (tick, cross, hook, dot by hash) stepping by (dx, dy)
  PUFF,      // x y r k: adds a puff to the frame's cloud (up to 16); draws nothing yet
  CLOUD,     // Cbody Crim bodyK density top: draws the puffs added so far as one cloud, then empties it
  ARC,       // cx cy rx ry C k split: the lower half of an ellipse a column at a time, steep runs joined by lines
  HEATRING,  // cx cy r0 r1 C k: brighten glyph pixels between two radii round (cx, cy) (readout)
  VESSEL,    // cx top bottom topW bottomW surface C Cdeep k band phase: water in a glass, the light gathered toward its walls
  RAYS,      // cx cy r n base vary scale low high lift C k0 k1 phase salt offset: n tongues round a circle, noise lengths, leaning up
  PRIMS};
constexpr uint8_t PRIM_ARGS[PRIMS]={1,6,6,6,7,7,6,8,9,8,11,8,7,13,3,2,2,2,5,4,8,9,32,42,24,17,11,17,10,4,9,8,8,15,18};
inline bool readoutPrim(uint8_t p){return (p>=GLINT && p<=BLADE) || p==HEATRING;}

enum class Error:uint8_t {None,Size,Magic,Format,Engine,Checksum,Header,Section,Name,Consts,Code,Args,Blocks,Opcode,Register,Primitive,Budget,Missing};
inline const char *label(Error e) {
  static const char *s[]={"none","size","magic","format","engine","checksum","header","section","name","consts","code","args","blocks","opcode","register","primitive","budget","missing"};
  return s[unsigned(e)];
}

inline uint16_t get16(const uint8_t *p){return uint16_t(p[0]|p[1]<<8);}
inline uint32_t get32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline void put16(uint8_t *p,uint16_t v){p[0]=uint8_t(v);p[1]=uint8_t(v>>8);}
inline void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
// CRC-32 (IEEE, reflected), bitwise: files are small and checked once.
inline uint32_t crc32(const uint8_t *p,size_t n,uint32_t crc=0) {
  crc=~crc;
  while(n--){crc^=*p++;for(int k=0;k<8;++k)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));}
  return ~crc;
}

// Sections follow the header as type, flags, 16-bit length, then the body.
// An unknown section is skipped unless flagged required (bit 0), which a
// newer format uses for anything an older engine must not ignore.
enum Section:uint8_t {NAME=1,CONSTS=2,CODE=3,ARGS=4,BLOCKS=5,SOUND=6,META=7,SPRITES=8,TABLES=9};
// Tables: a count, then per table its first pool index and its length (16 bits each).
constexpr unsigned MAX_TABLES=64;
// Sprites: a count, then per sprite width and height (1-64 each) and its
// pixels row by row, two bits each (0 clear, 1-3 a colour), four to a byte.
constexpr unsigned MAX_SPRITES=16,MAX_SPRITE_SIDE=64;
// flags: 0, or GUARDED|register: the block is skipped when that register is not 0.
struct Block {uint16_t start=0,length=0,count=0,flags=0;};
constexpr uint16_t GUARDED=0x8000;

// A validated file. It points into the caller's bytes, which must outlive it.
struct Program {
  uint64_t uid=0;uint16_t revision=0,duration=0,readoutEnd=0,soundMs=0;
  char name[MAX_NAME+1]={};
  const uint8_t *code=nullptr;unsigned codeLength=0;  // instructions
  const uint8_t *args=nullptr;unsigned argsLength=0;  // register indices
  // The constant pool: the first MAX_CONSTS (the registers) decoded, all
  // of it in the file for LDK.
  float consts[MAX_CONSTS]={};const uint8_t *pool=nullptr;unsigned constCount=0;
  float constant(unsigned i)const{const uint32_t bits=get32(pool+4*i);float v;std::memcpy(&v,&bits,4);return v;}
  Block blocks[MAX_BLOCKS];unsigned blockCount=0;
  // The sound (SOUND section): the control block's code range, the voice
  // records and the notes, read from the file by Sound::begin.
  bool hasSound=false;uint16_t soundStart=0,soundLength=0,soundEvents=0;uint8_t soundVoices=0;
  const uint8_t *soundVoice=nullptr,*soundEvent=nullptr;
  struct Sprite {uint8_t w=0,h=0;const uint8_t *bits=nullptr;};
  struct Table {uint16_t base=0,length=0;};
  Table tables[MAX_TABLES];unsigned tableCount=0;
  Sprite sprites[MAX_SPRITES];unsigned spriteCount=0;
  uint32_t soundSamples()const{return hasSound?uint32_t(soundMs)*SOUND_RATE/1000:0;}
  uint32_t maxOps=0,maxDraws=0;                       // static bounds per frame
  // META: the built-in effect that plays when this one cannot (missing,
  // damaged, needs a newer engine); NO_FALLBACK means Orbit.
  static constexpr uint8_t NO_FALLBACK=0xff;
  uint8_t fallback=NO_FALLBACK;
};

// Header: magic, format, engine, flags (16), uid (64), revision, duration,
// readout end, sound length (16 each, ms), file bytes (32), CRC-32 of the
// whole file with its own field zeroed (32).
// `toPlay`: the code is executed from where it lies, so it must start on a
// 4-byte boundary; a check of a file that only travels or is stored (a
// backup, the store's install) passes false and never plays the result.
inline Error load(const uint8_t *p,size_t n,Program &out,bool toPlay=true) {
  out=Program();
  if(!p || n<HEADER_BYTES || n>MAX_FILE)return Error::Size;
  if(get32(p)!=MAGIC)return Error::Magic;
  if(p[4]!=FORMAT)return Error::Format;
  if(p[5]==0 || p[5]>ENGINE)return Error::Engine;
  if(get32(p+24)!=n)return Error::Size;
  {
    uint8_t zero[4]={};
    uint32_t crc=crc32(p,28);crc=crc32(zero,4,crc);crc=crc32(p+32,n-32,crc);
    if(crc!=get32(p+28))return Error::Checksum;
  }
  if(get16(p+6))return Error::Header;  // no flags defined yet
  out.uid=uint64_t(get32(p+8))|uint64_t(get32(p+12))<<32;
  out.revision=get16(p+16);out.duration=get16(p+18);out.readoutEnd=get16(p+20);out.soundMs=get16(p+22);
  if(out.duration<MIN_DURATION || out.duration>MAX_DURATION || out.readoutEnd>MAX_READOUT_MS ||
     uint32_t(out.readoutEnd)*4>uint32_t(out.duration)*3 || out.soundMs>MAX_SOUND_MS || out.soundMs>out.duration)return Error::Header;
  bool seen[16]={};
  for(size_t at=HEADER_BYTES;at<n;) {
    if(n-at<4)return Error::Section;
    const uint8_t type=p[at],flags=p[at+1];const size_t length=get16(p+at+2);
    const uint8_t *body=p+at+4;
    if(length>n-at-4)return Error::Section;
    at+=4+length;
    if(type<16 && seen[type] && type!=0)return Error::Section;
    if(type<16)seen[type]=true;
    switch(type) {
      case NAME:
        if(length<1 || length>MAX_NAME)return Error::Name;
        for(size_t i=0;i<length;++i)if(body[i]<0x20 || body[i]>0x7e)return Error::Name;
        if(body[0]==' ' || body[length-1]==' ')return Error::Name;
        std::memcpy(out.name,body,length);break;
      case CONSTS:
        if(length%4 || length/4>MAX_POOL)return Error::Consts;
        out.constCount=unsigned(length/4);out.pool=body;
        for(unsigned i=0;i<out.constCount;++i) {
          const float v=out.constant(i);
          if(!std::isfinite(v))return Error::Consts;
          if(i<MAX_CONSTS)out.consts[i]=v;
        }
        break;
      // Instructions are read as aligned 32-bit words.
      case CODE:
        if(length%4 || length==0 || length/4>MAX_CODE || (toPlay && (reinterpret_cast<uintptr_t>(body)&3u)))return Error::Code;
        out.code=body;out.codeLength=unsigned(length/4);break;
      case ARGS:
        if(length>MAX_ARGS)return Error::Args;
        out.args=body;out.argsLength=unsigned(length);break;
      case BLOCKS:
        if(length%8 || length==0 || length/8>MAX_BLOCKS)return Error::Blocks;
        out.blockCount=unsigned(length/8);
        for(unsigned i=0;i<out.blockCount;++i)
          out.blocks[i]={get16(body+8*i),get16(body+8*i+2),get16(body+8*i+4),get16(body+8*i+6)};
        break;
      // Version 1, the output register, the code's start and length, then zero.
      case SPRITES: {
        if(length<1 || body[0]>MAX_SPRITES)return Error::Section;
        out.spriteCount=body[0];size_t at=1;
        for(unsigned k=0;k<out.spriteCount;++k) {
          if(length-at<2)return Error::Section;
          const uint8_t w=body[at],h=body[at+1];
          if(!w || !h || w>MAX_SPRITE_SIDE || h>MAX_SPRITE_SIDE)return Error::Section;
          const size_t bytes=(size_t(w)*h+3)/4;
          if(length-at-2<bytes)return Error::Section;
          out.sprites[k]={w,h,body+at+2};at+=2+bytes;
        }
        if(at!=length)return Error::Section;
        break;
      }
      case TABLES:
        if(length<1 || body[0]>MAX_TABLES || length!=1+4u*body[0])return Error::Section;
        out.tableCount=body[0];
        for(unsigned k=0;k<out.tableCount;++k)out.tables[k]={get16(body+1+4*k),get16(body+3+4*k)};
        break;
      // Version 1, then the fallback effect's ID; later versions may add bytes after.
      case META:
        if(length<2 || body[0]!=1)return Error::Section;
        out.fallback=body[1];break;
      // Version, voices, step shift, outputs, the control code's start and
      // length, the notes, sample banks (none yet); the voice records, then
      // the notes. Checked once the code and constants are known.
      case SOUND:
        if(length<1)return Error::Section;
        if(body[0]>SOUND_VERSION)return Error::Engine;  // a newer sound: a later engine's
        if(body[0]!=SOUND_VERSION || length<SOUND_HEAD)return Error::Section;
        if(body[2]!=SOUND_STEP_SHIFT || body[3]!=1 || get16(body+10))return Error::Engine;
        out.soundVoices=body[1];out.soundStart=get16(body+4);out.soundLength=get16(body+6);out.soundEvents=get16(body+8);
        if(!out.soundVoices || out.soundVoices>MAX_VOICES || out.soundEvents>MAX_SOUND_EVENTS ||
           length!=SOUND_HEAD+size_t(out.soundVoices)*VOICE_BYTES+size_t(out.soundEvents)*EVENT_BYTES)return Error::Section;
        out.hasSound=true;out.soundVoice=body+SOUND_HEAD;out.soundEvent=out.soundVoice+size_t(out.soundVoices)*VOICE_BYTES;break;
      default:if(flags&1)return Error::Section;  // required but unknown
    }
  }
  if(!seen[NAME] || !seen[CODE] || !seen[BLOCKS])return Error::Missing;
  for(unsigned k=0;k<out.tableCount;++k)
    if(!out.tables[k].length || out.tables[k].base>=out.constCount || out.tables[k].length>out.constCount-out.tables[k].base)return Error::Consts;
  // Every instruction of every block: a known op, writes only to temps,
  // reads only constants that exist, draws only known primitives whose
  // arguments lie inside ARGS.
  const unsigned lastReg=FIRST_CONST+std::min(out.constCount,MAX_CONSTS);
  auto readable=[&](uint8_t r){return r<BUILTINS || (r>=FIRST_TEMP && r<lastReg);};
  // One instruction: a known op that writes only temps and reads only what
  // exists; a draw (where allowed) names a known primitive whose argument
  // registers lie inside ARGS.
  auto check=[&](unsigned pc,bool drawing)->Error {
    const uint8_t *in=out.code+4*pc;const uint8_t op=in[0],d=in[1],a=in[2],bb=in[3];
    if(op>=OPS)return Error::Opcode;
    if(op==DRAW) {
      if(!drawing || d>=PRIMS)return Error::Primitive;
      const unsigned offset=a|bb<<8;
      if(offset>out.argsLength || PRIM_ARGS[d]>out.argsLength-offset)return Error::Args;
      // A colour argument spans three registers, each listed and checked.
      for(unsigned j=0;j<PRIM_ARGS[d];++j)if(!readable(out.args[offset+j]))return Error::Register;
      return Error::None;
    }
    if(writes(op) && (d<FIRST_TEMP || d>=FIRST_CONST))return Error::Register;
    if(op==LDK)return unsigned(a|bb<<8)<out.constCount?Error::None:Error::Consts;
    if(op==LDX)return bb<out.tableCount?(readable(a)?Error::None:Error::Register):Error::Consts;
    if((op==CMIX || op==CSEL) && (d>FIRST_CONST-3 || !readable(a) || !readable(uint8_t(a+2)) || !readable(bb) || !readable(uint8_t(bb+2)) || a>REGS-3 || bb>REGS-3))return Error::Register;
    if(op!=NOP && !readable(a))return Error::Register;
    const bool binary=op==ADD || op==SUB || op==MUL || op==DIV || op==MIN || op==MAX || op==MIX || op==LT || op==LE || op==EQ ||
                      op==SEL || op==RND || op==NOISE || op==MOD || op==MADD;
    if(binary && !readable(bb))return Error::Register;
    return Error::None;
  };
  for(unsigned b=0;b<out.blockCount;++b) {
    const Block &k=out.blocks[b];
    if(k.count<1 || k.count>MAX_COUNT || k.length<1 || k.start>=out.codeLength || k.length>out.codeLength-k.start)return Error::Blocks;
    if(k.flags && ((k.flags&~0xffu)!=GUARDED || !readable(uint8_t(k.flags))))return Error::Blocks;
    uint32_t draws=0;
    for(unsigned pc=k.start;pc<k.start+k.length;++pc) {
      const Error e=check(pc,true);if(e!=Error::None)return e;
      draws+=out.code[4*pc]==DRAW;
    }
    out.maxOps+=uint32_t(k.count)*k.length;out.maxDraws+=uint32_t(k.count)*draws;
  }
  if(out.hasSound) {
    // The control block (it may be empty): no draws, writes only temps.
    if(!out.soundMs || out.soundLength>MAX_SOUND_CODE || (out.soundLength && (out.soundStart>=out.codeLength ||
       out.soundLength>out.codeLength-out.soundStart)))return Error::Section;
    for(unsigned pc=out.soundStart;pc<out.soundStart+out.soundLength;++pc){const Error e=check(pc,false);if(e!=Error::None)return e;}
    // Voices: type, waveform, filter, flags, a 32-bit datum (a noise seed),
    // a register per parameter, two zero bytes.
    for(unsigned v=0;v<out.soundVoices;++v) {
      const uint8_t *r=out.soundVoice+size_t(v)*VOICE_BYTES;
      const uint8_t type=r[0],wave=r[1],filter=r[2],flags=r[3];
      if(!type || type>=VOICE_TYPES || (type==TONE_VOICE?wave>=WAVES:wave!=0) || filter>=FILTERS || (flags&~VOICE_FLAGS) ||
         (type==NOISE_VOICE && (flags&NOTE_PITCH)) || get16(r+18))return Error::Engine;
      if(type==NOISE_VOICE?!get32(r+4):get32(r+4)!=0)return Error::Section;  // xorshift needs a seed; a tone has none
      for(unsigned k=0;k<VOICE_PARAMS;++k)if(!readable(r[8+k]))return Error::Register;
    }
    // Notes: sample (at the sound's 16 kHz, in order, within the sound),
    // voice, kind, two zero bytes, level and frequency (finite floats).
    const uint32_t samples=out.soundSamples();uint32_t last=0;
    for(unsigned e=0;e<out.soundEvents;++e) {
      const uint8_t *r=out.soundEvent+size_t(e)*EVENT_BYTES;
      if(!r[5] || r[5]>=EVENT_KINDS || get16(r+6))return Error::Engine;
      const uint32_t at=get32(r);float level,hz;const uint32_t lb=get32(r+8),hb=get32(r+12);std::memcpy(&level,&lb,4);std::memcpy(&hz,&hb,4);
      if(at<last || at>=samples || r[4]>=out.soundVoices || !std::isfinite(level) || !std::isfinite(hz))return Error::Section;
      last=at;
    }
  }
  if(out.maxOps>MAX_FRAME_OPS || out.maxDraws>MAX_FRAME_DRAWS)return Error::Budget;
  return Error::None;
}

// The parts of a file read every frame (the blocks' instructions, primitive
// arguments and the constant pool, tables included) copied into `hot`, at
// most HOT_BYTES of internal RAM, and the program pointed at the copy; the
// file itself (sprites, the sound) can then stay in PSRAM. The placed
// program only draws: code past the last block (the sound's control block,
// which the compiler puts last) is not copied, and the sound is rendered
// from the file (Sound). False, with the program unchanged, when they do
// not fit or `hot` is not 4-byte aligned.
constexpr size_t HOT_BYTES=4096;
inline unsigned drawCode(const Program &p){unsigned end=0;for(unsigned b=0;b<p.blockCount;++b)end=std::max(end,unsigned(p.blocks[b].start+p.blocks[b].length));return end;}
inline size_t hotBytes(const Program &p){return size_t(drawCode(p))*4+size_t(p.constCount)*4+p.argsLength;}
inline bool place(Program &p,uint8_t *hot,size_t capacity) {
  const size_t need=hotBytes(p);
  if(!hot || (reinterpret_cast<uintptr_t>(hot)&3) || need>capacity || need>HOT_BYTES)return false;
  uint8_t *at=hot;const unsigned code=drawCode(p);
  std::memcpy(at,p.code,size_t(code)*4);p.code=at;p.codeLength=code;at+=size_t(code)*4;
  p.hasSound=false;p.soundStart=p.soundLength=p.soundEvents=0;p.soundVoices=0;p.soundVoice=p.soundEvent=nullptr;
  if(p.constCount)std::memcpy(at,p.pool,size_t(p.constCount)*4);p.pool=at;at+=size_t(p.constCount)*4;
  if(p.argsLength)std::memcpy(at,p.args,p.argsLength);p.args=at;
  return true;
}

// The arithmetic of one instruction, shared with the compiler's constant
// folding so a folded constant equals what the engine would compute.
// No libm calls whose results differ between C libraries: sine is the
// renderer's table, exp is computed here, sqrt is correctly rounded.
// The renderer's sine and smoothstep tables, copied into internal RAM; the
// engine's sine and noise compute exactly what the renderer's do.
alignas(4) LLFX_RAM inline const std::array<float,257> SINE_RAM=BestEffectUI::detail::sineTable();
alignas(4) LLFX_RAM inline const std::array<uint8_t,256> SMOOTH_RAM=BestEffectUI::detail::smoothTable();
struct Math {
  static float sine(float a) {
    if(!(std::fabs(a)<1e6f))a=std::isfinite(a)?std::fmod(a,6.28318531f):0.0f;
    const float u=a*(256/(2*3.14159265f));int i=int(u);i-=u<i;const float f=u-i;const int j=i&255;
    return SINE_RAM[size_t(j)]+(SINE_RAM[size_t(j+1)]-SINE_RAM[size_t(j)])*f;
  }
  // Renderer::noise: value noise, coordinates in Q8 cells, 0..255.
  static int noise(int x,int y,uint32_t salt) {
    const int ix=x>>8,iy=y>>8,u=SMOOTH_RAM[size_t(x&255)],v=SMOOTH_RAM[size_t(y&255)];
    using R=BestEffectUI::Renderer;
    const int a=R::lattice(ix,iy,salt),b=R::lattice(ix+1,iy,salt),c=R::lattice(ix,iy+1,salt),d=R::lattice(ix+1,iy+1,salt);
    const int top=a+(((b-a)*u)>>8),bottom=c+(((d-c)*u)>>8);
    return top+(((bottom-top)*v)>>8);
  }
  LLFX_EXACT static float exp(float x) {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    if(!(x>-80.0f))return 0.0f;
    if(x>80.0f)x=80.0f;
    // 2^(x*log2 e): an integer power by the exponent bits and a polynomial
    // for the fraction (relative error about 2e-7).
    const float y=x*1.44269504f;const float fl=std::floor(y);const float f=y-fl;
    float p=1.53533974e-4f;p=p*f+1.33988747e-3f;p=p*f+9.61835631e-3f;p=p*f+5.55041086e-2f;p=p*f+2.40226507e-1f;p=p*f+6.93147182e-1f;p=p*f+1.0f;
    const int e=int(fl)+127;
    if(e<=0)return 0.0f;
    const uint32_t bits=uint32_t(e)<<23;float scale;std::memcpy(&scale,&bits,4);
    return p*scale;
  }
  static float clamp01(float v){return v<0?0:v>1?1:v;}
  static float rnd(float i,float salt){return BestEffectUI::Renderer::rnd(toU32(i),toU32(salt));}
  // floor without libm (a library call on the ESP32-S3): exact for every
  // finite float; non-finite gives 0.
  static float floor(float a) {
    if(!(std::fabs(a)<8388608.0f))return std::isfinite(a)?a:0.0f;
    const float i=float(int32_t(a));return i>a?i-1.0f:i;
  }
  static uint32_t toU32(float v) {
    if(!(v==v))return 0;
    const float c=v<-2147483648.0f?-2147483648.0f:v>2147483520.0f?2147483520.0f:v;
    return uint32_t(int32_t(c));
  }
  static int toInt(float v,int lo,int hi){if(!(v==v))return 0;return v<float(lo)?lo:v>float(hi)?hi:int(v);}
  LLFX_EXACT static float apply(uint8_t op,float a,float b,float d) {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    switch(op) {
      case MOV:return a;
      case ADD:return a+b;
      case SUB:return a-b;
      case MUL:return a*b;
      case DIV:return b==0?0.0f:a/b;
      case MIN:return a<b?a:b;
      case MAX:return a>b?a:b;
      case ABS:return std::fabs(a);
      case FLOOR:return floor(a);
      case FRACT:return std::isfinite(a)?a-floor(a):0.0f;
      case SIN:return sine(a);
      case COS:return sine(a+3.14159265f/2);
      case SQRT:return a>0?std::sqrt(a):0.0f;
      case EXP:return exp(a);
      case CLAMP01:return clamp01(a);
      case SMOOTH:{const float v=clamp01(a);return v*v*(3-2*v);}
      case EASEOUT:{const float v=1-clamp01(a);return 1-v*v*v;}
      case MIX:{const float t=b-a;const float u=t*d;return a+u;}
      case LT:return a<b?1.0f:0.0f;
      case LE:return a<=b?1.0f:0.0f;
      case EQ:return a==b?1.0f:0.0f;
      case SEL:return d!=0?a:b;
      case RND:return rnd(a,b);
      case NOISE:return float(noise(toInt(a,-(1<<24),1<<24),toInt(b,-(1<<24),1<<24),toU32(d)));
      case MOD:{if(b==0 || !std::isfinite(a) || !std::isfinite(b))return 0.0f;const float q=floor(a/b);const float m=b*q;return a-m;}
      case MADD:{const float m=a*b;return m+d;}
      case NEG:return -a;
      default:return 0.0f;
    }
  }
};

// Runs instructions pc..end on the registers R, counting them in ops; DRAW
// calls draw(primitive, arguments), which returns false to stop. False when
// the run ended early (EXIT or a stopped draw).
// Every common instruction is handled here, inline: a second dispatch
// through Math::apply cost about half of each instruction on the device.
// The cases must compute exactly what Math::apply does (test_fx_effects
// checks every op on edge and random values). The code is read a 32-bit
// word at a time (the loader requires it aligned) and dispatched through a
// table of labels where the compiler has them, a switch otherwise.
template<class Draw> LLFX_HOT LLFX_EXACT inline bool execute(const Program &p,float *R,unsigned pc,unsigned end,uint32_t &ops,Draw &&draw) {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
  const uint32_t *const code=reinterpret_cast<const uint32_t *>(p.code);
  const uint32_t *w=code+pc,*const stop=code+end;const unsigned start=pc;
  uint32_t ins;unsigned d,a,b;
#define LLFX_OP(name,...) L_##name:__VA_ARGS__;LLFX_NEXT;
#if defined(__GNUC__)
  static const void *const LABELS[OPS] LLFX_RAM={&&L_NOP,&&L_MOV,&&L_ADD,&&L_SUB,&&L_MUL,&&L_DIV,&&L_MIN,&&L_MAX,&&L_ABS,&&L_FLOOR,&&L_FRACT,&&L_SIN,&&L_COS,
    &&L_SQRT,&&L_EXP,&&L_CLAMP01,&&L_SMOOTH,&&L_EASEOUT,&&L_MIX,&&L_LT,&&L_LE,&&L_EQ,&&L_SEL,&&L_RND,&&L_NOISE,&&L_MOD,&&L_MADD,&&L_NEG,&&L_CMIX,
    &&L_EXIT,&&L_DRAW,&&L_LDK,&&L_LDX,&&L_CSEL};
  static_assert(sizeof(LABELS)/sizeof(*LABELS)==OPS,"one label per op");
#define LLFX_NEXT do{if(w==stop)goto done;ins=*w++;d=(ins>>8)&255u;a=(ins>>16)&255u;b=ins>>24;goto *LABELS[ins&255u];}while(0)
  LLFX_NEXT;
#else
#define LLFX_NEXT continue
  for(;;) {
    if(w==stop)goto done;
    ins=*w++;d=(ins>>8)&255u;a=(ins>>16)&255u;b=ins>>24;
    switch(ins&255u) {
      case NOP:goto L_NOP;case MOV:goto L_MOV;case ADD:goto L_ADD;case SUB:goto L_SUB;case MUL:goto L_MUL;case DIV:goto L_DIV;case MIN:goto L_MIN;
      case MAX:goto L_MAX;case ABS:goto L_ABS;case FLOOR:goto L_FLOOR;case FRACT:goto L_FRACT;case SIN:goto L_SIN;case COS:goto L_COS;case SQRT:goto L_SQRT;
      case EXP:goto L_EXP;case CLAMP01:goto L_CLAMP01;case SMOOTH:goto L_SMOOTH;case EASEOUT:goto L_EASEOUT;case MIX:goto L_MIX;case LT:goto L_LT;
      case LE:goto L_LE;case EQ:goto L_EQ;case SEL:goto L_SEL;case RND:goto L_RND;case NOISE:goto L_NOISE;case MOD:goto L_MOD;case MADD:goto L_MADD;
      case NEG:goto L_NEG;case CMIX:goto L_CMIX;case EXIT:goto L_EXIT;case DRAW:goto L_DRAW;case LDK:goto L_LDK;case LDX:goto L_LDX;default:goto L_CSEL;
    }
#endif
  LLFX_OP(NOP,)
  LLFX_OP(MOV,R[d]=R[a])
  LLFX_OP(ADD,R[d]=R[a]+R[b])
  LLFX_OP(SUB,R[d]=R[a]-R[b])
  LLFX_OP(MUL,R[d]=R[a]*R[b])
  LLFX_OP(DIV,R[d]=R[b]==0?0.0f:R[a]/R[b])
  LLFX_OP(MIN,R[d]=R[a]<R[b]?R[a]:R[b])
  LLFX_OP(MAX,R[d]=R[a]>R[b]?R[a]:R[b])
  LLFX_OP(ABS,R[d]=std::fabs(R[a]))
  LLFX_OP(FLOOR,R[d]=Math::floor(R[a]))
  LLFX_OP(FRACT,{const float v=R[a];R[d]=std::isfinite(v)?v-Math::floor(v):0.0f;})
  LLFX_OP(SIN,R[d]=Math::sine(R[a]))
  LLFX_OP(COS,R[d]=Math::sine(R[a]+3.14159265f/2))
  LLFX_OP(SQRT,R[d]=Math::apply(SQRT,R[a],0,0))
  LLFX_OP(EXP,R[d]=Math::exp(R[a]))
  LLFX_OP(CLAMP01,R[d]=Math::clamp01(R[a]))
  LLFX_OP(SMOOTH,{const float v=Math::clamp01(R[a]);R[d]=v*v*(3-2*v);})
  LLFX_OP(EASEOUT,{const float v=1-Math::clamp01(R[a]);R[d]=1-v*v*v;})
  LLFX_OP(MIX,{const float t=R[b]-R[a];const float u=t*R[d];R[d]=R[a]+u;})
  LLFX_OP(LT,R[d]=R[a]<R[b]?1.0f:0.0f)
  LLFX_OP(LE,R[d]=R[a]<=R[b]?1.0f:0.0f)
  LLFX_OP(EQ,R[d]=R[a]==R[b]?1.0f:0.0f)
  LLFX_OP(SEL,R[d]=R[d]!=0?R[a]:R[b])
  LLFX_OP(RND,R[d]=Math::rnd(R[a],R[b]))
  LLFX_OP(NOISE,R[d]=Math::apply(NOISE,R[a],R[b],R[d]))
  LLFX_OP(MOD,R[d]=Math::apply(MOD,R[a],R[b],0))
  LLFX_OP(MADD,{const float m=R[a]*R[b];R[d]=m+R[d];})
  LLFX_OP(NEG,R[d]=-R[a])
  LLFX_OP(CMIX,{const float k=R[d];for(unsigned c=0;c<3;++c){const float x=R[a+c],y=R[b+c];const float u=(y-x)*k;R[d+c]=x+u;}})
  LLFX_OP(EXIT,if(R[a]!=0){ops+=unsigned(w-code)-start;return false;})
  // The count is brought up to date before a draw, which the guard reads.
  LLFX_OP(DRAW,{const unsigned run=unsigned(w-code)-start;ops+=run;if(!draw(uint8_t(d),p.args+(a|b<<8)))return false;ops-=run;})
  LLFX_OP(LDK,R[d]=p.constant(a|b<<8))
  LLFX_OP(LDX,{const Program::Table &tb=p.tables[b];R[d]=p.constant(tb.base+unsigned(Math::toInt(R[a],0,tb.length-1)));})
  LLFX_OP(CSEL,{const unsigned from=R[d]!=0?a:b;const float x=R[from],y=R[from+1],z=R[from+2];R[d]=x;R[d+1]=y;R[d+2]=z;})
#if !defined(__GNUC__)
  }
#endif
done:
  ops+=end-start;
  return true;
#undef LLFX_OP
#undef LLFX_NEXT
}

// Renders a file's sound like BestEffectSound::Synth: resumable, a slice at
// a time, any slicing giving the same samples, through the same limiter.
//
// Every SOUND_STEP samples (2 ms) the control block runs once, with t the
// time of the step's first sample and i the step's index; its registers
// start at zero and keep their values from one step to the next. The
// voices then read their parameters from their registers and render the
// samples in between natively, as the built-in synth's helpers do
// (BestEffectSound::detail). The block runs one step ahead, so a voice's
// gain, frequency (glide) and filter coefficients move in a straight line
// from one step's value to the next, without stairs or lag. A trigger above
// 0 restarts a voice's envelope at that level on the step's first sample;
// a note does the same on its own sample and may set the voice's pitch. A
// restarted tone starts from phase 0.
//
// A voice is a source (a tone read from the built-in synth's 1024-point
// sine table with a Q32 phase: a sine with an octave partial and cubic
// drive, a square, a triangle or a saw; or xorshift noise from a seed,
// advanced every sample even while silent, so voices sharing a seed share
// their noise), an optional filter (one-pole low-pass y += (x-y)*k1, a
// band-pass as the difference of one-poles k1 and k2, or a high-pass x
// minus the low-pass), and an envelope (a smoothstep attack, an exponential
// decay applied per sample, and an optional length after which it stops).
// A voice whose envelope has stopped, or that has none and a gain of 0 over
// a whole step, renders nothing and holds its phase and filters.
// Parameters are clamped, so any file renders finite, bounded samples, and
// the cost is bounded by the file: at most MAX_SOUND_CODE instructions a
// step and MAX_VOICES voices a sample.
class Sound {
public:
  // What a render did, for the device cost model (docs/FIRMWARE_LOADABLE_EFFECTS.md, "Sound").
  struct Work {uint32_t steps=0,ops=0,events=0,voiceSamples=0,toneSamples=0,noiseSamples=0,filterSamples=0,partialSamples=0,attackSamples=0,idleNoise=0;};
  void begin(const Program &p) {
    p_=&p;index_=step_=event_=filled_=0;work_=Work();
    if(!p.hasSound)return;
    std::memset(reg_,0,sizeof(reg_));
    std::memcpy(reg_+FIRST_CONST,p.consts,std::min(p.constCount,MAX_CONSTS)*sizeof(float));
    reg_[DUR]=p.duration*.001f;reg_[N]=float(p.soundVoices);
    for(unsigned v=0;v<p.soundVoices;++v) {
      const uint8_t *r=p.soundVoice+size_t(v)*VOICE_BYTES;Voice &x=voice_[v];x=Voice();
      x.type=r[0];x.wave=r[1];x.filter=r[2];x.flags=r[3];x.noise=get32(r+4);
      for(unsigned k=0;k<VOICE_PARAMS;++k)x.reg[k]=r[8+k];
      x.on=!(x.flags&ENVELOPE);
      // Noise voices with one seed always hold the same state: a silent one
      // takes it from the first of them instead of advancing its own.
      x.leader=uint8_t(v);
      if(x.type==NOISE_VOICE)for(unsigned u=0;u<v;++u)if(voice_[u].type==NOISE_VOICE && get32(p.soundVoice+size_t(u)*VOICE_BYTES+4)==x.noise){x.leader=uint8_t(u);break;}
    }
    control(0);
    for(unsigned v=0;v<p.soundVoices;++v)read(voice_[v],voice_[v].now);
  }
  size_t run(int16_t *out,size_t count) {
    if(!p_ || !p_->hasSound)return 0;
    const uint32_t n=p_->soundSamples();size_t written=0;
    while(written<count && index_<n) {
      const uint32_t within=index_&(SOUND_STEP-1);
      if(within==0 && filled_<=index_)render();
      out[written++]=limit(mix_[within],index_,n);++index_;
    }
    return written;
  }
  bool done()const{return !p_ || !p_->hasSound || index_>=p_->soundSamples();}
  uint32_t ops()const{return work_.ops;}
  const Work &work()const{return work_;}
  // BestEffectSound::Synth's limiter: 2 ms in, 25 ms out, linear to 0.6 of
  // full scale and compressing toward 0.95; silence for a non-finite sample.
  // Never contracted into fused multiply-adds (the device's FPU has them),
  // so the device renders what the host and the browser render.
  LLFX_EXACT static int16_t limit(float v,uint32_t i,uint32_t n) {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    if(!std::isfinite(v))return 0;
    constexpr uint32_t ATTACK=SOUND_RATE/500,FADE=SOUND_RATE/40;
    if(i<ATTACK)v*=float(i)/ATTACK;
    if(n-i<FADE)v*=float(n-i)/FADE;
    const float a=std::fabs(v),d=a-.6f,limited=d<=0?a:.6f+.35f*3*d/(1+3*d);
    return int16_t((v<0?-limited:limited)*32767);
  }
  // The built-in synth's clock: sample i is at i/16000 s, in float.
  static float timeOf(uint32_t sample){return float(sample)*(1.0f/SOUND_RATE);}

private:
  // A voice's parameters at one step, clamped.
  struct Params {float gain=0,hz=0,octave=0,cubic=0,k1=0,k2=0,trigger=0,attack=0,decay=0,length=0;};
  struct Voice {
    uint8_t type=0,wave=0,filter=0,flags=0,leader=0;uint8_t reg[VOICE_PARAMS]={};
    Params now;                        // at the step's first sample
    uint32_t phase=0,noise=1;          // tone phase (Q32 turns), noise state
    float y1=0,y2=0;                   // filter states
    float note=0;                      // the last note's frequency (NOTE_PITCH)
    bool on=false;                     // sounding: no envelope, or one running
    float level=0,k=1,rate=0;          // envelope level, its per-sample factor and the rate that is for
    uint32_t age=0,attack=0,length=0;  // samples since the trigger, attack and length in samples
  };
  static float clampf(float v,float lo,float hi){return v==v?(v<lo?lo:v>hi?hi:v):0.0f;}
  void read(const Voice &v,Params &q)const {
    auto r=[&](unsigned k){return reg_[v.reg[k]];};
    q.gain=clampf(r(GAIN),-8,8);q.hz=clampf(r(HZ),0,7999);q.octave=clampf(r(OCTAVE),-4,4);q.cubic=clampf(r(CUBIC),-4,4);
    q.k1=clampf(r(K1),0,1);q.k2=clampf(r(K2),0,1);q.trigger=clampf(r(TRIGGER),0,8);q.attack=clampf(r(ATTACK),0,1);
    q.decay=clampf(r(DECAY),0,20000);q.length=clampf(r(LENGTH),0,10);
  }
  // The control block for step j.
  void control(uint32_t j) {
    if(!p_->soundLength)return;
    reg_[T]=timeOf(j*SOUND_STEP);reg_[I]=float(j);
    execute(*p_,reg_,p_->soundStart,p_->soundStart+p_->soundLength,work_.ops,[](uint8_t,const uint8_t *){return false;});
  }
  LLFX_EXACT static uint32_t increment(float hz){return uint32_t(hz*(4294967296.0f/SOUND_RATE));}
  LLFX_EXACT static uint32_t samplesOf(float seconds) {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    const float n=seconds*SOUND_RATE;return uint32_t(n+.5f);
  }
  void trigger(Voice &v,float level) {
    v.on=level>0 || !(v.flags&ENVELOPE);v.level=level;v.age=0;v.phase=0;
    v.attack=samplesOf(v.now.attack);v.length=samplesOf(v.now.length);
  }
  // The per-sample factor of a decay rate, e^(-rate/16000), when the rate
  // changes: a Taylor series in double (|x| <= 1.25, 24 terms), so the
  // factor is as exact as the synth's std::exp and the same everywhere;
  // Math::exp's error near 1 (about 5e-6) would make a 5.5/s decay 0.08/s
  // faster, 0.45 dB by the end of Orbit's chime.
  LLFX_EXACT static void decayFor(Voice &v) {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    if(v.now.decay==v.rate)return;
    v.rate=v.now.decay;
    const double x=-double(v.rate)/SOUND_RATE;double term=1,sum=1;
    for(int n=1;n<=24;++n){term=term*x/n;sum=sum+term;}
    v.k=float(sum);
  }
  static void noiseFor(Voice &v,uint32_t samples){for(uint32_t j=0;j<samples;++j){v.noise^=v.noise<<13;v.noise^=v.noise>>17;v.noise^=v.noise<<5;}}
  // One step: the control block for the next step, then the voices over
  // this one (split at its notes), mixed into mix_.
  void render() {
    const Program &p=*p_;
    const uint32_t base=index_,len=std::min<uint32_t>(SOUND_STEP,p.soundSamples()-base);
    ++work_.steps;
    control(step_+1);
    Params next[MAX_VOICES];
    for(unsigned v=0;v<p.soundVoices;++v)read(voice_[v],next[v]);
    for(unsigned j=0;j<SOUND_STEP;++j)mix_[j]=0;
    for(unsigned v=0;v<p.soundVoices;++v){Voice &x=voice_[v];decayFor(x);if(x.now.trigger>0)trigger(x,x.now.trigger);}
    for(uint32_t at=0;at<len;) {
      while(event_<p.soundEvents) {
        const uint8_t *e=p.soundEvent+size_t(event_)*EVENT_BYTES;
        if(get32(e)>base+at)break;
        // A note: the envelope restarts at its level, the tone takes its frequency if it gives one.
        Voice &x=voice_[e[4]];float level,hz;const uint32_t lb=get32(e+8),hb=get32(e+12);std::memcpy(&level,&lb,4);std::memcpy(&hz,&hb,4);
        if(hz>0)x.note=clampf(hz,0,7999);
        trigger(x,clampf(level,0,8));++event_;++work_.events;
      }
      const uint32_t end=event_<p.soundEvents?std::min(len,get32(p.soundEvent+size_t(event_)*EVENT_BYTES)-base):len;
      for(unsigned v=0;v<p.soundVoices;++v)voice(voice_[v],next[v],at,end);
      at=end;
    }
    for(unsigned v=0;v<p.soundVoices;++v) {
      Voice &x=voice_[v];x.now=next[v];
      // An envelope below half a least significant bit has ended.
      if(x.on && (x.flags&ENVELOPE) && x.level<1.0f/65536)x.on=false;
    }
    filled_=base+len;++step_;
  }
  // Samples [from, to) of the step for one voice: its parameters move in a
  // straight line from x.now (the step's first sample) toward next (the
  // next step's first).
  LLFX_EXACT void voice(Voice &x,const Params &next,uint32_t from,uint32_t to) {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    const uint32_t count=to-from;
    if(!count)return;
    const bool noise=x.type==NOISE_VOICE,envelope=(x.flags&ENVELOPE)!=0;
    if(!x.on || (!envelope && x.now.gain==0 && next.gain==0)) {
      // The noise keeps running (the first voice of its seed has run this span already).
      if(noise){if(x.leader!=&x-voice_)x.noise=voice_[x.leader].noise;else{noiseFor(x,count);work_.idleNoise+=count;}}
      return;
    }
    constexpr float STEP_INV=1.0f/SOUND_STEP;
    Span s;s.count=count;s.mix=mix_+from;
    s.dg=(next.gain-x.now.gain)*STEP_INV;s.dk1=(next.k1-x.now.k1)*STEP_INV;s.dk2=(next.k2-x.now.k2)*STEP_INV;
    s.g=x.now.gain+s.dg*float(from);s.k1=x.now.k1+s.dk1*float(from);s.k2=x.now.k2+s.dk2*float(from);
    if(!noise) {
      const bool pitched=(x.flags&NOTE_PITCH)!=0;
      const uint32_t i0=increment(pitched?clampf(x.note*x.now.hz,0,7999):x.now.hz),i1=increment(pitched?clampf(x.note*next.hz,0,7999):next.hz);
      s.dinc=uint32_t(int32_t(int64_t(i1)-int64_t(i0))/int32_t(SOUND_STEP));s.inc=i0+s.dinc*from;
    }
    s.octave=x.now.octave;s.cubic=x.now.cubic;s.ainv=x.attack?1.0f/float(x.attack):0.0f;
    const uint32_t age0=x.age;
    // One loop per kind of voice, so no sample tests what the voice is.
    using Loop=uint32_t (*)(Voice &,Span &);
    static constexpr Loop LOOPS[16]={&Sound::span<false,0,false>,&Sound::span<false,0,true>,&Sound::span<false,1,false>,&Sound::span<false,1,true>,
      &Sound::span<false,2,false>,&Sound::span<false,2,true>,&Sound::span<false,3,false>,&Sound::span<false,3,true>,
      &Sound::span<true,0,false>,&Sound::span<true,0,true>,&Sound::span<true,1,false>,&Sound::span<true,1,true>,
      &Sound::span<true,2,false>,&Sound::span<true,2,true>,&Sound::span<true,3,false>,&Sound::span<true,3,true>};
    const uint32_t j=LOOPS[(noise?8:0)+x.filter*2+(envelope?1:0)](x,s);
    if(noise && j<count)noiseFor(x,count-j);
    work_.voiceSamples+=j;(noise?work_.noiseSamples:work_.toneSamples)+=j;
    if(x.filter)work_.filterSamples+=j;
    if(!noise && x.wave==WAVE_SINE && (s.octave!=0 || s.cubic!=0))work_.partialSamples+=j;
    if(envelope && age0<x.attack)work_.attackSamples+=std::min(j,x.attack-age0);
  }
  // A span's state: gain, filter coefficients and phase increment with their steps per sample.
  struct Span {float g=0,dg=0,k1=0,dk1=0,k2=0,dk2=0,octave=0,cubic=0,ainv=0;uint32_t inc=0,dinc=0,count=0;float *mix=nullptr;};
  // The samples of a span, for one kind of voice; the number rendered
  // (fewer when its envelope's length ends inside the span).
  template<bool NOISE,uint8_t FILTER,bool ENV> LLFX_EXACT static uint32_t span(Voice &x,Span &s) {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    const auto &SINE=BestEffectSound::detail::SINE;
    const uint8_t wave=x.wave;const uint32_t count=s.count;
    float g=s.g,k1=s.k1,k2=s.k2;uint32_t inc=s.inc,j=0;
    for(;j<count;++j) {
      float v;
      if(NOISE){x.noise^=x.noise<<13;x.noise^=x.noise>>17;x.noise^=x.noise<<5;v=float(int32_t(x.noise))*(1.0f/2147483648.0f);}
      else {
        const uint32_t ph=x.phase;x.phase=ph+inc;inc+=s.dinc;
        if(wave==WAVE_SINE) {
          v=SINE[ph>>22];
          if(s.cubic!=0){const float c=v*v*v;v=v+s.cubic*c;}
          if(s.octave!=0)v=v+s.octave*SINE[(x.phase<<1)>>22];  // from the advanced phase, as Osc::octave() reads it
        }
        else if(wave==WAVE_SQUARE)v=SINE[ph>>22]>0?1.0f:-1.0f;
        else if(wave==WAVE_TRIANGLE)v=float(int32_t(ph<0x80000000u?ph:~ph))*(1.0f/1073741824.0f)-1.0f;
        else v=float(int32_t(ph))*(1.0f/2147483648.0f);
      }
      if(FILTER==LOWPASS){x.y1+=(v-x.y1)*k1;v=x.y1;k1+=s.dk1;}
      else if(FILTER==BANDPASS){x.y1+=(v-x.y1)*k1;x.y2+=(v-x.y2)*k2;v=x.y1-x.y2;k1+=s.dk1;k2+=s.dk2;}
      else if(FILTER==HIGHPASS){x.y1+=(v-x.y1)*k1;v=v-x.y1;k1+=s.dk1;}
      float e=1;
      if(ENV) {
        e=x.level;x.level*=x.k;
        if(x.age<x.attack){const float u=float(x.age)*s.ainv;e=e*(u*u*(3-2*u));}
        ++x.age;
      }
      s.mix[j]+=g*e*v;g+=s.dg;
      if(ENV && x.length && x.age>=x.length){x.on=false;return j+1;}
    }
    return j;
  }
  const Program *p_=nullptr;
  uint32_t index_=0,step_=0,event_=0,filled_=0;Work work_;
  float reg_[REGS];
  Voice voice_[MAX_VOICES];
  float mix_[SOUND_STEP];
};

// Stops a frame between draw calls: a deterministic cap on instructions and
// an optional clock (the device's: time since the frame began).
struct Guard {
  uint32_t maxOps=MAX_FRAME_OPS;
  bool (*expired)(void *context)=nullptr;void *context=nullptr;
};

class Engine {
public:
  using Renderer=BestEffectUI::Renderer;
  explicit Engine(Renderer &renderer):r_(renderer){}
  // touches: steps of the native primitives' own loops outside addw (one per
  // particle, column, curve point or sprite pixel), the renderer's
  // cost-model term for loop steps.
  // Each loop primitive's steps are also kept apart (particles, columns,
  // curve points, sprite pixels), since a step costs differently in each.
  // A frame's counts: the renderer's (plots, probes, scans, touches, roots:
  // Renderer::modelledUs(), as renderUs, which the engine's primitives feed
  // too), the bytecode's (ops, draws) and the primitives' work by kind.
  struct Stats {uint32_t ops=0,draws=0,plots=0,probes=0,scans=0,touches=0,roots=0,particles=0,columns=0,points=0,pixels=0;double renderUs=0;bool stopped=false;};
  // Draws `p` `elapsed` ms in, after the UI, like Renderer::draw. Nothing
  // once elapsed reaches the duration; no readout primitive from the
  // readout end on, so the UI is exact from then.
  bool draw(const BestEffectUI::Surface &s,const Program &p,uint32_t elapsed,uint16_t accent,const Renderer::Anchor &anchor=Renderer::Anchor(),uint16_t background=0,const Guard &guard=Guard()) {
    stats_=Stats();
    Renderer &r=r_;
    r.plots_=r.probes_=r.scans_=r.fills_=r.spans_=r.touches_=r.roots_=0;r.bg_=Renderer::store(background);
    if(!s.pixels || s.width<64 || s.width>Renderer::MAX_W || s.height<Renderer::NUMBER_BOTTOM+Renderer::HALO+1 || s.height>Renderer::MAX_H ||
       elapsed>=p.duration || !p.code)return false;
    r.px_=s.pixels;r.w_=s.width;r.h_=s.height;
    if(++r.stamp_==0){for(auto &row:r.inkStamp_)row.fill(0);for(auto &row:r.blockedStamp_)row.fill(0);r.stamp_=1;}
    r.findNumber(anchor);
    r.accent_=Renderer::rgb(accent);r.bottom_=r.h_-1;
    readout_=elapsed<p.readoutEnd;
    const float t=elapsed*.001f;
    std::memset(reg_,0,sizeof(reg_));
    reg_[T]=t;reg_[DUR]=p.duration*.001f;reg_[W]=float(r.w_);reg_[H]=float(r.h_);
    reg_[AX0]=float(r.number_.x0);reg_[AY0]=float(r.number_.y0);reg_[AX1]=float(r.number_.x1);reg_[AY1]=float(r.number_.y1);
    reg_[ACX]=r.number_.cx();reg_[ACY]=r.number_.cy();reg_[BOTTOM]=float(r.bottom_);
    reg_[ACCENT_R]=r.accent_.r;reg_[ACCENT_G]=r.accent_.g;reg_[ACCENT_B]=r.accent_.b;
    reg_[WHITE_R]=31;reg_[WHITE_G]=63;reg_[WHITE_B]=31;reg_[FRAME]=std::floor(t*20+.001f);
    std::memcpy(reg_+FIRST_CONST,p.consts,std::min(p.constCount,MAX_CONSTS)*sizeof(float));
    guard_=&guard;program_=&p;puffs_=0;
#if defined(LLFX_COUNTS)
    for(unsigned k=0;k<PRIMS;++k)primCalls[k]=primTouches[k]=0;
#endif
    for(unsigned b=0;b<p.blockCount && !stats_.stopped;++b) {
      const Block &k=p.blocks[b];reg_[N]=float(k.count);
      if((k.flags&GUARDED) && reg_[k.flags&0xff]!=0)continue;
      for(unsigned i=0;i<k.count;++i) {
        // Runs without a draw are checked too, every eighth.
        if(!(i&7) && over())break;
        reg_[I]=float(i);
        run(p,k.start,k.start+k.length,guard);
        if(stats_.stopped)break;
      }
    }
    stats_.plots=r.plots_;stats_.probes=r.probes_;stats_.scans=r.scans_;stats_.touches=r.touches_;stats_.roots=r.roots_;stats_.renderUs=r.modelledUs();
    return true;
  }
  const Stats &stats()const{return stats_;}

private:
  Renderer &r_;
  float reg_[REGS];
  Stats stats_;bool readout_=false;const Guard *guard_=nullptr;const Program *program_=nullptr;
  static constexpr unsigned MAX_PUFFS=16;
  struct Puff {float x,y,r,k;};
  Puff puff_[MAX_PUFFS];unsigned puffs_=0;
  // The guard's verdict, sticky for the frame. Checked between draws,
  // every eighth run of a block, and inside the primitives that can take
  // milliseconds (per row or column), never inside a readout primitive.
  bool over() {
    if(stats_.stopped)return true;
    if(stats_.ops>guard_->maxOps || (guard_->expired && guard_->expired(guard_->context)))stats_.stopped=true;
    return stats_.stopped;
  }

  // One instance of a block. False when it ended early (EXIT or the guard).
  bool run(const Program &p,unsigned pc,unsigned end,const Guard &guard) {
    (void)guard;
    return execute(p,reg_,pc,end,stats_.ops,[&](uint8_t prim,const uint8_t *args) {
      if(over())return false;
      this->prim(prim,args);++stats_.draws;return true;
    });
  }

  // Arguments, sanitized: coordinates within a few screens of the canvas,
  // weights and sizes within their primitive's range, colours in range.
  float f(const uint8_t *arg,unsigned i,float lo,float hi)const{const float v=reg_[arg[i]];return v==v?(v<lo?lo:v>hi?hi:v):lo;}
  float xy(const uint8_t *arg,unsigned i)const{return f(arg,i,-1000,1000);}
  static float coord(float v){return v==v?(v<-1000?-1000.0f:v>1000?1000.0f:v):-1000.0f;}
  float k(const uint8_t *arg,unsigned i)const{return f(arg,i,0,8);}
  Renderer::Rgb color(const uint8_t *arg,unsigned i)const{return {f(arg,i,0,31),f(arg,i+1,0,63),f(arg,i+2,0,31)};}
  // One pass: x*0 is 0 for a finite x and NaN otherwise.
  bool finite(const uint8_t *arg,unsigned n)const{float z=0;for(unsigned i=0;i<n;++i)z+=reg_[arg[i]]*0.0f;return z==0;}

private:
  // One loop step of the engine's own: a touch in the renderer's model, and
  // a step for the cost table (primTouches). The renderer's own primitives
  // count their pixels as touches too (spark, since #46), which the table's
  // per-step costs never included, so steps are counted apart.
  void loopStep(uint32_t n=1){r_.touches_+=n;steps_+=n;}
  uint32_t steps_=0;
public:
#if defined(LLFX_PROFILE) || defined(LLFX_COUNTS)
public:
#if defined(LLFX_PROFILE)
  // Profiling builds (host tools, the HIL spike): while `profiling` is set,
  // each primitive's calls and time in us (the host clock, or esp_timer on
  // the device), summed until cleared.
#if defined(ESP_PLATFORM)
  bool profiling=false;  // on the device only the bench's profiling pass times primitives
#else
  bool profiling=true;
#endif
  double profile[PRIMS]={};uint32_t profileCalls[PRIMS]={};
#endif
#if defined(LLFX_COUNTS)
  // Host tools only: the last frame's calls and loop steps (touches) per
  // primitive, for the validator's cost model.
  uint32_t primCalls[PRIMS]={},primTouches[PRIMS]={};
#endif
private:
#if defined(LLFX_PROFILE)
  static double nowUs() {
#if defined(ESP_PLATFORM)
    return double(esp_timer_get_time());
#else
    return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
  }
#endif
  void prim(uint8_t kind,const uint8_t *a) {
#if defined(LLFX_COUNTS)
    const uint32_t steps=steps_;
#endif
#if defined(LLFX_PROFILE)
    const double t0=profiling?nowUs():0;
#endif
    primBody(kind,a);
#if defined(LLFX_PROFILE)
    if(profiling){profile[kind]+=nowUs()-t0;++profileCalls[kind];}
#endif
#if defined(LLFX_COUNTS)
    ++primCalls[kind];primTouches[kind]+=steps_-steps;
#endif
  }
  void primBody(uint8_t kind,const uint8_t *a) {
#else
  void prim(uint8_t kind,const uint8_t *a) {
#endif
    Renderer &r=r_;
    if(!finite(a,PRIM_ARGS[kind]))return;
    if(readoutPrim(kind) && !readout_)return;
    switch(kind) {
      case CLIP:r.bottom_=Math::toInt(std::floor(xy(a,0)+.5f),std::min(r.h_-1,r.number_.y1),r.h_-1);reg_[BOTTOM]=float(r.bottom_);break;
      case PLOT:r.plot(xy(a,0),xy(a,1),color(a,2),k(a,5));break;
      case DOT:r.dot(xy(a,0),xy(a,1),color(a,2),k(a,5));break;
      case VDOT:r.vdot(Math::toInt(xy(a,0),-1000,1000),xy(a,1),Renderer::inkOf(color(a,2)),Renderer::q8(k(a,5)));break;
      case SPARK:r.spark(xy(a,0),xy(a,1),color(a,2),k(a,5),f(a,6,.25f,3.5f));break;
      case TWINKLE:r.twinkle(xy(a,0),xy(a,1),color(a,2),k(a,5),Math::toInt(f(a,6,0,4),0,4));break;
      case STAR:r.star(xy(a,0),xy(a,1),f(a,2,0,2),color(a,3));break;
      case LINE:r.line(xy(a,0),xy(a,1),xy(a,2),xy(a,3),color(a,4),k(a,7));break;
      case FLINE:r.sorcLine(xy(a,0),xy(a,1),xy(a,2),xy(a,3),Renderer::inkOf(color(a,4)),Renderer::q8(k(a,7)),Renderer::q8(k(a,8)));break;
      case SEGMENT:r.segment(xy(a,0),xy(a,1),xy(a,2),xy(a,3),color(a,4),k(a,7));break;
      case ELLIPSE:ellipse(xy(a,0),xy(a,1),f(a,2,0,400),f(a,3,0,400),color(a,4),k(a,7),Math::toInt(f(a,8,4,600),4,600),f(a,9,0,1),f(a,10,0,4));break;
      case GLOW:glow(xy(a,0),xy(a,1),f(a,2,.5f,80),f(a,3,.5f,60),color(a,4),k(a,7));break;
      case BOLT:r.bolt(xy(a,0),xy(a,1),xy(a,2),xy(a,3),Math::toU32(f(a,4,0,16777215)),k(a,5),Math::toInt(f(a,6,1,5),1,5));break;
      case RIMBOLT:r.sorcRimBolt(xy(a,0),xy(a,1),xy(a,2),xy(a,3),Math::toU32(f(a,4,0,16777215)),Renderer::inkOf(color(a,5)),Renderer::inkOf(color(a,8)),
                                 Renderer::q8(k(a,11)),Math::toInt(f(a,12,1,3),1,3));break;
      case FIRE:fire(Math::toInt(xy(a,0),0,r.h_-1),f(a,1,0,1.5f),f(a,2,0,60));break;
      case PRIME:{const int y0=Math::toInt(xy(a,0),-1000,1000);r.sorcPrime(y0,std::min(y0+15,Math::toInt(xy(a,1),-1000,1000)));break;}
      case GLINT:r.glint(reg_[T],f(a,0,-10,10),f(a,1,.05f,5));break;
      case SHIFTROW:r.sorcShiftRow(Math::toInt(xy(a,0),-1000,1000),Math::toInt(std::floor(f(a,1,-8,8)+.5f),-8,8));break;
      case SHADE:r.shadeAnchor(Renderer::inkOf(color(a,0)),Renderer::q8(f(a,3,0,1)),f(a,4,0,1)!=0);break;
      case CUT:r.cut(f(a,0,-6,6),f(a,1,-6,6),Renderer::q8(f(a,2,0,1)),Math::toInt(xy(a,3),-1000,1000));break;
      case HEATLINE:r.sorcHeatLine(xy(a,0),xy(a,1),xy(a,2),xy(a,3),Renderer::inkOf(color(a,4)),Renderer::q8(f(a,7,0,1)));break;
      case BLADE:blade(xy(a,0),xy(a,1),xy(a,2),xy(a,3),f(a,4,-4,4),color(a,5),f(a,8,0,1));break;
      case EMIT:emit(a);break;
      case STROKE:stroke(a);break;
      case TRAIL:trail(a);break;
      case STAMP:stamp(a);break;
      case AURA:aura(a);break;
      case DISC:disc(a);break;
      case RUNES:runes(a);break;
      case PUFF:if(puffs_<MAX_PUFFS){Puff &q=puff_[puffs_++];q.x=xy(a,0);q.y=xy(a,1);q.r=f(a,2,0,60);q.k=f(a,3,0,1);}break;
      case CLOUD:cloud(a);break;
      case ARC:arc(a);break;
      case HEATRING:heatRing(a);break;
      case VESSEL:vessel(a);break;
      case RAYS:rays(a);break;
      default:break;
    }
  }
  // An ellipse of `points` plots, squashed in y and brighter toward its lower side by `tilt`;
  // points whose centre lies off the canvas or below the clip are skipped before a probe.
  void ellipse(float cx,float cy,float rx,float ry,const Renderer::Rgb &c,float k,int points,float tilt,float squash) {
    Renderer &r=r_;
    const float step=2*Renderer::HALF_TURN/points;const Renderer::Ink ink=Renderer::inkOf(c);const int kq=Renderer::q8(k);
    for(int i=0;i<points;++i) {
      if(!(i&63) && i && over())return;
      loopStep();++stats_.points;
      const float an=i*step;const float sa=Math::sine(an);
      const float x=cx+rx*Math::sine(an+Renderer::HALF_TURN/2),y=cy+ry*sa*squash;
      if(x<0 || x>=r.w_ || y<0 || y>r.bottom_)continue;
      r.addw(Renderer::roundi(x),Renderer::roundi(y),ink,tilt>0?Renderer::q8(k*((1-tilt)+tilt*sa)):kq);
    }
  }
  // ---- loop primitives -------------------------------------------------
  void style(int kind,float x,float y,const Renderer::Rgb &c,float k) {
    Renderer &r=r_;
    switch(kind) {
      case 1:r.dot(x,y,c,k);break;
      case 2:r.twinkle(x,y,c,k,1);break;
      case 3:r.spark(x,y,c,k,1.5f);break;
      case 4:r.star(x,y,std::min(k,2.0f),c);break;
      default:r.plot(x,y,c,k);break;
    }
  }
  // Particles: particle i is born at birth0+birthSpan*r, lives life0+lifeSpan*r,
  // starts in a box of spreadX x spreadY around (x0, y0), leaves at dir+-dirSpread
  // with speed0+speedSpan*r, slowed by linear drag and pulled by gravity
  // (closed form), swaying sideways; its colour runs from Cbirth to Cdeath
  // and its weight fades in and out over the given fractions of its life,
  // flickering by `flicker`; style 0 plot, 1 dot, 2 twinkle, 3 spark, 4 star
  // (its weight its size). Shape 1 starts each particle on the ellipse of
  // radii spreadX, spreadY at its own direction angle (a radial burst).
  // Each r is the particle's own seeded number.
  void emit(const uint8_t *a) {
    const int n=Math::toInt(f(a,0,0,256),0,256);const uint32_t salt=Math::toU32(f(a,1,0,16777215));
    const float birth0=f(a,2,-10,10),birthSpan=f(a,3,0,10),life0=f(a,4,.01f,10),lifeSpan=f(a,5,0,10);
    const float x0=xy(a,6),y0=xy(a,7),spreadX=f(a,8,0,400),spreadY=f(a,9,0,400);
    const float dir=f(a,10,-100,100),dirSpread=f(a,11,0,10),speed0=f(a,12,-1000,1000),speedSpan=f(a,13,-1000,1000);
    const float drag=f(a,14,0,50),gravity=f(a,15,-2000,2000),swayAmp=f(a,16,-100,100),swayFreq=f(a,17,-100,100);
    const Renderer::Rgb from=color(a,18),to=color(a,21);
    const float k=this->k(a,24),fadeIn=f(a,25,0,1),fadeOut=f(a,26,0,1),flicker=f(a,27,0,1);
    const int kind=Math::toInt(f(a,28,0,4),0,4);const float trail=f(a,29,0,1),time=f(a,30,-10,20);const bool ring=f(a,31,0,1)!=0;
    // A position computed from wild arguments (a drag near zero divides by
    // it) is held within a few screens, as every argument is.
    auto position=[&](float age,float ca,float sa,float v,float ox,float oy,float phase,float &x,float &y) {
      const float travel=drag>0?(1-Math::exp(-drag*age))/drag:age;
      const float fall=drag>0?gravity*(age-travel)/drag:.5f*gravity*age*age;
      x=coord(ox+ca*v*travel+swayAmp*Math::sine(swayFreq*age+phase));y=coord(oy+sa*v*travel+fall);
    };
    for(int i=0;i<n;++i) {
      if(!(i&31) && i && over())return;
      loopStep();++stats_.particles;
      const uint32_t id=uint32_t(i);
      const float birth=birth0+birthSpan*Renderer::rnd(id,salt),life=life0+lifeSpan*Renderer::rnd(id,salt+1);
      const float age=time-birth;
      if(age<0 || age>=life)continue;
      const float u=age/life;
      const float an=dir+dirSpread*(2*Renderer::rnd(id,salt+4)-1),v=speed0+speedSpan*Renderer::rnd(id,salt+5);
      const float phase=6.2831853f*Renderer::rnd(id,salt+6),ca=Math::sine(an+1.5707963f),sa=Math::sine(an);
      const float ox=ring?x0+spreadX*ca:x0+spreadX*(2*Renderer::rnd(id,salt+2)-1);
      const float oy=ring?y0+spreadY*sa:y0+spreadY*(2*Renderer::rnd(id,salt+3)-1);
      float x,y;position(age,ca,sa,v,ox,oy,phase,x,y);
      float w=k;
      if(fadeIn>0 && u<fadeIn)w*=u/fadeIn;
      if(fadeOut>0 && 1-u<fadeOut)w*=(1-u)/fadeOut;
      if(flicker>0)w*=1-flicker*(.5f+.5f*Math::sine(age*23+phase));
      const Renderer::Rgb c=Renderer::mixed(from,to,u);
      style(kind,x,y,c,w);
      if(trail>0){float tx,ty;position(std::max(0.0f,age-trail),ca,sa,v,ox,oy,phase,tx,ty);r_.plot(tx,ty,c,w*.45f);}
    }
  }
  // One point per column from x0 to x1, at y0 + slope*(x-x0), plus three
  // sine waves ai*sin(fi*x+pi), a bump bumpH/(1+((x-bumpX)/bumpW)^2), rings
  // ringA*sin(ringF*|x-bumpX|+ringP) spreading from it, and value noise
  // (noiseAmp*(2n-1), n in 0..1 along x at noiseScale cells a pixel); a gate
  // above 0 draws only columns whose noise is above it. Colour and weight run
  // from the start values to the end ones, the weight scaled by
  // 1+shimmer*sin(shimmerFreq*x+shimmerPhase). Width 1 and 2 add rows above
  // and below at 0.6 and 0.25 of the weight; smooth splits each point over
  // two rows; bodyRows rows from 2 below the line fade from bodyK to 0 in Cbody.
  void stroke(const uint8_t *a) {
    Renderer &r=r_;
    const int x0=Math::toInt(Math::floor(xy(a,0)),-1000,1000),x1=Math::toInt(Math::floor(xy(a,1)),-1000,1000);
    const float y0=xy(a,2),slope=f(a,3,-8,8);
    float wa[3],wf[3],wp[3];for(int j=0;j<3;++j){wa[j]=f(a,4+3*j,-200,200);wf[j]=f(a,5+3*j,-10,10);wp[j]=f(a,6+3*j,-1000,1000);}
    const float bumpX=xy(a,13),bumpH=f(a,14,-200,200),bumpW=f(a,15,.5f,400),ringA=f(a,16,-200,200),ringF=f(a,17,-10,10),ringP=f(a,18,-1000,1000);
    const float nA=f(a,19,-200,200),nS=f(a,20,0,16),nP=f(a,21,-1000,1000);const uint32_t nSalt=Math::toU32(f(a,22,0,16777215));const float gate=f(a,23,0,1);
    const Renderer::Rgb from=color(a,24),to=color(a,27);
    const float k0=f(a,30,0,8),k1=f(a,31,0,8),sh=f(a,32,-1,1),shF=f(a,33,-10,10),shP=f(a,34,-1000,1000);
    const int width=Math::toInt(f(a,35,0,2),0,2);const bool smooth=f(a,36,0,1)!=0;
    const Renderer::Ink body=Renderer::inkOf(color(a,37));const int bodyK=Renderer::q8(f(a,40,0,2)),bodyRows=Math::toInt(f(a,41,0,8),0,8);
    const int first=std::max(0,std::min(x0,x1)),last=std::min(r.w_-1,std::max(x0,x1));
    const float span=x1!=x0?float(x1-x0):1.0f,invW=1/bumpW;
    const bool noisy=nA!=0 || gate>0;const int noiseY=Math::toInt(nP*256,-(1<<24),1<<24);
    // Each wave's phase is stepped by rotation from column to column.
    float wc[3],ws[3],stc[3],sts[3];
    for(int j=0;j<3;++j){wc[j]=Math::sine(wf[j]*first+wp[j]+1.5707963f);ws[j]=Math::sine(wf[j]*first+wp[j]);stc[j]=Math::sine(wf[j]+1.5707963f);sts[j]=Math::sine(wf[j]);}
    const Renderer::Ink inkFrom=Renderer::inkOf(from),inkTo=Renderer::inkOf(to);
    for(int x=first;x<=last;++x) {
      if(!((x-first)&31) && x!=first && over())return;
      loopStep(4);++stats_.columns;
      const float u=(x-x0)/span;
      float y=y0+slope*(x-x0);
      for(int j=0;j<3;++j)if(wa[j]!=0){y+=wa[j]*ws[j];const float nc=wc[j]*stc[j]-ws[j]*sts[j];ws[j]=ws[j]*stc[j]+wc[j]*sts[j];wc[j]=nc;}
      if(bumpH!=0 || ringA!=0) {
        const float dx=std::fabs(x-bumpX),e=dx*invW;
        y+=bumpH/(1+e*e)+(ringA!=0?ringA*Math::sine(ringF*dx+ringP):0.0f);
      }
      float w=k0+(k1-k0)*u;
      if(noisy) {
        loopStep(2);
        const float nv=Math::noise(Math::toInt(x*nS*256,-(1<<24),1<<24),noiseY,nSalt)*(1/255.0f);
        if(gate>0 && nv<=gate)continue;
        y+=nA*(2*nv-1);
      }
      if(sh!=0)w*=1+sh*Math::sine(shF*x+shP);
      if(w<=0)continue;
      const Renderer::Ink ink=Renderer::mixedInk(inkFrom,inkTo,int(Math::clamp01(u)*256));
      const int kq=Renderer::q8(w),yi=Renderer::floori(y);
      if(smooth)r.vdot(x,y,ink,kq);else r.addw(x,Renderer::roundi(y),ink,kq);
      if(width>=1){const int yr=Renderer::roundi(y),k1q=(kq*154)>>8;r.addw(x,yr-1,ink,k1q);r.addw(x,yr+1,ink,k1q);
        if(width>=2){const int k2q=kq>>2;r.addw(x,yr-2,ink,k2q);r.addw(x,yr+2,ink,k2q);}}
      for(int j=0;j<bodyRows;++j)r.addw(x,yi+2+j,body,(bodyK*(bodyRows-j))/bodyRows);
    }
  }
  // n points along an ellipse arc from the head angle back by `sweep`, the
  // ellipse turned by `rot`: the radius scales from 1 at the head to
  // tailScale at the tail, colour runs from head to tail, weight is
  // k*((1-tilt)+tilt*sin(angle))*(1-u)^falloff*(1-(1-tailK)*u) (u = 0 at the
  // head, falloff a whole number 0-4); `edge` adds every other point a
  // second plot 0.9 px outward at that fraction of the weight; `width` steps
  // a body of Cbody inward from each point, int(width*6.75*v*v*(1-v)) pixels
  // deep at v = 1-u (a crescent, thickest past the middle; a negative width
  // is a band of -width pixels all the way round). Points over the
  // readout's box and halo are skipped before they cost a probe.
  void trail(const uint8_t *a) {
    Renderer &r=r_;
    const float cx=xy(a,0),cy=xy(a,1),rx=f(a,2,0,400),ry=f(a,3,0,400),head=f(a,4,-1000,1000),sweep=f(a,5,-50,50);
    const int n=Math::toInt(f(a,6,1,256),1,256);
    const Renderer::Ink from=Renderer::inkOf(color(a,7)),to=Renderer::inkOf(color(a,10));
    const float k=this->k(a,13),tilt=f(a,14,0,1),tailScale=f(a,15,0,4),edge=f(a,17,0,2);const int falloff=Math::toInt(f(a,16,0,4)+.5f,0,4);
    const float rot=f(a,18,-100,100),width=f(a,19,-8,8),tailK=f(a,23,0,1);const Renderer::Ink body=Renderer::inkOf(color(a,20));
    const float cr=Math::sine(rot+1.5707963f),sr=Math::sine(rot),inv=rx+ry>0?2/(rx+ry):0.0f;
    const float bx0=r.number_.x0-Renderer::HALO-1,bx1=r.number_.x1+Renderer::HALO+1,by0=r.number_.y0-Renderer::HALO-1,by1=r.number_.y1+Renderer::HALO+1;
    // The unit vector is stepped by rotation from the tail to the head, as
    // the built-in crescents do: no table look-up per point.
    const float step=n>1?sweep/(n-1):0.0f,cs=Math::sine(step+1.5707963f),ss=Math::sine(step);
    float ca=Math::sine(head-sweep+1.5707963f),sa=Math::sine(head-sweep);
    const int band=width<0?Math::toInt(-width,0,8):0;
    for(int i=n-1;i>=0;--i) {
      if(!(i&31) && i!=n-1 && over())return;
      loopStep(2);++stats_.points;
      const float c0=ca,s0=sa;
      {const float nc=ca*cs-sa*ss;sa=sa*cs+ca*ss;ca=nc;}
      const float u=n>1?float(i)/(n-1):0.0f,sc=1+(tailScale-1)*u;
      const float lx=rx*sc*c0,ly=ry*sc*s0,ex=lx*cr-ly*sr,ey=lx*sr+ly*cr,x=cx+ex,y=cy+ey;
      if(x>bx0 && x<bx1 && y>by0 && y<by1)continue;
      float w=k*((1-tilt)+tilt*s0)*(1-(1-tailK)*u);
      const float rest=1-u;
      for(int e=0;e<falloff;++e)w*=rest;
      const Renderer::Ink ink=Renderer::mixedInk(from,to,int(u*256));const int kq=Renderer::q8(w);
      r.addw(Renderer::roundi(x),Renderer::roundi(y),ink,kq);
      if(edge>0 && !(i&1))r.addw(Renderer::roundi(x+c0*.9f),Renderer::roundi(y+s0*.9f),ink,Renderer::q8(w*edge));
      const int layers=band?band:(width>0?int(width*6.75f*rest*rest*u):0);
      if(layers>0) {
        int x8=Renderer::q8(x)+128,y8=Renderer::q8(y)+128;const int sx=Renderer::q8(ex*inv),sy=Renderer::q8(ey*inv);
        loopStep(unsigned(layers));
        for(int d=1;d<=layers;++d){x8-=sx;y8-=sy;r.addw(x8>>8,y8>>8,body,kq*(layers+1-d)/(layers+1));}
      }
    }
  }
  // A sprite's pixels as light, each class in its colour, scaled 1-3 times,
  // flipped by bit 0 (x) and bit 1 (y).
  void stamp(const uint8_t *a) {
    Renderer &r=r_;const Program &p=*program_;
    const int id=Math::toInt(f(a,0,0,MAX_SPRITES-1),0,MAX_SPRITES-1);
    if(unsigned(id)>=p.spriteCount)return;
    const Program::Sprite &sp=p.sprites[id];
    const int x0=Renderer::roundi(xy(a,1)),y0=Renderer::roundi(xy(a,2)),scale=Math::toInt(f(a,3,1,3),1,3),flip=Math::toInt(f(a,4,0,3),0,3);
    Renderer::Ink ink[4]={{},Renderer::inkOf(color(a,5)),Renderer::inkOf(color(a,8)),Renderer::inkOf(color(a,11))};
    const int kq[4]={0,Renderer::q8(this->k(a,14)),Renderer::q8(this->k(a,15)),Renderer::q8(this->k(a,16))};
    for(int py=0;py<sp.h;++py) {
      if(over())return;
      for(int px=0;px<sp.w;++px) {
        loopStep();++stats_.pixels;
        const unsigned at=unsigned(py*sp.w+px),cls=(sp.bits[at>>2]>>((at&3)*2))&3u;
        if(!cls)continue;
        const int sx=flip&1?sp.w-1-px:px,sy=flip&2?sp.h-1-py:py;
        for(int dy=0;dy<scale;++dy)for(int dx=0;dx<scale;++dx)r.addw(x0+sx*scale+dx,y0+sy*scale+dy,ink[cls],kq[cls]);
      }
    }
  }

  // Glowing shells round the readout's silhouette (Overdrive's aura): shell
  // s is the ring of pixels one 3x3 dilation outside shell s-1, the first
  // just outside the UI halo, over the readout's rows and 5 more each way.
  // Shell 0 is Cinner, the others Couter, at weights 1, .9, .6 of k; a band
  // of light climbs through them with `phase` (a triangle wave per column);
  // `climb` grows the outer shells only from below and sideways, `dither`
  // lights every other pixel of the outer shells.
  void aura(const uint8_t *a) {
    Renderer &r=r_;
    constexpr int ROWS=48;constexpr unsigned WORDS=Renderer::WORDS;
    const int shells=Math::toInt(f(a,0,0,3),0,3);
    const Renderer::Ink inner=Renderer::inkOf(color(a,1)),outer=Renderer::inkOf(color(a,4));
    const int kq=Renderer::q8(f(a,7,0,1.5f)),phase=Math::toInt(f(a,8,-1e6f,1e6f),-1000000,1000000);
    const bool climb=f(a,9,0,1)!=0,dither=f(a,10,0,1)!=0;
    if(!shells || kq<5)return;
    const Renderer::Box &b=r.number_;
    const int words=(r.w_+31)/32,y0=std::max(0,b.y0-5),y1=std::min({r.bottom_,b.y1+5,y0+ROWS-3}),n=y1-y0+3;
    if(n<3)return;
    uint32_t d[ROWS][WORDS],above[WORDS],was[WORDS];
    const uint32_t lastMask=(r.w_&31)?(1u<<(r.w_&31))-1:~0u;
    for(int row=0;row<n;++row) {
      const int y=y0-1+row;
      for(int i=0;i<words;++i) {
        if(y<0 || y>r.bottom_ || y<b.y0-3 || y>b.y1+3){d[row][i]=0;continue;}
        if(r.blockedStamp_[y][i]!=r.stamp_)r.buildBlocked(y,i);
        d[row][i]=r.blocked_[y][i];
      }
    }
    auto spread=[&](const uint32_t *row,int i){
      const uint32_t c=row[i],l=i>0?row[i-1]>>31:0,rr=i+1<words?row[i+1]<<31:0;
      return c|c<<1|l|c>>1|rr;
    };
    static constexpr int LEVEL[3]={256,230,150};
    for(int sh=0;sh<shells;++sh) {
      if(over())return;
      const int level=(kq*LEVEL[sh])>>8;const Renderer::Ink &ink=sh?outer:inner;
      std::memcpy(above,d[0],sizeof(above));
      for(int row=1;row<n-1;++row) {
        const int y=y0-1+row;
        std::memcpy(was,d[row],sizeof(was));
        uint32_t ring[WORDS];
        for(int i=0;i<words;++i) {
          uint32_t g=spread(was,i)|(row+1<n?spread(d[row+1],i):0);if(!climb || sh==0)g|=spread(above,i);
          ring[i]=g&~was[i]&(i==words-1?lastMask:~0u);
          d[row][i]|=ring[i];
        }
        std::memcpy(above,was,sizeof(above));
        for(int i=0;i<words;++i)for(uint32_t bits=ring[i];bits;bits&=bits-1) {
          const int x=i*32+__builtin_ctz(bits);
          loopStep();++stats_.pixels;
          if(sh && dither && ((x+y)&1))continue;
          const int ph=(y*26+(Renderer::lattice(x>>1,uint32_t(sh),61)>>2)+phase)&255,tri=ph<128?ph:255-ph;
          r.addw(x,y,ink,(level*(70+tri))>>8);
        }
      }
    }
  }
  // A filled ellipse: rows from the top (rows above `top` skipped), each
  // pixel lit where the 4x4 ordered dither passes `dither`/16 (16 lights
  // every pixel, 0 none), weighted by k and, with `noise`, by value noise (heat);
  // then a rim of Crim brighter on top, at rimK.
  void disc(const uint8_t *a) {
    Renderer &r=r_;
    static constexpr uint8_t BAYER[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    const float cx=xy(a,0),cy=xy(a,1),rx=f(a,2,.5f,80),ry=f(a,3,.5f,80);
    const Renderer::Ink fill=Renderer::inkOf(color(a,4));const int kq=Renderer::q8(this->k(a,7));
    const int density=Math::toInt(f(a,8,0,16),0,16),top=Math::toInt(xy(a,9),-1000,1000);
    const Renderer::Ink rim=Renderer::inkOf(color(a,10));const int rimq=Renderer::q8(f(a,13,0,8));
    const int nq=Renderer::q8(f(a,14,0,1));const float nS=f(a,15,0,16);const int nP=Math::toInt(f(a,16,-1e6f,1e6f)*256,-(1<<24),1<<24);
    if(kq>=5 && density)for(int y=std::max({0,top,int(cy-ry)});y<=std::min(r.bottom_,int(cy+ry)+1);++y) {
      if(over())return;
      const float dy=(y-cy)/ry,rest=1-dy*dy;if(rest<=0)continue;
      const float span=rx*r.root(rest);
      const int xa=std::max(0,int(cx-span)+1),xb=std::min(r.w_-1,int(cx+span));
      // Only the lit columns of this dither row: phase p every 4 px.
      for(int p=0;p<4;++p) {
        if(BAYER[(y&3)*4+p]>=density)continue;
        for(int x=xa+((p-xa)&3);x<=xb;x+=4) {
          loopStep();++stats_.pixels;
          int w=kq;
          if(nq)w=(w*(256-nq+(nq*Math::noise(Math::toInt(x*nS*256,-(1<<24),1<<24),Math::toInt(y*nS*256,-(1<<24),1<<24)+nP,7))/255))>>8;
          r.addw(x,y,fill,w);
        }
      }
    }
    if(rimq<5)return;
    const int n=std::max(12,int((rx+ry)*3.3f));
    const float step=2*Renderer::HALF_TURN/n,cs=Math::sine(step+1.5707963f),ss=Math::sine(step);
    float ca=1,sa=0;
    for(int i=0;i<n;++i) {
      const float c0=ca,s0=sa;{const float nc=ca*cs-sa*ss;sa=sa*cs+ca*ss;ca=nc;}
      const float y=cy+ry*s0;if(y<top)continue;
      loopStep();++stats_.points;
      const int w=(rimq*(205-int(50*s0)))>>8;if(w<5)continue;
      r.addw(Renderer::roundi(cx+rx*c0),Renderer::roundi(y),rim,w);
    }
  }

  // Summon's script: glyph g at (x+g*dx, y+g*dy), its kind the hash of
  // salt+g: a tick (two pixels up, the top at half), a cross (arms at
  // half), a hook (one pixel up and right) or a dot.
  void runes(const uint8_t *a) {
    Renderer &r=r_;
    const float x0=xy(a,0),y0=xy(a,1),dx=f(a,2,-40,40),dy=f(a,3,-40,40);
    const int n=Math::toInt(f(a,4,0,128),0,128);const Renderer::Ink c=Renderer::inkOf(color(a,5));const int kq=Renderer::q8(k(a,8));
    const uint32_t salt=Math::toU32(f(a,9,0,16777215));
    if(kq<5)return;
    for(int g=0;g<n;++g) {
      if(!(g&15) && g && over())return;
      loopStep();++stats_.points;
      const int x=Renderer::roundi(x0+g*dx),y=Renderer::roundi(y0+g*dy),kind=int(Renderer::hash((salt+uint32_t(g))*2654435761u)&3);
      r.addw(x,y,c,kq);
      if(kind==0){r.addw(x,y-1,c,kq);r.addw(x,y-2,c,kq>>1);}
      else if(kind==1){r.addw(x-1,y,c,kq>>1);r.addw(x+1,y,c,kq>>1);r.addw(x,y-1,c,kq>>1);}
      else if(kind==2)r.addw(x+1,y-1,c,kq);
    }
  }

  // Boundless's funnel rings: the lower half of the ellipse (the upper with
  // a negative ry), one point per column (split between the two pixels round it with `split`, for slow
  // smooth motion), a line where a column's step is steeper than 1.5 px;
  // nothing below the screen's foot.
  void arc(const uint8_t *a) {
    Renderer &r=r_;
    const float cx=xy(a,0),cy=xy(a,1),rx=f(a,2,.5f,1000),ry=f(a,3,-1000,1000);
    const Renderer::Ink c=Renderer::inkOf(color(a,4));const int kq=Renderer::q8(k(a,7));const bool split=f(a,8,0,1)!=0;
    if(kq<5)return;
    const int x0=std::max(0,int(std::ceil(cx-rx))),x1=std::min(r.w_-1,int(cx+rx));
    if(x0>x1 || cy+ry*.2f>r.bottom_+1)return;
    const float inv=1/rx;float py=-1;
    for(int x=x0;x<=x1;++x) {
      if(!(x&31) && over())return;
      loopStep();++stats_.columns;
      const float u=(x-cx)*inv,y=cy+ry*r.root(1-u*u);
      if(y>r.bottom_+1){py=-1;continue;}
      if(py>=0 && std::fabs(y-py)>1.5f)r.sorcLine(float(x-1),py,float(x),y,c,kq,kq);
      else if(split)r.vdot(x,y,c,kq);else r.addw(x,Renderer::roundi(y),c,kq);
      py=y;
    }
  }

  // Waterleaf's glass of water: rows from the surface down to the base
  // between walls running from topW to bottomW half widths (top to bottom);
  // the light gathers toward the walls (outer quarter of each half the
  // edge colour), under a lit band `band` rows deep below the surface,
  // deeper and dimmer toward the base, each row shimmering by value noise
  // at `phase`.
  void vessel(const uint8_t *a) {
    Renderer &r=r_;
    const float cx=xy(a,0),gt=xy(a,1),gb=xy(a,2),topW=f(a,3,0,100),botW=f(a,4,0,100),ys=xy(a,5);
    const Renderer::Rgb water=color(a,6),deep=color(a,9);
    const float k=f(a,12,0,1),band=f(a,13,.1f,100);const int phase=Math::toInt(f(a,14,-1e6f,1e6f),-1000000,1000000);
    if(k<=.01f || gb<=gt || gb<=ys)return;
    const float slope=(botW-topW)/(gb-gt),invDepth=1/(gb-ys);
    for(int y=std::max(0,Renderer::roundi(ys+1));y<=std::min(r.bottom_,Renderer::roundi(gb-1));++y) {
      if(over())return;
      const float d=(y-ys)*invDepth,half=topW+slope*(y-gt);
      const int l=Renderer::roundi(cx-half)+1,rr=Renderer::roundi(cx+half)-1;if(rr<l)continue;
      const int band8=std::max(0,Renderer::q8(1-(y-ys)/band)),inv16=65536/(rr-l+1),depth8=Renderer::q8(1-.35f*d);
      const Renderer::Ink c=Renderer::inkOf(Renderer::mixed(water,deep,d)),edge=Renderer::inkOf(Renderer::mixed(water,deep,.4f+.6f*d));
      const int sh=Math::noise(y*90,phase,581),kRow=Renderer::q8(k*(.85f+.15f*(sh-128)*(1/128.0f)));
      for(int x=std::max(0,l);x<=std::min(r.w_-1,rr);++x) {
        loopStep();++stats_.pixels;
        const int e8=(std::abs(2*x-l-rr)*inv16)>>8,g8=std::max(band8,e8<128?0:(e8-128)*2);
        if(g8<=5)continue;
        r.addw(x,y,e8>179?edge:c,(kRow*((((g8*g8)>>8)*depth8)>>8))>>8);
      }
    }
  }

  // Waterleaf's burning hands: ray j of n leaves the circle of radius r at
  // angle 2*pi*j/n along (cos, sin - lift) normalised, its length
  // (base + vary*noise/255)*(low + (high - low)*up)*scale, up = (1 - sin)/2
  // (longest at the top) and noise the value noise at (j*190 + offset,
  // phase); a line fading from k0 to k1. Rays under a pixel are skipped.
  void rays(const uint8_t *a) {
    Renderer &r=r_;
    const float cx=xy(a,0),cy=xy(a,1),rad=f(a,2,0,200);const int n=Math::toInt(f(a,3,0,64),0,64);
    const float base=f(a,4,0,100),vary=f(a,5,0,100),scale=f(a,6,0,10),low=f(a,7,0,10),high=f(a,8,0,10),lift=f(a,9,-10,10);
    const Renderer::Ink c=Renderer::inkOf(color(a,10));const int k0=Renderer::q8(k(a,13)),k1=Renderer::q8(k(a,14));
    const int phase=Math::toInt(f(a,15,-1e6f,1e6f),-1000000,1000000);const uint32_t salt=Math::toU32(f(a,16,0,16777215));
    const int offset=Math::toInt(f(a,17,-1e6f,1e6f),-1000000,1000000);
    if(!n || (k0<5 && k1<5))return;
    const float step=2*Renderer::HALF_TURN/n;
    for(int j=0;j<n;++j) {
      if(over())return;
      loopStep();++stats_.points;
      const float an=j*step,ca=Math::sine(an+1.5707963f),sa=Math::sine(an),up=(1-sa)*.5f;
      const float len=(base+vary*Math::noise(j*190+offset,phase,salt)*(1/255.0f))*(low+(high-low)*up)*scale;if(len<1)continue;
      const float dx=ca,dy=sa-lift,inv=1/r.root(dx*dx+dy*dy),x0=cx+rad*ca,y0=cy+rad*sa;
      r.sorcLine(x0,y0,x0+dx*inv*len,y0+dy*inv*len,c,k0,k1);
    }
  }

  // Sunblade's wave crossing the header: the readout's glyph pixels whose
  // squared distance from (round cx, round cy) lies in [int r0^2, int r1^2]
  // are pulled toward C (brighten only, as heatline), each row only over
  // the columns of its two spans.
  void heatRing(const uint8_t *a) {
    Renderer &r=r_;const Renderer::Box &b=r.number_;
    const int cx=Renderer::roundi(xy(a,0)),cy=Renderer::roundi(xy(a,1));
    const float r0=f(a,2,0,1000),r1=f(a,3,0,1000);
    const Renderer::Ink c=Renderer::inkOf(color(a,4));const int k8=Renderer::q8(f(a,7,0,1));
    if(k8<=0 || r1<r0)return;
    const int lo=int(r0*r0),hi=int(r1*r1);
    for(int y=b.y0;y<=b.y1;++y) {
      const int dy2=(y-cy)*(y-cy);if(dy2>hi)continue;
      // |dx| from the smallest with dx^2 >= lo-dy2 to the largest with dx^2 <= hi-dy2.
      int inner=lo>dy2?int(r.root(float(lo-dy2))):0;while(inner*inner+dy2<lo)++inner;while(inner>0 && (inner-1)*(inner-1)+dy2>=lo)--inner;
      int outer=int(r.root(float(hi-dy2)));while((outer+1)*(outer+1)+dy2<=hi)++outer;while(outer>0 && outer*outer+dy2>hi)--outer;
      if(inner>outer)continue;
      // Left span cx-outer..cx-inner, right span from cx+inner (cx+1 when
      // the left one already holds cx) to cx+outer.
      const int spans[2][2]={{cx-outer,cx-inner},{cx+std::max(inner,1),cx+outer}};
      for(const auto &span:spans)
        for(int x=std::max(span[0],b.x0);x<=std::min(span[1],b.x1);++x){loopStep();++stats_.pixels;r.sorcHeat(x,y,c,k8);}
    }
  }

  // Summon's smoke: the puffs as one cloud. Body: an ordered dither of
  // density*k sixteenths at weight bodyK*k, by screen position, each pixel
  // claimed by the first puff that covers it, from the row below `top`
  // down, a mask word at a time. Rim: a circle of Crim brighter on top,
  // skipped where another puff covers it, so the cloud has one outline.
  void cloud(const uint8_t *a) {
    Renderer &r=r_;
    static constexpr uint8_t BAYER[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    constexpr unsigned WORDS=Renderer::WORDS;
    const Renderer::Ink body=Renderer::inkOf(color(a,0)),rim=Renderer::inkOf(color(a,3));
    const float bodyK=f(a,6,0,1),density=f(a,7,0,16);const int top=Math::toInt(xy(a,8),-1000,1000);
    const unsigned n=puffs_;puffs_=0;
    int ya=r.bottom_+1,yb=-1;
    BestEffectUI::ninja::Step steps[MAX_PUFFS];uint32_t dither[MAX_PUFFS][4]={};
    for(unsigned i=0;i<n;++i) {
      const Puff &q=puff_[i];if(q.k<=.02f)continue;
      ya=std::min(ya,int(q.y-q.r));yb=std::max(yb,int(q.y+q.r)+1);
      const int dens=int(density*q.k+.5f),kb=Renderer::q8(bodyK*q.k);
      if(kb>=5)for(int v=0;v<4;++v)for(int x=0;x<4;++x)if(BAYER[v*4+x]<dens)dither[i][v]|=0x11111111u<<x;
      steps[i]=BestEffectUI::ninja::step(body,kb);
    }
    for(int y=std::max({ya,top,0});y<=std::min(yb,r.bottom_);++y) {
      if(over())return;
      uint32_t claimed[WORDS]={},lit[MAX_PUFFS][WORDS];
      for(unsigned i=0;i<n;++i) {
        for(auto &word:lit[i])word=0;
        const Puff &q=puff_[i];if(q.k<=.02f)continue;
        const float dy=y-q.y;if(dy*dy>=q.r*q.r)continue;
        loopStep();++stats_.columns;
        const float span=r.root(q.r*q.r-dy*dy);
        const int xa=std::max(0,int(q.x-span)+1),xb=std::min(r.w_-1,int(q.x+span));
        if(xa>xb)continue;
        const uint32_t pattern=dither[i][y&3];
        for(int w=xa>>5;w<=xb>>5;++w) {
          const int lo=std::max(xa,w*32)-w*32,hi=std::min(xb,w*32+31)-w*32;
          const uint32_t covers=(~0u>>(31-(hi-lo)))<<lo;
          lit[i][w]=covers&~claimed[w]&pattern;claimed[w]|=covers;
        }
      }
      for(int w=0;w*32<r.w_;++w) {
        uint32_t all=0;for(unsigned i=0;i<n;++i)all|=lit[i][w];
        if(!all)continue;
        r.probes_+=__builtin_popcount(all);
        if(r.blockedStamp_[y][w]!=r.stamp_)r.buildBlocked(y,w);
        uint32_t free=~r.blocked_[y][w];if((w+1)*32>r.w_)free&=~(~0u<<(r.w_-w*32));
        if(!(all&free))continue;
        if((all&free&3u) && w>0)r.ink(y,w-1);
        if(((all&free)>>30) && (w+1)*32<r.w_)r.ink(y,w+1);
        uint16_t *row=r.px_+y*r.w_+w*32;
        for(unsigned i=0;i<n;++i)if(lit[i][w]&free)r.plots_+=BestEffectUI::ninja::addBits(row,lit[i][w]&free,steps[i]);
      }
    }
    for(unsigned i=0;i<n;++i) {
      if(over())return;
      const Puff &q=puff_[i];if(q.k<=.02f)continue;
      const float rr=q.r;const int kq=Renderer::q8(q.k);
      if(((kq*255)>>8)<5)continue;
      float ux[MAX_PUFFS],uy[MAX_PUFFS],cut[MAX_PUFFS];int nearN=0;
      for(unsigned j=0;j<n;++j) {
        const Puff &o=puff_[j];if(j==i || o.k<=.02f)continue;
        const float rj=o.r-.6f,ex=o.x-q.x,ey=o.y-q.y,d2=ex*ex+ey*ey,need=(rr*rr+d2-rj*rj)*.5f;
        if(need>=0 && need*need>=rr*rr*d2)continue;
        ux[nearN]=ex;uy[nearN]=ey;cut[nearN]=rr>0?need/rr:0;++nearN;
      }
      const int pts=std::max(12,int(rr*6.6f));
      float cd,sd;BestEffectUI::ninja::cosSin(2*Renderer::HALF_TURN/pts,cd,sd);
      float ca=1,sa=0;
      for(int s=0;s<pts;++s,BestEffectUI::ninja::turn(ca,sa,cd,sd)) {
        loopStep();++stats_.points;
        const float y=q.y+rr*sa;if(y<top)continue;
        const int w=(kq*(205-int(50*sa)))>>8;if(w<5)continue;
        bool inside=false;
        for(int k=0;k<nearN && !inside;++k)inside=ca*ux[k]+sa*uy[k]>cut[k];
        if(!inside)r.addw(BestEffectUI::ninja::Mask::floorOf(q.x+rr*ca+.5f),BestEffectUI::ninja::Mask::floorOf(y+.5f),rim,w);
      }
    }
  }

  // Slash's blade line over the readout: every pixel on the line, glyph or
  // not, pulled toward c; rows within 6 of the readout only.
  void blade(float x0,float x1,float cx,float cy,float slope,const Renderer::Rgb &c,float k) {
    Renderer &r=r_;const int kq=Renderer::q8(k);if(kq<=0)return;
    const Renderer::Ink ink=Renderer::inkOf(c);
    const int top=std::max(0,r.number_.y0-6),last=std::min(r.h_-1,r.number_.y1+6);
    for(int x=std::max(0,int(x0));x<=std::min(r.w_-1,int(x1));++x) {
      loopStep();++stats_.columns;
      const int y=Renderer::roundi(cy-(x-cx)*slope);if(y<top || y>last)continue;
      uint16_t &p=r.px_[y*r.w_+x];p=Renderer::heated(p,ink,kq);++r.plots_;
    }
  }
  // Renderer::glow about any centre.
  void glow(float cx,float cy,float rx,float ry,const Renderer::Rgb &col,float k) {
    Renderer &r=r_;
    const Renderer::Ink c=Renderer::inkOf(col);const int kq=Renderer::q8(k);
    const int cx2=Renderer::roundi(cx*2),irx=int(65536/(4*rx*rx));
    for(int y=std::max(0,int(cy-ry));y<=std::min(r.bottom_,int(cy+ry));++y) {
      if(over())return;
      const float dy=(y-cy)/ry;const int dy2=int(dy*dy*65536);if(dy2>=65536)continue;
      const float span=rx*r.root(1-dy*dy);
      for(int x=std::max(0,int(cx-span));x<=std::min(r.w_-1,int(cx+span)+1);++x) {
        loopStep();++stats_.pixels;
        const int dx=2*x-cx2,d=dx*dx*irx+dy2;if(d>=65536)continue;
        const int fall=(65536-d)>>8;r.addw(x,y,c,(((fall*fall)>>8)*kq)>>8);
      }
    }
  }
  // Renderer::flames' fire field with its height and scroll clock given.
  void fire(int base,float height,float scrollT) {
    Renderer &r=r_;
    if(height<=.01f)return;
    base=std::min(r.bottom_-2,base);
    const int rise1=int(scrollT*80*.10f*256),rise2=int(scrollT*130*.21f*256),sway=int(scrollT*1.7f*256);
    for(int x=0;x<r.w_;x+=2) {
      if(!(x&7) && over())return;
      const int tongue=int(height*(10+19*Renderer::noise(x*18,sway,11)*(1/255.0f))),inv=65536/std::max(1,tongue);
      for(int y=base;y>=0 && base-y<=tongue+2;y-=2) {
        loopStep();++stats_.pixels;
        const int rise=base-y;
        auto heatAt=[&](int n){return ((((287-((rise*inv)>>8))*(115+((218*n)>>8)))>>8)-26)*BestEffectUI::detail::SMOOTH[std::min(255,rise*21)]>>8;};
        // As Renderer::flames: a cell that cannot pass even at the hottest
        // noise needs neither its mask nor its noise (byte-identical frames).
        if(std::max(heatAt(0),heatAt(255))<=34)continue;
        if(r.blocked(x,y) && r.blocked(x+1,y) && r.blocked(x,y+1) && r.blocked(x+1,y+1))continue;
        const int n=(158*Renderer::noise(x*31,y*26+rise1,12)+97*Renderer::noise(x*67,y*54+rise2,13))>>8;
        int heat=heatAt(n);
        if(heat<=34)continue;
        heat=std::min(255,heat);
        const Renderer::Ink c=Renderer::fire(heat);const int kq=std::min(256,(heat*666)>>8);
        if(heat>110){r.addw(x,y,c,kq);r.addw(x+1,y,c,kq);r.addw(x,y+1,c,kq);r.addw(x+1,y+1,c,kq);}
        else{r.addw(x,y,c,kq);r.addw(x+1,y+1,c,kq);}
      }
    }
  }
};
}
