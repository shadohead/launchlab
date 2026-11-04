#pragma once
#include <cstdint>

// Lines a celebration's frames up with its sound. A frame's elapsed time is
// read just before the effect is drawn and the frame is on the glass once the
// canvas is pushed (the present lag, measured each frame). A sound's first
// sample reaches the speaker after the codec starts and after the I2S DMA
// ring ahead of it plays out (the output latency). The effect clock starts so
// the frame showing elapsed e and the sound's sample at e arrive together:
// at the cue it moves to where the sound will actually be heard, and a sound
// that only becomes ready later starts partway in, at the matching sample.
namespace SoundSync {
// The StickS3 speaker as M5Unified sets it up: 22050 Hz out, eight DMA
// buffers of 256 frames (M5Unified.inl, Speaker_Class.hpp).
constexpr uint32_t OUTPUT_RATE=22050,DMA_FRAMES=256,DMA_BUFFERS=8;
// From playRaw to the first sample on the speaker: about one DMA buffer when
// nothing was playing (the ring had drained, or the codec just started), the
// whole ring when it cuts into a sound still playing.
constexpr uint32_t outputUs(bool interrupting) {
  return uint32_t(uint64_t(interrupting?DMA_BUFFERS:1)*DMA_FRAMES*1000000/OUTPUT_RATE);
}
// The effect clock's start (ms) for a sound whose playRaw returned at
// `playedMs`: its sample at elapsed e is heard at playedMs+output+e, and the
// frame showing e is seen present lag after the clock reads e.
constexpr uint32_t clockStart(uint32_t playedMs,uint32_t outputUs,uint32_t presentLagUs) {
  return playedMs+uint32_t(int32_t(int32_t(outputUs)-int32_t(presentLagUs))/1000);
}
// The samples to skip when a sound starts at `nowMs` into an effect whose
// clock started at `startMs`: the sample heard first is the one at the
// elapsed time of the frame on the glass then. 0 if not yet started.
constexpr uint32_t skipSamples(uint32_t nowMs,uint32_t outputUs,uint32_t presentLagUs,uint32_t startMs,uint32_t rate) {
  const int32_t heardAt=int32_t(nowMs-startMs)+int32_t(int32_t(outputUs)-int32_t(presentLagUs))/1000;
  return heardAt>0?uint32_t(uint64_t(heardAt)*rate/1000):0;
}
}
