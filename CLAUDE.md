# Claude Code project notes

Firmware for a La Marzocco Micra BLE remote on Waveshare ESP32 touch boards.
Read README.md "Developer documentation" first — the architecture section is
accurate and the layering rules there are hard rules:

- `include/core/` + `src/core/` must stay free of LVGL/Arduino/NimBLE/SDL.
  The BLE protocol code (Micra link, Bookoo scale) lives in `src/core/` over
  the `core::ble::ICentral` port; only transports (`nimble_central.cpp`) are
  per-platform. New platforms (Pi/BlueZ, Pico/btstack) implement ICentral,
  never touch protocol code.
- The UI depends only on `core::` interfaces. It compiles unchanged on-device
  and in the host simulator (`make sim` → renders/*.png — the fastest way to
  check UI work; refresh docs/img/ when the UI changes).
- Board differences live in `include/platform_esp32/board_config.h` blocks +
  feature macros; driver code never hardcodes pins.

## Git conventions

- Commit as `marcus@turboio.com` (repo-local config). Do NOT add
  "Co-Authored-By: Claude" or other AI-signature trailers.

## Boards / build

Board targets are `<chip>-<panel>` after the Waveshare product names:
`make build` (default, the 2-inch S3), `build-s3-7b`, `build-s3-4-3b`,
`build-s3-4-3c`, `build-p4-4-3`; matching `flash-*` targets auto-detect the
port and can probe a running board's serial banner (pre-rename names like
`build-p4`/`flash-7b` remain as aliases). All envs + `sim` must compile before
committing platform changes.

### ESP32-S3-Touch-LCD-4.3C (env `esp32-s3-micra-4-3c`) — verified on HW

The 4.3B's RGB/GT911/RTC wiring plus the 7B-style register-based IO extension
(Waveshare CH32V003 @0x24) instead of the 4.3B's CH422G — which adds true PWM
backlight dimming (reg 0x05, inverted duty; vendor clamps at 95% to avoid
full-off) and battery monitoring via the expander's ADC (reg 0x06, ÷3 divider,
scale 3·3.3/1023 — not yet multimeter-calibrated; the 7B's same-family chip
needed 0.009632). Support is config-only: no new driver code. RGB timings are
the 4.3C demo's (pulse 4, porches 8/8, 16 MHz), not the 4.3B's.
(USB-vs-battery is now inferred from the battery-node voltage alone —
kUsbPowerVolts — on every board; HWCDC::isPlugged() is no longer used.)
Bring-up gotcha: scripted DTR/RTS toggling on the USB-CDC port can strand the
board in ROM download mode — black screen, silent serial, yet flashing still
works. Recover with the physical RST button; don't script reset dances.

Paddle (brew-by-weight, verified on HW): 3-wire harness — Micra white -> DO0,
paddle switch -> DI0, Micra black + paddle return -> shared GND. DI COM is the
*biased* side of the input opto (internally ~5V), so a dry contact must close
DI0 to GND — wiring it to DI COM does nothing. Drive = EXIO6 (active-low),
sense = EXIO0 (low = closed), direction mask 0xDE. GOTCHA: the expander can
ACK its init yet DROP the first direction-mask write (inputs read 0xFF
forever); paddle.cpp re-asserts the mask in begin() and every ~64th sense
poll — keep that if refactoring.

### ESP32-P4-WIFI6-Touch-LCD-4.3 (env `esp32-p4-micra-43`) — bring-up pending

First non-S3 board: P4NRW32, 480x800 ST7701 over 2-lane MIPI-DSI (rotated to
landscape), GT911 touch, WiFi6/BLE via on-board ESP32-C6 over SDIO
(esp-hosted). Compiles; NOT yet validated on hardware. Key facts:

- NimBLE-Arduino does NOT support the P4 (maintainer statement, issue #906).
  This env uses `h2zero/esp-nimble-cpp` (same `NimBLE*` API) against the
  stock Arduino core's IDF NimBLE host, which ships NimBLE-over-hosted
  enabled (verified in the core's esp32p4/sdkconfig). `lib_compat_mode = off`
  is required. Do not "simplify" the P4 env to NimBLE-Arduino.
- Hosted SDIO pin defaults match the board exactly — no WiFi.setPins needed.
- The C6 runs factory esp-hosted slave firmware; `make flash-p4-4-3` never
  touches it. Slave updates (if ever needed): esp-hosted OTA from the P4, or
  the board's P1 header + esptool.

#### Silicon revision gotcha (hit + solved on first hardware, 2026-07)

ESP32-P4 chips exist in two binary-incompatible generations: pre-production
rev v1.x ("engineering sample" retroactively; sold in volume through 2025)
and mass-production rev v3.0+ (v2 was skipped). Our first board is **v1.3**
— check any new board with `esptool chip-id` (or espefuse `WAFER_VERSION_*`).

- Symptom on v1.x when built for the default (rev >= 3.0) target: the
  2nd-stage bootloader itself boot-loops with `Guru Meditation ... (Illegal
  instruction)` where PC == the bootloader entry address and MTVAL=0. Screen
  stays black; app code never runs. Easy to misread as a display problem —
  check serial first.
- Fix: `"chip_variant": "esp32p4_es"` in boards/esp32-p4-wifi6-43.json.
  The Arduino core ships PREBUILT `esp32p4_es` libs (rev v1.x memory map,
  NimBLE + esp-hosted enabled) and pioarduino selects them + the matching
  rev<3 linker templates off that field. Same fix as arduino-esp32
  PR #12341 (M5Stack Tab5). A rev >= 3.0 board needs `"esp32p4"` instead —
  one image cannot boot both generations. Dead ends we tried so the next
  session doesn't: platform downgrade to 54.03.21-2 (P4 BT compiled out) and
  hand-rolled `custom_sdkconfig` rev overrides (pioarduino's hybrid compile
  generates rev3 linker scripts regardless — builder bug).
- v1.x runs the CPU at 360MHz max (boot warning about 400MHz is expected).

#### Hosted BLE bring-up (the second trap)

`NimBLEDevice::init()` alone dies on this board: esp-nimble-cpp drops into
the IDF NimBLE host whose vhci layer single-shots the hosted-SDIO transport
init and aborts with `H_SDIO_DRV: card init failed`. The transport must be
brought up first through Arduino's hosted HAL — `hostedInitBLE()` from
`esp32-hal-hosted.h` (does esp_hosted_init + connect_to_slave + BT
controller RPC). main.cpp calls it before NimBLEDevice::init, guarded by
`CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE`. Verified on hardware: SDIO link up,
NimBLE host init clean. A `Req_GetCoprocessorFwVersion` RPC timeout error
at boot is harmless (factory C6 slave predates that RPC).
Waveshare's factory image + demo repo (waveshareteam/ESP32-P4-WIFI6-Touch-
LCD-4.3) is the known-good reference: flashing its FactoryOnly bin is the
fastest way to prove the C6/slave/wiring are healthy when debugging.

Hardware bring-up status (2026-07-17: boots clean — hosted link, DSI panel,
GT911 all up; NimBLE host inits):
1. Display: panel + LVGL come up (800x480). Arduino_GFX rotation=1 goes
   through a per-pixel rotated bitmap path — still check flow-graph fps;
   fall back to LVGL-side rotation if slow.
2. Touch: GT911 detected at 0x5D. swap/mirror flags in board_config.h are
   best-guess; serial logs one line per press for calibration.
3. BLE: host init verified; actual Micra/scale connections not yet tested.
   Known bug esp-hosted-mcu#180: scan results stall after ~60-90s of
   continuous scanning (our scans are short; reconnects are direct-by-MAC).
4. Paddle (brew-by-weight): native GPIOs on the header corner — GND, GPIO52,
   GPIO51 fit one 3-pin screw terminal. GPIO52 -> PC817 opto module IO (module
   input GND -> board GND); output side VCC left floating so OUT/GND are an
   isolated dry contact: OUT -> Micra white, output GND -> Micra black.
   Active-HIGH drive (IO high = contact closed) — opposite of the 4.3C's
   expander. GPIO51 <- paddle switch to board GND (INPUT_PULLUP, low =
   closed); the physical paddle touches only this board, never the Micra.
   Config-only (native-GPIO path in paddle.cpp); not yet tested on HW.
5. Audio: config-only reuse of the 4.3C's ES8311 driver — BSP pins MCLK 13 /
   BCLK 12 / LRCLK 10 / DOUT 9, codec at 0x18 on the shared I2C bus, PA
   enable native GPIO53 active-high (BOARD_AUDIO_PA_IOEXT selects expander-vs-
   GPIO PA in sound.cpp). Not yet tested on HW.
6. Battery: WORKING — BAT_ADC GPIO20, divider ÷3 (confirmed: raw*3 == 4.20V
   LiPo CV level while charging). `HWCDC::isPlugged()` is non-functional on
   the P4's USB-Serial-JTAG (always false) — moot now that USB-vs-battery is
   voltage-only (>= kUsbPowerVolts) on all boards. Known hardware trait:
   plugging/unplugging USB with a battery attached FULLY POWER-CYCLES the
   board (reset reason: power-on) — the power path's VBUS<->boost switchover
   drops the rail; not fixable in firmware.
