#pragma once

#include <cstdint>

#include "core/history.h"

// Temperature history ring buffer. add() is called periodically from the device
// loop with the latest brew/boiler temps; series() downsamples a window for the
// Stats charts. Single-threaded (loop only), so no locking. At 2-min spacing a
// full 24 h is ~720 samples (~8.6 KB) — kept in plain RAM. The coarse spacing is
// fine for the 24 h view and series() interpolates short gaps so the 30 min view
// still reads as a continuous line (genuine disconnects stay as gaps).

namespace platform {

class History : public core::IHistory {
 public:
  // Sampling cadence (the device loop uses this for its sample timer too). Coarse
  // on purpose: even on the widest view 1 px is minutes, so finer just wastes RAM.
  static constexpr uint32_t kSampleIntervalS = 120;  // 2 minutes

  void add(float brew_c, float boiler_c);  // append one sample, stamped "now"
  void series(uint32_t window_s, float* brew, float* boiler, int n) const override;

 private:
  // ~24 h of headroom at the sample cadence (the widest chart window is 24 h).
  static constexpr int kCap = 24 * 60 * 60 / kSampleIntervalS + 16;  // ~736
  // Empty spans up to this long are interpolated (sparse sampling on a short
  // window); longer spans are real gaps (machine off / BLE dropped) -> NaN.
  static constexpr uint32_t kGapThresholdS = kSampleIntervalS * 3;  // 6 min
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
