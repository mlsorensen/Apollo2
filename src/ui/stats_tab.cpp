#include "ui/stats_tab.h"

#include "ui/theme.h"
#include "ui/units.h"
#include "ui/widgets.h"

namespace ui {

namespace {

// A full-width container that fills the remaining height below the selector.
lv_obj_t* make_box(lv_obj_t* parent) {
  lv_obj_t* box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_flex_grow(box, 1);  // fill the height below the selector
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(box, ui::dp(6), 0);
  return box;
}

// Compact "time ago" for the bottom scale.
void fmt_ago(char* buf, size_t n, uint32_t secs) {
  if (secs == 0) lv_snprintf(buf, n, "now");
  else if (secs < 3600) lv_snprintf(buf, n, "%um", secs / 60);
  else lv_snprintf(buf, n, "%uh", secs / 3600);
}

// Post-draw overlay for the chart (this widget has no built-in axes): grey every
// no-data (NaN) run (leading "before recording" spans and mid-stream BLE-drop
// gaps), draw the set-point reference line, the Y temperature labels (left
// margin) and the time scale (bottom margin).
void chart_overlay_cb(lv_event_t* e) {
  auto* w = static_cast<StatsWidgets*>(lv_event_get_user_data(e));
  if (w->series == nullptr) return;
  auto* chart = static_cast<lv_obj_t*>(lv_event_get_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);

  lv_area_t plot;
  lv_obj_get_content_coords(chart, &plot);  // plot area (inside padding)
  lv_area_t obj;
  lv_obj_get_coords(chart, &obj);
  const int64_t w_px = plot.x2 - plot.x1 + 1;
  const int32_t h_px = plot.y2 - plot.y1;
  const int32_t range = (w->y_max > w->y_min) ? (w->y_max - w->y_min) : 1;
  const uint32_t cnt = lv_chart_get_point_count(chart);
  const int32_t* ys = lv_chart_get_series_y_array(chart, w->series);

  // grey contiguous no-data runs
  lv_draw_rect_dsc_t rect;
  lv_draw_rect_dsc_init(&rect);
  rect.bg_color = lv_color_hex(ui::theme::rail());
  rect.bg_opa = LV_OPA_50;
  for (uint32_t i = 0; i < cnt;) {
    if (ys[i] == LV_CHART_POINT_NONE) {
      uint32_t j = i;
      while (j < cnt && ys[j] == LV_CHART_POINT_NONE) j++;
      lv_area_t a = plot;
      a.x1 = plot.x1 + static_cast<int32_t>(i * w_px / cnt);
      a.x2 = plot.x1 + static_cast<int32_t>(j * w_px / cnt);
      lv_draw_rect(layer, &rect, &a);
      i = j;
    } else {
      i++;
    }
  }

  // set-point reference line (NaN target => skip; NaN != itself)
  if (w->target == w->target && w->target >= w->y_min && w->target <= w->y_max) {
    const int32_t y =
        plot.y2 - static_cast<int32_t>((w->target - w->y_min) * h_px / range);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(ui::theme::warn());
    line.width = ui::dp(2);
    line.p1.x = plot.x1;
    line.p1.y = y;
    line.p2.x = plot.x2;
    line.p2.y = y;
    lv_draw_line(layer, &line);
  }

  // LVGL's gridlines span the full object width (into the padding), so they run
  // through the Y-label margin. Repaint the left margin with the card colour to
  // clear them before drawing the labels.
  lv_draw_rect_dsc_t maskd;
  lv_draw_rect_dsc_init(&maskd);
  maskd.bg_color = lv_color_hex(ui::theme::card());
  maskd.bg_opa = LV_OPA_COVER;
  lv_area_t mask = {obj.x1, obj.y1, static_cast<int32_t>(plot.x1 - 1), obj.y2};
  lv_draw_rect(layer, &maskd, &mask);

  lv_draw_label_dsc_t lbl;
  lv_draw_label_dsc_init(&lbl);
  lbl.color = lv_color_hex(ui::theme::muted());
  lbl.font = ui::font_dp(14);
  char buf[8];

  // Y labels (left margin): 5 ticks, top = y_max .. bottom = y_min. The chart math
  // stays Celsius; only the label text converts to the display unit.
  lbl.align = LV_TEXT_ALIGN_RIGHT;
  for (int t = 0; t <= 4; ++t) {
    const float v = ui::temp_disp(static_cast<float>(w->y_max - range * t / 4), w->fahrenheit);
    lv_snprintf(buf, sizeof(buf), "%d", static_cast<int>(v + (v < 0 ? -0.5f : 0.5f)));
    lbl.text = buf;
    const int32_t y = plot.y1 + h_px * t / 4;
    lv_area_t la = {obj.x1 + ui::dp(2), static_cast<int32_t>(y - ui::dp(9)),
                    plot.x1 - ui::dp(4), static_cast<int32_t>(y + ui::dp(9))};
    lv_draw_label(layer, &lbl, &la);
  }

  // Time scale (bottom margin): 4 ticks, left = -window .. right = now. Anchor the
  // end labels inward (left-align the first, right-align "now") so neither clips.
  for (int t = 0; t <= 3; ++t) {
    fmt_ago(buf, sizeof(buf), static_cast<uint32_t>(w->window_s * (3 - t) / 3));
    lbl.text = buf;
    const int32_t x = plot.x1 + static_cast<int32_t>(w_px * t / 3);
    lv_area_t la;
    if (t == 0) {
      lbl.align = LV_TEXT_ALIGN_LEFT;
      la = {plot.x1, plot.y2 + ui::dp(4), plot.x1 + ui::dp(60), plot.y2 + ui::dp(20)};
    } else if (t == 3) {
      lbl.align = LV_TEXT_ALIGN_RIGHT;
      la = {plot.x2 - ui::dp(60), plot.y2 + ui::dp(4), plot.x2, plot.y2 + ui::dp(20)};
    } else {
      lbl.align = LV_TEXT_ALIGN_CENTER;
      la = {x - ui::dp(30), plot.y2 + ui::dp(4), x + ui::dp(30), plot.y2 + ui::dp(20)};
    }
    lv_draw_label(layer, &lbl, &la);
  }
}

// One key/value row for the Info table; returns the value label to fill later.
lv_obj_t* make_info_row(lv_obj_t* parent, const char* key, const lv_font_t* font,
                        bool compact) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(row, ui::dp(compact ? 6 : 10), 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(ui::theme::rail()), 0);

