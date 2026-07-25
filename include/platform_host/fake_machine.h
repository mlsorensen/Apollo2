#pragma once

#include "core/machine.h"

// Host-only stand-in for a real machine. On the device this role is played by
// the BLE transport; here it just returns canned state so we can design and
// render the UI on a laptop. Keeping it behind core::IMachine means the UI
// code that renders from it is byte-for-byte the same code that will later
// render from the live BLE feed.

namespace host {

class FakeMachine : public core::IMachine {
 public:
  core::MachineSnapshot snapshot() const override;
  void set_power(bool on) override;
  void set_brew_target(float celsius) override;
  void set_steam_target(float celsius) override;
  void set_steam_enabled(bool enabled) override;

  // Pose the current boiler temps (e.g. below-target to render the Heating
  // state in the sim). Defaults sit at/above their targets ("ready").
  void set_temps(float brew_c, float boiler_c) {
    brew_temp_ = brew_c;
    boiler_temp_ = boiler_c;
  }

 private:
  core::Power power_ = core::Power::On;
  float brew_temp_ = 93.0f;
  float boiler_temp_ = 123.0f;
  float brew_target_ = 93.0f;
  float steam_target_ = 131.0f;
  bool steam_enabled_ = true;
};

}  // namespace host
