#pragma once

#include "core/dose.h"

// Persisted settings for the dose feature (Settings > Scale > Shot settings >
// Dose): the master switch and the stored container weights. The UI reads
// the config once at build and keeps the DoseTracker fed from its own copy;
// each setter writes one key.

namespace core {

struct DoseConfig {
  bool enabled = true;  // the whole feature: Record block, chip, ratio
  float containers[kMaxDoseContainers] = {};  // grams; 0 = slot empty
};

class IDoseSettings {
 public:
  virtual ~IDoseSettings() = default;
  virtual DoseConfig config() const = 0;
  virtual void set_enabled(bool on) = 0;
  virtual void set_container(int index, float grams) = 0;  // 0 clears the slot
};

}  // namespace core
