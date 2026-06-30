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
        .name = "BOOKOO_THEMIS",
        .connected = connected_,
        .weight_g = 36.4f,
        .flow_gps = 2.1f,
        .timer_ms = 27000,
        .battery_valid = true,
        .battery_pct = 78,
    };
  }
  core::ScaleFeatures features() const override {
    return core::ScaleFeatures{
        .tare = true, .flow = true, .timer = true, .battery = true, .beep = true};
  }
  void tare() override {}
  size_t drain_flow(float* /*out*/, size_t /*max*/) override { return 0; }

  void set_connected(bool c) { connected_ = c; }

 private:
  bool connected_ = true;
};

}  // namespace host
