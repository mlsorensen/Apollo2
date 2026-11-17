// Host check for core::ReadyChime — plain asserts, no framework.
//   make test-chime
// Header-only: builds with g++ against include/ alone.

#include <cstdio>

#include "core/machine.h"
#include "core/ready_chime.h"

using namespace core;

namespace {

int checks = 0;
#define CHECK(x)                                                        \
  do {                                                                  \
    ++checks;                                                           \
    if (!(x)) {                                                         \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
      return 1;                                                         \
    }                                                                   \
  } while (0)

// A connected, powered-on machine with the steam boiler off; brew at `t`.
MachineSnapshot running(float t, float target = 93.0f) {
  MachineSnapshot s{};
  s.link = Link::Connected;
  s.power = Power::On;
  s.brew_temp_c = t;
  s.brew_target_c = target;
  s.steam_enabled = false;
  return s;
}

// Drive the latch the way App::refresh does: heating derived with hysteresis
// from the previous poll, then fed to the latch. Returns true if it chimed.
struct Rig {
  ReadyChime chime;
  bool heating = false;
  bool poll(const MachineSnapshot& s) {
    heating = derive_heating(s, heating);
    return chime.update(s, heating);
  }
  // n polls at the same snapshot; true if any chimed.
  bool polls(const MachineSnapshot& s, int n) {
    bool any = false;
    for (int i = 0; i < n; ++i) any = poll(s) || any;
    return any;
  }
};

int test_cold_start_chimes_once() {
  Rig r;
  CHECK(!r.polls(running(25), 5));   // warming up: arms, no chime yet
  CHECK(!r.polls(running(80), 2));
  CHECK(r.poll(running(93)));        // reached: the one chime
  CHECK(!r.polls(running(93), 5));
  CHECK(!r.polls(running(85), 5));   // post-shot sag + recovery: quiet
  CHECK(!r.polls(running(93), 5));
  return 0;
}

int test_warm_at_connect_never_chimes() {
  // Owner's case (2026-11-15): a short link drop on a warm machine, then a
  // shot's boiler sag and recovery. The reconnect must not re-arm the chime.
  Rig r;
  CHECK(!r.polls(running(93), 3));   // connected to a machine at temperature
  CHECK(!r.polls(running(84), 5));   // 9 C sag, heating for 5 polls
  CHECK(!r.polls(running(93), 5));   // recovers: silent
  return 0;
}

int test_stale_zero_readings_do_not_count_as_warm() {
  // Right after connecting the snapshot carries 0 C until the first poll
  // answers; the assessment must wait for real numbers.
  Rig r;
  MachineSnapshot blank = running(0, 0);
  CHECK(!r.polls(blank, 4));
  CHECK(!r.polls(running(25), 5));   // the real readings: a cold machine
  CHECK(r.poll(running(93)));        // ...which chimes when it gets there
  return 0;
}

int test_drop_while_warm_then_reconnect() {
  Rig r;
  CHECK(!r.polls(running(25), 5));
  CHECK(r.poll(running(93)));
  MachineSnapshot down = running(93);
  down.link = Link::Disconnected;
  CHECK(!r.polls(down, 3));          // a 1.5 s blip clears the latch...
  CHECK(!r.polls(running(93), 3));   // ...but the reconnect finds it warm
  CHECK(!r.polls(running(84), 5));
  CHECK(!r.polls(running(93), 5));   // no second chime
  return 0;
}

int test_standby_then_on_chimes_again() {
  Rig r;
  CHECK(!r.polls(running(93), 3));   // joined warm: silent session
  MachineSnapshot sb = running(60);
  sb.power = Power::Standby;
  CHECK(!r.polls(sb, 3));
  CHECK(!r.polls(running(60), 5));   // turned on cold-ish: a real warm-up
  CHECK(r.poll(running(93)));
  return 0;
}

int test_reconnect_mid_warmup_still_chimes() {
  Rig r;
  CHECK(!r.polls(running(25), 2));
  MachineSnapshot down = running(40);
  down.link = Link::Connecting;
  CHECK(!r.polls(down, 2));
  CHECK(!r.polls(running(40), 5));   // reconnected while still heating
  CHECK(r.poll(running(93)));        // the warm-up it joined still chimes
  return 0;
}

int test_steam_toggle_no_second_chime() {
  Rig r;
  CHECK(!r.polls(running(25), 5));
  CHECK(r.poll(running(93)));
  MachineSnapshot st = running(93);
  st.steam_enabled = true;
  st.boiler_temp_c = 100;
  st.boiler_target_c = 125;
  CHECK(!r.polls(st, 5));            // steam boiler warming after the toggle
  st.boiler_temp_c = 125;
  CHECK(!r.polls(st, 3));            // once per turn-on holds
  return 0;
}

}  // namespace

int main() {
  int (*tests[])() = {
      test_cold_start_chimes_once,
      test_warm_at_connect_never_chimes,
      test_stale_zero_readings_do_not_count_as_warm,
      test_drop_while_warm_then_reconnect,
      test_standby_then_on_chimes_again,
      test_reconnect_mid_warmup_still_chimes,
      test_steam_toggle_no_second_chime,
  };
  for (auto t : tests)
    if (t() != 0) return 1;
  std::printf("ready chime: %d checks passed\n", checks);
  return 0;
}
