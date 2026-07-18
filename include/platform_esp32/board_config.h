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
#define BOARD_DISPLAY_SPI    // ST7789 over SPI
#define BOARD_TOUCH_CST816   // CST816, 8-bit registers
constexpr bool kSupportsBrightness = true;  // LEDC PWM backlight (dimmable)

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
// Full reads ~4.13 V at rest on this board's ADC path (measured: 92% with the old
// 4.20 cap, charge-current = 0), so cap there to read ~100% when full.
constexpr float kBatteryFullVolts = 4.13f;
constexpr float kBatteryEmptyVolts = 3.40f;  // 0% with margin above the brownout zone
constexpr float kBatteryCutoffVolts = 3.40f; // deep-sleep at/below this (avoids brownout)
constexpr float kBatteryResumeVolts = 3.70f; // only wake fully once charged past here
constexpr float kBatteryChargingVolts = 4.15f;   // (legacy voltage-only charge guess)
// On USB power with NO cell installed, the charge node floats above any real
// battery; above this we report "no battery" (plug icon) rather than a percent.
constexpr float kBatteryNoCellVolts = 4.35f;

// Optional VBUS (USB 5V) sense on an ADC GPIO — the robust "is USB plugged in"
// signal (catches dumb chargers too, unlike HWCDC::isPlugged). -1 = none, fall
// back to the USB-peripheral check. If you wire a divider from the 5V bus to a
// spare ADC pin, set the pin + divider here.
constexpr int   kVbusAdc = -1;
constexpr float kVbusDivider = 2.0f;       // vbus_volts = adc_volts * divider
constexpr float kVbusPresentVolts = 4.0f;  // above this => USB present

// --- Paddle control (brew-by-weight) ---
// Drive line into the Micra's paddle circuit (a relay, or the 4.3B's opto-
// isolated GPIO). The Micra just watches this line: we hold it to keep a shot
// running and release it to stop. kPaddleSensePin reads the physical paddle.
// -1 = no paddle hardware wired on this board -> auto-stop disabled, the scale
// is display/timer/flow/alert-only. (This 2-inch is a portable battery remote;
// wire a relay to spare GPIOs and set these if you want auto-stop here.)
constexpr int  kPaddleDrivePin = -1;
constexpr int  kPaddleSensePin = -1;
constexpr bool kPaddleActiveHigh = true;   // drive level that "closes" the shot

#elif defined(BOARD_WAVESHARE_S3_LCD_7B)

// Waveshare ESP32-S3-Touch-LCD-7B — 7", 1024x600 RGB parallel panel, GT911
// capacitive touch, and an I2C "IO extension" expander (addr 0x24) that drives
// LCD reset / backlight / touch reset. Pins + timing traced to Waveshare's 7B
// Arduino demo (examples/Arduino/examples/06_LCD + 08_TOUCH). UNVERIFIED on
// hardware yet — tweak here if the panel is dark/garbled or touch is off.
constexpr char kName[] = "Waveshare ESP32-S3-Touch-LCD-7B";
#define BOARD_DISPLAY_RGB     // RGB parallel panel via Arduino_GFX
#define BOARD_TOUCH_GT911     // GT911, 16-bit registers
#define BOARD_HAS_IO_EXTENSION
constexpr bool kSupportsBrightness = true;  // IO-extension PWM backlight (dimmable)

// --- Shared I2C bus (IO extension + GT911 touch) ---
constexpr int kI2cSda = 8;
constexpr int kI2cScl = 9;

// --- IO extension (I2C expander @ 0x24): output-pin assignments ---
constexpr int kIoExtAddr = 0x24;
constexpr int kIoExtTouchReset = 1;  // IO_1
constexpr int kIoExtBacklight = 2;   // IO_2
constexpr int kIoExtLcdReset = 3;    // IO_3

// --- Display: 16-bit RGB565 parallel panel ---
constexpr int  kLcdNativeW = 1024;
constexpr int  kLcdNativeH = 600;
constexpr int  kLcdRotation = 0;     // panel is already landscape
constexpr int  kRgbDe = 5, kRgbVsync = 3, kRgbHsync = 46, kRgbPclk = 7;
constexpr int  kRgbR[5] = {1, 2, 42, 41, 40};        // R3..R7
constexpr int  kRgbG[6] = {39, 0, 45, 48, 47, 21};   // G2..G7
constexpr int  kRgbB[5] = {14, 38, 18, 17, 10};      // B3..B7
constexpr int  kRgbHsyncFront = 48,  kRgbHsyncPulse = 162, kRgbHsyncBack = 152;
constexpr int  kRgbVsyncFront = 3,   kRgbVsyncPulse = 45,  kRgbVsyncBack = 13;
constexpr int  kRgbPclkActiveNeg = 1;
constexpr long kRgbPclkHz = 30000000;  // 30 MHz (Waveshare demo value; 24 MHz fell
                                       // below the panel's stable range -> no image)
