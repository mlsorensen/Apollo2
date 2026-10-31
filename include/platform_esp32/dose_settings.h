#pragma once

#include "core/dose_settings.h"

namespace platform {

class Config;

// Implements core::IDoseSettings over NVS (Config): the Record dose switch
// and the three container weights. Mirrors ScheduleSettings.
class DoseSettings : public core::IDoseSettings {
 public:
  explicit DoseSettings(Config& config) : config_(config) {}

  core::DoseConfig config() const override;
  void set_enabled(bool on) override;
  void set_container(int index, float grams) override;

 private:
  Config& config_;
};

}  // namespace platform
