#include "platform_esp32/schedule_settings.h"

#include "platform_esp32/config.h"

namespace platform {

core::ScheduleConfig ScheduleSettings::config() const {
  core::ScheduleConfig c;  // compiled defaults for every key not yet written
  c.enabled = config_.schedule_enabled();
  c.same_every_day = config_.schedule_same_daily();
  c.warmup_enabled = config_.schedule_warmup();
  int warm = config_.schedule_warmup_min();
  if (warm < 0) warm = 0;
  if (warm > core::kWarmupMaxMin) warm = core::kWarmupMaxMin;
  c.warmup_min = static_cast<uint8_t>(warm);
  int32_t packed = 0;
  if (config_.schedule_day_packed(-1, packed)) c.daily = core::unpack_day(packed);
  for (int d = 0; d < 7; ++d) {
    if (config_.schedule_day_packed(d, packed)) c.days[d] = core::unpack_day(packed);
  }
  return c;
}

void ScheduleSettings::set_config(const core::ScheduleConfig& c) {
  const core::ScheduleConfig was = config();
  if (c.enabled != was.enabled) config_.set_schedule_enabled(c.enabled);
  if (c.same_every_day != was.same_every_day) config_.set_schedule_same_daily(c.same_every_day);
  if (c.warmup_enabled != was.warmup_enabled) config_.set_schedule_warmup(c.warmup_enabled);
  if (c.warmup_min != was.warmup_min) config_.set_schedule_warmup_min(c.warmup_min);
  if (c.daily != was.daily) config_.set_schedule_day_packed(-1, core::pack_day(c.daily));
  for (int d = 0; d < 7; ++d) {
    if (c.days[d] != was.days[d]) config_.set_schedule_day_packed(d, core::pack_day(c.days[d]));
  }
}

bool ScheduleSettings::cloud_warning_dismissed() const {
  return config_.schedule_warning_dismissed();
}

void ScheduleSettings::set_cloud_warning_dismissed(bool on) {
  config_.set_schedule_warning_dismissed(on);
}

}  // namespace platform
