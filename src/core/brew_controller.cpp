#include "core/brew_controller.h"

#include <cmath>

#include "core/system.h"

namespace core {

namespace {
// Wrap-safe "a has reached b" for uint32 millis.
bool reached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}
}  // namespace

void BrewController::poll(uint32_t now_ms) {
  last_now_ms_ = now_ms;
  if (!paddle_.available()) return;

  // --- Paddle relay: edge-triggered, unconditional (the user is in charge).
  if (reached(now_ms, last_sense_ms_ + kSensePeriodMs)) {
    last_sense_ms_ = now_ms;
    // Glitch filter: the raw level must hold kSenseStablePolls consecutive
    // polls before it becomes an edge (see the constant's comment — phantom
    // edge pairs from machine-wake EMI start unintended shots).
    const bool raw = paddle_.sensed();
    if (raw != sense_candidate_) {
      sense_candidate_ = raw;
      sense_stable_ = 1;
    } else if (sense_stable_ < kSenseStablePolls) {
      ++sense_stable_;
    }
    const bool on = (sense_stable_ >= kSenseStablePolls) ? sense_candidate_ : paddle_on_;
    if (!sensed_once_) {
      // Boot safety: adopt the first STABLE level WITHOUT acting on it. If the
      // ESP rebooted mid-shot with the paddle left on, the machine stays off
      // until the user makes a fresh edge — no surprise auto-start.
      if (sense_stable_ >= kSenseStablePolls) {
        sensed_once_ = true;
        paddle_on_ = on;
      }
    } else if (on != paddle_on_) {
      paddle_on_ = on;
      // Every ACCEPTED edge is logged (the raw pin is not): a paddle mystery
      // always comes down to which edges the controller acted on and when.
      logf("Brew: paddle %s edge (phase %d)\n", on ? "ON" : "OFF",
           static_cast<int>(phase_));
      if (on && phase_ == ShotPhase::kReview) {
        // Swallow the ON edge while the frozen review is up: the user almost
        // certainly expects the NEXT (auto) shot, but automation only re-arms
        // from kIdle — relaying here would silently run a MANUAL shot. The
        // machine stays off; the UI flashes the Reset button (review_reject_seq)
        // to say "dismiss the review first". The matching OFF edge then falls
        // through to the branch below, which is a harmless no-op here.
        ++review_reject_seq_;
      } else if (on && standby_ && standby_()) {
        // The machine is connected and in standby: this flip is its WAKE
        // switch (the Micra powers on; no water moves). Drive the line through
        // so the wake happens, but skip the timer + shot automation — there is
        // no shot to time or tare for. The OFF edge below is then a no-op for
        // the (never-started) timer, and the NEXT flip — machine now on —
        // runs as a normal shot.
        paddle_.drive(true);
        driving_ = true;
      } else if (on) {
        paddle_.drive(true);
        driving_ = true;
        timer_.start(now_ms);  // ESP times every shot, automated or not
        logf("Brew: shot timer started\n");
        if (phase_ == ShotPhase::kIdle && shot_mode_) {
          const ScaleSnapshot s0 = scale_.snapshot();
          if (s0.connected) {
            phase_ = ShotPhase::kBrewing;
            auto_stopped_ = false;
            blind_since_ms_ = 0;
            if (s0.weight_g > -kPreTaredG && s0.weight_g < kPreTaredG) {
              // Scale already reads ~0 (tared with the cup on — the natural
              // flow): baseline is confirmed instantly, no mid-shot tare, no
              // graph hold, no dropped samples at the start of the trace.
              start_g_ = s0.weight_g;
              baseline_set_ = true;
            } else {
              // Fallback for a forgotten tare: zero the scale now and hold the
              // baseline (and the graph) until the command settles.
              scale_.tare();
              baseline_set_ = false;
              baseline_at_ms_ = now_ms + kBaselineDelayMs;
              start_g_ = 0.0f;
            }
          }
        }
      } else {
        paddle_.drive(false);
        driving_ = false;
        timer_.stop(now_ms);
        if (phase_ == ShotPhase::kBrewing) end_shot(now_ms);  // manual cut -> settle
      }
    }
  }

  // --- Automated shot: baseline then stop-at-target.
  if (phase_ == ShotPhase::kBrewing) {
    const ScaleSnapshot s = scale_.snapshot();
    if (!s.connected) {
      // Scale went dark mid-shot. A transient BLE blip (supervision timeout +
      // fast reconnect — machine vibration can cause one) must NOT kill the
      // shot: fly blind for a grace period and resume seamlessly on reconnect
      // (the tare survives a link blip, so the baseline stays valid). Only a
      // sustained loss cancels the automation — the machine keeps running
      // either way; the user still owns the paddle.
      if (blind_since_ms_ == 0) {
        blind_since_ms_ = now_ms;
      } else if (reached(now_ms, blind_since_ms_ + kBlindGraceMs)) {
        phase_ = ShotPhase::kIdle;
        timer_.stop(last_now_ms_);
        blind_since_ms_ = 0;
      }
    } else if (!baseline_set_) {
      blind_since_ms_ = 0;
      // Confirm the instant the tare visibly lands (weight snaps to ~0 — a
      // notify or two, typically 100-300ms), so the graph hold is barely
      // noticeable. The deadline is only the dropped-command fallback: past it,
      // whatever the scale reads becomes the baseline and the math still works.
      if ((s.weight_g > -kPreTaredG && s.weight_g < kPreTaredG) ||
          reached(now_ms, baseline_at_ms_)) {
        start_g_ = s.weight_g;
        baseline_set_ = true;
      }
    } else if (s.weight_g - start_g_ >= target_g_ - overshoot_g_) {
      blind_since_ms_ = 0;
      paddle_.drive(false);
      driving_ = false;
      timer_.stop(now_ms);
      auto_stopped_ = true;
      end_shot(now_ms);
    } else {
      blind_since_ms_ = 0;
    }
  }

  // --- Settle: keep the graph live for the drip tail, then freeze into review.
  if (phase_ == ShotPhase::kSettling) {
    const ScaleSnapshot s = scale_.snapshot();
    if (!s.connected) {
      phase_ = ShotPhase::kIdle;  // scale gone -> nothing left to review
    } else if (reached(now_ms, settle_until_ms_)) {
      // Overshoot learning (apollo-style): the shot stopped overshoot_g_ early;
      // the drips have now settled, so the miss vs target is exactly the error
      // in that estimate. Absorb a fraction per shot. Only target-triggered
      // stops teach — a manual paddle cut says nothing about drip lag.
      if (auto_stopped_ && baseline_set_) {
        const float err = (s.weight_g - start_g_) - target_g_;  // + = overshot
        float next = overshoot_g_ + kOvershootLearnRate * err;
        if (next < 0.0f) next = 0.0f;
        if (next > kOvershootMaxG) next = kOvershootMaxG;
        overshoot_g_ = next;
        if (persist_overshoot_) persist_overshoot_(next);
      }
      phase_ = ShotPhase::kReview;
      review_until_ms_ = now_ms + static_cast<uint32_t>(review_hold_s_) * 1000u;
    }
  }

  // --- Review auto-dismiss.
  if (phase_ == ShotPhase::kReview && reached(now_ms, review_until_ms_)) {
    phase_ = ShotPhase::kIdle;
  }
}

