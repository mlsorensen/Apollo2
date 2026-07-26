#pragma once

#include "core/brew.h"

// Host stand-in for the brew-by-weight controller. On the device this is
// core::BrewController over real paddle + scale ports; here it returns canned
// state so the paddle-status / target-weight / shot-mode UI can be rendered.
// Setters let the sim render the various states (idle, brewing, review, ...).

namespace host {

class FakeBrewController : public core::IBrewController {
 public:
  core::BrewSnapshot snapshot() const override {
    // Same degradation rule as the real controller: kAuto without the wired
    // relay reports (and behaves as) kDetect.
    const bool relay = paddle_hw_ && wired_;
    const core::ShotMode eff =
        (mode_ == core::ShotMode::kAuto && !relay) ? core::ShotMode::kDetect : mode_;
    return core::BrewSnapshot{
        .paddle_hw = paddle_hw_,
        .wired_setting = wired_,
        .paddle_wired = relay && eff != core::ShotMode::kDetect,
        .paddle_pressed = paddle_,
        .brewing = brewing_,
        .phase = phase_,
        .mode = eff,
        .shot_ms = shot_ms_,
        .baseline_set = true,
        .start_weight_g = 0.0f,
        .target_weight_g = target_g_,
        .overshoot_g = 2.0f,
        .review_hold_s = review_hold_s_,
        .review_reject_seq = 0,
        .stop_hint = false,
        .flush_s = flush_s_,
        .flush_delay_s = flush_delay_s_,
        .relay = relay,
        .clean_ready = relay && clean_ready_,
        .manual_flush = manual_flush_,
        .backflush_active = bf_active_,
        .backflush_on = bf_on_,
        .backflush_cycle = bf_active_ ? bf_cycle_ : 0,
        .backflush_phase_ms = bf_phase_ms_,
        .backflush_done = bf_done_,
    };
  }
  void set_target_weight_g(float grams) override { target_g_ = grams; }
  void set_shot_mode(core::ShotMode mode) override { mode_ = mode; }
  void set_review_hold_s(int seconds) override { review_hold_s_ = seconds; }
  void set_wired_paddle(bool on) override { wired_ = on; }
  void set_flush_s(int seconds) override { flush_s_ = seconds; }
  void set_flush_delay_s(int seconds) override { flush_delay_s_ = seconds; }
  void dismiss_review() override {
    if (phase_ == core::ShotPhase::kReview) phase_ = core::ShotPhase::kIdle;
  }
  void toggle_manual_flush() override { manual_flush_ = !manual_flush_; }
  bool start_backflush() override {
    if (!(paddle_hw_ && wired_ && clean_ready_)) return false;
    bf_active_ = true;
    bf_on_ = true;
    bf_cycle_ = 1;
    bf_phase_ms_ = core::kBackflushOnMs;
    bf_done_ = false;
    return true;
  }
  void cancel_backflush() override {
    bf_active_ = false;
    bf_on_ = false;
    bf_cycle_ = 0;
  }

  void set_paddle_hw(bool hw) { paddle_hw_ = hw; }
  void set_paddle(bool p) { paddle_ = p; }
  void set_brewing(bool b) { brewing_ = b; }
  void set_phase(core::ShotPhase ph) { phase_ = ph; }
  void set_shot_ms(uint32_t ms) { shot_ms_ = ms; }
  void set_clean_ready(bool r) { clean_ready_ = r; }
  // Pose a mid-sequence backflush for the render (the real controller advances
  // these from its poll).
  void set_backflush(bool active, int cycle, bool on, uint32_t phase_ms) {
    bf_active_ = active;
    bf_cycle_ = cycle;
    bf_on_ = on;
    bf_phase_ms_ = phase_ms;
  }
  void set_backflush_done(bool done) { bf_done_ = done; }

 private:
  bool paddle_hw_ = true;  // board has the paddle harness (wired-capable)
  bool wired_ = true;      // the "Wired paddle" user setting. NOTE: the DEVICE
                           // default is now OFF (Config::wired_paddle); the sim
                           // keeps true so the standard renders show the wired
                           // look (main.cpp toggles it for the unwired render)
  bool paddle_ = false;
  bool brewing_ = false;
  core::ShotMode mode_ = core::ShotMode::kAuto;  // wired renders show "Auto shot"
  core::ShotPhase phase_ = core::ShotPhase::kIdle;
  uint32_t shot_ms_ = 27000;  // matches the fake scale's 27.0s render
  float target_g_ = 36.0f;
  int review_hold_s_ = 30;
  int flush_s_ = 0;
  int flush_delay_s_ = 3;
  bool clean_ready_ = true;   // machine on, no shot in flight
  bool manual_flush_ = false;
  bool bf_active_ = false;
  bool bf_on_ = false;
  int bf_cycle_ = 0;
  uint32_t bf_phase_ms_ = 0;
  bool bf_done_ = false;
};

}  // namespace host
