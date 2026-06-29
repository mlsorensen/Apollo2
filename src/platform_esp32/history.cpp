#include "platform_esp32/history.h"

#include <Arduino.h>

#include <array>
#include <cmath>

namespace platform {

namespace {
constexpr int kMaxPoints = 256;  // upper bound on chart resolution
}

void History::add(float brew_c, float boiler_c) {
  buf_[head_] = Sample{millis() / 1000u, brew_c, boiler_c};
  head_ = (head_ + 1) % kCap;
  if (count_ < kCap) ++count_;
}

void History::series(uint32_t window_s, float* brew, float* boiler, int n) const {
  if (n > kMaxPoints) n = kMaxPoints;
  for (int i = 0; i < n; ++i) {
    brew[i] = NAN;
    boiler[i] = NAN;
  }
  if (count_ == 0 || n <= 0 || window_s == 0) return;

  // Right edge is always "now"; left edge is now-window (signed — it can sit
  // before boot while uptime < window, which just leaves the left buckets empty).
  // Anchoring on boot instead put the newest sample mid-chart, not at "now".
  const int64_t now = static_cast<int64_t>(millis() / 1000u);
  const int64_t left = now - static_cast<int64_t>(window_s);

  std::array<int, kMaxPoints> cnt{};
  for (int i = 0; i < n; ++i) {
    brew[i] = 0.0f;
    boiler[i] = 0.0f;
  }

  // Walk the ring oldest-first, bucketing each in-window sample by its time.
  for (int i = 0; i < count_; ++i) {
    const int idx = (head_ - count_ + i + 2 * kCap) % kCap;
    const Sample& s = buf_[idx];
    const int64_t t = static_cast<int64_t>(s.t_s);
    if (t < left || t > now) continue;  // outside the visible window
    int b = static_cast<int>((t - left) * n / static_cast<int64_t>(window_s));
    if (b < 0) b = 0;
    if (b >= n) b = n - 1;
    brew[b] += s.brew;
    boiler[b] += s.boiler;
    ++cnt[b];
  }

  for (int i = 0; i < n; ++i) {
    if (cnt[i] > 0) {
      brew[i] /= cnt[i];
      boiler[i] /= cnt[i];
    } else {
      brew[i] = NAN;
      boiler[i] = NAN;
    }
  }

  // Fill interior empty-bucket runs by linear interpolation when the run is short
  // (sparse sampling on a short window). Leave long runs as NaN — those are real
  // gaps (machine off / BLE dropped). Leading/trailing NaN (no data) also stay.
  const uint32_t bucket_s = (window_s + n - 1) / n;  // seconds per bucket (ceil)
  int prev = -1;
  for (int i = 0; i < n; ++i) {
    if (cnt[i] == 0) continue;
    if (prev >= 0 && i - prev > 1) {
      const uint32_t span = static_cast<uint32_t>(i - prev) * bucket_s;
      if (span <= kGapThresholdS) {
        for (int k = prev + 1; k < i; ++k) {
          const float f = static_cast<float>(k - prev) / static_cast<float>(i - prev);
          brew[k] = brew[prev] + f * (brew[i] - brew[prev]);
          boiler[k] = boiler[prev] + f * (boiler[i] - boiler[prev]);
        }
      }
    }
    prev = i;
  }
}

}  // namespace platform
