// Host check for the BrewController's paddle breaker — plain asserts.
//   make test-paddle
// Builds with g++ against src/core/brew_controller.cpp + shot_detector.cpp
// (the core headers are LVGL/Arduino-free by rule). The scenarios replay what
// the owner's X-8 logged the night its GPIO5 died (raw flips at tens per
// second, then OFF/ON pairs inside one second) against what hands do: a
// manual backflush, flush-flush-shot, a quick flick or three.

#include <cassert>
#include <cstdarg>
#include <cstdio>

#include "core/brew_controller.h"

using namespace core;

namespace core {
void logf(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vprintf(fmt, ap);
  va_end(ap);
}
uint32_t now_ms() { return 0; }
}  // namespace core

namespace {

int g_fail = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
      ++g_fail;                                                         \
    }                                                                   \
  } while (0)

struct FakePaddle : public IPaddle {
  bool level = false;   // what sensed() returns (true = paddle closed)
  bool driven = false;  // the machine's line as last driven
  int drive_true = 0, drive_false = 0;
  bool owned = true;
  int own_true = 0, own_false = 0;
  bool available() const override { return true; }
  bool sensed() override { return level; }
  void drive(bool closed) override {
    driven = closed;
    if (closed) ++drive_true; else ++drive_false;
  }
  void own(bool o) override {
    owned = o;
    if (o) ++own_true; else ++own_false;
  }
};

struct FakeScale : public IScale {
  bool connected = true;
  float weight = 0.0f;
  uint32_t seq = 0;
  ScaleSnapshot snapshot() const override {
    ScaleSnapshot snap{};
    snap.name = "fake";
    snap.connected = connected;
    snap.weight_g = weight;
    snap.seq = seq;
    return snap;
  }
  ScaleFeatures features() const override { return ScaleFeatures{}; }
  void tare() override { weight = 0.0f; }
  int device_setting_count() const override { return 0; }
  ScaleSettingDesc device_setting(int) const override { return ScaleSettingDesc{}; }
  int device_setting_value(int) const override { return -1; }
  void set_device_setting(int, int) override {}
  ScaleBatteryInfo battery_info() const override { return ScaleBatteryInfo{}; }
  void set_battery_drain_rate(int) override {}
  void set_battery_calibration(bool) override {}
  int take_battery_calibration_result() override { return -1; }
};

// One rig per scenario: controller in Manual mode with the wired relay on
// (the path where every accepted ON edge closes the machine's line).
struct Rig {
  FakePaddle pad;
  FakeScale scale;
  BrewController bc{pad, scale};
  uint32_t t = 1000;
  explicit Rig(ShotMode mode = ShotMode::kManual, int flush_s = 3) {
    bc.seed(36.0f, static_cast<int>(mode), 2.0f, 30, /*wired=*/true, flush_s, 3, 2.0f, 3);
    // Boot safety: the first stable level is adopted without an edge.
    run(500);
  }
  // Advance `ms` with the paddle held at its current level, polling every 25 ms
  // (the controller's own sense cadence).
  void run(uint32_t ms) {
    for (uint32_t end = t + ms; t < end; t += 25) bc.poll(t);
  }
  void set(bool level, uint32_t hold_ms) {
    pad.level = level;
    run(hold_ms);
  }
  // A real edge with a visible bounce: the new level, one poll back at the
  // old one, then the new level held. At a 25 ms poll a ~1 ms contact bounce
  // is seen about one edge in 25; the backflush scenario shows one in five,
  // five times worse than reality.
  void bounce_to(bool level, uint32_t hold_ms) {
    pad.level = level;
    run(25);
    pad.level = !level;
    run(25);
    pad.level = level;
    run(hold_ms);
  }
  // The dying pad: the raw level changes between every poll.
  void chatter(uint32_t ms) {
    for (uint32_t end = t + ms; t < end; t += 25) {
      pad.level = !pad.level;
      bc.poll(t);
    }
  }
  bool tripped() const { return bc.snapshot().paddle_tripped; }
};

void test_chatter_trips() {
  std::printf("chatter trips the breaker\n");
  Rig r;
  r.chatter(1000);
  CHECK(r.tripped());
  CHECK(r.bc.snapshot().paddle_trip_seq == 1);
  CHECK(!r.pad.driven);         // the line is open
  CHECK(r.pad.own_false == 1);  // the Link handed the paddle back
  CHECK(!r.pad.owned);
  // Tripped: a clean, held ON edge does nothing, cleaning is refused.
  const int drives = r.pad.drive_true;
  r.set(false, 500);
  r.set(true, 2000);
  CHECK(r.pad.drive_true == drives);
  CHECK(!r.pad.driven);
  CHECK(!r.bc.snapshot().clean_ready);
  r.bc.toggle_manual_flush();
  CHECK(!r.pad.driven);
  CHECK(!r.bc.start_backflush());
  r.set(false, 500);
}

