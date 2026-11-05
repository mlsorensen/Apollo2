# The schedule engine fires one-shot edges inside a 2-minute window, and only on a clock it trusts

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-09-22 · **Last changed:** 2026-11-02
- **Boards:** all

## Context

A schedule turns the Micra on and sends it to standby: one "on" window per
day, daily or per weekday, with a warm-up lead so the machine is at
temperature by the set time ([the request](#sources)). The hazards are all
about firing wrongly: a machine switched on at 06:30 while nobody is home,
a standby that cuts a shot short, a stale trigger replayed after a reboot,
or a clock that is plausibly wrong ([the RTC boards](#alternatives-and-dead-ends)).
The page design and auto-standby's behaviour are the ux area's record.

## Decision

`core::ScheduleEngine` (`include/core/schedule.h`, `src/core/schedule.cpp`)
is pure domain code owned by `ui::App`, fed from the 500 ms refresh with the
clock, the link and power state, `BrewSnapshot::phase`, a `busy` flag
(cleaning, or the drive line held with no shot phase) and the tick. Its
rules are deliberate decisions, not to drift:

| rule | detail |
|---|---|
| edges, not periods | a time is a one-shot event; the engine never holds the machine in a state ([why](#alternatives-and-dead-ends)) |
| fire window | fires only if the link is Connected inside `kScheduleFireWindowMin` = 2 min of its time, else dropped; no catch-up ([why](#alternatives-and-dead-ends)) |
| latch | each slot latches by minute-of-week and fires once; an edit pre-latches anything already in-window, so dragging a time across "now" never switches the machine |
| no-op "on" | "on" when already on does nothing |
| deferred "off" | during a shot, its review, a cleaning cycle or a held drive line ([since](#history)), standby waits until everything has been idle for `kStandbyGraceMs` = 2 min (covers the auto-flush), then fires if still on; new activity restarts the grace |
| trusted time | nothing fires unless WiFi and NTP are enabled, a real SNTP sync landed this boot (`ntp_seconds_since_sync() >= 0`) and the date is real ([why](#alternatives-and-dead-ends)) |
| wrap | the warm-up lead may cross into the previous evening (minute-of-week arithmetic) |
| brewing source | `MachineSnapshot::brewing` is never set by the link; the engine reads `ShotPhase` |

A trigger in its window that cannot fire notes why once ("due but waiting:
Micra not connected" / "clock not trusted"); a window that closes unfired
logs `schedule: ... MISSED`. The Micra's reconnects are not parked under the
screensaver (portal only), so a link lost overnight is back before the
morning window ([the cold morning](#ev-cold-morning); [why not keep the
park](#alternatives-and-dead-ends)).

## Consequences

- The warm-up default is 8 min ([from bench tests](#history)).
- Auto-standby lives in the same engine and needs no clock; its rules are
  the ux record's ([when it arrived and changed](#history)).
- The page disables the schedule controls until a Micra is paired and the
  time is trusted (auto-standby needs only the pairing); the manual carries
  the reason.
- `make test-schedule` must stay green ([cost and test](#evidence)).

---

## Details

### Evidence

- `make test-schedule` (`tools/schedule_test.cpp`, plain g++) covers the
  rules above and must stay green.
- Cost at introduction: ~80 B internal .bss for the engine and working
  config; no task, no heap, no stack change. The MISSED logging added two
  int16 slots (4 B).

<a id="ev-cold-morning"></a>
- 2026-09-30: a cold machine on a scheduled morning, the update notice
  waiting behind the screensaver. The ring had been lost to a beta install,
  so the cause is unproven; the one mechanism that fit exactly was the
  saver park (v0.11.2) holding both links' reconnects, so a Micra link
  dropped overnight stayed down until the first touch and the 06:30 trigger
  fell outside its window silently.

### Alternatives and dead ends

- A held "on period" that re-asserts the state: rejected at design; a user
  switching the machine by hand or from the cloud app must win.
- Catch-up firing after the window: rejected, a late "on" is the scenario
  we least want.
- Trusting any plausible system time: the RTC boards defeat it, booting
  with a coin-cell time that looks fine and may not be (the same false
  positive broke the update check's network gate on 2026-09-09).
- Keeping the Micra parked under the saver: a connected Micra costs no more
  RAM asleep than awake, and the reconnect churn exists only while the
  machine is unreachable, which the update check's heap gate already covers.

### History

- 2026-09-22: engine and page shipped (`194dd92`, issue #3), v1.0.0-beta.1;
  the warm-up default moved from 6 to 8 min after bench tests.
- 2026-09-23: auto-standby added to the same engine (its rules are the ux
  record's).
- 2026-09-30: MISSED logging; Micra reconnects kept alive under the saver
  (`1bcca5e`), v1.0.0-beta.7.
- 2026-11-02: auto-standby reworked to an inactivity count; the `busy`
  input: cleaning and a held drive line count as activity for auto-standby
  and restart a deferred standby's grace (`7a9cdf7`, `8c2a918`).

### Sources

Commits `194dd92`, `add289d`, `1bcca5e`, `7a9cdf7`, `8c2a918`; issue #3
(the request for the schedule; closed 2026-10-10); `include/core/schedule.h`,
`src/core/schedule.cpp`; `tools/schedule_test.cpp`; CLAUDE.md "Scheduled
on/standby".
