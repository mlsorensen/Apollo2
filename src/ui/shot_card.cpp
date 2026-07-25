#include "ui/shot_card.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "ui/theme.h"
#include "ui/widgets.h"

namespace ui {

namespace {

// One value+caption stack for the hero row. Returns the value label so the
// caller can recolor it (the diff chip).
lv_obj_t* hero_stat(lv_obj_t* parent, const char* value, const char* caption,
                    const lv_font_t* value_font, const lv_font_t* cap_font) {
  lv_obj_t* stack = lv_obj_create(parent);
  lv_obj_remove_style_all(stack);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(stack, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(stack, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t* val = lv_label_create(stack);
  lv_label_set_text(val, value);
  lv_obj_set_style_text_color(val, lv_color_hex(theme::text()), 0);
  lv_obj_set_style_text_font(val, value_font, 0);

  lv_obj_t* cap = lv_label_create(stack);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, lv_color_hex(theme::muted()), 0);
  lv_obj_set_style_text_font(cap, cap_font, 0);
  return val;
}

// Nice round top-of-axis for a value (2, 4, 5, 10, 20, 30...).
float nice_ceil(float v) {
  if (v <= 0.0f) return 1.0f;
  const float steps[] = {1, 2, 3, 4, 5, 6, 8, 10, 15, 20, 30, 40, 50, 60, 80, 100};
  for (float s : steps)
    if (v <= s) return s;
  return std::ceil(v / 50.0f) * 50.0f;
}

// Post-draw painter for the graph box: grid, weight trace (left axis, accent),
// flow trace (right axis, ok-green), target line, axis labels. Draws straight
// from the ShotRecord in the event user_data — no pixel buffer, so it stays
// crisp at any size (modal, PNG export, sim).
void shot_graph_cb(lv_event_t* e) {
  auto* rec = static_cast<const core::ShotRecord*>(lv_event_get_user_data(e));
  if (rec == nullptr || rec->n_samples < 2) return;
  auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);

  lv_area_t plot;
  lv_obj_get_content_coords(obj, &plot);
  const int32_t w_px = plot.x2 - plot.x1;
  const int32_t h_px = plot.y2 - plot.y1;
  if (w_px < 10 || h_px < 10) return;

  // Axis ranges. Weight spans result and target; flow gets its own scale.
  float wmax = rec->summary.final_g;
  if (rec->summary.target_g > wmax) wmax = rec->summary.target_g;
  float fmax = 0.0f;
  for (int i = 0; i < rec->n_samples; ++i)
    if (rec->samples[i].flow_gps > fmax) fmax = rec->samples[i].flow_gps;
  wmax = nice_ceil(wmax * 1.05f);
  fmax = nice_ceil(fmax * 1.05f);
  const uint32_t t_end = rec->samples[rec->n_samples - 1].t_ms;
  if (t_end == 0) return;

  // Grid: 4 horizontal spans.
  lv_draw_line_dsc_t grid;
  lv_draw_line_dsc_init(&grid);
  grid.color = lv_color_hex(theme::scrollbar());
  grid.width = 1;
  for (int i = 0; i <= 4; ++i) {
    const int32_t y = plot.y1 + h_px * i / 4;
    grid.p1.x = plot.x1;
    grid.p1.y = y;
    grid.p2.x = plot.x2;
    grid.p2.y = y;
    lv_draw_line(layer, &grid);
  }

  // Target reference on the weight scale.
  if (rec->summary.target_g > 0.0f && rec->summary.target_g <= wmax) {
    lv_draw_line_dsc_t tl;
    lv_draw_line_dsc_init(&tl);
    tl.color = lv_color_hex(theme::warn());
    tl.width = ui::dp(2);
    tl.dash_width = ui::dp(6);
    tl.dash_gap = ui::dp(4);
    const int32_t y =
        plot.y2 - static_cast<int32_t>(rec->summary.target_g / wmax * h_px);
    tl.p1.x = plot.x1;
    tl.p1.y = y;
    tl.p2.x = plot.x2;
    tl.p2.y = y;
    lv_draw_line(layer, &tl);
  }

  // Traces. Flow first so the weight line reads on top.
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.width = ui::dp(2);
  line.round_start = 1;
  line.round_end = 1;

  auto x_at = [&](uint32_t t) {
    return plot.x1 + static_cast<int32_t>(static_cast<int64_t>(t) * w_px / t_end);
  };

  line.color = lv_color_hex(theme::ok());
  for (int i = 1; i < rec->n_samples; ++i) {
    line.p1.x = x_at(rec->samples[i - 1].t_ms);
    line.p1.y = plot.y2 -
                static_cast<int32_t>(rec->samples[i - 1].flow_gps / fmax * h_px);
    line.p2.x = x_at(rec->samples[i].t_ms);
    line.p2.y =
        plot.y2 - static_cast<int32_t>(rec->samples[i].flow_gps / fmax * h_px);
    lv_draw_line(layer, &line);
  }

  line.color = lv_color_hex(theme::accent());
  line.width = ui::dp(3);
  for (int i = 1; i < rec->n_samples; ++i) {
    float w0 = rec->samples[i - 1].weight_g;
    float w1 = rec->samples[i].weight_g;
    if (w0 < 0) w0 = 0;
    if (w1 < 0) w1 = 0;
    line.p1.x = x_at(rec->samples[i - 1].t_ms);
    line.p1.y = plot.y2 - static_cast<int32_t>(w0 / wmax * h_px);
    line.p2.x = x_at(rec->samples[i].t_ms);
    line.p2.y = plot.y2 - static_cast<int32_t>(w1 / wmax * h_px);
    lv_draw_line(layer, &line);
  }

  // Axis labels: weight (left, accent), flow (right, green), time (bottom).
  lv_draw_label_dsc_t lbl;
  lv_draw_label_dsc_init(&lbl);
  lbl.font = ui::font_dp(13);
  char buf[16];

  lbl.color = lv_color_hex(theme::accent());
  lbl.align = LV_TEXT_ALIGN_LEFT;
  std::snprintf(buf, sizeof(buf), "%dg", static_cast<int>(wmax));
  lbl.text = buf;
  lv_area_t la = {plot.x1 + ui::dp(4), plot.y1 + ui::dp(2), plot.x1 + ui::dp(60),
                  plot.y1 + ui::dp(20)};
  lv_draw_label(layer, &lbl, &la);

  lbl.color = lv_color_hex(theme::ok());
  lbl.align = LV_TEXT_ALIGN_RIGHT;
  char fbuf[16];
  std::snprintf(fbuf, sizeof(fbuf), "%dg/s", static_cast<int>(fmax));
  lbl.text = fbuf;
  la = {plot.x2 - ui::dp(70), plot.y1 + ui::dp(2), plot.x2 - ui::dp(4),
        plot.y1 + ui::dp(20)};
  lv_draw_label(layer, &lbl, &la);

  lbl.color = lv_color_hex(theme::muted());
  lbl.align = LV_TEXT_ALIGN_RIGHT;
  char tbuf[16];
  std::snprintf(tbuf, sizeof(tbuf), "%lus",
                static_cast<unsigned long>((t_end + 500) / 1000));
  lbl.text = tbuf;
  la = {plot.x2 - ui::dp(70), plot.y2 - ui::dp(20), plot.x2 - ui::dp(4),
        plot.y2 - ui::dp(2)};
  lv_draw_label(layer, &lbl, &la);
}

}  // namespace

void format_shot_datetime(char* buf, size_t n, int64_t unix_time, bool use_24h,
                          bool include_year) {
  const time_t t = static_cast<time_t>(unix_time);
  struct tm tm;
  localtime_r(&t, &tm);
  const int mon = tm.tm_mon + 1;
  if (use_24h) {
    if (include_year)
      std::snprintf(buf, n, "%d/%d/%d %d:%02d", mon, tm.tm_mday,
                    tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    else
      std::snprintf(buf, n, "%d/%d %d:%02d", mon, tm.tm_mday, tm.tm_hour,
                    tm.tm_min);
  } else {
    const int h12 = (tm.tm_hour % 12 == 0) ? 12 : tm.tm_hour % 12;
    const char* ap = tm.tm_hour < 12 ? "AM" : "PM";
    if (include_year)
      std::snprintf(buf, n, "%d/%d/%d %d:%02d %s", mon, tm.tm_mday,
                    tm.tm_year + 1900, h12, tm.tm_min, ap);
    else
      std::snprintf(buf, n, "%d/%d %d:%02d %s", mon, tm.tm_mday, h12, tm.tm_min,
                    ap);
  }
}

lv_obj_t* build_shot_card(lv_obj_t* parent, const core::ShotRecord& rec,
                          const ScreenProfile& screen, bool use_24h) {
  const bool compact = is_compact(screen);
  const bool xl = is_xl(screen);
  const lv_font_t* big = ui::font_dp(compact ? 24 : xl ? 44 : 34);
  const lv_font_t* mid = ui::font_dp(compact ? 16 : xl ? 30 : 22);
  const lv_font_t* small = ui::font_dp(compact ? 12 : xl ? 18 : 14);

  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(card, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(card, lv_color_hex(theme::card()), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, ui::dp(12), 0);
  lv_obj_set_style_pad_all(card, ui::dp(compact ? 8 : 14), 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(card, ui::dp(compact ? 4 : 8), 0);

  // Header: datetime (left), shot mode (right).
  lv_obj_t* header = lv_obj_create(card);
  lv_obj_remove_style_all(header);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(header, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_width(header, lv_pct(100));
  lv_obj_set_height(header, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  char when[40];
  format_shot_datetime(when, sizeof(when), rec.summary.unix_time, use_24h, true);
  lv_obj_t* when_lbl = lv_label_create(header);
  lv_label_set_text(when_lbl, when);
  lv_obj_set_style_text_color(when_lbl, lv_color_hex(theme::muted()), 0);
  lv_obj_set_style_text_font(when_lbl, small, 0);

  const char* mode_txt = rec.summary.wired ? "Auto shot"
                         : rec.summary.mode == core::ShotMode::kDetect
                             ? "Detected"
                             : "Manual";
  lv_obj_t* mode_lbl = lv_label_create(header);
  lv_label_set_text(mode_lbl, mode_txt);
  lv_obj_set_style_text_color(mode_lbl, lv_color_hex(theme::muted()), 0);
  lv_obj_set_style_text_font(mode_lbl, small, 0);

  // Hero metrics: result / target / diff / time / avg flow.
  lv_obj_t* hero = lv_obj_create(card);
  lv_obj_remove_style_all(hero);
  lv_obj_remove_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(hero, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_width(hero, lv_pct(100));
  lv_obj_set_height(hero, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  char b[24];
  std::snprintf(b, sizeof(b), "%.1f g", static_cast<double>(rec.summary.final_g));
  hero_stat(hero, b, "result", big, small);

  if (rec.summary.target_g > 0.0f) {
    std::snprintf(b, sizeof(b), "%.0f g", static_cast<double>(rec.summary.target_g));
    hero_stat(hero, b, "target", mid, small);

    const float diff = rec.summary.final_g - rec.summary.target_g;
    std::snprintf(b, sizeof(b), "%+.1f g", static_cast<double>(diff));
    lv_obj_t* diff_val = hero_stat(hero, b, "diff", mid, small);
    const float ad = diff < 0 ? -diff : diff;
    const uint32_t dc = ad <= 0.5f ? theme::ok() : ad <= 1.5f ? theme::warn()
                                                              : theme::alert();
    lv_obj_set_style_text_color(diff_val, lv_color_hex(dc), 0);
  }

  std::snprintf(b, sizeof(b), "%.1f s",
                static_cast<double>(rec.summary.duration_ms) / 1000.0);
  hero_stat(hero, b, "time", mid, small);

  std::snprintf(b, sizeof(b), "%.2f g/s", static_cast<double>(rec.summary.avg_gps));
  hero_stat(hero, b, "avg flow", mid, small);

  // Graph: custom-painted from the record (see shot_graph_cb).
  lv_obj_t* graph = lv_obj_create(card);
  lv_obj_remove_style_all(graph);
  lv_obj_remove_flag(graph, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(graph, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_width(graph, lv_pct(100));
  lv_obj_set_flex_grow(graph, 1);
  lv_obj_set_style_bg_color(graph, lv_color_hex(theme::bg()), 0);
  lv_obj_set_style_bg_opa(graph, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(graph, ui::dp(8), 0);
  lv_obj_set_style_pad_all(graph, ui::dp(6), 0);
  lv_obj_add_event_cb(graph, shot_graph_cb, LV_EVENT_DRAW_MAIN_END,
                      const_cast<core::ShotRecord*>(&rec));
  return card;
}

#if LV_USE_SNAPSHOT
lv_draw_buf_t* render_shot_card(const core::ShotRecord& rec, int w, int h,
                                bool use_24h) {
  // Canonical card: build on a detached screen at scale 1.0 so the export is
  // identical from every panel, restore the live scale after.
  const float live_scale = ui::scale();
  ui::set_scale(1.0f);
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_set_size(screen, w, h);
  lv_obj_set_style_bg_color(screen, lv_color_hex(theme::bg()), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(screen, ui::dp(8), 0);
  build_shot_card(screen, rec, ScreenProfile{w, h, 1.0f}, use_24h);
  lv_obj_update_layout(screen);
  lv_draw_buf_t* buf = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
  lv_obj_delete(screen);
  ui::set_scale(live_scale);
  return buf;
}
#endif

}  // namespace ui
