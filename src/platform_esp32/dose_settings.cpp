#include "platform_esp32/dose_settings.h"

#include "platform_esp32/config.h"

namespace platform {

core::DoseConfig DoseSettings::config() const {
  core::DoseConfig c;
  c.enabled = config_.dose_enabled();
  for (int i = 0; i < core::kMaxDoseContainers; ++i) c.containers[i] = config_.dose_container(i);
  return c;
}

void DoseSettings::set_enabled(bool on) { config_.set_dose_enabled(on); }

void DoseSettings::set_container(int index, float grams) {
  config_.set_dose_container(index, grams);
}

}  // namespace platform
