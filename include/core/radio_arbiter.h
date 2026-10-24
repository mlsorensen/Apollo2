#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "core/provisioner.h"  // core::AddrType

// One radio "listener" at a time, for every BLE link sharing the host.
//
// The NimBLE host can run ONE initiating (connect-by-address) procedure at a
// time, cannot scan while one is pending, and its connect-cancel is GLOBAL —
// `ble_gap_conn_cancel()` aborts whoever is connecting, not the caller's own
// attempt. Three links (the Micra, the espresso scale, the bench scale) each
// reconnecting on their own task therefore need an arbiter, and the arbiter
// has to be the ONLY thing that ever cancels: it cancels the current holder
// and nobody else.
//
// Shape: platform-free (std::mutex + plain state), POLLED by the link loops
// from their own tasks — no task of its own. A link asks `poll()` what to do
// next (idle, connect by address, run one finder scan chunk for everyone, or
// run the user's Settings scan), does that blocking work itself on its own
// transport, then `release()`s. The arbiter decides; the links act.
//
// Scale reconnects ("the finder"): with ONE scale missing the slot gets the
// direct connect-by-address it always had (initiating is a 100 %-duty listen
// for that address and answers in the same advertising event — faster than
// any scan+connect). With TWO missing, one chunked scan (kChunkMs on, a gap
// off) matches BOTH saved addresses; a match stops the scan early and is
// reported as a SIGHTING for that slot, which the slot's OWN task then turns
// into a short direct connect (the scale is advertising right now). The
// connection is never handed across tasks — only the sighting is. Nothing
// scans while nothing is missing, and empty chunks back off (kGapMinMs ->
// x5 -> kGapMaxMs) so two scales left off overnight don't keep the radio at
// 91 % scan duty against the live Micra link and WiFi.
//
// Parking is a per-slot BITMASK of sources (screensaver, setup portal, shot)
// so one source clearing can never un-park another's hold — the old single
// pause level was shared by the peer-scan pause and the saver park, and the
// Micra scan ending un-parked a saver-parked scale. A parked slot is not
// "missing"; parking the current holder preempts it.
//
// Host test: tools/radio_arbiter_test.cpp (`make test-radio`).

namespace core {

enum class RadioSlot : uint8_t { kMicra = 0, kScaleEspresso = 1, kScaleBench = 2, kCount = 3 };

enum class RadioWork : uint8_t {
  kIdle,           // nothing for this slot now: sleep ~100 ms and poll again
  kDirectConnect,  // connect-by-address on your own transport
  kFinderChunk,    // run ONE scan chunk matching finder_targets(); report sightings
  kUserScan,       // run the user's Settings scan (highest priority)
};

// Sources that park a slot's reconnects. ORed per slot; each source clears
// only its own bit.
enum class ParkSource : uint8_t { kSaver = 1, kPortal = 2, kShot = 4 };

enum class ReleaseResult : uint8_t { kConnected, kFailed, kPreempted, kScanDone };

// What a finder chunk matches on: the saved address + type of every scale
// slot that is currently missing.
struct FinderTarget {
  char addr[18];   // "aa:bb:cc:dd:ee:ff" + NUL, as ScanResult::mac
  AddrType type;   // kUnknown = match either
  RadioSlot slot;
};
struct FinderTargets {
  FinderTarget t[2];
  int count = 0;
};

// A finder chunk saw one of its targets advertising.
struct Sighting {
  bool found = false;
  RadioSlot slot = RadioSlot::kCount;
  AddrType type = AddrType::kUnknown;
};

struct RadioGrant {
  RadioWork work = RadioWork::kIdle;
  // kDirectConnect: the type to try. From a fresh sighting that is the
  // advertised type (known for sure, so one short try); otherwise the slot's
  // stored type (kUnknown = try both).
  AddrType addr_type = AddrType::kUnknown;
  bool from_sighting = false;  // -> short timeout: the peer is advertising now
  uint32_t ticket = 0;         // echo to release() / was_preempted()
};

class RadioArbiter {
 public:
  static constexpr uint32_t kChunkMs = 10000;          // one finder scan chunk
  static constexpr uint32_t kSightingWindowMs = 3000;  // a sighting stays fresh
  static constexpr uint32_t kGapMinMs = 1000;          // after a chunk, before the next
  static constexpr uint32_t kGapMaxMs = 20000;         // empty-chunk backoff cap

