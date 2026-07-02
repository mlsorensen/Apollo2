#pragma once

#include "core/clock.h"
#include "platform_esp32/config.h"

namespace platform {

// Wall-clock backed by the ESP32-S3 internal RTC. Kept alive by the LiPo while
// powered; resets on a full power-off (a board with a PCF85063 would swap in
// here behind the same core::IClock). Time is stored against a fixed recent
// date so we can tell "set" from "never set" by the year. The 12/24h display
// preference is persisted via Config.
class Clock : public core::IClock {
 public:
  explicit Clock(Config& config) : config_(config) {}

  // Seed the system clock from a persistent RTC (PCF85063) if the board has one,
  // so the time survives a full power-off. Call once after I2C (Wire) is up.
  // No-op on boards without an external RTC.
  void begin();

  core::WallTime now() const override;
  void set(int hour, int minute) override;
  void set_unix(std::time_t utc) override;
  bool use_24h() const override { return config_.clock_24h(); }
  void set_24h(bool on) override { config_.set_clock_24h(on); }

 private:
  Config& config_;
};

}  // namespace platform
