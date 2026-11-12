#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// The start floor while a sound plays (0.20.1). The speaker couples into the
// optical input; on the rig it left about 25 counts peak to peak after the
// detector's 0.5 ms mean, but a fitted sensor's coupling is unmeasured and
// audio-shaped coupling of +-60 counts made phantom launches on the host
// (test_sound_interference). So while a sound plays and the detector waits
// for a pull, a launch must start on marks at least half as strong as this
// unit's own pulls: clamp(0.5 x the median weakest start mark of its last
// COUNT confirmed pulls, minMark, CEILING). The median needs MIN_PULLS, so
// no single odd pull sets it; with fewer it is minMark, as before 0.20.1.
// The weakest start marks in the recorded corpus are 812+ counts for QRE in
// the 2026 enclosure, 84-96 for older QRE builds and 63-65 for TCRT, so the
// floor only ever asks a unit for half of what its own pulls give.
// It is kept for one sensor profile, cleared when the profile changes, and
// saved with the NVS mirror at sleep and power-off (its own key, which older
// firmware ignores; Wipe all clears its namespace).
namespace StartMarkReference {
constexpr unsigned COUNT=5,MIN_PULLS=3;
constexpr float CEILING=400;
constexpr unsigned IMAGE_BYTES=18;
using Image=uint8_t[IMAGE_BYTES];

struct Reference {
  uint8_t profile=0,count=0,next=0;
  uint16_t marks[COUNT]={};

  void clear(uint8_t forProfile){*this=Reference();profile=forProfile;}
  // A confirmed pull's weakest start mark, in ADC counts.
  void add(float mark) {
    marks[next]=uint16_t(std::lround(std::clamp(mark,0.0f,4095.0f)));
    next=uint8_t((next+1)%COUNT);
    if(count<COUNT)++count;
  }
  // The median of the kept marks (the lower middle of an even count); 0 with
  // fewer than MIN_PULLS.
  float median()const {
    if(count<MIN_PULLS)return 0;
    uint16_t sorted[COUNT];std::copy(marks,marks+count,sorted);std::sort(sorted,sorted+count);
    return float(sorted[(count-1)/2]);
  }
  // The start floor while a sound plays.
  float floor(float minMark)const {
    const float m=median();
    if(m<=0)return minMark;
    return std::clamp(.5f*m,minMark,std::max(minMark,CEILING));
  }
  // 'S','M', version 1, profile, count, next, five marks (16 bits each,
  // little endian), and a 16-bit checksum of the bytes before it.
  void encode(Image &out)const {
    std::memset(out,0,IMAGE_BYTES);
    out[0]='S';out[1]='M';out[2]=1;out[3]=profile;out[4]=count;out[5]=next;
    for(unsigned k=0;k<COUNT;++k){out[6+2*k]=uint8_t(marks[k]);out[7+2*k]=uint8_t(marks[k]>>8);}
    const uint16_t sum=checksum(out);out[16]=uint8_t(sum);out[17]=uint8_t(sum>>8);
  }
  // False (and unchanged) unless the image is whole and valid.
  bool decode(const uint8_t *in,unsigned length) {
    if(length!=IMAGE_BYTES || in[0]!='S' || in[1]!='M' || in[2]!=1 || in[4]>COUNT || in[5]>=COUNT)return false;
    if((uint16_t(in[16])|uint16_t(in[17])<<8)!=checksum(in))return false;
    Reference r;r.profile=in[3];r.count=in[4];r.next=in[5];
    for(unsigned k=0;k<COUNT;++k){r.marks[k]=uint16_t(in[6+2*k]|in[7+2*k]<<8);if(r.marks[k]>4095)return false;}
    *this=r;return true;
  }
private:
  static uint16_t checksum(const uint8_t *b) {
    uint16_t a=1,c=0;for(unsigned i=0;i<16;++i){a=uint16_t((a+b[i])%251);c=uint16_t((c+a)%251);}
    return uint16_t(c<<8|a);
  }
};
}
