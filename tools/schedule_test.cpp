// Host check for core::ScheduleEngine — plain asserts, no framework.
//   make test-schedule
// Builds with g++ against src/core/schedule.cpp only (the core headers are
// LVGL/Arduino-free by rule), so it runs in under a second.

#include <cassert>
#include <cstdio>

#include "core/schedule.h"

using namespace core;

namespace {

WallTime at(int year, int month, int day, int hour, int minute) {
  return WallTime{true, hour, minute, true, year, month, day};
}

// 2026-09-21 is a Monday; 2026-09-27 a Sunday.
constexpr int kMon = 21, kSun = 27;

ScheduleInputs in(const WallTime& t, Link link = Link::Connected, Power power = Power::Standby,
                  ShotPhase phase = ShotPhase::kIdle, uint32_t ms = 1000, bool trusted = true) {
  return ScheduleInputs{t, trusted, link, power, phase, ms};
}

ScheduleConfig daily(int on, int off, bool warm = false, int warm_min = 6) {
  ScheduleConfig c;
  c.enabled = true;
  c.same_every_day = true;
  c.warmup_enabled = warm;
  c.warmup_min = static_cast<uint8_t>(warm_min);
  c.daily = DaySchedule{static_cast<uint16_t>(on), static_cast<uint16_t>(off), true};
  return c;
}

int checks = 0;
#define CHECK(x)                                                    \
  do {                                                              \
    ++checks;                                                       \
    if (!(x)) {                                                     \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
      return 1;                                                     \
    }                                                               \
  } while (0)

int test_helpers() {
  CHECK(weekday_iso0(2026, 9, 22) == 1);   // Tuesday
  CHECK(weekday_iso0(2026, 9, 21) == 0);   // Monday
  CHECK(weekday_iso0(2026, 9, 27) == 6);   // Sunday
  CHECK(weekday_iso0(2026, 1, 1) == 3);    // Thursday (Jan: the year-1 branch)
  CHECK(weekday_iso0(2024, 2, 29) == 3);   // leap day, Thursday
  CHECK(minute_of_week(at(2026, 9, 22, 6, 30)) == 1 * 1440 + 390);

  DaySchedule d{390, 540, true};
  CHECK(unpack_day(pack_day(d)) == d);
  DaySchedule off{1430, 1435, false};
  CHECK(unpack_day(pack_day(off)) == off);
  // Garbage sanitizes to a legal window rather than anything out of range.
  const DaySchedule g = unpack_day(0x7FFFFFFF);
  CHECK(g.on_min < g.off_min && g.off_min <= kScheduleMaxMin && g.on_min % 5 == 0);
  DaySchedule cross{600, 600, true};
  CHECK(sanitize_day(cross) && cross.on_min == 600 && cross.off_min == 605);
  DaySchedule top{1435, 1435, true};
  CHECK(sanitize_day(top) && top.on_min == 1430 && top.off_min == 1435);
  DaySchedule grid{393, 547, true};
  CHECK(sanitize_day(grid) && grid.on_min == 390 && grid.off_min == 545);
  return 0;
}

int test_triggers() {
  ScheduleEngine e;
  const WallTime quiet = at(2026, 9, kMon, 3, 0);
  // Warm-up wraps into the previous evening: Mon 00:03 - 6 min = Sun 23:57.
  ScheduleConfig c = daily(0, 60, true, 6);
  c.daily.on_min = 5;  // 00:05 - 6 = Sun 23:59
  e.set_config(c, in(quiet));
  CHECK(e.on_trigger(0) == 6 * 1440 + 1439);
  CHECK(e.off_trigger(0) == 60);

  // Fires once inside the window, not before, not after.
  e.set_config(daily(390, 540), in(quiet));
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 29))) == ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 30))) == ScheduleAction::TurnOn);
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 30))) == ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 31))) == ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 32))) == ScheduleAction::None);  // window closed
  // ...and again next week (the latch cleared with the window).
  CHECK(e.tick(in(at(2026, 9, kMon + 7, 6, 30))) == ScheduleAction::TurnOn);

  // Not trusted / no date / disabled: nothing.
  e.set_config(daily(390, 540), in(quiet));
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 30), Link::Connected, Power::Standby, ShotPhase::kIdle,
                  1000, /*trusted=*/false)) == ScheduleAction::None);
  WallTime nodate = at(2026, 9, kMon, 6, 30);
  nodate.date_valid = false;
  CHECK(e.tick(in(nodate)) == ScheduleAction::None);
  ScheduleConfig off = daily(390, 540);
  off.enabled = false;
  e.set_config(off, in(quiet));
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 30))) == ScheduleAction::None);

  // Disconnected at the slot: no latch; a link inside the window still fires.
  e.set_config(daily(390, 540), in(quiet));
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 30), Link::Disconnected)) == ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 31), Link::Connected)) == ScheduleAction::TurnOn);
  // Past the window: gone for good.
  e.set_config(daily(390, 540), in(quiet));
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 30), Link::Disconnected)) == ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 32), Link::Connected)) == ScheduleAction::None);

  // Already on: a no-op that still consumes the slot.
  e.set_config(daily(390, 540), in(quiet));
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 30), Link::Connected, Power::On)) == ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 31), Link::Connected, Power::Standby)) ==
        ScheduleAction::None);

  // Off: plain standby when idle; already in standby = no-op.
  e.set_config(daily(390, 540), in(quiet));
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 0), Link::Connected, Power::On)) == ScheduleAction::Standby);
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 0), Link::Connected, Power::On)) == ScheduleAction::None);
  e.set_config(daily(390, 540), in(quiet));
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 0), Link::Connected, Power::Standby)) == ScheduleAction::None);

  // set_config with the slot in-window pre-latches: an edit never fires.
  e.set_config(daily(390, 540), in(at(2026, 9, kMon, 9, 0), Link::Connected, Power::On));
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 0), Link::Connected, Power::On)) == ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 1), Link::Connected, Power::On)) == ScheduleAction::None);
  // An unchanged config keeps the latch (a theme rebuild is not an edit).
  e.set_config(daily(390, 540), in(at(2026, 9, kMon, 9, 1), Link::Connected, Power::On));
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 1), Link::Connected, Power::On)) == ScheduleAction::None);

  // Per-day: a disabled weekday is skipped, others use their own times.
  ScheduleConfig p = daily(390, 540);
  p.same_every_day = false;
  for (DaySchedule& d : p.days) d = DaySchedule{480, 660, true};  // 08:00-11:00
  p.days[0].enabled = false;                                       // Monday off
  p.days[6] = DaySchedule{540, 600, true};                          // Sunday 09:00-10:00
  e.set_config(p, in(quiet));
  CHECK(e.tick(in(at(2026, 9, kMon, 8, 0))) == ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 30))) == ScheduleAction::None);  // daily template ignored
  CHECK(e.tick(in(at(2026, 9, kMon + 1, 8, 0))) == ScheduleAction::TurnOn);
  CHECK(e.tick(in(at(2026, 9, kSun, 8, 0))) == ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kSun, 9, 0))) == ScheduleAction::TurnOn);

  // Warm-up lead: fires early, and the wrapped Sunday-evening slot belongs to Monday.
  e.set_config(daily(390, 540, true, 6), in(quiet));
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 23))) == ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 6, 24))) == ScheduleAction::TurnOn);
  ScheduleConfig w = daily(0, 60, true, 6);
  e.set_config(w, in(quiet));
  CHECK(e.tick(in(at(2026, 9, kSun, 23, 54))) == ScheduleAction::TurnOn);
  CHECK(e.fired_on_weekday() == 0);  // Monday's window, fired Sunday evening
  return 0;
}

