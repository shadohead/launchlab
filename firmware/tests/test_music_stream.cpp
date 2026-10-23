// Streaming a song to the speaker (music_stream.h): what reaches the speaker
// is exactly the song rendered whole, from its first sample or a late
// start's; with the render task polling every 4 ms and the UI feeding
// even once per 31 ms frame it never runs dry; a render that cannot keep up
// is counted as underruns, never a stall; a fade reaches silence within 20 ms
// and stops; stop and restart begin cleanly; a file without a sound or
// that does not load is refused.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
#include "../tools/fx/fx_compiler.h"
#include "music_stream.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>

using namespace LoadableFx;

namespace {
struct HostLock {std::mutex m;struct Hold {std::mutex &m;explicit Hold(HostLock &l):m(l.m){m.lock();}~Hold(){m.unlock();}};};
using Stream=MusicStream<HostLock>;

// The speaker's channel: holds at most two blocks, plays them at 16 kHz in
// order, and records every sample it plays and every volume it is set to.
struct FakeChannel:MusicOutput {
  struct Block {const int16_t *p;size_t n,at=0;};
  std::deque<Block> held;std::vector<int16_t> played;std::vector<uint8_t> volumes;unsigned stops=0;
  unsigned queued() override{return unsigned(held.size());}
  bool queue(const int16_t *p,size_t n) override{if(held.size()>=2)return false;held.push_back({p,n});return true;}
  void volume(uint8_t v) override{volumes.push_back(v);}
  void stop() override{++stops;held.clear();}
  // Plays `samples` samples; how many it had none for (silence).
  size_t play(size_t samples) {
    size_t dry=0;
    while(samples--) {
      if(held.empty()){++dry;continue;}
      Block &b=held.front();played.push_back(b.p[b.at++]);
      if(b.at==b.n)held.pop_front();
    }
    return dry;
  }
};

std::vector<uint8_t> compileFile(const std::string &path) {
  std::ifstream in(path);std::stringstream s;s<<in.rdbuf();
  const auto r=FxCompiler::compile(s.str());
  if(!r.ok)std::fprintf(stderr,"%s:%d: %s\n",path.c_str(),r.line,r.error.c_str());
  assert(r.ok);return r.bytes;
}
std::vector<int16_t> renderWhole(const std::vector<uint8_t> &f) {
  Program p;assert(load(f.data(),f.size(),p)==Error::None);
  static Sound s;s.begin(p);std::vector<int16_t> out(p.soundSamples());
  for(size_t at=0;at<out.size();)at+=s.run(out.data()+at,out.size()-at);
  return out;
}
// Runs a stream in simulated time: the render task every 4 ms, the UI's feed
// every `feedMs`, the channel playing in between. Stops when the stream
// ends or after `limitMs`; `each` runs before every feed.
struct Run {size_t dry=0;uint32_t ms=0;};
template<class Each> Run simulate(Stream &stream,FakeChannel &ch,uint32_t feedMs,uint32_t limitMs,Each each,uint32_t renderMs=4) {
  Run r;
  for(uint32_t ms=0;ms<limitMs && stream.active();++ms) {
    if(ms%renderMs==0)stream.render();
    if(ms%feedMs==0){each(ms);stream.feed(ms,ch);}
    r.dry+=ch.play(16);r.ms=ms;
  }
  return r;
}
}

