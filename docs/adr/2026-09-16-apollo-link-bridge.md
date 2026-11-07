# Apollo Link: a solid-state bridge so the paddle works without Apollo, on one cable that also powers it

- **Status:** Accepted (rev 0.6 in service; rev 0.7 drawn, gerbers cut and parts ordered 2026-11-03, not yet built)
- **Area:** hardware
- **First decided:** 2026-09-16 · **Last changed:** 2026-11-07
- **Boards:** P4-5, P4-4.3, X 8" (any P4 with the 40-pin header). Not the S3-4.3C: no header, its own isolators.

## Context

With the paddle wired through Apollo, the Micra's paddle is dead whenever
Apollo is off, unplugged or mid-update. The one hard requirement: unplug
the remote and the machine still works as stock. Out of scope: a clean
hand-over mid-shot, and a remote that is hung or misconfigured (the
Micra's own brew cutoff, about a minute, bounds a wedged DRIVE to one
run). A second wish: one cable to the display, no USB lead at the machine.

## Decision

A small open-hardware board (`hardware/paddle-bridge`, KiCad + gerbers +
BOM) in the paddle loop:

- Two Vishay LH1502 dual solid-state relays as SPDT (NC: the paddle to
  the Micra in copper; NO: to Apollo) and an AQY212EH form-A PhotoMOS
  standing in for the paddle on the Micra's wires. Every LED is driven
  straight from a P4 GPIO; the board has no logic
  ([why not a relay, an LCC110 or jumpers](#alternatives-and-dead-ends)).
- **CTRL is held HIGH from `Paddle::begin()` for as long as the firmware
  runs**, never tied to the Wired paddle setting: Apollo owns the paddle
  while booted; at reset, in install mode, crashed or unplugged the pin is
  an input and the paddle is in copper. The green LED means "Apollo has
  the paddle".
- A USB-C 5 V input (Rd on both CC pins, 1.5 A PTC) powers the display
  through its header over the same 5-wire cable
  ([which boards charge from it](#ev-bench)).
- A **second native pin set** at the header's 5 V end: DRV GPIO3, SENSE
  GPIO5, CTRL GPIO4 ([pins ruled out](#alternatives-and-dead-ends)). The
  X boards use the same set ([since v1.0.1](#ev-bench)).
- Both pin sets are driven and polled together by default (no connector
  to pick). **Settings > Micra > Controls > Paddle wiring** (Auto / Apollo
  Link / Opto cable, NVS `padwire`, per unit) pins a unit to one set: the
  senses are ORed, and a dead pad on the unused set can read LOW and hold
  the paddle closed ([the bench X 8"'s did](#history)).
- Rev 0.6 is in service with R6 1 kΩ on SENSE. Rev 0.7 puts SENSE behind
  an opto and adds C1 100 nF and an ESD diode at J1
  ([the parts, and how 0.7 got there](#alt-rev07)).

## Consequences

- Rev 0.6 boards in the field have only R6 1 kΩ on SENSE; connectors and
  wiring are identical across revisions and the firmware cannot tell them
  apart.
- A rotated Apollo-end plug with the Link powered can destroy the P4:
  power the Link last (WIRING.md). Never parallel two Apollos on one J1
  ([why](#ev-parallel)).
- Rev 0.6 boards carry D1 fitted reversed; harmless there
  ([the footprint slip](#hist-d1)).
- The paddle breaker drops CTRL on a trip, handing the paddle back in
  copper.

---

## Details

### Design detail

- Header pins for the Link set (P4-4.3 / P4-5 header; the X's J8 is
  pin-identical at the 5 V end): 5 V on 1 and 3, GND on 5 and 13, SENSE
  GPIO5 on 11, DRV GPIO3 on 12, CTRL GPIO4 on 14.
- Paddle wiring is `core::PaddleWiring` (kAuto 0, kLink 1, kCable 2) on the
  `IPaddle` port: `wiring_selectable()` (true only where both native sets
  exist, the P4s; the 4.3C reports no choice and the row is absent),
  `set_wiring()`, `wiring()`. NVS `padwire` (i32, an added key) is per
  unit: it stays out of backups and survives a restore, like `padsense` /
  `paddrive`. `main.cpp` sets the stored choice before `Paddle::begin()`,
  which then applies it once; a change at runtime drives the departing
  set's outputs low, makes them inputs (a Link on it drops to copper) and
  re-runs `begin()`, leaving the kept set in place.
- The manual shows Settings > Micra > Controls as two screenshots (the
  `micra_controls_2` render, scrolled by `App::scroll_settings_section`).

### Evidence

<a id="ev-bench"></a>Bench results by date:

| date | unit | result |
|---|---|---|
| 2026-10-01 | every assembled rev 0.6 board | passed the 8-step guided test in the `esp32-p4-linktest` firmware: copper pass-through with CTRL off, sense only with CTRL on, Micra line opens when Apollo owns it, SHOT closes it |
| 2026-10-01 | P4-5 | runs from the Link's 5 V; battery node 0.03 V, so the header does NOT reach the charger (P4-5 / P4-4.3 do not charge from the Link) |
| 2026-10-02 | X 8" | runs AND charges from the Link: 4.93 V at 1.00 A at the Link's input |
| 2026-10-05 | bench Micra, P4-5 | Link installed; basic function confirmed, the full set (auto shot, flush, copper fallback) not spelled out |
| 2026-10-16 | bench X 8" | v1.0.0 shipped the X block with the Link pins unmapped: the unit ran from the Link but never saw the paddle, LED dark. v1.0.1 maps the same set (the X header is pin-identical at the 5 V end) |

The 2026-10-01 and 2026-10-05 rows are from the working notes (the
linktest commit `18e4628` records the 0.03 V finding as "~0 V"); the
2026-10-02 figure is in `4976e8a` and WIRING.md; the 2026-10-16 row is
`6aa35cc`.

<a id="ev-parallel"></a>Two Apollos on one J1: a bench note of
2026-09-18 in the working notes records push-pull contention on
DRIVE/CTRL, and an unpowered unit clamps all three lines through its ESD
diodes.

### Alternatives and dead ends

- A DPDT mechanical relay with its coil on the rail (the first settled design): superseded by GPIO-driven SSRs so firmware can release the paddle.
- Heartbeat encoding on the drive line, a pre-takeover sense peek, make-before-break timing: only needed for the dropped hung-remote requirement.
- LCC110 (8 mA per LED, too much for one GPIO, per the working notes); solder jumpers for the AQW612EH alternate (too fiddly; through-hole footprints make it a drop-in instead).
- Pins for the second set: GPIO2 was rejected (touch INT on the P4-4.3), 7/8 are the I2C bus, 37/38 the console UART.
- <a id="alt-rev07"></a>An opto-only rev 0.7 (SENSE through a PC817, drawn 2026-11-02) was ordered and cancelled the same week, never built: the scope showed a capacitor, not a switch, holds the node. The sheet was briefly numbered 0.8 and renamed back: **rev 0.7 = opto (U4, D2, R6 560 Ω) + C1 100 nF + D3, a unidirectional 5 V ESD diode at J1 (BOM: Littelfuse SD05-01FTG, Nexperia PESD5V0L1UA as the alternate; the ...1BA part is bidirectional and must not be substituted). There is no 0.8.**

### History

- 2026-09-16: schematic and rev 0.5 gerbers (`bbc89b5`); rev 0.5 (4-pin Apollo connector, no USB-C) was never ordered.
- 2026-09-18: rev 0.5 BOM (`8a0aeac`); CTRL policy settled the same day per the working notes (no commit until the firmware landed on 2026-10-01).
- 2026-09-24: rev 0.6: USB-C power in, 5-pin J1 (`acff534`).
- 2026-10-01: firmware drives the Link set alongside the corner pins (`c2b8d9a`); bench tester (`18e4628`); docs.
- 2026-10-02: the X 8" charges from the Link (`4976e8a`).
- 2026-10-16: v1.0.1 maps the Link pins on the X boards (`6aa35cc`).
- 2026-11-02: the bench X 8"'s GPIO5 (Link SENSE) found dead, reading LOW; rev 0.7 drawn with SENSE opto-isolated and the D_H symbol fixed (`4fdaea0`).
- 2026-11-03: the opto-only 0.7 withdrawn; C1 and D3 added under the number 0.8, then renamed 0.7 (`89bcba9`, `4011ba3`, `846d9d4`); layout and gerbers (`cac0a35`); D3 corrected to a unidirectional part (`e482d11`, `1a803ab`); Paddle wiring setting (`455e7c9`).
- 2026-11-07: folded in from the project's working notes (CLAUDE.md).

<a id="hist-d1"></a>The D1 footprint slip: the rev 0.5 and 0.6 sheets
place D1 reversed (symbol pin 1 was the anode, the footprint's band is
pad 1); harmless there, fatal for D2, fixed in the 0.7 symbol
(`4fdaea0`). Rev 0.5 was designed but never built: before it was ordered,
the design changed so that the one cable also carries power to Apollo,
and rev 0.6 is rev 0.5 plus the USB-C input with its fuse and the
resistors it requires, and a 5-pin Apollo connector carrying 5 V in place
of rev 0.5's 4-pin one. Its gerbers (`bbc89b5`) and BOM (`8a0aeac`) were
superseded on 2026-09-24, and the first boards built were rev 0.6, so the
reversed D1 exists only on rev 0.6 boards.

### Sources

hardware/paddle-bridge/ (sheet notes, BOM); docs/WIRING.md "Apollo Link"; src/platform_esp32/paddle.cpp; include/core/paddle.h; src/platform_esp32/config_backup.cpp (per-unit keys); src/linktest/main.cpp; the sense-protection record `2026-07-31-paddle-sense-protection.md`.
