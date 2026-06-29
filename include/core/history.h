#pragma once

#include <cstddef>
#include <cstdint>

// Temperature history port. The device samples brew/boiler temps into a ring
// buffer; the Stats charts read a downsampled view of it. Like the other ports,
// the UI depends only on this — the device fills a real buffer, the host fakes a
// curve. Times are monotonic seconds since boot (relative); the axis is labelled
// "N ago -> now", which sidesteps needing a wall clock for the graph.

namespace core {

class IHistory {
 public:
  virtual ~IHistory() = default;

  // Downsample the most recent `window_s` seconds into `n` buckets, oldest at
  // index 0 and newest at n-1. Each output is the mean temperature in that
  // bucket, or NaN where no sample falls in it (the chart shows a gap). `brew`
  // and `boiler` must each hold `n` floats.
  virtual void series(uint32_t window_s, float* brew, float* boiler, int n) const = 0;
};

}  // namespace core
