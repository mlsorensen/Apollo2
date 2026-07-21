#include "core/shot_detector.h"

#include "core/system.h"

namespace core {

void ShotDetector::reset() {
  if (state_ == State::kIdle && hist_n_ == 0) return;  // already pristine — the
  // controller calls this every poll while parked in settle/review/disarmed.
  state_ = State::kIdle;
  hist_n_ = 0;
  hist_head_ = 0;
  last_sample_ms_ = 0;
  blind_since_ms_ = 0;
  cease_since_ms_ = 0;
  last_flow_gps_ = 0.0f;
}

// Trailing-window flow rate over the sample ring: delta-weight over the span
// back to the oldest sample still inside kFlowWindowMs. Also reports that
// reference sample — it doubles as the candidate's retroactive start/baseline.
float ShotDetector::flow_gps(uint32_t now_ms, uint32_t* t_ref, float* w_ref) const {
  int ref = -1;
  for (int i = 0; i < hist_n_; ++i) {
    const int idx = (hist_head_ - hist_n_ + i + 2 * kHistCap) % kHistCap;
    ref = idx;
    if (now_ms - hist_t_[idx] <= kFlowWindowMs) break;
  }
  if (ref < 0) return 0.0f;
  const int newest = (hist_head_ - 1 + kHistCap) % kHistCap;
  *t_ref = hist_t_[ref];
  *w_ref = hist_w_[ref];
  const float span_s = static_cast<float>(hist_t_[newest] - hist_t_[ref]) / 1000.0f;
  if (span_s < kMinSpanS) return 0.0f;
  return (hist_w_[newest] - hist_w_[ref]) / span_s;
}

ShotDetector::Status ShotDetector::poll(uint32_t now_ms, float weight_g,
                                        bool connected) {
  Status st{};

  if (!connected) {
    if (state_ == State::kIdle) {
      reset();  // stale samples must not feed the next flow computation
      return st;
    }
    // Mid-candidate/shot blackout: fly blind briefly (a BLE blip must not kill
    // the classification), abort silently if it sustains — without weight data
    // there is nothing honest left to report.
    if (blind_since_ms_ == 0) {
      blind_since_ms_ = now_ms;
    } else if (now_ms - blind_since_ms_ >= kBlindAbortMs) {
      logf("ShotDetector: scale blackout -> abort\n");
      reset();
      st.event = Event::kAborted;
    }
    return st;
  }
  blind_since_ms_ = 0;

  // Sample at <=10Hz; the state machine advances once per stored sample so the
  // step heuristics ("one sample jumped 5g") mean the same thing at any poll rate.
  if (hist_n_ > 0 && now_ms - last_sample_ms_ < kSampleMs) return st;
  const int prev_idx = (hist_head_ - 1 + kHistCap) % kHistCap;
  const float w_prev = hist_n_ > 0 ? hist_w_[prev_idx] : weight_g;
  const uint32_t t_prev = hist_n_ > 0 ? hist_t_[prev_idx] : now_ms;
  hist_t_[hist_head_] = now_ms;
  hist_w_[hist_head_] = weight_g;
  hist_head_ = (hist_head_ + 1) % kHistCap;
  if (hist_n_ < kHistCap) ++hist_n_;
  last_sample_ms_ = now_ms;

  // The single-sample step heuristics (cup placed / cup pulled) only mean
  // anything between ADJACENT ~100ms samples. Across a tolerated blackout gap
  // the delta spans seconds of legitimate flow — a 3s blip at 2 g/s reads as a
  // 6g "step" — so a gapped sample contributes no step at all.
  const bool step_valid = hist_n_ > 1 && now_ms - t_prev <= 3 * kSampleMs;
  const float step = step_valid ? weight_g - w_prev : 0.0f;
  uint32_t t_ref = now_ms;
  float w_ref = weight_g;
  const float flow = flow_gps(now_ms, &t_ref, &w_ref);
  last_flow_gps_ = flow;

  switch (state_) {
    case State::kIdle:
      if (flow >= kStartFlowGps && step < kStepAbortG) {
        state_ = State::kCandidate;
        t_cand_ = t_ref;  // retroactive: flow was already running at the window's tail
        w_cand_ = w_ref;
        t_cross_ = now_ms;
        logf("ShotDetector: candidate (flow %.2f g/s)\n", static_cast<double>(flow));
      }
      break;

    case State::kCandidate:
      if (step > kStepAbortG || step < kPullNegG) {
        // A cup landing on (or leaving) the scale, not espresso flow.
        state_ = State::kIdle;
      } else if (flow < kKeepFlowGps) {
        state_ = State::kIdle;  // flow died before the confirm — noise
      } else if (now_ms - t_cross_ >= kSustainMs &&
                 weight_g - w_cand_ >= kConfirmNetG) {
        state_ = State::kActive;
        cease_since_ms_ = 0;
        st.event = Event::kStarted;
        st.start_ms = t_cand_;
        st.start_weight_g = w_cand_;
        logf("ShotDetector: shot confirmed (started %ums ago)\n",
             static_cast<unsigned>(now_ms - t_cand_));
      }
      break;

    case State::kActive:
      if (step < kPullNegG) {
        // Cup pulled: end at the previous sample — this one weighs the bare scale.
        st.event = Event::kEnded;
        st.end_ms = t_prev;
        st.end_weight_g = w_prev;
        logf("ShotDetector: cup pulled -> end\n");
        reset();
      } else if (now_ms - t_cand_ >= kRunawayMs) {
        // Minutes of unbroken "flow" is drift or a pour, not espresso.
        logf("ShotDetector: runaway -> abort\n");
        reset();
        st.event = Event::kAborted;
      } else if (flow < kEndFlowGps) {
        if (cease_since_ms_ == 0) {
          cease_since_ms_ = now_ms;
          cease_w_ = weight_g;
        } else if (now_ms - cease_since_ms_ >= kEndSustainMs) {
          st.event = Event::kEnded;
          st.end_ms = cease_since_ms_;  // the timer stops when flow ceased
          st.end_weight_g = cease_w_;
          logf("ShotDetector: flow ceased -> end\n");
          reset();
        }
      } else {
        cease_since_ms_ = 0;
      }
      break;
  }
  return st;
}

}  // namespace core
