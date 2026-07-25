#pragma once

#include <string>
#include <vector>

#include "core/shot_store.h"

namespace host {

// Host implementation of the ON-DISK shot-store format (core/shot_csv.h),
// rooted at a local directory instead of an SD card — `make sim` exercises it
// so the exact files the device writes can be inspected on a laptop without
// hardware. Synchronous (no writer task); not used for renders (FakeShotStore
// is), just for format validation.
class DirShotStore : public core::IShotStore {
 public:
  explicit DirShotStore(std::string root);  // e.g. "sim_sd" -> sim_sd/Apollo2

  bool available() const override { return true; }
  void save(const core::ShotRecord& record) override;
  int count() const override { return static_cast<int>(index_.size()); }
  int list(core::ShotSummary* out, int max, int offset) const override;
  bool read(uint32_t id, core::ShotRecord& out) const override;
  core::ShotStats stats(int64_t now_unix) const override;

 private:
  std::string dir_;                       // <root>/Apollo2
  std::vector<core::ShotSummary> index_;  // newest first
  uint32_t next_id_ = 1;
};

}  // namespace host
