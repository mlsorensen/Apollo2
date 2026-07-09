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

`make build` (2-inch S3), `build-7b`, `build-4-3b`, `build-p4`; matching
`flash-*` targets auto-detect the port and can probe a running board's serial
banner. All envs + `sim` must compile before committing platform changes.

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
- The C6 runs factory esp-hosted slave firmware; `make flash-p4` never
  touches it. Slave updates (if ever needed): esp-hosted OTA from the P4, or
  the board's P1 header + esptool.

Hardware bring-up watchlist (validate in this order):
1. Display: Arduino_GFX rotation=1 goes through a per-pixel rotated bitmap
   path — check flow-graph fps; fall back to LVGL-side rotation if slow.
2. Touch: swap/mirror flags in board_config.h are best-guess; serial logs one
   line per press for calibration.
3. BLE early: no Waveshare example demonstrates P4 BLE at all. Known bug
   esp-hosted-mcu#180: scan results stall after ~60-90s of continuous
   scanning (our scans are short; reconnects are direct-by-MAC).
4. Battery: BAT_ADC is GPIO20 via an unmeasured divider; `kBatteryAdc = -1`
   until calibrated with a multimeter (wrong divider risks spurious
   low-battery deep-sleep).
