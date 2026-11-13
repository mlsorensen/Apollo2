# Umbra battery: estimate the level once the scale's gauge freezes on sleep

- **Status:** Open (built and shipping in the 1.3.0 betas; blob format frozen; the frozen-vs-last-live check at a sleep below 100 % and the first full calibration run are still outstanding)
- **Area:** radio
- **First decided:** 2026-10-30 · **Last changed:** 2026-11-13
- **Boards:** all (Umbra in partner mode only)

## Context

Once an Umbra has slept, its reported battery percent stops updating until
the scale restarts (power cycle, or plugging or unplugging the charger).
Every client sees the same stale value, so it is the scale's own gauge, and
the pack then dies "suddenly" at an unchanged reading. Partner mode sends
the Umbra to sleep with Apollo's screensaver and its button sits in the drip
tray, so a manual power cycle is not a practical workaround. Reported to
the vendor ([issue](#sources)).

## Decision

`core::ScaleBatteryEstimate`, Umbra-only, gated on partner mode (only a
partner Umbra sleeps; other scales may report inaccurate levels but never
stale ones):

- Two states only. LIVE: show the reported value as-is, charging or not.
  STALE: from the moment Apollo sends the sleep frame until a reading
  DIFFERS from the stored last-live value (a frozen gauge cannot change).
  The sleep timer is never consulted ([why not the timer or charging
  detection](#alternatives-and-dead-ends)).
- Estimate while stale = last-live % minus drain rate x days stale, floored
  at 0, fed to the normal low-battery warning and marked `~`.
- Drain rate is a user setting on the Umbra's Device settings page, with an
  optional user-armed calibration that measures the next stale period up to
  the scale's next restart and offers the result; a run under a day or under
  10 % of drop is reported as too short, never guessed at
  ([how](#dd-calibration), [why not
  auto-learned](#alternatives-and-dead-ends)).
- Time counts only after a real NTP sync this boot (the schedule's gate); no
  trusted clock = never goes stale.
- Persisted only at live/stale transitions and session ends, never per
  reading ([a live gauge flickers +-1](#evidence)), through `persist_for`
  under the roles lock ([constants and blob](#constants)).

## Consequences

- The blob format is FROZEN: a live calibration run depends on it
  ([the armed run](#evidence)).
- The sleep note is set BEFORE the sleep frame goes out, because the Umbra
  may drop the link on receipt and the disconnect callback must find it.
- Open: the +-1 frozen-vs-last-live check at a sleep below 100 %; an Apollo
  reboot while the Umbra is connected and awake may let it sleep unmarked
  (unproven); the first complete calibration result.

---

## Details

### Design detail

<a id="dd-calibration"></a>
Calibration: arm, use the scale, restart it after a day or more (its button,
or the charger: both restart it). The run starts at the last-live % when the
scale goes stale and ends on the first reading that differs from it. That
reading is the measurement when it is below the start (drop = start minus
reading, over the time from going stale to the reading); at or above the
start the scale came back charged and the pack is taken to have died when
last seen, the original "use until empty" arithmetic. Guard rails: a span
under 24 h or a drop under 10 % is reported as too short (a whole-percent
gauge cannot resolve less; a 1 % tick after a recharge Apollo missed looks
the same). The measured drop and hours are persisted with the pending result
in the blob's former reserved bytes and shown in the Store / Discard modal.
The ending reading must be fresh: an Umbra that charged for an hour before
its next session reads high and the rate comes out low (MANUAL says so).
Disabled while the scale's sleep is Off, because with sleep Off the value
keeps moving and the state flips live at once.

<a id="constants"></a>

| Constant | Value |
|---|---|
| Drain rate default | 15 %/day (stock battery ~6 to 7 days; [bench drain](#evidence)) |
| Drain rate range | 1 to 30 %/day |
| Calibration guard rails | span >= 24 h and drop >= 10 % (`kCalibrationMinSpanS`, `kCalibrationMinDropPct`) |
| Blob (`sbatest` / `sbatest2`) | 20 bytes: rate + state + FNV-1a tag of the scale's MAC ([why tagged](#alternatives-and-dead-ends)); bytes 17..19 = the pending result's drop % and hours since v1.4.0 (zeros before) |
| Host test | `make test-battery` |

### Evidence

Bench facts, 2026-10-30: drain is calendar-dominated (15 or 45 min/day of
use both die in 10 to 12 days on an upgraded battery, about 8 %/day);
readings while charging are inflated (a drained scale reads 28 % the moment
it is plugged in; unplugged at "75 %" it reconnected at 51 %); plug-in and
unplug both restart the scale; live readings flicker 35/34/35.

A calibration run armed at 7 %/day on the bench X-8: `stale at 100 %` at
15:18:12 on 2026-10-30, the estimate held `~100 %` through two reboots.

### Alternatives and dead ends

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

### History

- 2026-10-30: design settled; core engine (f7a9ca7), wired to the partner Umbra + settings (9b9e56e), review fixes incl. MAC-tagged blob (d4037be), persist under the roles lock of issue #6 (8e11974), one `~` (2f17262); issue #5 opened.
- 2026-11-01: shipped in v1.3.0-beta.1 (9f8379e).
- 2026-11-13: a run ends at any restart and uses the restart reading when it is below the start; guard rails 24 h / 10 %; drop + hours persisted in the reserved bytes and shown in the modal (issue #12). Before, only a full drain measured and the ending reading was ignored.

### Sources

- Commits f7a9ca7, 9b9e56e, d4037be, 8e11974, 2f17262, 9f8379e; tag v1.3.0-beta.1.
- Issue #5 (the vendor report and this estimate); issue #6 (the roles lock); issue #12 (restart-reading calibration).
- `include/core/scale_battery_estimate.h`, `tools/scale_battery_test.cpp`, `src/platform_esp32/umbra_partner.cpp`.
