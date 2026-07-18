#pragma once

#include <lvgl.h>

#include "core/battery.h"
#include "core/brew.h"
#include "core/clock.h"
#include "core/machine.h"
#include "core/network.h"
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
  lv_obj_t* status_dot = nullptr;     // top-left (compact / no-scale top bar)
  lv_obj_t* status_label = nullptr;
  lv_obj_t* battery_label = nullptr;  // top-right, or rail tray on large scale layout
  lv_obj_t* clock_label = nullptr;    // top-right, or rail tray on large scale layout
  lv_obj_t* wifi_label = nullptr;     // WiFi glyph in the tray (hidden when WiFi is off)
  bool compact = false;               // compact tray: battery shows icon only (no %), wifi inline
  // Per-device status shown in the panel headers of the large scale-aware layout
  // (null on compact / no-scale, where the single top-bar status is used instead).
  lv_obj_t* micra_status_dot = nullptr;
  lv_obj_t* micra_status_label = nullptr;
  lv_obj_t* scale_status_dot = nullptr;
  lv_obj_t* scale_status_label = nullptr;
  lv_obj_t* shot_timer_label = nullptr;  // scale panel: shot timer (scale.timer_ms)
  lv_obj_t* target_minus = nullptr;      // scale panel: target-weight steppers
  lv_obj_t* target_plus = nullptr;
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
  // Shot button under the TIMER column (large scale layout + paddle boards only):
  // toggles shot mode when idle/brewing, becomes "Reset" during review.
  lv_obj_t* shot_btn = nullptr;
  lv_obj_t* shot_btn_label = nullptr;
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
  float flow_prev_y = -1.0f;  // previous sample's row, fractional (AA); -1 = pen up
  uint32_t flow_tick = 0;    // last advance timestamp (ms); 0 = uninitialized
  uint32_t flow_accum_ms = 0;  // fractional-pixel time bank
  bool flow_blanked = false;   // true while the plot is cleared (scale disconnected)
  bool flow_drop_negative = true;  // clamp outflow (weight-decreasing) g/s to zero
  // Graph style. false = scroll (whole plot memmoves left each step, newest at the
  // right edge). true = oscilloscope: the trace is stationary and flow_cursor is a
  // write head that advances left->right and wraps, repainting only its own column
  // (+ a small gap ahead) — far cheaper and lower-tearing. In scope mode the rings
  // and pixels are indexed by screen-x with the seam just after flow_cursor.
  bool flow_scope_mode = false;
  int flow_cursor = 0;         // scope write head (screen column); unused in scroll
  // Mode toggle (top-left button) + auto-ranging Y axis. flow_mode 0 = flow (g/s),
  // 1 = weight (g). We shadow the pixels with BOTH raw quantities per column (the
  // scale gives us both every update) so toggling the unit just re-scales and
  // redraws the existing trace instead of discarding it; the active ring also lets
  // us re-scale (redraw at a new flow_ymax) when the window's max crosses a "nice"
  // threshold. flow_y* labels are updated on rescale.
  int flow_mode = 0;
  // Flow rate = trailing-window derivative over a short time-stamped weight
  // history (~1.5s): Δweight/Δtime across the window. Any real flow spans many
  // scale-resolution steps in that window (no fake zero-dips when a reading
  // repeats), and after a stop the rate ramps linearly to zero as the window
  // slides past the edge (no EMA shelf). Ring sampled every ~50ms.
  static constexpr int kFlowHistCap = 64;  // 64 x 50ms = 3.2s of history
  float flow_hist_w[kFlowHistCap] = {};
  uint32_t flow_hist_t[kFlowHistCap] = {};
  int flow_hist_n = 0;                // valid entries
  int flow_hist_head = 0;             // next write slot
  uint32_t flow_hist_last_ms = 0;     // last sample time
  float* flow_weights = nullptr;      // per-column weight (g) ring (freed by App)
  float* flow_flows = nullptr;        // per-column flow rate (g/s) ring (freed by App)
  float flow_ymax = 6.0f;             // current axis full-scale
  lv_obj_t* flow_unit_btn = nullptr;  // the g/s <-> g toggle button
  lv_obj_t* flow_unit_label = nullptr;
  lv_obj_t* flow_ylabels[3] = {nullptr, nullptr, nullptr};  // Y = max, 2max/3, max/3
  // X-axis labels. flow_xlabels are the "60s..now" age ticks (scroll only — in scope
  // mode x maps to sweep position, not age, so they'd lie). flow_xspan_label is a
  // single "60 s window" caption shown in scope mode instead.
  lv_obj_t* flow_xlabels[4] = {nullptr, nullptr, nullptr, nullptr};
  lv_obj_t* flow_xspan_label = nullptr;

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

