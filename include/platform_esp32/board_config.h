#pragma once

// Per-board pin & panel configuration. Exactly ONE board is selected at build
// time via a -DBOARD_* flag in platformio.ini. This is the single place board
// pins live — driver code reads these constants and never hardcodes a pin.
//
// To add a board (e.g. the 4.3 RGB panel later), add another #elif block with
// the same constant names that that board's driver needs.

namespace board {

#if defined(BOARD_WAVESHARE_S3_LCD_2)

// Waveshare ESP32-S3-Touch-LCD-2 — 2.0", 240x320 ST7789T3 over 4-wire SPI,
// CST816 capacitive touch over I2C. Values traced to Waveshare's demo source.
constexpr char kName[] = "Waveshare ESP32-S3-Touch-LCD-2";

// --- Display: ST7789 on the FSPI bus ---
constexpr int  kLcdSclk = 39;
constexpr int  kLcdMosi = 38;
constexpr int  kLcdMiso = 40;
constexpr int  kLcdCs = 45;
constexpr int  kLcdDc = 42;
constexpr int  kLcdRst = -1;          // no reset line; tied to system reset
constexpr int  kLcdBacklight = 1;     // active-high; PWM-capable (LEDC) later
constexpr int  kLcdNativeW = 240;     // panel native size (portrait)
constexpr int  kLcdNativeH = 320;
constexpr int  kLcdRotation = 1;      // 1 => 320x240 landscape (vendor default)
constexpr bool kLcdIps = true;
constexpr long kLcdSpiHz = 80000000;  // 80 MHz

// --- Touch: CST816 on I2C ---
constexpr int  kTouchSda = 48;
constexpr int  kTouchScl = 47;
constexpr int  kTouchAddr = 0x15;
constexpr int  kTouchRst = -1;        // no reset line
constexpr int  kTouchInt = -1;        // not wired in vendor demo; we poll
// Touch reports coordinates in the panel's native (portrait) orientation; the
// display runs rotated. These map native -> on-screen. Best-guess for
// rotation=1; flip a flag if a tap lands in the wrong place (see serial log).
constexpr bool kTouchSwapXY = true;
constexpr bool kTouchMirrorX = false;
constexpr bool kTouchMirrorY = true;

// --- Battery monitoring (from the vendor demo) ---
// GPIO5 = ADC1_CH4, battery divided by 3. Use analogReadMilliVolts for the
// calibrated reading. NOTE: GPIO5 is also a MOSFET-control solder pad on this
// board — we only ever read it as an ADC input. This board exposes NO charge-
// status pin, so charging is inferred from voltage (kBatteryChargingVolts).
constexpr int   kBatteryAdc = 5;             // ADC GPIO reading battery voltage
constexpr float kBatteryDivider = 3.0f;      // battery_volts = adc_volts * divider
constexpr int   kBatteryChargePin = -1;      // none on this board -> infer
constexpr bool  kBatteryChargeActiveLow = true;  // (unused; no pin)
constexpr float kBatteryFullVolts = 4.20f;
constexpr float kBatteryEmptyVolts = 3.30f;
constexpr float kBatteryChargingVolts = 4.15f;   // infer charging above this

#else
#error "No board selected. Add -DBOARD_WAVESHARE_S3_LCD_2 to build_flags in platformio.ini."
#endif

}  // namespace board
