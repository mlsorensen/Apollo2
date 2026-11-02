#pragma once

#include "core/dose.h"

// Persisted settings for the dose feature (Settings > Scale > Shot settings >
// Dose): the master switch and the stored container weights. The UI reads
// the config once at build and keeps the DoseTracker fed from its own copy;
// each setter writes one key.

namespace core {

struct DoseConfig {
  bool enabled = false;  // the whole feature: Record block, chip, ratio (opt-in)
  float containers[kMaxDoseContainers] = {};  // grams; 0 = slot empty
  float default_dose = 0.0f;       // grams; 0 = the Default dose switch is off
  float default_dose_last = 0.0f;  // the value the switch restores (0 = never set)
};
constexpr float kDefaultDoseMinG = 5.0f;   // the stepper's range, 0.5 g steps
constexpr float kDefaultDoseMaxG = 30.0f;
constexpr float kDefaultDoseInitialG = 18.0f;  // the first value the switch starts at

class IDoseSettings {
 public:
  virtual ~IDoseSettings() = default;
  virtual DoseConfig config() const = 0;
  virtual void set_enabled(bool on) = 0;
  virtual void set_container(int index, float grams) = 0;  // 0 clears the slot
  virtual void set_default_dose(float grams) = 0;         // 0 = off; a value is remembered as default_dose_last
};

}  // namespace core
