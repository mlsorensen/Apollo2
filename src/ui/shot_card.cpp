#include "ui/shot_card.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "ui/theme.h"
#include "ui/widgets.h"

namespace ui {

namespace {

// One stat column for the hero row: value over an uppercase micro-caption.
// Every column uses the SAME value font and grows equally, so the five values
// sit on one shared baseline in an even grid — targeted or not, every card
// lays out identically. Returns the value label for recoloring (diff).
lv_obj_t* hero_stat(lv_obj_t* parent, const char* value, const char* caption,
                    const lv_font_t* value_font, const lv_font_t* cap_font) {
  lv_obj_t* stack = lv_obj_create(parent);
  lv_obj_remove_style_all(stack);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(stack, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_height(stack, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(stack, 1);
  lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(stack, ui::dp(2), 0);

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

// Post-draw painter for the graph box: ONE combined plot — weight (accent,
// left axis) and flow (green, right axis) over shared time, with quarter
// gridlines and tick values on both sides so the Y coordinates are readable.
// Each side gets a small color-keyed legend; the tick text itself stays in
// muted text tokens. Draws straight from the ShotRecord in the event
// user_data — no pixel buffer, so it stays crisp at any size (modal, PNG
// export, sim).
void shot_graph_cb(lv_event_t* e) {
  auto* rec = static_cast<const core::ShotRecord*>(lv_event_get_user_data(e));
  if (rec == nullptr || rec->n_samples < 2) return;
  auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
  lv_layer_t* layer = lv_event_get_layer(e);

  lv_area_t box;
  lv_obj_get_content_coords(obj, &box);
  if (box.x2 - box.x1 < 10 || box.y2 - box.y1 < 10) return;
  const uint32_t t_end = rec->samples[rec->n_samples - 1].t_ms;
  if (t_end == 0) return;

  // Margins: legend strip on top, tick columns left/right, time strip below.
  const int32_t legend_h = ui::dp(26);
  const int32_t axis_w = ui::dp(46);
  const int32_t time_strip = ui::dp(20);
  const lv_area_t plot = {box.x1 + axis_w, box.y1 + legend_h, box.x2 - axis_w,
                          box.y2 - time_strip};
  const int32_t w_px = plot.x2 - plot.x1;
  const int32_t h_px = plot.y2 - plot.y1;

  // Ranges: weight spans result and target; flow gets its own scale.
  float wmax = rec->summary.final_g;
  if (rec->summary.target_g > wmax) wmax = rec->summary.target_g;
  wmax = nice_ceil(wmax * 1.05f);
  float fmax = 0.0f;
  for (int i = 0; i < rec->n_samples; ++i)
    if (rec->samples[i].flow_gps > fmax) fmax = rec->samples[i].flow_gps;
  fmax = nice_ceil(fmax * 1.05f);

  auto x_at = [&](uint32_t t) {
    return plot.x1 + static_cast<int32_t>(static_cast<int64_t>(t) * w_px / t_end);
  };
  auto y_weight = [&](float v) {
    if (v < 0) v = 0;
    return plot.y2 - static_cast<int32_t>(v / wmax * h_px);
  };
  auto y_flow = [&](float v) {
    if (v < 0) v = 0;
    return plot.y2 - static_cast<int32_t>(v / fmax * h_px);
  };

  // Gridlines: hairline, one step off the surface, at every quarter.
  lv_draw_line_dsc_t grid;
  lv_draw_line_dsc_init(&grid);
  grid.color = lv_color_hex(theme::rail());
  grid.width = 1;
  for (int i = 0; i <= 4; ++i) {
    const int32_t y = plot.y1 + h_px * i / 4;
    grid.p1.x = plot.x1;
    grid.p1.y = y;
    grid.p2.x = plot.x2;
    grid.p2.y = y;
    lv_draw_line(layer, &grid);
  }

  // Washes (~10% of each hue under its curve), tiled sample-interval rects so
  // the opacity stays even. Flow first; weight reads on top.
  auto draw_wash = [&](bool flow_series) {
    lv_draw_rect_dsc_t wash;
    lv_draw_rect_dsc_init(&wash);
    wash.bg_color = lv_color_hex(flow_series ? theme::ok() : theme::accent());
    wash.bg_opa = LV_OPA_10;
    for (int i = 1; i < rec->n_samples; ++i) {
      lv_area_t a;
      a.x1 = x_at(rec->samples[i - 1].t_ms);
      a.x2 = x_at(rec->samples[i].t_ms) - 1;
      if (a.x2 < a.x1) continue;
      a.y1 = flow_series ? y_flow(rec->samples[i].flow_gps)
                         : y_weight(rec->samples[i].weight_g);
      a.y2 = plot.y2;
      if (a.y2 <= a.y1) continue;
      lv_draw_rect(layer, &wash, &a);
    }
  };
  draw_wash(true);
  draw_wash(false);

  // Target reference: dashed warn line on the weight scale.
  if (rec->summary.target_g > 0.0f && rec->summary.target_g <= wmax) {
    lv_draw_line_dsc_t tl;
    lv_draw_line_dsc_init(&tl);
    tl.color = lv_color_hex(theme::warn());
    tl.width = ui::dp(2);
    tl.dash_width = ui::dp(6);
    tl.dash_gap = ui::dp(4);
    const int32_t y = y_weight(rec->summary.target_g);
    tl.p1.x = plot.x1;
    tl.p1.y = y;
    tl.p2.x = plot.x2;
    tl.p2.y = y;
    lv_draw_line(layer, &tl);
  }

  // Traces: 2px, round joins; flow under, weight over.
  auto draw_trace = [&](bool flow_series) {
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(flow_series ? theme::ok() : theme::accent());
    line.width = ui::dp(2);
    line.round_start = 1;
    line.round_end = 1;
    for (int i = 1; i < rec->n_samples; ++i) {
      line.p1.x = x_at(rec->samples[i - 1].t_ms);
      line.p1.y = flow_series ? y_flow(rec->samples[i - 1].flow_gps)
                              : y_weight(rec->samples[i - 1].weight_g);
      line.p2.x = x_at(rec->samples[i].t_ms);
      line.p2.y = flow_series ? y_flow(rec->samples[i].flow_gps)
                              : y_weight(rec->samples[i].weight_g);
      lv_draw_line(layer, &line);
    }
    // End-dot with a surface ring so it reads over the other trace.
    const int last = rec->n_samples - 1;
    const int32_t cx = x_at(rec->samples[last].t_ms);
    const int32_t cy = flow_series ? y_flow(rec->samples[last].flow_gps)
                                   : y_weight(rec->samples[last].weight_g);
    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.radius = LV_RADIUS_CIRCLE;
    dot.bg_color = lv_color_hex(theme::bg());
    dot.bg_opa = LV_OPA_COVER;
    const int32_t ring = ui::dp(6);
    lv_area_t a = {cx - ring, cy - ring, cx + ring, cy + ring};
    lv_draw_rect(layer, &dot, &a);
    dot.bg_color = lv_color_hex(flow_series ? theme::ok() : theme::accent());
    const int32_t r = ui::dp(4);
    a = {cx - r, cy - r, cx + r, cy + r};
    lv_draw_rect(layer, &dot, &a);
  };
  draw_trace(true);
  draw_trace(false);

  // Axis legends (top strip): a short color key beside muted text — the text
  // never wears the series color, the dash does.
  lv_draw_label_dsc_t lbl;
  lv_draw_label_dsc_init(&lbl);
  lbl.font = ui::font_dp(14);
  lbl.color = lv_color_hex(theme::muted());
  const int32_t key_y = box.y1 + legend_h / 2;
  lv_draw_line_dsc_t key;
  lv_draw_line_dsc_init(&key);
  key.width = ui::dp(3);
  key.round_start = 1;
  key.round_end = 1;

  key.color = lv_color_hex(theme::accent());
  key.p1.x = box.x1 + ui::dp(2);
  key.p1.y = key_y;
  key.p2.x = box.x1 + ui::dp(16);
  key.p2.y = key_y;
  lv_draw_line(layer, &key);
  lbl.align = LV_TEXT_ALIGN_LEFT;
  lbl.text = "Weight  g";
  lv_area_t la = {box.x1 + ui::dp(22), box.y1 + ui::dp(3), box.x1 + ui::dp(160),
                  box.y1 + legend_h};
  lv_draw_label(layer, &lbl, &la);

  key.color = lv_color_hex(theme::ok());
  key.p1.x = box.x2 - ui::dp(16);
  key.p2.x = box.x2 - ui::dp(2);
  lv_draw_line(layer, &key);
  lbl.align = LV_TEXT_ALIGN_RIGHT;
  lbl.text = "Flow  g/s";
  la = {box.x2 - ui::dp(160), box.y1 + ui::dp(3), box.x2 - ui::dp(22),
        box.y1 + legend_h};
  lv_draw_label(layer, &lbl, &la);

  // Tick values on both sides at 0 / 25 / 50 / 75 / 100% of each scale,
  // centered on their gridline. Integers stay integers ("2.5" only when the
  // quarter isn't whole).
  char tb[12];
  auto fmt_tick = [&](float v) {
    // Exact or nothing: a 1.25 tick prints "1.25", never a rounded "1.3".
    if (v == static_cast<float>(static_cast<int>(v)))
      std::snprintf(tb, sizeof(tb), "%d", static_cast<int>(v));
    else if (v * 10.0f == static_cast<float>(static_cast<int>(v * 10.0f)))
      std::snprintf(tb, sizeof(tb), "%.1f", static_cast<double>(v));
    else
      std::snprintf(tb, sizeof(tb), "%.2f", static_cast<double>(v));
  };
  for (int i = 0; i <= 4; ++i) {
    const int32_t y = plot.y1 + h_px * i / 4;
    const float frac = static_cast<float>(4 - i) / 4.0f;
    fmt_tick(wmax * frac);
    lbl.align = LV_TEXT_ALIGN_RIGHT;
    lbl.text = tb;
    la = {box.x1, y - ui::dp(9), plot.x1 - ui::dp(8), y + ui::dp(9)};
    lv_draw_label(layer, &lbl, &la);
    fmt_tick(fmax * frac);
    lbl.align = LV_TEXT_ALIGN_LEFT;
    lbl.text = tb;
    la = {plot.x2 + ui::dp(8), y - ui::dp(9), box.x2, y + ui::dp(9)};
    lv_draw_label(layer, &lbl, &la);
  }

  // Time axis: 0s left, duration right, under the plot.
  lbl.align = LV_TEXT_ALIGN_LEFT;
  lbl.text = "0 s";
  la = {plot.x1, plot.y2 + ui::dp(4), plot.x1 + ui::dp(80), box.y2};
  lv_draw_label(layer, &lbl, &la);
  char tbuf[16];
  std::snprintf(tbuf, sizeof(tbuf), "%lu s",
                static_cast<unsigned long>((t_end + 500) / 1000));
  lbl.align = LV_TEXT_ALIGN_RIGHT;
  lbl.text = tbuf;
  la = {plot.x2 - ui::dp(80), plot.y2 + ui::dp(4), plot.x2, box.y2};
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
                          const ScreenProfile& screen, bool use_24h,
                          lv_obj_t** out_delete_btn) {
  const bool compact = is_compact(screen);
  const bool xl = is_xl(screen);
  // One value size for every stat (a shared baseline, not a size-per-stat
  // jumble); captions and header share the small face.
  const lv_font_t* value_font = ui::font_dp(compact ? 16 : xl ? 36 : 28);
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

  // Right side of the header: mode text, plus the optional delete button.
  lv_obj_t* hdr_right = lv_obj_create(header);
  lv_obj_remove_style_all(hdr_right);
  lv_obj_remove_flag(hdr_right, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(hdr_right, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_size(hdr_right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(hdr_right, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr_right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(hdr_right, ui::dp(compact ? 8 : 12), 0);

  const char* mode_txt = rec.summary.wired ? "Auto shot"
                         : rec.summary.mode == core::ShotMode::kDetect
                             ? "Detected"
                             : "Manual";
  lv_obj_t* mode_lbl = lv_label_create(hdr_right);
  lv_label_set_text(mode_lbl, mode_txt);
  lv_obj_set_style_text_color(mode_lbl, lv_color_hex(theme::muted()), 0);
  lv_obj_set_style_text_font(mode_lbl, small, 0);

  if (out_delete_btn != nullptr) {
    lv_obj_t* del = ui::make_button(hdr_right);
    lv_obj_set_height(del, ui::dp(compact ? 26 : 38));
    lv_obj_set_style_pad_hor(del, ui::dp(compact ? 10 : 14), 0);
    lv_obj_set_style_bg_color(del, lv_color_hex(theme::alert()), 0);
    lv_obj_t* l = lv_label_create(del);
    lv_label_set_text(l, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_color(l, lv_color_hex(theme::text()), 0);
    lv_obj_set_style_text_font(l, small, 0);
    lv_obj_center(l);
    *out_delete_btn = del;
  }

  // Hero metrics: five equal columns, always all five (untargeted shots show
  // "-" for target/diff) so every card lays out identically.
  lv_obj_t* hero = lv_obj_create(card);
  lv_obj_remove_style_all(hero);
  lv_obj_remove_flag(hero, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(hero, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_width(hero, lv_pct(100));
  lv_obj_set_height(hero, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_ver(hero, ui::dp(compact ? 2 : 6), 0);

  const bool targeted = rec.summary.target_g > 0.0f;
  char b[24];
  std::snprintf(b, sizeof(b), "%.1f g", static_cast<double>(rec.summary.final_g));
  hero_stat(hero, b, "Result", value_font, small);

  if (targeted)
    std::snprintf(b, sizeof(b), "%.0f g", static_cast<double>(rec.summary.target_g));
  else
    std::snprintf(b, sizeof(b), "-");
  hero_stat(hero, b, "Target", value_font, small);

  if (targeted) {
    const float diff = rec.summary.final_g - rec.summary.target_g;
    std::snprintf(b, sizeof(b), "%+.1f g", static_cast<double>(diff));
    lv_obj_t* diff_val = hero_stat(hero, b, "Diff", value_font, small);
    const float ad = diff < 0 ? -diff : diff;
    const uint32_t dc = ad <= 0.5f ? theme::ok() : ad <= 1.5f ? theme::warn()
                                                              : theme::alert();
    lv_obj_set_style_text_color(diff_val, lv_color_hex(dc), 0);
  } else {
    hero_stat(hero, "-", "Diff", value_font, small);
  }

  std::snprintf(b, sizeof(b), "%.1f s",
                static_cast<double>(rec.summary.duration_ms) / 1000.0);
  hero_stat(hero, b, "Time", value_font, small);

  std::snprintf(b, sizeof(b), "%.2f g/s", static_cast<double>(rec.summary.avg_gps));
  hero_stat(hero, b, compact ? "Avg" : "Avg flow", value_font, small);

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


}  // namespace ui
