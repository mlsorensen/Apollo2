# The schedule engine fires one-shot edges inside a 2-minute window, and only on a clock it trusts

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-09-22 · **Last changed:** 2026-11-02
- **Boards:** all

## Context

Issue #3 asked for one "on" window per day, daily or per weekday, that turns
the Micra on and sends it to standby, with a warm-up lead so the machine is
at temperature by the set time. The hazards are all about firing wrongly: a
machine switched on at 06:30 while nobody is home, a standby that cuts a
shot short, a stale trigger replayed after a reboot, or a clock that is
plausibly wrong (the RTC boards boot with a coin-cell time that looks fine
and may not be). This record covers the engine's rules; the page design and
auto-standby's behaviour are the ux area's record.

## Decision

`core::ScheduleEngine` (`include/core/schedule.h`, `src/core/schedule.cpp`)
is pure domain code owned by `ui::App` and fed from the 500 ms refresh with
the clock, the link and power state, `BrewSnapshot::phase`, a `busy` flag
(cleaning, or the drive line held with no shot phase) and the tick. Its
rules, deliberate decisions, not to drift:

| rule | detail |
|---|---|
| edges, not periods | a time is a one-shot event; the engine never holds the machine in a state |
| fire window | a trigger fires only if the link is Connected inside `kScheduleFireWindowMin` = 2 min of its time, otherwise it is dropped; no catch-up |
| latch | each slot latches by minute-of-week, so it fires once; an edit pre-latches anything already in-window, so dragging a time across "now" never switches the machine |
| no-op "on" | "on" when already on does nothing |
| deferred "off" | during a shot, its review or (since 2026-11-02) a cleaning cycle or a held drive line, the standby waits until everything has been idle for `kStandbyGraceMs` = 2 min (covers the auto-flush), then fires if still on; any new activity restarts the grace |
| trusted time | nothing fires unless WiFi and NTP are enabled, a real SNTP sync landed this boot (`ntp_seconds_since_sync() >= 0`), and the date is real |
| wrap | the warm-up lead may cross into the previous evening (minute-of-week arithmetic) |
| brewing source | `MachineSnapshot::brewing` is never set by the link; the engine reads `ShotPhase` |

A trigger inside its window that cannot fire notes why once ("due but
waiting: Micra not connected" / "clock not trusted"); if the window closes
unfired it logs `schedule: ... MISSED`. The Micra link's reconnects are no
longer parked under the screensaver (portal only), so a link lost overnight
is back before the morning window opens.

## Evidence

- `make test-schedule` (`tools/schedule_test.cpp`, plain g++) covers the
  rules above and must stay green.
- Cost at introduction: ~80 B internal .bss for the engine and working
  config; no task, no heap, no stack change. The MISSED logging added two
  int16 slots (4 B).
- 2026-09-30: a cold machine on a scheduled morning, the update notice
  waiting behind the screensaver. The ring had been lost to a beta install,
  so the cause is unproven; the one mechanism that fit exactly was the
  saver park (v0.11.2) holding both links' reconnects, so a Micra link
  dropped overnight stayed down until the first touch and the 06:30 trigger
  fell outside its window silently.

## Alternatives and dead ends

- A held "on period" that re-asserts the state: rejected at design; a user
  switching the machine by hand or from the cloud app must win.
- Catch-up firing after the window: rejected, a late "on" is the scenario
  we least want.
- Trusting any plausible system time: the RTC boards defeat it (the same
  false positive broke the update check's network gate on 2026-09-09).
- Keeping the Micra parked under the saver: a connected Micra costs no more
  RAM asleep than awake, and the reconnect churn exists only while the
  machine is unreachable, which the update check's heap gate already covers.

## Consequences

- The warm-up default moved from 6 to 8 min after bench tests (2026-09-22).
- Auto-standby (added 2026-09-23, reworked to an inactivity count on
  2026-11-02) lives in the same engine and needs no clock; its rules are in
  the ux record.
- The page disables the schedule controls until a Micra is paired and the
  time is trusted (auto-standby needs only the pairing); the manual carries
  the reason.

## History

- 2026-09-22: engine and page shipped (`194dd92`, issue #3), v1.0.0-beta.1.
- 2026-09-30: MISSED logging; Micra reconnects kept alive under the saver
  (`1bcca5e`), v1.0.0-beta.7.
- 2026-11-02: the `busy` input: cleaning and a held drive line count as
  activity for auto-standby and restart a deferred standby's grace
  (`7a9cdf7`, `8c2a918`).

## Sources

Commits `194dd92`, `add289d`, `1bcca5e`, `7a9cdf7`, `8c2a918`; issue #3
(closed 2026-10-10); `include/core/schedule.h`, `src/core/schedule.cpp`;
`tools/schedule_test.cpp`; CLAUDE.md "Scheduled on/standby".