  // --- State fed by the links / device main (any thread; mutex inside). ---
  // The slot's saved peer (empty = nothing saved) and its stored address type.
  void set_target(RadioSlot s, const std::string& addr, AddrType type);
  // "This slot wants to be connected" (saved + user-enabled + credentials).
  void set_wanted(RadioSlot s, bool wanted);
  // The raw link is up (before any weight-stream gate).
  void set_connected(RadioSlot s, bool connected);
  // Umbra partner mode: the peer connects IN; the slot is never "missing".
  void set_partner_mode(RadioSlot s, bool partner);
  // Park / unpark one source's bit. Parking the current holder preempts it.
  void set_parked(RadioSlot s, ParkSource src, bool parked);
  // The user tapped Scan on this slot's page: preempts any holder (even this
  // slot's own connect attempt) and is granted next.
  void request_user_scan(RadioSlot s);
  // Disconnect / Forget / Save / address change: abort this slot's attempt IF
  // it is the holder. Never touches another slot's attempt.
  void abort_slot(RadioSlot s);
  // Transport hooks, called OUTSIDE the arbiter's mutex: `hold(true)` must
  // cancel an in-flight connect on that slot's transport and refuse new ones
  // until `hold(false)`; `cancel_scan()` must stop a running scan chunk.
  void set_hold_hook(RadioSlot s, std::function<void(bool)> hook);
  void set_scan_cancel_hook(RadioSlot s, std::function<void()> hook);

  // --- Link-task protocol (each slot from its OWN task only). ---
  // Idempotent for the holder: polling again returns the same grant.
  RadioGrant poll(RadioSlot s, uint32_t now_ms);
  void release(RadioSlot s, uint32_t ticket, ReleaseResult result, uint32_t now_ms);
  bool was_preempted(RadioSlot s, uint32_t ticket) const;
  bool user_scan_pending(RadioSlot s) const;
  FinderTargets finder_targets() const;
  // From the slot running a chunk: one of the targets was seen advertising.
  void report_sighting(RadioSlot seen, AddrType type, uint32_t now_ms);

  // Diagnostics (log lines).
  RadioSlot holder() const;
  RadioWork holder_work() const;

 private:
  struct Slot {
    std::string addr;
    AddrType type = AddrType::kUnknown;
    bool wanted = false;
    bool connected = false;
    bool partner = false;
    uint8_t parked = 0;
    bool user_scan = false;
    bool has_sighting = false;
    uint32_t sighting_ms = 0;
    AddrType sighting_type = AddrType::kUnknown;
    uint32_t last_served_ms = 0;
    std::function<void(bool)> hold;
    std::function<void()> cancel_scan;
  };

  struct HookCall {
    int slot = -1;
    bool hold_level = false;   // hold(level)
    bool is_hold = false;      // else cancel_scan
  };

  bool missing_locked(const Slot& s) const;
  int missing_count_locked() const;
  bool ready_locked(int i, uint32_t now_ms, RadioWork& work) const;
  bool fresh_sighting_locked(const Slot& s, uint32_t now_ms) const;
  void preempt_holder_locked(HookCall& call);
  void touch_locked();  // state changed: restart the chunk backoff
  void run_hook(const HookCall& call);

  mutable std::mutex m_;
  Slot slots_[static_cast<int>(RadioSlot::kCount)];
  int holder_ = -1;
  RadioWork holder_work_ = RadioWork::kIdle;
  AddrType holder_type_ = AddrType::kUnknown;  // the grant's address type
  bool holder_from_sighting_ = false;
  bool holder_preempted_ = false;
  uint32_t ticket_ = 0;
  uint32_t next_chunk_ok_ms_ = 0;
  uint32_t chunk_gap_ms_ = kGapMinMs;
  bool sighting_in_chunk_ = false;
};

inline const char* radio_slot_name(RadioSlot s) {
  switch (s) {
    case RadioSlot::kMicra: return "micra";
    case RadioSlot::kScaleEspresso: return "scale";
    case RadioSlot::kScaleBench: return "bench";
    default: return "?";
  }
}

}  // namespace core
