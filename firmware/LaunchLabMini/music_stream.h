#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "fx_effect.h"

// Streams a loaded effect's song (engine level 2, docs/FIRMWARE_SOUND_0_20.md)
// to the speaker: a song runs up to 120 s, far past what the speaker's
// pre-rendered buffers hold (2.8 s), so it is rendered as it plays.
//
// step() runs in its own task on core 0 at priority 1, below the optical
// ADC owner and the IMU, so neither can be delayed by music: it renders
// BLOCK samples at a time into a ring of BLOCKS, from its own copy of the
// file with its own program and renderer, never sharing the UI core's.
// Above IDLE0, though, the task is bounded: a step renders one block at most
// and the task then sleeps at least 1 ms, in proportion to the block's render
// time, so it is busy at most DUTY_PERCENT of the time and IDLE0 always runs
// (a 45% song in the 0.20 gate, with the optical and IMU tasks, kept IDLE0
// from running for 5 s: a task-watchdog reset). The render time is measured
// on the wall clock, preemption by the optical owner and the IMU included,
// so a song past the render budget the validator holds songs to
// (LoadableFx::SONG_BUDGET, 20% of core 0; a file that skipped the
// validator) underruns, gaps of silence, never a starved core. The pacing
// applies to every block: on the device the worst-case song's blocks take
// 4-34 ms of wall time each, 15 ms on average, for about 6 ms of the task's
// own work (0.20.8 lab trace), so it renders only about 1.3 times as fast
// as it plays, slower than that for a few blocks at a time.
// The ring carries it through those stretches: up to BLOCKS (512 ms) ahead.
// A block's slot is free once the speaker no longer holds it, which the
// task reads from the speaker's own count, never waiting for the UI to
// look (0.20.8: freed only by the UI's next feed, every 40-60 ms through a
// celebration, four slots left the task idle with the speaker running dry,
// a worst-case song underran in about a third of HIL runs). A song's first
// LEAD blocks are rendered back to back, 1 ms apart, a bounded burst per
// song (never mid-song, so a song past the budget still paces): it starts
// once PREROLL of them are rendered, so its first block never plays alone,
// and the rest put it ahead before the first long UI frame.
//
// feed() runs in the UI loop on core 1, where every speaker call is made:
// it hands rendered blocks to the speaker's channel, QUEUED requests at
// most (M5 holds two a channel), each a span of up to SPAN blocks next to
// each other in the ring (128 ms). M5's speaker task copies a request into
// its I2S DMA ring (93 ms) as room frees and then releases it; once its
// channel has been empty for 6 ms per buffer of the ring it filled, 48 ms
// at most, it writes silence, even with real audio still in the ring. With
// up to 256 ms queued and the task ahead from the song's first blocks, a
// celebration's 60 ms UI frames leave the channel empty 24 ms at most, and
// a single 200 ms stall (a frame and a flash erase) none is heard, where
// single-block requests let a 110 ms one through (test_music_stream's
// model of the device). A block the speaker needs before it is rendered is
// an underrun: silence, counted, never a stall.
// released() is told of each request the speaker has finished reading;
// while more is to come, the time from a release that left the channel
// empty to the next request is how close the stream came to that silence
// (maxDryMs()).
//
// fade() lowers the channel's volume to 0 over FADE_MS from the next feed
// on, then stops it: a pull's start silences music within 20 ms without a
// click and without bus traffic.
//
// The output is the speaker's channel (M5.Speaker on the device, a fake on
// the host): how many of our requests it still holds, queue one, set its
// volume, stop it. queued() is also read by the music task on core 0.
struct MusicOutput {
  virtual unsigned queued()=0;
  virtual bool queue(const int16_t *samples,size_t count)=0;
  virtual void volume(uint8_t v)=0;
  virtual void stop()=0;
};

