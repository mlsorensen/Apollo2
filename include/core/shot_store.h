#pragma once

#include <cstdint>

#include "core/brew.h"

// Shot-history port. When a shot finishes (reaches review), the UI assembles a
// ShotRecord — headline stats plus the sampled weight/flow series — and hands
// it to this port. The device implementation persists to an SD card under
// /Apollo2 (records + a rendered PNG per shot: a take-away "database" readable
// on any computer); the host fake serves canned records to the sim. Boards
// without storage use NullShotStore and the History UI shows guidance instead.

namespace core {

struct ShotSample {
  uint32_t t_ms;   // since shot start
  float weight_g;  // net shot grams at t
  float flow_gps;  // flow rate at t (post drop-negative policy)
};

struct ShotSummary {
  uint32_t id;         // store-assigned, monotonic
  int64_t unix_time;   // shot END, UTC epoch
  uint32_t duration_ms;
  float target_g;      // 0 = no target (manual/stopwatch shot)
  float final_g;       // settled net weight
  float avg_gps;       // final_g / duration
};

// A full record: summary + the series. ~7 KB — pass by reference, never copy
// onto the stack casually on-device.
struct ShotRecord {
  static constexpr int kSampleCap = 600;  // mirrors the UI plot ring (60s @ 100ms)
  ShotSummary summary;
  ShotMode mode = ShotMode::kAuto;  // effective mode the shot ran under
  bool wired = false;               // paddle harness vs weight-stream detection
  int n_samples = 0;
  ShotSample samples[kSampleCap];
};

// Headline metrics for the History view. Accuracy = 100% minus the mean
// absolute final-vs-target error, over targeted shots only (see stats()).
struct ShotStats {
  int total = 0;               // all recorded shots
  float acc_lifetime_pct = 0;  // 0 when no targeted shots exist
  float acc_30d_pct = 0;       // last 30 days before `now_unix`
};

class IShotStore {
 public:
  virtual ~IShotStore() = default;

  // Storage is reachable (SD card mounted / fake posed present). The UI shows
  // setup guidance when false; save() calls are dropped.
  virtual bool available() const = 0;

  // Persist a record. Must not block: device implementations copy and hand off
  // to a background writer. The record is complete when this returns.
  virtual void save(const ShotRecord& record) = 0;

  virtual int count() const = 0;

  // Fill `out` with up to `max` summaries, newest first, skipping `offset`.
  // Returns how many were written.
  virtual int list(ShotSummary* out, int max, int offset) const = 0;

  // Load one full record (with samples) by id. False if unknown/unreadable.
  virtual bool read(uint32_t id, ShotRecord& out) const = 0;

  // Headline metrics; `now_unix` anchors the 30-day window.
  virtual ShotStats stats(int64_t now_unix) const = 0;
};

// For boards/builds without storage: never available, drops everything.
class NullShotStore : public IShotStore {
 public:
  bool available() const override { return false; }
  void save(const ShotRecord&) override {}
  int count() const override { return 0; }
  int list(ShotSummary*, int, int) const override { return 0; }
  bool read(uint32_t, ShotRecord&) const override { return false; }
  ShotStats stats(int64_t) const override { return {}; }
};

}  // namespace core
