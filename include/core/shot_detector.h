#pragma once

#include <cstdint>

// Weight-stream shot detector for "unwired" mode (no paddle harness): infers
// shot start/end purely from the scale's weight stream, so the full shot-review
// UX works on boards that never touch the Micra's paddle circuit.
//
// IMPORTANT: stays free of LVGL/Arduino/BLE/SDL like the other core code.
//
// Lifecycle (v1, confirm-after-sustain):
//   Idle -> Candidate  when the trailing-window flow rate reaches ~0.4 g/s.
//                      The candidate's retroactive start time / baseline weight
//                      are the OLDEST sample of the flow window, so the eventual
//                      timer start predates the confirm delay (honest duration).
//   Candidate -> Started (event) after the flow sustains ~2.5s AND >= ~2g net.
//                      A single-sample step > ~5g aborts (a cup was placed).
//   Started -> Ended (event) when flow stays < ~0.2 g/s for ~2s (the reported
//                      end time is the flow-cease moment, not the confirm), or
//                      immediately on a sharp negative step (cup pulled).
//   A sustained scale blackout or a ~120s runaway aborts silently.
//
// The caller (core::BrewController) applies the review gates (minimum duration/
// weight) and drives the shared shot phases; this class only classifies the
// stream. All thresholds are first-pass guesses pending hardware tuning.

namespace core {

class ShotDetector {
 public:
  enum class Event : uint8_t {
    kNone,     // nothing happened this poll
    kStarted,  // shot confirmed; start_ms/start_weight_g are the retro start
    kEnded,    // shot ended; end_ms = flow-cease time, end_weight_g = final
    kAborted,  // candidate/active state discarded (blackout, runaway, step)
  };

  struct Status {
    Event event = Event::kNone;
    uint32_t start_ms = 0;     // kStarted: retroactive shot start
    float start_weight_g = 0;  // kStarted: baseline weight (delta-based, no tare)
    uint32_t end_ms = 0;       // kEnded: when flow actually ceased
    float end_weight_g = 0;    // kEnded: settled-enough final weight
  };

  // Feed the latest scale reading (~every poll; internally sampled at <=10Hz).
  // now_ms must be the caller's monotonic poll clock. Events are edge-reported
  // exactly once.
  Status poll(uint32_t now_ms, float weight_g, bool connected);

  // Discard any candidate/active state (mode flip, shot-mode disarm, review).
  void reset();

  bool active() const { return state_ == State::kActive; }

  // The most recent trailing-window flow rate (g/s; 0 until warmed up). The
  // stop-early hint uses it to lead the target by the user's reaction time.
  float current_flow_gps() const { return last_flow_gps_; }

 private:
  enum class State : uint8_t { kIdle, kCandidate, kActive };

  float flow_gps(uint32_t now_ms, uint32_t* t_ref, float* w_ref) const;

  State state_ = State::kIdle;

  // Small (t, w) ring for the trailing-window flow derivative (like the UI's,
  // but self-contained — core cannot reach the UI's history).
  static constexpr int kHistCap = 16;              // 16 x 100ms = 1.6s
  static constexpr uint32_t kSampleMs = 100;       // <=10Hz sampling
  static constexpr uint32_t kFlowWindowMs = 1000;  // derivative window
  static constexpr float kMinSpanS = 0.4f;         // need this much for a rate
  uint32_t hist_t_[kHistCap] = {};
  float hist_w_[kHistCap] = {};
  int hist_n_ = 0;
  int hist_head_ = 0;
  uint32_t last_sample_ms_ = 0;

  uint32_t blind_since_ms_ = 0;  // scale dark since; 0 = seeing it
  float last_flow_gps_ = 0.0f;   // latest computed rate (current_flow_gps)

  // Candidate bookkeeping.
  uint32_t t_cand_ = 0;    // retroactive start (oldest flow-window sample)
  float w_cand_ = 0.0f;    // baseline weight at t_cand_
  uint32_t t_cross_ = 0;   // when flow first crossed the start threshold
                           // (the sustain clock; t_cand_ is earlier)

  // Active bookkeeping.
  uint32_t cease_since_ms_ = 0;  // flow below the end threshold since; 0 = flowing
  float cease_w_ = 0.0f;         // weight when the cease window opened

  // Thresholds — HW-tuning guesses (see the class comment).
  static constexpr float kStartFlowGps = 0.4f;   // Idle -> Candidate
  static constexpr float kKeepFlowGps = 0.3f;    // candidate dies below this
  static constexpr uint32_t kSustainMs = 2500;   // candidate confirm delay
  static constexpr float kConfirmNetG = 2.0f;    // net gain required to confirm
  static constexpr float kStepAbortG = 5.0f;     // single-sample step = cup placed
  static constexpr float kEndFlowGps = 0.2f;     // active -> ending below this
  static constexpr uint32_t kEndSustainMs = 2000;  // cease confirm delay
  static constexpr float kPullNegG = -5.0f;      // sharp negative = cup pulled
  static constexpr uint32_t kBlindAbortMs = 4000;  // tolerated scale blackout
  static constexpr uint32_t kRunawayMs = 120000;   // sanity cap on a "shot"
};

}  // namespace core
