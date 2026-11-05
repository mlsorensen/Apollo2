# The host simulator is the UI verification path

- **Status:** Accepted
- **Area:** display
- **First decided:** 2026-06-28 · **Last changed:** 2026-11-01
- **Boards:** all (the sim renders every released form factor)

## Context
UI work on a device means a flash per look, a shared `.pio` build directory
that a concurrent flash can corrupt, and a bench unit someone may be
using. The UI depends only on `core::` interfaces, so nothing in it needs a
board, and the same `src/ui/` compiles unchanged on-device and on the host.

## Decision
- `make sim` builds the `sim` PlatformIO env (native host: `src/sim/` +
  `src/ui/` + `src/platform_host/`), runs it, and writes `renders/*.png`
  ([what it is made of](#sources)).
- The sim is the first and usual check for any UI change; a device env is
  built only for the board actually being tested ([why not on-device
  only](#alternatives-and-dead-ends)). The manual's screenshots are
  curated sim renders (`make docs-img`).
- Form factors are rendered explicitly: 800x480, 320x240, 1024x600,
  1280x720 at scale 1.5 and 1280x800 at scale 1.6 ([scale-1.0 boards
  verified bit-identical](#ev-checks)), plus a deliberate between-tiers
  1280x960 that exercises the saver's stretch fallback.
- The sim also checks invariants a PNG cannot show: it fails the run if the
  saver artwork's box, drawn size and ext-draw padding disagree
  (`check_saver_dirty_area`), and under Detailed log it prints the Home
  card geometry lines the row-alignment rule is checked against ([what it
  has caught](#ev-checks)).

## Consequences
- The sim's `render()` parameters are mirrored by a forwarding lambda `r`
  in `src/sim/main.cpp`; change both together, or a widened parameter
  silently narrows through the lambda ([the midshot-pose
  widening](#history)).
- `make sim` needs no node and no board.
- What the sim cannot show: frame timing, PSRAM bandwidth, DMA pools. Those
  are bench measurements, recorded in the display and platform logs.

---

## Details

### Evidence
<a id="ev-checks"></a>
| Date | Check | Result |
|---|---|---|
| 2026-07-22 | `BOARD_UI_SCALE` 1.5 introduced for the P4-5; scale-1.0 boards compared against baseline renders | bit-identical |
| 2026-07-23 | scroll-step coalescing on the graph canvas (`9198f72`); change-detecting setters and pace quantization (`66803c6`) | sim render bit-identical; identical except the intended caption rows |
| 2026-09-10 | saver invalidation area measured with an `INVALIDATE_AREA` hook in the sim | 251K px/frame at 1280x720, 388K at 1280x800, 19K at 800x480; named the P4 regression before a board was touched |
| 2026-09-11 | "Restart display" row removed | render diff vs a stashed baseline showed exactly the expected files |

### Alternatives and dead ends
- Verifying on-device only: every rule above exists because a bench board
  was busy or a flash mid-build corrupted object files.
- Continuous reflow instead of discrete layouts: not taken at the start
  (2026-06-28, compact + wide only); the tiers (compact, wide, xl) plus a
  continuous scale on top is what shipped (`include/ui/screen.h`). The
  1280x720 at 1.5 and 1280x800 at 1.6 renders go through the same
  `ui::dp()` / `ui::font_dp()` scaling the P4-5 and X-8 use.

### History
- 2026-06-28: layered core / ui / platform with a PNG-rendering host sim
  from the first checkpoint (`a960211`).
- 2026-07-22: proportional UI scale, verified bit-identical at 1.0 (`2d4617a`).
- 2026-08-29: 1.6 scale and the 1280x800 renders for the X-8 (`1b1dac4`;
  the X-7 is pixel-identical to the 1280x720 at 1.5 profile).
- 2026-09-10: the between-tiers 1280x960 saver render added (`4363da6`).
- 2026-09-10: saver geometry asserts in the sim (`4363da6`).
- 2026-09-18: the midshot pose in `render()` went from a `bool` to a shot
  length in ms (`855d97e`), the case the forwarding-lambda rule guards
  against; the hazard is from the working notes, no bad render is
  recorded.
- 2026-11-01: the Home card geometry log lines (`home: scale card ...`,
  `home: bench card ... clipped=N`), printed under Detailed log
  (`f621a59`, `d155c7d`).

### Sources
`Makefile` (`sim`, `docs-img`; the sim needs neither node nor the web app
header, which only device builds embed), `src/sim/main.cpp` (the
`render()` poses and the fake ports for the machine, scales, store and
provisioners), `src/platform_host/png_display.cpp` (`PngDisplay`: LVGL
into an RGB565 buffer, written out as PNG), `include/ui/screen.h`, README
"The simulator".
