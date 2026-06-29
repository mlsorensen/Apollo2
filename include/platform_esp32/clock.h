#pragma once

#include "core/clock.h"

namespace platform {

// Wall-clock backed by the ESP32-S3 internal RTC. Kept alive by the LiPo while
// powered; resets on a full power-off (a board with a PCF85063 would swap in
// here behind the same core::IClock). Time is stored against a fixed recent
// date so we can tell "set" from "never set" by the year.
class Clock : public core::IClock {
 public:
  core::WallTime now() const override;
  void set(int hour, int minute) override;
};

}  // namespace platform