// Build the clock/battery system tray at the bottom of the side rail (the tab
// bar). Only the large scale-aware Home drops the top bar, so this is called just
// for that case; the labels live in `out` and are updated by update_home every
// refresh, so they stay live on every tab. The tray + its spacer are plain
// objects, so the tabview (which counts/indexes tabs by button class) ignores them.
void build_rail_tray(lv_obj_t* rail, const lv_font_t* font, HomeWidgets& out);

// Compact counterpart: clock + battery pushed to the right of the bottom tab bar
// (the compact tab bar is horizontal). Same "no top bar" treatment as build_rail_tray.
void build_bottom_tray(lv_obj_t* bar, const lv_font_t* font, HomeWidgets& out);

// Apply machine + battery + scale state to the built widgets (live, no rebuild).
void update_home(HomeWidgets& w, const core::MachineSnapshot& state,
                 const core::BatteryState& battery, const core::WallTime& clock,
                 bool clock_24h, bool fahrenheit, const core::ScaleSnapshot& scale,
                 const core::BrewSnapshot& brew, core::NetState net);

// Scroll the flow strip chart left by however many pixels elapsed wall-clock time
// calls for, painting the new right-edge column(s) with the scale's current flow.
// Called every device-loop iteration (see App::pump_scale_chart); a cheap no-op
// when there's no graph or not enough time has passed for a pixel step. Time-based
// (not per-sample) scrolling keeps it smooth regardless of the sample rate.
void flow_graph_tick(HomeWidgets& w, const core::ScaleSnapshot& scale);

// Flip the graph between flow rate (g/s) and weight (g). Both quantities are kept
// per column, so the existing trace is re-scaled and redrawn in the new unit rather
// than discarded. Bound to the unit button in the graph's top-left corner.
void toggle_flow_mode(HomeWidgets& w);

// Set whether negative (outflow) g/s is clamped to zero. Clears the g/s ring, since
// its history was recorded under the previous policy; the weight ring is untouched.
void set_flow_drop_negative(HomeWidgets& w, bool on);

// Switch between scroll and oscilloscope (sweep) graph styles. The two index the
// ring/pixels differently, so this resets the plot to an empty grid and starts fresh.
void set_flow_scope_mode(HomeWidgets& w, bool on);

// Show the X-axis labels appropriate to the current style: age ticks in scroll mode,
// a single window-span caption in scope mode. Call after setting flow_scope_mode.
void apply_flow_xaxis_labels(HomeWidgets& w);

// Clear the flow plot back to an empty grid (both value rings + axis reset).
// Used at shot start so the graph records exactly one shot, and by the style
// toggles. A no-op when there's no graph on this layout. Does NOT clear the
// flow-rate history — the weight stream is continuous across a plot clear.
void reset_flow_graph(HomeWidgets& w);

// Clear the flow-rate weight history too — needed when the weight STREAM
// becomes discontinuous (a tare re-zeroes it; a disconnect ends it). The rate
// then reads 0 briefly while the window re-warms.
void reset_flow_history(HomeWidgets& w);

}  // namespace ui
