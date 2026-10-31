#pragma once

#include "core/dose_settings.h"

namespace host {

// Sim stand-in: a portafilter and the owner's dosing cup stored, so the
// Shot settings render shows filled rows and the Record poses have a
// container to subtract.
class FakeDoseSettings : public core::IDoseSettings {
 public:
  FakeDoseSettings() {
    cfg_.containers[0] = 480.0f;
    cfg_.containers[1] = 111.0f;
  }
  core::DoseConfig config() const override { return cfg_; }
  void set_enabled(bool on) override { cfg_.enabled = on; }
  void set_container(int index, float grams) override {
    if (index >= 0 && index < core::kMaxDoseContainers) cfg_.containers[index] = grams;
  }

 private:
  core::DoseConfig cfg_;
};

}  // namespace host
