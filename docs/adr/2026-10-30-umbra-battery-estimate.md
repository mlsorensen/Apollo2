# Umbra battery: estimate the level once the scale's gauge freezes on sleep

- **Status:** Open (built and shipping in the 1.3.0 betas; blob format frozen; the frozen-vs-last-live check at a sleep below 100 % and the first full calibration run are still outstanding)
- **Area:** radio
- **First decided:** 2026-10-30 · **Last changed:** 2026-11-01
- **Boards:** all (Umbra in partner mode only)

## Context

Once an Umbra has slept, its reported battery percent stops updating until
the scale restarts (power cycle, or plugging or unplugging the charger).
Every client sees the same stale value, so it is the scale's own gauge, and
the pack then dies "suddenly" at an unchanged reading. Partner mode sends
the Umbra to sleep with Apollo's screensaver and its button sits in the drip
tray, so a manual power cycle is not a practical workaround. Reported to
the vendor. Issue #5.

## Decision

`core::ScaleBatteryEstimate` (f7a9ca7), Umbra-only, gated on partner mode
(only a partner Umbra sleeps; other scales may report inaccurate levels but
never stale ones):

- Two states only. LIVE: show the reported value as-is, charging or not.
  STALE: from the moment Apollo sends the sleep frame until a reading
  DIFFERS from the stored last-live value (a frozen gauge cannot change).
  The sleep timer is never consulted; with sleep Off the value moves and
  flips live at once.
- Estimate while stale = last-live % minus drain rate x days stale, floored
  at 0, fed to the normal low-battery warning and marked `~`.
- Drain rate is a user setting on the Umbra's Device settings page; optional
  user-armed calibration ("charge fully, use until empty, then charge")
  measures the next stale period and offers the result (Store / Discard).
  Disabled while the scale's sleep is Off.
- Time counts only after a real NTP sync this boot (the schedule's gate); no
  trusted clock = never goes stale.
- Persisted only at live/stale transitions and session ends, never per
  reading (a live gauge flickers +-1), through `persist_for` under the roles
  lock (#6).

| Constant | Value |
|---|---|
| Drain rate default | 15 %/day (stock battery ~6 to 7 days) |
| Drain rate range | 1 to 30 %/day |
| Blob (`sbatest` / `sbatest2`) | 20 bytes: rate + state + FNV-1a tag of the scale's MAC |
| Host test | `make test-battery`, 58 checks |

## Evidence

Bench facts, 2026-10-30: drain is calendar-dominated (15 or 45
min/day of use both die in 10 to 12 days on an upgraded battery, about
8 %/day); readings while charging are inflated (a drained scale reads 28 %
the moment it is plugged in; unplugged at "75 %" it reconnected at 51 %);
plug-in and unplug both restart the scale; live readings flicker 35/34/35.
A calibration run armed at 7 %/day on the bench X-8: `stale at 100 %` at
15:18:12 on 2026-10-30, the estimate held `~100 %` through two reboots.

## Alternatives and dead ends

- Awake-minutes models and auto-learning from the %-curve: rejected (the
  gauge plateaus at 100 % for an hour or more).
- Staying connected through the saver: rejected (shortens the charge
  interval).
- Deciding staleness from the sleep timer: rejected; the design must work
  for every timer value.
- Charging detection / distrust of plugged-in readings: dropped
  the same day for the two-state model.
- A separate `sdrain` key: folded into the blob with a MAC tag so a blob
  left behind for another scale is ignored (d4037be).

## Consequences

- The blob format is FROZEN: a live calibration run depends on it.
- The sleep note is set BEFORE the sleep frame goes out, because the Umbra
  may drop the link on receipt and the disconnect callback must find it.
- Open: the +-1 frozen-vs-last-live check at a sleep below 100 %; an Apollo
  reboot while the Umbra is connected and awake may let it sleep unmarked
  (unproven); the first complete calibration result.

## History

- 2026-10-30: design settled; core engine (f7a9ca7), wired to the partner Umbra + settings (9b9e56e), review fixes incl. MAC-tagged blob (d4037be), persist under the roles lock (8e11974), one `~` (2f17262); issue #5 opened.
- 2026-11-01: shipped in v1.3.0-beta.1 (9f8379e).

## Sources

- Commits f7a9ca7, 9b9e56e, d4037be, 8e11974, 2f17262, 9f8379e; tag v1.3.0-beta.1.
- Issue #5.
- `include/core/scale_battery_estimate.h`, `tools/scale_battery_test.cpp`, `src/platform_esp32/umbra_partner.cpp`.
