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

 private:
  // Smoothing state (mutable: battery() is a const read but filters over time).
  mutable float volts_filt_ = 0.0f;  // EMA of cell voltage; 0 = uninitialized
  mutable int shown_pct_ = -1;       // last reported percent (hysteresis); -1 = none
};

}  // namespace platform
