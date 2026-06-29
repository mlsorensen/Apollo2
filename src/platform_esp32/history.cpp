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

  // Mean per bucket, or NaN where empty. The caller sizes n so buckets reliably
  // hold a sample (no sparsity gaps), so empty buckets here are real gaps
  // (machine off / BLE dropped) and the chart breaks the line there.
  for (int i = 0; i < n; ++i) {
    if (cnt[i] > 0) {
      brew[i] /= cnt[i];
      boiler[i] /= cnt[i];
    } else {
      brew[i] = NAN;
      boiler[i] = NAN;
    }
  }
}

}  // namespace platform
