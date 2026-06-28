#pragma once

#include "core/battery.h"

// Host stand-in: an idle battery at 82%, so the sim previews the percent view
// (the charging fill animation only shows on a live device with a timer ticking).

namespace host {

class FakeBattery : public core::IBattery {
 public:
  core::BatteryState battery() const override {
    return core::BatteryState{/*present=*/true, /*charging=*/false, /*percent=*/82};
  }
};

}  // namespace host
