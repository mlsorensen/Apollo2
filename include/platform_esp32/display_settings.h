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
  bool supports_brightness() const override;
  int theme() const override;
  void set_theme(int index) override;
  bool use_fahrenheit() const override;
  void set_use_fahrenheit(bool on) override;
  bool drop_negative_flow() const override;
  void set_drop_negative_flow(bool on) override;
  bool scope_graph() const override;
  void set_scope_graph(bool on) override;
  int flow_smooth() const override;
  void set_flow_smooth(int level) override;
  bool perf_overlay() const override;
  void set_perf_overlay(bool on) override;
  bool click_sound() const override;
  void set_click_sound(bool on) override;

 private:
  Display& display_;
  Config& config_;
};

}  // namespace platform
