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
  // 7B: the battery is read by the IO-extension's ADC, not an ESP32 pin. The ADC
  // is noisy (~+/-35 counts), so oversample and average.
  constexpr int kSamples = 16;
  uint32_t raw_sum = 0;
  for (int i = 0; i < kSamples; ++i) raw_sum += io_extension().read_adc();
  const uint16_t raw = static_cast<uint16_t>(raw_sum / kSamples);
  volts = raw * board::kBatteryIoExtScale;
#else
  // No battery hardware on this board -> external-power (USB) indicator only.
  if (board::kBatteryAdc < 0) {
    s.usb = true;
    return s;
  }

  // Oversample to beat per-read ADC noise (calibrated millivolts, averaged),
  // then scale back up through the divider.
  constexpr int kSamples = 16;
  uint32_t mv_sum = 0;
  for (int i = 0; i < kSamples; ++i) mv_sum += analogReadMilliVolts(board::kBatteryAdc);
  const float raw_mv = mv_sum / static_cast<float>(kSamples);
  volts = raw_mv / 1000.0f * board::kBatteryDivider;

#if defined(BOARD_WAVESHARE_P4_WIFI6_43)
  // Bring-up calibration log (divider unverified on this board): compare the
  // scaled volts against a multimeter on the pack, fix kBatteryDivider, then
  // delete this block.
  {
    static uint32_t last_log_ms = 0;
    if (millis() - last_log_ms > 10000) {
      last_log_ms = millis();
      Serial.printf("battery: adc raw=%.0fmV x%.2f -> %.2fV\n", raw_mv,
                    board::kBatteryDivider, volts);
    }
  }
#endif
#endif

  // --- Windowed reporting: the charger bounces the node hard (top-off pulses
  // to ~4.2 V vs sag toward the pack level between them), so per-read
  // thresholding flapped the tray between "USB" and a percent. Accumulate every
  // poll; re-decide on a per-state cadence. Source = window MAX vs
  // kUsbPowerVolts: only external power can push the node that high, so
  //   - battery -> USB is INSTANT: one qualifying sample proves a plug-in;
  //   - USB -> battery takes a full kUsbHoldMs with no qualifying sample (the
  //     inter-pulse sag reads exactly like a pack, so absence of pulses is the
  //     only unplug signal — this is as "immediate" as voltage alone can be);
  //   - on battery the percent recomputes only once a minute (window AVERAGE),
  //     so the readout sits still while nothing is really changing.
  constexpr uint32_t kWindowMs = 60000;   // battery: percent update cadence
  constexpr uint32_t kUsbHoldMs = 15000;  // USB: max no-pulse span before "unplugged"
  const uint32_t now = millis();
  if (win_n_ == 0) win_started_ms_ = now;
  win_sum_ += volts;
  win_max_ = (win_n_ == 0 || volts > win_max_) ? volts : win_max_;
  ++win_n_;

  const bool saw_usb = win_max_ >= board::kUsbPowerVolts;
  const bool promote_to_usb = saw_usb && !state_.usb;  // instant USB flip
  const uint32_t hold_ms = state_.usb ? kUsbHoldMs : kWindowMs;
  if (state_valid_ && !promote_to_usb && now - win_started_ms_ < hold_ms)
    return state_;  // hold the reported state until the window closes

  const float avg = win_sum_ / static_cast<float>(win_n_);
  win_sum_ = 0.0f;
  win_max_ = 0.0f;
  win_n_ = 0;

  core::BatteryState next;
  if (saw_usb || avg < 2.5f) {
    // External power (or no usable cell reading — yet the board runs, so
    // external power too). No percent: the node tracks the charger, not the pack.
    next.usb = true;
  } else {
    next.present = true;
    int pct = static_cast<int>(std::lround(
        (avg - board::kBatteryEmptyVolts) /
        (board::kBatteryFullVolts - board::kBatteryEmptyVolts) * 100.0f));
    next.percent = std::max(0, std::min(100, pct));
    next.volts = avg;  // windowed pack voltage (for the low-battery cutoff)
  }
  state_ = next;
  state_valid_ = true;
  return state_;
}

}  // namespace platform
