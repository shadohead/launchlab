#pragma once
// HIL only (included by hil.h): the device bench of the 0.20 sound study
// (docs/FIRMWARE_SOUND_0_20.md), timing the kernels of hil_sound_kernels.h.
//   !sndbench               every kernel over 16,000 samples (1 s at 16 kHz) in
//                           64-sample blocks at 80, 160 and 240 MHz; one
//                           HIL_SND line per kernel and clock, then HIL_SND_END
//   !sndplay <demo> <level> renders a 4 s demo (0 chiptune, 1 sample voices,
//                           2 SoundFont-class voices and reverb) into PSRAM,
//                           plays it (level 1 Low, 2 High) and reports the
//                           ADC's range and the detector's edges during
//                           playback against 2 s of silence before it
//   !sndwatch <ms>          the ADC's range and the detector's edges and
//                           launches over the next <ms> (100-30000), whatever
//                           plays, from its own task so the UI loop keeps
//                           feeding a streamed song: HIL_SNDWATCH when done
// Buffers are allocated for the command and freed after it, so neither
// command adds static internal RAM.
#include <atomic>
#include <memory>
#include <new>
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#if LAUNCHLAB_CPU_PM
#include "esp_pm.h"
#endif
#include "hil_sound_kernels.h"

namespace HilSound {
using namespace SoundKernels;
constexpr unsigned BLOCK=64,BENCH_SAMPLES=16000,RATE=16000,PCM=32768;
template<class T> struct Freer {void operator()(T *p)const{heap_caps_free(p);}};
template<class T> using Buffer=std::unique_ptr<T,Freer<T>>;
template<class T> Buffer<T> allocate(size_t n,uint32_t caps) {
  return Buffer<T>(static_cast<T *>(heap_caps_malloc(n*sizeof(T),caps)));
}
// One harmonic test instrument (as the host bench), a guard sample past the loop end.
inline void fillInstrument(int16_t *pcm,int8_t *pcm8) {
  for(unsigned i=0;i<=PCM;++i) {
    const float t=float(i)/RATE;
    pcm[i]=int16_t(12000*std::sin(6.2831853f*220*t)+4000*std::sin(6.2831853f*660*t));
    if(pcm8)pcm8[i]=int8_t(pcm[i]>>8);
  }
}
inline uint32_t step(float hz){return uint32_t(hz*(4294967296.0f/RATE));}

// The CPU at `mhz` for the duration: the PM maximum raised, a CPU_FREQ_MAX
// lock held; restored to the firmware's 80/40 MHz policy afterwards.
class Clock {
public:
  explicit Clock(unsigned mhz) {
#if LAUNCHLAB_CPU_PM
    if(!CpuPower::active() || mhz==80)return;
    const esp_pm_config_t boosted={int(mhz),40,false};
    if(esp_pm_configure(&boosted)!=ESP_OK)return;
    static esp_pm_lock_handle_t lock=nullptr;
    if(!lock && esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"ll_snd",&lock)!=ESP_OK){restore();return;}
    lock_=lock;esp_pm_lock_acquire(lock_);
#endif
    (void)mhz;
  }
  ~Clock() {
#if LAUNCHLAB_CPU_PM
    if(lock_){esp_pm_lock_release(lock_);restore();}
#endif
  }
private:
#if LAUNCHLAB_CPU_PM
  static void restore(){const esp_pm_config_t normal={80,40,false};esp_pm_configure(&normal);}
  esp_pm_lock_handle_t lock_=nullptr;
#endif
};

template<class Kernel> void time(const char *name,unsigned mhz,const char *data,float *mix,Kernel kernel) {
  const uint32_t c0=esp_cpu_get_cycle_count();const int64_t t0=esp_timer_get_time();
  for(unsigned done=0;done<BENCH_SAMPLES;done+=BLOCK){for(unsigned i=0;i<BLOCK;++i)mix[i]=0;kernel(mix,BLOCK);}
  const uint32_t cycles=esp_cpu_get_cycle_count()-c0;const int64_t us=esp_timer_get_time()-t0;
  Hil::reply("HIL_SND kernel=%s mhz=%u data=%s us_per_sample=%.3f cycles_per_sample=%.1f\n",name,mhz,data,
    double(us)/BENCH_SAMPLES,double(cycles)/BENCH_SAMPLES);
}

