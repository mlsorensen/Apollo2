# S3 boards: bring NimBLE up before WiFi, and never script a reset over USB-CDC

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-07-18 · **Last changed:** 2026-09-09
- **Boards:** the native-radio S3 boards (4.3C released; 2-inch, 7B, 4.3B internal); the P4 is unaffected by both

## Context

Two S3-only traps that each masqueraded as something else.

The boot crash. On the 4.3C about half of all boots died with `Guru
Meditation: Stack canary watchpoint triggered (ipc0)`, always right at
`Network: connecting`. It first surfaced on 2026-09-08 as "on-device OTA
doesn't stick": the freshly installed v0.10.0 image crashed inside its 60 s
rollback-verify window and the bootloader correctly reverted to the old slot.

The serial trap. During 4.3C bring-up on 2026-07-18, scripted DTR/RTS
toggling on the board's USB-Serial-JTAG port dropped it into ROM download
mode (`boot:0x0 DOWNLOAD`): black screen, no serial output, yet flashing
still succeeded because the ROM answers esptool. It looked exactly like a
firmware hang at boot, and time went into a phantom display bug.

## Decision

- `setup()` calls `NimBLEDevice::init()` before `g_network.begin()`, so the
  BT controller allocates its interrupts on a quiet `ipc0` task before any
  WiFi radio ISR exists. The ordering is harmless on the P4 (its radio is
  brought up earlier through `hostedInitBLE()`).
- Never toggle DTR/RTS on an S3's USB-CDC port and never script reset
  dances. `monitor_dtr = 0` / `monitor_rts = 0` are pinned in every env.
  Verification is on the device screen; if serial is needed, open the port
  passively and accept missing the boot banner, or press RST by hand
  with a monitor attached. A board that goes black and silent right after
  serial experiments is in download mode until proven otherwise: physical
  RST or a USB replug recovers it.

## Evidence

From the 2026-09-08 coredump decode (4.3C):

| item | value |
|---|---|
| crashed task | `ipc0`, 896 B used / 124 B free of ~1 KB |
| blocked caller | `btController` in `esp_ipc_call_blocking(func=btdm_intr_alloc)` |
| `loopTask` | `setup()` -> `NimBLEDevice::init` -> `esp_bt_controller_init` |
| trigger | `g_network.begin()` (async WiFi connect) ran first; WiFi ISRs nested on `btdm_intr_alloc` in the starved task |
| panic PC | the same address in v0.0.1 and v0.10.0 (IDF library code) |

After the reorder (`4c17e71`): 10 of 10 clean boots with WiFi connect
exercised each time, coredump bytes 0. The reorder shows in the boot log as
a lower heap-free at `Network: connecting`, NimBLE having taken its share
first.

For the serial trap: software resets (`esptool --after hard_reset`, RTS
pulses) did not reliably leave download mode; only the RST button or a
replug did (2026-07-18, 4.3C).

## Alternatives and dead ends

- Raising `CONFIG_ESP_IPC_TASK_STACK_SIZE`: `esp_ipc` is in the prebuilt
  core, and the S3 hybrid-sdkconfig route fails on unused managed components.
- A settle delay between WiFi start and BT init: weaker than the reorder,
  since it only narrows the race.
- Validating firmware over scripted serial on the S3: given up on the
  standing instruction after the download-mode incident.

## Consequences

- The S3's OTA path was never broken; the rollback was doing its job. A
  "didn't stick" report on an S3 means: read the coredump of the new image
  first.
- The P4's console (CH343 on the X 8", USB-Serial-JTAG on the 4.3/5) does
  reset the board on open in some setups, so the one-step-at-a-time bench
  rule applies to every board.
- The `pio device monitor` route became safe for log reading once the
  monitor flags were pinned (2026-09-09), which is what made polling `/log`
  during performance work unnecessary (see the diagnostics record).

## History

- 2026-07-18: download-mode trap found during 4.3C bring-up; on-device
  verification adopted.
- 2026-09-08: ipc0 crash root-caused from the coredump and fixed by
  reordering `setup()` (`4c17e71`), shipped in v0.11.0.
- 2026-09-09: `monitor_dtr` / `monitor_rts` pinned to 0 in platformio.ini
  (`c3feeef`).

## Sources

Commits `4c17e71`, `c3feeef`; tag v0.11.0; `src/device/main.cpp` (the
comment above `NimBLEDevice::init`); CLAUDE.md "ESP32-S3-Touch-LCD-4.3C"
bring-up gotcha; `platformio.ini` monitor flags.
