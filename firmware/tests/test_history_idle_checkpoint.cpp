#include "../LaunchLabMini/history_checkpoint.h"
#include "../LaunchLabMini/sensor_profile.h"
#include <cassert>
#include <iostream>
static void level(AnalogTachometer &d,unsigned value,unsigned us){for(unsigned i=0;i<us/20;++i)d.feed(value);}
static void square(AnalogTachometer &d,unsigned count,unsigned amplitude=100){
  for(unsigned i=0;i<count;++i){level(d,140+amplitude,5000);level(d,140,5000);}
}
int main(){
  using Phase=AnalogTachometer::Phase;
  assert(!HistoryCheckpoint::practiceIdle(9999,0,10000,false,false,Phase::Ready));
  assert(HistoryCheckpoint::practiceIdle(10000,0,10000,false,false,Phase::Ready));
  for(Phase phase:{Phase::Launch,Phase::Hold,Phase::Settling})assert(!HistoryCheckpoint::practiceIdle(60000,0,10000,false,false,phase));
  assert(!HistoryCheckpoint::practiceIdle(60000,0,10000,true,false,Phase::Ready));
  assert(!HistoryCheckpoint::practiceIdle(60000,0,10000,false,true,Phase::Ready));
  assert(HistoryCheckpoint::practiceIdle(8999,UINT32_MAX-1000,10000,false,false,Phase::Ready));
  // Automatic checkpoints keep the short quiet only for a launcher the IMU has
  // shown motion-free for 2 s (set down); handheld or unknown waits 60 s.
  // Stillness first seen now is not 2 s old (an off-by-one stamp ahead of now
  // wrapped and allowed the short quiet at once).
  {
    HistoryCheckpoint::Stillness imu;assert(HistoryCheckpoint::automaticQuietMs(50000,imu,0,10000)==60000);
    imu.observe(48000,true);imu.observe(49000,true);
    assert(HistoryCheckpoint::automaticQuietMs(48000,imu,0,10000)==60000);
    assert(HistoryCheckpoint::automaticQuietMs(49999,imu,0,10000)==60000);
    assert(HistoryCheckpoint::automaticQuietMs(50000,imu,0,10000)==10000);
    imu.observe(50020,false);assert(HistoryCheckpoint::automaticQuietMs(50020,imu,0,10000)==60000);
    imu.observe(UINT32_MAX-1000,true);assert(HistoryCheckpoint::automaticQuietMs(1000,imu,0,10000)==10000);
    // Unsaved data older than three minutes is written soon after a pull,
    // handheld or not, so a handled bout is never only in RAM.
    HistoryCheckpoint::Stillness handheld;
    assert(HistoryCheckpoint::automaticQuietMs(50000,handheld,179999,10000)==60000);
    assert(HistoryCheckpoint::automaticQuietMs(50000,handheld,180000,10000)==3000);
    assert(HistoryCheckpoint::automaticQuietMs(50000,imu,180000,10000)==3000);
  }
  AnalogTachometer d;StickS3SensorProfile::configure(d);
  assert(!HistoryCheckpoint::allowedIdle(d,0)); // Starting is not safe.
  level(d,140,1200000);assert(HistoryCheckpoint::allowedIdle(d,0));
  // First strong mark precedes any completed cycle or confirmed launch.
  level(d,240,2000);assert(d.phase==AnalogTachometer::Phase::Ready);
  assert(!HistoryCheckpoint::allowedIdle(d,0));
  level(d,140,8000);square(d,1);
  assert(d.phase==AnalogTachometer::Phase::Ready && !HistoryCheckpoint::allowedIdle(d,0));
  square(d,10);assert(d.phase==AnalogTachometer::Phase::Launch && !HistoryCheckpoint::allowedIdle(d,0));
  // Marks stopping at peak speed end the burst once the next marks fail to
  // continue it (50 ms here), not at the plain two-turn gap.
  level(d,140,60000);assert(d.launches==1 && d.phase==AnalogTachometer::Phase::Hold);
  assert(!HistoryCheckpoint::allowedIdle(d,1));
  level(d,140,1200000);assert(d.phase==AnalogTachometer::Phase::Ready);
  assert(!HistoryCheckpoint::allowedIdle(d,0)); // Accepted after UI snapshot: reject stale counter.
  assert(HistoryCheckpoint::allowedIdle(d,1));
  // A stopped incomplete attempt clears its candidate and eventually permits a save.
  square(d,2);assert(!HistoryCheckpoint::allowedIdle(d,1));
  level(d,140,200000);assert(HistoryCheckpoint::allowedIdle(d,1));
  // Weak periodic background is not an accepted candidate and cannot starve saves.
  square(d,10,30);assert(d.phase==AnalogTachometer::Phase::Ready && HistoryCheckpoint::allowedIdle(d,1));
  d.signalFault=true;assert(!HistoryCheckpoint::allowedIdle(d,1));d.signalFault=false;
  {
    // An automatic save taken from idle Ready resumes Ready, so a pull that
    // starts right after the flash write is measured (0.10.21 lost it).
    level(d,140,1200000);assert(d.phase==Phase::Ready && HistoryCheckpoint::allowedIdle(d,1));
    HistoryCheckpoint idle(d);idle.restore(d);assert(d.phase==Phase::Ready);
    level(d,140,1000);square(d,10);level(d,140,60000);
    assert(d.launches==2 && d.resultValid && d.phase==Phase::Hold);
    // The ADC restart transient alone (a single level step) never starts a launch.
    level(d,140,1200000);assert(d.phase==Phase::Ready);
    HistoryCheckpoint again(d);again.restore(d);
    level(d,900,3000);level(d,140,1500000);assert(d.launches==2 && d.phase==Phase::Ready);
    // A checkpoint restored from Settling still requires fresh quiet.
    d.dataLoss();HistoryCheckpoint starting(d);starting.restore(d);assert(d.phase==Phase::Settling);
    level(d,140,1200000);
  }
  d.setEnabled(false);assert(HistoryCheckpoint::allowedIdle(d,2));
  std::cout<<"PASS: automatic idle history guard rejects startup, first strong mark, candidate, confirmed burst, rearm, stale counter and fault; quiet recovery and weak background permit saves\n";
}
