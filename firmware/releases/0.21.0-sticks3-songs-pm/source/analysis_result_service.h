#pragma once
#include "analysis_mode.h"
#include "fixed_riccati.h"
#include "analysis_details.h"
#include "mic_trace.h"
#include "stamina_finish.h"
#include "audio_finish.h"
#include "decay_record.h"
#include <atomic>
#include <new>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace AnalysisResult {
static_assert(MicTrace::ANALYSIS_READINGS == Decay::MAX_SAMPLES);
static_assert(AudioMetadata::LIMIT_MS == AnalysisMode::Session::LIMIT_US / 1000);
// The loop snapshots immutable readings. Only the worker touches unfinished fits.
// A release/acquire completion flag transfers ownership back to the loop.
class Service {
  struct Job {
    bool preview = false;
    Decay::Session input;
    Decay::Workspace *workspace = nullptr;
    Decay::Results results;
    Decay::Curve winner;
    AnalysisDetails::Summary details;
    std::atomic<bool> cancel{false}, done{false};
    int64_t lastYield = 0;
  };
  static constexpr unsigned STACK_BYTES = 8192;
  static_assert(2 * sizeof(Job) + sizeof(Decay::Workspace) + STACK_BYTES <= 120 * 1024,
                "Live refitting exceeds its PSRAM budget");
  Job *job_ = nullptr;
  // Keep the completed plot while its replacement runs. The worker never
  // touches this snapshot, and the exporter still sees only completed jobs.
  Job *previous_ = nullptr;
  // Small, allocation-free fallback if a stopped session cannot start a worker.
  Decay::Results estimate_;
  Decay::Curve estimateCurve_;
  AnalysisDetails::Summary estimateDetails_;
  void *stack_ = nullptr;
  StaticTask_t *block_ = nullptr;
  TaskHandle_t task_ = nullptr;
  uint32_t launch_ = 0;
  bool ready_ = false, failed_ = false, recordPending_ = false;
  bool preview_ = false;
  unsigned snapshotCount_ = 0, lastFitMs_ = 0;
  uint32_t publishedLaunch_ = 0;
  StaminaFinish::Publications publications_;
  StaminaFinish::EarlyReference earlyReference_;
  StaminaFinish::Scratch forecastScratch_;
  AudioFinish::Predictor audioForecast_;
  EarlySpin::Forecast forecast_;
  Decay::RiccatiSupport publishedSupport_{};
  bool needsRecord_ = false;
  Decay::Record::Image record_{};
  static bool proceed(void *context) {
    auto &j = *static_cast<Job *>(context);
    if (j.cancel.load(std::memory_order_relaxed))
      return false;
    const auto now = esp_timer_get_time();
    if (now - j.lastYield >= 4000) {
      vTaskDelay(1);
      j.lastYield = esp_timer_get_time();
    }
    return !j.cancel.load(std::memory_order_relaxed);
  }
  static void entry(void *context) {
    auto &j = *static_cast<Job *>(context);
    auto &workspace = *j.workspace;
    // An interrupted or timed-out recording has no observed endpoint. Keep
    // the bounded live fit; unconstrained post-stop searches add no label.
    j.results = (j.preview || !j.input.hasStop)
                    ? Decay::fitLiveModels(j.input, workspace, j.winner, proceed, &j)
                    : Decay::fitStoppedModels(j.input, workspace, j.winner, proceed, &j);
    // The microphone snapshot is chronological. Match the retained times back
    // to its raw observations so the results graph can show the fit exclusions.
    if (!j.cancel.load(std::memory_order_relaxed) && (!j.preview || j.results.best >= 0)) {
      unsigned retained = 0;
      for (unsigned i = 0; i < j.input.count; ++i) {
        auto &sample = j.input.samples[i];
        sample.excludedFromFit = true;
        if (retained < workspace.count) {
          const float time = workspace.points[retained].t + j.results.acousticStart;
          if (sample.ms == uint32_t(std::lround(time * 1000))) {
            sample.excludedFromFit = false;
            ++retained;
          }
        }
      }
    }
    if (!j.preview && j.results.best >= 0 && proceed(&j)) {
      j.details = AnalysisDetails::summarize(j.winner, j.input.launcherRpm, j.input.stopMs / 1000.f);
      j.details.endpointEstimate = j.results.endpointEstimate;
    }
    j.done.store(true, std::memory_order_release);
    // Loop deletes the suspended worker before reclaiming its stack/TCB.
    for (;;)
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
  void reap() {
    if (task_) {
      vTaskDelete(task_);
      task_ = nullptr;
    }
    heap_caps_free(stack_);
    stack_ = nullptr;
    heap_caps_free(block_);
    block_ = nullptr;
    // The graph owns its raw points and curve. Only the worker needs this buffer.
    if (job_ && job_->workspace) {
      job_->workspace->~Workspace();
      heap_caps_free(job_->workspace);
      job_->workspace = nullptr;
    }
  }
  void releasePrevious() {
    if (previous_) {
      previous_->~Job();
      heap_caps_free(previous_);
      previous_ = nullptr;
    }
  }
  void releasePending() {
    reap();
    if (job_) {
      job_->~Job();
      heap_caps_free(job_);
      job_ = nullptr;
    }
    ready_ = false;
  }
  void release() {
    releasePending();
    releasePrevious();
  }
  void estimateStopped(const AnalysisMode::Session &s, unsigned count) {
    failed_ = true;
    if (s.running() || !s.hasManualStop())
      return;
    if (Decay::FixedRiccati::estimate(s.rpm(), 0, uint32_t(s.elapsedUs(0) / 1000), count, estimate_,
                                      estimateCurve_)) {
      estimateDetails_ = AnalysisDetails::summarize(estimateCurve_, s.rpm(), estimateCurve_.end);
      estimateDetails_.endpointEstimate = true;
      ready_ = needsRecord_ = true;
    }
  }

public:
  struct Plot {
    const Decay::Results *fit = nullptr;
    const Decay::Session *input = nullptr;
    const Decay::Curve *curve = nullptr;
  };
  Plot plot(const AnalysisMode::Session &session) const {
    if (launch_ != session.number() || preview_ != session.running())
      return {};
    if (ready_ && !job_)
      return {&estimate_, nullptr, &estimateCurve_};
    const auto *shown = ready_ ? job_ : previous_;
    if (!shown)
      return {};
    return {&shown->results, &shown->input, shown->results.best >= 0 ? &shown->winner : nullptr};
  }
  bool busy() const { return task_ != nullptr; }
  bool ready() const { return ready_ && !preview_; }
  bool matches(const AnalysisMode::Session &session) const {
    return ready_ && launch_ == session.number() && preview_ == session.running();
  }
  EarlySpin::Forecast forecast(uint32_t launch) const {
    return publishedLaunch_ == launch ? forecast_ : EarlySpin::Forecast{};
  }
  float publishedFinish(uint32_t launch) const { return publishedLaunch_ == launch ? publications_.previous : NAN; }
  Decay::RiccatiSupport publishedSupport(uint32_t launch) const {
    return std::isfinite(publishedFinish(launch)) ? publishedSupport_ : Decay::RiccatiSupport{};
  }
  bool failed() const { return failed_ && !preview_; }
  const Decay::Results *results() const {
    if (!ready_)
      return nullptr;
    return job_ ? &job_->results : &estimate_;
  }
  const Decay::Session *input() const { return ready_ && job_ ? &job_->input : nullptr; }
  const Decay::Curve *winner() const {
    const auto *fit = results();
    if (!fit || fit->best < 0)
      return nullptr;
    return job_ ? &job_->winner : &estimateCurve_;
  }
  const AnalysisDetails::Summary *details() const {
    if (!ready() || !winner())
      return nullptr;
    return job_ ? &job_->details : &estimateDetails_;
  }
  bool recordPending() const { return recordPending_; }
  const Decay::Record::Image &record() const { return record_; }
  void recordHandled() { recordPending_ = false; }
  // Called on every loop, including outside Analysis, so cancelled workers are reaped.
  bool tick(bool open, const AnalysisMode::Session &s, const MicTrace::Trace *trace, uint64_t now = 0,
            const AudioMetadata::Recorder *audio = nullptr) {
    bool changed = false;
    const bool live = s.running();
    const bool currentTrace = trace && trace->launch() == s.number();
    const unsigned rawCount = currentTrace ? trace->rawCount() : 0;
    const bool stale = (job_ || previous_ || ready_) && (!open || s.number() != launch_ || live != preview_);
    if (open && live && publishedLaunch_ != s.number()) {
      publishedLaunch_ = s.number();
      forecast_ = {};
      publishedSupport_ = {};
      publications_ = {};
      earlyReference_ = {};
      audioForecast_ = {};
    }
    if (!open || !live)
      publications_ = {};
    if (stale) {
      if (job_)
        job_->cancel.store(true, std::memory_order_relaxed);
      if (previous_) {
        releasePrevious();
        changed = true;
      }
      if (!job_ && ready_) {
        ready_ = false;
        changed = true;
      }
    }
    if (job_ && (!task_ || job_->done.load(std::memory_order_acquire))) {
      if (stale || job_->cancel.load(std::memory_order_relaxed) || job_->results.cancelled) {
        release();
        changed = true;
      } else if (task_) {
        reap();
        releasePrevious();
        ready_ = true;
        needsRecord_ = !preview_;
        if (preview_) {
          publications_.publish(job_->results.fixedRiccati ? job_->winner.end : NAN);
          publishedSupport_ = job_->results.support;
        }
        changed = true;
      }
    }
    if (!open || !s.number()) {
      if (!job_) {
        launch_ = 0;
        failed_ = false;
      }
      return changed;
    }
    const uint64_t elapsed = live ? s.elapsedUs(now ? now : esp_timer_get_time()) : 0;
    const unsigned ms = unsigned(elapsed / 1000);
    // Inference runs once per clock second, even with no new readings or with
    // a refit in flight. Drawing, replay and flash retries only read the snapshot.
    if (live && ms >= EarlySpin::FIRST_MS && ms / 1000 != forecast_.atMs / 1000) {
      auto reading = [&](unsigned i) {
        const auto rpm = trace->rawRpm(i);
        if (!rpm) return StaminaFinish::Observation{};
        return StaminaFinish::Observation{unsigned(std::lround(MicTrace::Trace::seconds(i) * 1000)),
                                          float(rpm), trace->recovered(i), trace->rejectedRpm(i) != 0};
      };
      const StaminaFinish::LiveFit fit{ms, s.rpm(), publishedSupport_.original, publishedSupport_.accepted};
      const unsigned count = currentTrace ? trace->count() : 0;
      double finish = StaminaFinish::predict(fit, publications_, count, reading, forecastScratch_, earlyReference_);
      if (audio && audio->launch() == s.number())
        finish = audioForecast_.predict(ms, s.rpm(), finish, count, reading,
            [&](unsigned i) { return audio->row(i); }, forecastScratch_.clock);
      forecast_ = {ms, rawCount, float(finish)};
    }
    if (ready_ && s.results() && needsRecord_) {
      const unsigned model = unsigned(std::max(0, results()->best));
      Decay::Record::encode(*results(), model, record_);
      needsRecord_ = false;
      recordPending_ = true;
      changed = true;
    }
    const bool sameAttempt = launch_ == s.number() && preview_ == live;
    if (task_ || (!live && (job_ || sameAttempt)))
      return changed;
    if (live) {
      if (!EarlySpin::refitDue(ms, rawCount, sameAttempt ? lastFitMs_ : 0, sameAttempt ? snapshotCount_ : 0))
        return changed;
      // Let the UI and exporter consume a completed snapshot for one pass,
      // even if many new readings arrived while the worker was fitting.
      if (changed)
        return true;
      if (job_) {
        releasePrevious();
        previous_ = job_;
        job_ = nullptr;
        ready_ = false;
      }
      lastFitMs_ = ms;
    }
    preview_ = live;
    snapshotCount_ = rawCount;
    launch_ = s.number();
    failed_ = false;
    auto *memory = heap_caps_malloc(sizeof(Job), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory) {
      job_ = new (memory) Job;
      auto *scratch = heap_caps_malloc(sizeof(Decay::Workspace), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (scratch)
        job_->workspace = new (scratch) Decay::Workspace;
    }
    stack_ = heap_caps_malloc(STACK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    block_ = static_cast<StaticTask_t *>(
        heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!job_ || !job_->workspace || !stack_ || !block_) {
      releasePending();
      estimateStopped(s, rawCount);
      return true;
    }
    job_->preview = live;
    auto &in = job_->input;
    in.launcherRpm = s.rpm();
    in.stopMs = uint32_t((live ? elapsed : s.elapsedUs(0)) / 1000);
    in.hasStop = !live && s.hasManualStop();
    if (currentTrace)
      for (unsigned i = 0; i < trace->count() && in.count < Decay::MAX_SAMPLES; ++i) {
        const auto rpm = trace->rawRpm(i);
        if (!rpm)
          continue;
        in.samples[in.count++] = {uint32_t(std::lround(MicTrace::Trace::seconds(i) * 1000)),
                                  float(rpm),
                                  trace->rejectedRpm(i) != 0,
                                  false,
                                  trace->recovered(i),
                                  trace->pitchConfidence(i)};
      }
    task_ = xTaskCreateStaticPinnedToCore(entry, "analysis_fit", STACK_BYTES, job_, 0,
                                          static_cast<StackType_t *>(stack_), block_, 1);
    if (!task_) {
      releasePending();
      estimateStopped(s, rawCount);
    }
    return true;
  }
};
} // namespace AnalysisResult
