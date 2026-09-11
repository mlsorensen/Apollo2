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
  README.md, and the screenshots. NOTE the user-facing docs cover only the
  RELEASED boards (see "Support matrix" below) — a change to an internal-only
  board updates CLAUDE.md and the code, and must NOT add that board to
  README/HARDWARE/site — refresh the affected docs/img/*.png from
  renders/ and re-run `make docs-img` (tools/annotate_docs.py +
  docs/img/manual/manifest.json; re-measure callout boxes if the layout
  moved — the script warns when a render's size changes).
- Board differences live in `include/platform_esp32/board_config.h` blocks +
  feature macros; driver code never hardcodes pins.

## OTA self-update (v0.11+)

Two separate parts: a CHECK/notify port (`update_check.*`) and an INSTALL that
runs in a dedicated early-boot mode (`install_mode.*`). Full rationale +
measurements: the `ota-p4-solution` memory. The short version of WHY install is
its own boot mode: a bulk download over the hosted-radio SDIO link needs a
~64 KB CONTIGUOUS internal-DMA buffer, and Apollo's graph-optimized DSI display
(use_dma2d + async double-buffer) fragments that pool to ~16 KB → the download
asserts `sdio_rx_get_buffer`; and flash writes disable the cache, stalling the
DSI's PSRAM framebuffer scan → the panel strobes. Both go away when the full
display isn't up.

- Artifact contract: `<site>/<tag>/firmware/app/<kUpdateSlug>.bin` (app-only
  image). The parts/ files are NOT usable for OTA — they bundle otadata with
  the app. CI asserts kUpdateSlug (board_config.h) == the matrix `board` field.
- Check (update_check.cpp): a short-lived task fetches releases.json + notes.txt
  via esp_http_client + `crt_bundle_attach` (the cert bundle in the core libs;
  the Arduino NetworkClientSecure wrapper CANNOT reach it). mbedTLS is pointed
  at PSRAM (use_psram_for_tls) so the small fetch runs with BLE up. Cadence Off/
  On boot/Daily (Config::update_check_mode). Notify only — never installs.
- Install (install_mode.cpp): "Install now" stashes the version in NVS
  (Config::pending_install) and reboots. VERY EARLY in setup() — before the full
  display / BLE / app — main runs install_mode::run(): a LIGHTWEIGHT display
  (display.cpp `install_panel_*`: same per-board DSI init but num_fbs=2, no
  use_dma2d, no async sync) shows a progress UI, downloads the whole image into
  PSRAM (no flash writes → clean), then blanks the backlight and writes
  PSRAM→flash (screen glitches during flash writes regardless) and reboots.
  Per display: DSI uses the light panel above; RGB (native-WiFi S3, no DMA-pool
  problem) uses the normal full Display and just blanks the backlight for the
  flash write. No-op only on the SPI 2-inch dev board (no real update UI there).
  Clock only needed for the cert dates: install mode inherits the RTC and only
  NTP-syncs as a failsafe.
- Rollback: BOOTLOADER_APP_ROLLBACK_ENABLE=y in all cores. main.cpp overrides
  `verifyRollbackLater()` -> true and marks the image valid after 60 s of
  healthy loop() — an image that bootloops is auto-reverted. Consequence:
  bootloader/partition-table changes CANNOT ship via OTA (app slot only).
- P4 16MB-FLASH-BOUNDARY RULE (found the hard way, see the
  `ota-p4-16mb-boundary` memory): the P4's 2nd-stage bootloader mis-reads flash
  ABOVE 0x1000000 (16MB) during its boot-time image verify (3-byte addressing
  wraps), so an OTA'd app that spills past 16MB is rejected as "invalid segment
  length" and rolls back — even though the flash content is byte-perfect (the
  write + the running app's reads are fine; only the bootloader's read wraps).
  FIX: `boards/p4_ota_under16.csv` (7MB app slots) keeps BOTH app slots entirely
  under 16MB (app1 ends ~14MB); all P4 envs use it via the base env. Never let a
  P4 app partition or its image cross 16MB. This is a partition-table change, so
  existing P4 units need a one-time web-flash to adopt it (then OTA works). S3
  boards are 16MB flash — no >16MB region, unaffected.
- C6 auto-update (c6_update.cpp): on every P4 boot, if the on-board ESP32-C6
  esp-hosted slave is older than the host lib, flash the embedded matching image
  (`firmware_blobs/c6_slave.bin`, committed, embedded on all P4 envs) to the C6
  over SDIO and reboot. Fixes the stale-factory-slave boot-WiFi delay. Bump the
  blob when the pinned platform's esp-hosted host version changes.
- Proving ground: the `esp32-p4-dltest` env (src/dltest/) is the standalone
  firmware the install mode was developed + verified in; keep it.

## Memory budget rule (owner, 2026-09-10)

**Never introduce more RAM use without review and a full explanation.** That
means: no task-stack increase, no new task, no larger buffer, no new
static/heap allocation on any device env — internal RAM, DMA-capable RAM or
PSRAM — without first stating what it costs (bytes, which pool, on which
boards), why the cheaper alternative doesn't work, and getting the owner's
OK. The S3 boards run a few KB from starvation (esp-aes/TLS and WiFi have
both failed for lack of internal RAM); a "harmless" 1.5 KB stack bump is
exactly how that happens. Prefer restructuring (move the work to a task that
already has the room, log from loop() instead of a small task, reuse an
existing buffer) over adding memory. Applies to fixes as much as features.

## Release checklist (every tag, no exceptions)

1. `include/version.h` — bump `fw::kVersion` to the tag (minus the `v`). The
   device compares THIS to releases.json; forget it and the new image reports
   the old version and re-prompts forever (v0.12.3, 2026-09-10). CI now fails
   the build if the binary's kVersion != the tag, but bump it first anyway.
2. `CHANGELOG.md` — real notes under `## vX.Y.Z` (the workflow extracts them).
3. `make build-release` green (or `make build-all` for a platform change).
4. Commit, push main, then `gh workflow run firmware-release.yml -f tag=vX.Y.Z`
   (CI creates the tag + Release; never build release binaries locally).

## Git conventions

- Commit as `marcus@turboio.com` (repo-local config). Do NOT add
  "Co-Authored-By: Claude" or other AI-signature trailers.

## Support matrix (READ BEFORE TOUCHING ANY BOARD LIST)

FOUR boards are released and advertised. Nothing else is, on purpose:

| env | product | role | slug |
|---|---|---|---|
| `esp32-p4-micra-5` | P4-WIFI6-Touch-LCD-5 | mount-on-Micra, the default pick | `p4-wifi6-touch-lcd-5` |
| `esp32-p4-micra-43` | P4-WIFI6-Touch-LCD-4.3 | mount-on-Micra, smaller glass | `p4-wifi6-touch-lcd-4.3` |
| `esp32-s3-micra-4-3c` | S3-Touch-LCD-4.3C BOX | mount-on-Micra, nothing to build | `s3-touch-lcd-4.3c` |
| `esp32-p4-micra-x-8` | P4-WIFI6-Touch-LCD-X 8" | counter-top companion | `p4-wifi6-touch-lcd-x-8` |

Positioning (keep the docs saying this): the **P4-5 is the recommendation** for
mounting on the machine — it just needs a printed shell and, for Auto shot, a
DIY opto cable. The **P4-4.3 is THE SAME BOARD with smaller glass** — the -5 env
literally `extends` the -43 env, and the only board_config deltas are panel
controller, DSI timings, reset/backlight polarity, touch rst/int and UI scale.
Everything functional (P4NRW32, C6 radio, ES8311 audio, battery path, SD, paddle
pins) is byte-identical. **Choose between them on screen size and a small price
difference — nothing else.** The **4.3C earns its place by removing BOTH DIY
steps**: it is the only supported board that is a finished box *and* has
opto-isolators on-board (three wires into screw terminals). It is the slower
part; that is the trade. The **X 8"** is the counter-top box.

Every OTHER env — `esp32-s3-micra` (2"), `-7b`, `-4-3b`, `-x-7`, `-x-10-1` — is
**INTERNAL-ONLY**: it still builds, is still maintained,
and is deliberately absent from the release matrix, the web flasher and every
user-facing doc. They are kept as the record of how those boards differ, and as
dev hardware (the 7" X box is the owner's desk unit). **Do not add one back to
README.md, docs/HARDWARE.md, site/index.html or the CI matrix without asking.**
Nothing is deleted to de-advertise a board — only de-listed.

The lists that must stay in sync when this changes: the
`.github/workflows/firmware-release.yml` matrix, `site/index.html` (board cards
*and* `BOARD_PATTERNS`), README.md ("Which board?", the 3D-print table, the
flash/`pio run` lists), docs/HARDWARE.md, and the `[RELEASED]`/`[INTERNAL-ONLY]`
markers in platformio.ini + board_config.h.

- **OTA on de-listed boards: knowingly broken, do not "fix" it.** `releases.json`
  is global, so an internal-only build still sees "update available" and its
  install 404s and rolls back harmlessly. Accepted because none of these boards
  are expected to be in anyone's hands — they get flashed over USB. Revisit only
  if one actually sees use. No `kOtaPublished` flag, no UI gating.
- **Coming: rev v3 production silicon for the P4 boards (BOTH the 5 and the
  4.3).** Samples are expected; the owner will buy a v3 P4-4.3 to test at least
  once, and the v3 P4-5 is expected to validate the pattern for the family.
  **The product list stays FOUR — the silicon split doubles the IMAGES, not the
  boards.** Waveshare does not change the SKU, so a buyer cannot tell which
  silicon they got, and surfacing it as a separate product in the README/board
  table would be wrong. Call it out in the DEVELOPER docs only; the flasher
  handles selection. Shape of it, per P4 product:
    * a SECOND board block + env + board json (`chip_variant "esp32p4"`,
      `BOARD_P4_SILICON_REV3` defined — already the macro display.cpp uses for
      the DSI PHY's XTAL PLL reference), slug `<plain-slug>-rev3`.
    * the PLAIN slug keeps meaning rev v1.x ("es") — never rename it, fielded
      units self-update from that path.
    * the flasher keeps ONE card per product and chooses the image: Detect
      reads the `REV=` field the identify banner already emits
      (`src/device/main.cpp`, efuse major*100+minor, >= 300 = rev3). A first
      install on a blank board can't be probed, so the card needs an explicit
      silicon choice there.
  **Picking wrong is recoverable and that is fine** — the board just won't come
  up until the other image is flashed; nothing is damaged. Don't over-engineer
  the guard-rails for it.

## Boards / build

Board targets are `<chip>-<panel>` after the Waveshare product names.
Released: `build-p4-5`, `build-s3-4-3c`, `build-p4-x-8`. Internal-only:
`make build` (default, the 2-inch S3), `build-s3-7b`, `build-s3-4-3b`,
`build-p4-4-3`, `build-p4-x-7`, `build-p4-x-10-1`. Matching `flash-*` targets
auto-detect the port and can probe a running board's serial banner (pre-rename
names like `build-p4`/`flash-7b` remain as aliases). Two sweeps:
`make build-release` (the three released envs + sim — run this before pushing a
platform change or cutting a release) and `make build-all` (every env + sim).

WHEN to run full builds: `make build-all` must pass before you PUSH or cut a
RELEASE of a platform change — not on every edit. During debug/development stay
on `make sim` (fast, no node, no device contention) and at most the ONE device
env you're actually testing on. Full sweeps mid-investigation are slow, contend
for the shared `.pio` dir (see the build-workflow notes about flashing), and
tell you nothing you don't already know until the fix is finished.

The firmware embeds the History web page as a GENERATED, git-ignored header
(`include/platform_esp32/webapp_dist.h`). Every device build/flash target
depends on it, so `make build` rebuilds it from `tools/webapp/` when stale —
that needs **node** (a real dev dependency; `make sim` does not need it). Bare
`pio run` does NOT generate it: run `make webapp` first. Never commit the
header or hand-edit `tools/webapp/dist/`.

### ESP32-P4-WIFI6-Touch-LCD-5 (env `esp32-p4-micra-5`) — HW-VERIFIED, the owner's daily-driver board and the default recommendation

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

RELEASE STATUS: the **8" is the released counter-top product**; the **7" is
INTERNAL-ONLY** (the owner's desk/dev unit — de-listed 2026-09-10, not a
capability judgement) and the 10.1" has never been released. Only the 8"
appears in CI, the web flasher and the docs.

The finished-box (all-in-one HMI) family. Electronics = the P4 4.3/5 (same
I2C 7/8, GT911 probe-only, battery GPIO20 ÷3, ES8311 + PA GPIO53, SD 39-44 on
LDO4, paddle 51/52 on the 40-pin header — GND/52/51 run consecutively in one
pin column like the 5). Deltas, all verified against the X schematic +
Waveshare BSP (waveshareteam/Waveshare-ESP32-components,
bsp/esp32_p4_wifi6_touch_lcd_x):

- **Silicon: X boards exist in BOTH revision generations — PER UNIT, not per
  size.** The user's 7" (bought 2026-08) is **rev v1.3**; the user's 8" is
  **rev v3.2** (verified in the countertop-display sister project, 2026-09).
  The boards jsons currently all use `esp32p4_es`, which boots the 7" but
  made the v0.9.0 x-8 image WRONG for the real 8": on rev3 the wrong variant
  boot-loops with `CHIP_LP_WDT_RESET` before printing anything — looks like a
  dead board. FIXED post-0.9.0: the x-8 and x-10-1 jsons now use `"esp32p4"`
  (400 MHz real; display.cpp selects the XTAL DSI-PHY PLL ref for those envs
  and the 8" JD9365 table carries the MADCTL 180° glass flip). The 7" json
  stays `esp32p4_es`. Always `esptool chip-id` a new unit first.
- **X-8 bring-up learnings (from the sister project, HW-verified there):**
  (1) rev3 DSI PHY: the legacy `MIPI_DSI_PHY_CLK_SRC_DEFAULT` (PLL_F20M)
  aborts inside the HAL on rev3 — it needs `MIPI_DSI_PHY_PLLREF_CLK_SRC_
  DEFAULT` (XTAL). Now selected by the `BOARD_P4_SILICON_REV3` macro, which a
  board's block in board_config.h defines when its json is `chip_variant
  "esp32p4"` — keyed on silicon, not on panel, so a rev3 build of ANY P4 board
  gets it. (A runtime `efuse_hal_chip_revision() >= 300` check would also work
  but buys nothing: chip_variant is a link-time split, so rev1 and rev3 are
  separate binaries regardless.) (2) Our JD9365 8"
  table + 80 MHz/1500 Mbps timings worked first time (panel up in ~340 ms).
  HEADS-UP (user, 2026-09): the OTHER P4 boards (4.3/5) may eventually ship
  with v3 silicon too. For the RELEASED P4-5 this is now planned work — a
  sample is expected; see the "Support matrix" section for the shape of it
  (second env + json, slug `p4-wifi6-touch-lcd-5-rev3`, plain slug stays ES).
  Prep in place: the serial identify line reports `REV=` (efuse
  major*100+minor) so the web flasher's Detect can pick the right variant, and
  display.cpp's PHY-clock branch already keys on `BOARD_P4_SILICON_REV3` — the
  new board block just defines it.
  (3) The 8" box mounts its glass the OPPOSITE way up from the 7": same
  table renders upside down — fix is rotating 270° instead of 90° in the
  flush (a direction constant; touch mapping follows it). Our per-size touch
  #if was the right structure. (4) GT911 at 0x5D like the others; TP_INT
  unverified on the 8" (the 7" schematic routes it via R32 to GPIO33 —
  relevant if wake-on-touch is ever wanted). (5) Rev3 pads hold state
  through deep sleep (documented; untested) — the v1.x boards' pull-down
  mods shouldn't be needed there.
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

### ESP32-P4-WIFI6-Touch-LCD-4.3 (env `esp32-p4-micra-43`) — RELEASED, HW-verified

Re-added to the released set 2026-09-11 (a user has one). Display, touch,
BLE, **paddle, audio and the battery ADC are all confirmed working on
hardware** (owner, 2026-09-11) — the bring-up list below is kept for the
wiring detail, not as a to-do. No 4.3 hardware is kept here, so the P4-5
(same board, different glass) is the proxy for anything non-panel.

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

Hardware bring-up detail (2026-07-17: boots clean — hosted link, DSI panel,
GT911 all up; NimBLE host inits. Paddle/audio/ADC since confirmed, 2026-09-11):
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
   Config-only (native-GPIO path in paddle.cpp). CONFIRMED working on HW.
5. Audio: config-only reuse of the 4.3C's ES8311 driver — BSP pins MCLK 13 /
   BCLK 12 / LRCLK 10 / DOUT 9, codec at 0x18 on the shared I2C bus, PA
   enable native GPIO53 active-high (BOARD_AUDIO_PA_IOEXT selects expander-vs-
   GPIO PA in sound.cpp). CONFIRMED working on HW.
6. Battery: WORKING — BAT_ADC GPIO20, divider ÷3 (confirmed: raw*3 == 4.20V
   LiPo CV level while charging). The bring-up calibration log that used to sit
   in battery.cpp behind BOARD_WAVESHARE_P4_WIFI6_43 is gone — divider settled. `HWCDC::isPlugged()` is non-functional on
   the P4's USB-Serial-JTAG (always false) — moot now that USB-vs-battery is
   voltage-only (>= kUsbPowerVolts) on all boards. Known hardware trait:
   plugging/unplugging USB with a battery attached FULLY POWER-CYCLES the
   board (reset reason: power-on) — the power path's VBUS<->boost switchover
   drops the rail; not fixable in firmware.
