#include "platform_esp32/display_settings.h"

#include "platform_esp32/board_config.h"
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

bool DisplaySettings::supports_brightness() const { return board::kSupportsBrightness; }

int DisplaySettings::theme() const { return config_.theme(); }

void DisplaySettings::set_theme(int index) { config_.set_theme(index); }  // UI applies it

bool DisplaySettings::use_fahrenheit() const { return config_.use_fahrenheit(); }

void DisplaySettings::set_use_fahrenheit(bool on) { config_.set_use_fahrenheit(on); }

}  // namespace platform
