// Streaming a song to the speaker (music_stream.h): what reaches the speaker
// is exactly the song rendered whole, from its first sample or a late
// start's; with the render task polling every 4 ms and the UI feeding
// even once per 31 ms frame it never runs dry; with the device's measured
// render times and a celebration's 60 ms frames, through a model of M5's
// speaker, no silence is heard and the task renders ahead while the UI is
// away; a song starts after its pre-roll; a render that cannot keep up
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
#include <climits>
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

// The speaker as M5's task drives it (Speaker_Class.inl): a queued request
// is copied into the I2S DMA ring (eight 256-frame buffers at 22050 Hz,
// 93 ms: 1486 samples of a 16 kHz song) as room frees, then released; the
// ring plays 16 samples a ms. With the channel empty the task waits 6 ms
// for each buffer of the ring it filled, 48 ms at most (taken as 48 here),
// then writes silence as room frees, which plays ahead of anything queued
// later. `silence`: the samples of it heard between the song's first
// sample and its last.
struct DmaChannel:FakeChannel {
  static constexpr size_t RING=1486,WAIT_MS=48;
  static constexpr int32_t SILENT=INT32_MIN;
  std::deque<int32_t> dma;size_t silence=0,pending=0,emptyMs=0;uint32_t ms=0;Stream *stream=nullptr;
  void tick() {
    while(dma.size()<RING && !held.empty()) {
      Block &b=held.front();dma.push_back(b.p[b.at++]);
      if(b.at==b.n){held.pop_front();if(stream)stream->released(held.empty(),ms);}
    }
    emptyMs=held.empty() && !played.empty()?emptyMs+1:0;
    if(emptyMs>WAIT_MS)while(dma.size()<RING)dma.push_back(SILENT);
    for(int i=0;i<16 && !dma.empty();++i) {
      const int32_t v=dma.front();dma.pop_front();
      if(v==SILENT){++pending;continue;}
      silence+=pending;pending=0;played.push_back(int16_t(v));
    }
    ++ms;
  }
};
// Runs a stream in simulated time against the DmaChannel: each block takes
// walls[k] ms to render (published at its end), the music task sleeping as
// step() asks; the UI feeds on each pass of a frame, `passes` (ms) apart.
struct DeviceRun {uint32_t ms=0,firstQueueMs=0,maxBuffered=0;};
DeviceRun simulateDevice(Stream &stream,DmaChannel &ch,const std::vector<uint32_t> &walls,const std::vector<uint32_t> &passes,uint32_t limitMs) {
  DeviceRun r;uint32_t next=0,doneAt=0,cost=0,reads=0,pass=0,feedAt=0;bool rendering=false;
  ch.stream=&stream;
  for(uint32_t ms=0;ms<limitMs && stream.active();++ms) {
    if(!rendering && ms>=next) {
      // The task renders if a slot is free (by the speaker's count, as step() reads it).
      if(stream.buffered()<Stream::BLOCKS){rendering=true;cost=walls[std::min<size_t>(stream.blocks(),walls.size()-1)];doneAt=ms+cost;}
      else{reads=0;const uint32_t sleep=stream.step([&]{return ms*1000;});next=ms+(sleep?sleep:4);}
    }
    if(rendering && ms>=doneAt) {
      reads=0;const uint32_t sleep=stream.step([&]{return ((reads++)%2?ms:ms-cost)*1000;});
      rendering=false;next=ms+(sleep?sleep:4);
    }
    if(ms>=feedAt) {
      const bool before=stream.begun();stream.feed(ms,ch);
      if(!before && stream.begun())r.firstQueueMs=ms;
      feedAt=ms+passes[pass++%passes.size()];
    }
    r.maxBuffered=std::max(r.maxBuffered,stream.buffered());
    ch.tick();r.ms=ms;
  }
  return r;
}

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
// Runs a stream in simulated time: the music task stepping as step() asks
// (each block taking `costUs` to render), the UI's feed every `feedMs`, the
// channel playing in between. Stops when the stream ends or after `limitMs`;
// `each` runs before every feed.
struct Run {size_t dry=0;uint32_t ms=0;};
template<class Each> Run simulate(Stream &stream,FakeChannel &ch,uint32_t feedMs,uint32_t limitMs,Each each,uint32_t costUs=0) {
  Run r;uint32_t next=0,reads=0;
  auto now=[&]{return (reads++)%2?costUs:0u;};
  for(uint32_t ms=0;ms<limitMs && stream.active();++ms) {
    if(ms>=next){const uint32_t sleep=stream.step(now);next=ms+costUs/1000+(sleep?sleep:4);}
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
    // speaker receives the song exactly and never runs dry while it plays,
    // the task stepping a block at a time with its 1 ms yields.
    for(uint32_t feedMs:{5u,17u,31u}) {
      Stream stream(space.get());FakeChannel ch;
      assert(stream.start(f.data(),f.size(),0,160,ch));
      const Run r=simulate(stream,ch,feedMs,200000,[](uint32_t){});
      assert(!stream.active() && ch.stops==1);
      assert(ch.played==whole);
      // Dry samples only before the first block arrives (the feed after the
      // pre-roll, rendered within 3 ms here) and after the end (the next feed).
      assert(r.dry<=2*size_t(feedMs)*16+16 && stream.underruns()==0);
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
    // The music task is bounded (0.20 gate, 2026-10-23: the 120 s worst-case
    // song took 45% of core 0 and, with the optical and IMU tasks, kept IDLE0
    // from running for 5 s: a task-watchdog reset). A step renders one block
    // at most, then the task sleeps at least 1 ms and long enough that it
    // takes at most DUTY_PERCENT of core 0; a song that needs more underruns.
    const auto f=compileFile(songs[1]);
    Stream stream(space.get());FakeChannel ch;
    assert(stream.start(f.data(),f.size(),0,160,ch));
    // Each block takes `cost` us to render by this clock (read before and after).
    uint32_t clock=0,cost=20000,reads=0;
    auto now=[&]{return clock+((reads++)%2?cost:0);};
    uint64_t busy=0,slept=0;
    for(unsigned k=0;k<Stream::BLOCKS;++k) {
      const uint32_t before=stream.blocks(),ms=stream.step(now);
      assert(stream.blocks()==before+1);  // one block a step
      clock+=cost+ms*1000;
      // The song's first LEAD blocks but the last: a burst of LEAD blocks at most, each followed by 1 ms.
      if(k+1<Stream::LEAD){assert(ms==1);continue;}
      assert(ms>=1 && uint64_t(ms)*1000*Stream::DUTY_PERCENT>=uint64_t(cost)*(100-Stream::DUTY_PERCENT));
      busy+=cost;slept+=uint64_t(ms)*1000;
    }
    assert(busy*100<=(busy+slept)*Stream::DUTY_PERCENT);
    // The ring is full until the speaker has played a request: a short wait, nothing rendered.
    const uint32_t full=stream.blocks();
    assert(stream.step(now)==Stream::FULL_WAIT_MS && stream.blocks()==full);
    stream.feed(0,ch);ch.play(Stream::BLOCK);
    assert(stream.step(now)==Stream::FULL_WAIT_MS && stream.blocks()==full);
    // Rendering that costs nothing still yields for 1 ms, so IDLE0 always runs.
    ch.play(Stream::SPAN*Stream::BLOCK);stream.feed(128,ch);
    cost=0;assert(stream.step(now)==1);
    stream.stop(ch);assert(stream.step(now)==0);  // nothing to do: wait to be woken
  }
  {
    // The worst-case song through a celebration on the device (0.20.8 lab
    // trace, HIL songs run 6): each block took the wall time below to render,
    // 4-27 ms, 15 ms on average, with the optical owner and the IMU
    // preempting the task, and the UI loop fed the speaker on three quick
    // passes per 60 ms frame (40-62 ms apart). 0.20.7 freed a ring slot only at
    // the UI's next feed, with four slots: the task sat idle while the
    // speaker ran dry, and the song underran (4 of 12 such plays).
    // Now the task frees slots from the speaker's own count and renders up
    // to 16 blocks ahead: nothing is heard but the song, and the channel is
    // never empty long enough for M5's task to write silence.
    const std::vector<uint32_t> walls={20,8,23,8,7,8,20,8,7,21,6,6,19,12,20,12,12,21,15,19,8,11,7,9,7,9,23,8,8,24,9,18,23,19,8,6,
      16,7,14,23,19,19,20,12,12,27,19,8,21,17,8,20,18,23,9,23,10,20,19,7,7,7,18,22,19,15,23,19,20,16,11,11,11,16,18,9,20,8,9,9,20,9,19,
      20,8,6,16,7,20,19,23,19,14,23,19,20,20,19,18,19,21,12,20,8,8,23,9,8,18,8,18,7,7,23,20,8,23,23,7,21,16,6,14,20,18,26,21,12,9,20,
      17,27,21,8,23,8,6,18,24,18,23,8,8,7,22,7,15,7,18,12,21,12,12,21,12,12,12,8,26,18,9,8,19,9,8,9,8,19,18,18,6,6,17,19,6,7,14,19,
      12,12,19,19,16,18,12,17,6,14};
    const std::vector<uint32_t> frame={2,2,56};
    const auto f=compileFile(songs[1]);const auto whole=renderWhole(f);
    assert((whole.size()+Stream::BLOCK-1)/Stream::BLOCK==walls.size());  // Night Spin: 6 s, 188 blocks, as the worst-case song
    Stream stream(space.get());DmaChannel ch;
    assert(stream.start(f.data(),f.size(),0,160,ch));
    const DeviceRun r=simulateDevice(stream,ch,walls,frame,200000);
    for(int ms=0;ms<200;++ms)ch.tick();  // the ring plays out
    assert(!stream.active() && ch.played==whole);
    assert(stream.underruns()==0 && ch.silence==0);
    assert(stream.maxDryMs()<DmaChannel::WAIT_MS);
    // The song began after its pre-roll, and the task rendered past four blocks ahead.
    assert(stream.begun() && stream.begunAt()==r.firstQueueMs && r.maxBuffered>4);
    // One 200 ms UI stall mid-song (a long frame and a flash erase): the
    // requests' spans keep the channel fed (single blocks played 50-140 ms
    // of silence for stalls of 110-200 ms).
    std::vector<uint32_t> stalled;
    for(int i=0;i<100;++i)stalled.insert(stalled.end(),{2,2,i==40?200u:56u});
    Stream again(space.get());DmaChannel ch2;
    assert(again.start(f.data(),f.size(),0,160,ch2));
    simulateDevice(again,ch2,walls,stalled,200000);
    for(int ms=0;ms<200;++ms)ch2.tick();
    assert(ch2.played==whole && again.underruns()==0 && ch2.silence==0);
  }
  {
    // While the UI is away (a long frame) the task keeps rendering: a slot
    // frees as soon as the speaker has finished a block, not at the next feed.
    const auto f=compileFile(songs[1]);
    Stream stream(space.get());DmaChannel ch;
    assert(stream.start(f.data(),f.size(),0,160,ch));
    auto now=[]{return 0u;};
    for(unsigned k=0;k<Stream::PREROLL;++k)stream.step(now);
    // The pre-roll is rendered: the first feed queues it as one request, and the song has begun.
    assert(stream.blocks()==Stream::PREROLL && !stream.begun());
    stream.feed(7,ch);
    assert(ch.held.size()==1 && ch.held.front().n==Stream::PREROLL*Stream::BLOCK && stream.begun() && stream.begunAt()==7);
    while(stream.step(now)!=Stream::FULL_WAIT_MS){}
    assert(stream.blocks()==Stream::BLOCKS && stream.buffered()==Stream::BLOCKS);
    // The speaker reads the request; with no feed since, the task fills its slots.
    for(unsigned ms=0;ms<64;++ms)ch.tick();
    assert(ch.held.empty() && stream.buffered()==Stream::BLOCKS-Stream::PREROLL);
    while(stream.step(now)!=Stream::FULL_WAIT_MS){}
    assert(stream.blocks()==Stream::BLOCKS+Stream::PREROLL);
    // The next feed queues spans of SPAN blocks, two requests.
    stream.feed(80,ch);
    assert(ch.held.size()==2 && ch.held[0].n==Stream::SPAN*Stream::BLOCK && ch.held[1].n==Stream::SPAN*Stream::BLOCK);
  }
  {
    // A song shorter than the pre-roll starts once it is all rendered.
    const auto f=FxCompiler::compile("effect \"Blip\"\nuid 0x2\nduration 4000\nreadout 900\nframe { x = W }\n"
      "voice v = tone(wave: square, attack: .001, decay: .02, gain: .3)\n"
      "song(tempo: 240, rows: 4) {\n  pattern p {\n    v: \"c5\"\n  }\n  order p\n}\n");
    assert(f.ok);
    Stream stream(space.get());FakeChannel ch;
    assert(stream.start(f.bytes.data(),f.bytes.size(),0,160,ch));
    simulate(stream,ch,5,2000,[](uint32_t){});
    assert(stream.blocks()<Stream::PREROLL);
    assert(!stream.active() && stream.begun() && ch.played==renderWhole(f.bytes));
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
    // A song too heavy for the duty cap (60 ms to render a 32 ms block, so a
    // block every 150 ms with the task's sleeps) underruns, counted, and still
    // plays to its end: gaps of silence, never a stall.
    const auto f=compileFile(songs[1]);
    Stream stream(space.get());FakeChannel ch;
    assert(stream.start(f.data(),f.size(),0,160,ch));
    simulate(stream,ch,5,2000000,[](uint32_t){},60000);
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
    // The speaker can finish its last block between feed()'s first look at
    // the channel and its end check: the stream ends with every block played
    // and must count them all (HIL songs, 2026-10-24: a 6 s song ended
    // with 187 of its 188 blocks counted).
    struct Racing:FakeChannel {
      unsigned reads=0;
      unsigned queued() override{if(++reads==2)play(SIZE_MAX/2);return FakeChannel::queued();}
      size_t play(size_t samples){size_t n=0;while(samples-- && !held.empty()){Block &b=held.front();played.push_back(b.p[b.at++]);++n;if(b.at==b.n)held.pop_front();}return n;}
    };
    for(const char *path:songs) {
      const auto f=compileFile(path);const auto whole=renderWhole(f);
      Stream stream(space.get());Racing ch;
      assert(stream.start(f.data(),f.size(),0,160,ch));
      simulate(stream,ch,5,200000,[&](uint32_t){ch.reads=0;});
      assert(!stream.active() && ch.played==whole);
      assert(stream.played()==stream.blocks() && stream.blocks()==(whole.size()+Stream::BLOCK-1)/Stream::BLOCK);
    }
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
              "the device's render times through 60 ms frames and a 200 ms stall with M5's speaker: no silence, the task renders ahead while the UI is away; "
              "a song starts after its pre-roll; "
              "a starved render counts underruns; every block counted when the speaker finishes during a feed; a fade ends within 20 ms; stop, restart and refusals\n");
}
