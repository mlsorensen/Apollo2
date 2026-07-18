#pragma once

#include <cstdint>

// Core port for brew-by-weight: paddle interception + automatic shot stop.
//
// IMPORTANT: stays free of LVGL/Arduino/BLE/SDL like the other core ports.
//
// This is only meaningful where the device sits in the Micra's paddle circuit —
// the 4.3C's isolated DO/DI, or a relay on plain-GPIO boards (core::IPaddle).
// `available` is false when no paddle hardware is configured; the UI then hides
// the paddle/auto-stop affordances and the scale is display/timer/flow/alert-only.
//
// The shot lifecycle (core::BrewController):
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

namespace core {

// Where the shot machinery is in its lifecycle (BrewSnapshot::phase).
enum class ShotPhase : uint8_t {
  kIdle,      // ready — next paddle ON edge starts a shot (armed if shot_mode)
  kBrewing,   // automated shot running: monitoring weight toward the target
  kSettling,  // shot stopped; graph still live for a few seconds so the drip
              // tail settles to zero before the freeze (no new shot can arm)
  kReview,    // graph frozen for review until reset/timeout
};

struct BrewSnapshot {
  bool  available;        // paddle hardware configured (else the rest is moot)
  bool  paddle_pressed;   // the physical paddle is currently engaged
  bool  brewing;          // the line is held closed (machine running via us)
  ShotPhase phase;        // shot lifecycle (see above)
  bool  shot_mode;        // automation armed (tare/timer/graph/auto-stop)
  uint32_t shot_ms;       // ESP-timed shot duration; holds after stop
  bool  baseline_set;     // starting weight confirmed post-tare (kBrewing only;
                          // the UI re-clears the graph here to shed the
                          // cup-placement spike + its inflated Y axis)
  float target_weight_g;  // stop target
  float overshoot_g;      // current learned drip/lag compensation
  int   review_hold_s;    // how long kReview lingers before auto-dismissing
  uint32_t review_reject_seq;  // ticks per paddle ON edge swallowed in kReview
                               // (UI flashes the Reset button on a change)
};

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
};

}  // namespace core
