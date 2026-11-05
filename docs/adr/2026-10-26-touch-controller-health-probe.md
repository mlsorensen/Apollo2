# A quiet touch controller gets counters and a live probe, not an automatic reset

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-10-26 · **Last changed:** 2026-10-26
- **Boards:** every GT911 board for the counters and probe; the reset lever needs a TP_RST line (`kTouchCanReset`: an expander bit on the 7B/4.3B/4.3C, GPIO23 on the P4-4.3 and X 8"), which the P4-5 and X 7"/10.1" (`kTouchRst = -1`, not wired to the P4) and the 2-inch (CST816) lack

## Context

To the driver, a GT911 that has stopped answering and a GT911 nobody is
touching look the same: LVGL renders, the heap is flat, the links and HTTP
are fine, and the log holds nothing. A reset restores the glass, so the
controller hangs, not the panel ([the night it happened](#ev-hang)). Two
faults hide behind that silence, the chip no longer acknowledging I2C or
acknowledging but never raising its "data ready" bit, and the fix for each
differs.

## Decision

- `touch.cpp` keeps health counters in the read callback: polls, I2C
  failures, the current and longest failure streak, last raw status, last
  press and last error, shown in `/api/summary`'s `touch` block. A streak
  past ~2 s (at LVGL's 33 ms read period) logs `touch: controller not
  answering`; its end logs `answering again`. Lines are at least a minute
  apart, deferred not dropped, so a flapping bus does not write a pair every
  few seconds.
- `GET /api/touch` runs a live probe (ACK, GT9xx product id, config version,
  raw status, INT level) on the LVGL task between two polls ([why not the
  web task](#alternatives-and-dead-ends)): queued with a sequence number,
  completed by the reader, one caller at a time, withdrawn on timeout.
  `?reset=1` pulses TP_RST (~110 ms) and re-probes: the in-place recovery,
  no reboot ([exercised on the X 8"](#ev-cost)).
- No automatic reset, on purpose ([why](#alternatives-and-dead-ends)). The
  next occurrence's signature decides the fix: `ack=false` or an empty
  product id means the chip stopped answering; `ack=true` with status bit 7
  never set means it answers but is not sensing.
- `gt911_find()` writes the address only on a hit, so a probe during a
  glitch never turns a quiet controller into a forgotten one; the polls
  must keep talking to the bus to see the chip come back.
- Cost: 120 B of .bss and two stack buffers, no task, no heap
  ([itemised](#ev-cost)).

## Consequences

- Next time: fetch `/api/touch` before anything resets the board, read
  `ack` and the status bit, then decide.
- A probe request that times out because LVGL stalled is withdrawn, with
  one acknowledged window: a tick that took it in the same instant still
  runs it (a reset included) and the caller sees `done=false`; re-fetch
  before concluding nothing happened.
- The cause of the hang is unknown; one occurrence, no capture yet. The
  record stays Accepted because the decision is about what to measure, not
  about the fault.

---

## Details

### Evidence

<a id="ev-hang"></a>
On the night of 2026-10-25 the bench X 8" sat under its screensaver
ignoring the glass. LVGL was rendering, the heap was flat, the Micra link
was fine, HTTP answered, and the log held nothing. A reset restored it, so
the controller had hung, not the panel. There was no way to tell, before
resetting, which of two things had happened: the chip no longer
acknowledging I2C, or acknowledging but never raising its "data ready" bit.

<a id="ev-cost"></a>

| item | value (X 8", 2026-10-26) |
|---|---|
| probe answer | product id "9271", config version 130 |
| after `?reset=1` | chip re-found at 0x5D, touch works |
| cost | 120 B .bss; summary stack buffer 736 -> 896 B; a 384 B stack buffer in the touch handler (web task on the P4s, loop task on the S3s); no task, no heap |
| probe of a NAKing chip | a few ms on the LVGL task (single-shot re-find) |
| wedged bus | ~250 ms, each transaction paying Wire's 50 ms timeout, the same cost every poll already pays there |

The cost was stated and approved under the memory budget rule before it was
built; two independent reviews (9 + 3 findings) were applied.

### Alternatives and dead ends

- Resetting the controller automatically after a long failure streak:
  rejected, it would destroy the evidence that distinguishes the two failure
  modes, and the fix for each differs.
- Probing from the web task directly: rejected, the GT911's register pointer
  is shared state with the poll running on the LVGL task; a second task
  reading registers would move the chip's pointer mid-poll.
- Logging every press (the original calibration aid): capped at the first
  ten presses of a boot in 2026-09 (`1762b99`, still `kMaxLoggedPresses` in
  `touch.cpp`, independent of the Detailed log switch); it is not how this
  fault shows.

### History

- 2026-10-25: the overnight hang on the X 8"; a reset restored it.
- 2026-10-26: counters, probe, reset lever and the two log lines shipped
  (`de3c6d8`); MANUAL "Touch check" added.

### Sources

Commit `de3c6d8` (v1.2.0); `src/platform_esp32/touch.cpp`, `web_ui.cpp`
(`handle_touch`, `handle_summary`), `include/platform_esp32/board_config.h`
(`kTouchRst`); CLAUDE.md "Touch controller diagnostics"; MANUAL.md "Touch
check".
