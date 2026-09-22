#pragma once

#include "core/schedule.h"

namespace platform {

class Config;

// Implements core::ISchedule over NVS (Config). config() reads every key on
// each call, so the UI reads it once at build and keeps its own copy;
// set_config() writes only the keys that changed, so a slider release costs
// one NVS write, not twelve.
class ScheduleSettings : public core::ISchedule {
 public:
  explicit ScheduleSettings(Config& config) : config_(config) {}

  core::ScheduleConfig config() const override;
  void set_config(const core::ScheduleConfig& c) override;
  bool cloud_warning_dismissed() const override;
  void set_cloud_warning_dismissed(bool on) override;

 private:
  Config& config_;
};

}  // namespace platform
