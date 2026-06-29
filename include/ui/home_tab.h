#pragma once

#include <lvgl.h>

#include "core/battery.h"
#include "core/clock.h"
#include "core/machine.h"
#include "ui/screen.h"

// Home tab: built once, then updated in place from new snapshots (no rebuild).
// Top bar carries the connection status (left) and battery (right); below are
// the temperature cards and the power button. ui::App owns it.

namespace ui {

struct HomeWidgets {
  lv_obj_t* status_dot = nullptr;     // top-left
  lv_obj_t* status_label = nullptr;
  lv_obj_t* battery_label = nullptr;  // top-right
  lv_obj_t* clock_label = nullptr;    // top-right, farthest right (macOS-style)
  lv_obj_t* brew_value = nullptr;
  lv_obj_t* brew_set = nullptr;
  lv_obj_t* boiler_value = nullptr;
  lv_obj_t* boiler_set = nullptr;
  lv_obj_t* power_btn = nullptr;
  lv_obj_t* power_label = nullptr;

  // Charging animation: a looping battery-fill icon (no percent — terminal
  // voltage under charge is charger-dependent). The timer advances the frame.
  lv_timer_t* batt_timer = nullptr;
  bool charging = false;
  int charge_frame = 0;
};

// Build the Home widgets into `parent`, sized for `screen` (values are set by a
// following update_home call).
void build_home_tab(lv_obj_t* parent, const ScreenProfile& screen, HomeWidgets& out);

// Apply machine + battery state to the built widgets (live update, no rebuild).
void update_home(HomeWidgets& w, const core::MachineSnapshot& state,
                 const core::BatteryState& battery, const core::WallTime& clock,
                 bool clock_24h);

}  // namespace ui
