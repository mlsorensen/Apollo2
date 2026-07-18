#pragma once

#include "core/display_settings.h"

// Host stand-in so the Display settings render in the simulator.

namespace host {

class FakeDisplaySettings : public core::IDisplaySettings {
 public:
  int brightness() const override { return brightness_; }
  void set_brightness(int percent) override { brightness_ = percent; }
  bool supports_brightness() const override { return true; }
  int theme() const override { return theme_; }
  void set_theme(int index) override { theme_ = index; }
  bool use_fahrenheit() const override { return fahrenheit_; }
  void set_use_fahrenheit(bool on) override { fahrenheit_ = on; }
  bool drop_negative_flow() const override { return drop_negative_flow_; }
  void set_drop_negative_flow(bool on) override { drop_negative_flow_ = on; }
  bool scope_graph() const override { return scope_graph_; }
  void set_scope_graph(bool on) override { scope_graph_ = on; }
  int flow_smooth() const override { return flow_smooth_; }
  void set_flow_smooth(int level) override { flow_smooth_ = level; }
  bool perf_overlay() const override { return perf_overlay_; }
  void set_perf_overlay(bool on) override { perf_overlay_ = on; }
  bool click_sound() const override { return click_sound_; }
  void set_click_sound(bool on) override { click_sound_ = on; }

 private:
  int brightness_ = 80;
  int theme_ = 0;
  bool fahrenheit_ = false;
  bool drop_negative_flow_ = true;
  bool scope_graph_ = true;  // matches the device default
  int flow_smooth_ = 1;      // light
  bool perf_overlay_ = false;
  bool click_sound_ = true;
};

}  // namespace host