  lv_obj_t* key_lbl = lv_label_create(row);
  lv_label_set_text(key_lbl, key);
  lv_obj_set_style_text_color(key_lbl, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(key_lbl, font, 0);

  lv_obj_t* val = lv_label_create(row);
  lv_label_set_text(val, "-");
  lv_obj_set_style_text_color(val, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(val, font, 0);
  return val;
}

// A section header for the Info table (groups the rows under it).
void make_info_header(lv_obj_t* parent, const char* text, const lv_font_t* font) {
  lv_obj_t* h = lv_label_create(parent);
  lv_label_set_text(h, text);
  lv_obj_set_style_text_color(h, lv_color_hex(ui::theme::accent()), 0);
  lv_obj_set_style_text_font(h, font, 0);
  lv_obj_set_style_pad_top(h, ui::dp(10), 0);  // breathing room above the group
  lv_obj_set_style_pad_bottom(h, ui::dp(2), 0);
}

}  // namespace

void build_stats_tab(lv_obj_t* parent, const ScreenProfile& screen, StatsWidgets& out) {
  const bool compact = is_compact(screen);
  const bool xl = is_xl(screen);
  const lv_font_t* font = ui::font_dp(compact ? 14 : xl ? 28 : 20);

  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, ui::dp(compact ? 8 : xl ? 24 : 16), 0);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, ui::dp(8), 0);

