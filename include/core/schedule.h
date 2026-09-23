#pragma once

#include <cstdint>

#include "core/brew.h"
#include "core/clock.h"
#include "core/machine.h"

// Scheduled on / standby for the machine: one "on" period per day, either the
// same every day or per weekday, with an optional warm-up lead that starts the
// machine early so it is up to temperature at the scheduled time.
//
// Pure domain logic — like every core header it stays free of LVGL, Arduino and
// NimBLE. The engine owns no clock and no transport: the caller feeds it a
// WallTime + the machine/shot state each poll and acts on what it returns
// (the same shape as core::ReadyChime).
//
// Semantics, settled with the owner (issue #3):
//   - Times are ONE-SHOT triggers, never a held period. The "on" trigger turns
//     the machine on if it is in standby; the "off" trigger sends it to standby
//     if it is on. Turning it on or off by hand in between is the user's
//     business and is never undone.
//   - A trigger fires only if the link is Connected at that minute. A short
//     window (kScheduleFireWindowMin) covers the caller's poll cadence; a
//     trigger missed beyond the window is simply gone — no catch-up, ever.
//   - Standby never lands on a running shot or one still under review: the
//     trigger is deferred until the shot machinery has been idle for
//     kStandbyGraceMs, then fires if the machine is still on.
//   - Nothing fires unless the caller vouches for the clock (time_trusted:
//     NTP configured + a real sync landed this boot) and the date is real.
//
// Auto-standby (owner's spec, 2026-09-23) rides along in the same engine so
// its one interaction with the schedule is in one place:
//   - It is a COUNTDOWN FROM THE LAST SHOT, never from turn-on: the end of a
//     shot arms it (and re-arms it — every later shot restarts the count), so
//     a machine the schedule switched on and nobody used stays on until the
//     scheduled off. The same holds for a hand turn-on with no shot (the
//     literal reading of "the paddle flip arms it"; revisit if unwanted).
//   - A scheduled "on" disarms it, even when the machine is already on: the
//     schedule says stay up until used.
//   - It fires only while connected, powered on and the shot machinery is
//     idle; a shot straddling the deadline just restarts the count when it
//     ends. Standby by any hand (user, schedule, this) disarms it.
//   - It needs no clock and ignores the schedule's master switch: only the
//     monotonic tick and a paired Micra. The scheduled off then finds the
//     machine already in standby and is a no-op.

namespace core {

constexpr int kMinutesPerDay = 24 * 60;
constexpr int kMinutesPerWeek = 7 * kMinutesPerDay;
constexpr int kScheduleStepMin = 5;  // the UI grid: 288 slots per day
constexpr int kScheduleSlotsPerDay = kMinutesPerDay / kScheduleStepMin;
constexpr int kScheduleMaxMin = kMinutesPerDay - kScheduleStepMin;  // 23:55
constexpr int kWarmupDefaultMin = 8;   // 6 at first; 8 after bench testing (owner, 2026-09-22)
constexpr int kWarmupMaxMin = 30;
// trigger <= now < trigger + window. Two minutes: the caller polls at 500 ms,
// so one minute would do, but a stalled loop (an OTA check, a card write)
// must not cost the morning turn-on.
constexpr int kScheduleFireWindowMin = 2;
// A deferred standby waits for this much idle time after a shot / its review
// ends. Long enough to cover the auto-flush, which can run up to ~60 s after
// the review starts, plus the drip and the cup coming off.
constexpr uint32_t kStandbyGraceMs = 2u * 60u * 1000u;
constexpr int kScheduleDefaultOnMin = 6 * 60 + 30;   // 06:30
constexpr int kAutoStandbyDefaultMin = 30;  // the Micra's own default
constexpr int kAutoStandbyMinMin = 10;
constexpr int kAutoStandbyMaxMin = 240;     // fits the uint8_t
constexpr int kAutoStandbyStepMin = 10;
constexpr int kScheduleDefaultOffMin = 9 * 60;       // 09:00

// One day's window. Minutes of the day on the 5-minute grid; on_min < off_min
// always (a window never crosses midnight — the warm-up lead may, see below).
struct DaySchedule {
  uint16_t on_min = kScheduleDefaultOnMin;
  uint16_t off_min = kScheduleDefaultOffMin;
  bool enabled = true;  // this weekday takes part (always true for `daily`)
};

inline bool operator==(const DaySchedule& a, const DaySchedule& b) {
  return a.on_min == b.on_min && a.off_min == b.off_min && a.enabled == b.enabled;
}
inline bool operator!=(const DaySchedule& a, const DaySchedule& b) { return !(a == b); }

// The whole setting, as the UI edits it and the settings port persists it.
struct ScheduleConfig {
  bool enabled = false;         // master switch
  bool same_every_day = true;   // true: `daily` applies to every weekday
  bool warmup_enabled = true;
  uint8_t warmup_min = kWarmupDefaultMin;
  bool auto_standby_enabled = false;  // standby N min after the last shot
  uint8_t auto_standby_min = kAutoStandbyDefaultMin;
  DaySchedule daily{};
  DaySchedule days[7]{};        // 0 = Monday .. 6 = Sunday

