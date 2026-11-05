# The paddle path by board: expander DI/DO on the 4.3C, native GPIO plus an external opto on the P4s

- **Status:** Accepted
- **Area:** hardware
- **First decided:** 2026-07-18 · **Last changed:** 2026-11-03
- **Boards:** S3-4.3C (expander); P4-5, P4-4.3, X 8" (native GPIO). The 2-inch S3, 7B and 4.3B have no paddle path (pins -1).

## Context

Auto shot needs Apollo in the middle of the Micra's paddle circuit: the
physical switch feeds a sense input and an isolated output stands in for
the paddle on the controller's white/black pair (about 5 V from the
machine). Each board offers different hardware for that, and the driver
(`src/platform_esp32/paddle.cpp`) must never hardcode pins: board
differences live in `board_config.h`.

## Decision

**S3-4.3C: the isolated terminal block.** DO0 (EXIO6 on the CH32V003 IO
extension, active-LOW) is the open-collector contact: Micra white to DO0,
Micra black to GND. DI0 (EXIO0, LOW = closed) senses the paddle: one
switch wire to DI0, the other to **GND, not DI COM**. DI COM is the
biased side of the input opto, so a dry contact wired to it does nothing.
Direction mask 0xDE (EXIO0 and EXIO5 inputs). Config only; no new driver.

**P4 boards: native GPIO and an external PC817 module.** GPIO52 drives
the module's input (active-HIGH, the opposite of the expander); the
module's output side runs with VCC floating so OUT/GND are an isolated
dry contact (OUT to Micra white, GND to Micra black). GPIO51 reads the
switch to board GND with the internal pull-up (LOW = closed). The three
pins GND/52/51 sit in a row on the header corner, so a screw-terminal
block clips on; the switch touches only Apollo, never the Micra. The X
boards use the same pins. A second set for the Apollo Link (GPIO3/5/4)
sits at the header's 5 V end (its own record).

Pins avoided on purpose: GPIO2 (touch INT on the P4-4.3), GPIO7/8 (the
touch and codec I2C bus), GPIO37/38 (console UART; per the working notes
the USB-UART bridge drives 38 whenever the board is powered). GPIO50 was
briefly a firmware-driven 3.3 V supply for a second sense module
(2026-11-03, morning) and freed again the same day; it is a free pin.

## Evidence

- 4.3C brew-by-weight verified on hardware 2026-07-18; P4-4.3 paddle confirmed 2026-07-19 and again on the bench unit 2026-09-11; X 8" 2026-10-16.
- The expander can ACK its init yet DROP the first direction-mask write, leaving the inputs at 0xFF forever (seen on hardware). `Paddle::begin()` re-asserts the mask and `sensed()` rewrites it every 64th poll (~1.6 s at 25 ms). Keep both.
- The P4's native sense (weak pull-up, unshielded harness) picked up phantom OFF/ON pairs when the paddle-woken machine's pump kicked, re-closing the line on an awake machine: a real unintended brew. The 4.3C never saw it (opto-filtered input). Hence the 75 ms glitch filter: a level counts only after three consecutive 25 ms polls, boot adoption included (`2b2802c`).
- 4.3C, 2026-11-03: a failed expander read now keeps the last level instead of reading as open, so a bus hiccup under a held paddle is not a rejected pulse for the breaker (`0fec2f2`).

## Alternatives and dead ends

- Wiring the 4.3C's paddle return to DI COM: silently does nothing (biased input).
- A relay or a bare transistor on the P4 drive instead of the opto module: never pursued; the dry contact keeps the Micra's 5 V off Apollo's ground.
- A dual opto module board for the P4: rejected, its shared output ground would tie the Micra's black wire to Apollo GND.

## Consequences

- Native-GPIO units can remap a dead corner pad at runtime (`padsense`, `paddrive` in NVS, kept out of backups; `make padsense PIN=n`) so stock builds keep working on a repaired unit (`244ab4f`, 2026-07-31). The Link set has no remap: since 2026-11-03 the per-unit `padwire` setting (Settings > Micra > Controls > Paddle wiring) pins a unit to one set instead, so a dead pad on the other set is simply not read.
- The bare sense pad is exposed to the switch wire: see the sense-protection record for the 1 kΩ + 100 nF rule and the two dead pads.
- `IPaddle::own()` is a no-op on the 4.3C and the opto cable (no transfer switch); the paddle works only through Apollo there, which the wiring guide states up front.

## History

- 2026-07-18: 4.3C expander path, direction-mask re-assert (`59999de`).
- 2026-07-19: P4 native path, glitch filter (`2b2802c`).
- 2026-07-31: sense diagnostics and per-unit pin overrides (`244ab4f`).
- 2026-10-01: Link pin set driven alongside the corner pins (`c2b8d9a`); 2026-10-16 mapped on the X boards (`6aa35cc`).
- 2026-11-03: GPIO50 supply added and removed (`0eaf58c`, `e61abcc`); 4.3C read-failure fix (`0fec2f2`); a serial `lnksense` override for the Link sense pin (`b9d39e0`) replaced the same day, unreleased, by the Paddle wiring setting (`455e7c9`).

## Sources

include/platform_esp32/board_config.h (paddle blocks); src/platform_esp32/paddle.cpp; src/platform_esp32/io_extension.cpp; docs/WIRING.md.
