# Four boards are released; every other env is internal-only, de-listed not deleted

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-09-10 · **Last changed:** 2026-09-22
- **Boards:** all

## Context

By v0.12 the firmware built for nine Waveshare boards and advertised most of
them. Few had been exercised past bring-up, each was another image to
publish and another flasher card, and a buyer reading the README could not
tell which one to get. We wanted a short list with a reason for each
entry.

## Decision

Four products are built by CI, offered by the web flasher and described in
the user docs:

| env | product | role | slug |
|---|---|---|---|
| `esp32-p4-micra-5` (+ `-rev3`) | P4-WIFI6-Touch-LCD-5 | mount on the Micra, the default pick | `p4-wifi6-touch-lcd-5` (`-rev3`) |
| `esp32-p4-micra-43` (+ `-rev3`) | P4-WIFI6-Touch-LCD-4.3 | the same board with smaller glass | `p4-wifi6-touch-lcd-4.3` (`-rev3`) |
| `esp32-s3-micra-4-3c` | S3-Touch-LCD-4.3C BOX | finished box with opto-isolators on board; the slower part | `s3-touch-lcd-4.3c` |
| `esp32-p4-micra-x-8` | P4-WIFI6-Touch-LCD-X 8" | counter-top companion | `p4-wifi6-touch-lcd-x-8` |

Every other env (`esp32-s3-micra` 2", `-7b`, `-4-3b`, `-x-7`, `-x-10-1`) is
INTERNAL-ONLY: it builds, is maintained, and is absent from the CI matrix,
the flasher and every user-facing doc. Nothing is deleted to de-advertise a
board; the envs, board jsons, `board_config.h` blocks, panel tables, make
targets and printable shells stay as the record of how those boards differ
and as dev hardware (the X 7" is the bench desk unit).

The P4 silicon split (issue #2) doubles the IMAGES, not the boards.
Waveshare ships rev v1.x ("es") and rev v3 silicon under one SKU and one
image cannot boot both, so each P4 product has a `-rev3` sibling env and slug
(six images for four products). The PLAIN slug keeps meaning rev v1.x
forever; fielded units self-update from it. `kName` is identical for both;
the identify banner's `REV=` names the silicon. The flasher keeps one card
per product with a "Chip revision" radio that Detect sets itself.

OTA on de-listed boards is knowingly broken: `releases.json` is global, so an
internal-only build sees "update available", its install 404s and rolls back
harmlessly. No `kOtaPublished` flag, no UI gating; revisit only if one of
those boards sees real use.

## Evidence

- 2026-09-11: diffing the P4-5 and P4-4.3 `board_config` blocks, everything
  below the touch section is byte-identical (battery, audio, paddle, SD,
  I2C, GT911); the 5 env literally `extends` the 4.3 env. Hence "choose on
  screen size and price, nothing else".
- 2026-09-10: the CI `kUpdateSlug` guard had been a no-op (it grepped
  `board_config.h`, where every slug is present), found while narrowing the
  matrix (`0ac64c8`); since 2026-10-23 it holds at compile time (`605e553`).
- 2026-09-22: a rev v3.2 P4-4.3 and a rev v3.2 P4-5 booted, drove panel and
  touch and completed the welcome flow on the `-rev3` images, flashed by the
  make target's chip-revision auto-pick (per the working notes; the
  commit `770f51f` itself predates the first boot).

## Alternatives and dead ends

- Deleting the de-listed boards' code: rejected, it is the only record of
  their wiring and panel tables, and the 7" is in daily dev use.
- A product entry per silicon revision: rejected, a buyer cannot tell which
  silicon they got, and picking wrong is recoverable by reflashing, so the
  guard-rails stay light.
- Gating "Install now" on de-listed boards: not worth a flag for boards
  nobody is expected to own.

## Consequences

Lists that change together when the matrix changes: the workflow matrix,
`site/index.html` (cards and `BOARD_PATTERNS`), README.md,
`docs/HARDWARE.md`, and the `[RELEASED]` / `[INTERNAL-ONLY]` markers in
`platformio.ini` and `board_config.h`. A change to an internal-only board
updates CLAUDE.md and the code only.

## History

- 2026-09-10: matrix narrowed to the P4-5, S3 4.3C and X 8" (`0ac64c8`, v0.12.2).
- 2026-09-11: P4-4.3 returned, a user has one (`240ff9b`, v0.13.0).
- 2026-09-22: `-rev3` images and slugs for both P4 products (`770f51f`,
  v1.0.0-beta.2); hardware-confirmed the same day.

## Sources

Issue #2 (closed 2026-10-10); `0ac64c8`, `240ff9b`, `770f51f`; `platformio.ini`,
`include/platform_esp32/board_config.h`,
`.github/workflows/firmware-release.yml`, `site/index.html`, `README.md`,
`docs/HARDWARE.md`.