// Bounce buffer (in internal RAM): the LCD peripheral DMAs from here, refilled
// from the PSRAM framebuffer — without it a PSRAM-backed RGB panel tears/flickers.
// Waveshare's demo uses H_RES*10.
constexpr int  kRgbBouncePx = kLcdNativeW * 10;

// --- Touch: GT911 on the shared I2C bus (reset via the IO extension) ---
constexpr int  kTouchSda = 8;        // == kI2cSda (Touch reads these names)
constexpr int  kTouchScl = 9;
constexpr int  kTouchAddr = 0x5D;    // 0x5D (INT low at boot) or 0x14
constexpr int  kTouchInt = 4;        // GPIO4
constexpr int  kTouchRst = -1;       // driven via kIoExtTouchReset, not a GPIO
constexpr bool kTouchSwapXY = false; // GT911 reports in on-screen orientation
constexpr bool kTouchMirrorX = false;
constexpr bool kTouchMirrorY = false;

// --- Battery: read via the IO-extension's ADC (reg 0x06), not a direct ESP pin.
//     kBatteryIoExtScale is volts-per-count. Calibrated: 4.026 V measured at the
//     terminals when raw ~418 -> 4.026/418. (The vendor's ESP32-ADC formula
//     3.3/4096*3 does NOT apply — this chip's ADC has its own range.) ---
#define BOARD_BATTERY_VIA_IOEXT
constexpr float kBatteryIoExtScale = 0.009632f;
constexpr int   kBatteryAdc = -1;               // no direct ESP32 ADC pin
constexpr float kBatteryDivider = 1.0f;         // (unused; folded into the scale)
constexpr int   kBatteryChargePin = -1;
constexpr bool  kBatteryChargeActiveLow = true;
// The IO-extension ADC path reads ~4.07 V at rest when full (measured: 86% with
// the old 4.20 cap, charge-current = 0), so cap there to read ~100% when full.
constexpr float kBatteryFullVolts = 4.07f;
constexpr float kBatteryEmptyVolts = 3.40f;  // 0% with margin above the brownout zone
constexpr float kBatteryCutoffVolts = 3.40f; // deep-sleep at/below this (avoids brownout)
constexpr float kBatteryResumeVolts = 3.70f; // only wake fully once charged past here
constexpr float kBatteryChargingVolts = 4.15f;
constexpr float kBatteryNoCellVolts = 4.35f;
constexpr int   kVbusAdc = -1;
constexpr float kVbusDivider = 2.0f;
constexpr float kVbusPresentVolts = 4.0f;

// --- Paddle control (brew-by-weight) ---
// See the 2-inch block above. The 4.3B has opto-isolated GPIO suited to driving
// the Micra's paddle line directly; set these to those pins when that port lands.
constexpr int  kPaddleDrivePin = -1;
constexpr int  kPaddleSensePin = -1;
constexpr bool kPaddleActiveHigh = true;

#elif defined(BOARD_WAVESHARE_S3_LCD_43B)

// Waveshare ESP32-S3-Touch-LCD-4.3B — 4.3", 800x480 RGB parallel panel, GT911
// touch, I2C IO-expander. Per the schematic this is the 7B board shrunk to
// 800x480: same RGB pin mapping, same GT911, same expander @0x24 — only the panel
// resolution + RGB timing differ (values from Waveshare's 4.3B config). We reuse
// the 7B's io_extension/touch/backlight/battery paths unchanged. UNVERIFIED on HW.
constexpr char kName[] = "Waveshare ESP32-S3-Touch-LCD-4.3B";
#define BOARD_DISPLAY_RGB
#define BOARD_TOUCH_GT911
#define BOARD_HAS_IO_EXTENSION
// The 4.3B's expander is a real CH422G — driven by single-byte writes to distinct
// command addresses (0x24 cfg / 0x38 IO / 0x23 OC), NOT the 7B's reg+value scheme.
// Same pin roles though: EXIO1 touch reset, EXIO2 backlight, EXIO3 LCD reset.
#define BOARD_IO_EXPANDER_CH422G
// PCF85063A RTC (I2C 0x51) backed by a coin cell -> wall-clock survives power-off.
#define BOARD_HAS_PCF85063_RTC
constexpr bool kSupportsBrightness = false;  // CH422G backlight is on/off only

