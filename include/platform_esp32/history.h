#pragma once

#include <cstdint>

#include "core/history.h"

// Temperature history ring buffer. add() is called periodically from the device
// loop with the latest brew/boiler temps; series() downsamples a window into a
// fixed bucket count for the Stats charts. Single-threaded (loop only), so no
// locking. 24 h at 30 s is ~2880 samples (~35 KB) in plain RAM — the chart
// renders the same regardless (it buckets), so the sample count is purely the
// history depth/resolution vs RAM trade-off. series() interpolates short empty
// runs so short windows read as a line; long empty runs stay gaps (disconnects).

namespace platform {

class History : public core::IHistory {
 public:
  // Sampling cadence (the device loop uses this for its sample timer too).
  static constexpr uint32_t kSampleIntervalS = 30;

  void add(float brew_c, float boiler_c);  // append one sample, stamped "now"
  void series(uint32_t window_s, float* brew, float* boiler, int n) const override;
  uint32_t sample_interval_s() const override { return kSampleIntervalS; }

 private:
  // ~24 h of headroom at the sample cadence (the widest chart window is 24 h).
  static constexpr int kCap = 24 * 60 * 60 / kSampleIntervalS + 16;  // ~2896
  struct Sample {
    uint32_t t_s;
    float brew;
    float boiler;
  };
  Sample buf_[kCap];
  int count_ = 0;  // valid samples (<= kCap)
  int head_ = 0;   // next write slot
};

}  // namespace platform
