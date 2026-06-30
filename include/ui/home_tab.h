#pragma once

#include <lvgl.h>

#include "core/battery.h"
#include "core/brew.h"
#include "core/clock.h"
#include "core/machine.h"
#include "core/scale.h"
#include "ui/screen.h"

// Home tab: built once, then updated in place from new snapshots (no rebuild).
// Top bar carries the connection status (left) and battery (right). Below, the
// layout depends on whether a scale is configured (scale_enabled, decided at
// build time):
//   - no scale: the two big temperature cards + power button (the classic Home).
//   - scale:    smaller brew/boiler temps + the live scale (weight, target, tare,
//               paddle status), plus a flow-rate graph on the larger screens.
// ui::App owns it and rebuilds when a scale is paired/forgotten.

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
  // Inline set-point steppers flanking the set label (large screens, no-scale
  // layout only; null otherwise). Wired to the same brew_adjust/boiler_adjust.
  lv_obj_t* brew_minus = nullptr;
  lv_obj_t* brew_plus = nullptr;
  lv_obj_t* boiler_minus = nullptr;
  lv_obj_t* boiler_plus = nullptr;
  lv_obj_t* power_btn = nullptr;
  lv_obj_t* power_label = nullptr;

  // Scale-aware widgets (null unless scale_enabled):
  bool scale_enabled = false;
  lv_obj_t* scale_weight = nullptr;   // big live weight
  lv_obj_t* scale_target = nullptr;   // "/ 36 g" target sub
  lv_obj_t* tare_btn = nullptr;
  lv_obj_t* tare_label = nullptr;
  lv_obj_t* paddle_pill = nullptr;    // paddle/brew status chip (bg = state color)
  lv_obj_t* paddle_label = nullptr;
  lv_obj_t* flow_chart = nullptr;     // flow-rate graph (large screens only)
  lv_chart_series_t* flow_series = nullptr;

  // Charging animation: a looping battery-fill icon (no percent — terminal
  // voltage under charge is charger-dependent). The timer advances the frame.
  lv_timer_t* batt_timer = nullptr;
  bool charging = false;
  int charge_frame = 0;
};

// Build the Home widgets into `parent`, sized for `screen`. `scale_enabled`
// selects the scale-aware layout. Values are set by a following update_home call.
void build_home_tab(lv_obj_t* parent, const ScreenProfile& screen, bool scale_enabled,
                    HomeWidgets& out);

// Apply machine + battery + scale state to the built widgets (live, no rebuild).
void update_home(HomeWidgets& w, const core::MachineSnapshot& state,
                 const core::BatteryState& battery, const core::WallTime& clock,
                 bool clock_24h, bool fahrenheit, const core::ScaleSnapshot& scale,
                 const core::BrewSnapshot& brew);

}  // namespace ui
