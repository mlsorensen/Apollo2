#include "platform_esp32/battery.h"

#include <Arduino.h>

#include <cmath>

#include "platform_esp32/board_config.h"

namespace platform {

void Battery::begin() {
  // analogReadMilliVolts() configures attenuation/calibration on first use, so
  // there's nothing to set up. (No charge pin on this board.)
}

core::BatteryState Battery::battery() const {
  core::BatteryState s;
  if (board::kBatteryAdc < 0) return s;  // monitoring disabled for this board

  // Calibrated pin millivolts, scaled back up through the divider.
  const float volts =
      analogReadMilliVolts(board::kBatteryAdc) / 1000.0f * board::kBatteryDivider;

  // A reading well below a real cell means no battery (USB-only).
  if (volts < 2.5f) return s;  // present = false

  int pct = static_cast<int>(std::lround(
      (volts - board::kBatteryEmptyVolts) /
      (board::kBatteryFullVolts - board::kBatteryEmptyVolts) * 100.0f));
  pct = std::max(0, std::min(100, pct));

  s.present = true;
  s.percent = pct;
  if (board::kBatteryChargePin >= 0) {
    const int raw = digitalRead(board::kBatteryChargePin);
    s.charging = board::kBatteryChargeActiveLow ? (raw == LOW) : (raw == HIGH);
  } else {
    s.charging = volts >= board::kBatteryChargingVolts;  // inferred (no CHG GPIO)
  }
  return s;
}

}  // namespace platform