void BrewController::end_shot(uint32_t now_ms) {
  phase_ = ShotPhase::kSettling;
  settle_until_ms_ = now_ms + kSettleMs;
}

BrewSnapshot BrewController::snapshot() const {
  return BrewSnapshot{
      .available = paddle_.available(),
      .paddle_pressed = paddle_on_,
      .brewing = driving_,
      .phase = phase_,
      .shot_mode = shot_mode_,
      .shot_ms = timer_.elapsed_ms(last_now_ms_),
      .baseline_set = baseline_set_,
      .target_weight_g = target_g_,
      .overshoot_g = overshoot_g_,
      .review_hold_s = review_hold_s_,
      .review_reject_seq = review_reject_seq_,
  };
}

void BrewController::set_target_weight_g(float grams) {
  target_g_ = grams;
  if (persist_target_) persist_target_(grams);
}

void BrewController::set_shot_mode(bool on) {
  shot_mode_ = on;
  if (persist_mode_) persist_mode_(on);
}

void BrewController::dismiss_review() {
  if (phase_ == ShotPhase::kReview) phase_ = ShotPhase::kIdle;
}

void BrewController::set_review_hold_s(int seconds) {
  if (seconds < 5) seconds = 5;
  if (seconds > 120) seconds = 120;
  review_hold_s_ = seconds;
  if (persist_review_) persist_review_(seconds);
}

}  // namespace core
