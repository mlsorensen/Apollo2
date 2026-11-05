# P4 boards power-cycle on USB plug or unplug: a break-before-make power mux, not firmware

- **Status:** Accepted (not fixable in firmware)
- **Area:** hardware
- **First decided:** 2026-07-23 · **Last changed:** 2026-10-02
- **Boards:** P4-5 and P4-4.3 (schematic-verified, identical power section). The X boards share the electronics but were not checked. S3 boards do not do this.

## Context

Plugging a USB cable into a running P4 with a battery fitted, or pulling
it, resets the board fully (reset reason: power-on). It looked like a
firmware or brown-out bug, and the question was whether it could be
fixed.

## Decision

It cannot, and we do not try. The board's charger is charge-only with no
system power path. System 5 V comes from EITHER the USB ports through a
pair of PMOS switches OR the battery through a boost converter, and a
small FET pulls the boost's enable low the moment any VBUS appears
(around 1.5 to 2 V), before the USB path can carry load; on unplug the
boost restarts through its soft-start. Bulk capacitance on the 5 V rail
is only about 30 to 45 µF, so at the board's 0.5 to 1 A draw the rail is
gone in under a millisecond either way, the 3.3 V buck drops out and the
chip sees a power-on reset. Both USB-C ports land on the same mux node,
so either port triggers it. The practical firmware answer is fast boot and
state restore; the user-facing docs note the trait.

Why the S3 boards are immune: their power-bank IC's boost output drives
the 5 V rail continuously, diode-ORed with VBUS behind 100 µF, so the
hand-over is make-before-break by construction.

## Evidence

Schematic reading of the P4-4.3 and P4-5 (same reference designators),
2026-07-23, recorded in the working notes only: nothing in the firmware
changed, so there is no commit, and the capacitance, current and
threshold figures above are the notes' estimates from the schematic, not
measurements. A due-diligence session the same day noted that the reset
reason cannot discriminate a rail collapse from an EN-pin reset through
the USB-UART bridge's DTR/RTS circuit (both log power-on, and RTC memory
survives neither). Clean discriminators: plug or unplug on the OTG port
(no path to EN at all), or a dumb charger on the UART port.

## Alternatives and dead ends

- A Schottky diode-OR from the battery to the 5 V rail (the S3 topology with a raw battery): marginal, the 3.3 V buck is a 16 V-class part whose under-voltage lockout sits in the battery's own band, so it would hold only on a high charge.
- Shrinking the boost's soft-start capacitor, or a bulk electrolytic or supercap on the header's 5 V pin sized for 5 to 10 ms of ride-through: sound board mods, not done.
- Firmware: nothing to do; the chip is unpowered for the event.

## Consequences

- Expect a reboot when cabling a battery unit; nothing is damaged.
- Related, measured later: back-feeding the header's 5 V pin from the Apollo Link powers the board, but on the P4-5 / P4-4.3 does NOT reach the charger (battery node 0.03 V header-powered, 2026-10-01), while the X 8" charges from it (2026-10-02). Whether header power also removes the plug/unplug reset is unmeasured.

## History

- 2026-07-23: root cause read off the schematics; verdict accepted.
- 2026-10-01: P4-5 powered from the Link's 5 V on the header, battery node 0.03 V (working notes; the linktest commit `18e4628` records "~0 V").
- 2026-10-02: the X 8" charges from the Link, 4.93 V at 1.00 A (`4976e8a`).

## Sources

The boards' published schematics; CLAUDE.md (P4-4.3 battery notes); docs/WIRING.md (Link, "Battery units"); the Link record `2026-09-16-apollo-link-bridge.md`.
