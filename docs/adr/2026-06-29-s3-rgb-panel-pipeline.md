# RGB panel pipeline on the S3 boards: single framebuffer, bounce buffers, and the ghost that was a parity bit

- **Status:** Accepted
- **Area:** display
- **First decided:** 2026-06-29 · **Last changed:** 2026-09-11
- **Boards:** S3-4.3C (released), S3-4.3B and S3-7B (internal-only)

## Context
The S3 RGB panels have no memory of their own: the framebuffer lives in
PSRAM and the LCD peripheral scans it continuously through two small
internal bounce buffers the IDF driver refills from a DMA end-of-buffer
interrupt. With one framebuffer the flow graph tears (partial flushes
during scan), and the driver's bounce bookkeeping can latch a corrupted
picture that only a reboot clears ([the symptom](#ev-ghost)).

## Decision
- The RGB path stays on Arduino_GFX with ONE framebuffer and the vendor
  bounce size (`kRgbBouncePx` = width x 10 lines); the LVGL draw buffer is
  200 lines in PSRAM. Cosmetic tearing on the graph is accepted ([a double
  framebuffer runs at ~5 fps](#ev-pipeline); [an internal-RAM draw buffer
  starves WiFi](#alternatives-and-dead-ends)).
- The ghost is the driver's bounce-buffer PARITY latch: `rgb_vsync_cb`
  zeroes the driver's private `bb_eof_count` every frame, armed only after
  `rgb_bounce_fix_arm` has validated the IDF 5.5.x struct layout at boot
  ([mechanism and A/B](#ev-parity); [what it checks](#sources)).
- A VSYNC-aligned `esp_lcd_panel_init` resync (`Display::rgb_resync`) runs
  once, 2 s after boot ([0 fresh ghosts in 60 aligned trials](#ev-ghost));
  there is no manual "Restart display" row ([removed](#history)).
- The 4.3C keeps PCLK at 14 MHz (vendor value 16); the 7B and 4.3B keep
  their vendor clocks ([refresh cost](#ev-ghost)).

## Consequences
- The parity fix depends on the IDF 5.5.x struct layout; after a platform
  bump the boot line `RGB: bounce parity fix armed` is the proof it still
  applies. Each absorbed flip is logged from `loop()`.
- PCLK 14 MHz was never measured in isolation; it costs no RAM and ~5 fps
  of refresh LVGL cannot use, so it stays as a setting, not as evidence.
- Bounce size and task stacks are DMA-pool budget ([x12 cost
  6.4 KB](#alternatives-and-dead-ends)).
- No ghost has been recorded in the notes or commits since the fix landed
  on 2026-09-10.

---

## Details

### Evidence

<a id="ev-pipeline"></a>
Bounce buffer and framebuffer count (2026-06-29 to 07-01):

| Date | Board | Observation |
|---|---|---|
| 2026-06-29 | 7B | 30 MHz PCLK from PSRAM flickers/tears without a bounce buffer; width x 10 fixes it |
| 2026-07-01 | 4.3B | esp_lcd double framebuffer: tear-free, but FULL-mode re-render is 126 ms (~5 fps) against the single-FB ~13 fps |

<a id="ev-ghost"></a>
The ghost and the underrun theory (2026-07-25, 4.3C). The symptom, seen on
every RGB board from the first S3 units and most on the 4.3C: a latched
corrupted picture, every tenth row flickering with the band above, the
frame about ten pixels high with the bottom wrapped to the top, sometimes
from power-on, cleared only by a reboot or luck.

| Date | Board | Observation |
|---|---|---|
| 2026-07-25 | 4.3C | ghost on ~1/4 of cold boots, or latched while serving web pages |
| 2026-07-25 | 4.3C | mid-frame `esp_lcd_panel_init` latches a fresh ghost ~30 % of the time; VSYNC-aligned 0 in 60 trials |
| 2026-07-25 | 4.3C | PCLK 16 to 14 MHz: refresh ~39 to ~34 fps; LVGL delivers 25-30 anyway |

<a id="ev-parity"></a>
Root cause: the parity latch (2026-09-10, 4.3C).

| Date | Board | Observation |
|---|---|---|
| 2026-09-10 | 4.3C | telemetry on the driver's counter: a 47-EOF frame flipped parity odd at the second the ghost appeared, odd every frame after |
| 2026-09-10 | 4.3C | A/B over six `GET /coredump?erase=1` flash erases: fix out, parity latched odd and the screen ghosted until reboot; fix in, every flip healed on the next frame, one visible blip |
| 2026-09-10 | 4.3C | bounce x12 to x10 and task-stack trims: largest DMA-capable block 2,932 to 21,492 B idle |

Mechanism: the driver picks the bounce buffer to refill as
`bb_eof_count % 2`. Two DMA EOFs serviced by one interrupt (any core-1
latency over one bounce period, ~585 us here; a flash write with the cache
off guarantees it) advance the counter once for two buffers. In the
prebuilt libs' `RESTART_IN_VSYNC` build nothing resets it, so every refill
lands in the buffer being scanned. The flash-erase repro is deterministic.

### Alternatives and dead ends
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

### History
- 2026-06-29: 7B bounce buffer added (`526eb5e`).
- 2026-07-01: double-framebuffer verdict recorded, single FB kept (`3614e5f`).
- 2026-07-25: underrun theory: 14 MHz + x12, VSYNC-aligned resync (`9690c5c`).
- 2026-09-10: x12 reverted with the task-stack trims (`4b69e95`, v0.12.2);
  root cause: parity latch, fix in `on_vsync` (`4380721`, v0.12.3). The
  underrun story was superseded: the symptom was real, the mechanism was
  bookkeeping, not bandwidth.
- 2026-09-11: the manual "Restart display" row dropped (`4c2beef`), its
  handler left unbound; the boot-time one-shot resync stays.

### Sources
`src/platform_esp32/display.cpp` (`RgbPanelMirror`, the mirrored struct
prefix; `rgb_vsync_cb`, the `on_vsync` callback; `rgb_bounce_fix_arm`,
guarded by IDF version, 5.5.x and S3 only, validating framebuffer and
bounce sizes, EOF count, resolution, pixel clock and buffer pointers at
boot, and on a mismatch staying disarmed and saying so in the log;
`rgb_resync`, `rgb_poll`), `src/device/main.cpp` (the 2 s one-shot
in `loop()`), `include/platform_esp32/board_config.h` 4.3C block,
commits `526eb5e`, `3614e5f`, `280bedf`, `e1842d2`, `9690c5c`, `4b69e95`,
`4380721`, `4c2beef`, CHANGELOG v0.12.2 and v0.12.3.
