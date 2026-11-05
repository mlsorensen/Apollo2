# RGB panel pipeline on the S3 boards: single framebuffer, bounce buffers, and the ghost that was a parity bit

- **Status:** Accepted
- **Area:** display
- **First decided:** 2026-06-29 · **Last changed:** 2026-09-11
- **Boards:** S3-4.3C (released), S3-4.3B and S3-7B (internal-only)

## Context
The S3 RGB panels have no memory of their own: the framebuffer lives in
PSRAM and the LCD peripheral scans it continuously through two small
internal bounce buffers the IDF driver refills from a DMA end-of-buffer
interrupt. Two problems followed: the flow graph tears (one framebuffer,
partial flushes during scan), and from the first S3 boards every RGB board
(the 4.3C most watched) would occasionally latch a corrupted picture, every
tenth row flickering with the band above, the frame about ten pixels high
with the bottom wrapped to the top, sometimes from power-on, cleared only
by a reboot or luck.

## Decision
- The RGB path stays on Arduino_GFX with ONE framebuffer and the vendor
  bounce size (`kRgbBouncePx` = width x 10 lines); the LVGL draw buffer is
  200 lines in PSRAM. Cosmetic tearing on the graph is accepted.
- The ghost is the driver's bounce-buffer PARITY latch; our `on_vsync`
  callback zeroes the driver's private `bb_eof_count` every frame
  (`rgb_vsync_cb`, display.cpp) through a mirrored struct prefix, guarded by
  IDF version (5.5.x, S3 only) and validated at boot against framebuffer
  and bounce sizes, EOF count, resolution, pixel clock and buffer pointers
  (`rgb_bounce_fix_arm`). On a mismatch it stays disarmed and says so in
  the log.
- A VSYNC-aligned `esp_lcd_panel_init` resync (`Display::rgb_resync`)
  remains as a one-shot at 2 s after boot (`loop()`, main.cpp); the manual
  "Restart display" row went on 2026-09-11, its handler left unbound.
- The 4.3C keeps PCLK at 14 MHz (vendor value 16); the 7B and 4.3B keep
  their vendor clocks.

## Evidence
| Date | Board | Observation |
|---|---|---|
| 2026-06-29 | 7B | 30 MHz PCLK from PSRAM flickers/tears without a bounce buffer; width x 10 fixes it |
| 2026-07-01 | 4.3B | esp_lcd double framebuffer: tear-free, but FULL-mode re-render is 126 ms (~5 fps) against the single-FB ~13 fps |
| 2026-07-25 | 4.3C | ghost on ~1/4 of cold boots, or latched while serving web pages |
| 2026-07-25 | 4.3C | mid-frame `esp_lcd_panel_init` latches a fresh ghost ~30 % of the time; VSYNC-aligned 0 in 60 trials |
| 2026-07-25 | 4.3C | PCLK 16 to 14 MHz: refresh ~39 to ~34 fps; LVGL delivers 25-30 anyway |
| 2026-09-10 | 4.3C | telemetry on the driver's counter: a 47-EOF frame flipped parity odd at the second the ghost appeared, odd every frame after |
| 2026-09-10 | 4.3C | A/B over six `GET /coredump?erase=1` flash erases: fix out, parity latched odd and the screen ghosted until reboot; fix in, every flip healed on the next frame, one visible blip |
| 2026-09-10 | 4.3C | bounce x12 to x10 and task-stack trims: largest DMA-capable block 2,932 to 21,492 B idle |

Mechanism: the driver picks the bounce buffer to refill as
`bb_eof_count % 2`. Two DMA EOFs serviced by one interrupt (any core-1
latency over one bounce period, ~585 us here; a flash write with the cache
off guarantees it) advance the counter once for two buffers. In the
prebuilt libs' `RESTART_IN_VSYNC` build nothing resets it, so every refill
lands in the buffer being scanned. The flash-erase repro is deterministic.

## Alternatives and dead ends
- Direct esp_lcd RGB with two framebuffers and a VSYNC swap (built
  2026-06-30 to 07-01 on the 4.3B, never merged; it survives only as a
  stash on the bench clone, "esp_lcd double-FB v2" on
  feat/scale-integration, still `stash@{0}` on 2026-11-04): LVGL
  DIRECT mode assumes a stable source and the scrolling canvas moves between
  the two buffers' renders; FULL mode is correct but ~5 fps. Dropped. Routes
  if ever revisited: scroll the strip inside both framebuffers, or a panel
  with its own GRAM.
- `esp_lcd_rgb_panel_restart()`: returned OK and healed nothing.
- Periodic auto-resync every 5 s: the one-frame ~5 px hop was distracting;
  dropped 2026-07-25 for the single boot-time shot.
- Bounce x12 as an underrun margin (2026-07-25): its one supporting
  observation was confounded by the web server moving to the main loop an
  hour earlier, it cost 6.4 KB of the internal DMA pool TLS was failing
  from, and the ghost recurred at x12 as at x10. Reverted 2026-09-10.
- An internal-RAM LVGL draw buffer, twice: a 200-line one starved
  `esp_wifi_init` so the setup SoftAP failed with NO_MEM (2026-06-30,
  `17b113a`), and a 32-line ~51 KB one (`280bedf`, 2026-07-18) gave a
  ~2.5x faster settings scroll but with NimBLE up left ~9 KB of internal
  heap and WiFi failed on every attempt; reverted the same day (`e1842d2`),
  the call made: working WiFi over scroll fps.

## Consequences
- The parity fix depends on the IDF 5.5.x struct layout; after a platform
  bump the boot line `RGB: bounce parity fix armed` is the proof it still
  applies. Each absorbed flip is logged from `loop()`.
- PCLK 14 MHz on the 4.3C was never measured in isolation; it costs no RAM
  and ~5 fps of refresh LVGL cannot use, so it stays, as a setting, not as
  evidence.
- No ghost has been recorded in the notes or commits since 2026-09-10.

## History
- 2026-06-29: 7B bounce buffer added (`526eb5e`).
- 2026-07-01: double-framebuffer verdict recorded, single FB kept (`3614e5f`).
- 2026-07-25: underrun theory: 14 MHz + x12, VSYNC-aligned resync (`9690c5c`).
- 2026-09-10: x12 reverted with the task-stack trims (`4b69e95`, v0.12.2);
  root cause: parity latch, fix in `on_vsync` (`4380721`, v0.12.3). The
  underrun story was superseded: the symptom was real, the mechanism was
  bookkeeping, not bandwidth.
- 2026-09-11: "Restart display" row dropped (`4c2beef`).

## Sources
`src/platform_esp32/display.cpp` (`RgbPanelMirror`, `rgb_vsync_cb`,
`rgb_bounce_fix_arm`, `rgb_resync`, `rgb_poll`), `src/device/main.cpp`
(the 2 s one-shot), `include/platform_esp32/board_config.h` 4.3C block,
commits `526eb5e`, `3614e5f`, `280bedf`, `e1842d2`, `9690c5c`, `4b69e95`,
`4380721`, `4c2beef`, CHANGELOG v0.12.2 and v0.12.3.
