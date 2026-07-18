#pragma once

#include <cstdint>

namespace core {

// Millisecond shot stopwatch driven by the caller's clock (no platform time
// source — pass the same monotonic ms value the caller polls with). After
// stop() the elapsed value holds so the UI keeps showing the finished shot's
// time until the next start(). uint32 subtraction makes wrap-around a non-issue.
class ShotTimer {
 public:
  void start(uint32_t now_ms) {
    start_ms_ = now_ms;
    elapsed_ms_ = 0;
    running_ = true;
  }
  void stop(uint32_t now_ms) {
    if (!running_) return;
    elapsed_ms_ = now_ms - start_ms_;
    running_ = false;
  }
  void reset() {
    running_ = false;
    elapsed_ms_ = 0;
  }
  bool running() const { return running_; }
  uint32_t elapsed_ms(uint32_t now_ms) const {
    return running_ ? now_ms - start_ms_ : elapsed_ms_;
  }

 private:
  uint32_t start_ms_ = 0;
  uint32_t elapsed_ms_ = 0;
  bool running_ = false;
};

}  // namespace core
