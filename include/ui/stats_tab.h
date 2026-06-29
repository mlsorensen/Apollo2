#pragma once

#include <lvgl.h>

#include "ui/screen.h"

// Stats tab: a segmented selector (Brew / Boiler / Info). Brew & Boiler show a
// temperature-over-time chart with a zoom control; Info shows the machine's
// device-information fields. ui::App owns it — builds the frame here, switches
// sections, and feeds the chart from core::IHistory.

namespace ui {

enum StatsSection {
  kStatsBrew = 0,
  kStatsBoiler,
  kStatsInfo,
  kStatsCount
};

constexpr int kStatsPoints = 96;  // chart resolution (buckets across the window)

struct StatsWidgets {
  lv_obj_t* seg[kStatsCount] = {nullptr, nullptr, nullptr};
  int active = kStatsBrew;

  // Graph view (Brew/Boiler): title + latest value, the chart, and a zoom cycler.
  lv_obj_t* graph_box = nullptr;
  lv_obj_t* title = nullptr;
  lv_obj_t* cur_label = nullptr;
  lv_obj_t* chart = nullptr;
  lv_chart_series_t* series = nullptr;
  lv_obj_t* zoom_btn = nullptr;
  lv_obj_t* zoom_label = nullptr;
  int zoom_idx = 0;  // index into the App's window table

  // Info view: a single multi-line label (filled from the device info service).
  lv_obj_t* info_box = nullptr;
  lv_obj_t* info_label = nullptr;
};

void build_stats_tab(lv_obj_t* parent, const ScreenProfile& screen, StatsWidgets& out);

// Show one section: graph (Brew/Boiler) or info, and highlight its button.
void stats_select_section(StatsWidgets& w, int section);

}  // namespace ui