int test_deferred_standby() {
  ScheduleEngine e;
  const WallTime quiet = at(2026, 9, kMon, 3, 0);
  e.set_config(daily(390, 540), in(quiet));
  uint32_t ms = 10000;
  // Off lands mid-shot: deferred.
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 0), Link::Connected, Power::On, ShotPhase::kBrewing, ms)) ==
        ScheduleAction::None);
  CHECK(e.standby_deferred());
  // Review: still waiting. Idle: the grace starts.
  ms += 30000;
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 1), Link::Connected, Power::On, ShotPhase::kReview, ms)) ==
        ScheduleAction::None);
  ms += 30000;
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 2), Link::Connected, Power::On, ShotPhase::kIdle, ms)) ==
        ScheduleAction::None);
  ms += kStandbyGraceMs - 1000;
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 4), Link::Connected, Power::On, ShotPhase::kIdle, ms)) ==
        ScheduleAction::None);
  // A new shot inside the grace restarts it.
  ms += 1000;
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 4), Link::Connected, Power::On, ShotPhase::kBrewing, ms)) ==
        ScheduleAction::None);
  ms += 60000;
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 5), Link::Connected, Power::On, ShotPhase::kIdle, ms)) ==
        ScheduleAction::None);
  ms += kStandbyGraceMs;
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 7), Link::Connected, Power::On, ShotPhase::kIdle, ms)) ==
        ScheduleAction::Standby);
  CHECK(!e.standby_deferred());
  // No second standby.
  ms += 1000;
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 7), Link::Connected, Power::On, ShotPhase::kIdle, ms)) ==
        ScheduleAction::None);

  // A manual standby cancels the deferral.
  e.set_config(daily(390, 540), in(quiet));
  ms = 10000;
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 0), Link::Connected, Power::On, ShotPhase::kBrewing, ms)) ==
        ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 1), Link::Connected, Power::Standby, ShotPhase::kIdle, ms)) ==
        ScheduleAction::None);
  CHECK(!e.standby_deferred());
  ms += 10 * 60 * 1000;
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 11), Link::Connected, Power::On, ShotPhase::kIdle, ms)) ==
        ScheduleAction::None);

  // Losing the clock drops the deferral but keeps the consumed slot.
  e.set_config(daily(390, 540), in(quiet));
  ms = 10000;
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 0), Link::Connected, Power::On, ShotPhase::kReview, ms)) ==
        ScheduleAction::None);
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 1), Link::Connected, Power::On, ShotPhase::kIdle, ms, false)) ==
        ScheduleAction::None);
  CHECK(!e.standby_deferred());
  CHECK(e.tick(in(at(2026, 9, kMon, 9, 1), Link::Connected, Power::On, ShotPhase::kIdle, ms)) ==
        ScheduleAction::None);
  return 0;
}

}  // namespace

