#pragma once

// Battery status port. Like IMachine/IProvisioner, the UI depends only on this —
// the device reads a real ADC; the host fakes it.

namespace core {

// Power source is inferred from the battery-node voltage alone: at/above a
// threshold it's external (USB) power — a charging pack held at the charger's
// level, or the bare charge node with no cell; we don't try to tell those
// apart. Below it, a real pack is discharging and its level is meaningful.
struct BatteryState {
  bool present = false;   // running on a battery (voltage below the USB threshold)
  bool usb = false;       // external (USB) power
  int percent = 0;        // 0..100 (meaningful when present)
  float volts = 0.0f;     // smoothed pack voltage (0 if unknown); drives low-batt cutoff
};

class IBattery {
 public:
  virtual ~IBattery() = default;
  virtual BatteryState battery() const = 0;
};

}  // namespace core
