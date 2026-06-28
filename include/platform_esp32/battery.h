#pragma once

#include "core/battery.h"

// Reads battery voltage from the board's ADC and maps it to a percent. This
// board exposes no charge-status pin, so "charging" is inferred from voltage
// (see board_config.h). If a CHG GPIO is ever wired (kBatteryChargePin), it's
// read instead.

namespace platform {

class Battery : public core::IBattery {
 public:
  void begin();
  core::BatteryState battery() const override;
};

}  // namespace platform
