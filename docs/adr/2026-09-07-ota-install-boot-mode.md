# OTA install runs in its own early-boot mode, not inside the running app

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-09-07 · **Last changed:** 2026-09-22
- **Boards:** all released boards; the SPI 2-inch dev board is a no-op

## Context

Self-update has two halves: a CHECK that fetches `releases.json` and the
notes, and an INSTALL that downloads a 2.5 to 3.8 MB app image into the
spare OTA slot. The check ran live once mbedTLS was pointed at PSRAM. The
install did not: on the P4 boards WiFi and BLE share the esp-hosted SDIO link
to the on-board ESP32-C6, and every in-app download crashed the radio driver
with `assert failed: sdio_rx_get_buffer`.

## Decision

"Install now" stashes the target version in NVS (`Config::pending_install`)
and reboots. Very early in `setup()`, before the full display, BLE or the app,
`install_mode::run()` brings up a LIGHT display, downloads the whole image
into PSRAM with no flash writes, blanks the backlight, writes PSRAM to flash
and reboots. Nothing is torn down live and no extra slot is needed. The new
image boots `PENDING_VERIFY` (`verifyRollbackLater()` returns true) and is
marked valid only after 60 s of healthy `loop()`, so a bootlooping install
reverts on its own.

| Backend | Display in install mode | Why |
|---|---|---|
| DSI (P4) | `install_panel_*`: same per-board init, `num_fbs=2`, no `use_dma2d`, no async sync | leaves a ~100 KB largest DMA block |
| RGB (S3) | the normal `Display`, backlight blanked for the write | native WiFi, no pool problem; only the flash-write strobe remains |
| SPI (2-inch, internal-only) | none; the reboot does nothing | no update UI there |

## Evidence

Measured on the P4-5 in the standalone `esp32-p4-dltest` env (`src/dltest/`,
kept as the proving ground), 2026-09-07 and 2026-09-08 (the download and
flash times are in `89e1d0e`; the buffer thresholds and the display's DMA
footprint are from the working notes of those two days):

| Finding | Value |
|---|---|
| Buffer the SDIO RX driver needs during a bulk download | one ~64 KB CONTIGUOUS internal-DMA block (fails at 40 and 56 KB largest, works at 65 KB) |
| Internal DMA used by the real display (`use_dma2d` + async double buffer) | ~347 KB; largest free block ~16 KB |
| Largest free block with the light display | ~100 KB |
| Download to PSRAM, 2.5 MB | ~4 s (streaming to flash was write-bound at ~100 KB/s) |
| PSRAM to flash | ~17 s, erase-bound |

The panel strobe is a separate mechanism: `esp_ota_write` disables the cache
and the DSI scans its framebuffer from PSRAM through it. Download plus
present with no flash write was clean; a flash write with a frozen UI still
strobed. Hence the dark write.

## Alternatives and dead ends

- Parking BLE during an in-app download (`d667f68`): crashed with BLE fully
  down (bench, per the working notes; the commit itself still says "to
  prove"). The flood is the download's own WiFi traffic on the shared link.
- Ranged 128 KB reads with an idle gap between them (`d667f68`), and a hard
  DMA-headroom gate before any TLS fetch (`ac6f0ae`) that effectively
  disabled the check on the P4: both dropped in `89e1d0e`. Smaller 4 to 8 KB
  reads were listed as a lever and never tried.
- Notify-only (`9140cf2`): the user was sent to the web flasher. Committed on
  2026-09-07 and superseded by the boot-mode design the same evening, so it
  never shipped on its own; both first appear in v0.11.0.
- A live deinit of the running display: fiddly and risky; the reboot gives a
  clean baseline for free.

## Consequences

- The install's NTP sync is a "network really up" failsafe only: the
  prebuilt core's mbedTLS has `MBEDTLS_HAVE_TIME_DATE` off, so certificate
  dates are never checked (verified 2026-09-19).
- Bootloader and partition-table changes cannot ship this way (app slot
  only); see the 16 MB and flash-epoch records.
- The check HEADs this board's exact image before offering a version
  (`9a24cb5`): on 2026-09-22 three units rebooted into install mode while the
  CDN had the index but not yet their images. A board whose C6 slave updates
  on the first boot after an OTA marks the image valid before that restart,
  otherwise the bootloader counts it as a bootloop.

## History

- 2026-09-07: in-app attempts crash the hosted radio; BLE parking ruled
  out (`d667f68`); notify-only committed (`9140cf2`).
- 2026-09-07: root cause measured in dltest; boot-mode design folded in
  (`89e1d0e`); S3 RGB path (`51ebec6`).
- 2026-09-08: shipped in v0.11.0 (`e85519f`, re-cut the same day as
  `f8637cc` with the under-16 MB P4 table, see that record); progress bar
  animated (`1d52c0f`), notice deferred under the screensaver (`f27b389`).
  The 4.3C took the same installs through v0.12.x (bench, 2026-09-11).
- 2026-09-22: offer only once the image is served; survive the C6 restart
  (`9a24cb5`).

## Sources

`89e1d0e`, `51ebec6`, `d667f68`, `ac6f0ae`, `9140cf2`, `9a24cb5`;
`src/platform_esp32/install_mode.cpp`, `display.cpp` (`install_panel_*`),
`src/dltest/`, `src/device/main.cpp`.
