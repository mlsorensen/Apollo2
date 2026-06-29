#pragma once

#include "core/display_settings.h"

namespace platform {

class Display;
class Config;

// Implements core::IDisplaySettings: applies brightness to the backlight (via
// Display) and persists it to NVS (via Config). Keeps Display free of NVS.
class DisplaySettings : public core::IDisplaySettings {
 public:
  DisplaySettings(Display& display, Config& config);

  int brightness() const override;
  void set_brightness(int percent) override;
  int theme() const override;
  void set_theme(int index) override;

 private:
  Display& display_;
  Config& config_;
};

}  // namespace platform
