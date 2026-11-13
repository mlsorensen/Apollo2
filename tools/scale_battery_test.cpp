// Host check for core::ScaleBatteryEstimate — plain asserts, no framework.
//   make test-battery
// Builds with g++ against src/core/scale_battery_estimate.cpp only.

#include <cstdarg>
#include <cstdio>

#include "core/scale_battery_estimate.h"

using namespace core;

namespace core {
void logf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
}
}  // namespace core

namespace {

constexpr std::time_t kT0 = 1'790'000'000;  // 2026-09
constexpr std::time_t kHour = 3600;
constexpr std::time_t kDay = 24 * kHour;
constexpr char kMac[] = "aa:bb:cc:dd:ee:01";

int checks = 0;
#define CHECK(x)                                                        \
  do {                                                                  \
    ++checks;                                                           \
    if (!(x)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
      return 1;                                                         \
    }                                                                   \
  } while (0)

ScaleBatteryEstimate with_rate(int rate) {
  ScaleBatteryEstimate e;
  e.set_rate(rate);
  e.take_dirty();
  return e;
}

int test_no_reading() {
  ScaleBatteryEstimate e;
  CHECK(!e.view(kT0).valid);
  e.on_session_end(true, kT0);  // a sleep before any reading: nothing to freeze
  CHECK(e.state().stale_since == 0);
  CHECK(!e.view(kT0).valid);
  return 0;
}

int test_live_flicker_is_ram_only() {
  ScaleBatteryEstimate e;
  e.on_reading(35);
  CHECK(e.take_dirty());  // the first reading is worth keeping
  for (int i = 0; i < 20; ++i) e.on_reading(i % 2 ? 34 : 35);
  CHECK(!e.take_dirty());
  const ScaleBatteryView v = e.view(kT0);
  CHECK(v.valid && !v.estimated && v.pct == 34);  // live: the latest reading
  return 0;
}

int test_sleep_freezes_and_estimates() {
  ScaleBatteryEstimate e = with_rate(10);
  e.on_reading(80);
  e.on_reading(79);
  e.on_session_end(true, kT0);
  CHECK(e.take_dirty());
  CHECK(e.state().stale_since == kT0);
  CHECK(e.state().live_pct == 79);
  // Waking: the frozen value repeats, still stale.
  e.on_reading(79);
  ScaleBatteryView v = e.view(kT0 + 2 * kDay);
  CHECK(v.valid && v.estimated && v.pct == 59);
  v = e.view(kT0 + 2 * kDay + 12 * kHour);
  CHECK(v.pct == 54);
  // Floored at zero.
  CHECK(e.view(kT0 + 30 * kDay).pct == 0);
  // Later sleeps don't move the stale point.
  e.on_session_end(true, kT0 + kDay);
  CHECK(e.state().stale_since == kT0);
  CHECK(e.state().last_seen == kT0 + kDay);
  return 0;
}

int test_rate_setting() {
  ScaleBatteryEstimate e;
  CHECK(e.rate() == kScaleDrainDefaultPctPerDay);
  e.set_rate(8);
  CHECK(e.rate() == 8 && e.take_dirty());
  e.set_rate(8);
  CHECK(!e.take_dirty());  // unchanged: nothing to persist
  e.set_rate(99);
  CHECK(e.rate() == kScaleDrainMaxPctPerDay);
  e.set_rate(0);
  CHECK(e.rate() == kScaleDrainMinPctPerDay);
  return 0;
}

int test_disconnect_without_sleep_stays_live() {
  ScaleBatteryEstimate e;
  e.on_reading(60);
  e.on_session_end(false, kT0);
  CHECK(e.state().stale_since == 0);
  CHECK(!e.view(kT0 + kDay).estimated);
  return 0;
}

int test_changed_value_goes_live() {
  // Covers the charge restart (any direction: 85 frozen -> 28 plugged in, or
  // up to 100 after a charge Apollo slept through) and the sleep-Off case.
  ScaleBatteryEstimate e;
  e.on_reading(85);
  e.on_session_end(true, kT0);
  e.on_reading(85);
  CHECK(e.view(kT0 + kDay).estimated);
  e.take_dirty();
  e.on_reading(28);
  CHECK(e.take_dirty());
  const ScaleBatteryView v = e.view(kT0 + kDay);
  CHECK(v.valid && !v.estimated && v.pct == 28);
  CHECK(e.state().stale_since == 0);
  return 0;
}

int test_no_clock() {
  ScaleBatteryEstimate e;
  e.on_reading(70);
  e.on_session_end(true, 0);  // untrusted: never goes stale
  CHECK(e.state().stale_since == 0);
  e.on_session_end(true, kT0);
  // Stale, but now unknown: hold the last-live value.
  const ScaleBatteryView v = e.view(0);
  CHECK(v.valid && v.estimated && v.pct == 70);
  return 0;
}

int test_calibration() {
  ScaleBatteryEstimate e;
  e.on_reading(100);
  e.arm();
  CHECK(e.armed());
  CHECK(e.take_calibration_result() == kCalibrationNone);
  e.on_session_end(true, kT0);  // goes stale at 100: the run starts
  CHECK(e.state().cal_running && e.state().cal_start_pct == 100);
  for (int d = 1; d <= 12; ++d) {
    e.on_reading(100);  // frozen every morning
    e.on_session_end(true, kT0 + d * kDay);
  }
  // Dead after day 12's session; plugged in later — the changed reading ends
  // it. Plugged-in readings are inflated, but 28 is still below the start, so
  // it is taken as the measurement: 72 % over the 15 days to this reading.
  CHECK(e.take_calibration_result() == kCalibrationNone);
  e.on_reading(28, kT0 + 15 * kDay);
  CHECK(e.state().cal_result == 5);  // pending, so a reboot keeps it
  CHECK(e.calibration_drop_pct() == 72 && e.calibration_hours() == 15 * 24);
  e.take_dirty();
  CHECK(e.take_calibration_result() == 5);
  CHECK(e.take_dirty());                    // handing it out is persisted
  CHECK(e.take_calibration_result() == kCalibrationNone);  // once
  CHECK(!e.armed() && !e.state().cal_running);
  return 0;
}

int test_calibration_full_drain_without_clock() {
  // The same run, but the ending reading comes with no trusted clock (or at
  // or above the start): the pack is taken to have died when last seen.
  ScaleBatteryEstimate e;
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0);
  for (int d = 1; d <= 12; ++d) {
    e.on_reading(100);
    e.on_session_end(true, kT0 + d * kDay);
  }
  e.on_reading(28);  // now = 0
  CHECK(e.state().cal_result == 8);  // 100 % / 12 days = 8.3
  CHECK(e.calibration_drop_pct() == 100 && e.calibration_hours() == 12 * 24);
  CHECK(e.take_calibration_result() == 8);
  // Armed at 80 and came back charged to 100: the reading is above the
  // start, so the pack is taken to have died when last seen: 80 % / 10 days.
  e.on_reading(80);
  e.arm();
  e.on_session_end(true, kT0 + 20 * kDay);
  e.on_reading(80);
  e.on_session_end(true, kT0 + 30 * kDay);
  e.on_reading(100, kT0 + 31 * kDay);
  CHECK(e.calibration_drop_pct() == 80 && e.calibration_hours() == 10 * 24);
  CHECK(e.take_calibration_result() == 8);
  // Armed at 100 and charged back to exactly 100 while Apollo wasn't looking:
  // the frozen value repeats (no flip), and the first real tick down is a
  // 1 % drop over the whole span -- too small to tell from a recharge, so it
  // is reported as too short rather than guessed at.
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0 + 40 * kDay);
  e.on_reading(100, kT0 + 52 * kDay);
  CHECK(e.take_calibration_result() == kCalibrationNone);
  e.on_reading(99, kT0 + 52 * kDay);
  CHECK(e.take_calibration_result() == kCalibrationTooShort);
  return 0;
}

