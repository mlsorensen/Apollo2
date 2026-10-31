// Host check for core::DoseTracker — plain asserts, no framework.
//   make test-dose
// Builds with g++ against src/core/dose.cpp only (the core headers are
// LVGL/Arduino-free by rule), so it runs in under a second.

#include <cmath>
#include <cstdio>

#include "core/dose.h"

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

bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

DoseInputs in(float g, uint32_t now, bool connected = true) {
  DoseInputs i;
  i.bench_g = g;
  i.bench_connected = connected;
  i.now_ms = now;
  return i;
}

// Hold `g` on the bench from `from` for `ms`, ticking every 100 ms; returns
// the time after the hold.
uint32_t hold(DoseTracker& t, float g, uint32_t from, uint32_t ms) {
  for (uint32_t at = from; at <= from + ms; at += 100) t.tick(in(g, at));
  return from + ms;
}

int test_container_rule() {
  DoseTracker t;
  CHECK(near(t.net_dose(18.0f), 18.0f));  // no containers: the reading is the dose

  const float c[] = {480.0f, 111.0f, 12.0f};  // the 12 g one is refused by the floor
  t.set_containers(c, 3);
  CHECK(t.container_count() == 2);
  CHECK(near(t.net_dose(498.3f), 18.3f));  // loaded portafilter
  CHECK(near(t.net_dose(118.0f), 7.0f));   // a 7 g single in the Niche cup
  CHECK(near(t.net_dose(18.0f), 18.0f));   // bare dose in a tared cup: no container below it
  CHECK(near(t.net_dose(49.0f), 49.0f));   // a heavy dose still under the lightest container
  CHECK(near(t.net_dose(111.0f), 0.0f));   // the empty container: nothing in it
  CHECK(DoseTracker::container_ok(50.0f) && !DoseTracker::container_ok(49.9f));
  // Two containers closer than a dose apart would misread a dose larger than
  // their gap (25 g in a 111 g cup = 136 g -> "6 g" against a 130 g one):
  // Capture refuses them.
  const float stored[] = {480.0f, 111.0f, 0.0f};
  CHECK(DoseTracker::container_clash(stored, 3, 2, 130.0f) == 1);
  CHECK(DoseTracker::container_clash(stored, 3, 2, 161.0f) == 1);   // exactly 50 apart: still a clash
  CHECK(DoseTracker::container_clash(stored, 3, 2, 161.1f) == -1);
  CHECK(DoseTracker::container_clash(stored, 3, 1, 115.0f) == -1);  // rewriting slot 1 itself
  CHECK(DoseTracker::container_clash(stored, 3, 2, 440.0f) == 0);
  return 0;
}

int test_candidate_gate() {
  DoseTracker t;
  const float c[] = {480.0f};
  t.set_containers(c, 1);

  // Nothing fed, nothing offered.
  CHECK(!t.candidate());
  // A fresh reading needs to hold still first.
  t.tick(in(498.3f, 1000));
  CHECK(!t.candidate());
  hold(t, 498.3f, 1000, 900);
  CHECK(!t.candidate());  // 0.9 s: not yet
  hold(t, 498.3f, 1900, 200);
  CHECK(t.candidate());
  CHECK(near(t.candidate_dose(), 18.3f));

  // The gate's edges are inclusive: 5.0 and 50.0 g qualify, 4.9 and 50.1 don't.
  hold(t, 485.0f, 2200, 2000);
  CHECK(t.candidate() && near(t.candidate_dose(), 5.0f));
  hold(t, 484.9f, 4300, 2000);
  CHECK(!t.candidate());
  hold(t, 530.0f, 6400, 2000);
  CHECK(t.candidate() && near(t.candidate_dose(), 50.0f));
  hold(t, 530.1f, 8500, 2000);
  CHECK(!t.candidate());
  // At the device's 500 ms refresh the third tick (1.0 s) offers Record.
  t.tick(in(498.3f, 20000));
  t.tick(in(498.3f, 20500));
  CHECK(!t.candidate());
  t.tick(in(498.3f, 21000));
  CHECK(t.candidate());
  // A milk jug (412.5 g, no container below it -> 412.5 g net): never a candidate.
  uint32_t now = hold(t, 412.5f, 2200, 2000);
  CHECK(!t.candidate());
  // An empty bench: nothing.
  now = hold(t, 0.0f, now, 2000);
  CHECK(!t.candidate());
  // A 7 g single straight on a tared scale: a candidate.
  now = hold(t, 7.0f, now, 2000);
  CHECK(t.candidate() && near(t.candidate_dose(), 7.0f));
  // Jitter inside the band keeps it stable; a jump restarts the clock.
  t.tick(in(7.1f, now + 100));
  CHECK(t.candidate());
  t.tick(in(7.6f, now + 200));
  CHECK(!t.candidate());
  // Disconnected: nothing, even if the last reading was fine.
  now = hold(t, 7.0f, now + 300, 2000);
  CHECK(t.candidate());
  t.tick(in(7.0f, now + 100, /*connected=*/false));
  CHECK(!t.candidate());
  return 0;
}

