#pragma once

#include "core/battery.h"

// Host stand-in: a charging battery at 82%, so the Home top-bar battery
// indicator can be designed/rendered in the simulator.

namespace host {

class FakeBattery : public core::IBattery {
 public:
  core::BatteryState battery() const override {
    return core::BatteryState{/*present=*/true, /*charging=*/true, /*percent=*/82};
  }
};

}  // namespace host