int test_calibration_from_restart_reading() {
  // Owner's case: armed at 100 %, the scale restarted (re-pairing, or its
  // button) after 13.8 days and reads 7 % live: 93 % over 13.8 days = 6.7.
  ScaleBatteryEstimate e;
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0);
  e.on_reading(100);
  e.on_session_end(true, kT0 + 13 * kDay);
  const std::time_t end = kT0 + 13 * kDay + 19 * kHour;
  e.on_reading(7, end);
  CHECK(e.calibration_drop_pct() == 93 && e.calibration_hours() == 13 * 24 + 19);
  CHECK(e.take_calibration_result() == 7);
  CHECK(!e.armed() && e.state().stale_since == 0 && e.state().live_pct == 7);
  // Re-armed from the live 7 %: the next run measures from there.
  e.arm();
  CHECK(e.calibration_drop_pct() == 0 && e.calibration_hours() == 0);
  e.on_session_end(true, end + kHour);
  CHECK(e.state().cal_running && e.state().cal_start_pct == 7);
  return 0;
}

int test_calibration_guard_rails() {
  // Under a day: too short, whatever the drop.
  ScaleBatteryEstimate e;
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0);
  e.on_reading(70, kT0 + 23 * kHour);
  CHECK(e.take_calibration_result() == kCalibrationTooShort);
  CHECK(e.calibration_drop_pct() == 30 && e.calibration_hours() == 23);
  // Under 10 % of drop: too short, however long.
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0 + 2 * kDay);
  e.on_reading(91, kT0 + 5 * kDay);
  CHECK(e.take_calibration_result() == kCalibrationTooShort);
  // Exactly at both limits: accepted (10 % / 1 day).
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0 + 10 * kDay);
  e.on_reading(90, kT0 + 11 * kDay);
  CHECK(e.take_calibration_result() == 10);
  return 0;
}

