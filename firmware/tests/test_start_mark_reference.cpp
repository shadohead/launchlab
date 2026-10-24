// The start floor while a sound plays (start_mark_reference.h, 0.20.1):
// clamp(0.5 x the median weakest start mark of a unit's last 5 confirmed
// pulls, minMark, 400), needing 3 pulls, kept for one sensor profile; and the
// detector holding back only a launch's start below it.
#include "../LaunchLabMini/start_mark_reference.h"
#include "../LaunchLabMini/sensor_profile.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using StartMarkReference::Reference;

namespace {
// Square marks at `period` us with `amplitude` counts on a 140-count base.
void level(AnalogTachometer &d,unsigned us,int v){for(unsigned i=0;i<us/20;++i)d.feed(uint16_t(v));}
void pull(AnalogTachometer &d,int amplitude) {
  for(unsigned p:{20000u,16000u,13000u,11000u,10000u,10000u,10000u,10000u,11000u,13000u,16000u,20000u})
    {level(d,p/2,140+amplitude);level(d,p-p/2,140);}
  level(d,1700000,140);
}
AnalogTachometer ready(StickS3SensorProfile::Mode mode=StickS3SensorProfile::Mode::Standard) {
  AnalogTachometer d;StickS3SensorProfile::configure(d,mode);d.singleTurnPeak=true;level(d,1200000,140);
  assert(d.phase==AnalogTachometer::Phase::Ready);return d;
}
}

int main() {
  {
    // No reference until 3 pulls: the floor is the profile's own (minMark).
    Reference r;r.clear(0);
    assert(r.floor(80)==80);r.add(1000);r.add(1000);assert(r.median()==0 && r.floor(80)==80);
    r.add(1000);assert(r.median()==1000 && r.floor(80)==400);  // half is 500: held to the 400 ceiling
  }
  {
    // Half the median, never below minMark: an older QRE build's 84-96 count
    // marks and a TCRT's 63-65 keep their own floor (the corpus's weakest).
    Reference qre;qre.clear(0);for(float m:{84.f,90.f,96.f})qre.add(m);assert(qre.floor(80)==80);
    Reference tcrt;tcrt.clear(1);for(float m:{63.f,65.f,64.f})tcrt.add(m);assert(tcrt.floor(60)==60);
    Reference mid;mid.clear(0);for(float m:{500.f,520.f,480.f})mid.add(m);assert(mid.floor(80)==250);
  }
  {
    // One odd pull cannot move it: the median of the last 5.
    Reference r;r.clear(0);for(float m:{900.f,950.f,1000.f,1050.f})r.add(m);
    const float before=r.floor(80);
    r.add(80);assert(r.floor(80)==before);           // a weak odd pull
    Reference s;s.clear(0);for(float m:{180.f,200.f,220.f})s.add(m);
    s.add(4000);assert(s.floor(80)==100);            // a strong odd pull: of 180, 200, 220 and 4000 the lower middle, 200
    // Only the newest 5 count: a run of new pulls replaces the old.
    Reference t;t.clear(0);for(int k=0;k<5;++k)t.add(1000);for(int k=0;k<3;++k)t.add(200);
    assert(t.median()==200 && t.floor(80)==100);
  }
  {
    // The record: a round trip, and anything damaged refused whole.
    Reference r;r.clear(1);for(float m:{63.4f,66.6f,64.f,70.f,71.f,62.f})r.add(m);
    StartMarkReference::Image image;r.encode(image);
    Reference back;assert(back.decode(image,sizeof(image)) && back.profile==1 && back.count==5 && back.median()==r.median());
    for(unsigned i=0;i<StartMarkReference::IMAGE_BYTES;++i) {
      StartMarkReference::Image bad;std::memcpy(bad,image,sizeof(bad));bad[i]^=0x40;
      Reference keep;keep.clear(0);keep.add(500);
      assert(!keep.decode(bad,sizeof(bad)) && keep.count==1);
    }
    Reference keep;assert(!keep.decode(image,sizeof(image)-1));
  }
  {
    // A profile change starts empty.
    Reference r;r.clear(0);for(int k=0;k<5;++k)r.add(1000);r.clear(1);
    assert(r.profile==1 && r.count==0 && r.floor(60)==60);
  }
  {
    // The detector: below the floor a pull's start is held back (counted);
    // at or above it the pull starts and reads exactly as without a floor.
    AnalogTachometer plain=ready(),held=ready(),clears=ready();
    held.startFloor=400;clears.startFloor=400;
    pull(plain,300);pull(held,300);pull(clears,600);
    assert(plain.launches==1 && plain.resultValid && std::fabs(plain.startMark-300)<1);
    assert(held.launches==0 && held.soundHeldWindows>0);
    AnalogTachometer free=ready();pull(free,600);
    assert(clears.launches==1 && clears.resultRpm==free.resultRpm && clears.burstEndUs==free.burstEndUs);
    // Once a launch is confirmed the floor does nothing: raised mid-pull, the
    // pull reads as without it.
    AnalogTachometer late=ready();
    for(unsigned p:{20000u,16000u,13000u,11000u,10000u}){level(late,p/2,440);level(late,p-p/2,140);}
    assert(late.phase==AnalogTachometer::Phase::Launch);late.startFloor=4000;
    for(unsigned p:{10000u,10000u,10000u,11000u,13000u,16000u,20000u}){level(late,p/2,440);level(late,p-p/2,140);}
    level(late,1700000,140);
    AnalogTachometer same=ready();pull(same,300);
    assert(late.launches==1 && late.resultRpm==same.resultRpm);
  }
  std::puts("PASS: the start floor while a sound plays: half a unit's median start mark (3-5 pulls, 400 at most, never below "
            "minMark), one odd pull cannot move it, its record round-trips or is refused whole, a profile change clears it, "
            "and the detector holds back only a start below it");
}
