#pragma once

// Battery status port. Like IMachine/IProvisioner, the UI depends only on this —
// the device reads a real ADC; the host fakes it.

namespace core {

struct BatteryState {
  bool present = false;   // is a battery connected / readable
  bool charging = false;  // present && on external power
  bool usb = false;       // external (USB) power connected
  int percent = 0;        // 0..100 (meaningful when present)
  float volts = 0.0f;     // smoothed pack voltage (0 if unknown); drives low-batt cutoff
};

class IBattery {
 public:
  virtual ~IBattery() = default;
  virtual BatteryState battery() const = 0;
};

}  // namespace core
