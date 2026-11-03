#include "platform_esp32/dose_settings.h"

#include "platform_esp32/config.h"

namespace platform {

core::DoseConfig DoseSettings::config() const {
  core::DoseConfig c;
  c.enabled = config_.dose_enabled();
  for (int i = 0; i < core::kMaxDoseContainers; ++i) c.containers[i] = config_.dose_container(i);
  c.default_dose = config_.default_dose();
  c.default_dose_last = config_.default_dose_last();
  return c;
}

void DoseSettings::set_enabled(bool on) { config_.set_dose_enabled(on); }

void DoseSettings::set_container(int index, float grams) {
  config_.set_dose_container(index, grams);
}

void DoseSettings::set_default_dose(float grams) { config_.set_default_dose(grams); }

}  // namespace platform
