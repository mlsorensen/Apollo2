#pragma once

#include "core/scale.h"

// Host stand-in for a Bluetooth scale. On the device this role is played by the
// NimBLE ScaleLink; here it returns canned state so the scale-aware UI can be
// designed and rendered on a laptop. Setters let the simulator render specific
// states (connected vs not).

namespace host {

class FakeScale : public core::IScale {
 public:
  core::ScaleSnapshot snapshot() const override {
    return core::ScaleSnapshot{
        .name = umbra_ ? "UMBRA-7F3A" : "BOOKOO_THEMIS",
        .connected = connected_,
        .weight_g = 36.4f,
        .timer_ms = 27000,
        .battery_valid = true,
        .battery_pct = 78,
        .seq = 1,
    };
  }
  core::ScaleFeatures features() const override {
    return core::ScaleFeatures{.tare = true,
                               .flow = true,
                               .timer = true,
                               .battery = true,
                               .beep = true,
                               .sleep = umbra_};
  }
  void tare() override {}

  void set_connected(bool c) { connected_ = c; }
  // Umbra persona: sleep-capable scale, for rendering the "Sleeping" Home state.
  void set_umbra(bool u) { umbra_ = u; }

 private:
  bool connected_ = true;
  bool umbra_ = false;
};

}  // namespace host
