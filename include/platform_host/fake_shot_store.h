#pragma once

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
  core::ShotStats stats(int64_t now_unix) const override;

  core::StorageInfo storage() const override { return storage_; }

  void set_available(bool on) { available_ = on; }
  void set_storage(uint64_t total, uint64_t free, bool full) {
    storage_ = {total, free, full};
  }

 private:
  bool available_ = true;
  // Plausible 32 GB card for renders; sim poses "full" via set_storage.
  core::StorageInfo storage_ = {32000000000ull, 12400000000ull, false};
  std::vector<core::ShotRecord> shots_;  // newest first
};

}  // namespace host