  // --- Segmented selector: Brew / Boiler / Info ---------------------------
  lv_obj_t* seg_row = lv_obj_create(parent);
  lv_obj_remove_style_all(seg_row);
  lv_obj_set_width(seg_row, lv_pct(100));
  lv_obj_set_height(seg_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(seg_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(seg_row, ui::dp(6), 0);

  const char* labels[kStatsCount] = {"Brew", "Boiler", "Info"};
  for (int i = 0; i < kStatsCount; ++i) {
    out.seg[i] = ui::make_button(seg_row);
    lv_obj_set_flex_grow(out.seg[i], 1);
    lv_obj_set_style_radius(out.seg[i], ui::dp(8), 0);
    lv_obj_t* l = lv_label_create(out.seg[i]);
    lv_label_set_text(l, labels[i]);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
  }

  // --- Graph view (Brew/Boiler): a full-height chart with drawn axes ---------
  // The section is shown by the selector above, and the Y axis is labeled, so no
  // separate title/current-value is needed.
  out.graph_box = make_box(parent);

  out.chart = lv_chart_create(out.graph_box);
  lv_obj_set_width(out.chart, lv_pct(100));
  lv_obj_set_flex_grow(out.chart, 1);
  lv_chart_set_type(out.chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(out.chart, kStatsPoints);
  lv_chart_set_div_line_count(out.chart, 5, 0);
  lv_obj_set_style_bg_color(out.chart, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(out.chart, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(out.chart, 0, 0);
  lv_obj_set_style_radius(out.chart, ui::dp(12), 0);
  // Gridlines in scrollbar() (a mid-tone) — rail() is darker than the card bg and
  // was nearly invisible.
  lv_obj_set_style_line_color(out.chart, lv_color_hex(ui::theme::scrollbar()), LV_PART_MAIN);
  // Reserve margins for the drawn axes: left for Y temperature labels, bottom for
  // the time scale, a little top room so the top Y label isn't clipped.
  lv_obj_set_style_pad_left(out.chart, ui::dp(compact ? 34 : 48), 0);
  lv_obj_set_style_pad_bottom(out.chart, ui::dp(22), 0);
  lv_obj_set_style_pad_top(out.chart, ui::dp(10), 0);
  // The theme's series line/point sizes are DPI-fixed; thicken them on scaled
  // boards so the trace keeps its weight. Untouched at scale 1 (theme default).
  if (ui::scale() > 1.0f) {
    lv_obj_set_style_line_width(out.chart, ui::dp(3), LV_PART_ITEMS);
    lv_obj_set_style_size(out.chart, ui::dp(8), ui::dp(8), LV_PART_INDICATOR);
  }
  out.series = lv_chart_add_series(out.chart, lv_color_hex(ui::theme::accent()),
                                   LV_CHART_AXIS_PRIMARY_Y);
  // MAIN_END (not POST): draw the overlay over the series but UNDER the chart's
  // children (zoom buttons / "No data yet"), so the grey no-data shading doesn't
  // cover the buttons.
  lv_obj_add_event_cb(out.chart, chart_overlay_cb, LV_EVENT_DRAW_MAIN_END, &out);

  // Overlaid +/- to zoom the time (X) axis, tucked in the chart's top-right.
  const int zsz = ui::dp(compact ? 30 : 42);
  const lv_font_t* zglyph = ui::font_dp(compact ? 20 : 28);
  out.zoom_out = ui::make_step_button(out.chart, LV_SYMBOL_MINUS, zsz, zglyph);
  lv_obj_align(out.zoom_out, LV_ALIGN_BOTTOM_RIGHT, -(zsz + ui::dp(6)), ui::dp(-6));
  out.zoom_in = ui::make_step_button(out.chart, LV_SYMBOL_PLUS, zsz, zglyph);
  lv_obj_align(out.zoom_in, LV_ALIGN_BOTTOM_RIGHT, ui::dp(-2), ui::dp(-6));

  // Centered "No data yet" overlay, shown when the whole window is empty.
  out.empty_label = lv_label_create(out.chart);
  lv_label_set_text(out.empty_label, "No data yet");
  lv_obj_set_style_text_color(out.empty_label, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.empty_label, font, 0);
  lv_obj_center(out.empty_label);
  lv_obj_add_flag(out.empty_label, LV_OBJ_FLAG_HIDDEN);

  // --- Info view: a key/value table ------------------------------------------
  out.info_box = make_box(parent);
  lv_obj_set_style_pad_row(out.info_box, 0, 0);
  // Scroll on small screens where the rows don't all fit (e.g. the 2-inch).
  lv_obj_add_flag(out.info_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(out.info_box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(out.info_box, LV_SCROLLBAR_MODE_ON);  // always visible
  lv_obj_set_style_pad_right(out.info_box, ui::dp(6), 0);  // gutter so rows clear the bar
  lv_obj_set_style_bg_color(out.info_box, lv_color_hex(ui::theme::scrollbar()),
                            LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(out.info_box, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(out.info_box, ui::dp(5), LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(out.info_box, ui::dp(3), LV_PART_SCROLLBAR);

  // Two groups: this remote, then the machine. info_val indices stay aligned with
  // update_stats_view's vals[] (0 our FW, 1 Runtime, 2 Uptime, 3..7 Micra DIS
  // fields). The "Device" header disambiguates, so the firmware row is just
  // "Firmware".
  make_info_header(out.info_box, "Device", font);
  out.info_val[0] = make_info_row(out.info_box, "Firmware", font, compact);
  out.info_val[1] =
      make_info_row(out.info_box, LV_SYMBOL_BATTERY_2 " Runtime", font, compact);
  out.info_val[2] = make_info_row(out.info_box, "Uptime", font, compact);

  make_info_header(out.info_box, "Micra", font);
  static const char* kMicraKeys[5] = {"Manufacturer", "Model", "Serial", "Firmware",
                                      "Software"};
  for (int i = 0; i < 5; ++i)
    out.info_val[3 + i] = make_info_row(out.info_box, kMicraKeys[i], font, compact);

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