  const DaySchedule& day(int weekday) const {
    return same_every_day ? daily : days[weekday];
  }
  DaySchedule& day(int weekday) { return same_every_day ? daily : days[weekday]; }
  int warmup() const { return warmup_enabled ? warmup_min : 0; }
};

bool operator==(const ScheduleConfig& a, const ScheduleConfig& b);
inline bool operator!=(const ScheduleConfig& a, const ScheduleConfig& b) {
  return !(a == b);
}

// Day of week for a calendar date, 0 = Monday .. 6 = Sunday (Sakamoto).
// WallTime carries no weekday, so it is derived here rather than widening the
// clock port on every platform.
int weekday_iso0(int year, int month, int day);

// weekday * 1440 + hour * 60 + minute. The caller has checked valid && date_valid.
int minute_of_week(const WallTime& t);

// Snap to the grid and enforce 0 <= on < off <= 23:55. Returns true if anything
// changed. Every value that comes from storage or the UI passes through here,
// so a corrupt key can never index anything out of range.
bool sanitize_day(DaySchedule& d);

// Clamp to [kAutoStandbyMinMin, kAutoStandbyMaxMin] on the 10-minute grid.
int sanitize_auto_standby_min(int minutes);

// NVS packing (one i32 per day): bits 0-10 on_min, 11-21 off_min, 22 enabled.
// unpack_day() sanitizes, so garbage degrades to a legal window.
int32_t pack_day(const DaySchedule& d);
DaySchedule unpack_day(int32_t v);

// What the engine needs to know each poll.
struct ScheduleInputs {
  WallTime now;         // clock->now()
  bool time_trusted;    // NTP configured and a real sync has landed this boot
  Link link;
  Power power;
  ShotPhase phase;      // BrewSnapshot::phase (MachineSnapshot::brewing is never set)
  uint32_t now_ms;      // monotonic; only the standby grace timer uses it
};

// AutoStandby is a Standby too; it is its own value so the caller can say why.
enum class ScheduleAction : uint8_t { None, TurnOn, Standby, AutoStandby };

class ScheduleEngine {
 public:
  // Adopt a configuration. A trigger already inside its window right now is
  // latched WITHOUT firing, so dragging a time across "now" never switches the
  // machine. An unchanged config keeps the engine's state (the UI rebuilds on
  // theme changes and must not reopen a latch).
  void set_config(const ScheduleConfig& c, const ScheduleInputs& in);
  const ScheduleConfig& config() const { return cfg_; }

  // Feed one poll. Returns the action to take this poll (at most one).
  ScheduleAction tick(const ScheduleInputs& in);

  // An off trigger is waiting for the shot machinery to go quiet.
  bool standby_deferred() const { return defer_; }

  // Auto-standby is counting down from the last shot.
  bool auto_standby_armed() const { return asb_armed_; }

  // Minute-of-week slots the engine acts on for `weekday` (for tests/logs).
  int on_trigger(int weekday) const;
  int off_trigger(int weekday) const;

  // The weekday whose window the last TurnOn belonged to (0 = Monday), or -1.
  // With a warm-up lead that crosses midnight this is TOMORROW's day, which
  // is why the caller asks rather than looking at the clock.
  int fired_on_weekday() const { return fired_on_weekday_; }

 private:
  static bool in_window(int now_mow, int trigger_mow);
  void clear_latches();
  ScheduleAction tick_auto_standby(const ScheduleInputs& in);

  ScheduleConfig cfg_{};
  bool loaded_ = false;
  int16_t on_latched_ = -1;   // minute-of-week slot already consumed, -1 none
  int16_t off_latched_ = -1;
  bool defer_ = false;         // an off trigger is waiting for kIdle + grace
  bool idle_timing_ = false;   // the grace timer is running
  uint32_t idle_since_ms_ = 0;
  int8_t fired_on_weekday_ = -1;
  // Auto-standby: armed by a shot's end, timed from it (so a minutes edit
  // while armed simply moves the deadline).
  bool asb_armed_ = false;
  bool asb_shot_seen_ = false;   // the previous poll had a shot in flight
  uint32_t asb_since_ms_ = 0;    // when the last shot ended
};

// The settings port: where the configuration lives (NVS on the device, a
// fake in the sim) and the "don't show again" flag of the cloud-app warning.
class ISchedule {
 public:
  virtual ~ISchedule() = default;
  virtual ScheduleConfig config() const = 0;
  virtual void set_config(const ScheduleConfig& c) = 0;
  virtual bool cloud_warning_dismissed() const = 0;
  virtual void set_cloud_warning_dismissed(bool on) = 0;
};

}  // namespace core