int main() {
  auto space=std::make_unique<Stream::Space>();
  const char *songs[]={"firmware/tools/fx/effects/music/victory_lap.fx","firmware/tools/fx/effects/music/night_spin.fx",
    "firmware/tools/fx/effects/music/ripcord.fx"};
  for(const char *path:songs) {
    const auto f=compileFile(path);const auto whole=renderWhole(f);
    // Fed once per 5 ms or once per 31 ms (the slowest UI frame seen): the
    // speaker receives the song exactly and never runs dry while it plays.
    for(uint32_t feedMs:{5u,17u,31u}) {
      Stream stream(space.get());FakeChannel ch;
      assert(stream.start(f.data(),f.size(),0,160,ch));
      const Run r=simulate(stream,ch,feedMs,200000,[](uint32_t){});
      assert(!stream.active() && ch.stops==1);
      assert(ch.played==whole);
      // Dry samples only before the first block arrives (the first feed) and after the end.
      assert(r.dry<=size_t(feedMs)*16+16 && stream.underruns()==0);
    }
    // A late start plays from `skip` samples in.
    {
      Stream stream(space.get());FakeChannel ch;const uint32_t skip=1234;
      assert(stream.start(f.data(),f.size(),skip,160,ch));
      simulate(stream,ch,5,200000,[](uint32_t){});
      assert(ch.played.size()==whole.size()-skip);
      assert(std::equal(ch.played.begin(),ch.played.end(),whole.begin()+skip));
    }
  }
  {
    // On the device the UI's first feed can come before the music task has
    // rendered a block: nothing has played yet, so it is not an underrun
    // (HIL songs run, 2026-10-23: one counted per song with every block played).
    const auto f=compileFile(songs[2]);
    Stream stream(space.get());FakeChannel ch;
    assert(stream.start(f.data(),f.size(),0,160,ch));
    stream.feed(0,ch);stream.feed(1,ch);
    simulate(stream,ch,5,200000,[](uint32_t){});
    assert(stream.underruns()==0 && ch.played==renderWhole(f));
  }
  {
    // A render that cannot keep up (polled every 64 ms, the ring holding 128 ms
    // but playing faster than one block a poll at the end) is counted, not a stall.
    const auto f=compileFile(songs[1]);
    Stream stream(space.get());FakeChannel ch;
    assert(stream.start(f.data(),f.size(),0,160,ch));
    simulate(stream,ch,5,200000,[](uint32_t){},100);
    assert(stream.underruns()>0 && !stream.active());
  }
  {
    // A fade: from the next feed the volume falls to 0 over 20 ms, then the channel stops.
    const auto f=compileFile(songs[0]);
    Stream stream(space.get());FakeChannel ch;
    assert(stream.start(f.data(),f.size(),0,160,ch));
    uint32_t fadeAt=0;
    const Run r=simulate(stream,ch,4,200000,[&](uint32_t ms){if(ms==1000){stream.fade(ms);fadeAt=ms;}});
    assert(!stream.active() && ch.stops==1 && fadeAt==1000);
    assert(r.ms>=1000+Stream::FADE_MS && r.ms<=1000+Stream::FADE_MS+4);
    // The volumes after the start: falling, never rising, to the last before the stop.
    assert(ch.volumes.front()==160);
    for(size_t i=2;i<ch.volumes.size();++i)assert(ch.volumes[i]<=ch.volumes[i-1]);
    assert(ch.volumes.back()<=160/5);
  }
  {
    // Stop is immediate; a restart plays the next song from its first sample.
    const auto a=compileFile(songs[0]),b=compileFile(songs[2]);
    Stream stream(space.get());FakeChannel ch;
    assert(stream.start(a.data(),a.size(),0,160,ch));
    simulate(stream,ch,5,500,[](uint32_t){});
    stream.stop(ch);assert(!stream.active() && ch.held.empty());
    FakeChannel next;assert(stream.start(b.data(),b.size(),0,72,next));
    simulate(stream,next,5,200000,[](uint32_t){});
    assert(next.played==renderWhole(b) && next.volumes.front()==72);
  }
  {
    // Refused: no room, a file that does not load, and a file without a sound.
    FakeChannel ch;Stream none(nullptr);const auto f=compileFile(songs[0]);
    assert(!none.start(f.data(),f.size(),0,160,ch));
    Stream stream(space.get());auto bad=f;bad[40]^=1;
    assert(!stream.start(bad.data(),bad.size(),0,160,ch) && !stream.active());
    const auto silent=FxCompiler::compile("effect \"Quiet\"\nuid 0x1\nduration 3000\nreadout 900\nframe { x = W }\n");
    assert(silent.ok && !stream.start(silent.bytes.data(),silent.bytes.size(),0,160,ch));
  }
  std::printf("PASS: songs reach the speaker exactly (from the start or late), fed every 5-31 ms without running dry; "
              "a starved render counts underruns; a fade ends within 20 ms; stop, restart and refusals\n");
}
