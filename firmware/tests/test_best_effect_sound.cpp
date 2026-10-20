#include "best_effect_sound.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
  using Appearance::Effect;
  assert(BestEffectSound::samples(Effect::Off)==0 && BestEffectSound::samples(Effect::Random)==0);
  for(unsigned e=0;e<Appearance::DRAWN;++e) {
    const auto effect=Appearance::drawnEffect(e);const size_t n=BestEffectSound::samples(effect);
    assert(n==Appearance::duration(effect)*BestEffectSound::RATE/1000);
    std::vector<int16_t> a(n),b(n);
    BestEffectSound::render(effect,a.data(),n);BestEffectSound::render(effect,b.data(),n);
    assert(a==b);
    int peak=0;double energy=0;
    for(const int v:a){peak=std::max(peak,std::abs(v));energy+=double(v)*v;}
    const double rms=std::sqrt(energy/n);
    // Soft limit keeps headroom; every sound is clearly audible; no click at
    // either end.
    assert(peak<=int(.96*32767) && rms>=1500);
    assert(std::abs(int(a[0]))<2000 && std::abs(int(a[n-1]))<300);
    // Rendered in uneven slices, as the sketch does while idle, the samples match.
    std::vector<int16_t> sliced(n);BestEffectSound::Synth synth(effect,n);size_t at=0,step=1;
    while(!synth.done()){at+=synth.run(sliced.data()+at,std::min(step,n-at));step=step*3+7;}
    assert(at==n && sliced==a && synth.run(sliced.data(),1)==0);
    // A shorter buffer is filled exactly, never overrun (ASan checks the bound).
    std::vector<int16_t> part(n/3);BestEffectSound::render(effect,part.data(),part.size());
  }
  std::cout<<"PASS: six deterministic effect sounds at effect length, soft-limited, audible, click-free, resumable slices, bounded writes\n";
}
