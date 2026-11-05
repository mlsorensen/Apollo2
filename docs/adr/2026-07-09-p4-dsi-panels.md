# DSI panels on the P4 family: one pipeline, a table and a few polarities per glass

- **Status:** Accepted
- **Area:** display
- **First decided:** 2026-07-09 · **Last changed:** 2026-09-22
- **Boards:** P4-4.3, P4-5 (both silicon revisions), X 8"; X 7" and X 10.1" internal-only

## Context
Every P4 board is the same electronics behind a different MIPI-DSI glass:
a native-portrait panel in video mode, rotated to a landscape UI. Each glass
needs its own DCS init table, the boxes mount their panels different ways
up, not every controller honours MADCTL in video mode, and production
(rev v3) silicon needs a different PHY clock source from the engineering
samples.

## Decision
- One DSI pipeline (display.cpp, direct esp_lcd, not Arduino_GFX): two
  PSRAM framebuffers owned by the DPI panel, LVGL in partial mode, the flush
  rotates strips into the back buffer and presents by a zero-copy flip at
  the frame boundary ([measured on the P4-4.3](#ev-pipeline)). Install mode
  reuses the per-board table with `num_fbs=2`, no DMA2D, no async sync (the
  OTA install-mode record).
- `BOARD_DSI_PANEL_*` selects the init table; every other difference is a
  constant in the board's block ([the per-glass table](#ev-glass)).
- Rotation is done in the flush: 90 degrees by default, 270 on the X-8
  (`kDsiRotate270`) because the JD9365 ignores MADCTL in video mode
  ([proven inert on the X-8](#ev-x8); [the MADCTL
  attempt](#alternatives-and-dead-ends)). The X-7 alone flips 180 at the
  panel (MADCTL 0x03 in its ILI9881C table).
- Rev v3 silicon selects the XTAL PLL reference for the DSI PHY
  (`MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT`) under `BOARD_P4_SILICON_REV3`,
  keyed on silicon, not on panel ([why not a runtime
  check](#alternatives-and-dead-ends)).
- UI scale per glass: 1.0 on the 4.3, 1.5 on the 5 and X-7, 1.6 on the X-8
  and X-10.1.

## Consequences
- Editing an init table: 0x36 also appears as a page-1 GIP register
  mid-table on the ILI9881C; only the page-0 writes are MADCTL (the X-7
  sets both to 0x03: GS|SS, both bits, one alone mirrors).
- A new DSI glass is a new table plus block constants; driver code never
  hardcodes pins or polarities.
- Every box bring-up asks the orientation question: camera position, then
  panel flip, then flush rotation.
- The X-7 / X-10.1 blocks still drive GPIO23 as "BL_EN"; it works only
  because high is the GT911 out of reset ([the misread
  pin](#alternatives-and-dead-ends)). Fix when one is on the bench.
- The X-10.1 env builds and has never been powered.

---

## Details

### Evidence

<a id="ev-glass"></a>
Per-glass constants and verification dates:

| Board | Native | Controller | DPI / lanes | Reset asserts | Backlight PWM | Boost enable | Rotation | Touch rst / int | Verified |
|---|---|---|---|---|---|---|---|---|---|
| P4-4.3 | 480x800 | ST7701 | 33.4 MHz / 500 Mbps | low | inverted | GPIO33 | 90 in flush | 23 / 2 | 2026-07-17, rev3 2026-09-22 |
| P4-5 | 720x1280 | HX8394 | 58 MHz / 700 Mbps | high | normal | none | 90 in flush | probe only | 2026-07-23, rev3 2026-09-22 |
| X-7 | 720x1280 | ILI9881C | 80 MHz / 1000 Mbps | low | normal | GPIO23 (see below) | 180 at panel | none | 2026-08-29 |
| X-8 | 800x1280 | JD9365 | 80 MHz / 1500 Mbps | low | normal | none | 270 in flush | 23 / 33 | 2026-09-22 |
| X-10.1 | 800x1280 | JD9365 (own table) | 80 MHz / 1500 Mbps | low | normal | GPIO23 | 90 in flush | none | never |

The P4-4.3's 33.4 MHz pixel clock is above the BSP's 30 MHz on purpose
(~66 Hz instead of ~60 Hz, to move a faint inversion shimmer off 60 Hz;
verdict on hardware "can't tell", kept as harmless).

Dating notes: the P4-5's first board landed 2026-07-22 untested; its
hardware date is the first on-device profiling (`c470f55`, `9198f72`). The
rev3 dates for the 4.3 and the 5 are per the working notes: `770f51f`
itself says "not yet booted on rev3 hardware", and no later commit records
the boot.

<a id="ev-pipeline"></a>
Pipeline (P4-4.3, 2026-07-17): the rolling graph at 26-30 fps, flush
4 | 10 ms against the blocking single-buffer path's 4 | 18 ms.

<a id="ev-x8"></a>
X-8 bring-up (2026-09-22): the 8" panel came up in ~340 ms with the vendor
table and 80 MHz / 1500 Mbps timings in a sibling project (per the working
notes). The first Apollo image on an 8" came up dark; the second, carrying
the two facts from that project (the 270 flush and GPIO23/33 as the touch
pins with no boost enable), booted the right way up with touch tracking.
The commit does not say which of the two cured the dark panel. MADCTL had
already been proven inert there: neither GS|SS nor MY|MX moved the
picture, so the 180-degree mount is the flush rotating 270 instead of 90,
the dirty box and the install-mode rotate following.

### Alternatives and dead ends
- MADCTL for the X-8 flip (2026-09-04): shipped in `94a9be5`, proven inert
  on hardware 2026-09-22; the table head is back to the vendor 0x00.
- GPIO23 as a backlight boost enable on the X family: the X schematic has
  GPIO23 as TP_RST and GPIO33 as TP_INT, and no boost-enable pin (PWM on
  GPIO26 only); driving 23 low held the GT911 in reset. The X-8 block uses
  them as touch pins with `kLcdBacklightEn = -1`; the X-7 / X-10.1 branch
  still drives 23 high as "BL_EN", which works only because high is the
  GT911 out of reset. Left as verified; fix when one is on the bench.
- A runtime chip-revision check for the PHY clock: buys nothing, the rev1
  and rev3 images are separate binaries anyway. The legacy default
  (PLL_F20M) aborts inside the HAL on rev3, so `BOARD_P4_SILICON_REV3` is
  defined by the rev3 envs and the X-8 / X-10.1 blocks and display.cpp
  keys the PHY clock on it.

### History
- 2026-07-09: P4-4.3 added, ST7701 over DSI (`400d95f`); first boot
  2026-07-17 (`a84f57b`), direct esp_lcd double buffer (`75ed63a`).
- 2026-07-22: P4-5: HX8394 table, reset active-high, normal backlight,
  no boost pin, 1.5x UI scale (`2d4617a`); on hardware the next day
  (`c470f55`, `9198f72`).
- 2026-08-29: X 7" / 8" / 10.1" boxes; 7" flipped 180 at the panel (`1b1dac4`).
- 2026-09-04: X-8 / X-10.1 target rev3; XTAL PHY clock (`94a9be5`).
- 2026-09-22: rev3 envs for the 5 and 4.3 (`770f51f`, issue #2); X-8
  verified: 270 flush, real touch pins, no BL_EN (`7bb4565`).

### Sources
`src/platform_esp32/display.cpp` (init tables, `dsi_flush_cb`,
`install_panel_begin`), `include/platform_esp32/board_config.h` P4 blocks,
commits `400d95f`, `a84f57b`, `75ed63a`, `2d4617a`, `1b1dac4`, `94a9be5`,
`770f51f`, `7bb4565`, issue #2 (opened 2026-09-20, closed 2026-10-10),
CHANGELOG v1.0.0.
