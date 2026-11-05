# A platform-free core, ports for everything the hardware does, and one header per board

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-07-09 · **Last changed:** 2026-10-24
- **Boards:** all

## Context

The same UI and the same Bluetooth protocol code have to run on a laptop,
an S3 and a P4 (whose radio sits on a separate ESP32-C6) without three
copies. The Micra link and the scale drivers are the code most worth
testing off-device, and the piece a Pi or Pico port would otherwise
rewrite. Boards differ in pins, expander and panel timing from the second
board on.

## Decision

Four layers, with hard rules on what each may include:

| layer | contains | may depend on |
|---|---|---|
| `include/core/` + `src/core/` | ports (`IMachine`, `IScale`, `IClock`, `INetwork`, `IShotStore`, `ble::ICentral`, ...) and protocol logic (Micra link, scale drivers, schedule engine, radio arbiter) | plain C++ only: no LVGL, Arduino, NimBLE or SDL |
| `src/ui/` | the LVGL interface | `core::` interfaces only |
| `src/platform_esp32/` | device implementations of the ports, drivers | anything |
| `src/platform_host/` | fakes that feed canned data | anything host-side |

Bluetooth protocol code is written against `core::ble::ICentral` (scan,
connect, read, write, subscribe by UUID); the only per-platform BLE code is
the transport (`nimble_central.cpp`). A new platform (Linux/BlueZ,
Pico/btstack) implements the transport and reuses the protocol unchanged
([why the seam sits here](#alternatives-and-dead-ends); [how it has
held](#evidence)).

Board differences live in `include/platform_esp32/board_config.h`, one
`#elif defined(BOARD_...)` block per board exposing the same constant names
and feature macros. Drivers read `board::` constants and never hardcode a
pin; a new board is a config block plus a `platformio.ini` env ([why not
`#ifdef`s in drivers](#alternatives-and-dead-ends)).

`src/core/` is compiled into the simulator build on purpose, so a platform
include slipping into core fails the host build instead of passing silently.

## Consequences

- The rules in README.md "Developer documentation" and CLAUDE.md are hard
  rules: `core/` stays free of LVGL/Arduino/NimBLE/SDL; the UI depends only
  on `core::`; new platforms implement `ICentral` and never touch protocol
  code.
- The host simulator renders every screen to PNG and is the primary check
  for UI work (the display area's simulator record).
- Buildable is not released; the release matrix is a separate, deliberate
  list (CLAUDE.md "Support matrix").

---

## Details

### Evidence

- Commit `4c7c117` (2026-07-09) moved the Micra protocol and the Bookoo
  driver out of `platform_esp32` into `src/core/` over the new port; the
  platform links became thin subclasses supplying the transport and the
  FreeRTOS task, and every env compiled unchanged.
- The P4 board (commit `400d95f`, the same day) was added as a board block
  plus a third display backend; the transport seam compiled against a
  different NimBLE packaging (see the P4 hosted radio record) with no change
  to protocol code.
- The seam held under later load: the two-scale radio arbiter
  (`core::RadioArbiter`, 2026-10-24) and the schedule engine (2026-09-22) are
  pure core code with host tests (`make test-radio`, `make test-schedule`).

### Alternatives and dead ends

- Keeping protocol code in the platform layer and abstracting only the UI.
  Rejected on 2026-07-09: the Micra link and the scale drivers were the code
  most worth testing off-device, and the one piece a Pi or Pico port would
  otherwise rewrite.
- Per-board `#ifdef`s inside drivers. Rejected from the second board on; the
  7B and 4.3B (2026-06-29 onward) already differed in pins, expander and
  panel timing, and the config-block form made the P4 a two-file change.

### History

- 2026-06-28: first checkpoint on the S3 2-inch board (`a960211`), a
  single-board S3 project: core / ui / platform split and the PNG-rendering
  host sim present, BLE protocol code still in the platform layer with
  NimBLE calls through it.
- 2026-06-29 / 2026-06-30: the 7B (`163c1ed`) and 4.3B (`c6ca7de`) boards
  added as config blocks.
- 2026-07-01: project renamed Apollo 2 (`b689d99`); the working directory
  kept its old name.
- 2026-07-09: a second chip family (the ESP32-P4 with its radio on a
  separate ESP32-C6) wanted within two weeks of the first checkpoint; BLE
  transport seam extracted (`4c7c117`); CLAUDE.md written with the layering
  rules (`84951b0`); first P4 board added as a config block (`400d95f`).
- 2026-10-24: radio arbitration added to core (`67e6f3e`), the largest
  later addition to the port layer, with no platform code in it.

### Sources

Commits `a960211`, `163c1ed`, `c6ca7de`, `b689d99`, `4c7c117`, `84951b0`,
`400d95f`, `67e6f3e`; `platformio.ini` `[env:sim]` `build_src_filter`; README.md "Developer
documentation"; CLAUDE.md top section; `include/core/`, `src/core/`,
`include/platform_esp32/board_config.h`.
