#pragma once

#include "core/battery.h"

// Host stand-in: an idle battery at 82%, so the sim previews the percent view
// (the charging fill animation only shows on a live device with a timer ticking).

namespace host {

class FakeBattery : public core::IBattery {
 public:
  core::BatteryState battery() const override {
    core::BatteryState s;
    s.present = true;
    s.percent = 82;
    s.volts = 3.9f;
    return s;  // idle on battery (no USB) for the sim preview
  }
};

}  // namespace host
