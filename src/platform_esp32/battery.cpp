#include "platform_esp32/battery.h"

#include <Arduino.h>

#include <cmath>

#include "platform_esp32/board_config.h"
#if defined(BOARD_BATTERY_VIA_IOEXT)
#include "platform_esp32/io_extension.h"
#endif

namespace platform {

void Battery::begin() {
  // analogReadMilliVolts() configures attenuation/calibration on first use, so
  // there's nothing to set up. (No charge pin on this board.)
}

core::BatteryState Battery::battery() const {
  core::BatteryState s;
  float volts;

#if defined(BOARD_BATTERY_VIA_IOEXT)
  // 7B: the battery is read by the IO-extension's ADC, not an ESP32 pin.
  const uint16_t raw = io_extension().read_adc();
  volts = raw * board::kBatteryIoExtScale;
  static uint32_t last_log = 0;  // periodic raw dump to calibrate kBatteryIoExtScale
  if (millis() - last_log > 5000) {
    last_log = millis();
    Serial.printf("battery: ioext ADC raw=%u -> %.2fV\n", raw, volts);
  }
  s.usb = HWCDC::isPlugged();
#else
  // No battery hardware on this board -> external-power (plug) indicator only.
  if (board::kBatteryAdc < 0) {
    s.usb = true;
    return s;
  }

  // External power. Prefer a real VBUS (5V) sense on an ADC pin if the board
  // wires one (works for chargers too); otherwise fall back to the USB
  // peripheral's host-activity check (HWCDC::isPlugged uses an IDF timer/SOF
  // check, so no serial terminal is needed, but a data-less charger may miss).
  if (board::kVbusAdc >= 0) {
    const float vbus =
        analogReadMilliVolts(board::kVbusAdc) / 1000.0f * board::kVbusDivider;
    s.usb = vbus >= board::kVbusPresentVolts;
  } else {
    s.usb = HWCDC::isPlugged();
  }

  // Oversample to beat per-read ADC noise (calibrated millivolts, averaged),
  // then scale back up through the divider.
  constexpr int kSamples = 16;
  uint32_t mv_sum = 0;
  for (int i = 0; i < kSamples; ++i) mv_sum += analogReadMilliVolts(board::kBatteryAdc);
  volts = (mv_sum / static_cast<float>(kSamples)) / 1000.0f * board::kBatteryDivider;
#endif

  // No cell: either no reading, or (on USB) the bare charge node floating above
  // any real battery. Either way -> not present (UI shows a plug if s.usb).
  if (volts < 2.5f || volts >= board::kBatteryNoCellVolts) {
    volts_filt_ = 0.0f;
    shown_pct_ = -1;
    return s;
  }

  s.present = true;
  // Charging: prefer the USB signal; fall back to an elevated voltage so a dumb
  // charger (no USB host activity) that bumps the voltage still reads as charging.
  s.charging = s.usb || (volts >= board::kBatteryChargingVolts);

  if (s.charging) {
    // Terminal voltage under charge is unreliable — it swings with the charger,
    // so a percent is meaningless here (the UI shows the fill animation, no
    // number). Holding the filter at 0 means % re-seeds from the REAL battery
    // level the instant we unplug, instead of sliding down from the inflated
    // charging voltage.
    volts_filt_ = 0.0f;
    shown_pct_ = -1;
    return s;  // percent stays 0; not shown while charging
  }

  // On battery: EMA (~5 s settle at the 500 ms poll) + percent, freshly seeded
  // right after unplug since the filter was held at 0 while charging.
  constexpr float kAlpha = 0.15f;
  volts_filt_ =
      (volts_filt_ <= 0.0f) ? volts : volts_filt_ + kAlpha * (volts - volts_filt_);

  int pct = static_cast<int>(std::lround(
      (volts_filt_ - board::kBatteryEmptyVolts) /
      (board::kBatteryFullVolts - board::kBatteryEmptyVolts) * 100.0f));
  pct = std::max(0, std::min(100, pct));

  // Hysteresis: ignore +/-1% flicker at a boundary; only move on a real change.
  const int delta = pct > shown_pct_ ? pct - shown_pct_ : shown_pct_ - pct;
  if (shown_pct_ < 0 || delta >= 2) shown_pct_ = pct;

  s.percent = shown_pct_;
  return s;
}

}  // namespace platform
