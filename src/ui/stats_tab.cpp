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

// One headline metric card for the History view: big value over a small
// caption. Returns the value label; optionally exposes the card (for tap
// handlers) and the caption label (the Total card's caption changes after a
// stats reset).
lv_obj_t* make_metric_card(lv_obj_t* parent, const char* caption,
                           const lv_font_t* value_font, const lv_font_t* cap_font,
                           lv_obj_t** out_card = nullptr,
                           lv_obj_t** out_cap = nullptr) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(card, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, ui::dp(12), 0);
  lv_obj_set_style_pad_all(card, ui::dp(10), 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t* val = lv_label_create(card);
  lv_label_set_text(val, "-");
  lv_obj_set_style_text_color(val, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(val, value_font, 0);

  lv_obj_t* cap = lv_label_create(card);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(cap, cap_font, 0);
  if (out_card != nullptr) *out_card = card;
  if (out_cap != nullptr) *out_cap = cap;
  return val;
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

  const char* labels[kStatsCount] = {"Brew", "Boiler", "History", "Info"};
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

  // --- History view: metric cards + (filters +) shot list --------------------
  out.history_box = make_box(parent);
  {
    const lv_font_t* big = ui::font_dp(compact ? 20 : xl ? 40 : 30);
    const lv_font_t* cap_font = ui::font_dp(compact ? 12 : xl ? 18 : 14);

    // Real content wrapper (hidden when guidance shows).
    out.history_content = lv_obj_create(out.history_box);
    lv_obj_remove_style_all(out.history_content);
    lv_obj_remove_flag(out.history_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(out.history_content, lv_pct(100));
    lv_obj_set_flex_grow(out.history_content, 1);
    lv_obj_set_flex_flow(out.history_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(out.history_content, ui::dp(8), 0);

    // Headline metric cards: total shots, lifetime accuracy, 30-day accuracy.
    lv_obj_t* metrics = lv_obj_create(out.history_content);
    lv_obj_remove_style_all(metrics);
    lv_obj_remove_flag(metrics, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(metrics, lv_pct(100));
    lv_obj_set_height(metrics, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(metrics, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(metrics, ui::dp(8), 0);
    // Lifetime metrics — NOT affected by the month filter below; the captions
    // say so explicitly.
    out.hist_stat_total =
        make_metric_card(metrics, compact ? "Shots" : "Total shots", big, cap_font,
                         &out.hist_metric_cards[0], &out.hist_stat_total_cap);
    out.hist_stat_life = make_metric_card(
        metrics, compact ? "Lifetime acc" : "Lifetime accuracy", big, cap_font,
        &out.hist_metric_cards[1]);
    out.hist_stat_30 = make_metric_card(
        metrics, compact ? "30-day acc" : "30-day accuracy", big, cap_font,
        &out.hist_metric_cards[2]);
    // Tapping a metric card offers a (non-destructive) stats reset — App
    // wires the callback + confirm modal.
    for (lv_obj_t* c : out.hist_metric_cards)
      lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);

    // Bottom: filter card (wide/xl only) + the scrollable list.
    lv_obj_t* bottom = lv_obj_create(out.history_content);
    lv_obj_remove_style_all(bottom);
    lv_obj_remove_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(bottom, lv_pct(100));
    lv_obj_set_flex_grow(bottom, 1);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bottom, ui::dp(8), 0);

    if (!compact) {
      lv_obj_t* filter = lv_obj_create(bottom);
      lv_obj_remove_style_all(filter);
      lv_obj_remove_flag(filter, LV_OBJ_FLAG_SCROLLABLE);
      // 1:2 with the list so the filter's edge lines up with the first
      // hero card's edge above it.
      lv_obj_set_flex_grow(filter, 1);
      lv_obj_set_height(filter, lv_pct(100));
      lv_obj_set_style_bg_color(filter, lv_color_hex(ui::theme::card()), 0);
      lv_obj_set_style_bg_opa(filter, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(filter, ui::dp(12), 0);
      lv_obj_set_style_pad_all(filter, ui::dp(10), 0);
      lv_obj_set_style_pad_row(filter, ui::dp(8), 0);
      lv_obj_set_flex_flow(filter, LV_FLEX_FLOW_COLUMN);

      lv_obj_t* fl = lv_label_create(filter);
      lv_label_set_text(fl, "Show");
      lv_obj_set_style_text_color(fl, lv_color_hex(ui::theme::muted()), 0);
      lv_obj_set_style_text_font(fl, cap_font, 0);

      // The month buttons live in their own scrollable column (a long history
      // has many months); App fills it — "All" + each month that has shots.
      out.history_filter_list = lv_obj_create(filter);
      lv_obj_remove_style_all(out.history_filter_list);
      lv_obj_set_width(out.history_filter_list, lv_pct(100));
      lv_obj_set_flex_grow(out.history_filter_list, 1);
      lv_obj_set_flex_flow(out.history_filter_list, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(out.history_filter_list, ui::dp(8), 0);
      lv_obj_add_flag(out.history_filter_list, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scroll_dir(out.history_filter_list, LV_DIR_VER);
      lv_obj_set_scrollbar_mode(out.history_filter_list, LV_SCROLLBAR_MODE_AUTO);
      // Opaque bg (the parent card's color) — transparent scrollables force
      // full-underlayer repaints per frame (the settings-page lesson).
      lv_obj_set_style_bg_color(out.history_filter_list,
                                lv_color_hex(ui::theme::card()), 0);
      lv_obj_set_style_bg_opa(out.history_filter_list, LV_OPA_COVER, 0);
    }

    // The list scrolls; opaque bg — a transparent scrollable forces LVGL to
    // re-render every layer beneath it each frame (the settings-page lesson).
    out.history_list = lv_obj_create(bottom);
    lv_obj_remove_style_all(out.history_list);
    lv_obj_set_flex_grow(out.history_list, compact ? 1 : 2);
    lv_obj_set_height(out.history_list, lv_pct(100));
    lv_obj_set_style_bg_color(out.history_list, lv_color_hex(ui::theme::card()), 0);
    lv_obj_set_style_bg_opa(out.history_list, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(out.history_list, ui::dp(12), 0);
    lv_obj_set_style_pad_all(out.history_list, ui::dp(compact ? 6 : 10), 0);
    lv_obj_set_flex_flow(out.history_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(out.history_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(out.history_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(out.history_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(out.history_list, lv_color_hex(ui::theme::scrollbar()),
                              LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(out.history_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(out.history_list, ui::dp(5), LV_PART_SCROLLBAR);

    // Footer row: browse URL centered, capacity right (alert "FULL" when
    // saves are dropped). Plain container + aligned children — flex can't
    // center one child independently of the other.
    lv_obj_t* footer = lv_obj_create(out.history_content);
    lv_obj_remove_style_all(footer);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(footer, lv_pct(100));
    lv_obj_set_height(footer, ui::dp(compact ? 16 : 20));

    out.history_url_label = lv_label_create(footer);
    lv_label_set_text(out.history_url_label, "");
    lv_obj_set_style_text_color(out.history_url_label,
                                lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(out.history_url_label, cap_font, 0);
    lv_obj_align(out.history_url_label, LV_ALIGN_CENTER, 0, 0);

    out.history_sd_label = lv_label_create(footer);
    lv_label_set_text(out.history_sd_label, "");
    lv_obj_set_style_text_color(out.history_sd_label,
                                lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(out.history_sd_label, cap_font, 0);
    lv_obj_align(out.history_sd_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // Guidance card (no SD / no date): swapped in for the content wrapper.
    out.history_guidance = lv_obj_create(out.history_box);
    lv_obj_remove_style_all(out.history_guidance);
    lv_obj_remove_flag(out.history_guidance, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(out.history_guidance, lv_pct(100));
    lv_obj_set_flex_grow(out.history_guidance, 1);
    lv_obj_set_style_bg_color(out.history_guidance, lv_color_hex(ui::theme::card()), 0);
    lv_obj_set_style_bg_opa(out.history_guidance, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(out.history_guidance, ui::dp(12), 0);
    lv_obj_set_style_pad_all(out.history_guidance, ui::dp(16), 0);
    lv_obj_set_flex_flow(out.history_guidance, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(out.history_guidance, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(out.history_guidance, ui::dp(10), 0);

    lv_obj_t* icon = lv_label_create(out.history_guidance);
    lv_label_set_text(icon, LV_SYMBOL_SD_CARD);
    lv_obj_set_style_text_color(icon, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(icon, ui::font_dp(compact ? 24 : 36), 0);

    out.history_guidance_label = lv_label_create(out.history_guidance);
    lv_label_set_text(out.history_guidance_label, "");
    lv_obj_set_width(out.history_guidance_label, lv_pct(90));
    lv_label_set_long_mode(out.history_guidance_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(out.history_guidance_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(out.history_guidance_label,
                                lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(out.history_guidance_label, font, 0);
    lv_obj_add_flag(out.history_guidance, LV_OBJ_FLAG_HIDDEN);
  }

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
  // update_stats_view's vals[] (0 our FW, 1 Runtime, 2 Uptime, 3 IP, 4..8 Micra
  // DIS fields). The "Device" header disambiguates, so the firmware row is just
  // "Firmware".
  make_info_header(out.info_box, "Device", font);
  out.info_val[0] = make_info_row(out.info_box, "Firmware", font, compact);
  out.info_val[1] =
      make_info_row(out.info_box, LV_SYMBOL_BATTERY_2 " Runtime", font, compact);
  out.info_val[2] = make_info_row(out.info_box, "Uptime", font, compact);
  out.info_val[3] = make_info_row(out.info_box, "IP address", font, compact);

  make_info_header(out.info_box, "Micra", font);
  static const char* kMicraKeys[5] = {"Manufacturer", "Model", "Serial", "Firmware",
                                      "Software"};
  for (int i = 0; i < 5; ++i)
    out.info_val[4 + i] = make_info_row(out.info_box, kMicraKeys[i], font, compact);

  stats_select_section(out, kStatsBrew);
}

void stats_select_section(StatsWidgets& w, int section) {
  w.active = section;
  // One content box per kind of section; show exactly the active one.
  lv_obj_t* boxes[3] = {w.graph_box, w.history_box, w.info_box};
  lv_obj_t* show = (section == kStatsInfo)      ? w.info_box
                   : (section == kStatsHistory) ? w.history_box
                                                : w.graph_box;
  for (lv_obj_t* box : boxes) {
    if (box == show) {
      lv_obj_remove_flag(box, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    }
  }
  for (int i = 0; i < kStatsCount; ++i) {
    if (i == section) {
      lv_obj_set_style_bg_color(w.seg[i], lv_color_hex(ui::theme::accent()), 0);
    } else {
      lv_obj_set_style_bg_color(w.seg[i], lv_color_hex(ui::theme::card()), 0);
    }
  }
}

lv_obj_t* history_add_row(StatsWidgets& w, const char* when, const char* result,
                          const char* diff, uint32_t diff_color,
                          const char* duration, const ScreenProfile& screen) {
  const bool compact = is_compact(screen);
  const bool xl = is_xl(screen);
  const lv_font_t* font = ui::font_dp(compact ? 13 : xl ? 24 : 17);

  lv_obj_t* row = lv_obj_create(w.history_list);
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(row, ui::dp(compact ? 7 : 10), 0);
  lv_obj_set_style_pad_hor(row, ui::dp(4), 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(ui::theme::rail()), 0);
  // Row must stay opaque for cheap list scrolling, matching the list card bg.
  lv_obj_set_style_bg_color(row, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(ui::theme::rail()), LV_STATE_PRESSED);

  lv_obj_t* when_lbl = lv_label_create(row);
  lv_label_set_text(when_lbl, when);
  lv_obj_set_style_text_color(when_lbl, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(when_lbl, font, 0);

  lv_obj_t* right = lv_obj_create(row);
  lv_obj_remove_style_all(right);
  lv_obj_remove_flag(right, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(right, LV_OBJ_FLAG_EVENT_BUBBLE);  // row tap works everywhere
  lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(right, ui::dp(6), 0);

  lv_obj_t* result_lbl = lv_label_create(right);
  lv_label_set_text(result_lbl, result);
  lv_obj_set_style_text_color(result_lbl, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(result_lbl, font, 0);

  if (diff != nullptr && diff[0] != '\0') {
    lv_obj_t* diff_lbl = lv_label_create(right);
    lv_label_set_text(diff_lbl, diff);
    lv_obj_set_style_text_color(diff_lbl, lv_color_hex(diff_color), 0);
    lv_obj_set_style_text_font(diff_lbl, font, 0);
  }

  lv_obj_t* dur_lbl = lv_label_create(right);
  lv_label_set_text(dur_lbl, duration);
  lv_obj_set_style_text_color(dur_lbl, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(dur_lbl, font, 0);
  return row;
}

void history_clear_rows(StatsWidgets& w) {
  if (w.history_list != nullptr) lv_obj_clean(w.history_list);
}

lv_obj_t* history_add_filter_button(StatsWidgets& w, const char* label,
                                    bool active, const ScreenProfile& screen) {
  if (w.history_filter_list == nullptr) return nullptr;
  const bool xl = is_xl(screen);
  lv_obj_t* btn = ui::make_button(w.history_filter_list);
  lv_obj_set_width(btn, lv_pct(100));
  lv_obj_set_style_radius(btn, ui::dp(8), 0);
  lv_obj_set_style_bg_color(
      btn, lv_color_hex(active ? ui::theme::accent() : ui::theme::rail()), 0);
  lv_obj_t* l = lv_label_create(btn);
  lv_label_set_text(l, label);
  lv_obj_set_style_text_font(l, ui::font_dp(xl ? 24 : 17), 0);
  lv_obj_center(l);
  return btn;
}

void history_clear_filter_buttons(StatsWidgets& w) {
  if (w.history_filter_list != nullptr) lv_obj_clean(w.history_filter_list);
}

void history_show_guidance(StatsWidgets& w, const char* text) {
  if (w.history_guidance == nullptr) return;
  if (text != nullptr) lv_label_set_text(w.history_guidance_label, text);
  lv_obj_add_flag(w.history_content, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(w.history_guidance, LV_OBJ_FLAG_HIDDEN);
}

void history_show_content(StatsWidgets& w) {
  if (w.history_guidance == nullptr) return;
  lv_obj_add_flag(w.history_guidance, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(w.history_content, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace ui
