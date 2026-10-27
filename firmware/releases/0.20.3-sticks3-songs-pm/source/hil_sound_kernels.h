#pragma once
// Inner mixing loops for the 0.20 sound study (docs/FIRMWARE_SOUND_0_20.md).
// Each kernel renders one voice into a float mix buffer, a block at a time,
// the way a streaming synth would. The same code is timed on the host
// (tools/sound_bench/bench.cpp), compiled for the ESP32-S3 to count instructions
// (tools/sound_bench/loop_size.py) and timed on the device (!sndbench).
// Prototype only: compiled into HIL test builds (hil_sound_bench.h) and the
// host bench, never into production firmware.
#include <cmath>
#include <cstddef>
#include <cstdint>

#ifndef SOUND_KERNEL
#define SOUND_KERNEL __attribute__((noinline))
#endif

namespace SoundKernels {
constexpr unsigned TABLE=1024;
// A sine table like the built-in synth's (1,024 points, read without interpolation).
struct Tables {
  float sine[TABLE+1];
  float wave[256+1];  // one wavetable cycle, guard point for interpolation
  Tables() {
    for(unsigned i=0;i<=TABLE;++i)sine[i]=std::sin(6.2831853f*float(i)/TABLE);
    for(unsigned i=0;i<=256;++i){const float x=6.2831853f*float(i)/256;wave[i]=.6f*std::sin(x)+.3f*std::sin(2*x)+.1f*std::sin(5*x);}
  }
};

// (a) Chiptune: a pulse with a duty cycle and a linear volume ramp (NES, Game Boy).
struct Pulse {uint32_t phase=0,inc=0,duty=0x40000000u;float level=0,dlevel=0;};
SOUND_KERNEL inline void pulse(Pulse &v,float *mix,unsigned n) {
  uint32_t ph=v.phase;float l=v.level;
  for(unsigned i=0;i<n;++i){ph+=v.inc;mix[i]+=(ph<v.duty?l:-l);l+=v.dlevel;}
  v.phase=ph;v.level=l;
}
// The same pulse band-limited with PolyBLEP at both edges, so high notes do
// not alias at a 16-22 kHz output rate.
SOUND_KERNEL inline void pulseBlep(Pulse &v,float *mix,unsigned n) {
  uint32_t ph=v.phase;float l=v.level;const float dt=float(v.inc)*(1.0f/4294967296.0f),idt=1/dt;
  const float duty=float(v.duty)*(1.0f/4294967296.0f);
  for(unsigned i=0;i<n;++i) {
    ph+=v.inc;const float t=float(ph)*(1.0f/4294967296.0f);
    float s=t<duty?1.0f:-1.0f;
    auto blep=[&](float x){if(x<dt){x*=idt;return x+x-x*x-1;}if(x>1-dt){x=(x-1)*idt;return x*x+x+x+1;}return 0.0f;};
    s+=blep(t);float u=t-duty;if(u<0)u+=1;s-=blep(u);
    mix[i]+=s*l;l+=v.dlevel;
  }
  v.phase=ph;v.level=l;
}
struct Triangle {uint32_t phase=0,inc=0;float level=0,dlevel=0;};
SOUND_KERNEL inline void triangle(Triangle &v,float *mix,unsigned n) {
  uint32_t ph=v.phase;float l=v.level;
  for(unsigned i=0;i<n;++i){ph+=v.inc;const int32_t x=int32_t(ph<0x80000000u?ph:~ph);mix[i]+=(float(x)*(1.0f/1073741824.0f)-1.0f)*l;l+=v.dlevel;}
  v.phase=ph;v.level=l;
}
// A 15-bit LFSR clocked at a period, as the NES noise channel.
struct Noise {uint16_t lfsr=1;uint32_t phase=0,inc=0;float level=0,dlevel=0;};
SOUND_KERNEL inline void noise(Noise &v,float *mix,unsigned n) {
  uint16_t r=v.lfsr;uint32_t ph=v.phase;float l=v.level;
  for(unsigned i=0;i<n;++i) {
    const uint32_t next=ph+v.inc;if(next<ph){const uint16_t b=(r^(r>>1))&1;r=uint16_t((r>>1)|(b<<14));}ph=next;
    mix[i]+=(r&1?l:-l);l+=v.dlevel;
  }
  v.lfsr=r;v.phase=ph;v.level=l;
}
// (a) Wavetable: one 256-point cycle, linear interpolation, linear volume ramp.
struct Wave {uint32_t phase=0,inc=0;float level=0,dlevel=0;};
SOUND_KERNEL inline void wavetable(Wave &v,const float *wave,float *mix,unsigned n) {
  uint32_t ph=v.phase;float l=v.level;
  for(unsigned i=0;i<n;++i) {
    const uint32_t k=ph>>24;const float f=float((ph>>8)&0xFFFF)*(1.0f/65536);
    const float a=wave[k],b=wave[k+1];mix[i]+=(a+(b-a)*f)*l;ph+=v.inc;l+=v.dlevel;
  }
  v.phase=ph;v.level=l;
}
// (a) Two-operator FM (an OPL-style voice): a modulator into a carrier, both from the sine table.
struct Fm {uint32_t carrier=0,modulator=0,cinc=0,minc=0;float index=0,level=0,dlevel=0;};
SOUND_KERNEL inline void fm(Fm &v,const float *sine,float *mix,unsigned n) {
  uint32_t c=v.carrier,m=v.modulator;float l=v.level;const float depth=v.index*float(1u<<30);
  for(unsigned i=0;i<n;++i) {
    const float mod=sine[m>>22]*depth;m+=v.minc;
    mix[i]+=sine[(c+uint32_t(int32_t(mod)))>>22]*l;c+=v.cinc;l+=v.dlevel;
  }
  v.carrier=c;v.modulator=m;v.level=l;
}

// (b) Sample playback: 16-bit PCM, 16.16 position, linear interpolation, a loop.
struct Sample {const int16_t *data=nullptr;uint32_t length=0,loopStart=0,pos=0,frac=0,step=0;float level=0,dlevel=0;};
SOUND_KERNEL inline void sample16(Sample &v,float *mix,unsigned n) {
  const int16_t *d=v.data;uint32_t p=v.pos,f=v.frac;float l=v.level*(1.0f/32768);const float dl=v.dlevel*(1.0f/32768);
  for(unsigned i=0;i<n;++i) {
    const float a=d[p],b=d[p+1];mix[i]+=(a+(b-a)*float(f)*(1.0f/65536))*l;
    f+=v.step;p+=f>>16;f&=0xFFFF;if(p>=v.length)p=v.loopStart+(p-v.length);l+=dl;
  }
  v.pos=p;v.frac=f;v.level=l*32768;
}
// 8-bit linear PCM (half the storage).
struct Sample8 {const int8_t *data=nullptr;uint32_t length=0,loopStart=0,pos=0,frac=0,step=0;float level=0,dlevel=0;};
SOUND_KERNEL inline void sample8(Sample8 &v,float *mix,unsigned n) {
  const int8_t *d=v.data;uint32_t p=v.pos,f=v.frac;float l=v.level*(1.0f/128);const float dl=v.dlevel*(1.0f/128);
  for(unsigned i=0;i<n;++i) {
    const float a=d[p],b=d[p+1];mix[i]+=(a+(b-a)*float(f)*(1.0f/65536))*l;
    f+=v.step;p+=f>>16;f&=0xFFFF;if(p>=v.length)p=v.loopStart+(p-v.length);l+=dl;
  }
  v.pos=p;v.frac=f;v.level=l*128;
}
// IMA ADPCM (a quarter of the storage): decode a block of nibbles into
// 16-bit samples. A voice decodes ahead into a small buffer and plays it
// with sample16, so this is a cost per source sample.
struct Adpcm {int32_t predictor=0;int index=0;};
SOUND_KERNEL inline void adpcmDecode(Adpcm &s,const uint8_t *in,int16_t *out,unsigned samples) {
  static const int16_t STEP[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
  static const int8_t INDEX[16]={-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
  int32_t pred=s.predictor;int idx=s.index;
  for(unsigned i=0;i<samples;++i) {
    const uint8_t nib=(in[i>>1]>>((i&1)*4))&15;const int step=STEP[idx];
    int diff=step>>3;if(nib&1)diff+=step>>2;if(nib&2)diff+=step>>1;if(nib&4)diff+=step;
    pred+=nib&8?-diff:diff;pred=pred<-32768?-32768:pred>32767?32767:pred;
    idx+=INDEX[nib];idx=idx<0?0:idx>88?88:idx;out[i]=int16_t(pred);
  }
  s.predictor=pred;s.index=idx;
}

// (c) A SoundFont-class voice: the sample voice plus a resonant two-pole
// low-pass (a biquad, coefficients set per block from the modulation
// envelope), an exponential volume envelope and vibrato on the step.
struct Sf2 {
  Sample s;float b0=0,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;float env=0,envMul=1;
  uint32_t lfoPhase=0,lfoInc=0;float vibrato=0;uint32_t baseStep=0;
};
SOUND_KERNEL inline void sf2(Sf2 &v,const float *sine,float *mix,unsigned n) {
  const int16_t *d=v.s.data;uint32_t p=v.s.pos,f=v.s.frac;
  float x1=v.x1,x2=v.x2,y1=v.y1,y2=v.y2,e=v.env;
  // Vibrato once a block, as a SoundFont synth updates its modulators.
  v.lfoPhase+=v.lfoInc*n;const uint32_t step=uint32_t(float(v.baseStep)*(1+v.vibrato*sine[v.lfoPhase>>22]));
  for(unsigned i=0;i<n;++i) {
    const float a=d[p],b=d[p+1];const float x=(a+(b-a)*float(f)*(1.0f/65536))*(1.0f/32768);
    const float y=v.b0*x+v.b1*x1+v.b2*x2-v.a1*y1-v.a2*y2;x2=x1;x1=x;y2=y1;y1=y;
    mix[i]+=y*e;e*=v.envMul;
    f+=step;p+=f>>16;f&=0xFFFF;if(p>=v.s.length)p=v.s.loopStart+(p-v.s.length);
  }
  v.s.pos=p;v.s.frac=f;v.x1=x1;v.x2=x2;v.y1=y1;v.y2=y2;v.env=e;
}

// A mono reverb after the mix (four damped combs and two all-passes, a
// small Freeverb), per output sample. Delay lines live in internal RAM.
struct Reverb {
  static constexpr unsigned C0=557,C1=593,C2=641,C3=677,A0=225,A1=341;
  float c0[C0]={},c1[C1]={},c2[C2]={},c3[C3]={},a0[A0]={},a1[A1]={};
  unsigned i0=0,i1=0,i2=0,i3=0,j0=0,j1=0;float s0=0,s1=0,s2=0,s3=0,feedback=.78f,damp=.3f,wet=.25f;
};
SOUND_KERNEL inline void reverb(Reverb &r,float *mix,unsigned n) {
  auto comb=[&](float *buf,unsigned size,unsigned &i,float &store,float x){const float o=buf[i];store=o*(1-r.damp)+store*r.damp;buf[i]=x+store*r.feedback;if(++i==size)i=0;return o;};
  auto allpass=[&](float *buf,unsigned size,unsigned &i,float x){const float o=buf[i];buf[i]=x+o*.5f;if(++i==size)i=0;return o-x;};
  for(unsigned k=0;k<n;++k) {
    const float x=mix[k]*.25f;
    float y=comb(r.c0,Reverb::C0,r.i0,r.s0,x)+comb(r.c1,Reverb::C1,r.i1,r.s1,x)+comb(r.c2,Reverb::C2,r.i2,r.s2,x)+comb(r.c3,Reverb::C3,r.i3,r.s3,x);
    y=allpass(r.a0,Reverb::A0,r.j0,y);y=allpass(r.a1,Reverb::A1,r.j1,y);
    mix[k]+=y*r.wet;
  }
}
// The output stage: the 0.19 limiter's curve and the conversion to 16 bits.
SOUND_KERNEL inline void output(const float *mix,int16_t *out,unsigned n) {
  for(unsigned i=0;i<n;++i) {
    const float v=mix[i],a=std::fabs(v),d=a-.6f,lim=d<=0?a:.6f+.35f*3*d/(1+3*d);
    out[i]=int16_t((v<0?-lim:lim)*32767);
  }
}
}
