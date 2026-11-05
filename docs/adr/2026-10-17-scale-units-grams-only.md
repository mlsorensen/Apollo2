# Apollo is grams-only; a scale's Unit row is kept only where it changes the scale's own display

- **Status:** Accepted
- **Area:** radio
- **First decided:** 2026-10-17 · **Last changed:** 2026-10-17
- **Boards:** all

## Context

On 2026-08-20 the Acaia driver gained a Unit row (g/oz) for both
generations and an oz -> g conversion keyed on the status frame's unit
byte, on the understanding that an Acaia streams weight in its display
unit. On 2026-10-17 an Umbra (firmware 1.0.8) set to oz streamed 29 for a
29 g load, and the conversion showed 822 g (29 x 28.35). An oz stream would
have read about 1.02.

## Decision

- Apollo displays grams everywhere, and only grams. Decided 2026-10-17: no
  one brews coffee in ounces at these weights. An earlier same-day plan (the
  UI following the scale's unit, per-model configurable) was dropped.
- Umbra: the weight stream is ALWAYS grams whatever its stored unit says;
  the unit is a preference a partner device displays in (the Umbra has no
  display). The driver never converts for the Umbra, offers no Unit row in
  either mode, and partner mode dropped its read-only "Grams only" row for
  the same reason (0301274).
- Lunar/Pyxis: the WRITABLE Unit row stays. It really switches the scale's
  own display, the stream follows that display unit, and the driver converts
  oz -> g (x 28.3495) off the status unit so targets, detector and graphs
  stay in grams. A read-only row was tried and reverted the same day (per
  the notes; the revert is not a separate commit).

## Evidence

| Observation | Unit / date |
|---|---|
| Umbra set to oz, 29 g load: stream 29, displayed 822 g before the fix; correct grams with g or oz after | Umbra fw 1.0.8, central link, 2026-10-17 |
| Partner (relay) stream with unit = 1: weight unchanged | S3 rig, 2026-10-17 |

## Alternatives and dead ends

- Converting the Umbra by its status unit (shipped 2026-08-20 to
  2026-10-17): wrong by a factor of 28 in oz mode.
- UI in the scale's unit: dropped.

## Consequences

- Resolution and capacity rows are deliberately not exposed (Apollo shows
  0.1 g; high-resolution mode halves a Lunar's range).
- Any new driver streams grams into core or converts inside the driver;
  nothing above the driver knows about ounces.

## History

- 2026-08-20: Acaia Unit row + oz -> g conversion for both generations (601e9b9).
- 2026-10-17: Umbra always grams, no conversion, no row (0301274, v1.1.0-beta.1); grams-only ruling.

## Sources

- Commits 601e9b9, 0301274; tag v1.1.0-beta.1.
- `src/core/scale_driver_acaia.cpp`.
