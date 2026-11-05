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
  // IMU stillness as the UI poll sees it: motion-free since `since`, the poll
  // time that first saw it (never ahead of a later now).
  struct Stillness {
    bool still=false;uint32_t since=0;
    void observe(uint32_t now,bool motionFree){if(!motionFree)still=false;else if(!still){still=true;since=now;}}
    bool settled(uint32_t now,uint32_t ms)const{return still && uint32_t(now-since)>=ms;}
  };
  // Automatic checkpoints (coalesced history, power log) stop the ADC for
  // 115-400 ms, and a pull starting then loses its peak (HIL
  // history_save_pull, power_log_pull). A launcher set down (IMU still for
  // STILL_MS) cannot be picked up and pulled within one write, so it keeps the
  // short quiet. Handheld, or without the IMU, the write waits for a longer
  // pause instead of landing on the usual 10-20 s re-pull time. Unsaved data
  // older than MAX_PENDING_MS (pendingMs) is written at the next idle Ready
  // moment AGED_QUIET_MS after a pull, so a handled bout with pulls every
  // 20-30 s is never left in RAM only (power loss).
  static constexpr uint32_t STILL_MS=2000,HANDHELD_QUIET_MS=60000,MAX_PENDING_MS=180000,AGED_QUIET_MS=3000;
  static uint32_t automaticQuietMs(uint32_t now,const Stillness &imu,uint32_t pendingMs,uint32_t quietMs) {
    if(pendingMs>=MAX_PENDING_MS)return std::min(quietMs,AGED_QUIET_MS);
    return imu.settled(now,STILL_MS)?quietMs:std::max(quietMs,HANDHELD_QUIET_MS);
  }
  // An ADC overflow, read error or malformed frame loses samples. A
  // completed result and an idle Ready survive it as they survive a flash
  // checkpoint (erasing them also put the detector in Settling, swallowing a
  // pull in the next second). A burst in progress cannot be joined across.
  static void sampleGap(AnalogTachometer &d) {
    if(d.phase==AnalogTachometer::Phase::Launch || d.phase==AnalogTachometer::Phase::Settling || d.signalFault)d.dataLoss();
    else HistoryCheckpoint(d).restore(d);
  }
  bool valid=false,rewindPending=false;
  float rpm=0,peak=0,amplitude=0,noise=0,three=0,single=0,threeAmplitude=0,singleAmplitude=0;
  uint32_t revision=0;uint64_t quietUs=0;
  AnalogTachometer::End end=AnalogTachometer::End::None;
  AnalogTachometer::Phase phase=AnalogTachometer::Phase::Paused;
  static bool allowed(const AnalogTachometer &d) {
    return d.phase==AnalogTachometer::Phase::Hold || d.phase==AnalogTachometer::Phase::Paused;
  }
  static bool allowedIdle(const AnalogTachometer &d,uint32_t expectedLaunches) {
    return d.launches==expectedLaunches && d.historyCheckpointIdle();
  }
  explicit HistoryCheckpoint(const AnalogTachometer &d):valid(d.resultValid),rpm(d.resultRpm),
    peak(d.peakRpm),amplitude(d.peakAmplitude),noise(d.startNoiseBand),revision(d.resultRevision),quietUs(d.servedQuietUs()),end(d.ended),phase(d.phase) {
    rewindPending=d.rewindPending;
    three=d.sustainedPeakRpm;single=d.singlePeakRpm;
    threeAmplitude=d.sustainedPeakAmplitude;singleAmplitude=d.singlePeakAmplitude;
  }
  HistoryCheckpoint()=default;
  // pausedUs: how long sampling was stopped. REARM resumes with the quiet it
  // had served plus that pause, and still expects the pull's rewind;
  // restarting a full second swallowed the next pull.
  void restore(AnalogTachometer &d,uint64_t pausedUs=0)const {
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
    if(d.phase==AnalogTachometer::Phase::Hold){d.resumeQuiet(quietUs+pausedUs);d.rewindPending=rewindPending;}
  }
};
