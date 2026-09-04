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

void DisplaySettings::set_screensaver(SaverMode mode) {
  // Live backlight only — the persisted preference stays what the user set.
  switch (mode) {
    case SaverMode::kDim:
      // Bouncing-logo saver: dim where the backlight can PWM; boards that can
      // only switch stay at full so the logo remains visible (Blank is the
      // style for going dark on those).
      display_.set_brightness(board::kSupportsBrightness ? 5 : 100);
      break;
    case SaverMode::kBlank:
      display_.set_brightness(0);  // true off on every backlight path
      break;
    case SaverMode::kOff:
      display_.set_brightness(board::kSupportsBrightness ? config_.brightness() : 100);
      break;
  }
}

int DisplaySettings::screensaver_style() const { return config_.screensaver_style(); }

void DisplaySettings::set_screensaver_style(int style) {
  config_.set_screensaver_style(style);
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

int DisplaySettings::ready_chime_volume() const { return config_.ready_chime_volume(); }

int DisplaySettings::ready_chime_melody() const { return config_.ready_chime_melody(); }

void DisplaySettings::set_ready_chime_melody(int melody) {
  config_.set_ready_chime_melody(melody);
}

void DisplaySettings::set_ready_chime_volume(int percent) {
  config_.set_ready_chime_volume(percent);
}

void DisplaySettings::set_perf_overlay(bool on) { config_.set_perf_overlay(on); }

}  // namespace platform
