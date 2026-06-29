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

  const uint32_t now = millis() / 1000u;
  const uint32_t start = (now > window_s) ? now - window_s : 0;

  std::array<int, kMaxPoints> cnt{};
  for (int i = 0; i < n; ++i) {
    brew[i] = 0.0f;
    boiler[i] = 0.0f;
  }

  // Walk the ring oldest-first, bucketing each in-window sample by its time.
  for (int i = 0; i < count_; ++i) {
    const int idx = (head_ - count_ + i + 2 * kCap) % kCap;
    const Sample& s = buf_[idx];
    if (s.t_s < start) continue;
    int b = static_cast<int>(static_cast<uint64_t>(s.t_s - start) * n / window_s);
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
}

}  // namespace platform
