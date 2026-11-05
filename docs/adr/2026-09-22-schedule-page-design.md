# Schedule page: one switch per line, triggers not periods, auto-standby from the last use

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-09-22 · **Last changed:** 2026-11-02
- **Boards:** all; auto-standby needs a shot source (a wired paddle or a paired scale in Shot detect)

## Context

Issue #3 (opened 2026-09-22, closed 2026-10-10) asked for scheduled on and
standby. The machine's own schedule lives
in the vendor cloud app; Apollo's runs from the device clock and BLE link.
The first page (`194dd92`, 2026-09-22) mixed week-wide and per-day controls
with a status line explaining the gate, and two bench rounds on the S3 4.3C
the same day could not make it read as three groups without crowding. The
next day's auto-standby had no room left on it at 800x480.

## Decision

**Behaviour the user sees:**

- Times are TRIGGERS, not a held period. "On at" switches on once, "Off at"
  sends to standby once; between them the user does as they like. Switching
  off by hand stays off; switching on early finds "On at" a no-op.
- A trigger fires only if the Micra is Connected inside a 2-minute window
  and is otherwise skipped, never queued. Nothing fires without WiFi, NTP
  and a real sync this boot, or without a paired Micra.
- "Off" during a shot, review or cleaning waits until the device has been
  idle ~2 min (covers the auto-flush and the cup coming off).
- Auto-standby counts from the END of the last ACTIVITY (a shot in any mode,
  a flush, a backflush), never from turn-on, so a machine the schedule
  switched on and nobody used stays on until its scheduled Off. A scheduled
  On, or standby by anyone, disarms it. It needs no clock and not the master
  switch. Since 2026-11-02 the drive line being held counts too.
- With "Show auto-standby timer" on, the Home MICRA header shows "N min"
  while a count really runs; it hides during a shot, review or cleaning.

**Page design:**

- Two lv_menu pages, neither scrolls at 800x480. Page one: switches, one
  per line, NO group headings: "Auto-standby when idle", "Show auto-standby
  timer", then "Enable schedules", "Same every day", "Smart Warm-up", and a
  "Configure schedule" entry whose value label summarises the window ("Per
  day" once days differ). Auto-standby sits FIRST so it never reads as
  being under the master.
- The gate line was dropped (2026-09-25, "too much for the page"): schedule
  rows grey silently (per-widget 40 % opacity, not a container) without a
  paired Micra or a trusted clock; the manual carries the reason. One muted
  line remains for the case that matters: no shot source, so the count can
  never start.
- Page two: day chips, a "Day enabled" switch, On/Off pickers, a range
  slider with an hour axis, "Copy times to". Chips are select-only.
- First visit: a notice to turn off the cloud app's schedule (OK = this
  session, Don't show again = persisted).

| parameter | value |
|---|---|
| fire window | 2 min |
| standby grace after activity | 2 min |
| auto-standby range | 10 to 240 min, 10-min steps, default 30, off by default |
| Smart Warm-up | default 8 min (6 min on 2026-09-22 morning), 0 to 30 |
| default window | 06:30 to 09:00, 5-min steps |

## Evidence

- Host test `make test-schedule`: 61 checks at `194dd92`, 99 with
  auto-standby (`1e72e44`), 119 after the activity rework (`8c2a918`,
  2026-11-02; counted from `tools/schedule_test.cpp` at each commit).
- Bench-reviewed on the S3 4.3C (2026-09-22, two rounds, `7fe979c` and
  `0d0ebce`; 2026-09-23) and the P4-5 (2026-09-25, per the working notes).
- 2026-09-30: a cold machine on a scheduled morning, cause unproven (the log
  ring was lost); the one mechanism that fit (Micra reconnects parked under
  the saver) was removed and a MISSED line with its reason now logs.

## Alternatives and dead ends

- Tristate day chips (tap the selected chip again to switch the day off,
  `7fe979c`): reverted the next day for a plain "Day enabled" switch.
- Group headings "After last shot" / "Schedule" (`1e72e44`): dropped two
  days later, the row names carry it.
- A status line naming the missing gate: dropped as above.
- Auto-standby from turn-on: rejected in the spec; and from shots
  only (until 2026-11-02): a backflush could be cut off mid-cycle.

## Consequences

- Engine `core::ScheduleEngine`, ~80 B .bss, fed from the 500 ms refresh.
- NVS keys all ADDs (`schen`, `schsame`, `schwarm`, `schwarmm`, `schwarn`,
  `schasb`, `schasbm`, `schasbt`, `schall`, `schd0..6`).
- Screenshots: `../img/manual/micra-schedule.png`,
  `../img/manual/micra-schedule-days.png`,
  `../img/manual/schedule-warn-modal.png`,
  `../img/manual/home-standby-timer.png`.

## History

- 2026-09-22: first page and engine (`194dd92`); two feedback rounds
  (`7fe979c`, `0d0ebce`); warm-up default to 8 min (`add289d`).
- 2026-09-23: auto-standby after the last shot; page split in two
  (`1e72e44`).
- 2026-09-25: Home countdown; headings and gate line dropped (`5d397a9`).
- 2026-09-30: missed-trigger logging; saver park lifted (`1bcca5e`).
- 2026-11-02: auto-standby is an inactivity count: cleaning and a held
  drive line are activity (`7a9cdf7`, `8c2a918`).

## Sources

Issue #3; commits above; `include/core/schedule.h`; CLAUDE.md "Scheduled
on/standby"; MANUAL.md "Schedule".
