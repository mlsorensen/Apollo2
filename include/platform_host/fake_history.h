#pragma once

#include <cmath>

#include "core/history.h"

// Host stand-in: a synthetic heat-up-then-plateau curve so the Stats charts have
// something to render in the simulator (brew settles ~93C, boiler ~123C).

namespace host {

class FakeHistory : public core::IHistory {
 public:
  void series(uint32_t /*window_s*/, float* brew, float* boiler, int n) const override {
    for (int i = 0; i < n; ++i) {
      const float frac = (n > 1) ? static_cast<float>(i) / (n - 1) : 1.0f;
      // exp rise from a cold start to the set point, plus a tiny ripple.
      brew[i] = 93.0f - 73.0f * std::exp(-frac * 6.0f) + 0.4f * std::sin(frac * 30.0f);
      boiler[i] = 123.0f - 103.0f * std::exp(-frac * 5.0f) + 0.5f * std::sin(frac * 24.0f);
    }
  }
};

}  // namespace host
