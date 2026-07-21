#pragma once

#include <cstdint>
#include <functional>

#include "core/brew.h"
#include "core/paddle.h"
#include "core/scale.h"
#include "core/shot_detector.h"
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
  // Returns true when the machine is KNOWN to be connected and not brewing-
  // capable (standby/off). The Micra treats a paddle flip in that state as its
  // wake switch — it powers on, no water moves — so the controller passes the
  // level through to the machine but skips the shot timer + automation.
  void set_standby_provider(std::function<bool()> p) { standby_ = std::move(p); }
  void set_target_persister(std::function<void(float)> p) { persist_target_ = std::move(p); }
  void set_shot_mode_persister(std::function<void(bool)> p) { persist_mode_ = std::move(p); }
  void set_overshoot_persister(std::function<void(float)> p) { persist_overshoot_ = std::move(p); }
  void set_review_hold_persister(std::function<void(int)> p) { persist_review_ = std::move(p); }
  void set_wired_paddle_persister(std::function<void(bool)> p) { persist_wired_ = std::move(p); }

  // Seed from persisted config at boot (does not re-persist).
  void seed(float target_g, bool shot_mode, float overshoot_g, int review_hold_s,
            bool wired_paddle) {
    target_g_ = target_g;
    shot_mode_ = shot_mode;
    overshoot_g_ = overshoot_g;
    review_hold_s_ = review_hold_s;
    wired_paddle_ = wired_paddle;
  }

  void poll(uint32_t now_ms);

  BrewSnapshot snapshot() const override;
  void set_target_weight_g(float grams) override;
  void set_shot_mode(bool on) override;
  void dismiss_review() override;
  void set_review_hold_s(int seconds) override;
  void set_wired_paddle(bool on) override;

 private:
  void end_shot(uint32_t now_ms);  // Brewing -> Review
  bool wired() const { return paddle_.available() && wired_paddle_; }
  void poll_wired(uint32_t now_ms);    // paddle relay + weight automation
  void poll_unwired(uint32_t now_ms);  // detector-driven phases
  void cancel_shot();                  // any phase -> kIdle, line opened, timer cleared

  IPaddle& paddle_;
  IScale& scale_;
  ShotTimer timer_;
  ShotDetector detector_;  // unwired mode's start/stop source
  std::function<bool()> standby_;  // machine known-in-standby (see setter)
  std::function<void(float)> persist_target_;
  std::function<void(bool)> persist_mode_;
  std::function<void(float)> persist_overshoot_;
  std::function<void(int)> persist_review_;
  std::function<void(bool)> persist_wired_;

  ShotPhase phase_ = ShotPhase::kIdle;
  bool shot_mode_ = true;
  bool wired_paddle_ = true;  // user setting; effective only with paddle hardware
  bool stop_hint_ = false;    // unwired: manual-stop point reached (see BrewSnapshot)
  uint8_t stop_hint_over_ = 0;   // consecutive scale updates past the hint threshold
  uint32_t stop_hint_seq_ = 0;   // last scale update judged (one vote per notify)
  bool paddle_on_ = false;    // last ACCEPTED level (edge detection)
  bool sensed_once_ = false;  // no edge until the first stable read (boot safety)
  bool sense_candidate_ = false;  // raw level being debounced toward acceptance
  uint8_t sense_stable_ = 0;      // consecutive polls the candidate has held
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
  uint32_t review_reject_seq_ = 0;  // paddle ON edges swallowed during kReview
  uint32_t blind_since_ms_ = 0;  // scale dark since (kBrewing); 0 = seeing it
  uint32_t last_sense_ms_ = 0;
  uint32_t last_now_ms_ = 0;  // for snapshot()'s elapsed time

  static constexpr uint32_t kSensePeriodMs = 25;      // paddle poll (edge latency)
  // A level change is accepted only after holding this many consecutive polls
  // (~75ms). Native-GPIO sense wiring (P4: weak pull-up, unshielded harness)
  // picked up phantom OFF/ON edge pairs from the machine's own wake-up
  // pump/solenoid kick — a phantom pair re-closes the line on an awake
  // machine, i.e. an unintended real brew. A hand flip holds for 100s of ms.
  static constexpr uint8_t kSenseStablePolls = 3;
  static constexpr uint32_t kBaselineDelayMs = 1200;  // tare settle before baseline
  static constexpr float kPreTaredG = 0.5f;           // |weight| under this = already tared
  static constexpr uint32_t kSettleMs = 3000;         // drip tail before the freeze
  static constexpr uint32_t kBlindGraceMs = 4000;     // tolerated mid-shot scale blackout
  static constexpr float kOvershootLearnRate = 0.5f;  // fraction of the error absorbed per shot
  static constexpr float kOvershootMaxG = 8.0f;       // sanity clamp (0..max)
  // Minimum-shot gate, ALL modes (end_shot): a run shorter than this never
  // reaches settle/review — it's a flush/rinse, not a shot, whether it came
  // from a manual paddle cut, the auto-stop, or the detector.
  static constexpr uint32_t kMinShotMs = 8000;
  // Unwired-only extra gate: a detected "shot" that gained less than this is
  // a splash/bump, not espresso.
  static constexpr float kUnwiredMinShotG = 5.0f;
  // Stop-early hint (unwired): fire the "flip the paddle now" signal this much
  // BEFORE the auto-stop point, in grams of current flow — the user's reaction
  // time stands in for the relay's instant cut.
  static constexpr float kStopHintLeadS = 0.25f;
};

}  // namespace core
