#pragma once

#include "core/display_settings.h"

// Host stand-in so the Display settings render in the simulator.

namespace host {

class FakeDisplaySettings : public core::IDisplaySettings {
 public:
  int brightness() const override { return brightness_; }
  void set_brightness(int percent) override { brightness_ = percent; }

 private:
  int brightness_ = 80;
};

}  // namespace host
