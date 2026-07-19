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

int DisplaySettings::screen_timeout_min() const { return config_.screen_timeout_min(); }

void DisplaySettings::set_screen_timeout_min(int minutes) {
  config_.set_screen_timeout_min(minutes);
}

void DisplaySettings::set_screensaver(bool on) {
  // Live backlight only — the persisted preference stays what the user set.
  // On/off-only boards get 0 (backlight off) instead of a 5% dim.
  if (on) {
    display_.set_brightness(board::kSupportsBrightness ? 5 : 0);
  } else {
    display_.set_brightness(board::kSupportsBrightness ? config_.brightness() : 100);
  }
}

int DisplaySettings::theme() const { return config_.theme(); }

void DisplaySettings::set_theme(int index) { config_.set_theme(index); }  // UI applies it

bool DisplaySettings::use_fahrenheit() const { return config_.use_fahrenheit(); }

void DisplaySettings::set_use_fahrenheit(bool on) { config_.set_use_fahrenheit(on); }

bool DisplaySettings::drop_negative_flow() const { return config_.drop_negative_flow(); }

void DisplaySettings::set_drop_negative_flow(bool on) { config_.set_drop_negative_flow(on); }

bool DisplaySettings::scope_graph() const { return config_.scope_graph(); }

void DisplaySettings::set_scope_graph(bool on) { config_.set_scope_graph(on); }

int DisplaySettings::flow_smooth() const { return config_.flow_smooth(); }

void DisplaySettings::set_flow_smooth(int level) { config_.set_flow_smooth(level); }

bool DisplaySettings::perf_overlay() const { return config_.perf_overlay(); }

bool DisplaySettings::click_sound() const { return config_.click_sound(); }

void DisplaySettings::set_click_sound(bool on) { config_.set_click_sound(on); }

void DisplaySettings::set_perf_overlay(bool on) { config_.set_perf_overlay(on); }

}  // namespace platform
