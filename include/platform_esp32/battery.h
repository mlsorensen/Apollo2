#pragma once

#include <cstdint>

#include "core/battery.h"

// Reads the battery-node voltage from the board's ADC and maps it to a percent.
// The power source is inferred from that voltage alone: at/above kUsbPowerVolts
// -> external (USB) power, below -> a discharging pack (see core/battery.h).
//
// The reported state is windowed: the charger cycles the node hard (top-off
// pulses vs sag), so raw per-read thresholding bounced the tray between "USB"
// and a percent. Window MAX decides the source (only external power can push
// the node to kUsbPowerVolts): plug-in flips to USB on a single qualifying
// sample; unplug is declared after 15 s without one; the battery percent
// (window AVERAGE) updates only once a minute.

namespace platform {

class Battery : public core::IBattery {
 public:
  void begin();
  core::BatteryState battery() const override;

 private:
  // Window state (mutable: battery() is a const read but accumulates over time).
  mutable core::BatteryState state_{};   // last reported state (held for a minute)
  mutable bool state_valid_ = false;
  mutable float win_sum_ = 0.0f;
  mutable float win_max_ = 0.0f;
  mutable int win_n_ = 0;
  mutable uint32_t win_started_ms_ = 0;
};

}  // namespace platform