int test_record_and_spend() {
  DoseTracker t;
  const float c[] = {480.0f};
  t.set_containers(c, 1);
  CHECK(near(t.record(), 0.0f));  // nothing to record
  uint32_t now = hold(t, 498.3f, 0, 1500);
  CHECK(near(t.record(), 18.3f));
  CHECK(t.has_pending() && near(t.pending(), 18.3f));
  // The same weight still on the bench: "recorded", not Record again.
  t.tick(in(498.4f, now + 100));
  CHECK(t.recorded_showing() && !t.candidate());
  // Portafilter lifted: the card goes dormant, the dose stays pending.
  now = hold(t, 0.0f, now + 200, 2000);
  CHECK(!t.recorded_showing() && !t.candidate() && t.has_pending());
  // A new dose-sized weight: Record offered again; recording replaces.
  now = hold(t, 496.0f, now, 2000);
  CHECK(t.candidate());
  CHECK(near(t.record(), 16.0f));
  CHECK(near(t.pending(), 16.0f));
  // The shot starts: the dose is spent.
  CHECK(near(t.take_for_shot(), 16.0f));
  CHECK(!t.has_pending());
  CHECK(near(t.take_for_shot(), 0.0f));
  // The user's tap clears too.
  hold(t, 496.0f, now + 100, 2000);
  // (496 g is within the recorded band of the last record, but nothing is
  // pending now, so it is a candidate again.)
  CHECK(t.candidate());
  t.record();
  t.clear();
  CHECK(!t.has_pending());
  return 0;
}

