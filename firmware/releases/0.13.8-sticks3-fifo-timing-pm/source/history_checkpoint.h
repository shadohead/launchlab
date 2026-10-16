#pragma once
#include "analog_tachometer.h"

// A flash write must not defer DMA interrupts through a measured burst.
// Preserve the completed result, then require fresh quiet after the gap.
struct HistoryCheckpoint {
  // Automatic flash checkpoints wait for a genuine pause in practice. A
  // pending-record age must never force a write between consecutive pulls.
  static bool practiceIdle(uint32_t now,uint32_t lastPull,uint32_t quietMs,
                           bool capturing,bool recap,AnalogTachometer::Phase phase) {
    return !capturing && !recap && uint32_t(now-lastPull)>=quietMs &&
      (phase==AnalogTachometer::Phase::Ready || phase==AnalogTachometer::Phase::Paused);
  }
  bool valid=false;
  float rpm=0,peak=0,amplitude=0,noise=0,three=0,single=0,threeAmplitude=0,singleAmplitude=0;
  uint32_t revision=0;
  AnalogTachometer::End end=AnalogTachometer::End::None;
  AnalogTachometer::Phase phase=AnalogTachometer::Phase::Paused;
  static bool allowed(const AnalogTachometer &d) {
    return d.phase==AnalogTachometer::Phase::Hold || d.phase==AnalogTachometer::Phase::Paused;
  }
  static bool allowedIdle(const AnalogTachometer &d,uint32_t expectedLaunches) {
    return d.launches==expectedLaunches && d.historyCheckpointIdle();
  }
  explicit HistoryCheckpoint(const AnalogTachometer &d):valid(d.resultValid),rpm(d.resultRpm),
    peak(d.peakRpm),amplitude(d.peakAmplitude),noise(d.startNoiseBand),revision(d.resultRevision),end(d.ended),phase(d.phase) {
    three=d.sustainedPeakRpm;single=d.singlePeakRpm;
    threeAmplitude=d.sustainedPeakAmplitude;singleAmplitude=d.singlePeakAmplitude;
  }
  HistoryCheckpoint()=default;
  void restore(AnalogTachometer &d)const {
    d.dataLoss();
    d.resultValid=valid;d.resultRpm=rpm;d.peakRpm=peak;d.peakAmplitude=amplitude;
    d.sustainedPeakRpm=three;d.singlePeakRpm=single;
    d.sustainedPeakAmplitude=threeAmplitude;d.singlePeakAmplitude=singleAmplitude;
    d.startNoiseBand=noise;d.resultRevision=revision;d.ended=end;
    // An idle checkpoint is only taken from a verified Ready state with no
    // candidate, so it resumes Ready. Forcing Settling made the detector
    // ignore any pull in the second after each automatic save.
    d.phase=!d.enabled?AnalogTachometer::Phase::Paused:
      (phase==AnalogTachometer::Phase::Hold || phase==AnalogTachometer::Phase::Ready)?phase:
      AnalogTachometer::Phase::Settling;
  }
};