// --- Shared I2C bus (IO expander + GT911 touch) ---
constexpr int kI2cSda = 8;
constexpr int kI2cScl = 9;

// --- IO expander @ 0x24: output-pin assignments (same roles as the 7B) ---
constexpr int kIoExtAddr = 0x24;
constexpr int kIoExtTouchReset = 1;  // IO_1
constexpr int kIoExtBacklight = 2;   // IO_2
constexpr int kIoExtLcdReset = 3;    // IO_3

// --- Display: 16-bit RGB565 parallel panel, 800x480 ---
constexpr int  kLcdNativeW = 800;
constexpr int  kLcdNativeH = 480;
constexpr int  kLcdRotation = 0;     // panel is already landscape
constexpr int  kRgbDe = 5, kRgbVsync = 3, kRgbHsync = 46, kRgbPclk = 7;
constexpr int  kRgbR[5] = {1, 2, 42, 41, 40};        // R3..R7 (same as 7B)
constexpr int  kRgbG[6] = {39, 0, 45, 48, 47, 21};   // G2..G7
constexpr int  kRgbB[5] = {14, 38, 18, 17, 10};      // B3..B7
constexpr int  kRgbHsyncFront = 40,  kRgbHsyncPulse = 48, kRgbHsyncBack = 88;
constexpr int  kRgbVsyncFront = 13,  kRgbVsyncPulse = 3,  kRgbVsyncBack = 32;
constexpr int  kRgbPclkActiveNeg = 1;
constexpr long kRgbPclkHz = 16000000;  // 16 MHz (Waveshare 4.3B value)
constexpr int  kRgbBouncePx = kLcdNativeW * 10;

// --- Touch: GT911 on the shared I2C bus (same as the 7B) ---
constexpr int  kTouchSda = 8;
constexpr int  kTouchScl = 9;
constexpr int  kTouchAddr = 0x5D;
constexpr int  kTouchInt = 4;
constexpr int  kTouchRst = -1;
constexpr bool kTouchSwapXY = false;
constexpr bool kTouchMirrorX = false;
constexpr bool kTouchMirrorY = false;

// --- Battery: the CH422G has NO ADC, so no battery reading for now -> the board
//     reports USB-powered (plug icon). A direct-ESP-ADC path can be added later
//     once the divider pin is known. (kBatteryAdc < 0 + no IOEXT -> usb=true.) ---
constexpr float kBatteryIoExtScale = 0.009632f;  // unused (kept for symmetry)
constexpr int   kBatteryAdc = -1;
constexpr float kBatteryDivider = 1.0f;
constexpr int   kBatteryChargePin = -1;
constexpr bool  kBatteryChargeActiveLow = true;
constexpr float kBatteryFullVolts = 4.07f;
constexpr float kBatteryEmptyVolts = 3.40f;
constexpr float kBatteryCutoffVolts = 3.40f;
constexpr float kBatteryResumeVolts = 3.70f;
constexpr float kBatteryChargingVolts = 4.15f;
constexpr float kBatteryNoCellVolts = 4.35f;
constexpr int   kVbusAdc = -1;
constexpr float kVbusDivider = 2.0f;
constexpr float kVbusPresentVolts = 4.0f;

// --- Paddle control (brew-by-weight): the intended target; pins TBD ---
constexpr int  kPaddleDrivePin = -1;
constexpr int  kPaddleSensePin = -1;
constexpr bool kPaddleActiveHigh = true;

#elif defined(BOARD_WAVESHARE_P4_WIFI6_43)

// Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 — ESP32-P4NRW32 (32MB flash, 32MB
// PSRAM), 4.3" 480x800 ST7701 panel over 2-lane MIPI-DSI, GT911 touch, WiFi/BLE
// via an on-board ESP32-C6 over SDIO (esp-hosted; SDIO pins match the esp-hosted
// P4 defaults so no override is needed). No IO expander — reset/backlight are
// native GPIOs. Pins traced from the board schematic + Waveshare's BSP
// (esp32_p4_wifi6_touch_lcd_4_3.h). UNVERIFIED on hardware yet.
constexpr char kName[] = "Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3";
#define BOARD_DISPLAY_DSI     // MIPI-DSI panel via Arduino_GFX
#define BOARD_TOUCH_GT911     // GT911, 16-bit registers
constexpr bool kSupportsBrightness = true;  // LEDC PWM backlight (dimmable)

// --- Shared I2C bus (GT911 touch; also the audio codecs / camera header) ---
constexpr int kI2cSda = 7;
constexpr int kI2cScl = 8;

