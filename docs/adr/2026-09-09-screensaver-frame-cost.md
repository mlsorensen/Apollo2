# Screensaver frame cost: the saver runs on its own screen

- **Status:** Accepted
- **Area:** display
- **First decided:** 2026-09-09 · **Last changed:** 2026-09-17
- **Boards:** all

## Context
The bouncing-artwork idle screen (v0.10.0, 2026-09-04) cost more than a
full graph redraw: 128 ms per frame at 5-6 fps and 83 % CPU on the 4.3C for
one small moving image. Three separate causes were found on the bench over
eight days, each one measured before the next was looked for.

## Decision
- The artwork styles load a dedicated black screen (`saver_screen_`) and
  load the UI screen back on wake; LVGL drops invalidations on an inactive
  screen, so nothing underneath is composited. Dim and Off keep a top-layer
  cover (Dim is fully transparent, the live UI shows through at the dim
  floor).
- The visible graph tick is skipped while the saver is up; the capture ring
  is still fed, so a shot detected right after wake has its lead-in.
- The artwork is pre-rendered per screen tier at 6/10 of the screen height
  (144 / 288 / 360 / 432 / 480 px tall for the 240 / 480 / 600 / 720 / 800
  px screens; `SCREEN_HEIGHTS` and `HEIGHT_TENTHS` in gen_logo.py), so no
  board transforms the image; an unlisted height falls back to the nearest
  variant stretched, with LVGL's ext-draw padding clamped to zero.
- Motion is 1 px per 33 ms tick on every board (~30 px/s).
- The dim floor is per board: `kSaverDimPercent` = 10 on the S3 boards,
  5 on the P4 boards.

## Evidence
Perf overlay, fps / frame / render | flush.

| Date | Board | Build | Result |
|---|---|---|---|
| 2026-09-09 | 4.3C | lion, graph still ticking under the cover | 5-6 fps, 128 ms (104 \| 24), 83 % CPU |
| 2026-09-09 | 4.3C | box sized to the drawn image (no scale) | 7 fps, 101 ms (81 \| 20) |
| 2026-09-09 | 4.3C | graph tick skipped under the saver | ~25 fps, 32 ms (26 \| 4) |
| 2026-09-10 | P4-5 / X-7 | v0.12.0 single asset, stretched up | 12 fps, 60 ms (37 \| 13), 93 % CPU (was 30 fps, 20 ms) |
| 2026-09-10 | X-7 | per-tier prescaled lion | 29 fps, 8 ms (6 \| 2), 50 % CPU |
| 2026-09-17 | 4.3C | Apollo at 6/10 height, top layer, A8 blend | 13 fps, 60 ms (50 \| 10) |
| 2026-09-17 | 4.3C | top layer, pre-baked RGB565 | 13 fps, 62 ms (52 \| 10) |
| 2026-09-17 | 4.3C | own screen, pre-baked | 26 fps, 25 ms (16 \| 9) |
| 2026-09-17 | 4.3C | own screen, A8 blend (shipped) | 25-27 fps, 28 ms (18 \| 10) |
| 2026-09-17 | P4-4.3 | own screen, same 174x288 artwork | 30 fps, 7 ms (5 \| 2) |
| 2026-09-17 | P4-5 | v0.14.1, lion 227x432 / Apollo 262x432 | 12 ms / 16 ms per frame (per the working notes; not in a commit) |

Every other row is from the commit messages (`b828884`, `c3feeef`,
`4363da6`, `3f83449`). The sim measured the stretched-asset dirty area with an invalidation hook:
251K px per frame at 1280x720 and 388K at 1280x800 against 19K at 800x480,
which is why v0.12.0 sped the 4.3C up and slowed every P4 down.

## Alternatives and dead ends
- Hiding the UI screen with `LV_OBJ_FLAG_HIDDEN` under a top-layer cover
  (2026-09-09): faster (render 26 to 11 ms) but the erase path breaks, old
  positions are never repainted and the panel fills with trails; wake took
  seconds. Both seen on hardware.
- Pre-blending the A8 map into an opaque RGB565 PSRAM buffer per bounce
  (100 KB for Apollo on the 4.3C, 279 KB on the X-8): 2 ms of a ~28 ms
  frame (18 | 10 to 16 | 9) and no fps. Removed (`App::set_saver_art`
  keeps the note); a buffer that size must never come from the LVGL pool
  anyway.
- A hard-coded 5 % dim on every board rendered black on the 4.3C's
  inverted-duty expander PWM; hence the per-board floor.

## Consequences
- LVGL composites the active screen inside every dirty rectangle before the
  top layer; anything drawn over a live screen pays for what is under it.
  New overlays that must be cheap go on their own screen.
- `kSaverArtHeightTenths` and the artwork generator's height table must
  agree; the sim fails the run if box, drawn size and ext-draw padding
  disagree.
- The P4 dim floor's low end is untested (noted 2026-09-09 in `c3feeef`
  and again 2026-09-12 in `ec537ac`); 5 % is the value that has shipped
  since.

## History
- 2026-09-04: bouncing-logo saver with Logo / Blank styles (`3a328a6`).
- 2026-09-09: graph tick skipped, box sized to the image, per-board dim
  floor, `lvgl:` telltale (`b828884`, `c3feeef`, v0.12.0).
- 2026-09-10: per-tier prescaled artwork fixes the P4 regression; the
  tiers were 2/5 of the screen then (96 / 192 / 240 / 288 / 320 px)
  (`4363da6`, v0.12.3).
- 2026-09-12: Dim shows the live UI again (`ec537ac`).
- 2026-09-17: saver on its own screen, Apollo artwork, both drawings
  raised to 6/10 of the screen (the tiers above), 1 px motion (`3f83449`,
  v0.14.1).

## Sources
`src/ui/app.cpp` (`start_screensaver`, `stop_screensaver`),
`include/ui/saver_art.h`, `tools/logo/gen_logo.py`,
`include/platform_esp32/board_config.h` (`kSaverDimPercent`),
CHANGELOG v0.12.0, v0.12.3, v0.14.1.
