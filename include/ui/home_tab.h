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
  // Flow-rate strip chart (large screens only). A self-managed lv_canvas: newest
  // sample at the right, the plot scrolls right->left by wall-clock time (see
  // flow_graph_tick) so older data trails off the left. Each step memmoves the
  // buffer left and paints one new column — cheap vs re-rendering the whole
  // polyline. We write flow_buf (an lv_malloc'd RGB565 bitmap, freed by App on
  // rebuild/teardown) DIRECTLY, not via lv_canvas_set_px (far too slow per-pixel).
  // flow_stride is the buffer's row pitch in uint16 units.
  lv_obj_t* flow_canvas = nullptr;
  uint16_t* flow_buf = nullptr;
  int flow_w = 0;             // plot width/height in px (canvas size)
  int flow_h = 0;
  int flow_stride = 0;        // buffer row pitch in pixels (>= flow_w)
  int flow_prev_y = -1;      // previous sample's row (continuous line; -1 = pen up)
  uint32_t flow_tick = 0;    // last advance timestamp (ms); 0 = uninitialized
  uint32_t flow_accum_ms = 0;  // fractional-pixel time bank
  // Mode toggle (top-left button) + auto-ranging Y axis. flow_mode 0 = flow (g/s),
  // 1 = weight (g). flow_values shadows the pixels with the raw plotted value per
  // column so we can re-scale (redraw the whole plot at a new flow_ymax) when the
  // window's max crosses a "nice" threshold. flow_y* labels are updated on rescale.
  int flow_mode = 0;
  float* flow_values = nullptr;       // ring of raw values (freed by App)
  float flow_ymax = 6.0f;             // current axis full-scale
  lv_obj_t* flow_unit_btn = nullptr;  // the g/s <-> g toggle button
  lv_obj_t* flow_unit_label = nullptr;
  lv_obj_t* flow_ylabels[3] = {nullptr, nullptr, nullptr};  // Y = max, 2max/3, max/3

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

// Scroll the flow strip chart left by however many pixels elapsed wall-clock time
// calls for, painting the new right-edge column(s) with the scale's current flow.
// Called every device-loop iteration (see App::pump_scale_chart); a cheap no-op
// when there's no graph or not enough time has passed for a pixel step. Time-based
// (not per-sample) scrolling keeps it smooth regardless of the sample rate.
void flow_graph_tick(HomeWidgets& w, const core::ScaleSnapshot& scale);

// Flip the graph between flow rate (g/s) and weight (g), clearing the plot and
// resetting the auto-ranging axis to the new mode's default. Bound to the unit
// button in the graph's top-left corner.
void toggle_flow_mode(HomeWidgets& w);

}  // namespace ui
