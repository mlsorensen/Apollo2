#pragma once

#include <lvgl.h>

#include "ui/screen.h"

// Stats tab: a segmented selector (Brew / Boiler / Info). Brew & Boiler show a
// temperature-over-time chart (Y temperature labels, a bottom time scale, a
// set-point reference line, greyed no-data spans; tap to cycle the window). Info
// shows the machine's device-information fields as a table. ui::App owns it —
// builds the frame here, switches sections, and feeds the chart from IHistory.

namespace ui {

enum StatsSection {
  kStatsBrew = 0,
  kStatsBoiler,
  kStatsInfo,
  kStatsCount
};

constexpr int kStatsPoints = 96;   // chart resolution (buckets across the window)
constexpr int kStatsInfoRows = 6;  // device-info table rows (see kInfoKeys)

struct StatsWidgets {
  lv_obj_t* seg[kStatsCount] = {nullptr, nullptr, nullptr};
  int active = kStatsBrew;

  // Graph view (Brew/Boiler): just the chart. The Y temperature labels, bottom
  // time scale, set-point line and no-data greying are drawn in an overlay; tap
  // the chart to cycle the time window.
  lv_obj_t* graph_box = nullptr;
  lv_obj_t* chart = nullptr;
  lv_chart_series_t* series = nullptr;
  lv_obj_t* zoom_in = nullptr;      // overlaid +/- to zoom the time (X) axis
  lv_obj_t* zoom_out = nullptr;
  lv_obj_t* empty_label = nullptr;  // "No data yet" overlay when the window is empty
  int zoom_idx = 0;          // index into the App's window table
  int y_min = 20;            // chart Y range; also drives the drawn Y labels
  int y_max = 100;
  uint32_t window_s = 1800;  // current time window (drives the bottom time scale)
  float target = 0.0f / 0.0f;  // set-point reference line (NaN = none)

  // Info view: a key/value table (values filled from the device-info service).
  lv_obj_t* info_box = nullptr;
  lv_obj_t* info_val[kStatsInfoRows] = {nullptr, nullptr, nullptr, nullptr, nullptr};
};

void build_stats_tab(lv_obj_t* parent, const ScreenProfile& screen, StatsWidgets& out);

// Show one section: graph (Brew/Boiler) or info, and highlight its button.
void stats_select_section(StatsWidgets& w, int section);

}  // namespace ui
