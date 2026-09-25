#include "core/schedule.h"

namespace core {

bool operator==(const ScheduleConfig& a, const ScheduleConfig& b) {
  if (a.enabled != b.enabled || a.same_every_day != b.same_every_day ||
      a.warmup_enabled != b.warmup_enabled || a.warmup_min != b.warmup_min ||
      a.auto_standby_enabled != b.auto_standby_enabled ||
      a.auto_standby_min != b.auto_standby_min || a.daily != b.daily) {
    return false;
  }
  for (int i = 0; i < 7; ++i) {
    if (a.days[i] != b.days[i]) return false;
  }
  return true;
}

int weekday_iso0(int year, int month, int day) {
  // Sakamoto: 0 = Sunday .. 6 = Saturday, then rotated so Monday is 0.
  static const int kOffset[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 1 || month > 12) return 0;
  if (month < 3) --year;
  const int sunday0 =
      (year + year / 4 - year / 100 + year / 400 + kOffset[month - 1] + day) % 7;
  return (sunday0 + 6) % 7;
}

int minute_of_week(const WallTime& t) {
  return weekday_iso0(t.year, t.month, t.day) * kMinutesPerDay + t.hour * 60 + t.minute;
}

namespace {
int snap(int minutes) {
  if (minutes < 0) minutes = 0;
  if (minutes > kScheduleMaxMin) minutes = kScheduleMaxMin;
  return (minutes / kScheduleStepMin) * kScheduleStepMin;
}
}  // namespace

int sanitize_auto_standby_min(int minutes) {
  if (minutes < kAutoStandbyMinMin) minutes = kAutoStandbyMinMin;
  if (minutes > kAutoStandbyMaxMin) minutes = kAutoStandbyMaxMin;
  return (minutes / kAutoStandbyStepMin) * kAutoStandbyStepMin;
}

bool sanitize_day(DaySchedule& d) {
  const DaySchedule was = d;
  int on = snap(d.on_min);
  int off = snap(d.off_min);
  if (off <= on) {
    // Push the off time past the on time; if the on time sits at the top of
    // the day, pull it back instead so the window stays inside the day.
    off = on + kScheduleStepMin;
    if (off > kScheduleMaxMin) {
      off = kScheduleMaxMin;
      on = off - kScheduleStepMin;
    }
  }
  d.on_min = static_cast<uint16_t>(on);
  d.off_min = static_cast<uint16_t>(off);
  return d != was;
}

int32_t pack_day(const DaySchedule& d) {
  return static_cast<int32_t>(d.on_min & 0x7FF) |
         (static_cast<int32_t>(d.off_min & 0x7FF) << 11) |
         (d.enabled ? (int32_t{1} << 22) : 0);
}

DaySchedule unpack_day(int32_t v) {
  DaySchedule d;
  d.on_min = static_cast<uint16_t>(v & 0x7FF);
  d.off_min = static_cast<uint16_t>((v >> 11) & 0x7FF);
  d.enabled = (v >> 22) & 1;
  sanitize_day(d);
  return d;
}

// --- engine ------------------------------------------------------------------

int ScheduleEngine::on_trigger(int weekday) const {
  const int t = weekday * kMinutesPerDay + cfg_.day(weekday).on_min - cfg_.warmup();
  return ((t % kMinutesPerWeek) + kMinutesPerWeek) % kMinutesPerWeek;  // may wrap into the previous evening
}

int ScheduleEngine::off_trigger(int weekday) const {
  return weekday * kMinutesPerDay + cfg_.day(weekday).off_min;
}

bool ScheduleEngine::in_window(int now_mow, int trigger_mow) {
  const int since = ((now_mow - trigger_mow) % kMinutesPerWeek + kMinutesPerWeek) %
                    kMinutesPerWeek;
  return since < kScheduleFireWindowMin;
}

void ScheduleEngine::clear_latches() {
  on_latched_ = -1;
  off_latched_ = -1;
  defer_ = false;
  idle_timing_ = false;
}

void ScheduleEngine::set_config(const ScheduleConfig& c, const ScheduleInputs& in) {
  ScheduleConfig clean = c;
  sanitize_day(clean.daily);
  for (DaySchedule& d : clean.days) sanitize_day(d);
  if (clean.warmup_min > kWarmupMaxMin) clean.warmup_min = kWarmupMaxMin;
  clean.auto_standby_min = static_cast<uint8_t>(sanitize_auto_standby_min(clean.auto_standby_min));
  if (loaded_ && clean == cfg_) return;  // a rebuild, not an edit: keep the latches
  cfg_ = clean;
  loaded_ = true;
  clear_latches();
  // Switching auto-standby off drops a running count; a minutes edit keeps it
  // (the deadline is computed from the shot's end each poll).
  if (!cfg_.auto_standby_enabled) asb_armed_ = false;
  // Pre-latch anything already inside its window: the user just moved a time
  // onto (or the warm-up lead across) the current minute, and an edit must
  // never switch the machine.
  if (!cfg_.enabled || !in.now.valid || !in.now.date_valid) return;
  const int now_mow = minute_of_week(in.now);
  for (int w = 0; w < 7; ++w) {
    if (!cfg_.day(w).enabled) continue;
    const int on = on_trigger(w);
    const int off = off_trigger(w);
    if (in_window(now_mow, on)) on_latched_ = static_cast<int16_t>(on);
    if (in_window(now_mow, off)) off_latched_ = static_cast<int16_t>(off);
  }
}

// Auto-standby: arm on a shot's end, fire when the count runs out. Needs no
// clock and no master switch — see the header.
ScheduleAction ScheduleEngine::tick_auto_standby(const ScheduleInputs& in) {
  const bool shot = in.phase == ShotPhase::kBrewing || in.phase == ShotPhase::kSettling;
  const bool ended = asb_shot_seen_ && !shot;
  asb_shot_seen_ = shot;
  if (!cfg_.auto_standby_enabled) {
    asb_armed_ = false;
    return ScheduleAction::None;
  }
  if (ended) {  // every shot (re)starts the count
    asb_armed_ = true;
    asb_since_ms_ = in.now_ms;
  }
  if (!asb_armed_) return ScheduleAction::None;
  if (in.power != Power::On) {  // standby by anyone: nothing left to do
    asb_armed_ = false;
    return ScheduleAction::None;
  }
  const uint32_t wait_ms = static_cast<uint32_t>(cfg_.auto_standby_min) * 60000u;
  if (in.now_ms - asb_since_ms_ < wait_ms) return ScheduleAction::None;
  // Due. A shot in flight (or its review) restarts the count when it ends;
  // a dropped link just waits — an unused machine is still unused.
  if (shot || in.phase != ShotPhase::kIdle || in.link != Link::Connected)
    return ScheduleAction::None;
  asb_armed_ = false;
  return ScheduleAction::AutoStandby;
}

uint32_t ScheduleEngine::auto_standby_remaining_ms(uint32_t now_ms) const {
  if (!asb_armed_) return 0;
  const uint32_t wait_ms = static_cast<uint32_t>(cfg_.auto_standby_min) * 60000u;
  const uint32_t since = now_ms - asb_since_ms_;
  return since >= wait_ms ? 0 : wait_ms - since;
}

ScheduleAction ScheduleEngine::tick(const ScheduleInputs& in) {
  const ScheduleAction asb = tick_auto_standby(in);
  if (!cfg_.enabled || !in.time_trusted || !in.now.valid || !in.now.date_valid) {
    defer_ = false;  // latches stay: an NTP blip must not re-fire a consumed slot
    idle_timing_ = false;
    return asb;
  }
  const int now_mow = minute_of_week(in.now);

  // A consumed slot is forgotten once its window has passed, so the same
  // minute fires again next week.
  if (on_latched_ >= 0 && !in_window(now_mow, on_latched_)) on_latched_ = -1;
  if (off_latched_ >= 0 && !in_window(now_mow, off_latched_)) off_latched_ = -1;

  // A standby held back by a shot: wait for the machinery to go quiet, then
  // act — unless the user (or the link) got there first.
  if (defer_) {
    if (in.link != Link::Connected || in.power != Power::On) {
      defer_ = false;
      idle_timing_ = false;
    } else if (in.phase != ShotPhase::kIdle) {
      idle_timing_ = false;  // a new shot restarts the grace
    } else if (!idle_timing_) {
      idle_timing_ = true;
      idle_since_ms_ = in.now_ms;
    } else if (in.now_ms - idle_since_ms_ >= kStandbyGraceMs) {
      defer_ = false;
      idle_timing_ = false;
      return ScheduleAction::Standby;
    }
  }

  const bool connected = in.link == Link::Connected;
  for (int w = 0; w < 7; ++w) {
    if (!cfg_.day(w).enabled) continue;

    const int on = on_trigger(w);
    if (in_window(now_mow, on) && on_latched_ != on) {
      // Not connected: leave the slot open — a link that comes up inside the
      // window still fires; past the window the slot just passes.
      if (connected) {
        on_latched_ = static_cast<int16_t>(on);
        defer_ = false;  // an "on" inside a pending standby wins
        idle_timing_ = false;
        asb_armed_ = false;  // ...and so does the schedule over an auto-standby count
        fired_on_weekday_ = static_cast<int8_t>(w);
        if (in.power != Power::On) return ScheduleAction::TurnOn;
      }
    }

    const int off = off_trigger(w);
    if (in_window(now_mow, off) && off_latched_ != off) {
      if (connected) {
        off_latched_ = static_cast<int16_t>(off);
        if (in.power == Power::On) {
          if (in.phase == ShotPhase::kIdle) return ScheduleAction::Standby;
          defer_ = true;  // mid-shot / in review: wait for quiet
          idle_timing_ = false;
        }
      }
    }
  }
  return asb;
}

}  // namespace core