int test_calibration_flicker_above_start_is_not_a_charge() {
  // Owner's case (2026-11-13): stale at 46, restarted days later with no
  // charge, the live gauge reads 47 (+-1 flicker). That is a ~0 drop, not a
  // full drain: too short, never "46 % over the span".
  ScaleBatteryEstimate e;
  e.on_reading(46);
  e.arm();
  e.on_session_end(true, kT0);
  e.on_reading(46);
  e.on_session_end(true, kT0 + 2 * kDay);
  e.on_reading(47, kT0 + 2 * kDay + kHour);
  CHECK(e.take_calibration_result() == kCalibrationTooShort);
  CHECK(e.calibration_drop_pct() == 0 && e.calibration_hours() == 49);
  // Two above is a charge: the old full-drain arithmetic, 46 % / 2 days.
  e.on_reading(46);
  e.arm();
  e.on_session_end(true, kT0 + 10 * kDay);
  e.on_reading(46);
  e.on_session_end(true, kT0 + 12 * kDay);
  e.on_reading(48, kT0 + 12 * kDay + kHour);
  CHECK(e.take_calibration_result() == 23);
  CHECK(e.calibration_drop_pct() == 46 && e.calibration_hours() == 48);
  return 0;
}

int test_calibration_pending_detail_round_trip() {
  ScaleBatteryEstimate e;
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0);
  e.on_reading(40, kT0 + 6 * kDay);  // 60 % / 6 days
  uint8_t blob[kScaleBatteryBlobLen];
  pack_scale_battery(e.state(), kMac, blob);
  ScaleBatteryState s;
  CHECK(unpack_scale_battery(blob, kScaleBatteryBlobLen, kMac, &s));
  CHECK(s.cal_result == 10 && s.cal_drop == 60 && s.cal_hours == 6 * 24);
  // A pre-v1.4 blob has zeros there: no detail, the result still stands.
  blob[17] = blob[18] = blob[19] = 0;
  CHECK(unpack_scale_battery(blob, kScaleBatteryBlobLen, kMac, &s));
  CHECK(s.cal_result == 10 && s.cal_drop == 0 && s.cal_hours == 0);
  return 0;
}

int test_calibration_old_tail() {
  // (kept as the old ending of test_calibration: the pending result is handed
  // out once and persisted)
  ScaleBatteryEstimate e;
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0);
  for (int d = 1; d <= 12; ++d) {
    e.on_reading(100);
    e.on_session_end(true, kT0 + d * kDay);
  }
  e.on_reading(28);
  CHECK(e.state().cal_result == 8);  // pending, so a reboot keeps it
  e.take_dirty();
  CHECK(e.take_calibration_result() == 8);  // 100 % / 12 days = 8.3
  CHECK(e.take_dirty());                    // handing it out is persisted
  CHECK(e.take_calibration_result() == kCalibrationNone);  // once
  CHECK(!e.armed() && !e.state().cal_running);
  return 0;
}

int test_calibration_too_short_is_reported() {
  // Armed, slept once, charged before any later session: no span to measure.
  ScaleBatteryEstimate e;
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0);
  e.on_reading(30);
  CHECK(e.take_calibration_result() == kCalibrationTooShort);
  CHECK(!e.armed());
  // A clock that went backwards reads the same way.
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0 + kDay);
  ScaleBatteryState s = e.state();
  s.last_seen = static_cast<uint32_t>(kT0);
  e.load(s);
  e.on_reading(40);
  CHECK(e.take_calibration_result() == kCalibrationTooShort);
  return 0;
}

int test_calibration_armed_while_stale_waits() {
  ScaleBatteryEstimate e;
  e.on_reading(90);
  e.on_session_end(true, kT0);
  e.arm();  // mid stale period: this one doesn't count
  e.on_reading(100);  // charged
  CHECK(e.take_calibration_result() == kCalibrationNone);
  CHECK(e.armed() && !e.state().cal_running);
  e.on_session_end(true, kT0 + kDay);
  CHECK(e.state().cal_running && e.state().cal_start_pct == 100);
  return 0;
}

