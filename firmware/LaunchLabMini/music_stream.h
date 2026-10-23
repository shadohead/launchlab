#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "fx_effect.h"

// Streams a loaded effect's song (engine level 2, docs/FIRMWARE_SOUND_0_20.md)
// to the speaker: a song runs up to 120 s, far past what the speaker's
// pre-rendered buffers hold (2.8 s), so it is rendered as it plays.
//
// render() runs in its own task on core 0 at priority 1, below the optical
// ADC owner and the IMU, so neither can be delayed by music: it renders
// BLOCK samples at a time into a ring of BLOCKS, from its own copy of the
// file with its own program and renderer, never sharing the UI core's.
// feed() runs in the UI loop on core 1, where every speaker call is made:
// it hands rendered blocks to the speaker's channel, keeping QUEUED there,
// and learns which have played. Four 32 ms blocks ahead and two queued
// cover a 31 ms UI frame and a 30 ms flash erase. A block the speaker needs
// before it is rendered is an underrun: silence, counted, never a stall.
//
// fade() lowers the channel's volume to 0 over FADE_MS from the next feed
// on, then stops it: a pull's start silences music within 20 ms without a
// click and without bus traffic.
//
// The output is the speaker's channel (M5.Speaker on the device, a fake on
// the host): how many of our blocks it still holds, queue one, set its
// volume, stop it.
struct MusicOutput {
  virtual unsigned queued()=0;
  virtual bool queue(const int16_t *samples,size_t count)=0;
  virtual void volume(uint8_t v)=0;
  virtual void stop()=0;
};

template<class Lock> class MusicStream {
public:
  static constexpr unsigned BLOCK=512,BLOCKS=4,QUEUED=2;
  static constexpr uint32_t FADE_MS=20;
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
    skip_=skip;filled_=0;fed_=0;played_=0;done_=false;volume_=volume;fadeStart_=0;fading_=false;underruns_=0;
    std::fill(std::begin(length_),std::end(length_),0u);
    out.volume(volume);
    active_=true;++plays_;
    return true;
  }
  // Core 0 (the music task): renders every free block; true while there is
  // more to render, so the task knows whether to keep polling.
  bool render() {
    typename Lock::Hold hold(lock_);
    if(!active_)return false;
    LoadableFx::Sound &s=space_->sound;
    while(skip_ && !s.done()) {
      // A late start: the samples before `skip` are rendered and dropped.
      int16_t scratch[64];const size_t n=s.run(scratch,std::min<size_t>(skip_,64));skip_-=uint32_t(n);if(!n)break;
    }
    // A slot is free once its block has played (or was never fed); only this
    // task writes a free slot, and it publishes the block after writing it.
    while(!s.done() && filled_.load()-played_.load()<BLOCKS) {
      const unsigned k=filled_.load()%BLOCKS;
      length_[k]=uint32_t(s.run(space_->ring[k],BLOCK));
      if(!length_[k])break;
      filled_.fetch_add(1);
    }
    done_=s.done();
    return !done_;
  }
  // Core 1 (each UI loop pass): what the channel has played, the next
  // blocks queued, the fade, and the end.
  void feed(uint32_t nowMs,MusicOutput &out) {
    if(!active_)return;
    const unsigned held=out.queued();
    // Everything fed but no longer held has played.
    played_=fed_-std::min<uint32_t>(held,fed_);
    if(fading_) {
      const uint32_t t=nowMs-fadeStart_;
      if(t>=FADE_MS){stop(out);return;}
      out.volume(uint8_t(uint32_t(volume_)*(FADE_MS-t)/FADE_MS));
    }
    // Read without the lock: the render task only adds blocks after these.
    const bool done=done_.load();const uint32_t filled=filled_.load();
    while(out.queued()<QUEUED && fed_<filled) {
      const unsigned k=fed_%BLOCKS;
      if(!out.queue(space_->ring[k],length_[k]))break;
      ++fed_;
    }
    // The speaker ran dry while blocks are still to come: an underrun, once
    // playing has begun (the first feed can precede the first render).
    if(fed_ && !out.queued() && fed_==filled && !done)++underruns_;
    if(done && fed_==filled && !out.queued()){active_=false;out.stop();}
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
private:
  Space *space_;Lock lock_;
  std::atomic<bool> active_{false},done_{false};bool fading_=false;
  uint32_t skip_=0,fed_=0,fadeStart_=0,underruns_=0,plays_=0;std::atomic<uint32_t> filled_{0},played_{0};
  uint32_t length_[BLOCKS]={};uint8_t volume_=0;
};
