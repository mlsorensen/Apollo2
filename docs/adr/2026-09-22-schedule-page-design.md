# Schedule page: one switch per line, triggers not periods, auto-standby from the last use

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-09-22 · **Last changed:** 2026-11-02
- **Boards:** all; auto-standby needs a shot source (a wired paddle or a paired scale in Shot detect)

## Context

The machine's own schedule lives in the vendor cloud app; Apollo's runs from
the device clock and the BLE link, so it can only act while the Micra is
connected and the clock is trusted. The page has to hold week-wide and
per-day controls plus auto-standby and read cleanly at 800x480 without
scrolling ([what crowded it](#alternatives-and-dead-ends)). The engine's
rules are `2026-09-22-schedule-engine-rules.md`; this record is the page.

## Decision

**What the user sees:**

- Times are TRIGGERS, not a held period: "On at" switches on once, "Off at"
  sends to standby once; between them the user does as they like. Switching
  off by hand stays off; switching on early makes "On at" a no-op.
- A trigger fires only with the Micra Connected inside a 2-minute window,
  otherwise it is skipped, never queued; nothing fires without a paired
  Micra and a clock synced this boot ([a missed morning](#evidence)). An
  "Off" during a shot, review or cleaning waits ~2 min of idle.
- Auto-standby counts from the END of the last activity (a shot in any
  mode, a flush, a backflush, a held drive line; [since](#history)), never
  from turn-on; a scheduled On, or standby by anyone, disarms it; it needs
  neither a clock nor the master switch. "Show auto-standby timer" puts
  "N min" in the Home MICRA header while a count runs.

**The page:**

- Two lv_menu pages, neither scrolls at 800x480. Page one: one switch per
  line, NO group headings ([why](#alternatives-and-dead-ends)):
  "Auto-standby when idle", "Show auto-standby timer", "Enable schedules",
  "Same every day", "Smart Warm-up", then "Configure schedule" with the
  window as its value ("Per day" once days differ). Auto-standby sits FIRST
  so it never reads as being under the master.
- No gate line ([why](#alternatives-and-dead-ends)): rows grey silently
  (per-widget 40 % opacity, not a container) without a paired Micra or a
  trusted clock; one muted line remains when there is no shot source.
- Page two: day chips (select-only, [why](#alternatives-and-dead-ends)), a
  "Day enabled" switch, On/Off pickers, a range slider on an hour axis,
  "Copy times to".
- First visit: a notice to turn off the cloud app's schedule (OK = this
  session, Don't show again = persisted).

| parameter | value |
|---|---|
| fire window | 2 min |
| standby grace after activity | 2 min |
| auto-standby range | 10 to 240 min, 10-min steps, default 30, off by default |
| Smart Warm-up | default 8 min ([was 6](#history)), 0 to 30 |
| default window | 06:30 to 09:00, 5-min steps |

## Consequences

- Engine `core::ScheduleEngine`, ~80 B .bss, fed from the 500 ms refresh;
  `make test-schedule` covers it ([check counts](#evidence)).
- NVS keys all ADDs (`schen`, `schsame`, `schwarm`, `schwarmm`, `schwarn`,
  `schasb`, `schasbm`, `schasbt`, `schall`, `schd0..6`).
- Screenshots: `../img/manual/micra-schedule.png`,
  `../img/manual/micra-schedule-days.png`,
  `../img/manual/schedule-warn-modal.png`,
  `../img/manual/home-standby-timer.png`.

---

## Details

### Evidence

- Host test `make test-schedule`: 61 checks at `194dd92`, 99 with
  auto-standby (`1e72e44`), 119 after the activity rework (`8c2a918`,
  2026-11-02; counted from `tools/schedule_test.cpp` at each commit).
- Bench-reviewed on the S3 4.3C (2026-09-22, two rounds, `7fe979c` and
  `0d0ebce`; 2026-09-23) and the P4-5 (2026-09-25, per the working notes).
- 2026-09-30: a cold machine on a scheduled morning, cause unproven (the log
  ring was lost); the one mechanism that fit (Micra reconnects parked under
  the saver) was removed and a MISSED line with its reason now logs.
- The ~2 min idle wait on a deferred Off covers the auto-flush and the cup
  coming off; the Home countdown hides during a shot, review or cleaning
  because no count really runs then.

### Alternatives and dead ends

- The first page (`194dd92`, 2026-09-22) mixed week-wide and per-day
  controls with a status line explaining the gate; two bench rounds on the
  S3 4.3C the same day could not make it read as three groups without
  crowding, and the next day's auto-standby had no room left on it at
  800x480. Hence the split into two pages.
- Tristate day chips (tap the selected chip again to switch the day off,
  `7fe979c`): reverted the next day for a plain "Day enabled" switch.
- Group headings "After last shot" / "Schedule" (`1e72e44`): dropped two
  days later, the row names carry it.
- A status line naming the missing gate: dropped 2026-09-25, "too much for
  the page"; the manual carries the reason instead.
- Auto-standby from turn-on: rejected in the spec, a machine the schedule
  switched on and nobody used should stay on until its scheduled Off; and
  from shots only (until 2026-11-02): a backflush could be cut off
  mid-cycle.

### History

- 2026-09-22: first page and engine (`194dd92`); two feedback rounds
  (`7fe979c`, `0d0ebce`); warm-up default 6 min that morning, then 8 min
  (`add289d`).
- 2026-09-23: auto-standby after the last shot; page split in two
  (`1e72e44`).
- 2026-09-25: Home countdown; headings and gate line dropped (`5d397a9`).
- 2026-09-30: missed-trigger logging; saver park lifted (`1bcca5e`).
- 2026-10-10: issue #3 closed.
- 2026-11-02: auto-standby is an inactivity count: cleaning and a held
  drive line are activity (`7a9cdf7`, `8c2a918`).

### Sources

Issue #3 (opened 2026-09-22, closed 2026-10-10); commits above;
`include/core/schedule.h`; `tools/schedule_test.cpp`; CLAUDE.md "Scheduled
on/standby"; MANUAL.md "Schedule".