int test_calibration_clamped_and_disarm() {
  ScaleBatteryEstimate e;
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0);
  e.on_session_end(true, kT0 + kHour);  // charged an hour later: too short now
  e.on_reading(50);
  CHECK(e.take_calibration_result() == kCalibrationTooShort);
  // A long run that would compute above the range is clamped.
  e.on_reading(100);
  e.arm();
  e.on_session_end(true, kT0 + 2 * kDay);
  e.on_reading(10, kT0 + 3 * kDay);  // 90 % in a day
  CHECK(e.take_calibration_result() == kScaleDrainMaxPctPerDay);
  e.on_reading(49);
  e.arm();
  e.disarm();
  e.on_session_end(true, kT0 + 2 * kHour);
  CHECK(!e.state().cal_running);
  return 0;
}

int test_blob_round_trip() {
  ScaleBatteryEstimate a = with_rate(12);
  a.on_reading(66);
  a.arm();
  a.on_session_end(true, kT0);
  uint8_t blob[kScaleBatteryBlobLen];
  pack_scale_battery(a.state(), kMac, blob);
  ScaleBatteryState s;
  CHECK(unpack_scale_battery(blob, kScaleBatteryBlobLen, kMac, &s));
  ScaleBatteryEstimate b;
  b.load(s);
  CHECK(b.rate() == 12);
  const ScaleBatteryView v = b.view(kT0 + kDay);
  CHECK(v.estimated && v.pct == 54);
  CHECK(b.state().cal_running && b.state().cal_start_pct == 66);
  CHECK(b.state().stale_since == kT0 && b.state().last_seen == kT0);
  CHECK(b.state().cal_result == kCalibrationNone);
  // A pending result survives too (the modal outlives a reboot).
  ScaleBatteryState p = s;
  p.cal_result = 9;
  pack_scale_battery(p, kMac, blob);
  CHECK(unpack_scale_battery(blob, kScaleBatteryBlobLen, kMac, &s) && s.cal_result == 9);
  p.cal_result = kCalibrationTooShort;
  pack_scale_battery(p, kMac, blob);
  CHECK(unpack_scale_battery(blob, kScaleBatteryBlobLen, kMac, &s) && s.cal_result == 0);
  return 0;
}

int test_blob_rejects_other_scale_and_garbage() {
  ScaleBatteryEstimate a;
  a.on_reading(50);
  uint8_t blob[kScaleBatteryBlobLen];
  pack_scale_battery(a.state(), kMac, blob);
  ScaleBatteryState s;
  s.live_pct = 77;
  // Left behind for another scale (e.g. a rolled-back build re-saved the slot).
  CHECK(!unpack_scale_battery(blob, kScaleBatteryBlobLen, "aa:bb:cc:dd:ee:02", &s));
  CHECK(s.live_pct == 77);  // untouched
  CHECK(!unpack_scale_battery(blob, 12, kMac, &s));  // wrong length
  CHECK(!unpack_scale_battery(nullptr, kScaleBatteryBlobLen, kMac, &s));
  // Out-of-range bytes degrade to legal values.
  blob[1] = 250;
  blob[3] = 200;
  blob[16] = 0;
  CHECK(unpack_scale_battery(blob, kScaleBatteryBlobLen, kMac, &s));
  CHECK(s.live_pct == 100 && s.cal_result == kCalibrationNone &&
        s.rate == kScaleDrainMinPctPerDay);
  return 0;
}

}  // namespace

int main() {
  int (*tests[])() = {
      test_no_reading,
      test_live_flicker_is_ram_only,
      test_sleep_freezes_and_estimates,
      test_rate_setting,
      test_disconnect_without_sleep_stays_live,
      test_changed_value_goes_live,
      test_no_clock,
      test_calibration,
      test_calibration_full_drain_without_clock,
      test_calibration_from_restart_reading,
      test_calibration_guard_rails,
      test_calibration_flicker_above_start_is_not_a_charge,
      test_calibration_pending_detail_round_trip,
      test_calibration_old_tail,
      test_calibration_too_short_is_reported,
      test_calibration_armed_while_stale_waits,
      test_calibration_clamped_and_disarm,
      test_blob_round_trip,
      test_blob_rejects_other_scale_and_garbage,
  };
  for (auto t : tests)
    if (t() != 0) return 1;
  std::printf("scale battery estimate: %d checks passed\n", checks);
  return 0;
}
