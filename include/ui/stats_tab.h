#pragma once

#include <lvgl.h>

#include "ui/screen.h"

// Stats tab: a segmented selector (Brew / Boiler / History / Info). Brew &
// Boiler show a temperature-over-time chart (Y temperature labels, a bottom
// time scale, a set-point reference line, greyed no-data spans; tap to cycle
// the window). History is the shot log: headline metric cards + a filterable
// list of recorded shots (rows are added by App from core::IShotStore; tapping
// one opens the shot-card modal). Info shows the machine's device-information
// fields as a table. ui::App owns it — builds the frame here, switches
// sections, and feeds the views from IHistory / IShotStore.

namespace ui {

enum StatsSection {
  kStatsBrew = 0,
  kStatsBoiler,
  kStatsHistory,
  kStatsInfo,
  kStatsCount
};

constexpr int kStatsPoints = 96;   // chart resolution (buckets across the window)
constexpr int kStatsInfoRows = 8;  // device-info table rows (see kInfoKeys)

struct StatsWidgets {
  lv_obj_t* seg[kStatsCount] = {};
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
  bool fahrenheit = false;     // convert the drawn Y temperature labels

  // Info view: a key/value table (values filled from the device-info service).
  lv_obj_t* info_box = nullptr;
  lv_obj_t* info_val[kStatsInfoRows] = {nullptr, nullptr, nullptr, nullptr, nullptr};

  // History view. Headline metric cards on top (total shots, lifetime / 30-day
  // accuracy), then a filter card (wide tier only) beside the scrollable shot
  // list. App owns the data: it fills the metric labels, adds/clears list rows
  // (history_add_row) and swaps in the guidance card when there's no storage
  // or no real date to stamp shots with.
  lv_obj_t* history_box = nullptr;
  lv_obj_t* hist_stat_total = nullptr;  // value labels inside the metric cards
  lv_obj_t* hist_stat_life = nullptr;
  lv_obj_t* hist_stat_30 = nullptr;
  lv_obj_t* hist_filter_btn[3] = {};    // All / 7 days / 30 days (null on compact)
  int history_filter = 0;               // 0 = all, 1 = last 7d, 2 = last 30d
  lv_obj_t* history_list = nullptr;     // scrollable rows (opaque bg — scroll cost)
  lv_obj_t* history_content = nullptr;  // metrics + filter + list wrapper
  lv_obj_t* history_guidance = nullptr; // "insert SD / set date" card
  lv_obj_t* history_guidance_label = nullptr;
};

void build_stats_tab(lv_obj_t* parent, const ScreenProfile& screen, StatsWidgets& out);

// Show one section: graph (Brew/Boiler), history, or info; highlight its button.
void stats_select_section(StatsWidgets& w, int section);

// History list helpers (App drives them from IShotStore data). A row shows
// "when" (left) and a compact stats string (right) and is clickable; the
// returned button carries no callback — App attaches one with the shot id.
lv_obj_t* history_add_row(StatsWidgets& w, const char* when, const char* stats,
                          const ScreenProfile& screen);
void history_clear_rows(StatsWidgets& w);

// Toggle between the real content and the guidance card (nullptr text keeps
// the current guidance message).
void history_show_guidance(StatsWidgets& w, const char* text);
void history_show_content(StatsWidgets& w);

}  // namespace ui