int test_walk_away() {
  const float c[] = {480.0f};

  // Screensaver starting clears; one already on at record time does not.
  {
    DoseTracker t;
    t.set_containers(c, 1);
    uint32_t now = hold(t, 498.3f, 0, 1500);
    t.record();
    DoseInputs i = in(498.3f, now + 100);
    i.screensaver = true;
    CHECK(t.tick(i) == DoseClear::Screensaver);
    CHECK(!t.has_pending());
    // Saver stays on; a new record under it is kept until the NEXT start.
    now = now + 200;
    for (int k = 0; k < 20; ++k) { i = in(498.3f, now += 100); i.screensaver = true; t.tick(i); }
    CHECK(t.candidate());
    t.record();
    i = in(498.3f, now += 100); i.screensaver = true;
    CHECK(t.tick(i) == DoseClear::None && t.has_pending());
  }
  // Either scale down for 30 s clears; a 5 s blip does not.
  {
    DoseTracker t;
    t.set_containers(c, 1);
    uint32_t now = hold(t, 498.3f, 0, 1500);
    t.record();
    DoseInputs i;
    for (uint32_t k = 0; k < 50; ++k) {  // 5 s down
      i = in(498.3f, now += 100); i.espresso_up = false;
      CHECK(t.tick(i) == DoseClear::None);
    }
    i = in(498.3f, now += 100);  // back up
    CHECK(t.tick(i) == DoseClear::None && t.has_pending());
    DoseClear why = DoseClear::None;
    for (uint32_t k = 0; k < 310 && why == DoseClear::None; ++k) {
      i = in(498.3f, now += 100); i.bench_up = false;
      why = t.tick(i);
    }
    CHECK(why == DoseClear::ScaleLost && !t.has_pending());
  }
  // A link already DOWN at Record time is not a drop: an espresso scale
  // asleep while the dose is weighed must not eat it. Its downtime counts
  // only once it has been seen up.
  {
    DoseTracker t;
    t.set_containers(c, 1);
    uint32_t now = 0;
    DoseInputs i;
    for (int k = 0; k < 20; ++k) { i = in(498.3f, now += 100); i.espresso_up = false; t.tick(i); }
    CHECK(t.candidate());
    t.record();
    for (int k = 0; k < 400; ++k) {  // 40 s still down
      i = in(498.3f, now += 100); i.espresso_up = false;
      CHECK(t.tick(i) == DoseClear::None);
    }
    CHECK(t.has_pending());
    i = in(498.3f, now += 100); t.tick(i);  // up once (cup on the scale)
    DoseClear why = DoseClear::None;
    for (int k = 0; k < 310 && why == DoseClear::None; ++k) {
      i = in(498.3f, now += 100); i.espresso_up = false;
      why = t.tick(i);
    }
    CHECK(why == DoseClear::ScaleLost);  // a real drop after that still counts
  }
  // Machine on -> standby clears; unknown power never does.
  {
    DoseTracker t;
    t.set_containers(c, 1);
    uint32_t now = hold(t, 498.3f, 0, 1500);
    DoseInputs i = in(498.3f, now += 100); i.power_known = true; i.machine_on = true; t.tick(i);
    t.record();
    i = in(498.3f, now += 100);  // link dropped: power unknown
    CHECK(t.tick(i) == DoseClear::None);
    i = in(498.3f, now += 100); i.power_known = true; i.machine_on = false;
    CHECK(t.tick(i) == DoseClear::None);  // no On->Standby edge seen (unknown before)
    CHECK(t.has_pending());
    i = in(498.3f, now += 100); i.power_known = true; i.machine_on = true; t.tick(i);
    i = in(498.3f, now += 100); i.power_known = true; i.machine_on = false;
    CHECK(t.tick(i) == DoseClear::Standby && !t.has_pending());
  }
  // Backstop: 60 min after Record.
  {
    DoseTracker t;
    t.set_containers(c, 1);
    uint32_t now = hold(t, 498.3f, 0, 1500);
    t.record();
    CHECK(t.tick(in(498.3f, now + kDoseBackstopMs - 1000)) == DoseClear::None);
    CHECK(t.tick(in(498.3f, now + kDoseBackstopMs + 1000)) == DoseClear::Expired);
    CHECK(!t.has_pending());
  }
  // Tick wrap-around: a record just before the tick counter wraps survives.
  {
    DoseTracker t;
    t.set_containers(c, 1);
    uint32_t now = hold(t, 498.3f, 0xFFFFFFFFu - 2000, 1500);
    t.record();
    CHECK(t.tick(in(498.3f, now + 1000)) == DoseClear::None);  // wrapped
    CHECK(t.has_pending());
  }
  return 0;
}

