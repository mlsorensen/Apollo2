// Host check for core::RadioArbiter — plain asserts, no framework.
//   make test-radio
// Builds with g++ against src/core/radio_arbiter.cpp only (the core headers
// are LVGL/Arduino-free by rule), so it runs in under a second. The cases are
// the decision table from the design review (2026-10-24): one scale missing,
// two missing, a sighting, the bench parked mid-shot while the espresso scale
// reconnects, user scans preempting, Micra/finder fairness, the global-cancel
// rule (only the holder is ever cancelled), and ticket idempotence.

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "core/radio_arbiter.h"

using namespace core;

namespace {

constexpr RadioSlot M = RadioSlot::kMicra;
constexpr RadioSlot E = RadioSlot::kScaleEspresso;
constexpr RadioSlot B = RadioSlot::kScaleBench;

struct Hooks {
  std::vector<std::pair<RadioSlot, bool>> holds;  // (slot, level)
  std::vector<RadioSlot> cancels;
  int hold_count(RadioSlot s, bool level) const {
    int n = 0;
    for (const auto& h : holds)
      if (h.first == s && h.second == level) ++n;
    return n;
  }
  int cancel_count(RadioSlot s) const {
    int n = 0;
    for (RadioSlot c : cancels)
      if (c == s) ++n;
    return n;
  }
};

struct Rig {
  RadioArbiter arb;
  Hooks hooks;
  uint32_t now = 1000;
  Rig() {
    for (RadioSlot s : {M, E, B}) {
      arb.set_hold_hook(s, [this, s](bool on) { hooks.holds.push_back({s, on}); });
      arb.set_scan_cancel_hook(s, [this, s]() { hooks.cancels.push_back(s); });
    }
  }
  void save(RadioSlot s, const char* addr, AddrType t = AddrType::kRandom) {
    arb.set_target(s, addr, t);
    arb.set_wanted(s, true);
  }
  RadioGrant poll(RadioSlot s) { return arb.poll(s, now); }
  // Poll every slot once (in index order) and return each grant.
  void poll_all(RadioGrant& m, RadioGrant& e, RadioGrant& b) {
    m = poll(M);
    e = poll(E);
    b = poll(B);
  }
  void release(RadioSlot s, const RadioGrant& g, ReleaseResult r) {
    arb.release(s, g.ticket, r, now);
  }
};

int idle_count(const RadioGrant& a, const RadioGrant& b, const RadioGrant& c) {
  return (a.work == RadioWork::kIdle) + (b.work == RadioWork::kIdle) + (c.work == RadioWork::kIdle);
}

// Drive polls until `s` holds work `w` (or give up).
bool settle(Rig& r, RadioSlot s, RadioWork w, RadioGrant& out, int max_rounds = 6) {
  for (int i = 0; i < max_rounds; ++i) {
    RadioGrant m, e, b;
    r.poll_all(m, e, b);
    const RadioGrant& g = s == M ? m : s == E ? e : b;
    if (g.work == w) {
      out = g;
      return true;
    }
    r.now += 100;
  }
  return false;
}

void test_all_connected_idle() {
  Rig r;
  r.save(E, "aa:00:00:00:00:01");
  r.save(B, "aa:00:00:00:00:02");
  r.save(M, "bb:00:00:00:00:01", AddrType::kPublic);
  r.arb.set_connected(E, true);
  r.arb.set_connected(B, true);
  r.arb.set_connected(M, true);
  for (int i = 0; i < 50; ++i) {
    RadioGrant m, e, b;
    r.poll_all(m, e, b);
    assert(idle_count(m, e, b) == 3);
    r.now += 1000;
  }
  std::puts("ok: all connected -> idle, no chunk");
}

void test_one_missing_direct() {
  Rig r;
  r.save(E, "aa:00:00:00:00:01");
  RadioGrant g;
  assert(settle(r, E, RadioWork::kDirectConnect, g));
  assert(g.addr_type == AddrType::kRandom);
  assert(!g.from_sighting);
  // The empty bench slot and the unconfigured Micra stay idle.
  assert(r.poll(B).work == RadioWork::kIdle);
  assert(r.poll(M).work == RadioWork::kIdle);
  // Holder re-poll: same ticket, same work.
  const RadioGrant again = r.poll(E);
  assert(again.work == RadioWork::kDirectConnect && again.ticket == g.ticket);
  // A stale-ticket release is ignored.
  r.arb.release(E, g.ticket + 7, ReleaseResult::kFailed, r.now);
  assert(r.arb.holder() == E);
  r.release(E, g, ReleaseResult::kConnected);
  r.arb.set_connected(E, true);
  assert(r.arb.holder() == RadioSlot::kCount);
  assert(r.poll(E).work == RadioWork::kIdle);
  std::puts("ok: one missing -> direct connect; idempotent; stale ticket ignored");
}

void test_two_saved_one_missing() {
  Rig r;
  r.save(E, "aa:00:00:00:00:01");
  r.save(B, "aa:00:00:00:00:02");
  r.arb.set_connected(E, true);
  RadioGrant g;
  assert(settle(r, B, RadioWork::kDirectConnect, g));
  for (int i = 0; i < 20; ++i) {
    assert(r.poll(E).work == RadioWork::kIdle);
    assert(r.poll(B).work == RadioWork::kDirectConnect);  // never a chunk
    r.now += 500;
  }
  std::puts("ok: two saved, one missing -> direct, never a chunk");
}

void test_two_missing_chunks_and_backoff() {
  Rig r;
  r.save(E, "aa:00:00:00:00:01", AddrType::kPublic);
  r.save(B, "aa:00:00:00:00:02", AddrType::kRandom);
  RadioGrant m, e, b;
  r.poll_all(m, e, b);
  // Exactly one chunk holder; the other scale slot idles.
  assert((e.work == RadioWork::kFinderChunk) != (b.work == RadioWork::kFinderChunk));
  assert(m.work == RadioWork::kIdle);
  const RadioSlot scanner = e.work == RadioWork::kFinderChunk ? E : B;
  const RadioSlot other = scanner == E ? B : E;
  const RadioGrant held = scanner == E ? e : b;
  // The targets are both saved addresses with their types.
  const FinderTargets t = r.arb.finder_targets();
  assert(t.count == 2);
  assert(std::string(t.t[0].addr) == "aa:00:00:00:00:01" && t.t[0].type == AddrType::kPublic);
  assert(std::string(t.t[1].addr) == "aa:00:00:00:00:02" && t.t[1].type == AddrType::kRandom);
  assert(r.poll(other).work == RadioWork::kIdle);
  // Empty chunk: the next chunk waits the minimum gap, and the gap grows x5
  // per empty chunk up to the cap.
  uint32_t expected_gap = RadioArbiter::kGapMinMs;
  for (int round = 0; round < 4; ++round) {
    r.now += RadioArbiter::kChunkMs;
    r.arb.release(scanner, held.ticket, ReleaseResult::kScanDone, r.now);
    const uint32_t released = r.now;
    r.now = released + expected_gap - 1;
    r.poll_all(m, e, b);
    assert(idle_count(m, e, b) == 3);  // not due yet
    r.now = released + expected_gap;
    r.poll_all(m, e, b);
    assert((e.work == RadioWork::kFinderChunk) != (b.work == RadioWork::kFinderChunk));
    const RadioSlot s2 = e.work == RadioWork::kFinderChunk ? E : B;
    const RadioGrant h2 = s2 == E ? e : b;
    // keep the loop generic
    const_cast<RadioGrant&>(held) = h2;
    const_cast<RadioSlot&>(scanner) = s2;
    expected_gap = expected_gap * 5 > RadioArbiter::kGapMaxMs ? RadioArbiter::kGapMaxMs
                                                              : expected_gap * 5;
  }
  // A sighting resets the gap.
  r.arb.report_sighting(B, AddrType::kRandom, r.now);
  r.now += 200;
  r.arb.release(scanner, held.ticket, ReleaseResult::kScanDone, r.now);
  // The sighted slot gets a short direct connect with the advertised type;
  // the other slot (and the Micra) idle meanwhile.
  r.poll_all(m, e, b);
  assert(b.work == RadioWork::kDirectConnect && b.from_sighting && b.addr_type == AddrType::kRandom);
  assert(e.work == RadioWork::kIdle && m.work == RadioWork::kIdle);
  r.release(B, b, ReleaseResult::kConnected);
  r.arb.set_connected(B, true);
  // One missing now -> direct for the espresso slot, no chunk.
  RadioGrant g;
  assert(settle(r, E, RadioWork::kDirectConnect, g));
  assert(!g.from_sighting && g.addr_type == AddrType::kPublic);
  std::puts("ok: two missing -> one chunk holder; empty-chunk backoff; sighting -> short direct");
}

void test_sighting_window_expires() {
  Rig r;
  r.save(E, "aa:00:00:00:00:01");
  r.save(B, "aa:00:00:00:00:02");
  RadioGrant m, e, b;
  r.poll_all(m, e, b);
  const RadioSlot scanner = e.work == RadioWork::kFinderChunk ? E : B;
  const RadioGrant held = scanner == E ? e : b;
  r.arb.report_sighting(E, AddrType::kPublic, r.now);
  r.arb.release(scanner, held.ticket, ReleaseResult::kScanDone, r.now);
  // Nobody polls the sighted slot for longer than the window: it expires and
  // the arbiter falls back to chunking (after the gap).
  r.now += RadioArbiter::kSightingWindowMs + RadioArbiter::kGapMinMs + 1;
  r.poll_all(m, e, b);
  assert(m.work == RadioWork::kIdle);
  assert((e.work == RadioWork::kFinderChunk) != (b.work == RadioWork::kFinderChunk));
  std::puts("ok: sighting window expiry -> back to chunks");
}

void test_bench_parked_mid_shot() {
  Rig r;
  r.save(E, "aa:00:00:00:00:01");
  r.save(B, "aa:00:00:00:00:02");
  r.arb.set_parked(B, ParkSource::kShot, true);
  // Only the espresso slot is missing -> direct connect, never a chunk.
  RadioGrant g;
  assert(settle(r, E, RadioWork::kDirectConnect, g));
  assert(r.poll(B).work == RadioWork::kIdle);
  r.release(E, g, ReleaseResult::kFailed);
  // Park bits are independent: saver parks too, then the shot ends -> still parked.
  r.arb.set_parked(B, ParkSource::kSaver, true);
  r.arb.set_parked(B, ParkSource::kShot, false);
  r.now += 2000;
  for (int i = 0; i < 5; ++i) {
    assert(r.poll(B).work == RadioWork::kIdle);
    r.now += 100;
  }
  // Saver clears -> both missing -> chunks.
  r.arb.set_parked(B, ParkSource::kSaver, false);
  r.now += 100;
  RadioGrant m, e, b;
  // Drain any direct grant the espresso slot took meanwhile.
  r.poll_all(m, e, b);
  if (e.work == RadioWork::kDirectConnect) {
    r.release(E, e, ReleaseResult::kFailed);
    r.now += 100;
    r.poll_all(m, e, b);
  }
  assert((e.work == RadioWork::kFinderChunk) != (b.work == RadioWork::kFinderChunk));
  std::puts("ok: bench parked mid-shot -> espresso direct; park bits independent");
}

void test_partner_slot_never_missing() {
  Rig r;
  r.save(E, "aa:00:00:00:00:01");
  r.save(B, "aa:00:00:00:00:02");
  r.arb.set_partner_mode(E, true);
  RadioGrant g;
  assert(settle(r, B, RadioWork::kDirectConnect, g));  // not a chunk
  assert(r.poll(E).work == RadioWork::kIdle);
  assert(r.arb.finder_targets().count == 1);
  std::puts("ok: partner slot never missing");
}

void test_user_scan_preempts_connect() {
  Rig r;
  r.save(M, "bb:00:00:00:00:01", AddrType::kPublic);
  RadioGrant g;
  assert(settle(r, M, RadioWork::kDirectConnect, g));
  assert(r.hooks.hold_count(M, false) >= 1);  // the grant cleared its hold level
  r.arb.request_user_scan(E);
  // Preempt is issued at the request and re-issued on every poll by the
  // requester until the holder releases.
  assert(r.hooks.hold_count(M, true) == 1);
  assert(r.poll(E).work == RadioWork::kIdle);
  assert(r.hooks.hold_count(M, true) == 2);
  assert(r.arb.was_preempted(M, g.ticket));
  r.release(M, g, ReleaseResult::kPreempted);
  // Now the scan is granted; the Micra idles while it runs.
  RadioGrant s = r.poll(E);
  assert(s.work == RadioWork::kUserScan);
  assert(r.poll(M).work == RadioWork::kIdle);
  r.release(E, s, ReleaseResult::kScanDone);
  assert(!r.arb.user_scan_pending(E));
  // The Micra's next grant clears its hold level.
  const int holds_false_before = r.hooks.hold_count(M, false);
  RadioGrant g2;
  assert(settle(r, M, RadioWork::kDirectConnect, g2));
  assert(r.hooks.hold_count(M, false) == holds_false_before + 1);
  std::puts("ok: user scan preempts a connect; hold level re-issued, cleared on next grant");
}

void test_user_scan_preempts_own_connect_and_chunk() {
  Rig r;
  r.save(E, "aa:00:00:00:00:01");
  RadioGrant g;
  assert(settle(r, E, RadioWork::kDirectConnect, g));
  r.arb.request_user_scan(E);  // this slot's OWN attempt is cancelled
  assert(r.hooks.hold_count(E, true) == 1);
  assert(r.arb.was_preempted(E, g.ticket));
  r.release(E, g, ReleaseResult::kPreempted);
  assert(r.poll(E).work == RadioWork::kUserScan);
  RadioGrant s = r.poll(E);
  r.release(E, s, ReleaseResult::kScanDone);

  // A chunk is preempted through the scan-cancel hook.
  r.save(B, "aa:00:00:00:00:02");
  r.now += 100;
  RadioGrant m, e, b;
  r.poll_all(m, e, b);
  const RadioSlot scanner = e.work == RadioWork::kFinderChunk ? E : B;
  const RadioGrant held = scanner == E ? e : b;
  r.arb.request_user_scan(M);
  assert(r.hooks.cancel_count(scanner) == 1);
  r.arb.release(scanner, held.ticket, ReleaseResult::kPreempted, r.now);
  assert(r.poll(M).work == RadioWork::kUserScan);
  std::puts("ok: user scan preempts own connect and a finder chunk");
}

void test_micra_vs_chunk_fairness() {
  Rig r;
  r.save(M, "bb:00:00:00:00:01", AddrType::kPublic);
  r.save(E, "aa:00:00:00:00:01");
  r.save(B, "aa:00:00:00:00:02");
  // Alternation: whoever was served longest ago goes next.
  int micra = 0, chunks = 0;
  RadioWork last = RadioWork::kIdle;
  for (int round = 0; round < 6; ++round) {
    RadioGrant m, e, b;
    r.poll_all(m, e, b);
    const bool mi = m.work == RadioWork::kDirectConnect;
    const bool ch = e.work == RadioWork::kFinderChunk || b.work == RadioWork::kFinderChunk;
    assert(mi != ch);  // exactly one holder
    const RadioWork w = mi ? RadioWork::kDirectConnect : RadioWork::kFinderChunk;
    if (round > 0) assert(w != last);
    last = w;
    if (mi) {
      ++micra;
      r.now += 8000;
      r.release(M, m, ReleaseResult::kFailed);
    } else {
      ++chunks;
      const RadioSlot s = e.work == RadioWork::kFinderChunk ? E : B;
      const RadioGrant h = s == E ? e : b;
      r.now += RadioArbiter::kChunkMs;
      r.arb.release(s, h.ticket, ReleaseResult::kScanDone, r.now);
      // touch: a state change keeps the chunk due (the gap would otherwise
      // let the Micra run twice, which is also fine, but keep the test strict)
      r.arb.set_wanted(M, false);
      r.arb.set_wanted(M, true);
    }
    r.now += 100;
  }
  assert(micra == 3 && chunks == 3);
  // A fresh sighting beats a waiting Micra.
  r.arb.report_sighting(E, AddrType::kRandom, r.now);
  RadioGrant m, e, b;
  r.poll_all(m, e, b);
  assert(m.work == RadioWork::kIdle);
  assert(e.work == RadioWork::kDirectConnect && e.from_sighting);
  std::puts("ok: Micra and finder alternate; a sighting beats a waiting Micra");
}

void test_abort_only_holder() {
  Rig r;
  r.save(E, "aa:00:00:00:00:01");
  r.save(B, "aa:00:00:00:00:02");
  r.arb.set_parked(B, ParkSource::kShot, true);
  RadioGrant g;
  assert(settle(r, E, RadioWork::kDirectConnect, g));
  // Bench Forget/Disconnect while the espresso attempt is in flight: the
  // espresso transport must NOT be told to cancel (its cancel is global).
  r.arb.abort_slot(B);
  r.arb.set_wanted(B, false);
  assert(r.hooks.hold_count(E, true) == 0);
  assert(!r.arb.was_preempted(E, g.ticket));
  // Aborting the holder itself does.
  r.arb.abort_slot(E);
  assert(r.hooks.hold_count(E, true) == 1);
  assert(r.arb.was_preempted(E, g.ticket));
  std::puts("ok: abort cancels only the holder");
}

void test_parking_holder_preempts() {
  Rig r;
  r.save(E, "aa:00:00:00:00:01");
  RadioGrant g;
  assert(settle(r, E, RadioWork::kDirectConnect, g));
  r.arb.set_parked(E, ParkSource::kSaver, true);
  assert(r.hooks.hold_count(E, true) == 1);
  assert(r.arb.was_preempted(E, g.ticket));
  r.release(E, g, ReleaseResult::kPreempted);
  for (int i = 0; i < 5; ++i) {
    assert(r.poll(E).work == RadioWork::kIdle);
    r.now += 100;
  }
  r.arb.set_parked(E, ParkSource::kSaver, false);
  assert(settle(r, E, RadioWork::kDirectConnect, g));
  std::puts("ok: parking the holder preempts; unpark resumes");
}

}  // namespace

int main() {
  test_all_connected_idle();
  test_one_missing_direct();
  test_two_saved_one_missing();
  test_two_missing_chunks_and_backoff();
  test_sighting_window_expires();
  test_bench_parked_mid_shot();
  test_partner_slot_never_missing();
  test_user_scan_preempts_connect();
  test_user_scan_preempts_own_connect_and_chunk();
  test_micra_vs_chunk_fairness();
  test_abort_only_holder();
  test_parking_holder_preempts();
  std::puts("radio_arbiter_test: all passed");
  return 0;
}
