#pragma once

#include <cstdint>

#include "core/history.h"

// Temperature history ring buffer. add() is called periodically from the device
// loop with the latest brew/boiler temps; series() downsamples a window for the
// Stats charts. Single-threaded (loop only), so no locking. 24 h at 30 s spacing
// is ~2880 samples (~34 KB) — kept in plain RAM.

namespace platform {

class History : public core::IHistory {
 public:
  void add(float brew_c, float boiler_c);  // append one sample, stamped "now"
  void series(uint32_t window_s, float* brew, float* boiler, int n) const override;

 private:
  static constexpr int kCap = 2880;
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
