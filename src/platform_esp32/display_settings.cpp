#include "platform_esp32/display_settings.h"

#include "platform_esp32/config.h"
#include "platform_esp32/display.h"

namespace platform {

DisplaySettings::DisplaySettings(Display& display, Config& config)
    : display_(display), config_(config) {}

int DisplaySettings::brightness() const { return config_.brightness(); }

void DisplaySettings::set_brightness(int percent) {
  display_.set_brightness(percent);  // live
  config_.set_brightness(percent);   // persist
}

}  // namespace platform
