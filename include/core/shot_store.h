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
  ShotMode mode = ShotMode::kAuto;  // effective mode the shot ran under
  bool wired = false;  // paddle harness vs weight-stream detection
};

// A full record: summary + the series. ~7 KB — pass by reference, never copy
// onto the stack casually on-device.
struct ShotRecord {
  static constexpr int kSampleCap = 600;  // mirrors the UI plot ring (60s @ 100ms)
  ShotSummary summary;  // includes mode/wired — the index round-trips them
  int n_samples = 0;
  ShotSample samples[kSampleCap];

  // Optional rendered shot-card image attached at save time (RGB565, row-major
  // with `card_stride_px` pixels per row). Borrowed pointer: valid only during
  // the save() call — implementations copy what they need before returning.
  // Stores turn it into the on-disk PNG; read() never returns pixels.
  const uint16_t* card_rgb565 = nullptr;
  int card_w = 0;
  int card_h = 0;
  int card_stride_px = 0;
};

// Headline metrics for the History view. Accuracy = 100% minus the mean
// absolute final-vs-target error, over targeted shots only (see stats()).
struct ShotStats {
  int total = 0;               // all recorded shots
  float acc_lifetime_pct = 0;  // 0 when no targeted shots exist
  float acc_30d_pct = 0;       // last 30 days before `now_unix`
};

// Capacity readout for the History view. `full` means saves are being
// DROPPED (free space under the store's reserve) — surface it loudly.
// When a card is present but unusable, `state`/`fs_type` say WHY (a raw
// boot-sector probe: "exFAT", "NTFS", ...) so the UI can tell the user to
// reformat instead of implying no card is inserted.
enum class MediumState : uint8_t {
  kNone,       // nothing inserted (or the medium doesn't respond)
  kOk,         // mounted and usable
  kBadFormat,  // card responds, filesystem unsupported/corrupt — see fs_type
};

struct StorageInfo {
  uint64_t total_bytes = 0;  // 0 = unknown (no medium / not applicable)
  uint64_t free_bytes = 0;
  bool full = false;
  MediumState state = MediumState::kNone;
  char fs_type[8] = "";  // "exFAT" / "NTFS" / "FAT32" / "GPT" / "?" when kBadFormat
};

// The one accuracy definition, shared by every store: 100% minus the mean
// relative |final - target| error over targeted shots (target_g > 0).
inline ShotStats compute_shot_stats(const ShotSummary* items, int n,
                                    int64_t now_unix) {
  ShotStats st;
  st.total = n;
  float err_all = 0, err_30 = 0;
  int n_all = 0, n_30 = 0;
  constexpr int64_t k30d = 30ll * 86400;
  for (int i = 0; i < n; ++i) {
    const ShotSummary& s = items[i];
    if (s.target_g <= 0.0f) continue;
    const float e = (s.final_g > s.target_g ? s.final_g - s.target_g
                                            : s.target_g - s.final_g) /
                    s.target_g;
    err_all += e;
    ++n_all;
    if (now_unix - s.unix_time <= k30d) {
      err_30 += e;
      ++n_30;
    }
  }
  if (n_all > 0) st.acc_lifetime_pct = 100.0f * (1.0f - err_all / n_all);
  if (n_30 > 0) st.acc_30d_pct = 100.0f * (1.0f - err_30 / n_30);
  return st;
}

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

  // Capacity of the backing medium. Default: unknown (all zeros). Device
  // implementations return a CACHED value — the UI thread must never touch
  // the medium directly.
  virtual StorageInfo storage() const { return {}; }
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
