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
    return core::BrewSnapshot{
        .paddle_hw = paddle_hw_,
        .paddle_wired = paddle_hw_ && wired_,
        .paddle_pressed = paddle_,
        .brewing = brewing_,
        .phase = phase_,
        .shot_mode = shot_mode_,
        .shot_ms = shot_ms_,
        .baseline_set = true,
        .start_weight_g = 0.0f,
        .target_weight_g = target_g_,
        .overshoot_g = 2.0f,
        .review_hold_s = review_hold_s_,
        .review_reject_seq = 0,
        .stop_hint = false,
        .flush_s = flush_s_,
    };
  }
  void set_target_weight_g(float grams) override { target_g_ = grams; }
  void set_shot_mode(bool on) override { shot_mode_ = on; }
  void set_review_hold_s(int seconds) override { review_hold_s_ = seconds; }
  void set_wired_paddle(bool on) override { wired_ = on; }
  void set_flush_s(int seconds) override { flush_s_ = seconds; }
  void dismiss_review() override {
    if (phase_ == core::ShotPhase::kReview) phase_ = core::ShotPhase::kIdle;
  }

  void set_paddle_hw(bool hw) { paddle_hw_ = hw; }
  void set_paddle(bool p) { paddle_ = p; }
  void set_brewing(bool b) { brewing_ = b; }
  void set_phase(core::ShotPhase ph) { phase_ = ph; }
  void set_shot_ms(uint32_t ms) { shot_ms_ = ms; }

 private:
  bool paddle_hw_ = true;  // board has the paddle harness (wired-capable)
  bool wired_ = true;      // the "Wired paddle" user setting
  bool paddle_ = false;
  bool brewing_ = false;
  bool shot_mode_ = true;
  core::ShotPhase phase_ = core::ShotPhase::kIdle;
  uint32_t shot_ms_ = 27000;  // matches the fake scale's 27.0s render
  float target_g_ = 36.0f;
  int review_hold_s_ = 30;
  int flush_s_ = 0;
};

}  // namespace host
