# Four boards are released; every other env is internal-only, de-listed not deleted

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-09-10 · **Last changed:** 2026-11-07
- **Boards:** all

## Context

The firmware builds for nine Waveshare boards. Few have been exercised past
bring-up, each is another image to publish and another flasher card, and a
buyer reading the README needs to be told which one to get. Waveshare also
ships two binary-incompatible P4 silicon generations under one SKU, so a
buyer cannot tell which chip they got ([the split](#ev-rev3)).

## Decision

Four products are built by CI, offered by the web flasher and described in
the user docs:

| env | product | role | slug |
|---|---|---|---|
| `esp32-p4-micra-5` (+ `-rev3`) | P4-WIFI6-Touch-LCD-5 | mount on the Micra, the default pick | `p4-wifi6-touch-lcd-5` (`-rev3`) |
| `esp32-p4-micra-43` (+ `-rev3`) | P4-WIFI6-Touch-LCD-4.3 | the same board with smaller glass ([shown](#ev-same-board)) | `p4-wifi6-touch-lcd-4.3` (`-rev3`) |
| `esp32-s3-micra-4-3c` | S3-Touch-LCD-4.3C BOX | finished box with opto-isolators on board; the slower part | `s3-touch-lcd-4.3c` |
| `esp32-p4-micra-x-8` | P4-WIFI6-Touch-LCD-X 8" | counter-top companion | `p4-wifi6-touch-lcd-x-8` |

Every other env (`esp32-s3-micra` 2", `-7b`, `-4-3b`, `-x-7`, `-x-10-1`) is
INTERNAL-ONLY: it builds, is maintained, and is absent from the CI matrix,
the flasher and every user-facing doc. Nothing is deleted to de-advertise a
board ([why](#alternatives-and-dead-ends)); its env, board json,
`board_config.h` block, panel table, make targets and printable shell stay as
the record of how it differs and as dev hardware (the X 7" is the bench desk
unit).

The P4 silicon split doubles the IMAGES, not the boards: each P4 product has
a `-rev3` sibling env and slug (six images for four products). The PLAIN slug
means rev v1.x ("es") forever; fielded units self-update from it. `kName` is
identical for both; the identify banner's `REV=` names the silicon. The
flasher keeps one card per product with a "Chip revision" radio that Detect
sets itself ([why not a product per revision](#alternatives-and-dead-ends)).

OTA on de-listed boards is knowingly broken: `releases.json` is global, so an
internal-only build sees "update available", its install 404s and rolls back
harmlessly. No `kOtaPublished` flag, no UI gating; revisit only if one of
those boards sees real use.

## Consequences

Lists that change together when the matrix changes: the workflow matrix,
`site/index.html` (cards and `BOARD_PATTERNS`), README.md,
`docs/HARDWARE.md`, and the `[RELEASED]` / `[INTERNAL-ONLY]` markers in
`platformio.ini` and `board_config.h`. A change to an internal-only board
updates the code and its decision record only.

---

## Details

### Design detail

- Positioning the docs keep: the P4-5 is the recommendation for mounting on
  the machine (it needs a printed shell and, for Auto shot, a DIY opto
  cable); the P4-4.3 is the same board with smaller glass, the only
  `board_config` deltas being panel controller, DSI timings, reset and
  backlight polarity, touch rst/int and UI scale, so the choice is screen
  size and a small price difference; the 4.3C removes both DIY steps (the
  only finished box with on-board opto-isolators, three wires into screw
  terminals) at the cost of being the slower part; the X 8" is the
  counter-top box. Re-adding an internal-only board to any user-facing list
  is asked first.
- The rev3 siblings: `boards/esp32-p4-wifi6-{5,43}-rev3.json` (`chip_variant
  "esp32p4"`), `-D BOARD_P4_SILICON_REV3` from build_flags, which selects
  the DSI PHY's XTAL PLL reference and the `<plain-slug>-rev3` slug in
  `board_config.h`. The X 8" is rev3-only and has no sibling.
- Picking the image: the identify reply's `REV=` is efuse
  major*100+minor, >= 300 = rev3. The flasher's radio defaults to v1.x and
  Detect sets it from a running board; a blank board cannot be probed over
  Web Serial, so the radio is the explicit choice there. `make flash-p4-5` /
  `flash-p4-4-3` (`tools/flash.sh`) read the revision off the ROM loader
  with esptool, which works on a blank board, and switch to the sibling
  env, announcing it; bare `make flash` asks a running board `id?`. The
  explicit `-rev3` targets exist but are rarely needed. P4 only: no scripted
  resets on an S3's USB-CDC port.

### Evidence

- By v0.12 the firmware built for nine boards and advertised most of them;
  the matrix was narrowed on 2026-09-10 to three and widened back to four on
  2026-09-11 ([History](#history)).
- <a id="ev-same-board"></a>2026-09-11: diffing the P4-5 and P4-4.3
  `board_config` blocks, everything below the touch section is
  byte-identical (battery, audio, paddle, SD, I2C, GT911); the 5 env
  literally `extends` the 4.3 env. Hence "choose on screen size and price,
  nothing else".
- 2026-09-10: the CI `kUpdateSlug` guard had been a no-op (it grepped
  `board_config.h`, where every slug is present), found while narrowing the
  matrix (`0ac64c8`); since 2026-10-23 it holds at compile time (`605e553`).
- <a id="ev-rev3"></a>Rev v1.x ("es") and rev v3 P4 silicon ship under one
  SKU and one image cannot boot both (issue #2). 2026-09-22: a rev v3.2
  P4-4.3 and a rev v3.2 P4-5 booted, drove panel and touch and completed the
  welcome flow on the `-rev3` images, flashed by the make target's
  chip-revision auto-pick (per the working notes; the commit `770f51f`
  itself predates the first boot).

### Alternatives and dead ends

- Deleting the de-listed boards' code: rejected, it is the only record of
  their wiring and panel tables, and the 7" is in daily dev use.
- A product entry per silicon revision: rejected, a buyer cannot tell which
  silicon they got, and picking wrong is recoverable by reflashing, so the
  guard-rails stay light.
- Gating "Install now" on de-listed boards: not worth a flag for boards
  nobody is expected to own.

### History

- 2026-09-10: matrix narrowed to the P4-5, S3 4.3C and X 8" (`0ac64c8`, v0.12.2).
- 2026-09-11: P4-4.3 returned, a user has one (`240ff9b`, v0.13.0).
- 2026-09-22: `-rev3` images and slugs for both P4 products (`770f51f`,
  v1.0.0-beta.2); hardware-confirmed the same day.
- 2026-11-07: folded in from the project's working notes (CLAUDE.md).

### Sources

Issue #2 (closed 2026-10-10); `0ac64c8`, `240ff9b`, `770f51f`; `platformio.ini`,
`include/platform_esp32/board_config.h`,
`.github/workflows/firmware-release.yml`, `site/index.html`, `README.md`,
`docs/HARDWARE.md`.
