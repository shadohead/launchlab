#include "../LaunchLabRpm/analog_tachometer.h"
#include <cassert>
#include <cmath>
#include <iostream>
using D=AnalogTachometer;
static uint32_t randomState=1337;
static void level(D &d,int value,uint32_t us,int noise=0) {
  for(uint32_t i=0;i<us/20;i++) {
    randomState=randomState*1664525u+1013904223u;
    int jitter=noise?int((randomState>>16)%(noise*2+1))-noise:0;
    d.feed(uint16_t(std::max(0,std::min(4095,value+jitter))));
  }
}
static void cycles(D &d,uint32_t period,unsigned count,int low=250,int high=1250,int noise=0) {
  for(unsigned i=0;i<count;i++){level(d,high,period/2,noise);level(d,low,period-period/2,noise);}
}
static D ready(){D d;level(d,250,1200000);assert(d.phase==D::Phase::Ready);return d;}
// The earlier printed case gave 10-40 count marks. Its regressions exercise
// hysteresis and burst rules with the mark floor disabled, as its replays do.
static D weakOptics(){D d=ready();d.minMark=0;return d;}
static void finish(D &d,int value=250){level(d,value,1600000);}
int main() {
  // Exact period oracle down to the 1,000 RPM launch floor (60 ms periods).
  for(uint32_t period: {4000u,8000u,15000u,40000u,59000u}) {
    D d=ready();cycles(d,period,12);finish(d);
    assert(d.launches==1 && d.resultValid);
    assert(std::fabs(d.resultRpm-60000000.0f/period)<3);
    assert(d.phase==D::Phase::Ready);
  }
  // Slower rotation never starts a launch, however clean. Through the
  // launcher opening its marks are no larger than idle AO wander, which
  // formed 90-640 RPM phantoms (0.5.4 idle flight recording).
  for(uint32_t period: {61000u,100000u,200000u,600000u}) {
    D d=ready();cycles(d,period,12);finish(d);
    assert(d.launches==0 && !d.resultValid && d.slowWindows>0);
    assert(d.phase==D::Phase::Ready);
  }
  // A pull accelerating through the floor starts once a window exceeds it.
  D rising=ready();cycles(rising,150000,4);cycles(rising,80000,4);
  const auto fastBegan=rising.nowUs;cycles(rising,20000,8);finish(rising);
  assert(rising.launches==1 && std::fabs(rising.resultRpm-3000)<2);
  assert(rising.burstStartUs+1000>=fastBegan && rising.burstStartUs<=fastBegan+20000);
  D rearm=ready();cycles(rearm,8000,10);level(rearm,250,60000);
  assert(rearm.launches==1);float result=rearm.resultRpm;
  cycles(rearm,5000,300);level(rearm,250,100000);
  assert(rearm.launches==1 && rearm.resultRpm==result && rearm.phase==D::Phase::Hold);
  const uint32_t remaining=rearm.rearmMs();level(rearm,250,(remaining-1)*1000);
  assert(rearm.phase==D::Phase::Hold);level(rearm,250,2000);assert(rearm.phase==D::Phase::Ready);
  cycles(rearm,10000,12);finish(rearm);assert(rearm.launches==2 && std::fabs(rearm.resultRpm-6000)<2);
  // Wear check (0.20.3): a retraction is the string rewinding from rest. One
  // released from Ready is measured. A slow extension started inside the
  // previous pull's rearm second keeps it armed through its turns, and a
  // release before the rearm ends is that pull's rewind (never reported): the
  // page shows READY only once the phase is Ready.
  for(uint32_t pauseMs:{300u,700u,1100u}) {
    D wear=ready();
    auto retraction=[&](float peak){for(float rpm=700;rpm<peak;rpm*=1.25f)cycles(wear,uint32_t(60e6f/rpm),1);cycles(wear,uint32_t(60e6f/peak),8);};
    retraction(3000);level(wear,250,200000);
    assert(wear.launches==1 && std::fabs(wear.resultRpm-3000)<2 && wear.phase==D::Phase::Hold);
    // strongTurns (0.20.4, read by Wear check) counts every strong turn in
    // any phase: the slow extension's turns in REARM too, weak ones never.
    const uint32_t turns=wear.strongTurns;assert(turns>=12);
    cycles(wear,100000,12);level(wear,250,pauseMs*1000);  // 600 RPM extension, then held out
    const uint32_t extension=wear.strongTurns-turns;assert(extension>=11 && extension<=12);

    const bool armed=wear.phase==D::Phase::Ready;
    assert(armed==(pauseMs>=1100));
    retraction(3200);finish(wear);
    assert(wear.launches==(armed?2u:1u));
    if(armed)assert(std::fabs(wear.resultRpm-3200)<10);
  }
  {
    // Weak turns never count toward strongTurns.
    D weak=ready();cycles(weak,100000,8,250,300);level(weak,250,200000);assert(weak.strongTurns==0);
    cycles(weak,100000,8);assert(weak.strongTurns>=7);
  }
  // Regression: rejected pulses must break the rewind-motion history. In
  // 0.4.0, one plausible cycle after rejected noise matched an OLD period and
  // restarted the countdown (353 ms remaining became 998 ms).
  D stray=ready();cycles(stray,8000,10);level(stray,250,640000);
  assert(stray.phase==D::Phase::Hold && stray.launches==1);
  const uint32_t before=stray.rearmMs(),extensions=stray.rearmExtensions;
  const float saved=stray.resultRpm;
  cycles(stray,1000,2);cycles(stray,8000,1);
  level(stray,1250,1000);level(stray,250,2000);
  assert(stray.rejectedPeriods>=2 && stray.rearmExtensions==extensions);
  assert(stray.rearmMs()+12<=before && stray.resultRpm==saved);
  level(stray,250,350000);
  assert(stray.phase==D::Phase::Ready && stray.rearmMs()==0 && stray.launches==1);
  cycles(stray,10000,12);finish(stray);
  assert(stray.launches==2 && std::fabs(stray.resultRpm-6000)<2);
  // Regression: Ready used to retain the previous large signal envelope
  // (about 105 ADC counts of hysteresis), hiding a valid 12-count next pull.
  for(int jitter: {0,10}) {
    D contrast=weakOptics();cycles(contrast,8000,10,250,1250,jitter);
    level(contrast,250,1050000,jitter);
    assert(contrast.phase==D::Phase::Ready && contrast.launches==1);
    cycles(contrast,10000,20,250,262,jitter);level(contrast,250,1600000,jitter);
    assert(contrast.launches==2 && contrast.resultValid);
    assert(contrast.resultRpm>5600 && contrast.resultRpm<6500);
  }
  // Regression (0.5.2 flight recording): a string pull shifted the whole
  // baseline by ~650 counts, then each revolution changed it by only ~30.
  // Hysteresis from the overall envelope stayed near 100-200 counts for the
  // entire pull and every revolution was missed; later pulls repeated it.
  for(int shift: {650,-200}) {
    D step=weakOptics();level(step,450,200000,3);level(step,450+shift,40000,3);
    cycles(step,22000,20,450+shift-30,450+shift,3);finish(step,450+shift);
    assert(step.launches==1 && std::fabs(step.resultRpm-60000000.0f/22000)<60);
  }
  // Several steps within one pull (up, down, up) still leave the edge
  // hysteresis at the revolution's own swing.
  D steps=weakOptics();level(steps,450,200000,3);
  for(int base: {1100,700,1300}){level(steps,base,30000,3);cycles(steps,9000,8,base-30,base,3);}
  finish(steps,1300);assert(steps.launches==1 && std::fabs(steps.resultRpm-60000000.0f/9000)<120);
  // Successive pulls: a missed or partial attempt with a huge excursion must
  // not suppress the next ordinary pull once its swings age out.
  D successive=weakOptics();
  for(int attempt=0;attempt<3;attempt++) {
    level(successive,450,300000,3);level(successive,2400,150000,3);level(successive,450,1300000,3);
    cycles(successive,15000,12,420,450,3);level(successive,450,1300000,3);
    assert(successive.launches==unsigned(attempt+1));
    assert(std::fabs(successive.resultRpm-4000)<40);
  }
  // Regression (0.5.2 recordings): each display transfer puts a ~1.5 ms dip
  // into AO about 4-10 ms later. With a slowly drifting baseline, drift-made
  // falls paired with dip recoveries into 100 ms "periods": 552-639 RPM
  // phantoms that also held off the next real pull. Blanking removes them.
  for(float drift: {1.5f,2.5f,4.0f}) {
    D display=ready();float base=640;
    for(int i=0;i<80;i++) {
      base-=drift;level(display,int(base),1000,20);display.displayWrite();
      level(display,int(base),5000,20);level(display,int(base)-15,1500,20);
      level(display,int(base),92500,20);
    }
    finish(display,int(base));assert(display.launches==0);
  }
  // A real transition inside a blanking window is registered when it ends.
  D blankPull=ready();
  for(int i=0;i<12;i++){if(i==5)blankPull.displayWrite();cycles(blankPull,20000,1);}
  finish(blankPull);assert(blankPull.launches==1 && std::fabs(blankPull.resultRpm-3000)<5);
  // One 25% slowdown no longer ends an otherwise valid outward burst.
  D load=ready();cycles(load,10000,8);cycles(load,12500,1);cycles(load,9000,8);finish(load);
  assert(load.launches==1 && load.resultRpm>6600 && load.resultRpm<6700);
  // One or two unusually fast turns cannot replace the three-turn peak.
  for(unsigned fastTurns: {1u,2u}) {
    D spike=ready();cycles(spike,10000,8);cycles(spike,4000,fastTurns);
    cycles(spike,10000,8);finish(spike);
    assert(spike.launches==1 && std::fabs(spike.resultRpm-6000)<2);
  }
  // A sustained real increase becomes the peak on the THIRD complete faster
  // period, not the first edge and not after a fixed speed-jump veto.
  D step=ready();cycles(step,10000,8);cycles(step,4000,3);
  assert(step.phase==D::Phase::Launch && std::fabs(step.peakRpm-6000)<2);
  level(step,1250,1000);
  assert(step.phase==D::Phase::Launch && std::fabs(step.peakRpm-15000)<2);
  finish(step);assert(step.launches==1 && std::fabs(step.resultRpm-15000)<2);
  // Regression: a suspect early pulse must not finalize an otherwise ongoing
  // pull. Its later clean, faster section supplies the winning window.
  D late=ready();cycles(late,12000,7);cycles(late,4000,1);
  cycles(late,10000,1);cycles(late,8000,1);cycles(late,6000,1);cycles(late,5000,8);
  assert(late.phase==D::Phase::Launch && late.launches==0 && std::fabs(late.peakRpm-12000)<2);
  finish(late);assert(late.launches==1 && std::fabs(late.resultRpm-12000)<2);
  D accel=ready();cycles(accel,12000,7);cycles(accel,10000,7);cycles(accel,8000,7);finish(accel);
  assert(accel.launches==1 && accel.resultRpm>7400 && accel.resultRpm<7600);
  // Background jitter, narrow periodic interference, and one color change
  // cannot produce a launch. A partial rotation isn't a 3-revolution average.
  D noise=ready();level(noise,250,5000000,10);
  for(int i=0;i<60;i++){level(noise,500,100);level(noise,250,99900);}
  level(noise,500,2000000);finish(noise);assert(noise.launches==0);
  D partial=ready();cycles(partial,10000,3);finish(partial);assert(partial.launches==0);
  D weak=weakOptics();level(weak,220,3000000,10);cycles(weak,10000,20,220,232,10);finish(weak,220);
  assert(weak.launches==1 && weak.resultRpm>5700 && weak.resultRpm<6400);
  // Streaming faults must clear a displayed result and never join timestamps
  // on opposite sides of a pause/drop, including resumed, shifted baselines.
  D loss=ready();cycles(loss,8000,10);loss.dataLoss();finish(loss);
  assert(!loss.resultValid && loss.launches==0);
  cycles(loss,10000,10);finish(loss);assert(loss.launches==1);
  level(loss,0,1200000);assert(loss.signalFault && !loss.resultValid && loss.resultRpm==0);
  level(loss,800,2200000);assert(!loss.signalFault && loss.phase==D::Phase::Ready);
  cycles(loss,10000,12,800,1800);finish(loss,800);assert(loss.launches==2);
  loss.setEnabled(false);cycles(loss,8000,12);finish(loss);assert(loss.launches==2);
  loss.setEnabled(true);finish(loss);cycles(loss,8000,12);finish(loss);assert(loss.launches==3);
  // Motion-window metadata excludes the gap confirmation and slowdown cycles.
  D gapBounds=ready();cycles(gapBounds,8000,12);
  const auto lastForward=gapBounds.nowUs;
  level(gapBounds,250,60000);
  assert(gapBounds.resultValid && gapBounds.ended==D::End::Gap);
  assert(gapBounds.burstStartUs<gapBounds.burstEndUs);
  assert(gapBounds.burstEndUs<=lastForward && gapBounds.resultUs-gapBounds.burstEndUs>=30000);
  D slowBounds=ready();cycles(slowBounds,8000,12);
  const auto slowBegan=slowBounds.nowUs;
  cycles(slowBounds,24000,4);
  assert(slowBounds.resultValid && slowBounds.ended==D::End::Slowdown);
  assert(slowBounds.burstEndUs+1000>=slowBegan && slowBounds.burstEndUs<=slowBegan+1000);
  assert(slowBounds.resultUs-slowBounds.burstEndUs>=47000);
  // Mark floor (0.6.0): enclosure phantoms were clean-looking trains of 10-21
  // count edges; real marks there are 900-1,100 counts. A weak train never
  // starts a launch, however regular, and is counted as weak_rejected.
  for(int mark: {20,120,299}) {
    D phantom=ready();cycles(phantom,10000,12,250,250+mark);finish(phantom);
    assert(phantom.launches==0 && !phantom.resultValid && phantom.weakWindows>0);
    assert(phantom.phase==D::Phase::Ready);
  }
  D floor=ready();cycles(floor,10000,12,250,560);finish(floor);
  assert(floor.launches==1 && std::fabs(floor.resultRpm-6000)<2 && floor.peakAmplitude>=300);
  // Once a strong pull runs, weak faster windows (handling as it stops) cannot
  // set the peak, but they do not end the burst either.
  D tail=ready();cycles(tail,10000,10);cycles(tail,6000,6,250,300);cycles(tail,10000,4);finish(tail);
  assert(tail.launches==1 && std::fabs(tail.resultRpm-6000)<2 && tail.weakWindows>0);
  // A weak phantom must not replace or clear the previous result.
  D keep=ready();cycles(keep,8000,12);finish(keep);const float kept=keep.resultRpm;
  cycles(keep,5000,12,250,270);finish(keep);
  assert(keep.launches==1 && keep.resultValid && keep.resultRpm==kept);
  // A shaft resting at the high rail (little IR returning) is not a fault: it
  // keeps the previous result and a pull starting from it is measured at once.
  D rest=ready();cycles(rest,10000,12,250,1250);level(rest,4095,8000000);
  assert(!rest.signalFault && rest.launches==1 && rest.resultValid && rest.phase==D::Phase::Ready);
  cycles(rest,9000,12,250,4095);finish(rest);
  assert(rest.launches==2 && std::fabs(rest.resultRpm-60000000.0f/9000)<3);
  // Long uptime: sample timestamps must not wrap at millis() or a 32-bit us tick.
  D uptime=ready();uptime.nowUs=uint64_t(1)<<40;finish(uptime);cycles(uptime,8000,12);finish(uptime);
  assert(uptime.launches==1 && std::fabs(uptime.resultRpm-7500)<2);
  std::cout<<"PASS: period oracle 1017-15000 RPM, no launch below 1000 RPM, loaded/accelerating pulls, 3-revolution minimum, rewind, stray-pulse rearm and strong-to-weak signal regressions, baseline steps during and between pulls, display-transfer blanking, one-second rearm, isolated-spike rejection, sustained speed steps and late peaks, jitter/glitches, weak contrast (earlier-case optics), 300-count mark floor, data loss, low-rail fault and recovery, resting at the high rail, pause and long uptime\n";
}