// --- Display: ST7701, 480x800 native PORTRAIT, 2-lane MIPI-DSI ---
// The UI wants landscape, so we run Arduino_GFX rotation=1 => 800x480 (the same
// native-portrait-rotated pattern as the 2-inch ST7789). DPI timing from the
// Waveshare BSP: 30 MHz pixel clock; HPW 12, HBP 42, HFP 42; VPW 8, VBP 2,
// VFP 60; 500 Mbps per lane.
constexpr int  kLcdNativeW = 480;
constexpr int  kLcdNativeH = 800;
constexpr int  kLcdRotation = 1;      // 1 => 800x480 landscape
constexpr int  kLcdRst = 27;          // native GPIO reset (no expander)
constexpr int  kLcdBacklight = 26;    // LEDC PWM (BSP uses 10-bit LEDC here)
constexpr int  kLcdBacklightEn = 33;  // backlight boost enable — drive high
// 33.4MHz over the 576x870 total raster ≈ 66Hz refresh (BSP default is 30MHz
// ≈ 60Hz). Raised to shift the panel's faint VCOM/inversion shimmer away from
// 60Hz; verdict on hardware was "can't tell" — kept because it's harmless and
// possibly helping. Revert to 30000000 if a future panel batch misbehaves.
constexpr long kDsiDpiClockHz = 33400000;
constexpr int  kDsiLaneBitRateMbps = 500;
constexpr int  kDsiHsyncPulse = 12, kDsiHsyncBack = 42, kDsiHsyncFront = 42;
constexpr int  kDsiVsyncPulse = 8,  kDsiVsyncBack = 2,  kDsiVsyncFront = 60;

// --- Touch: GT911 on I2C, native-GPIO reset (the S3 RGB boards reset it via
//     the IO expander; here it's a plain pin) ---
constexpr int  kTouchSda = 7;   // == kI2cSda (Touch reads these names)
constexpr int  kTouchScl = 8;
constexpr int  kTouchAddr = 0x5D;    // 0x5D (INT low at boot) or 0x14
constexpr int  kTouchRst = 23;
constexpr int  kTouchInt = 2;        // wired per schematic (BSP polls; so do we)
// GT911 reports in the panel's native PORTRAIT orientation; the display runs
// rotated. Best-guess mapping for rotation=1 — flip a flag if a tap lands in
// the wrong place (see the one-line-per-press serial log).
constexpr bool kTouchSwapXY = true;
constexpr bool kTouchMirrorX = false;
constexpr bool kTouchMirrorY = true;

// --- Battery: BAT_ADC is GPIO20 through an R12/R15 divider (schematic), but the
//     ratio is unmeasured and Waveshare's own code never reads it. DISABLED until
//     calibrated on hardware (a wrong divider could fake a low-battery deep-sleep)
//     -> reports USB-powered. To enable: set kBatteryAdc = 20 and calibrate
//     kBatteryDivider against a multimeter at the battery terminals. ---
// BAT_ADC per the schematic; divider is a PROVISIONAL ÷3 guess (Waveshare's
// demos never read the battery, so there's no reference value). ÷3 is the
// safe wrong guess: if the real divider is smaller we OVER-read (falls into
// the no-cell/plug display path) rather than under-read into a spurious
// low-battery deep-sleep. battery.cpp logs raw+scaled volts every ~10s on
// this board — calibrate kBatteryDivider from that against a multimeter.
constexpr int   kBatteryAdc = 20;
constexpr float kBatteryDivider = 3.0f;
constexpr int   kBatteryChargePin = -1;
constexpr bool  kBatteryChargeActiveLow = true;
constexpr float kBatteryFullVolts = 4.13f;
constexpr float kBatteryEmptyVolts = 3.40f;
constexpr float kBatteryCutoffVolts = 3.40f;
constexpr float kBatteryResumeVolts = 3.70f;
constexpr float kBatteryChargingVolts = 4.15f;
constexpr float kBatteryNoCellVolts = 4.35f;
constexpr int   kVbusAdc = -1;
constexpr float kVbusDivider = 2.0f;
constexpr float kVbusPresentVolts = 4.0f;

// --- Paddle control (brew-by-weight): pins TBD (40-pin header available) ---
constexpr int  kPaddleDrivePin = -1;
constexpr int  kPaddleSensePin = -1;
constexpr bool kPaddleActiveHigh = true;

#else
#error "No board selected. Add -DBOARD_WAVESHARE_S3_LCD_2 (or _7B / _43B / P4_WIFI6_43) to build_flags in platformio.ini."
#endif

}  // namespace board
