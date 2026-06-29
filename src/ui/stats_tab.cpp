#include "ui/stats_tab.h"

#include "ui/theme.h"
#include "ui/widgets.h"

namespace ui {

namespace {

// A full-width container that fills the remaining height below the selector.
lv_obj_t* make_box(lv_obj_t* parent) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_height(box, LV_SIZE_CONTENT);  // TEST: was flex_grow 1
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, 6, 0);
  return box;
}

}  // namespace

void build_stats_tab(lv_obj_t* parent, const ScreenProfile& screen, StatsWidgets& out) {
  const bool compact = is_compact(screen);
  const bool xl = is_xl(screen);
  const lv_font_t* font =
      compact ? &lv_font_montserrat_14 : xl ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
  const lv_font_t* big =
      compact ? &lv_font_montserrat_20 : xl ? &lv_font_montserrat_48 : &lv_font_montserrat_28;

  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, compact ? 8 : xl ? 24 : 16, 0);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 8, 0);

  // --- Segmented selector: Brew / Boiler / Info ---------------------------
  lv_obj_t* seg_row = lv_obj_create(parent);
  lv_obj_remove_style_all(seg_row);
  lv_obj_set_width(seg_row, lv_pct(100));
  lv_obj_set_height(seg_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(seg_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(seg_row, 6, 0);

  const char* labels[kStatsCount] = {"Brew", "Boiler", "Info"};
  for (int i = 0; i < kStatsCount; ++i) {
    out.seg[i] = ui::make_button(seg_row);
    lv_obj_set_flex_grow(out.seg[i], 1);
    lv_obj_set_style_radius(out.seg[i], 8, 0);
    lv_obj_t* l = lv_label_create(out.seg[i]);
    lv_label_set_text(l, labels[i]);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
  }

  (void)big;

  // --- Graph view (Brew/Boiler): a flat vertical stack ---------------------
  out.graph_box = make_box(parent);

  out.title = lv_label_create(out.graph_box);
  lv_label_set_text(out.title, "Brew");
  lv_obj_set_style_text_color(out.title, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.title, font, 0);

  out.cur_label = lv_label_create(out.graph_box);
  lv_label_set_text(out.cur_label, "--");
  lv_obj_set_style_text_color(out.cur_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.cur_label, font, 0);

  out.chart = lv_chart_create(out.graph_box);
  lv_obj_set_width(out.chart, lv_pct(100));
  lv_obj_set_flex_grow(out.chart, 1);
  lv_chart_set_type(out.chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(out.chart, kStatsPoints);
  lv_chart_set_div_line_count(out.chart, 5, 0);
  lv_obj_set_style_bg_color(out.chart, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(out.chart, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(out.chart, 0, 0);
  lv_obj_set_style_radius(out.chart, 12, 0);
  lv_obj_set_style_line_color(out.chart, lv_color_hex(ui::theme::rail()), LV_PART_MAIN);
  out.series = lv_chart_add_series(out.chart, lv_color_hex(ui::theme::accent()),
                                   LV_CHART_AXIS_PRIMARY_Y);

  out.zoom_btn = ui::make_button(out.graph_box);
  lv_obj_set_style_bg_color(out.zoom_btn, lv_color_hex(ui::theme::card()), 0);
  out.zoom_label = lv_label_create(out.zoom_btn);
  lv_label_set_text(out.zoom_label, LV_SYMBOL_LOOP "  30m");
  lv_obj_set_style_text_color(out.zoom_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.zoom_label, font, 0);
  lv_obj_center(out.zoom_label);

  // --- Info view ----------------------------------------------------------
  out.info_box = make_box(parent);
  out.info_label = lv_label_create(out.info_box);
  lv_label_set_text(out.info_label, "Connect to read device info.");
  lv_obj_set_style_text_color(out.info_label, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.info_label, font, 0);

  stats_select_section(out, kStatsBrew);
}

void stats_select_section(StatsWidgets& w, int section) {
  w.active = section;
  const bool info = (section == kStatsInfo);
  if (info) {
    lv_obj_add_flag(w.graph_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(w.info_box, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(w.graph_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.info_box, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(w.title, section == kStatsBoiler ? "Boiler" : "Brew");
  }
  for (int i = 0; i < kStatsCount; ++i) {
    if (i == section) {
      lv_obj_set_style_bg_color(w.seg[i], lv_color_hex(ui::theme::accent()), 0);
    } else {
      lv_obj_set_style_bg_color(w.seg[i], lv_color_hex(ui::theme::card()), 0);
    }
  }
}

}  // namespace ui
