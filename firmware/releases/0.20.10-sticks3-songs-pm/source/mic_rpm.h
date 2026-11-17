#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>
#ifdef ESP_PLATFORM
#include <dsps_fft2r.h>
#endif

// Acoustic spin estimate for the StickS3's built-in mic: core/src/tracker.rs
// (the phone apps' harmonic tracker) in float32, without allocation, at 4 kHz.
// It hears a Bey spinning after release, about half a second after it leaves:
// a different quantity from the optical peak (see docs/FIRMWARE_MIC_RPM.md).
// The window is 0.4 s and a median pull is 90 ms, so it cannot follow a pull.
//
// Differences from the apps, all on purpose (and the flatness band, the near
// search and re-acquisition below, from the unit's own recorded spins):
// - 4 kHz ends the spectrum at 2 kHz where they read 2.2 kHz, so a sixth
//   harmonic above 333 Hz reads as absent. That is above 20,000 RPM, and the
//   search still ends at 350 Hz like theirs.
// - The device knows the launch's optical peak, which the phones do not. Given
//   one, reset() takes it as a prior: the spin is below that speed, so the
//   search starts just under it at 1, 2 and 3 sound cycles per turn, and the
//   nearest cycle count that fits turns Hz into RPM. With no prior (an invalid
//   optical reading) it searches everything, as the apps do.
// - It runs at 80 MHz beside the UI, so it spends nothing it does not need:
//   a quiet or clipped window stops before the transform, a noisy one before
//   the search, and contrast is worked out only for the bins the search looks
//   at. Once locked it reads every 0.2 s, close to the last reading.
namespace MicRpm {
constexpr uint32_t CAPTURE_HZ=16000,DECIMATE=4,TRACK_HZ=CAPTURE_HZ/DECIMATE;
constexpr unsigned WINDOW=1600,HOP=400,LOCKED_HOP=800,MAX_FFT=2048;  // 0.4 s every 0.1 s as the apps, 0.2 s once locked
constexpr unsigned CANDIDATES=641;                    // 30..350 Hz in 0.5 Hz steps
constexpr unsigned TAPS=63;
// The apps' acceptance limits. QUIET_DB is relative to a phone's level, so it
// is provisional here until the mic's level is measured.
constexpr float QUIET_DB=-62.f,CLIP=0.995f,MAX_FLATNESS=0.32f,MIN_SCORE=22.f,MIN_MARGIN=1.8f;
// The prior: as a share of the optical peak's turn rate, the spin is at most
// PRIOR_TOP and at least PRIOR_BOTTOM x e^(-t/PRIOR_DECAY_S), t seconds after
// release, never below PRIOR_FLOOR. Heard so far: 0.91-0.98 about a second in.
constexpr float PRIOR_TOP=1.03f,PRIOR_BOTTOM=0.8f,PRIOR_DECAY_S=12.f,PRIOR_FLOOR=0.25f;
constexpr unsigned MAX_CYCLES=3;
// Readings with a sound the prior cannot place before the search widens to everything.
constexpr unsigned PRIOR_MISSES=4;
constexpr unsigned STREAK=3;           // readings in a row to trust one, as the apps
// Where the next reading looks first: 0.1 octave below the last, and 3% above
// it, as a spin only slows (the apps look 0.1 octave above too).
constexpr float NEAR_BELOW=0.933f,NEAR_ABOVE=1.03f;
// A tone that passes every limit within REACQUIRE_S of the last trusted
// reading, at most REACQUIRE_FALL below it, is trusted at once: the spin, back
// after an impact or a hand covered it for a window or two.
constexpr float REACQUIRE_S=1.f,REACQUIRE_FALL=0.9f;
// Flatness is read where a spin's first harmonics are. The apps read
// 200-1,800 Hz: on the device that band is mostly mic floor once a spin is
// quiet, and called a clean 70 Hz tone noise.
constexpr float FLAT_LOW_HZ=40.f,FLAT_HIGH_HZ=600.f;

enum class Reason:uint8_t {Tracking,Quiet,Clipping,BroadbandNoise,WeakHarmonics,Ambiguous,HarmonicAmbiguity,Acquiring,FasterThanOptical};
constexpr unsigned REASONS=9;
enum class Search:uint8_t {None,Prior,Near,Full};  // which search a reading came from, cheapest first: None never searched
inline const char *reasonName(Reason r) {
  switch(r){case Reason::Tracking:return "tracking";case Reason::Quiet:return "quiet";case Reason::Clipping:return "clipping";
    case Reason::BroadbandNoise:return "broadband";case Reason::WeakHarmonics:return "weak";case Reason::Ambiguous:return "ambiguous";
    case Reason::HarmonicAmbiguity:return "harmonic";case Reason::FasterThanOptical:return "faster";default:return "acquiring";}
}
// log2 of a positive normal float to 0.0001, for what would otherwise be a
// libm call per bin: 0.0003 dB, far inside every limit here.
inline float log2Fast(float x) {
  uint32_t bits;std::memcpy(&bits,&x,sizeof(bits));
  const int exponent=int(bits>>23)-127;
  bits=(bits&0x7fffffu)|0x3f800000u;
  float m;std::memcpy(&m,&bits,sizeof(m));
  return float(exponent)+(((((-0.080010877f*m+0.63551112f)*m-2.0994023f)*m+4.0496169f)*m-2.5056147f));
}
struct Reading {
  float hz=0,rpm=0,candidateHz=0,competingHz=0,level=0,score=0,margin=0,flatness=0;
  bool valid=false;Reason reason=Reason::Quiet;Search search=Search::None;uint8_t support=0,cycles=0;  // cycles and rpm: 0 unless valid
};

// 16 kHz to 4 kHz: a Hann-windowed sinc, cut at 2 kHz. What it lets through
// at 2.0-2.4 kHz folds to 1.6-2.0 kHz, which only the sixth harmonic of a spin
// over 16,000 RPM reaches.
class Decimator {
public:
  Decimator() {
    float sum=0;
    for(unsigned i=0;i<TAPS;++i) {
      const float x=float(i)-float(TAPS-1)/2;
      const float sinc=x==0?1.f:std::sin(float(M_PI)*x/DECIMATE)/(float(M_PI)*x/DECIMATE);
      taps_[i]=sinc*(0.5f-0.5f*std::cos(2*float(M_PI)*float(i)/float(TAPS-1)));
      sum+=taps_[i];
    }
    for(auto &t:taps_)t/=sum;
    reset();
  }
  void reset(){std::fill(std::begin(line_),std::end(line_),0.f);at_=0;phase_=0;}
  // Takes one 16 kHz sample; true when `out` holds a 4 kHz one.
  bool feed(float in,float &out) {
    line_[at_]=line_[at_+TAPS]=in;at_=at_+1==TAPS?0:at_+1;
    if(++phase_<DECIMATE)return false;
    // The filter is symmetric: half the multiplies.
    phase_=0;const float *history=line_+at_;float sum=taps_[TAPS/2]*history[TAPS/2];
    for(unsigned i=0;i<TAPS/2;++i)sum+=taps_[i]*(history[i]+history[TAPS-1-i]);
    out=sum;return true;
  }
private:
  float taps_[TAPS],line_[TAPS*2];unsigned at_=0,phase_=0;
};

// Everything the tracker keeps: about 40 KB, for PSRAM.
struct Space {
  alignas(16) float work[MAX_FFT];
  alignas(16) float dspTwiddle[MAX_FFT/2];  // esp-dsp's order, device only
  float hann[WINDOW],twiddle[MAX_FFT],power[MAX_FFT/2+1],db[MAX_FFT/2+1],contrast[MAX_FFT/2+1],logHz[CANDIDATES],raw[CANDIDATES];
  uint16_t worked[MAX_FFT/2+1];  // the frame each contrast bin was last worked out in
};

class Tracker {
public:
  // `window` and `fft` take shorter windows for the offline sweep; the device
  // always uses the defaults. `fft` is a power of two of at least `window`.
  explicit Tracker(Space *space,unsigned window=WINDOW,unsigned fft=2048):s_(space),window_(window),fft_(fft),
      df_(float(TRACK_HZ)/fft),bins_(fft/2+1),radius_(std::max(4,int(std::lround(20.f/df_)))) {
    for(unsigned i=0;i<window_;++i)s_->hann[i]=0.5f-0.5f*std::cos(2*float(M_PI)*float(i)/float(window_-1));
    // The half-size complex transform's twiddles, then the real split's.
    for(unsigned i=0;i<fft_/2;++i) {
      const float angle=-2*float(M_PI)*float(i)/float(fft_);
      s_->twiddle[2*i]=std::cos(angle);s_->twiddle[2*i+1]=std::sin(angle);
    }
    for(unsigned i=0;i<CANDIDATES;++i)s_->logHz[i]=std::log2(hzOf(i));
#ifdef ESP_PLATFORM
    // As dsps_fft2r_init_fc32 would, without the internal heap its reversal table takes.
    dsps_gen_w_r2_fc32(s_->dspTwiddle,fft_/2);
    dsps_bit_rev_fc32_ansi(s_->dspTwiddle,fft_/4);
#endif
  }
  unsigned window()const{return window_;}
  void benchTransform(bool plain){transform(plain);}  // the transform alone, for the device's bench
  // A new spin. `opticalRpm` is the launch's peak, 0 when it has none.
  void reset(float opticalRpm=0){previous_=0;trusted_=0;trustedAt_=0;streak_=0;gaps_=0;conflictHz_=0;conflictFrames_=0;turnHz_=opticalRpm/60;cycles_=0;misses_=0;wide_=false;}
  bool locked()const{return streak_>=STREAK;}
  // Analyzes the latest window() samples, oldest first, `seconds` after the
  // release at their centre. Forward only.
  Reading analyze(const float *samples,float seconds=0) {
    Space &s=*s_;
    float sum=0;unsigned clipped=0;
    std::fill(s.work,s.work+fft_,0.f);
    for(unsigned i=0;i<window_;++i) {
      const float v=std::isnan(samples[i])?0.f:samples[i];
      sum+=v*v;clipped+=std::fabs(v)>=CLIP;s.work[i]=v*s.hann[i];
    }
    Reading out;
    out.level=20.f*std::log10(std::sqrt(sum/float(window_))+1e-12f);
    if(out.level<QUIET_DB)return withhold(out,Reason::Quiet);
    if(float(clipped)/float(window_)>0.01f)return withhold(out,Reason::Clipping);
    transform();
    const float norm=float(window_)*float(window_);
    for(unsigned i=0;i<bins_;++i){s.power[i]=(s.power[i])/norm;s.db[i]=DB_PER_OCTAVE*log2Fast(s.power[i]+1e-18f);}
    if(++frame_==0){std::fill(s.worked,s.worked+bins_,uint16_t(0));frame_=1;}
    float logSum=0,powerSum=0;unsigned count=0;
    for(unsigned i=unsigned(std::ceil(FLAT_LOW_HZ/df_));i<unsigned(std::floor(FLAT_HIGH_HZ/df_));++i) {
      logSum+=log2Fast(s.power[i]+1e-18f);powerSum+=s.power[i];++count;
    }
    out.flatness=std::exp(logSum/float(count)*float(M_LN2))/(powerSum/float(count)+1e-18f);
    if(out.flatness>MAX_FLATNESS)return withhold(out,Reason::BroadbandNoise);

    logPrevious_=previous_>0?std::log2(previous_):0;
    const Pick pick=search(seconds,out.search);
    const float bestHz=pick.hz,bestRaw=pick.raw,margin=pick.margin;
    const unsigned hits=pick.hits;

    // A strong upper harmonic can outscore its real fundamental: never divide
    // a measured frequency blindly, or invent a speed when both are plausible.
    float peakDb=-1e30f;for(unsigned k=1;k<=4;++k)peakDb=std::max(peakDb,dbAt(bestHz*float(k)));
    float upper=-2,competing=0;
    for(unsigned divisor=2;divisor<=4;++divisor) {
      const float lower=bestHz/float(divisor);
      if(lower<15.f || contrastAt(lower)<6.f || dbAt(lower)<peakDb-40.f)continue;
      unsigned interleaved=0;
      for(unsigned k=1;k<=6;++k)if(k%divisor && contrastAt(lower*float(k))>6.f && dbAt(lower*float(k))>=peakDb-40.f)++interleaved;
      if(interleaved>=2) {
        if(upper<-1)upper=similarity(samples,bestHz);
        const float lowerSimilarity=similarity(samples,lower);
        if(lowerSimilarity>0.6f && lowerSimilarity-upper>0.001f){competing=lower;break;}
      }
    }

    bool valid=true;Reason reason=Reason::Tracking;
    if(hits<3 || bestRaw<MIN_SCORE || contrastAt(bestHz)<5.f || (out.search==Search::Prior && !pick.ok))reason=Reason::WeakHarmonics;
    else if(margin<MIN_MARGIN)reason=Reason::Ambiguous;
    if(reason!=Reason::Tracking)valid=false;
    // Isolated background peaks must not repeatedly break a clean spin trace.
    if(competing>0 && valid) {
      const bool same=conflictHz_>0 && std::fabs(std::log2(competing/conflictHz_))<0.08f;
      conflictFrames_=same?conflictFrames_+1:1;conflictHz_=competing;
      // Its real fundamental sits below whatever the search was held to: look
      // everywhere. Before the lock, at once: a window that is half launch
      // noise can start a streak before the conflict has three frames.
      if(conflictFrames_>=3 || !locked()){valid=false;reason=Reason::HarmonicAmbiguity;wide_=true;}
    } else {conflictHz_=0;conflictFrames_=0;}
    // Above three cycles a turn of the launch's peak, it is not this spin.
    const unsigned cycles=cyclesFor(bestHz);
    if(valid && !cycles){valid=false;reason=Reason::FasterThanOptical;}

    if(valid) {
      const bool close=previous_>0 && std::fabs(std::log2(bestHz/previous_))<0.15f;
      streak_=close?streak_+1:1;previous_=bestHz;gaps_=0;misses_=0;
      if(trusted_>0 && seconds-trustedAt_<=REACQUIRE_S && bestHz<=trusted_*NEAR_ABOVE && bestHz>=trusted_*REACQUIRE_FALL)streak_=std::max(streak_,STREAK);
      if(streak_<STREAK){valid=false;reason=Reason::Acquiring;}
      else{trusted_=bestHz;trustedAt_=seconds;cycles_=uint8_t(cycles);out.cycles=uint8_t(cycles);out.rpm=bestHz*60/float(cycles);}
    } else missed();
    out.hz=valid?bestHz:0;out.candidateHz=bestHz;out.competingHz=competing;out.valid=valid;out.reason=reason;
    out.support=uint8_t(hits);out.score=bestRaw;out.margin=margin;
    return out;
  }
private:
  static constexpr float WEIGHTS[4]={1.f,0.65f,0.45f,0.3f};
  static constexpr float DB_PER_OCTAVE=3.0103f;
  static float hzOf(unsigned i){return 30.f+float(i)*0.5f;}
  static unsigned indexOf(float hz){return unsigned((std::min(std::max(hz,30.f),350.f)-30.f)*2);}
  struct Pick {float hz=30,raw=-1e30f,margin=0;unsigned hits=0;bool ok=false;};
  void missed(){++gaps_;streak_=0;if(gaps_>=5)previous_=0;}
  Reading withhold(Reading &out,Reason reason) {
    conflictHz_=0;conflictFrames_=0;missed();
    out.reason=reason;return out;
  }
  // The cycle count that puts `hz` at or below the launch's peak: 1 with no prior,
  // the spin's own once it has one, 0 when none fits.
  unsigned cyclesFor(float hz)const {
    if(turnHz_<=0)return 1;
    if(cycles_)return hz<=float(cycles_)*turnHz_*PRIOR_TOP?cycles_:0;
    for(unsigned c=1;c<=MAX_CYCLES;++c)if(hz<=float(c)*turnHz_*PRIOR_TOP)return c;
    return 0;
  }
  // The best candidate from `low` to `high` Hz, scored as the apps do: the
  // first-found best is the lowest on a tie, like their stable sort.
  Pick scan(float low,float high) {
    Space &s=*s_;
    const unsigned first=indexOf(low),last=std::min(indexOf(high),CANDIDATES-1);
    Pick out;unsigned best=first;
    for(unsigned i=first;i<=last;++i) {
      const float hz=hzOf(i);float raw=0;
      for(unsigned k=0;k<4;++k)raw+=WEIGHTS[k]*contrastAt(hz*float(k+1));
      s.raw[i]=raw-(previous_>0?std::min(std::fabs(s.logHz[i]-logPrevious_)*35.f,9.f):0.f);
      if(s.raw[i]>out.raw){out.raw=s.raw[i];best=i;}
    }
    float alternative=-1e30f;bool haveAlternative=false;
    for(unsigned i=first;i<=last;++i)if(std::fabs(s.logHz[i]-s.logHz[best])>0.12f && s.raw[i]>alternative){alternative=s.raw[i];haveAlternative=true;}
    // A band too narrow to hold a second tone leaves nothing to compare with.
    out.margin=haveAlternative?out.raw-alternative:first==0 && last==CANDIDATES-1?0:MIN_MARGIN;
    out.hz=hzOf(best);out.raw=0;
    for(unsigned k=0;k<4;++k)out.raw+=WEIGHTS[k]*contrastAt(out.hz*float(k+1));
    for(unsigned k=1;k<=6;++k)out.hits+=contrastAt(out.hz*float(k))>6.f;
    out.ok=out.hits>=3 && out.raw>=MIN_SCORE && contrastAt(out.hz)>=5.f && out.margin>=MIN_MARGIN;
    return out;
  }
  float loudestHz(float from)const {
    const Space &s=*s_;
    const unsigned first=unsigned(from/df_),last=std::min(unsigned(1400.f/df_),bins_-1);
    unsigned best=first;
    for(unsigned i=first;i<=last;++i)if(s.power[i]>s.power[best])best=i;
    return float(best)*df_;
  }
  static bool harmonicOf(float hz,float tone) {
    const float k=std::max(1.f,std::round(tone/hz));
    return std::fabs(tone-k*hz)<=std::max(3.f,tone*0.015f);
  }
  // Close to the last candidate first: all a locked spin ever looks at. Then
  // under the optical peak, the fewest cycles a turn first. The wide search
  // runs with no prior, and every PRIOR_MISSES readings the prior cannot place.
  Pick search(float seconds,Search &used) {
    Pick best;
    if(wide_) {
      wide_=false;previous_=0;logPrevious_=0;
      used=Search::Full;
      return scan(30.f,350.f);
    }
    if(previous_>0) {
      used=Search::Near;
      best=scan(previous_*NEAR_BELOW,previous_*NEAR_ABOVE);
      if(best.ok || locked())return best;
    }
    if(turnHz_>0) {
      used=Search::Prior;
      const float bottom=std::max(PRIOR_BOTTOM*std::exp(-seconds/PRIOR_DECAY_S),PRIOR_FLOOR);
      const float loudest=loudestHz(turnHz_*bottom);
      for(unsigned c=cycles_?cycles_:1;c<=(cycles_?cycles_:MAX_CYCLES);++c) {
        Pick pick=scan(float(c)*turnHz_*bottom,float(c)*turnHz_*PRIOR_TOP);
        // A band under the optical peak always holds something. It is the spin
        // only if the loudest tone in the window is one of its harmonics, and
        // the fundamental is really there, by the harmonic check's own measure.
        pick.ok=pick.ok && harmonicOf(pick.hz,loudest) && dbAt(pick.hz)>=dbAt(loudest)-40.f;
        if(pick.ok){misses_=0;return pick;}
        if(pick.raw>best.raw)best=pick;
      }
      if(++misses_%PRIOR_MISSES)return best;
    }
    used=Search::Full;
    return scan(30.f,350.f);
  }
  static float median(float *v,unsigned n) {
    std::nth_element(v,v+n/2,v+n);const float upper=v[n/2];
    if(n&1)return upper;
    return (upper+*std::max_element(v,v+n/2))/2;
  }
  float at(const float *values,float hz)const {
    const float position=hz/df_;const unsigned index=unsigned(position);const float fraction=position-float(index);
    const float a=index<bins_?values[index]:-180.f,b=index+1<bins_?values[index+1]:-180.f;
    return a*(1-fraction)+b*fraction;
  }
  // A bin's height over the median of those within 20 Hz of it.
  float contrastBin(unsigned i) {
    Space &s=*s_;
    if(s.worked[i]==frame_)return s.contrast[i];
    const unsigned lo=i>unsigned(radius_)?i-radius_:0,hi=std::min(bins_,i+radius_+1);
    float near[64];const unsigned n=hi-lo;std::copy(s.db+lo,s.db+hi,near);
    s.worked[i]=frame_;return s.contrast[i]=s.db[i]-median(near,n);
  }
  float contrastAt(float hz) {
    const float position=hz/df_;const unsigned index=unsigned(position);const float fraction=position-float(index);
    const float a=index<bins_?contrastBin(index):-180.f,b=index+1<bins_?contrastBin(index+1):-180.f;
    return a*(1-fraction)+b*fraction;
  }
  float dbAt(float hz)const{return at(s_->db,hz);}
  // Normalized correlation between the window and itself one period of `hz` later.
  float similarity(const float *samples,float hz)const {
    const float lag=float(TRACK_HZ)/hz;const unsigned whole=unsigned(lag);const float fraction=lag-float(whole);
    float dot=0,aa=0,bb=0;
    for(unsigned i=unsigned(std::ceil(lag));i<window_;++i) {
      const float a=samples[i],b=samples[i-whole]*(1-fraction)+(i>whole?samples[i-whole-1]:0.f)*fraction;
      dot+=a*b;aa+=a*a;bb+=b*b;
    }
    return dot/(std::sqrt(aa*bb)+1e-24f);
  }
  void reverse() {
    float *w=s_->work;const unsigned n=fft_/2;
    for(unsigned i=1,j=0;i<n;++i) {
      unsigned bit=n>>1;
      for(;j&bit;bit>>=1)j^=bit;
      j^=bit;
      if(i<j){std::swap(w[2*i],w[2*j]);std::swap(w[2*i+1],w[2*j+1]);}
    }
  }
  // Real FFT of s_->work into s_->power (not yet normalized): a half-size
  // complex transform of the even and odd samples, then the usual split. The
  // device's complex transform is esp-dsp's assembly, about twice as fast;
  // the portable one is the host's, and the device's bench checks them against
  // each other.
  void transform(bool plain=false) {
    float *w=s_->work;const float *t=s_->twiddle;const unsigned n=fft_/2;
#ifdef ESP_PLATFORM
    if(!plain){dsps_fft2r_fc32_aes3_(w,int(n),s_->dspTwiddle);reverse();}
    else
#else
    (void)plain;
#endif
    {
      reverse();
      for(unsigned len=2;len<=n;len<<=1) {
        const unsigned step=fft_/len;
        for(unsigned i=0;i<n;i+=len)for(unsigned j=0;j<len/2;++j) {
          const float wr=t[2*j*step],wi=t[2*j*step+1];
          float *a=w+2*(i+j),*b=w+2*(i+j+len/2);
          const float re=b[0]*wr-b[1]*wi,im=b[0]*wi+b[1]*wr;
          b[0]=a[0]-re;b[1]=a[1]-im;a[0]+=re;a[1]+=im;
        }
      }
    }
    float *p=s_->power;
    const float dc=w[0]+w[1],nyquist=w[0]-w[1];p[0]=dc*dc;p[n]=nyquist*nyquist;
    for(unsigned i=1;i<n;++i) {
      const unsigned m=n-i;
      const float ar=w[2*i],ai=w[2*i+1],br=w[2*m],bi=-w[2*m+1];
      const float er=(ar+br)/2,ei=(ai+bi)/2,or_=(ar-br)/2,oi=(ai-bi)/2;
      const float wr=t[2*i],wi=t[2*i+1];
      const float re=er+or_*wi+oi*wr,im=ei+oi*wi-or_*wr;
      p[i]=re*re+im*im;
    }
  }
  Space *s_;unsigned window_,fft_;float df_;unsigned bins_;int radius_;
  float previous_=0,logPrevious_=0,turnHz_=0,trusted_=0,trustedAt_=0;unsigned streak_=0,gaps_=0,conflictFrames_=0,misses_=0;float conflictHz_=0;
  uint8_t cycles_=0;bool wide_=false;uint16_t frame_=0;
};

// A launch's spin as heard: the first readings the tracker trusts and how
// many it withheld, for one report line. Hz stays Hz: 60 x Hz is RPM only at
// one sound cycle per turn, which is unproven beyond one combo.
struct Summary {
  static constexpr unsigned KEPT=5;
  unsigned frames=0,valid=0,firstFrame=0,cycles=0;float hz[KEPT]={};unsigned withheld[REASONS]={};
  void add(const Reading &r) {
    if(r.valid){if(valid<KEPT)hz[valid]=r.hz;if(!valid)firstFrame=frames;++valid;cycles=r.cycles;}
    else ++withheld[unsigned(r.reason)];
    ++frames;
  }
  float firstHz()const{return valid?hz[0]:0;}
  // The median of the first KEPT accepted readings.
  float settledHz()const {
    if(!valid)return 0;
    float sorted[KEPT];const unsigned n=std::min(valid,KEPT);
    std::copy(hz,hz+n,sorted);std::sort(sorted,sorted+n);
    return n&1?sorted[n/2]:(sorted[n/2-1]+sorted[n/2])/2;
  }
};

// The 4 kHz line the tracker reads, kept for a recording of whole spins: a ring
// of int16 at full scale 1, which a replay feeds back through Stream::line()
// for the same readings. Lost sound is kept as silence, so a sample's place in
// a spin's recording is its time since the release.
struct Tape {
  int16_t *samples=nullptr;uint32_t capacity=0;uint64_t written=0;
  void put(float v){if(samples)samples[written++%capacity]=int16_t(std::lround(std::min(std::max(v,-1.f),32767.f/32768)*32768));}
  void gap(uint64_t n){if(samples)for(n=std::min<uint64_t>(n,capacity);n;--n)samples[written++%capacity]=0;}
};

// Feeds 16 kHz samples through the decimator to the tracker, one reading per
// hop, and twice as far apart once it is locked.
class Stream {
public:
  explicit Stream(Space *space,unsigned window=WINDOW,unsigned hop=HOP,unsigned fft=2048):tracker_(space,window,fft),hop_(hop) {}
  void reset(float opticalRpm=0){decimator_.reset();tracker_.reset(opticalRpm);used_=0;taken_=0;settling_=TAPS/DECIMATE;}
  // Where the line goes as well; nullptr for nowhere.
  void record(Tape *tape){tape_=tape;}
  // Passes over `samples` of capture the reader lost: the clock still counts
  // from the release, so the prior decays and the trace stays in time, and the
  // window and filter refill from the next sample.
  void skip(uint64_t samples) {
    decimator_.reset();used_=0;settling_=TAPS/DECIMATE;taken_+=unsigned(samples/DECIMATE);
    if(tape_)tape_->gap(samples/DECIMATE);
  }
  Tracker &tracker(){return tracker_;}
  // Seconds from the first sample to the centre of the latest reading's window.
  float seconds()const{return (float(taken_)-float(tracker_.window())/2)/float(TRACK_HZ);}
  // True when `out` holds a reading: every hop at 4 kHz once a window is full.
  bool feed(float sample,Reading &out) {
    float low;if(!decimator_.feed(sample,low))return false;
    // A window holds no sample of the filter filling: a launch restarts it.
    if(settling_){--settling_;return false;}
    return line(low,out);
  }
  // One sample of the 4 kHz line, from the decimator or a recording of it.
  bool line(float low,Reading &out) {
    if(tape_)tape_->put(low);
    const unsigned window=tracker_.window();
    if(used_==window) {
      const unsigned hop=tracker_.locked()?hop_*2:hop_;
      std::memmove(line_,line_+hop,(window-hop)*sizeof(float));used_=window-hop;
    }
    line_[used_++]=low;++taken_;
    if(used_<window)return false;
    out=tracker_.analyze(line_,seconds());return true;
  }
  // Takes PCM until a reading is ready or it runs out, and returns how many
  // samples it took: the task's path, from the ring.
  size_t feed(const int16_t *samples,size_t count,Reading &out,bool &ready) {
    ready=false;
    for(size_t i=0;i<count;++i)if(feed(float(samples[i])*(1.f/32768),out)){ready=true;return i+1;}
    return count;
  }
private:
  Decimator decimator_;Tracker tracker_;Tape *tape_=nullptr;unsigned hop_,used_=0,taken_=0,settling_=TAPS/DECIMATE;float line_[WINDOW];
};
}
