#pragma once

#include <cstdio>
#include <vector>

#include "core/shot_store.h"

namespace host {

// Canned shot history for sim renders: a spread of plausible shots over the
// weeks before FakeClock's fixed date, so the History section shows realistic
// metrics, list rows, and shot cards. set_available(false) poses the no-SD
// state (guidance card).
class FakeShotStore : public core::IShotStore {
 public:
  FakeShotStore();

  bool available() const override { return available_; }
  void save(const core::ShotRecord& record) override;
  int count() const override;
  int list(core::ShotSummary* out, int max, int offset) const override;
  bool read(uint32_t id, core::ShotRecord& out) const override;
  bool remove(uint32_t id) override;
  core::ShotStats stats(int64_t now_unix) const override;

  core::StorageInfo storage() const override { return storage_; }
  int64_t stats_since() const override { return stats_since_; }
  void set_stats_since(int64_t t) override { stats_since_ = t; }

  void set_available(bool on) { available_ = on; }
  void set_storage(uint64_t total, uint64_t free, bool full) {
    storage_.total_bytes = total;
    storage_.free_bytes = free;
    storage_.full = full;
  }
  // Pose an unusable card ("exFAT", "NTFS", ...) for the guidance render.
  void set_medium(core::MediumState state, const char* fs) {
    storage_.state = state;
    std::snprintf(storage_.fs_type, sizeof(storage_.fs_type), "%s", fs);
  }

 private:
  bool available_ = true;
  int64_t stats_since_ = 0;
  // Plausible 32 GB card for renders; sim poses "full" via set_storage.
  core::StorageInfo storage_ = {32000000000ull, 12400000000ull, false,
                                core::MediumState::kOk, "FAT"};
  std::vector<core::ShotRecord> shots_;  // newest first
};

}  // namespace host
