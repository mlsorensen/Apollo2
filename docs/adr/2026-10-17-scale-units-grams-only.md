# Apollo is grams-only; a scale's Unit row is kept only where it changes the scale's own display

- **Status:** Accepted
- **Area:** radio
- **First decided:** 2026-10-17 · **Last changed:** 2026-10-17
- **Boards:** all

## Context

Acaia scales carry a unit preference (g/oz), but the generations treat it
differently: a Lunar/Pyxis streams weight in its display unit, while an
Umbra (which has no display) streams grams whatever its stored unit says.
Converting an Umbra's stream by its unit byte therefore multiplies a
correct gram value by 28 ([the 822 g reading](#evidence)).

## Decision

- Apollo displays grams everywhere, and only grams: no one brews coffee in
  ounces at these weights ([the UI-follows-the-scale plan that was
  dropped](#alternatives-and-dead-ends)).
- Umbra: the weight stream is ALWAYS grams; the unit is a preference a
  partner device displays in. The driver never converts for the Umbra,
  offers no Unit row in either mode, and partner mode has no read-only
  "Grams only" row for the same reason.
- Lunar/Pyxis: the WRITABLE Unit row stays. It really switches the scale's
  own display, the stream follows that display unit, and the driver converts
  oz -> g (x 28.3495) off the status unit so targets, detector and graphs
  stay in grams ([why not read-only](#alternatives-and-dead-ends)).

## Consequences

- Resolution and capacity rows are deliberately not exposed (Apollo shows
  0.1 g; high-resolution mode halves a Lunar's range).
- Any new driver streams grams into core or converts inside the driver;
  nothing above the driver knows about ounces.

---

## Details

### Evidence

| Observation | Unit / date |
|---|---|
| Umbra set to oz, 29 g load: stream 29, displayed 822 g (29 x 28.35) before the fix; an oz stream would have read about 1.02; correct grams with g or oz after | Umbra fw 1.0.8, central link, 2026-10-17 |
| Partner (relay) stream with unit = 1: weight unchanged | S3 rig, 2026-10-17 |

### Alternatives and dead ends

- Converting the Umbra by its status unit (shipped 2026-08-20 to
  2026-10-17, on the understanding that an Acaia streams weight in its
  display unit): wrong by a factor of 28 in oz mode.
- UI in the scale's unit, per-model configurable: planned on 2026-10-17 and
  dropped the same day for grams-only.
- A read-only Unit row for the Lunar/Pyxis: tried and reverted the same day
  (per the notes; the revert is not a separate commit).

### History

- 2026-08-20: Acaia Unit row + oz -> g conversion for both generations (601e9b9).
- 2026-10-17: Umbra always grams, no conversion, no row, partner mode's "Grams only" row dropped (0301274, v1.1.0-beta.1); grams-only ruling.

### Sources

- Commits 601e9b9, 0301274; tag v1.1.0-beta.1.
- `src/core/scale_driver_acaia.cpp`.