template<class Lock> class MusicStream {
public:
  static constexpr unsigned BLOCK=512,BLOCKS=16,QUEUED=2,SPAN=4,PREROLL=3,LEAD=8;
  static constexpr uint32_t FADE_MS=20;
  // The most of the wall clock the music task may be busy for (it sleeps the
  // rest, so IDLE0 runs whenever the optical owner and the IMU leave room);
  // and the wait before trying again while the ring is full (a block plays in 32 ms).
  static constexpr uint32_t DUTY_PERCENT=60,FULL_WAIT_MS=4;
  // What start() needs: room for the file and the renderer (PSRAM on the
  // device), and the ring.
  struct Space {uint8_t file[LoadableFx::MAX_FILE];LoadableFx::Program program;LoadableFx::Sound sound;int16_t ring[BLOCKS][BLOCK];};
  explicit MusicStream(Space *space):space_(space){}
  bool ready()const{return space_!=nullptr;}

  // Core 1: plays a file's sound from `skip` samples in, at `volume`.
  // False (nothing changes) if it does not load or has no sound.
  bool start(const uint8_t *file,size_t bytes,uint32_t skip,uint8_t volume,MusicOutput &out) {
    if(!space_ || !file || bytes>sizeof(space_->file))return false;
    stop(out);
    typename Lock::Hold hold(lock_);
    std::memcpy(space_->file,file,bytes);
    if(LoadableFx::load(space_->file,bytes,space_->program)!=LoadableFx::Error::None || !space_->program.hasSound)return false;
    space_->sound.begin(space_->program);
    out_=&out;skip_=skip;filled_=0;queue_=0;played_=0;done_=false;volume_=volume;fadeStart_=0;fading_=false;underruns_=0;
    begun_=false;begunAt_=0;dry_=false;maxDryMs_=0;
    std::fill(std::begin(length_),std::end(length_),0u);
    out.volume(volume);
    active_=true;++plays_;
    return true;
  }
  // Core 0 (the music task): one block rendered (or a late start's skipped
  // samples, a block's worth); returns how long to sleep, in ms, before the
  // next step: in proportion to the render time `now` (microseconds) measured,
  // at least 1, or FULL_WAIT_MS while the ring is full; 0 with nothing to do
  // (wait to be woken by start()). The song's first LEAD blocks but the last
  // follow each other after 1 ms: a burst of LEAD blocks at most per song.
  template<class Now> uint32_t step(Now now) {
    const uint32_t t0=now();
    bool lead=false;
    {
      typename Lock::Hold hold(lock_);
      if(!active_ || done_)return 0;
      LoadableFx::Sound &s=space_->sound;
      if(skip_) {
        // A late start: the samples before `skip` are rendered and dropped.
        int16_t scratch[64];size_t left=std::min<size_t>(skip_,BLOCK);
        while(left && !s.done()){const size_t n=s.run(scratch,std::min<size_t>(left,64));if(!n)break;left-=n;skip_-=uint32_t(n);}
        if(s.done())skip_=0;
      } else if(filled_.load()-playedNow()>=BLOCKS) {
        return FULL_WAIT_MS;
      } else {
        // A slot is free once the speaker no longer holds its block (or it was
        // never fed); only this task writes a free slot, and it publishes the
        // block after writing it.
        const unsigned k=filled_.load()%BLOCKS;
        length_[k]=uint32_t(s.run(space_->ring[k],BLOCK));
        if(length_[k])filled_.fetch_add(1);
        lead=filled_.load()<LEAD;
      }
      done_=s.done() && !skip_;
    }
    const uint32_t us=now()-t0;
    if(lead && !done_)return 1;
    return std::max<uint32_t>(1,uint32_t((uint64_t(us)*(100-DUTY_PERCENT)/DUTY_PERCENT+999)/1000));
  }
  // Blocks rendered since start(), and of those the ones the speaker has not
  // finished reading (the ring holds BLOCKS).
  uint32_t blocks()const{return filled_.load();}
  uint32_t buffered()const{return out_?filled_.load()-playedNow():0;}
  // Core 1 (each UI loop pass): what the channel has played, the next
  // blocks queued (from the pre-roll on), the fade, and the end.
  void feed(uint32_t nowMs,MusicOutput &out) {
    if(!active_)return;
    const unsigned held=out.queued();
    // Everything fed but no longer held has played.
    const uint32_t q=queue_.load(),fed=fedOf(q);
    played_=fed-std::min(fed,heldBlocks(q,held));
    if(fading_) {
      const uint32_t t=nowMs-fadeStart_;
      if(t>=FADE_MS){stop(out);return;}
      out.volume(uint8_t(uint32_t(volume_)*(FADE_MS-t)/FADE_MS));
    }
    // Read without the lock: the render task only adds blocks after these.
    const bool done=done_.load();const uint32_t filled=filled_.load();
    // The first block waits for the pre-roll (or the whole of a shorter song).
    if(fed || filled>=PREROLL || done) {
      while(out.queued()<QUEUED) {
        const uint32_t w=queue_.load(),f=fedOf(w);
        // A span: the rendered blocks up to SPAN, not past the ring's end
        // (only a song's last block is short, and it is the last rendered).
        const uint32_t n=std::min({filled-f,uint32_t(SPAN),uint32_t(BLOCKS-f%BLOCKS)});
        if(!n)break;
        size_t samples=0;for(uint32_t i=0;i<n;++i)samples+=length_[(f+i)%BLOCKS];
        if(!out.queue(space_->ring[f%BLOCKS],samples))break;
        if(!f){begun_=true;begunAt_=nowMs;}
        if(dry_.load()){maxDryMs_=std::max(maxDryMs_,nowMs-dryAt_.load());dry_=false;}
        queue_.store(pack(f+n,n,(w>>24)&15));
      }
    }
    // The speaker ran dry while blocks are still to come: an underrun, once
    // playing has begun.
    const uint32_t sent=fedOf(queue_.load());
    if(sent && !out.queued() && sent==filled && !done)++underruns_;
    // The end: the channel holds nothing, so everything fed has played (its
    // last block may have finished since `held` was read above).
    if(done && sent==filled && !out.queued()){played_=sent;active_=false;out.stop();}
  }
  // The speaker's task, once it has finished reading one of the channel's
  // requests; `empty` if the channel holds none now. While more of the song
  // is to come, the channel is empty from then until feed() queues the next.
  void released(bool empty,uint32_t nowMs) {
    const uint32_t fed=fedOf(queue_.load());
    if(!empty || !active_.load() || !fed || (done_.load() && fed==filled_.load()))return;
    dryAt_=nowMs;dry_=true;
  }
  // Core 1: lowers the volume to silence over FADE_MS, then stops.
  void fade(uint32_t nowMs){if(active_ && !fading_){fading_=true;fadeStart_=nowMs;}}
  // Core 1: silence now.
  void stop(MusicOutput &out) {
    typename Lock::Hold hold(lock_);
    if(active_)out.stop();
    active_=false;fading_=false;
  }
  bool active()const{return active_;}
  bool fading()const{return fading_;}
  uint32_t underruns()const{return underruns_;}
  uint32_t plays()const{return plays_;}
  uint32_t played()const{return played_.load();}
  // Whether the song's first block has been queued, and when (feed()'s clock):
  // it is heard from then.
  bool begun()const{return begun_;}
  uint32_t begunAt()const{return begunAt_;}
  // The longest the channel was empty with more to come, this play (ms).
  uint32_t maxDryMs()const{return maxDryMs_;}
private:
  // The blocks fed and the spans of the last two requests, in one word so
  // the music task reads them together: fed in the low 24 bits, the last
  // request's blocks in the next 4, the one before's in the top 4. The
  // speaker holds the latest `held` requests (it plays them in order).
  static uint32_t pack(uint32_t fed,uint32_t last,uint32_t before){return fed|last<<24|before<<28;}
  static uint32_t fedOf(uint32_t q){return q&0xffffff;}
  static uint32_t heldBlocks(uint32_t q,unsigned held){return (held>=1?(q>>24)&15:0)+(held>=2?q>>28:0);}
  // Core 0: the blocks the speaker no longer holds, from its own count.
  // The word is read first: a request queued after that only raises the
  // count, so a slot is never freed early.
  uint32_t playedNow()const{const uint32_t q=queue_.load(),f=fedOf(q);const unsigned h=out_->queued();return f-std::min(f,heldBlocks(q,h));}
  Space *space_;Lock lock_;MusicOutput *out_=nullptr;
  std::atomic<bool> active_{false},done_{false},dry_{false};bool fading_=false,begun_=false;
  uint32_t skip_=0,fadeStart_=0,underruns_=0,plays_=0,begunAt_=0,maxDryMs_=0;
  std::atomic<uint32_t> filled_{0},played_{0},queue_{0},dryAt_{0};
  uint32_t length_[BLOCKS]={};uint8_t volume_=0;
};
