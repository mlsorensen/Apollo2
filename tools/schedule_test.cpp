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

int main() {
  if (test_helpers() || test_triggers() || test_deferred_standby()) return 1;
  std::printf("schedule: %d checks passed\n", checks);
  return 0;
}
