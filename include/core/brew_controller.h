#pragma once

#include <cstdint>
#include <functional>

#include "core/brew.h"
#include "core/paddle.h"
#include "core/scale.h"
#include "core/shot_timer.h"

namespace core {

// The brew-by-weight state machine (see brew.h for the lifecycle). Pure logic
// over the IPaddle + IScale ports; the caller pumps poll(now_ms) from its main
// loop (a few ms cadence — paddle sensing is internally throttled) and wires
// the persister callbacks to whatever config store the platform has.
//
// Single-threaded by design: poll() and every setter/snapshot() must run on the
// same task (the device loop / UI task). IScale::snapshot() is internally
// thread-safe against its BLE task; IScale::tare() only sets a flag.
class BrewController : public IBrewController {
 public:
  BrewController(IPaddle& paddle, IScale& scale)
      : paddle_(paddle), scale_(scale) {}

  // Wire before the loop starts (all optional).
  void set_target_persister(std::function<void(float)> p) { persist_target_ = std::move(p); }
  void set_shot_mode_persister(std::function<void(bool)> p) { persist_mode_ = std::move(p); }
  void set_overshoot_persister(std::function<void(float)> p) { persist_overshoot_ = std::move(p); }
  void set_review_hold_persister(std::function<void(int)> p) { persist_review_ = std::move(p); }

  // Seed from persisted config at boot (does not re-persist).
  void seed(float target_g, bool shot_mode, float overshoot_g, int review_hold_s) {
    target_g_ = target_g;
    shot_mode_ = shot_mode;
    overshoot_g_ = overshoot_g;
    review_hold_s_ = review_hold_s;
  }

  void poll(uint32_t now_ms);

  BrewSnapshot snapshot() const override;
  void set_target_weight_g(float grams) override;
  void set_shot_mode(bool on) override;
  void dismiss_review() override;
  void set_review_hold_s(int seconds) override;

 private:
  void end_shot(uint32_t now_ms);  // Brewing -> Review

  IPaddle& paddle_;
  IScale& scale_;
  ShotTimer timer_;
  std::function<void(float)> persist_target_;
  std::function<void(bool)> persist_mode_;
  std::function<void(float)> persist_overshoot_;
  std::function<void(int)> persist_review_;

  ShotPhase phase_ = ShotPhase::kIdle;
  bool shot_mode_ = true;
  bool paddle_on_ = false;    // last sensed level (edge detection)
  bool sensed_once_ = false;  // no edge until the first real read (boot safety)
  bool driving_ = false;      // we currently hold the line closed
  bool auto_stopped_ = false; // this shot was ended by the target (not the paddle)
  float target_g_ = 36.0f;
  // Drip/lag compensation: the shot is stopped overshoot_g_ EARLY, and each
  // auto-stopped shot's settled final weight nudges the estimate (learned, NVS).
  float overshoot_g_ = 2.0f;
  int review_hold_s_ = 30;  // review linger before auto-dismiss (user setting)

  // Brewing bookkeeping: the baseline weight is (re)read ~1.2s after the tare
  // command so a dropped tare can't fake progress ("confirm starting weight").
  float start_g_ = 0.0f;
  bool baseline_set_ = false;
  uint32_t baseline_at_ms_ = 0;

  uint32_t settle_until_ms_ = 0;
  uint32_t review_until_ms_ = 0;
  uint32_t blind_since_ms_ = 0;  // scale dark since (kBrewing); 0 = seeing it
  uint32_t last_sense_ms_ = 0;
  uint32_t last_now_ms_ = 0;  // for snapshot()'s elapsed time

  static constexpr uint32_t kSensePeriodMs = 25;      // paddle poll (edge latency)
  static constexpr uint32_t kBaselineDelayMs = 1200;  // tare settle before baseline
  static constexpr float kPreTaredG = 0.5f;           // |weight| under this = already tared
  static constexpr uint32_t kSettleMs = 3000;         // drip tail before the freeze
  static constexpr uint32_t kBlindGraceMs = 4000;     // tolerated mid-shot scale blackout
  static constexpr float kOvershootLearnRate = 0.5f;  // fraction of the error absorbed per shot
  static constexpr float kOvershootMaxG = 8.0f;       // sanity clamp (0..max)
};

}  // namespace core
