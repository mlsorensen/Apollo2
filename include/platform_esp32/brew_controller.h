#pragma once

#include "core/brew.h"
#include "platform_esp32/board_config.h"

// Device brew-by-weight controller: drives the Micra's paddle line and stops the
// shot at a target weight.
//
// PHASE 0 SKELETON: available() reflects whether this board has paddle pins
// configured (board::kPaddleDrivePin >= 0); everything else is inert. Phase 4
// adds the GPIO drive, paddle-state sensing, and the adaptive-overshoot stop loop
// fed by the live scale weight (target/overshoot persisted to NVS).

namespace platform {

class BrewController : public core::IBrewController {
 public:
  core::BrewSnapshot snapshot() const override {
    return core::BrewSnapshot{
        .available = board::kPaddleDrivePin >= 0,
        .paddle_pressed = false,
        .brewing = false,
        .target_weight_g = target_g_,
        .overshoot_g = 2.0f,
    };
  }
  void set_target_weight_g(float grams) override { target_g_ = grams; }

 private:
  float target_g_ = 36.0f;
};

}  // namespace platform