// The owner's question: with realistic containers and doses, where does the
// weight-only rule get it wrong? Every reading a real counter produces is
// run through the container rule + the candidate gate and classified
// against what was actually on the scale. Hard failures (a real dose read
// as the wrong grams, or not offered) fail the test; the unavoidable
// look-alikes (a non-dose that happens to weigh like a dose) are counted
// and printed, since the only cure for those is not tapping Record.
int test_realistic_sweep() {
  struct Kit {
    const char* name;
    float containers[kMaxDoseContainers];
    int n;
  };
  // Portafilters: 58 mm bottomless ~400-460 g, spouted ~480-560 g. Dosing
  // cups: Niche 111 g, Weber Bird ~120 g, a heavy steel cup ~180 g, light
  // plastic ~35 g (refused by the floor: tare it).
  const Kit kits[] = {
      {"bottomless only", {430.0f, 0.0f, 0.0f}, 1},
      {"spouted + Niche cup", {520.0f, 111.0f, 0.0f}, 2},
      {"bottomless + Niche + steel cup", {430.0f, 111.0f, 180.0f}, 3},
      {"two portafilters + cup", {430.0f, 520.0f, 120.0f}, 3},
  };
  int lookalikes = 0, lookalike_milk = 0, lookalike_untared_milk = 0;
  for (const Kit& k : kits) {
    // Every kit must pass Capture's own rules, or the sweep means nothing.
    for (int i = 0; i < k.n; ++i) {
      CHECK(DoseTracker::container_ok(k.containers[i]));
      CHECK(DoseTracker::container_clash(k.containers, k.n, i, k.containers[i]) == -1);
    }
    DoseTracker t;
    t.set_containers(k.containers, k.n);
    auto settled = [&](float reading) { hold(t, reading, 0, 1500); };
    // Doses 8..28 g in 0.5 g steps, in every container untared, and in a
    // tared cup: each must be offered at exactly its grams.
    for (float d = 8.0f; d <= 28.01f; d += 0.5f) {
      for (int i = 0; i < k.n; ++i) {
        settled(k.containers[i] + d);
        CHECK(t.candidate());
        CHECK(near(t.candidate_dose(), d, 0.05f));
      }
      settled(d);  // tared
      CHECK(t.candidate());
      CHECK(near(t.candidate_dose(), d, 0.05f));
    }
    // Each container empty, and an empty bench: nothing offered.
    for (int i = 0; i < k.n; ++i) {
      settled(k.containers[i]);
      CHECK(!t.candidate());
    }
    settled(0.0f);
    CHECK(!t.candidate());
    // Milk in a TARED jug or cup, 30..400 g in 10 g steps: a look-alike
    // whenever the reading lands 5..50 g above a stored container (or is
    // itself 30..50 g). Never a wrong number for a real dose, just an offer
    // nobody has to take.
    for (float m = 30.0f; m <= 400.01f; m += 10.0f) {
      settled(m);
      if (t.candidate()) { ++lookalikes; ++lookalike_milk; }
    }
    // Milk poured into an UNTARED stored cup (the cup's weight included):
    // 30..300 g of milk. A look-alike only while the milk itself weighs
    // 30..50 g.
    for (int i = 0; i < k.n; ++i) {
      for (float m = 30.0f; m <= 300.01f; m += 10.0f) {
        settled(k.containers[i] + m);
        if (t.candidate()) { ++lookalikes; ++lookalike_untared_milk; }
      }
    }
    // A full milk jug (jug ~600 g + 300 g milk) and a bag of beans: never.
    settled(900.0f);
    CHECK(!t.candidate());
    settled(1000.0f);
    CHECK(!t.candidate());
  }
  std::printf("dose_test: realistic sweep — %d look-alike offers (tared milk/water %d, "
              "untared milk 30-50 g %d), 0 wrong doses\n",
              lookalikes, lookalike_milk, lookalike_untared_milk);
  return 0;
}

int test_ratio() {
  CHECK(near(brew_ratio(36.4f, 18.2f), 2.0f));
  CHECK(near(brew_ratio(36.4f, 0.0f), 0.0f));
  CHECK(near(brew_ratio(0.0f, 18.2f), 0.0f));
  return 0;
}

}  // namespace

int main() {
  if (test_container_rule() || test_candidate_gate() || test_record_and_spend() ||
      test_walk_away() || test_realistic_sweep() || test_ratio())
    return 1;
  std::printf("dose_test: all %d checks passed\n", checks);
  return 0;
}
