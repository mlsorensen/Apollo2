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

int checks = 0;
#define CHECK(x)                                                        \
  do {                                                                  \
    ++checks;                                                           \
    if (!(x)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
      return 1;                                                         \
    }                                                                   \
  } while (0)

int test_no_reading() {
  ScaleBatteryEstimate e;
  CHECK(!e.view(15, kT0).valid);
  e.on_session_end(true, kT0);  // a sleep before any reading: nothing to freeze
  CHECK(e.state().stale_since == 0);
  CHECK(!e.view(15, kT0).valid);
  return 0;
}

int test_live_flicker_is_ram_only() {
  ScaleBatteryEstimate e;
  e.on_reading(35);
  CHECK(e.take_dirty());  // the first reading is worth keeping
  for (int i = 0; i < 20; ++i) e.on_reading(i % 2 ? 34 : 35);
  CHECK(!e.take_dirty());
  const ScaleBatteryView v = e.view(15, kT0);
  CHECK(v.valid && !v.estimated && v.pct == 34);
  return 0;
}

int test_sleep_freezes_and_estimates() {
  ScaleBatteryEstimate e;
  e.on_reading(80);
  e.on_reading(79);
  e.on_session_end(true, kT0);
  CHECK(e.take_dirty());
  CHECK(e.state().stale_since == kT0);
  CHECK(e.state().live_pct == 79);
  // Waking: the frozen value repeats, still stale.
  e.on_reading(79);
  ScaleBatteryView v = e.view(10, kT0 + 2 * kDay);
  CHECK(v.valid && v.estimated && v.pct == 59);
  v = e.view(10, kT0 + 2 * kDay + 12 * kHour);
  CHECK(v.pct == 54);
  // Floored at zero.
  CHECK(e.view(10, kT0 + 30 * kDay).pct == 0);
  // Later sleeps don't move the stale point.
  e.on_session_end(true, kT0 + kDay);
  CHECK(e.state().stale_since == kT0);
  CHECK(e.state().last_seen == kT0 + kDay);
  return 0;
}

int test_disconnect_without_sleep_stays_live() {
  ScaleBatteryEstimate e;
  e.on_reading(60);
  e.on_session_end(false, kT0);
  CHECK(e.state().stale_since == 0);
  CHECK(!e.view(15, kT0 + kDay).estimated);
  return 0;
}

int test_changed_value_goes_live() {
  // Covers the charge restart (any direction: 85 frozen -> 28 plugged in, or
  // up to 100 after a charge Apollo slept through) and the sleep-Off case.
  ScaleBatteryEstimate e;
  e.on_reading(85);
  e.on_session_end(true, kT0);
  e.on_reading(85);
  CHECK(e.view(15, kT0 + kDay).estimated);
  e.take_dirty();
  e.on_reading(28);
  CHECK(e.take_dirty());
  const ScaleBatteryView v = e.view(15, kT0 + kDay);
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
  const ScaleBatteryView v = e.view(15, 0);
  CHECK(v.valid && v.estimated && v.pct == 70);
  return 0;
}

int test_calibration() {
  ScaleBatteryEstimate e;
  e.on_reading(100);
  e.arm();
  CHECK(e.armed());
  CHECK(e.take_calibration_result() == -1);
  e.on_session_end(true, kT0);  // goes stale at 100: the run starts
  CHECK(e.state().cal_running && e.state().cal_start_pct == 100);
  for (int d = 1; d <= 12; ++d) {
    e.on_reading(100);  // frozen every morning
    e.on_session_end(true, kT0 + d * kDay);
  }
  // Dead after day 12's session; plugged in later — the changed reading ends it.
  CHECK(e.take_calibration_result() == -1);
  e.on_reading(28);
  CHECK(e.take_calibration_result() == 8);  // 100 % / 12 days = 8.3
  CHECK(e.take_calibration_result() == -1);  // once
  CHECK(!e.armed() && !e.state().cal_running);
  return 0;
}

int test_calibration_armed_while_stale_waits() {
  ScaleBatteryEstimate e;
  e.on_reading(90);
  e.on_session_end(true, kT0);
  e.arm();  // mid stale period: this one doesn't count
  e.on_reading(100);  // charged
  CHECK(e.take_calibration_result() == -1);
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

int test_persist_round_trip() {
  ScaleBatteryEstimate a;
  a.on_reading(66);
  a.arm();
  a.on_session_end(true, kT0);
  ScaleBatteryEstimate b;
  b.load(a.state());
  const ScaleBatteryView v = b.view(12, kT0 + kDay);
  CHECK(v.estimated && v.pct == 54);
  CHECK(b.state().cal_running && b.state().cal_start_pct == 66);
  return 0;
}

}  // namespace

int main() {
  int (*tests[])() = {
      test_no_reading,
      test_live_flicker_is_ram_only,
      test_sleep_freezes_and_estimates,
      test_disconnect_without_sleep_stays_live,
      test_changed_value_goes_live,
      test_no_clock,
      test_calibration,
      test_calibration_armed_while_stale_waits,
      test_calibration_clamped_and_disarm,
      test_persist_round_trip,
  };
  for (auto t : tests)
    if (t() != 0) return 1;
  std::printf("scale battery estimate: %d checks passed\n", checks);
  return 0;
}
