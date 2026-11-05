# The paddle path by board: expander DI/DO on the 4.3C, native GPIO plus an external opto on the P4s

- **Status:** Accepted
- **Area:** hardware
- **First decided:** 2026-07-18 · **Last changed:** 2026-11-03
- **Boards:** S3-4.3C (expander); P4-5, P4-4.3, X 8" (native GPIO). The 2-inch S3, 7B and 4.3B have no paddle path (pins -1).

## Context

Auto shot puts Apollo in the middle of the Micra's paddle circuit: the
physical switch feeds a sense input and an isolated output stands in for
the paddle on the controller's white/black pair (about 5 V from the
machine). Each board offers different hardware for that. The driver
(`src/platform_esp32/paddle.cpp`) never hardcodes pins; board differences
live in `board_config.h`.

## Decision

**S3-4.3C: the isolated terminal block.** DO0 (EXIO6 on the CH32V003 IO
extension, active-LOW) is the open-collector contact on the Micra's
wires; DI0 (EXIO0, LOW = closed) senses the paddle, its return to **GND,
not DI COM** ([why](#alternatives-and-dead-ends)). Direction mask 0xDE,
re-asserted at begin and on every 64th poll because
[the expander can drop the first write](#ev-mask-drop). Config only; no
new driver ([the wiring](#ev-wiring)).

**P4 boards: native GPIO and an external PC817 module.** GPIO52 drives
the module's input (active-HIGH); the module's output side, VCC floating,
is the isolated dry contact on the Micra's wires. GPIO51 reads the switch
to board GND with the internal pull-up (LOW = closed). GND/52/51 sit in a
row on the header corner for a screw-terminal block; the switch touches
only Apollo, never the Micra. The X boards use the same pins; the Apollo
Link's second set (GPIO3/5/4) sits at the header's 5 V end (its own
record). GPIO2, 7/8 and 37/38 are [avoided on purpose](#ev-pins); GPIO50
is a free pin ([once briefly a sense supply](#history)).

A sense level counts only after three consecutive 25 ms polls (the 75 ms
glitch filter) because [the bare P4 sense picks up phantom edge
pairs](#ev-glitch).

## Consequences

- Native-GPIO units remap a dead corner pad at runtime (`padsense`,
  `paddrive` in NVS, outside backups; `make padsense PIN=n`). The Link
  set has no remap: the per-unit `padwire` setting (Settings > Micra >
  Controls > Paddle wiring) pins a unit to one set instead, so a dead pad
  on the other set is not read ([since 2026-11-03](#history)).
- The bare sense pad is exposed to the switch wire: the sense-protection
  record has the 1 kΩ + 100 nF rule and the two dead pads.
- `IPaddle::own()` is a no-op on the 4.3C and the opto cable (no transfer
  switch); the paddle works only through Apollo there, as the wiring guide
  states up front.
- A failed 4.3C expander read keeps the last level, so a bus hiccup under
  a held paddle is not a rejected pulse for the breaker
  ([since 2026-11-03](#history)).

---

## Details

### Evidence

- <a id="ev-wiring"></a>Wiring, as verified on hardware. 4.3C: Micra
  white to DO0, Micra black to GND; one switch wire to DI0, the other to
  GND. The mask 0xDE makes EXIO0 and EXIO5 inputs. P4: GPIO52 to the
  module's IO (module input GND to board GND), active-HIGH (the opposite
  of the expander); on the output side OUT to Micra white, GND to Micra
  black. 4.3C brew-by-weight verified 2026-07-18; P4-4.3 paddle confirmed
  2026-07-19 and again on the bench unit 2026-09-11; X 8" 2026-10-16.
- <a id="ev-mask-drop"></a>The expander can ACK its init yet DROP the
  first direction-mask write, leaving the inputs at 0xFF forever (seen on
  hardware). `Paddle::begin()` re-asserts the mask and `sensed()`
  rewrites it every 64th poll (~1.6 s at 25 ms). Keep both.
- <a id="ev-glitch"></a>The P4's native sense (weak pull-up, unshielded
  harness) picked up phantom OFF/ON pairs when the paddle-woken machine's
  pump kicked, re-closing the line on an awake machine: a real unintended
  brew. The 4.3C never saw it (opto-filtered input). Hence the 75 ms
  glitch filter: a level counts only after three consecutive 25 ms polls,
  boot adoption included (`2b2802c`).
- <a id="ev-pins"></a>Pins avoided on purpose: GPIO2 (touch INT on the
  P4-4.3), GPIO7/8 (the touch and codec I2C bus), GPIO37/38 (console
  UART; per the working notes the USB-UART bridge drives 38 whenever the
  board is powered).
- 4.3C, 2026-11-03: a failed expander read now keeps the last level
  instead of reading as open, so a bus hiccup under a held paddle is not
  a rejected pulse for the breaker (`0fec2f2`).

### Alternatives and dead ends

- Wiring the 4.3C's paddle return to DI COM: silently does nothing. DI
  COM is the biased side of the input opto, so a dry contact wired to it
  never closes the input.
- A relay or a bare transistor on the P4 drive instead of the opto
  module: never pursued; the dry contact keeps the Micra's 5 V off
  Apollo's ground.
- A dual opto module board for the P4: rejected, its shared output
  ground would tie the Micra's black wire to Apollo GND.

### History

- 2026-07-18: 4.3C expander path, direction-mask re-assert (`59999de`).
- 2026-07-19: P4 native path, glitch filter (`2b2802c`).
- 2026-07-31: sense diagnostics and per-unit pin overrides `padsense` /
  `paddrive`, so stock builds keep working on a repaired unit (`244ab4f`).
- 2026-10-01: Link pin set driven alongside the corner pins (`c2b8d9a`).
- 2026-10-16: Link pin set mapped on the X boards (`6aa35cc`).
- 2026-11-03: GPIO50 briefly a firmware-driven 3.3 V supply for a second
  sense module, added in the morning (`0eaf58c`) and removed the same day
  (`e61abcc`); 4.3C read-failure fix (`0fec2f2`); a serial `lnksense`
  override for the Link sense pin (`b9d39e0`) replaced the same day,
  unreleased, by the Paddle wiring setting `padwire` (`455e7c9`).

### Sources

include/platform_esp32/board_config.h (paddle blocks); src/platform_esp32/paddle.cpp; src/platform_esp32/io_extension.cpp; docs/WIRING.md.
