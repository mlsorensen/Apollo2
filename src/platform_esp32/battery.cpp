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
  // External power: USB host connected. HWCDC::isPlugged() uses an IDF
  // timer/SOF check, so it works without a serial terminal open. (A dumb
  // charger with no data may not register — true VBUS sensing needs a GPIO.)
  s.usb = HWCDC::isPlugged();

  if (board::kBatteryAdc < 0) return s;  // monitoring disabled for this board

  // Oversample to beat per-read ADC noise (calibrated millivolts, averaged),
  // then scale back up through the divider.
  constexpr int kSamples = 16;
  uint32_t mv_sum = 0;
  for (int i = 0; i < kSamples; ++i) mv_sum += analogReadMilliVolts(board::kBatteryAdc);
  const float volts =
      (mv_sum / static_cast<float>(kSamples)) / 1000.0f * board::kBatteryDivider;

  // No cell: either no reading, or (on USB) the bare charge node floating above
  // any real battery. Either way -> not present (UI shows a plug if s.usb).
  if (volts < 2.5f || volts >= board::kBatteryNoCellVolts) {
    volts_filt_ = 0.0f;
    shown_pct_ = -1;
    return s;
  }

  // Exponential moving average across calls (~5 s settle at the 500 ms poll) so
  // the percent doesn't jitter; seed it on the first real sample.
  constexpr float kAlpha = 0.15f;
  volts_filt_ =
      (volts_filt_ <= 0.0f) ? volts : volts_filt_ + kAlpha * (volts - volts_filt_);

  int pct = static_cast<int>(std::lround(
      (volts_filt_ - board::kBatteryEmptyVolts) /
      (board::kBatteryFullVolts - board::kBatteryEmptyVolts) * 100.0f));
  // Charging elevates the terminal voltage a little, so the raw reading runs
  // high; nudge it back down a couple percent while on USB power.
  if (s.usb) pct -= 2;
  pct = std::max(0, std::min(100, pct));

  // Hysteresis: ignore +/-1% flicker at a boundary; only move on a real change.
  const int delta = pct > shown_pct_ ? pct - shown_pct_ : shown_pct_ - pct;
  if (shown_pct_ < 0 || delta >= 2) shown_pct_ = pct;

  s.present = true;
  s.percent = shown_pct_;
  s.charging = s.usb;  // battery present + external power = charging
  return s;
}

}  // namespace platform
