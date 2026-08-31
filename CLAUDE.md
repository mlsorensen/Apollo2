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
  check UI work).
- DOCS SYNC RULE: when features, settings, screens, or boards change, update
  in the same change: MANUAL.md prose + its "Where everything lives" tree +
  TOCs, docs/HARDWARE.md,
  README.md, and the screenshots — refresh the affected docs/img/*.png from
  renders/ and re-run `make docs-img` (tools/annotate_docs.py +
  docs/img/manual/manifest.json; re-measure callout boxes if the layout
  moved — the script warns when a render's size changes).
- Board differences live in `include/platform_esp32/board_config.h` blocks +
  feature macros; driver code never hardcodes pins.

## Git conventions

- Commit as `marcus@turboio.com` (repo-local config). Do NOT add
  "Co-Authored-By: Claude" or other AI-signature trailers.

## Boards / build

Board targets are `<chip>-<panel>` after the Waveshare product names:
`make build` (default, the 2-inch S3), `build-s3-7b`, `build-s3-4-3b`,
`build-s3-4-3c`, `build-p4-4-3`, `build-p4-5`; matching `flash-*` targets
auto-detect the port and can probe a running board's serial banner (pre-rename
names like `build-p4`/`flash-7b` remain as aliases). All envs + `sim` must
compile before committing platform changes.

The firmware embeds the History web page as a GENERATED, git-ignored header
(`include/platform_esp32/webapp_dist.h`). Every device build/flash target
depends on it, so `make build` rebuilds it from `tools/webapp/` when stale —
that needs **node** (a real dev dependency; `make sim` does not need it). Bare
`pio run` does NOT generate it: run `make webapp` first. Never commit the
header or hand-edit `tools/webapp/dist/`.

### ESP32-P4-WIFI6-Touch-LCD-5 (env `esp32-p4-micra-5`) — NOT yet HW-verified

Electronically the P4 4.3 (same radio/audio/battery/paddle wiring — everything
in that section applies, including the rev v1.x chip_variant); only the panel
differs: 5" 720x1280 HX8394 over the same 2-lane DSI. Panel deltas, all from
Waveshare's BSP (esp32_p4_wifi6_touch_lcd_5 + esp_lcd_hx8394): different DCS
init table (display.cpp, selected by BOARD_DSI_PANEL_HX8394), reset asserts
HIGH (kLcdRstActiveHigh), backlight LEDC is normal polarity (kBacklightActiveLow
= false) with NO boost-enable pin, GT911 rst/int not wired to the P4 (probe
only), 58 MHz DPI / 700 Mbps lanes. UI: `BOARD_UI_SCALE 1.5f` renders the wide
800x480 layout at 1.5x via ui::dp()/ui::font_dp() (see include/ui/screen.h) —
scale 1.0 boards are bit-identical, verified against baseline renders.

### ESP32-P4-WIFI6-Touch-LCD-X 7"/8"/10.1" (envs `esp32-p4-micra-x-7` / `-x-8` / `-x-10-1`) — 7" verified on HW (2026-08-29: boot, display, touch, hosted link, paddle sense); 8"/10.1" NOT yet

The finished-box (all-in-one HMI) family. Electronics = the P4 4.3/5 (same
I2C 7/8, GT911 probe-only, battery GPIO20 ÷3, ES8311 + PA GPIO53, SD 39-44 on
LDO4, paddle 51/52 on the 40-pin header — GND/52/51 run consecutively in one
pin column like the 5). Deltas, all verified against the X schematic +
Waveshare BSP (waveshareteam/Waveshare-ESP32-components,
bsp/esp32_p4_wifi6_touch_lcd_x):

- **Silicon: X boards exist in BOTH revision generations.** Waveshare's X
  repo says current boards ship rev v3.0+ (400 MHz) and defaults its CI to a
  rev3 profile — but our first real unit (7", bought 2026-08) is **rev v1.3**
  (esptool-verified after a rev3-built image reproduced the exact bootloader
  illegal-instruction loop from the silicon-revision section below). The
  boards jsons therefore use `chip_variant` `"esp32p4_es"` like the other P4
  boards. A genuine rev3 unit fails the same way in mirror image and needs
  `"esp32p4"` — always run `esptool chip-id` on a new board first.
- Panels (native portrait, rotated like the other P4 DSI boards): 7" =
  720x1280 ILI9881C (80 MHz DPI, 1000 Mbps), 8" and 10.1" = 800x1280 JD9365
  (80 MHz, 1500 Mbps) — but the 8" and 10.1" glasses take DIFFERENT vendor
  init tables (the BSP's #if/#else), so each size is its own env/image.
  Tables live in display.cpp (BOARD_DSI_PANEL_ILI9881C / _JD9365 /
  _JD9365_10). Reset active-LOW (unlike the 5's HX8394).
- The 7" BOX MOUNTS ITS PANEL 180° from the P4-5 convention (camera on top =
  correct orientation; HW-verified). Fixed at the panel: both MADCTL writes
  in the ILI9881C table are 0x03 (GS|SS scan flip — both bits = true 180°,
  one alone would mirror), and the 7"'s touch flags toggle BOTH mirrors vs
  the P4-5 values (per-size #if in the X block). Careful editing that table:
  0x36 also appears as a page-1 GIP register mid-table — only the page-0
  writes are MADCTL. Expect the same 180° question on the 8"/10.1" at
  bring-up (check camera position; JD9365 has the same GS/SS bits).
- Backlight: LEDC GPIO26 normal polarity + AP3032 boost-enable GPIO23
  (kLcdBacklightEn — the 4.3 has one too, the 5 doesn't).
- UI scale: 7" = 1.5 (same logical 853x480 as the 5); 8"/10.1" = 1.6 →
  logical 800x500 — exact 800 width, the extra height feeds the flex-grow
  regions (sim renders at 1280x800 cover it).
- Extras we don't drive: ES7210 mic ADC, second USB OTG (GPIO24/25), camera.
- The 10.1" env exists and builds (`esp32-p4-micra-x-10-1`, its own JD9365_10
  table) but is deliberately UNRELEASED and unmarketed — no hardware to test
  it on, so it's kept out of the README, web flasher, and release matrix
  (owner's call 2026-08-29). Don't re-add it to those without asking.

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