void test_backflush_never_trips() {
  std::printf("manual backflush (10 x 5 s on / 5 s off, bouncy) never trips\n");
  Rig r;
  for (int i = 0; i < 10; ++i) {
    if (i % 5 == 0) r.bounce_to(true, 5000); else r.set(true, 5000);
    if (i % 5 == 2) r.bounce_to(false, 5000); else r.set(false, 5000);
  }
  CHECK(!r.tripped());
  CHECK(r.pad.drive_true == 10);   // every ON edge reached the machine
  CHECK(r.pad.drive_false >= 10);
  CHECK(r.pad.own_false == 0);
}

void test_flush_flush_shot() {
  std::printf("flush, flush, shot never trips\n");
  Rig r;
  r.set(true, 1500);
  r.set(false, 1000);
  r.set(true, 1500);
  r.set(false, 1000);
  r.set(true, 30000);
  r.set(false, 1000);
  CHECK(!r.tripped());
  CHECK(r.pad.drive_true == 3);
}

void test_short_pulses() {
  std::printf("three quick flicks pass, four trip\n");
  {
    Rig r;
    for (int i = 0; i < 3; ++i) {
      r.set(true, 200);
      r.set(false, 1000);
    }
    CHECK(!r.tripped());
    CHECK(r.pad.drive_true == 3);
  }
  {
    Rig r;
    for (int i = 0; i < 4; ++i) {
      r.set(true, 200);
      r.set(false, 1000);
    }
    CHECK(r.tripped());
    CHECK(!r.pad.driven);
  }
  {
    // The window: four flicks spread over more than a minute do not add up.
    Rig r;
    for (int i = 0; i < 4; ++i) {
      r.set(true, 200);
      r.set(false, 25000);
    }
    CHECK(!r.tripped());
  }
}

void test_trip_mid_cleaning() {
  std::printf("a trip mid-backflush and mid-flush opens the line at once\n");
  {
    Rig r;
    CHECK(r.bc.start_backflush());
    CHECK(r.pad.driven);
    CHECK(r.bc.snapshot().backflush_active);
    r.chatter(1000);
    CHECK(r.tripped());
    CHECK(!r.pad.driven);
    CHECK(!r.bc.snapshot().backflush_active);
    // ...and the pulses do not resume: the sequence is gone, the line stays open.
    r.pad.level = false;
    r.run(20000);
    CHECK(!r.pad.driven);
  }
  {
    Rig r;
    r.bc.toggle_manual_flush();
    CHECK(r.pad.driven);
    CHECK(r.bc.snapshot().flush_running);
    r.chatter(1000);
    CHECK(r.tripped());
    CHECK(!r.pad.driven);
    CHECK(!r.bc.snapshot().flush_running);
  }
  {
    // Mid-shot (Manual, line held by the user's flip): the shot ends, line opens.
    Rig r;
    r.set(true, 5000);
    CHECK(r.pad.driven);
    r.chatter(1000);
    CHECK(r.tripped());
    CHECK(!r.pad.driven);
    CHECK(r.bc.snapshot().phase == ShotPhase::kIdle);
  }
}

void test_reset() {
  std::printf("reset: re-trips under flicker, holds when quiet, paddle works again\n");
  Rig r;
  r.chatter(1000);
  CHECK(r.tripped());
  r.bc.reset_paddle_breaker();
  CHECK(!r.tripped());
  CHECK(r.pad.own_true == 1);
  CHECK(r.pad.owned);
  r.chatter(2000);  // still flickering -> trips again at once
  CHECK(r.tripped());
  CHECK(r.bc.snapshot().paddle_trip_seq == 2);
  CHECK(!r.pad.driven);
  // Quiet line, reset, a normal flip: the machine runs again.
  r.pad.level = false;
  r.run(1000);
  r.bc.reset_paddle_breaker();
  CHECK(!r.tripped());
  const int drives = r.pad.drive_true;
  r.set(true, 3000);
  CHECK(r.pad.driven);
  CHECK(r.pad.drive_true == drives + 1);
  r.set(false, 500);
  CHECK(!r.pad.driven);
  CHECK(!r.tripped());
  r.bc.reset_paddle_breaker();  // not tripped: a no-op
  CHECK(r.pad.own_true == 2);
}

void test_boot_adoption_is_not_a_pulse() {
  std::printf("boot with the paddle left ON adopts the level without counting\n");
  FakePaddle pad;
  FakeScale scale;
  BrewController bc{pad, scale};
  bc.seed(36.0f, static_cast<int>(ShotMode::kManual), 2.0f, 30, true, 3, 3, 2.0f, 3);
  pad.level = true;
  for (uint32_t t = 1000; t < 3000; t += 25) bc.poll(t);
  CHECK(!bc.snapshot().paddle_tripped);
  CHECK(!pad.driven);  // no surprise auto-start
}

}  // namespace

int main() {
  test_chatter_trips();
  test_backflush_never_trips();
  test_flush_flush_shot();
  test_short_pulses();
  test_trip_mid_cleaning();
  test_reset();
  test_boot_adoption_is_not_a_pulse();
  if (g_fail != 0) {
    std::printf("paddle breaker: %d FAILED\n", g_fail);
    return 1;
  }
  std::printf("paddle breaker: all checks passed\n");
  return 0;
}
