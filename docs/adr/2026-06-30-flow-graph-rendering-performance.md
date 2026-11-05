# Flow graph rendering and performance: what it costs to scroll a trace on a PSRAM-scanned panel

- **Status:** Accepted
- **Area:** display
- **First decided:** 2026-06-30 · **Last changed:** 2026-11-04
- **Boards:** every board with the wide layout (the 2-inch compact layout has no graph)

## Context
The Home flow graph is the most expensive thing on screen: a trace that
scrolls once per time step, fed at the scale's rate, on panels whose
framebuffers live in PSRAM. Every board hit its own wall: the S3 at ~13 fps
with a tearing graph, the P4-5 at 15 fps with three quarters of each frame
in the flush. This log records what moved the numbers, what did not, and
where the remaining stutter was accepted.

## Decision
What we kept:
- The graph is a self-managed `lv_canvas` strip chart writing RGB565
  directly: memmove the plot left, paint one column, re-blit (2026-06-30).
- The canvas buffer comes straight from PSRAM through `ui::flow_buf_alloc`
  (64-byte aligned); `lv_malloc` is only the no-PSRAM fallback (the host
  sim), tracked so the matching free is used. The 1 MB LVGL pool holds
  widgets only (2026-11-01).
- Periodic UI paths use the change-detecting `ui::set_*` setters, never raw
  LVGL setters; the scroll pace is quantized to the refresh grid.
- On DSI: a cache-tiled rotate, a column-bounded dirty sync on the DMA
  engine in <=128 KB chunks (32 KB when the big descriptor list fails, held
  for 2 s), draw buffer always in PSRAM.
- Shot window growth: Snap / Smooth / Continuous (default Continuous); any
  mapping change is a full repaint capped at one per `kShotRemapMinMs`
  (50 ms), measured by a `pump:` telemetry line every 60 s.
- Libraries pinned exactly (LVGL 9.5.0, GFX 1.6.6).
- The 4.3C's residual stutter is accepted (2026-11-04).

## Evidence
| Date | Board | What | Before | After |
|---|---|---|---|---|
| 2026-06-30 | S3 RGB (the commit says only "the RGB panel"; the 4.3B landed two hours earlier) | graph CPU, `lv_chart` to canvas | ~90 % | ~70 % |
| 2026-07-17 | P4-4.3 | direct esp_lcd, two framebuffers, rolling graph | flush 4 \| 18 ms | 26-30 fps, flush 4 \| 10 ms |
| 2026-07-23 | P4-5 | scroll graph, DSI flush overhaul | 15 fps, 80 % CPU, ~15 \| 45 ms render \| flush | ~21.5 fps at the graph's ~24 fps demand cap |
| 2026-07-23 | P4-5 | frame budget after overhaul | | ~22 ms render, ~13 ms rotate+wait, ~5 ms memmove |
| 2026-07-23 | 4.3C | worst refresh pass (500 ms setter churn) | 138-151 ms | 60-78 ms |
| 2026-07-23 | P4-5 | settings scroll, chunked DMA sync | 7 fps, 110 ms (60 \| 50) | 9-10 fps |
| 2026-07-27 | P4-5 | dirty sync at 32 KB chunks unconditionally | 21.5 fps | 17-19 fps, 85 % CPU (so 128 KB first) |
| 2026-10-17 | 4.3C | LVGL 9.5.0 vs 9.6.0, graph on Home, 4 min each | 656 frames/min; 28 / 48 / 24 % in 50-67 / 67-100 / 100-200 ms | 658; 26 / 50 / 23 % |
| 2026-10-18 to 10-21 | P4-5 rev v3 | graph frames/min by L2 cache geometry | ~875 (128 KB / 64 B) | ~1,000 (256 KB / 64 B), ~1,200 (256 KB / 128 B) |

The A/B shows the 4.3C's choppiness (about a quarter of frames at
100-200 ms) is our render cost, not the library; the commit (`9840933`)
records only "identical distributions", the figures are per the working
notes. The L2 figures are cited from the sibling record on the P4 L2 cache
and bootloader compatibility, which also has the rev v1.x column.

Histogram recipe (reusable): count intervals between completed frames
(`lv_display_flush_is_last` in the flush callback) into buckets
<20 / 33 / 50 / 67 / 100 / 200 / 500 ms, print with the 60 s `lvgl:` line,
36 B of .bss, remove afterwards.

## Alternatives and dead ends
- PPA hardware rotate (2026-07-23): no faster than the tiled software
  rotate; a 90-degree rotate is bandwidth-bound whoever executes it.
- Pairing stores into 32-bit writes: identical timing (PSRAM-bound).
- 45 MHz DPI clock: zero fps gain, sparser refresh boundaries gate the flush.
- Internal-RAM draw buffer on DSI: removed 2026-07-25 after a 62 KB buffer
  starved the hosted radio's SDIO buffers on the 4.3 (boot loop).
- `LV_USE_OS` + two software draw units: no gain on a PSRAM draw buffer,
  CPU% overlay reads 100 %, 2 x 8 KB internal stacks (note in lv_conf.h).
- A 250 ms legend repaint throttle (2026-11-04): bought nothing on the 4.3C
  and read as a low frame rate; reverted the same day.
- Snapshot-scroll during the gesture (blit a pre-rendered page image):
  named as the remaining settings-scroll lever on 2026-07-23 and left
  parked; never built.

## Consequences
- Never take a large buffer from `lv_malloc` on a device: the pool is 1 MB
  (`LV_MEM_SIZE`, lv_conf.h) and the TLSF allocator spins in `lv_realloc`
  instead of returning NULL when it is exhausted, so an under-sized pool is
  a hang.
- The 4.3C stutter is an accepted cost; any further gain comes from our
  graph code, measured with the histogram recipe.
- Bumping LVGL or GFX is a deliberate, bench-checked, one-at-a-time change.

## History
- 2026-06-30: canvas strip chart replaces `lv_chart` (`17b113a`).
- 2026-07-17: DSI direct esp_lcd with two framebuffers (`75ed63a`).
- 2026-07-23: P4-5 flush overhaul, setter guards, pace quantization,
  chunked DMA sync (`9198f72`, `66803c6`, `895735c`); PPA path landed and
  was measured out the same day (`c470f55`).
- 2026-07-25: DSI draw buffer PSRAM-only (`c4f839e`).
- 2026-07-27: DMA-pool aware chunk fallback, 128 KB then 32 KB held 2 s
  (`a4af25b`).
- 2026-09-18: Window growth modes, `kShotRemapMinMs`, `pump:` line
  (`855d97e`, v0.14.2-beta.1).
- 2026-10-17: LVGL 9.6 A/B, exact library pins (`9840933`).
- 2026-11-01: canvas from PSRAM after the X-8's empty graph (`36f87be`).
- 2026-11-04: legend throttle tried and dropped (`a82f7c7`, `c6fca82`);
  4.3C performance accepted.

## Sources
`src/ui/home_tab.cpp` (`flow_buf_alloc`, `kShotRemapMinMs`,
`shot_live_mapping`), `src/platform_esp32/display.cpp` (`dsi_flush_cb`),
`include/lv_conf.h`, commits `17b113a`, `75ed63a`, `c470f55`, `9198f72`,
`66803c6`, `895735c`, `c4f839e`, `a4af25b`, `855d97e`, `9840933`,
`36f87be`, `a82f7c7`, `c6fca82`; tags v0.14.2-beta.1, v1.1.0,
v1.3.0-beta.3.
