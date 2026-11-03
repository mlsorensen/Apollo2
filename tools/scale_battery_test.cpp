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
  // Dead after day 12's session; plugged in later — the changed reading ends it.
  CHECK(e.take_calibration_result() == kCalibrationNone);
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
  e.on_session_end(true, kT0 + kHour);  // charged an hour later: off procedure
  e.on_reading(50);
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