inline void bench() {
  auto tables=allocate<Tables>(1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  auto reverbState=allocate<Reverb>(1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  auto mix=allocate<float>(BLOCK,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  auto out=allocate<int16_t>(BLOCK,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  auto pcm=allocate<int16_t>(PCM+1,MALLOC_CAP_SPIRAM);auto pcm8=allocate<int8_t>(PCM+1,MALLOC_CAP_SPIRAM);
  auto adpcm=allocate<uint8_t>(PCM/2,MALLOC_CAP_SPIRAM);
  // An internal-RAM copy of part of the instrument, for the PSRAM cost.
  constexpr unsigned SMALL=4096;auto small=allocate<int16_t>(SMALL+1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(!tables || !reverbState || !mix || !out || !pcm || !pcm8 || !adpcm || !small){Hil::replyLine("HIL_ERR sndbench memory");return;}
  new(tables.get()) Tables();new(reverbState.get()) Reverb();
  fillInstrument(pcm.get(),pcm8.get());for(unsigned i=0;i<=SMALL;++i)small.get()[i]=pcm.get()[i];
  for(unsigned i=0;i<PCM/2;++i)adpcm.get()[i]=uint8_t(i*37+11);
  float *m=mix.get();
  for(unsigned mhz:{80u,160u,240u}) {
    Clock clock(mhz);
    Pulse p;p.inc=step(440);p.level=.2f;
    Triangle tr;tr.inc=step(110);tr.level=.2f;
    Noise nz;nz.inc=step(4000);nz.level=.1f;
    Wave w;w.inc=step(330);w.level=.2f;
    Fm f;f.cinc=step(440);f.minc=step(880);f.index=1.5f;f.level=.2f;
    Sample s16;s16.data=pcm.get();s16.length=PCM;s16.loopStart=RATE;s16.step=uint32_t(1.4983f*65536);s16.level=.3f;
    Sample in16=s16;in16.data=small.get();in16.length=SMALL;in16.loopStart=0;
    Sample8 s8;s8.data=pcm8.get();s8.length=PCM;s8.loopStart=RATE;s8.step=s16.step;s8.level=.3f;
    Sf2 sf;sf.s=s16;sf.b0=.067f;sf.b1=.134f;sf.b2=.067f;sf.a1=-1.143f;sf.a2=.413f;sf.env=.4f;sf.envMul=.99999f;sf.lfoInc=step(5);sf.vibrato=.005f;sf.baseStep=s16.step;
    Adpcm ad;unsigned at=0;int16_t *o=out.get();
    time("a_pulse",mhz,"internal",m,[&](float *x,unsigned n){pulse(p,x,n);});
    time("a_pulse_blep",mhz,"internal",m,[&](float *x,unsigned n){pulseBlep(p,x,n);});
    time("a_triangle",mhz,"internal",m,[&](float *x,unsigned n){triangle(tr,x,n);});
    time("a_noise",mhz,"internal",m,[&](float *x,unsigned n){noise(nz,x,n);});
    time("a_wavetable",mhz,"internal",m,[&](float *x,unsigned n){wavetable(w,tables->wave,x,n);});
    time("a_fm2op",mhz,"internal",m,[&](float *x,unsigned n){fm(f,tables->sine,x,n);});
    time("b_sample16",mhz,"psram",m,[&](float *x,unsigned n){sample16(s16,x,n);});
    time("b_sample16",mhz,"internal",m,[&](float *x,unsigned n){sample16(in16,x,n);});
    time("b_sample8",mhz,"psram",m,[&](float *x,unsigned n){sample8(s8,x,n);});
    time("b_adpcm_decode",mhz,"psram",m,[&](float *x,unsigned n){adpcmDecode(ad,adpcm.get()+((at+=n/2)%(PCM/2-n)),o,n);x[0]+=o[0];});
    time("c_sf2_voice",mhz,"psram",m,[&](float *x,unsigned n){sf2(sf,tables->sine,x,n);});
    time("x_reverb",mhz,"internal",m,[&](float *x,unsigned n){x[0]=.1f;reverb(*reverbState,x,n);});
    time("x_output",mhz,"internal",m,[&](float *x,unsigned n){output(x,o,n);});
  }
  Hil::replyLine("HIL_SND_END");
}

// A 4 s demo: 120 bpm sixteenths of a short original phrase.
inline bool renderDemo(unsigned demo,int16_t *out,unsigned samples) {
  auto tables=allocate<Tables>(1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  auto reverbState=allocate<Reverb>(1,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  auto pcm=allocate<int16_t>(PCM+1,MALLOC_CAP_SPIRAM);
  if(!tables || !reverbState || !pcm)return false;
  new(tables.get()) Tables();new(reverbState.get()) Reverb();fillInstrument(pcm.get(),nullptr);
  static const uint8_t LEAD[16]={76,79,83,79,81,79,76,74,76,79,83,86,84,83,79,76},BASS[4]={40,43,45,38};
  auto hz=[](unsigned note){return 440.0f*std::pow(2.0f,(float(note)-69)/12);};
  constexpr unsigned SIXTEENTH=RATE/8;  // 125 ms at 120 bpm
  Pulse lead;lead.duty=0x40000000u;Pulse harmony;harmony.duty=0x20000000u;Triangle bass;Noise hat;hat.inc=step(6000);
  Sample piano;piano.data=pcm.get();piano.length=PCM;piano.loopStart=RATE;
  Sf2 pad;pad.s=piano;pad.b0=.067f;pad.b1=.134f;pad.b2=.067f;pad.a1=-1.143f;pad.a2=.413f;pad.envMul=.99995f;pad.lfoInc=step(5);pad.vibrato=.004f;
  float mix[BLOCK];
  for(unsigned at=0;at<samples;at+=BLOCK) {
    const unsigned n=std::min(BLOCK,samples-at);
    if(at%SIXTEENTH==0) {
      const unsigned k=at/SIXTEENTH;
      lead.inc=step(hz(LEAD[k%16]));lead.level=.22f;lead.dlevel=-.22f/SIXTEENTH;
      harmony.inc=step(hz(LEAD[k%16]-5));harmony.level=.1f;harmony.dlevel=-.1f/SIXTEENTH;
      if(k%4==0){bass.inc=step(hz(BASS[(k/4)%4]));bass.level=.3f;bass.dlevel=-.15f/(4*SIXTEENTH);}
      hat.level=k%2?.04f:.07f;hat.dlevel=-hat.level/SIXTEENTH;
      piano.pos=0;piano.frac=0;piano.step=uint32_t(hz(LEAD[k%16]-12)/220*65536);piano.level=.35f;piano.dlevel=-.3f/SIXTEENTH;
      if(k%8==0){pad.s=piano;pad.baseStep=uint32_t(hz(LEAD[k%16]-24)/220*65536);pad.env=.35f;}
    }
    for(unsigned i=0;i<n;++i)mix[i]=0;
    if(demo==0){pulse(lead,mix,n);pulse(harmony,mix,n);triangle(bass,mix,n);noise(hat,mix,n);}
    else if(demo==1){sample16(piano,mix,n);triangle(bass,mix,n);noise(hat,mix,n);}
    else {sf2(pad,tables->sine,mix,n);sample16(piano,mix,n);reverb(*reverbState,mix,n);}
    output(mix,out+at,n);
  }
  return true;
}

// The ADC and detector over a window: its range, edges and launches.
struct Window {float lo=1e9f,hi=-1e9f;uint32_t edges=0,launches=0;};
inline Window watch(uint32_t ms) {
  Window w;const auto first=acquisition.snapshot();const uint32_t start=millis();
  while(millis()-start<ms){const auto s=acquisition.snapshot();w.lo=std::min(w.lo,float(s.minimum));w.hi=std::max(w.hi,float(s.maximum));delay(20);}
  const auto last=acquisition.snapshot();w.edges=last.edges-first.edges;w.launches=last.launches-first.launches;return w;
}
inline std::atomic<bool> watching{false};
inline void watchTask(void *arg) {
  const uint32_t ms=uint32_t(uintptr_t(arg));const Window w=watch(ms);
  Hil::reply("HIL_SNDWATCH ms=%lu min=%.0f max=%.0f edges=%lu launches=%lu\n",(unsigned long)ms,w.lo,w.hi,(unsigned long)w.edges,(unsigned long)w.launches);
  watching=false;vTaskDelete(nullptr);
}
inline void watchCommand(uint32_t ms) {
  if(ms<100 || ms>30000 || watching.exchange(true)){Hil::replyLine("HIL_ERR sndwatch");return;}
  if(xTaskCreatePinnedToCore(watchTask,"hil_snd",3072,reinterpret_cast<void *>(uintptr_t(ms)),1,nullptr,1)!=pdPASS) {
    watching=false;Hil::replyLine("HIL_ERR sndwatch task");
  }
}

inline void play(unsigned demo,unsigned level) {
  constexpr unsigned SECONDS=4,SAMPLES=SECONDS*RATE;
  auto buffer=allocate<int16_t>(SAMPLES,MALLOC_CAP_SPIRAM);
  if(!buffer || demo>2 || level<1 || level>2){Hil::replyLine("HIL_ERR sndplay");return;}
  const int64_t r0=esp_timer_get_time();
  if(!renderDemo(demo,buffer.get(),SAMPLES)){Hil::replyLine("HIL_ERR sndplay memory");return;}
  const uint32_t renderUs=uint32_t(esp_timer_get_time()-r0);
  const Window quiet=watch(2000);
  if(!soundPlayer.power()){Hil::replyLine("HIL_ERR sndplay power");return;}
  M5.Speaker.setVolume(level==2?160:72);
  if(!M5.Speaker.playRaw(buffer.get(),SAMPLES,RATE,false,1,0,true)){Hil::replyLine("HIL_ERR sndplay play");return;}
  const Window loud=watch(SECONDS*1000);
  while(M5.Speaker.isPlaying())delay(5);
  Hil::reply("HIL_SNDPLAY demo=%u level=%u render_us=%lu quiet_min=%.0f quiet_max=%.0f quiet_edges=%lu playing_min=%.0f playing_max=%.0f playing_edges=%lu playing_launches=%lu\n",
    demo,level,(unsigned long)renderUs,quiet.lo,quiet.hi,(unsigned long)quiet.edges,loud.lo,loud.hi,(unsigned long)loud.edges,(unsigned long)loud.launches);
}
}

LOOP_HELPER static void hilSoundCommand(const char *cmd,const char *a1,const char *a2) {
  if(!strcmp(cmd,"sndbench")){HilSound::bench();return;}
  if(!strcmp(cmd,"sndwatch")){HilSound::watchCommand(a1?uint32_t(atoi(a1)):0);return;}
  if(!strcmp(cmd,"sndplay")){HilSound::play(a1?unsigned(atoi(a1)):0,a2?unsigned(atoi(a2)):1);return;}
  Hil::replyLine("HIL_ERR snd command");
}
