# P4 silicon revisions: two binary-incompatible generations, one image each, picked per unit

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-07-17 · **Last changed:** 2026-11-07
- **Boards:** every P4 board (P4-5, P4-4.3, X 7" / 8" / 10.1")

## Context

ESP32-P4 chips come in two generations that cannot run each other's
images: pre-production rev v1.x (sold in volume through 2025) and
mass-production rev v3.0+ (there is no v2). Waveshare ships both under the
same SKU, so a buyer cannot tell which silicon they have, and the split is
per unit, not per board size. An image built for the wrong generation never
reaches app code, and [the failure looks like a dead
board](#ev-symptoms).

## Decision

- The generation is picked at link time by the board json's
  `chip_variant`: `"esp32p4_es"` for rev v1.x, `"esp32p4"` for rev v3+. The
  Arduino core ships prebuilt `esp32p4_es` libs (rev v1.x memory map,
  NimBLE + esp-hosted enabled), and pioarduino selects them and the matching
  linker templates from that field. This is the same fix arduino-esp32 used
  for another rev v1.x P4 product. [Why not a platform downgrade or an
  sdkconfig override](#alternatives-and-dead-ends).
- A product that exists in both generations gets TWO envs and two images,
  never a runtime check: `esp32-p4-micra-5` / `-5-rev3` and
  `esp32-p4-micra-43` / `-43-rev3`. A rev3 env defines
  `BOARD_P4_SILICON_REV3`, which selects the DSI PHY's XTAL PLL reference
  (see the DSI panels record) and the `<plain-slug>-rev3` OTA slug. The
  plain slug keeps meaning rev v1.x forever, because fielded units
  self-update from it. One product, two OTA lines; `kName` is the same.
- The X boxes are handled per unit: the X 8" and X 10.1" jsons are
  `esp32p4` (rev3-only images), the X 7" json is `esp32p4_es`.
- Picking the image is the flasher's and `make flash`'s job: the identify
  banner's `REV=` (efuse major*100+minor, >= 300 = rev3) and the ROM
  loader's chip revision. The details are in the supported-board-matrix
  record. Picking wrong is recoverable: flash the other image.

## Consequences

- Always `esptool chip-id` a new P4 unit before flashing it.
- One image cannot boot both generations, so every new P4 product that
  ships in both doubles its images, not its product list.
- rev v1.x runs the CPU at 360 MHz at most; the boot warning about
  400 MHz is expected there.

---

## Details

### Evidence
<a id="ev-symptoms"></a>

| Image built for | Running on | What happens |
|---|---|---|
| rev v3 (default target) | rev v1.x | The 2nd-stage bootloader boot-loops: `Guru Meditation ... (Illegal instruction)` with PC at the bootloader entry and MTVAL=0. Black screen, no app output. |
| rev v1.x (`esp32p4_es`) | rev v3 | Boot-loops with `CHIP_LP_WDT_RESET` before printing anything (the v0.9.0 X 8" image on a rev v3.2 unit). |

- First P4 hardware (a rev v1.3 P4-4.3, 2026-07-17, `a84f57b`) hit the
  first row; `chip_variant "esp32p4_es"` fixed it.
- The bench X 7" is rev v1.3; the bench X 8" is rev v3.2. Rev v3.2
  P4-4.3 and P4-5 units booted on the rev3 images on 2026-09-22 (issue #2).

### Alternatives and dead ends

- **Platform downgrade to 54.03.21-2:** P4 Bluetooth is compiled out there.
- **Hand-rolled `custom_sdkconfig` revision overrides:** pioarduino's hybrid
  compile generates rev3 linker scripts regardless (a builder bug), and the
  hybrid build is a dead end on these projects anyway (see the repo-hygiene
  record).
- **A runtime `efuse_hal_chip_revision()` check:** buys nothing.
  `chip_variant` is a link-time split, so the two generations are separate
  binaries either way.

### Open

- Rev3 pads are documented to hold their state through deep sleep, so the
  pull-down mods the rev v1.x boards needed should be unnecessary there.
  Untested.

### History

- 2026-07-17: first P4 bring-up; `esp32p4_es` chosen for the rev v1.3 unit
  (`a84f57b`).
- 2026-09-22: rev3 sibling envs and slugs for the P4-5 and P4-4.3, the
  `REV=` field drives the flasher's Detect (issue #2, `770f51f`); the X 8"
  json moved to `esp32p4` after the v0.9.0 image boot-looped on rev v3.2.
- 2026-11-07: written as its own record from the project's working notes
  (CLAUDE.md); previously scattered across the board notes.

### Sources

`boards/*.json` (`chip_variant`); `platformio.ini` (the `-rev3` envs);
`include/platform_esp32/board_config.h` (`BOARD_P4_SILICON_REV3`, slugs);
`tools/flash.sh`; `site/index.html` (Chip revision radio); commits
`a84f57b`, `770f51f`; issue #2.
