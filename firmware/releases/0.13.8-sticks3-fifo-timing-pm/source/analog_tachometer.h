#pragma once
#include <algorithm>
#include <cmath>
#include <stdint.h>

// AO only. Time is measured in ADC sample positions, not loop/USB timing.
// Analog pulse extraction and first-burst selection are deliberately separate.
class AnalogTachometer {
public:
  enum class Phase { Paused, Settling, Ready, Launch, Hold };
  enum class End { None, Slowdown, Gap, Signal, DataLoss, Cancel };
  static constexpr uint32_t SAMPLE_HZ=50000, QUIET_US=1000000;
  static constexpr uint32_t MIN_PERIOD_US=2000, MAX_PERIOD_US=750000;
  // A launch starts only on a three-revolution window of at least this speed.
  // Through the launcher opening a revolution changes AO by 10-40 counts, and
  // idle AO wanders by the same 11-30 counts over 100-700 ms. Three matching
  // periods of that wander occasionally formed idle 90-640 RPM "launches",
  // indistinguishable from slow rotation by amplitude. Every recorded launch
  // began above 1,180 RPM, so slower windows cannot start a result.
  static constexpr float MIN_LAUNCH_RPM=1000;
  // Every mark in a counted three-revolution window must swing AO by at least
  // this much. In the enclosure (2026-09-26) real revolution marks measured
  // 900-1,100 counts, handling after a pull 100-270, and every idle or
  // picked-up phantom 10-21 counts (edges riding the noise band). The earlier
  // case gave real marks of only 10-40 counts, so its recordings are replayed
  // with minMark=0; a floor cannot separate phantoms from pulls there.
  static constexpr float MIN_LAUNCH_MARK=300;
  float minMark=MIN_LAUNCH_MARK;
  // Board/optics profiles may require more reversal hysteresis for a broad
  // mark with subsidiary peaks. The Waveshare default remains unchanged.
  float edgeSwingFraction=0.30f;
  // Optional board profile: confirm a launch with three consistent turns, then
  // let a clean single complete turn set its peak. Original defaults stay put.
  bool singleTurnPeak=false;
  // Compute the alternative alongside the old result without changing its
  // confirmation, end boundary or rearm timing. M5 selects the display later.
  bool trackSingleTurnPeak=false;
  // A display transfer disturbs AO a few milliseconds later: 1,000+ recorded
  // dips all fell 3.7-10.5 ms after the first sample read once the transfer
  // began. Edge and swing tracking hold their state through this window, so a
  // dip that comes and goes inside it is invisible, while a real transition
  // is still registered (late) when the window ends.
  static constexpr uint32_t BLANK_FROM_US=2500,BLANK_TO_US=14000;
  void displayWrite() {
    if(nowUs>=blankUntil)blankFrom=nowUs+BLANK_FROM_US;
    blankUntil=nowUs+BLANK_TO_US;
  }
  Phase phase=Phase::Settling;
  End ended=End::None;
  bool enabled=true, resultValid=false, signalFault=false;
  float peakRpm=0, resultRpm=0, candidateRpm=0;
  float sustainedPeakRpm=0,singlePeakRpm=0,sustainedPeakAmplitude=0,singlePeakAmplitude=0;
  uint32_t singlePeakUpdates=0,singlePeakRejected=0;
  // Diagnostics for the burst: weakest mark in the fastest window, and the
  // noise band when the burst began. Phantoms sit near the noise band.
  float peakAmplitude=0, startNoiseBand=0;
  float signalLow=0, signalHigh=0, signalContrast=0, noiseBand=3, hysteresis=3;
  uint32_t launches=0, attempts=0, resultRevision=0, cycles=0, revolutions=0;
  uint32_t opticalEdges=0, rejectedPeriods=0, rejectedShape=0, inconsistentWindows=0, slowWindows=0, weakWindows=0;
  uint32_t rearmExtensions=0;
  uint64_t nowUs=0, resultUs=0, burstStartUs=0, burstEndUs=0;
  uint32_t quietMs() const {return uint32_t((nowUs-quietSince)/1000);}
  uint32_t rearmMs() const {return nowUs-quietSince>=QUIET_US?0:uint32_t((QUIET_US-(nowUs-quietSince)+999)/1000);}
  bool historyCheckpointIdle() const {
    if(phase==Phase::Paused)return true;
    if(phase!=Phase::Ready || signalFault || peakPending)return false;
    // Ready can contain the first strong cycles of a new, unconfirmed pull.
    // Weak background cycles do not indefinitely starve an idle history save.
    for(unsigned i=0;i<std::min<uint32_t>(3,nPeriods);++i)if(amplitudes[i]>=minMark)return false;
    const uint64_t edge=std::max(rise,fall);
    const float swing=high?extreme-signalLow:signalHigh-extreme;
    const uint32_t startPeriod=uint32_t(60000000.0f/MIN_LAUNCH_RPM);
    return !edge || nowUs-edge>=startPeriod || swing<minMark;
  }
  void setEnabled(bool value) {
    enabled=value; dataLoss(); ended=End::Cancel;
    phase=value?Phase::Settling:Phase::Paused;
  }
  void dataLoss(bool =true) {
    resultValid=false;resultRpm=peakRpm=sustainedPeakRpm=singlePeakRpm=sustainedPeakAmplitude=singlePeakAmplitude=0;resultRevision++;ended=End::DataLoss;
    phase=enabled?Phase::Settling:Phase::Paused;quietSince=nowUs;
    clearSequence();rise=fall=0;initialized=false;sum=0;count=index=group=0;
    diffCount=diffIndex=0;clearMotion();railSince=0;signalFault=false;swingCount=swingNext=0;
  }
  void feed(uint16_t raw) {
    nowUs+=20;
    if(count<25)count++;else sum-=smooth[index];
    sum+=raw;smooth[index]=raw;index=(index+1)%25;
    if(++group<5)return;group=0;if(count<25)return;
    const float value=float(sum)/25;
    if(!initialized){initialized=true;extreme=swingExtreme=swingPivot=previous=value;high=swingHigh=false;return;}
    // Median slope rejects optical transitions and isolated interference when
    // estimating the background jitter of the 0.5 ms rolling mean.
    diffs[diffIndex]=fabsf(value-previous);diffIndex=(diffIndex+1)%128;
    if(diffCount<128)diffCount++;
    previous=value;
    if(diffCount==128 && diffIndex%32==0) {
      float sorted[128];std::copy(diffs,diffs+128,sorted);
      std::nth_element(sorted,sorted+64,sorted+128);
      noiseBand=fmaxf(3.0f,20.0f*sorted[64]);
    }
    const bool blanked=nowUs>=blankFrom && nowUs<blankUntil;
    if(!blanked)trackSwing(value);
    hysteresis=fmaxf(noiseBand,edgeSwingFraction*pulseSwing());
    // Only the LOW rail is a fault. More reflected IR pulls AO down, and the
    // strongest reflection in the enclosure reads about 130, so a full second
    // near 0 means lost sensor power or wiring. The HIGH rail is an ordinary
    // state there: a shaft resting where little IR returns reads 4095 for as
    // long as it stays put (2026-09-26 recording). Treating it as a fault
    // cleared the previous result and swallowed the next pull, which began
    // inside the rearm second that followed recovery.
    if(value<5) {
      if(!railSince)railSince=nowUs;
      if(nowUs-railSince>=QUIET_US && !signalFault) {
        signalFault=true;resultValid=false;resultRpm=peakRpm=sustainedPeakRpm=singlePeakRpm=sustainedPeakAmplitude=singlePeakAmplitude=0;
        resultRevision++;ended=End::Signal;phase=enabled?Phase::Hold:Phase::Paused;
        clearSequence();clearMotion();rise=fall=0;quietSince=nowUs;
      }
    } else {
      railSince=0;
      if(signalFault){signalFault=false;quietSince=nowUs;extreme=value;rise=fall=0;high=false;}
    }
    if(!signalFault && !blanked) {
      if(high) {
        extreme=fmaxf(extreme,value);
        if(extreme-value>=hysteresis) {
          signalHigh=extreme;
          high=false;extreme=value;fall=nowUs-260;
        }
      } else {
        extreme=fminf(extreme,value);
        if(value-extreme>=hysteresis) {
          signalLow=extreme;signalContrast=fmaxf(0,signalHigh-signalLow);
          high=true;extreme=value;
          const uint64_t stamp=nowUs-260;
          if(rise && fall>rise)cycle(stamp,uint32_t(fall-rise));
          rise=stamp;
        }
      }
    }
    tick();
  }
  const char *stateName() const {
    if(signalFault)return "CHECK SENSOR";
    switch(phase){case Phase::Paused:return "PAUSED";case Phase::Settling:return "STARTING";
      case Phase::Ready:return "READY";case Phase::Launch:return "MEASURING";default:return "REARM";}
  }
  const char *endName() const {
    switch(ended){case End::Slowdown:return "slowdown";case End::Gap:return "gap";
      case End::Signal:return "signal_fault";
      case End::DataLoss:return "data_loss";case End::Cancel:return "cancelled";default:return "none";}
  }
private:
  uint16_t smooth[25]={};uint32_t sum=0;unsigned count=0,index=0,group=0;
  float diffs[128]={},previous=0,extreme=0;
  unsigned diffCount=0,diffIndex=0;
  bool initialized=false,high=false;
  // Every reversal larger than the noise band, independent of the edge
  // hysteresis. A pulse's size comes from these swings rather than an overall
  // min/max envelope: a string pull can shift the whole baseline by hundreds of
  // counts while each revolution's mark changes it by only tens.
  static constexpr unsigned SWINGS=8;
  static constexpr uint32_t SWING_WINDOW_US=1000000;
  float swingSize[SWINGS]={},swingExtreme=0,swingPivot=0;
  uint64_t swingUs[SWINGS]={};
  unsigned swingCount=0,swingNext=0;
  bool swingHigh=false;
  void trackSwing(float value) {
    const bool reversed=swingHigh?swingExtreme-value>=noiseBand:value-swingExtreme>=noiseBand;
    if(swingHigh)swingExtreme=fmaxf(swingExtreme,value);else swingExtreme=fminf(swingExtreme,value);
    if(!reversed)return;
    swingSize[swingNext]=fabsf(swingExtreme-swingPivot);swingUs[swingNext]=nowUs;
    swingNext=(swingNext+1)%SWINGS;if(swingCount<SWINGS)swingCount++;
    swingPivot=swingExtreme;swingExtreme=value;swingHigh=!swingHigh;
  }
  // Third-largest swing of the last second: a revolution repeats its swing, so
  // up to two isolated baseline steps cannot raise the edge hysteresis. Old
  // strong pulses age out, so they cannot hide a weaker next pull either.
  float pulseSwing() const {
    float top[3]={0,0,0};
    for(unsigned i=0;i<swingCount;i++) {
      if(nowUs-swingUs[i]>SWING_WINDOW_US)continue;
      float size=swingSize[i];
      for(unsigned j=0;j<3;j++)if(size>top[j])std::swap(size,top[j]);
    }
    return top[2];
  }
  uint64_t rise=0,fall=0,railSince=0,quietSince=0,lastCycle=0,slowStartUs=0,lastStrongCycle=0;
  uint64_t blankFrom=0,blankUntil=0;
  uint32_t periods[3]={},nPeriods=0,lastPeriod=0,slowCount=0,motionPeriod=0,motionPreviousPeriod=0;
  float motionDuty=0,motionAmplitude=0;
  uint64_t motionStamp=0;
  void clearMotion(){motionPeriod=motionPreviousPeriod=0;motionStamp=0;clearSinglePending();}
  float duties[3]={},amplitudes[3]={};
  float validatedDuty=0,validatedAmplitude=0;
  bool peakPending=false;
  bool peakArrivalChecked=false,splitPending=false;
  uint64_t peakPendingSince=0;
  float pendingRpm=0,pendingAmplitude=0;
  uint32_t pendingPeriod=0,pendingPreviousPeriod=0;
  // Optical edges are evaluated on a 100-us grid. Comparing two adjoining
  // intervals with the preceding one uses three edge timestamps, with up to
  // 200 us of quantization error. This is not an allowed speed-jump limit.
  static constexpr uint32_t SPLIT_TOLERANCE_US=200;
  static bool nearPeriod(uint64_t a,uint64_t b) {
    return a>b?a-b<=SPLIT_TOLERANCE_US:b-a<=SPLIT_TOLERANCE_US;
  }
  void clearSinglePending() {
    peakPending=peakArrivalChecked=splitPending=false;
    pendingPeriod=pendingPreviousPeriod=0;pendingRpm=pendingAmplitude=0;
  }
  void singlePeak(float rpm,float amplitude) {
    if(rpm>singlePeakRpm) {
      singlePeakRpm=rpm;singlePeakAmplitude=amplitude;singlePeakUpdates++;
      if(singleTurnPeak){peakRpm=rpm;peakAmplitude=amplitude;}
    }
  }
  void commitSinglePending() {
    if(peakPending && peakArrivalChecked)singlePeak(pendingRpm,pendingAmplitude);
    clearSinglePending();
  }
  void resolveSingleNeighbor(uint32_t period,bool credible) {
    if(!peakPending)return;
    if(!peakArrivalChecked || !credible) {
      singlePeakRejected++;clearSinglePending();return;
    }
    if(splitPending) {
      // A subdivided mark rejoins the preceding full-turn cadence. Preserve
      // continuing acceleration; only this near-exact split/recovery pattern
      // vetoes the two candidate intervals. Genuine transient motion with the
      // identical optical waveform is necessarily ambiguous.
      if(nearPeriod(period,pendingPreviousPeriod)) {
        singlePeakRejected++;clearSinglePending();
      } else commitSinglePending();
    } else if(pendingPreviousPeriod && pendingPeriod<pendingPreviousPeriod &&
              period<pendingPreviousPeriod &&
              nearPeriod(uint64_t(pendingPeriod)+period,pendingPreviousPeriod)) {
      splitPending=true;
      const float rpm=60000000.0f/period;
      if(rpm>pendingRpm){pendingRpm=rpm;pendingAmplitude=signalContrast;}
      // Validate the second interval's arrival too, even if the first was
      // faster. Neither half may become a committed peak before recovery.
      peakArrivalChecked=false;peakPendingSince=nowUs;
    } else commitSinglePending();
  }
  void offerSingle(float rpm,float amplitude,uint32_t period,uint32_t previousPeriod,uint32_t olderPeriod=0) {
    if(rpm>singlePeakRpm && (!peakPending || rpm>pendingRpm || (!previousPeriod && rpm==pendingRpm))) {
      peakPending=true;peakPendingSince=nowUs;pendingRpm=rpm;pendingAmplitude=amplitude;
      peakArrivalChecked=splitPending=false;pendingPeriod=period;pendingPreviousPeriod=previousPeriod;
      // The first half may be slower than an earlier genuine peak, so it need
      // not have created a candidate. Keep cycle context independently and
      // recognize a split when its second half first sets a new maximum.
      if(previousPeriod && olderPeriod && period<olderPeriod && previousPeriod<olderPeriod &&
         nearPeriod(uint64_t(previousPeriod)+period,olderPeriod)) {
        splitPending=true;pendingPreviousPeriod=olderPeriod;
      }
    }
  }
  void clearSequence(){nPeriods=lastPeriod=slowCount=0;lastCycle=slowStartUs=lastStrongCycle=0;}
  void finish(End reason,uint64_t boundary=0) {
    // The burst ends at the inferred boundary, not at the later instant that
    // a gap / second slow cycle confirms it.
    // No following full cycle is available at a terminal gap. Keep a clean
    // arrival-validated final surge, including two genuine faster turns.
    // A terminal subdivision with identical samples remains indistinguishable.
    // Commit before revision/launch publication; never revise a saved result.
    commitSinglePending();
    burstEndUs=boundary?boundary:reason==End::Slowdown?slowStartUs:lastCycle;
    ended=reason;phase=Phase::Hold;resultUs=nowUs;
    resultValid=peakRpm>0;resultRpm=peakRpm;resultRevision++;
    if(resultValid)launches++;
    clearSequence();
  }
  void tick() {
    if(!enabled || signalFault)return;
    if(peakPending && !peakArrivalChecked && nowUs-peakPendingSince>=500) {
      // Check the arrival mark after the 0.5-ms optical mean settles. A much
      // larger mark crosses the threshold earlier and otherwise fakes a short
      // period, even when the preceding completed mark looked ordinary.
      const float arrival=std::max(0.0f,(high?extreme:signalHigh)-signalLow);
      if(phase==Phase::Launch && arrival>=minMark && validatedAmplitude>0 &&
        arrival<=validatedAmplitude*2.5f && validatedAmplitude<=arrival*2.5f) {
        peakArrivalChecked=true;
        // This peak has already passed three-turn confirmation. Retain its
        // original arrival-only check, even if later activity is malformed.
        if(!pendingPreviousPeriod)commitSinglePending();
      }
      else {singlePeakRejected++;clearSinglePending();}
    }
    if(phase==Phase::Launch || (phase==Phase::Ready && nPeriods)) {
      const uint32_t limit=std::min<uint32_t>(900000,std::max<uint32_t>(30000,2*lastPeriod));
      if(lastCycle && nowUs-lastCycle>limit) {
        if(phase==Phase::Launch)finish(End::Gap);else clearSequence();
      }
    }
    // A shape-valid but subthreshold tail is background, not evidence that
    // the pull continues. Its edges can otherwise suppress the ordinary gap
    // forever. Allow a full quiet second for a temporarily weak signal to
    // recover, then publish the retained peak at the last strong cycle.
    // minMark=0 keeps the earlier weak-optics detector behavior unchanged.
    if(phase==Phase::Launch && minMark>0 && lastStrongCycle && nowUs-lastStrongCycle>=QUIET_US)
      finish(End::Gap,lastStrongCycle);
    if((phase==Phase::Hold || phase==Phase::Settling) && nowUs-quietSince>=QUIET_US) {
      const bool afterPull=phase==Phase::Hold && resultValid;
      phase=Phase::Ready;clearSequence();
      if(afterPull) {
        // Preserve the learned noise floor; discard the completed pull's edge
        // history. Its swings stop counting one second after they occurred.
        clearMotion();extreme=previous;
        signalLow=signalHigh=previous;signalContrast=0;
        high=false;rise=fall=0;
      }
    }
  }
  void cycle(uint64_t stamp,uint32_t highUs) {
    opticalEdges++;
    const uint64_t wide=stamp-rise;
    if(wide<MIN_PERIOD_US || wide>MAX_PERIOD_US) {rejectedPeriods++;nPeriods=0;clearMotion();return;}
    const uint32_t period=uint32_t(wide);
    // Both colors must occupy a meaningful part of the cycle. Narrow electrical
    // dips repeated by display updates are not complete shaft revolutions.
    if(highUs<300 || period-highUs<300 || highUs*20<period || (period-highUs)*20<period) {
      rejectedShape++;nPeriods=0;clearMotion();return;
    }
    const float duty=float(highUs)/period;
    const uint32_t previousPeriod=motionPeriod;
    const uint32_t olderPeriod=motionPreviousPeriod;
    const float previousDuty=motionDuty,previousAmplitude=motionAmplitude;
    candidateRpm=60000000.0f/period;
    if(!enabled || signalFault)return;
    // Rewind needs adjacent, similar full cycles. Never join a pulse to one
    // before a rejected edge/gap: that allowed stray noise to restart the timer.
    const bool adjacent=motionStamp && stamp-motionStamp==period;
    const bool matching=motionPeriod && uint64_t(period)*4<=uint64_t(motionPeriod)*5 &&
      uint64_t(motionPeriod)*4<=uint64_t(period)*5 && fabsf(duty-motionDuty)<=0.20f &&
      signalContrast>0 && motionAmplitude>0 && signalContrast<=motionAmplitude*2.5f &&
      motionAmplitude<=signalContrast*2.5f;
    const bool previousCredible=motionAmplitude>=minMark && validatedAmplitude>0 &&
      fabsf(motionDuty-validatedDuty)<=0.20f &&
      motionAmplitude<=validatedAmplitude*2.5f &&
      validatedAmplitude<=motionAmplitude*2.5f;
    const bool credible=adjacent && previousCredible &&
      signalContrast>=minMark && validatedAmplitude>0 &&
      fabsf(duty-validatedDuty)<=0.20f &&
      signalContrast<=validatedAmplitude*2.5f &&
      validatedAmplitude<=signalContrast*2.5f;
    resolveSingleNeighbor(period,credible);
    if(adjacent && matching && signalContrast>=minMark && motionAmplitude>=minMark) {
      if(phase==Phase::Hold){quietSince=nowUs;rearmExtensions++;}
      else if(phase==Phase::Launch)quietSince=nowUs;
    }
    motionPreviousPeriod=previousPeriod;
    motionPeriod=period;motionDuty=duty;motionAmplitude=signalContrast;motionStamp=stamp;
    if(lastCycle && stamp-lastCycle>uint64_t(period)*3/2) {
      const uint64_t strongCycle=lastStrongCycle;
      clearSequence();
      if(phase==Phase::Launch)lastStrongCycle=strongCycle;
    }
    lastCycle=stamp;
    if(phase==Phase::Launch && signalContrast>=minMark)lastStrongCycle=stamp;
    if(phase==Phase::Launch && sustainedPeakRpm>0) {
      // A one-turn surge must not make ordinary following turns look like
      // rewind. End detection still uses the established three-turn speed.
      const float peakPeriod=60000000.0f/sustainedPeakRpm;
      // Subthreshold marks cannot establish slowdown any more than they can
      // establish a peak. Otherwise two weak turns discard a later valid peak.
      if(signalContrast>=minMark && period>peakPeriod*1.45f) {
        if(!slowCount)slowStartUs=stamp-period;
        slowCount++;
      } else {slowCount=0;slowStartUs=0;}
      if(slowCount>=2){finish(End::Slowdown);return;}
    }
    if((singleTurnPeak || trackSingleTurnPeak) && phase==Phase::Launch && candidateRpm>singlePeakRpm) {
      // Timing, full-cycle shape and neighboring optics passed above. Defer
      // a new maximum for the narrow split/recovery check, without an RPM cap
      // or a three-turn speed-consistency requirement on genuine acceleration.
      if(credible){offerSingle(candidateRpm,signalContrast,period,previousPeriod,olderPeriod);if(singleTurnPeak)quietSince=nowUs;}
      else singlePeakRejected++;
    }
    lastPeriod=period;
    periods[nPeriods%3]=period;duties[nPeriods%3]=duty;
    amplitudes[nPeriods%3]=signalContrast;nPeriods++;
    if(phase==Phase::Launch)revolutions++;
    if(nPeriods<3)return;
    const auto bounds=std::minmax_element(periods,periods+3);
    const auto dutyBounds=std::minmax_element(duties,duties+3);
    const auto ampBounds=std::minmax_element(amplitudes,amplitudes+3);
    if(uint64_t(*bounds.second)*4>uint64_t(*bounds.first)*5 ||
        *dutyBounds.second-*dutyBounds.first>0.20f ||
        *ampBounds.first<=0 || *ampBounds.second>*ampBounds.first*2.5f) {
      inconsistentWindows++;return;
    }
    const bool strong=*ampBounds.first>=minMark;
    // Consistency alone cannot make weak electrical/optical noise postpone
    // startup or rearm. Strong slow rewind still requires fresh quiet.
    if(strong)quietSince=nowUs;
    if(phase!=Phase::Ready && phase!=Phase::Launch)return;
    const float rpm=180000000.0f/(periods[0]+periods[1]+periods[2]);
    const bool starting=phase==Phase::Ready;
    if(phase==Phase::Ready) {
      if(rpm<MIN_LAUNCH_RPM){slowWindows++;return;}
      if(!strong){weakWindows++;return;}
      burstStartUs=stamp-uint64_t(periods[0])-periods[1]-periods[2];
      phase=Phase::Launch;attempts++;peakRpm=sustainedPeakRpm=singlePeakRpm=sustainedPeakAmplitude=singlePeakAmplitude=0;cycles=0;revolutions=3;startNoiseBand=noiseBand;
      lastStrongCycle=stamp;
      resultValid=false;resultRpm=0;ended=End::None;
    }
    if(!strong){weakWindows++;return;} // Weak windows never set the peak.
    if(rpm>sustainedPeakRpm){sustainedPeakRpm=rpm;sustainedPeakAmplitude=*ampBounds.first;}
    validatedDuty=(duties[0]+duties[1]+duties[2])/3;
    validatedAmplitude=amplitudes[0]+amplitudes[1]+amplitudes[2]-*ampBounds.first-*ampBounds.second;
    if(singleTurnPeak || trackSingleTurnPeak) {
      if(starting) {
        const unsigned fastest=unsigned(bounds.first-periods);
        if(fastest==(nPeriods-1)%3) {
          // The other two qualified periods have complete arrival marks.
          // Keep their fastest peak while the latest arrival is checked.
          unsigned completed=(fastest+1)%3;
          const unsigned other=(fastest+2)%3;
          if(periods[other]<periods[completed])completed=other;
          singlePeak(60000000.0f/periods[completed],amplitudes[completed]);
          offerSingle(60000000.0f/periods[fastest],amplitudes[fastest],period,0);
        } else singlePeak(60000000.0f/periods[fastest],amplitudes[fastest]);
      } else if(adjacent && previousAmplitude>=minMark &&
        fabsf(previousDuty-validatedDuty)<=0.20f && fabsf(duty-validatedDuty)<=0.20f &&
        previousAmplitude<=validatedAmplitude*2.5f && validatedAmplitude<=previousAmplitude*2.5f &&
        signalContrast<=validatedAmplitude*2.5f && validatedAmplitude<=signalContrast*2.5f) {
        // A newly validated optical shape can establish fresh context. Do not
        // retroactively promote an earlier edge next to a malformed mark.
        offerSingle(candidateRpm,signalContrast,period,0);
      }
    }
    if(!singleTurnPeak && rpm>peakRpm){peakRpm=rpm;peakAmplitude=*ampBounds.first;}
    cycles=nPeriods;
  }
};
