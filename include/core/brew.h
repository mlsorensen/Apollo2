#pragma once

#include <cstdint>

// Core port for brew-by-weight: paddle interception + automatic shot stop.
//
// IMPORTANT: stays free of LVGL/Arduino/BLE/SDL like the other core ports.
//
// Two ways into the shot lifecycle, selected by `paddle_wired`:
//   WIRED — the device sits in the Micra's paddle circuit (the 4.3C's isolated
//   DO/DI, or a relay on plain-GPIO boards; core::IPaddle) and shots start/stop
//   on paddle edges, with weight-triggered auto-stop.
//   UNWIRED — no paddle harness in the shot machinery (boards without the
//   hardware, or the "Wired paddle" setting off): a weight-stream detector
//   (core::ShotDetector) infers shot start/end from the scale alone. No
//   auto-stop, no overshoot learning, no review-reject flash; the target
//   weight is informational only (the stop_hint flash stands in for the
//   auto-stop). Where the paddle HARDWARE exists, unwired mode still relays
//   it pass-through — sense + drive, no timer/phase involvement — so a wired
//   rig can test unwired mode by just flipping the setting.
//
// The WIRED shot lifecycle (core::BrewController):
//   Idle -> (paddle ON edge, shot mode armed, scale connected) Brewing: tare,
//   ESP shot timer starts, graph resets -> target-overshoot reached (or manual
//   paddle OFF): line opened, timer stops -> Review: graph frozen for reading,
//   dismissed by the Reset button or a timeout -> Idle.
// The physical paddle is relayed EDGE-triggered regardless of the shot
// machinery — an ON edge starts the machine, an OFF edge stops it; an
// automation-initiated stop just leaves the line open until the user's next
// edge — with ONE exception: during kReview an ON edge is swallowed (machine
// stays off) and review_reject_seq ticks so the UI can flash the Reset
// button. Automation only re-arms from kIdle, so relaying there would
// silently run a manual shot when the user expected an auto one.
//
// The UNWIRED lifecycle drives the SAME phases from the detector: a confirmed
// detection enters kBrewing with a retroactive timer start (honest duration
// despite the confirm delay); flow-cease stops the timer and enters kSettling
// -> kReview exactly like a paddle cut.
//
// In EVERY mode, a run shorter than the minimum-shot gate (~8s) never reaches
// settle/review — that's a flush/rinse, not a shot, and it drops silently back
// to kIdle (unwired additionally requires a few grams of net gain).

namespace core {

// Where the shot machinery is in its lifecycle (BrewSnapshot::phase).
enum class ShotPhase : uint8_t {
  kIdle,      // ready — next paddle ON edge starts a shot (armed if shot_mode)
  kBrewing,   // automated shot running: monitoring weight toward the target
  kSettling,  // shot stopped; graph still live for a few seconds so the drip
              // tail settles to zero before the freeze (no new shot can arm)
  kReview,    // graph frozen for review until reset/timeout
};

// The shot machinery is always active — wired boards relay the paddle,
// everything else runs the detector — so there is no "unavailable" state.
struct BrewSnapshot {
  bool  paddle_hw;        // paddle hardware exists on this board (the Settings
                          // "Wired paddle" toggle is only built when it does)
  bool  paddle_wired;     // effective mode: paddle_hw && the user setting. False
                          // = unwired (detector-driven; see the header comment)
  bool  paddle_pressed;   // the physical paddle is currently engaged (wired only)
  bool  brewing;          // the line is held closed (machine running via us)
  ShotPhase phase;        // shot lifecycle (see above)
  bool  shot_mode;        // automation armed (tare/timer/graph/auto-stop)
  uint32_t shot_ms;       // ESP-timed shot duration; holds after stop
  bool  baseline_set;     // starting weight confirmed post-tare (kBrewing only;
                          // the UI re-clears the graph here to shed the
                          // cup-placement spike + its inflated Y axis)
  float start_weight_g;   // shot baseline weight. ~0 wired (post-tare); unwired
                          // it's the detector's untared baseline — the UI
                          // subtracts it so the review plot shows shot grams
  float target_weight_g;  // stop target (informational when !paddle_wired)
  float overshoot_g;      // current learned drip/lag compensation
  int   review_hold_s;    // how long kReview lingers before auto-dismissing
  uint32_t review_reject_seq;  // ticks per paddle ON edge swallowed in kReview
                               // (UI flashes the Reset button on a change)
  bool  stop_hint;        // unwired shots only: the running shot has reached the
                          // point where the auto-stop WOULD fire (target -
                          // overshoot, led by ~250ms of current flow for human
                          // reaction time) — the UI flashes "stop the shot now".
                          // Latches true until the shot ends.
};

// True when the ESP-side shot timer is the authoritative shot-time source:
// wired (paddle edges drive it), or unwired with detection armed. Unwired with
// detection OFF has no edge source at all, so displays fall back to the
// scale's built-in timer. One definition — the label writer (update_home) and
// the fast 10Hz writer (pump_scale_chart) must never disagree.
inline bool esp_shot_timer(const BrewSnapshot& b) {
  return b.paddle_wired || b.shot_mode;
}

class IBrewController {
 public:
  virtual ~IBrewController() = default;

  // Latest known state. Cheap, synchronous, no blocking I/O.
  virtual BrewSnapshot snapshot() const = 0;

  // Set the target weight (grams) the shot should stop at. Persisted by the
  // implementation. No-op when !available.
  virtual void set_target_weight_g(float grams) = 0;

  // Arm/disarm the shot automation (a flush shouldn't tare/log a "shot").
  // Paddle pass-through and the shot timer keep working either way. Persisted.
  virtual void set_shot_mode(bool on) = 0;

  // Leave kReview early (the UI's Reset button). No-op outside kReview.
  virtual void dismiss_review() = 0;

  // How long the frozen review lingers before auto-dismissing (seconds). Persisted.
  virtual void set_review_hold_s(int seconds) = 0;

  // The "Wired paddle" setting (only meaningful on boards with paddle hardware;
  // a no-op elsewhere — they are always unwired). Flipping it mid-shot cancels
  // to kIdle. Persisted.
  virtual void set_wired_paddle(bool on) = 0;
};

}  // namespace core
