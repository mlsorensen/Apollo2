#pragma once

#include "core/brew.h"

// Host stand-in for the brew-by-weight controller. On the device this drives the
// paddle GPIO and runs the stop loop; here it returns canned state so the
// paddle-status / target-weight UI can be rendered. Setters let the sim render
// the various states (no paddle hardware, idle, paddle pressed, brewing).

namespace host {

class FakeBrewController : public core::IBrewController {
 public:
  core::BrewSnapshot snapshot() const override {
    return core::BrewSnapshot{
        .available = available_,
        .paddle_pressed = paddle_,
        .brewing = brewing_,
        .target_weight_g = target_g_,
        .overshoot_g = 2.0f,
    };
  }
  void set_target_weight_g(float grams) override { target_g_ = grams; }

  void set_available(bool a) { available_ = a; }
  void set_paddle(bool p) { paddle_ = p; }
  void set_brewing(bool b) { brewing_ = b; }

 private:
  bool available_ = true;
  bool paddle_ = false;
  bool brewing_ = false;
  float target_g_ = 36.0f;
};

}  // namespace host