// Auto-standby: a countdown from the last shot, schedule-aware.
int test_auto_standby() {
  const uint32_t kMin = 60000u;
  // Master switch OFF and no trusted clock: auto-standby still works.
  ScheduleConfig c = daily(390, 540);
  c.enabled = false;
  c.auto_standby_enabled = true;
  c.auto_standby_min = 30;
  const WallTime t = at(2026, 9, kMon, 7, 0);
  auto poll = [&](Power p, ShotPhase ph, uint32_t ms, Link l = Link::Connected) {
    return in(t, l, p, ph, ms, /*trusted=*/false);
  };
  ScheduleEngine e;
  e.set_config(c, poll(Power::On, ShotPhase::kIdle, 1000));
  // On by hand, never used: nothing arms.
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, 1000)) == ScheduleAction::None);
  CHECK(!e.auto_standby_armed());
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, 1000 + 120 * kMin)) == ScheduleAction::None);
  // A shot: brewing, settling, review, idle. Its END (settling -> review) arms.
  uint32_t ms = 200 * kMin;
  CHECK(e.tick(poll(Power::On, ShotPhase::kBrewing, ms)) == ScheduleAction::None);
  CHECK(e.tick(poll(Power::On, ShotPhase::kSettling, ms + 30000)) == ScheduleAction::None);
  CHECK(!e.auto_standby_armed());
  const uint32_t end = ms + 40000;
  CHECK(e.tick(poll(Power::On, ShotPhase::kReview, end)) == ScheduleAction::None);
  CHECK(e.auto_standby_armed());
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, end + 29 * kMin)) == ScheduleAction::None);
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, end + 30 * kMin)) == ScheduleAction::AutoStandby);
  CHECK(!e.auto_standby_armed());
  CHECK(e.tick(poll(Power::Standby, ShotPhase::kIdle, end + 31 * kMin)) == ScheduleAction::None);

  // A second shot restarts the count; a shot straddling the deadline waits.
  ms = end + 60 * kMin;
  e.tick(poll(Power::On, ShotPhase::kBrewing, ms));
  e.tick(poll(Power::On, ShotPhase::kIdle, ms + 40000));        // shot 1 ends
  e.tick(poll(Power::On, ShotPhase::kBrewing, ms + 20 * kMin)); // shot 2 starts
  e.tick(poll(Power::On, ShotPhase::kIdle, ms + 20 * kMin + 40000));  // ...ends
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, ms + 40 * kMin)) == ScheduleAction::None);  // shot 1 + 30 passed
  CHECK(e.tick(poll(Power::On, ShotPhase::kBrewing, ms + 50 * kMin)) == ScheduleAction::None);  // due, but brewing
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, ms + 51 * kMin)) == ScheduleAction::None);  // restarted
  CHECK(e.auto_standby_armed());
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, ms + 81 * kMin)) == ScheduleAction::AutoStandby);

  // Due while the link is down: fires when it is back and the machine is still on.
  ms += 200 * kMin;
  e.tick(poll(Power::On, ShotPhase::kBrewing, ms));
  e.tick(poll(Power::On, ShotPhase::kIdle, ms + 40000));
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, ms + 40 * kMin, Link::Disconnected)) == ScheduleAction::None);
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, ms + 41 * kMin)) == ScheduleAction::AutoStandby);

  // Standby by hand disarms.
  ms += 200 * kMin;
  e.tick(poll(Power::On, ShotPhase::kBrewing, ms));
  e.tick(poll(Power::On, ShotPhase::kIdle, ms + 40000));
  CHECK(e.auto_standby_armed());
  CHECK(e.tick(poll(Power::Standby, ShotPhase::kIdle, ms + 5 * kMin)) == ScheduleAction::None);
  CHECK(!e.auto_standby_armed());
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, ms + 60 * kMin)) == ScheduleAction::None);

  // A minutes edit while armed moves the deadline; switching it off drops it.
  ms += 200 * kMin;
  e.tick(poll(Power::On, ShotPhase::kBrewing, ms));
  e.tick(poll(Power::On, ShotPhase::kIdle, ms + 40000));
  c.auto_standby_min = 10;
  e.set_config(c, poll(Power::On, ShotPhase::kIdle, ms + 1 * kMin));
  CHECK(e.auto_standby_armed());
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, ms + 11 * kMin)) == ScheduleAction::AutoStandby);
  e.tick(poll(Power::On, ShotPhase::kBrewing, ms + 20 * kMin));
  e.tick(poll(Power::On, ShotPhase::kIdle, ms + 21 * kMin));
  CHECK(e.auto_standby_armed());
  c.auto_standby_enabled = false;
  e.set_config(c, poll(Power::On, ShotPhase::kIdle, ms + 22 * kMin));
  CHECK(!e.auto_standby_armed());
  CHECK(e.tick(poll(Power::On, ShotPhase::kIdle, ms + 60 * kMin)) == ScheduleAction::None);
  c.auto_standby_enabled = true;
  c.auto_standby_min = 30;

  // Schedule ON, trusted clock: a scheduled "on" (even onto a machine already
  // on) disarms a running count; the machine then stays on until used.
  c.enabled = true;
  ScheduleEngine s;
  const WallTime early = at(2026, 9, kMon, 6, 0);
  s.set_config(c, in(early, Link::Connected, Power::On, ShotPhase::kIdle, 1000));
  s.tick(in(early, Link::Connected, Power::On, ShotPhase::kBrewing, 1000));
  s.tick(in(at(2026, 9, kMon, 6, 1), Link::Connected, Power::On, ShotPhase::kIdle, 1000 + kMin));
  CHECK(s.auto_standby_armed());  // due at ~06:31
  CHECK(s.tick(in(at(2026, 9, kMon, 6, 30), Link::Connected, Power::On, ShotPhase::kIdle,
                  1000 + 30 * kMin)) == ScheduleAction::None);  // on-slot, already on
  CHECK(!s.auto_standby_armed());
  CHECK(s.tick(in(at(2026, 9, kMon, 6, 32), Link::Connected, Power::On, ShotPhase::kIdle,
                  1000 + 32 * kMin)) == ScheduleAction::None);
  // Scheduled on, never used: no auto-standby; the scheduled off still lands.
  CHECK(s.tick(in(at(2026, 9, kMon, 9, 0), Link::Connected, Power::On, ShotPhase::kIdle,
                  1000 + 180 * kMin)) == ScheduleAction::Standby);
  // Off early by auto-standby inside the window: the scheduled off is a no-op.
  ScheduleEngine u;
  u.set_config(c, in(at(2026, 9, kMon + 1, 6, 0), Link::Connected, Power::Standby, ShotPhase::kIdle, 1000));
  CHECK(u.tick(in(at(2026, 9, kMon + 1, 6, 30), Link::Connected, Power::Standby, ShotPhase::kIdle, 1000)) ==
        ScheduleAction::TurnOn);
  u.tick(in(at(2026, 9, kMon + 1, 7, 0), Link::Connected, Power::On, ShotPhase::kBrewing, 1000 + 30 * kMin));
  u.tick(in(at(2026, 9, kMon + 1, 7, 1), Link::Connected, Power::On, ShotPhase::kIdle, 1000 + 31 * kMin));
  CHECK(u.tick(in(at(2026, 9, kMon + 1, 7, 31), Link::Connected, Power::On, ShotPhase::kIdle,
                  1000 + 61 * kMin)) == ScheduleAction::AutoStandby);
  CHECK(u.tick(in(at(2026, 9, kMon + 1, 9, 0), Link::Connected, Power::Standby, ShotPhase::kIdle,
                  1000 + 150 * kMin)) == ScheduleAction::None);
  // Sanitizer: grid + range.
  CHECK(sanitize_auto_standby_min(0) == 10 && sanitize_auto_standby_min(35) == 30 &&
        sanitize_auto_standby_min(999) == 240);
  return 0;
}

int main() {
  if (test_helpers() || test_triggers() || test_deferred_standby() || test_auto_standby()) return 1;
  std::printf("schedule: %d checks passed\n", checks);
  return 0;
}
