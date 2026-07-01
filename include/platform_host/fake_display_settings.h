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
  bool perf_overlay() const override { return perf_overlay_; }
  void set_perf_overlay(bool on) override { perf_overlay_ = on; }

 private:
  int brightness_ = 80;
  int theme_ = 0;
  bool fahrenheit_ = false;
  bool drop_negative_flow_ = true;
  bool scope_graph_ = false;
  bool perf_overlay_ = false;
};

}  // namespace host
