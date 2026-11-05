# The host simulator is the UI verification path

- **Status:** Accepted
- **Area:** display
- **First decided:** 2026-06-28 · **Last changed:** 2026-11-01
- **Boards:** all (the sim renders every released form factor)

## Context
UI work on a device means a flash per look, a shared `.pio` build directory
that a concurrent flash can corrupt, and a bench unit someone may be
using. The UI depends only on `core::` interfaces, so nothing in it needs a
board to run.

## Decision
- `make sim` builds the `sim` PlatformIO env (native host, `src/sim/` +
  `src/ui/` + `src/platform_host/`), runs it, and writes `renders/*.png`.
  The same `src/ui/` compiles unchanged on-device and on the host; the host
  side is `PngDisplay` (LVGL into an RGB565 buffer, written out as PNG) plus
  fake ports for the machine, scales, store and provisioners.
- The sim is the first and usual check for any UI change; a device env is
  built only for the board actually being tested. The manual's screenshots
  are curated copies of sim renders (`make docs-img`).
- Form factors are rendered explicitly: 800x480, 320x240, 1024x600,
  1280x720 at scale 1.5 and 1280x800 at scale 1.6, the last two through the
  same `ui::dp()` / `ui::font_dp()` scaling the P4-5 and X-8 use, plus a
  deliberate between-tiers 1280x960 that exercises the saver's stretch
  fallback.
- The sim also carries UI invariants a PNG cannot show: it fails the run if
  the saver artwork's box, drawn size and ext-draw padding disagree
  (`check_saver_dirty_area`), and with Detailed log on it prints the Home
  card geometry lines (`home: scale card ...`, `home: bench card ...
  clipped=N`) the row-alignment rule is checked against.

## Evidence
| Date | Check | Result |
|---|---|---|
| 2026-07-22 | `BOARD_UI_SCALE` 1.5 introduced for the P4-5; scale-1.0 boards compared against baseline renders | bit-identical |
| 2026-07-23 | scroll-step coalescing on the graph canvas (`9198f72`); change-detecting setters and pace quantization (`66803c6`) | sim render bit-identical; identical except the intended caption rows |
| 2026-09-10 | saver invalidation area measured with an `INVALIDATE_AREA` hook in the sim | 251K px/frame at 1280x720, 388K at 1280x800, 19K at 800x480; named the P4 regression before a board was touched |
| 2026-09-11 | "Restart display" row removed | render diff vs a stashed baseline showed exactly the expected files |

## Alternatives and dead ends
- Verifying on-device only: every rule above exists because a bench board
  was busy or a flash mid-build corrupted object files.
- Continuous reflow instead of discrete layouts: not taken at the start
  (2026-06-28, compact + wide only); the tiers (compact, wide, xl) plus a
  continuous scale on top is what shipped (`include/ui/screen.h`).

## Consequences
- The sim's `render()` parameters are mirrored by a forwarding lambda `r`
  in `src/sim/main.cpp`; change both together, or a widened parameter
  silently narrows through the lambda (the midshot pose went from a `bool`
  to a shot length in ms on 2026-09-18, `855d97e`; the hazard is from the
  working notes, no bad render is recorded).
- `make sim` needs no node and no board; the web app header is only needed
  by device builds.
- What the sim cannot show: frame timing, PSRAM bandwidth, DMA pools. Those
  are bench measurements and are recorded in the display and platform logs.

## History
- 2026-06-28: layered core / ui / platform with a PNG-rendering host sim
  from the first checkpoint (`a960211`).
- 2026-07-22: proportional UI scale, verified bit-identical at 1.0 (`2d4617a`).
- 2026-08-29: 1.6 scale and the 1280x800 renders for the X-8 (`1b1dac4`;
  the X-7 is pixel-identical to the 1280x720 at 1.5 profile).
- 2026-09-10: the between-tiers 1280x960 saver render added (`4363da6`).
- 2026-09-10: saver geometry asserts in the sim (`4363da6`).
- 2026-11-01: the Home card geometry log lines, printed under Detailed log
  (`f621a59`, `d155c7d`).

## Sources
`Makefile` (`sim`, `docs-img`), `src/sim/main.cpp`,
`src/platform_host/png_display.cpp`, `include/ui/screen.h`, README
"The simulator".
