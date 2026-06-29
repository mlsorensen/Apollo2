#pragma once

#include "core/display_settings.h"

// Host stand-in so the Display settings render in the simulator.

namespace host {

class FakeDisplaySettings : public core::IDisplaySettings {
 public:
  int brightness() const override { return brightness_; }
  void set_brightness(int percent) override { brightness_ = percent; }
  int theme() const override { return theme_; }
  void set_theme(int index) override { theme_ = index; }

 private:
  int brightness_ = 80;
  int theme_ = 0;
};

}  // namespace host
