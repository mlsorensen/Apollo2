#pragma once

// Core port for brew-by-weight: paddle interception + automatic shot stop.
//
// IMPORTANT: stays free of LVGL/Arduino/BLE/SDL like the other core ports.
//
// This is only meaningful where the device drives the Micra's paddle line via
// GPIO — a relay or the 4.3B's opto-isolated outputs (set the pins in
// board_config.h). `available` is false when no paddle hardware is configured;
// the UI then hides the paddle/auto-stop affordances and the scale is
// display/timer/flow/alert-only.
//
// The stop logic (compare live scale weight to target-overshoot, drive the line,
// learn the overshoot from the settled final weight — adapted from the apollo
// project) lives in the device implementation, which has the scale feed + GPIO.
// The UI only reads state and sets the target weight.

namespace core {

struct BrewSnapshot {
  bool  available;        // paddle hardware configured (else the rest is moot)
  bool  paddle_pressed;   // the physical paddle is currently engaged
  bool  brewing;          // a shot is in progress (line held closed by us)
  float target_weight_g;  // stop target
  float overshoot_g;      // current learned drip/lag compensation
};

class IBrewController {
 public:
  virtual ~IBrewController() = default;

  // Latest known state. Cheap, synchronous, no blocking I/O.
  virtual BrewSnapshot snapshot() const = 0;

  // Set the target weight (grams) the shot should stop at. Persisted by the
  // implementation. No-op when !available.
  virtual void set_target_weight_g(float grams) = 0;
};

}  // namespace core
