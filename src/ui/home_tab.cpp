#include "ui/home_tab.h"

#include <cstdio>
#include <cstring>

#include "ui/theme.h"
#include "ui/units.h"
#include "ui/widgets.h"

namespace {

using ui::HomeWidgets;  // used by the flow-graph paint helpers below

void format_now(char* out, size_t n, float c, bool f) {
  std::snprintf(out, n, "%.1f %s", ui::temp_disp(c, f), ui::temp_unit(f));
}
void format_set(char* out, size_t n, float c, bool f) {
  std::snprintf(out, n, "Set  %.1f %s", ui::temp_disp(c, f), ui::temp_unit(f));
}

// A temperature panel: caption (static) on top, big current value, set point
// below. On large screens the set point is flanked by [-]/[+] steppers (returned
// for wiring); on compact it's a plain label. The value/set labels are returned
// for live updates.
void make_temp_card(lv_obj_t* parent, const char* caption,
                    const lv_font_t* caption_font, const lv_font_t* value_font,
                    const lv_font_t* sub_font, int pad, bool with_steppers,
                    int btn_size, const lv_font_t* symbol_font,
                    lv_obj_t** out_value, lv_obj_t** out_set,
                    lv_obj_t** out_minus, lv_obj_t** out_plus) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(card, lv_pct(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_style_bg_color(card, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_pad_all(card, pad, 0);

  lv_obj_t* cap = lv_label_create(card);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(cap, caption_font, 0);
  lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_t* val = lv_label_create(card);
  lv_obj_set_style_text_color(val, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(val, value_font, 0);
  lv_obj_align(val, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* set;
  if (with_steppers) {
    // [-]  set  [+] row pinned to the bottom of the card.
    lv_obj_t* setrow = lv_obj_create(card);
    lv_obj_remove_style_all(setrow);
    lv_obj_remove_flag(setrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(setrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(setrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(setrow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(setrow, 12, 0);
    lv_obj_align(setrow, LV_ALIGN_BOTTOM_MID, 0, 0);

    *out_minus = ui::make_step_button(setrow, LV_SYMBOL_MINUS, btn_size, symbol_font);

    set = lv_label_create(setrow);
    lv_obj_set_width(set, btn_size * 2);  // stable width so the buttons don't shift
    lv_obj_set_style_text_align(set, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(set, lv_color_hex(ui::theme::text()), 0);
    lv_obj_set_style_text_font(set, sub_font, 0);

    *out_plus = ui::make_step_button(setrow, LV_SYMBOL_PLUS, btn_size, symbol_font);
  } else {
    set = lv_label_create(card);
    lv_obj_set_style_text_color(set, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(set, sub_font, 0);
    lv_obj_align(set, LV_ALIGN_BOTTOM_MID, 0, 0);
    *out_minus = nullptr;
    *out_plus = nullptr;
  }

  *out_value = val;
  *out_set = set;
}

// A small "BREW 93.0°" temperature chip for the scale-aware layout, where the
// temps shrink to make room for the scale. Returns the value label.
lv_obj_t* make_temp_chip(lv_obj_t* parent, const char* caption,
                         const lv_font_t* caption_font, const lv_font_t* value_font,
                         int pad) {
  lv_obj_t* chip = lv_obj_create(parent);
  lv_obj_remove_style_all(chip);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_grow(chip, 1);
  lv_obj_set_height(chip, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(chip, 6, 0);
  lv_obj_set_style_bg_color(chip, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(chip, 12, 0);
  lv_obj_set_style_pad_all(chip, pad, 0);

  lv_obj_t* cap = lv_label_create(chip);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(cap, caption_font, 0);

  lv_obj_t* val = lv_label_create(chip);
  lv_obj_set_style_text_color(val, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(val, value_font, 0);
  return val;
}

// Flow graph: Y axis 0..6 g/s, swept left->right over a fixed time window. The
// pen advances by wall-clock time (see flow_graph_tick), so the sweep speed is
// constant regardless of the scale's sample rate. kFlowWindowS is the only knob —
// wire it to a setting (like the Stats zoom) to make the window adjustable.
constexpr float kFlowYMaxGps = 6.0f;   // full-scale flow (top of the plot)
constexpr int kFlowWindowS = 60;       // seconds spanned by the full plot width
// Pixels scrolled per re-blit. The whole-plot copy is the cost, so scrolling N px
// at 1/N the cadence keeps the same average speed for ~1/N the CPU (chunkier
// motion). 1 = smoothest/most expensive; bump to trade smoothness for CPU.
constexpr int kFlowStepPx = 1;

// Map a flow rate (g/s) to a canvas row: 0 = top = full-scale, h-1 = bottom = 0.
int flow_row(float gps, int h) {
  if (gps < 0.0f) gps = 0.0f;
  float frac = gps / kFlowYMaxGps;
  if (frac > 1.0f) frac = 1.0f;
  int y = static_cast<int>((1.0f - frac) * (h - 1) + 0.5f);
  if (y < 0) y = 0;
  if (y >= h) y = h - 1;
  return y;
}

// Repaint one canvas column to the background, restoring the faint horizontal
// gridlines (at 2 and 4 g/s) so the sweep leaves a clean grid behind it. Writes
// the RGB565 buffer directly (lv_canvas_set_px is far too slow per-pixel).
void clear_flow_column(HomeWidgets& w, int x) {
  const uint16_t bg = lv_color_to_u16(lv_color_hex(ui::theme::card()));
  const uint16_t grid = lv_color_to_u16(lv_color_hex(ui::theme::scrollbar()));
  uint16_t* col = w.flow_buf + x;
  for (int y = 0; y < w.flow_h; ++y) col[y * w.flow_stride] = bg;
  for (int t = 1; t <= 2; ++t) {  // thirds -> 4 g/s and 2 g/s
    const int gy = w.flow_h * t / 3;
    if (gy >= 0 && gy < w.flow_h) col[gy * w.flow_stride] = grid;
  }
}

// Draw the flow line into column x: a vertical run joining the previous pen row
// to the new one, so consecutive samples connect into a continuous line.
void draw_flow_segment(HomeWidgets& w, int x, int y) {
  const uint16_t accent = lv_color_to_u16(lv_color_hex(ui::theme::accent()));
  const int y0 = (w.flow_prev_y < 0) ? y : w.flow_prev_y;
  const int lo = y0 < y ? y0 : y;
  const int hi = y0 < y ? y : y0;
  uint16_t* col = w.flow_buf + x;
  for (int yy = lo; yy <= hi; ++yy) {
    if (yy >= 0 && yy < w.flow_h) col[yy * w.flow_stride] = accent;
  }
}

// Scroll the whole plot one pixel left (memmove per row) — the cheap half of the
// strip chart. Horizontal gridlines are invariant under a horizontal shift, so
// they survive; the vacated right column is repainted by the caller.
void shift_flow_left(HomeWidgets& w) {
  for (int y = 0; y < w.flow_h; ++y) {
    uint16_t* row = w.flow_buf + static_cast<size_t>(y) * w.flow_stride;
    std::memmove(row, row + 1, static_cast<size_t>(w.flow_w - 1) * sizeof(uint16_t));
  }
}

// A compact vertical readout card: small caption on top, value below. Used for
// the single brew/boiler/scale row on large screens (the graph is the hero, so
// these stay short). Returns the value label; optionally hands back the card.
lv_obj_t* make_readout_card(lv_obj_t* parent, const char* caption,
                            const lv_font_t* caption_font, const lv_font_t* value_font,
                            int pad, lv_obj_t** out_card) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_height(card, lv_pct(100));
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(card, pad, 0);
  lv_obj_set_style_pad_row(card, 2, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 16, 0);

  lv_obj_t* cap = lv_label_create(card);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(cap, caption_font, 0);

  lv_obj_t* val = lv_label_create(card);
  lv_obj_set_style_text_color(val, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(val, value_font, 0);
  if (out_card != nullptr) *out_card = card;
  return val;
}

// The styled (empty) flow-graph card. The canvas that fills it is created later
// by populate_flow_graph — only once every sibling exists and the layout is
// final, so the content box we measure is the real one. Returns the card.
lv_obj_t* make_flow_graph_card(lv_obj_t* parent) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(card, lv_pct(100));
  lv_obj_set_flex_grow(card, 1);  // hero: take the vertical space left over
  lv_obj_set_style_bg_color(card, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_pad_left(card, 30, 0);    // Y labels
  lv_obj_set_style_pad_bottom(card, 22, 0);  // X labels
  lv_obj_set_style_pad_top(card, 18, 0);
  lv_obj_set_style_pad_right(card, 12, 0);
  return card;
}

// A flow-rate (g/s) sweep graph: a canvas we paint one column at a time as time
// passes (see flow_graph_tick), with static X/Y axis labels drawn once. Cheap to
// animate — only the new column is repainted per step, not the whole plot. Must
// be called after the tab's layout is final (see make_flow_graph_card).
void populate_flow_graph(lv_obj_t* card, HomeWidgets& out) {
  int pw = lv_obj_get_content_width(card);
  int ph = lv_obj_get_content_height(card);
  if (pw < 16) pw = 16;
  if (ph < 16) ph = 16;

  const uint32_t stride_bytes = lv_draw_buf_width_to_stride(pw, LV_COLOR_FORMAT_RGB565);
  out.flow_buf = static_cast<uint16_t*>(lv_malloc(stride_bytes * static_cast<size_t>(ph)));
  if (out.flow_buf == nullptr) return;  // OOM: no graph (flow_canvas stays null)

  lv_obj_t* canvas = lv_canvas_create(card);
  lv_canvas_set_buffer(canvas, out.flow_buf, pw, ph, LV_COLOR_FORMAT_RGB565);
  lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);  // fills the content box
  out.flow_canvas = canvas;
  out.flow_w = pw;
  out.flow_h = ph;
  out.flow_stride = static_cast<int>(stride_bytes / sizeof(uint16_t));
  out.flow_prev_y = -1;
  out.flow_tick = 0;
  out.flow_accum_ms = 0;

  for (int x = 0; x < pw; ++x) clear_flow_column(out, x);  // background + gridlines

  // Static Y labels (6,4,2,0 g/s, top..bottom) + "g/s" caption. Negative x offsets
  // sit them in the left margin (padding), aligned with the plot rows.
  lv_obj_t* unit = lv_label_create(card);
  lv_label_set_text(unit, "g/s");
  lv_obj_set_style_text_color(unit, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(unit, &lv_font_montserrat_14, 0);
  lv_obj_align(unit, LV_ALIGN_TOP_LEFT, -26, -16);
  for (int t = 0; t <= 3; ++t) {
    lv_obj_t* yl = lv_label_create(card);
    lv_label_set_text_fmt(yl, "%d", 6 - 2 * t);
    lv_obj_set_style_text_color(yl, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(yl, &lv_font_montserrat_14, 0);
    lv_obj_align(yl, LV_ALIGN_TOP_LEFT, -26, ph * t / 3 - 8);
  }

  // Static X labels (window..now) below the plot, in the bottom margin.
  for (int t = 0; t <= 3; ++t) {
    lv_obj_t* xl = lv_label_create(card);
    const int secs = kFlowWindowS * (3 - t) / 3;
    if (secs == 0) lv_label_set_text(xl, "now");
    else lv_label_set_text_fmt(xl, "%ds", secs);
    lv_obj_set_style_text_color(xl, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(xl, &lv_font_montserrat_14, 0);
    int x = pw * t / 3;
    if (t == 3) x -= 24;
    else if (t > 0) x -= 12;
    lv_obj_align(xl, LV_ALIGN_TOP_LEFT, x, ph + 4);
  }

#ifndef ESP_PLATFORM
  // Sim only: seed a representative espresso flow curve so the graph reads at a
  // glance in the rendered PNGs. On hardware the plot starts empty and sweeps in.
  int prev = -1;
  for (int x = 0; x < pw; ++x) {
    const float t = x / static_cast<float>(pw - 1);
    float f;  // g/s
    if (t < 0.18f) f = 2.7f * (t / 0.18f);          // pre-infuse ramp
    else if (t < 0.82f) f = 2.7f - 0.5f * ((t - 0.18f) / 0.64f);  // gentle decline
    else f = 2.2f * (1.0f - (t - 0.82f) / 0.18f);   // taper to 0
    const int y = flow_row(f, ph);
    out.flow_prev_y = prev;
    draw_flow_segment(out, x, y);
    prev = y;
  }
  out.flow_prev_y = -1;
#endif
}

const char* battery_icon(int pct) {
  if (pct >= 90) return LV_SYMBOL_BATTERY_FULL;
  if (pct >= 65) return LV_SYMBOL_BATTERY_3;
  if (pct >= 40) return LV_SYMBOL_BATTERY_2;
  if (pct >= 15) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

const char* battery_fill_icon(int frame) {
  switch (frame % 5) {
    case 0:  return LV_SYMBOL_BATTERY_EMPTY;
    case 1:  return LV_SYMBOL_BATTERY_1;
    case 2:  return LV_SYMBOL_BATTERY_2;
    case 3:  return LV_SYMBOL_BATTERY_3;
    default: return LV_SYMBOL_BATTERY_FULL;
  }
}

// Loops the battery fill while charging (no percent — see update_home).
void battery_anim_cb(lv_timer_t* t) {
  auto* w = static_cast<ui::HomeWidgets*>(lv_timer_get_user_data(t));
  if (!w->charging) return;
  w->charge_frame = (w->charge_frame + 1) % 5;
  char buf[24];
  std::snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %s",
                battery_fill_icon(w->charge_frame));
  lv_label_set_text(w->battery_label, buf);
}

// Top bar: status (left) + clock & battery (right). Shared by both layouts.
void build_top_bar(lv_obj_t* parent, const lv_font_t* sub_font, ui::HomeWidgets& out) {
  lv_obj_t* bar = lv_obj_create(parent);
  lv_obj_remove_style_all(bar);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(bar, lv_pct(100));
  lv_obj_set_height(bar, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t* status_group = lv_obj_create(bar);
  lv_obj_remove_style_all(status_group);
  lv_obj_set_size(status_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(status_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(status_group, 8, 0);

  out.status_dot = lv_obj_create(status_group);
  lv_obj_remove_style_all(out.status_dot);
  lv_obj_set_size(out.status_dot, 12, 12);
  lv_obj_set_style_radius(out.status_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(out.status_dot, LV_OPA_COVER, 0);

  out.status_label = lv_label_create(status_group);
  lv_obj_set_style_text_color(out.status_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.status_label, sub_font, 0);

  lv_obj_t* right_group = lv_obj_create(bar);
  lv_obj_remove_style_all(right_group);
  lv_obj_set_size(right_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(right_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(right_group, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(right_group, 10, 0);

  out.battery_label = lv_label_create(right_group);
  lv_obj_set_style_text_color(out.battery_label, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.battery_label, sub_font, 0);

  out.clock_label = lv_label_create(right_group);
  lv_obj_set_style_text_color(out.clock_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.clock_label, sub_font, 0);
  out.batt_timer = lv_timer_create(battery_anim_cb, 350, &out);  // drives charge anim
}

// The full-width power button at the bottom (both layouts).
void build_power_button(lv_obj_t* parent, int btn_h, const lv_font_t* btn_font,
                        ui::HomeWidgets& out) {
  out.power_btn = ui::make_button(parent);
  lv_obj_set_width(out.power_btn, lv_pct(100));
  lv_obj_set_height(out.power_btn, btn_h);
  lv_obj_set_style_radius(out.power_btn, 14, 0);
  out.power_label = lv_label_create(out.power_btn);
  lv_obj_set_style_text_color(out.power_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.power_label, btn_font, 0);
  lv_obj_center(out.power_label);
}

// Scale readout card: big live weight, "/ target g" sub, and a paddle-status
// pill in the corner. Returns the card so the caller can size/place it.
lv_obj_t* build_scale_card(lv_obj_t* parent, const lv_font_t* weight_font,
                           const lv_font_t* sub_font, int pad, ui::HomeWidgets& out) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_pad_all(card, pad, 0);

  out.scale_weight = lv_label_create(card);
  lv_obj_set_style_text_color(out.scale_weight, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.scale_weight, weight_font, 0);
  lv_obj_align(out.scale_weight, LV_ALIGN_CENTER, 0, -6);

  out.scale_target = lv_label_create(card);
  lv_obj_set_style_text_color(out.scale_target, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.scale_target, sub_font, 0);
  lv_obj_align(out.scale_target, LV_ALIGN_BOTTOM_MID, 0, 0);
  return card;
}

// A Tare button (icon + label). Returns it (the caller sizes/places it).
void build_tare_button(lv_obj_t* parent, const lv_font_t* font, ui::HomeWidgets& out) {
  out.tare_btn = ui::make_button(parent);
  lv_obj_set_style_bg_color(out.tare_btn, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_radius(out.tare_btn, 14, 0);
  out.tare_label = lv_label_create(out.tare_btn);
  lv_label_set_text(out.tare_label, LV_SYMBOL_LOOP "  Tare");
  lv_obj_set_style_text_color(out.tare_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.tare_label, font, 0);
  lv_obj_center(out.tare_label);
}

}  // namespace

namespace ui {

void build_home_tab(lv_obj_t* parent, const ScreenProfile& screen, bool scale_enabled,
                    HomeWidgets& out) {
  const bool compact = is_compact(screen);
  const bool xl = is_xl(screen);
  out.scale_enabled = scale_enabled;

  // HomeWidgets is reused across rebuilds, and build() frees the old widgets. Null
  // every optional pointer up front so the layout branch that runs leaves the rest
  // null (not dangling) — update_home / pump_scale_chart null-check these. Without
  // this, forgetting a scale (rebuild scale-aware -> no-scale) crashes on the stale
  // flow_chart / scale_* pointers (LoadProhibited).
  out.brew_minus = out.brew_plus = out.boiler_minus = out.boiler_plus = nullptr;
  out.brew_set = out.boiler_set = nullptr;
  out.scale_weight = out.scale_target = nullptr;
  out.tare_btn = out.tare_label = nullptr;
  out.paddle_pill = out.paddle_label = nullptr;
  out.flow_canvas = nullptr;  // flow_buf is freed by App before each rebuild

  const int pad = compact ? 8 : xl ? 28 : 20;
  const int gap = compact ? 6 : xl ? 22 : 16;
  const int card_pad = compact ? 8 : xl ? 24 : 16;
  const int btn_h = compact ? 40 : xl ? 92 : 64;
  const lv_font_t* value_font = compact ? &lv_font_montserrat_28 : &lv_font_montserrat_48;
  const lv_font_t* sub_font =
      compact ? &lv_font_montserrat_14 : xl ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
  const lv_font_t* btn_font =
      compact ? &lv_font_montserrat_14 : xl ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
  const lv_font_t* caption_font =
      compact ? &lv_font_montserrat_14 : xl ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
  const lv_font_t* step_font = compact ? &lv_font_montserrat_20 : &lv_font_montserrat_28;
  const int step_btn = compact ? 36 : xl ? 64 : 50;

  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, pad, 0);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(parent, gap, 0);

  build_top_bar(parent, sub_font, out);

  if (!scale_enabled) {
    // --- Classic Home: two big temperature cards + power button -------------
    const int row_h = compact ? 96 : xl ? 300 : 210;
    const bool steppers = !compact;
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, row_h);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, gap, 0);
    make_temp_card(row, "BREW", caption_font, value_font, sub_font, card_pad, steppers,
                   step_btn, step_font, &out.brew_value, &out.brew_set, &out.brew_minus,
                   &out.brew_plus);
    make_temp_card(row, "BOILER", caption_font, value_font, sub_font, card_pad, steppers,
                   step_btn, step_font, &out.boiler_value, &out.boiler_set,
                   &out.boiler_minus, &out.boiler_plus);

    lv_obj_t* spacer = lv_obj_create(parent);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, lv_pct(100));
    lv_obj_set_height(spacer, 0);
    lv_obj_set_flex_grow(spacer, 1);

    build_power_button(parent, btn_h, btn_font, out);
    return;
  }

  // --- Scale-aware Home -----------------------------------------------------
  // Compact: a small temp strip, a big scale readout, then Tare + Power.
  // Large:   shorter temp cards, then a scale readout beside a flow graph.
  if (compact) {
    lv_obj_t* strip = lv_obj_create(parent);
    lv_obj_remove_style_all(strip);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(strip, lv_pct(100));
    lv_obj_set_height(strip, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(strip, gap, 0);
    out.brew_value = make_temp_chip(strip, "BREW", caption_font, sub_font, 6);
    out.boiler_value = make_temp_chip(strip, "STEAM", caption_font, sub_font, 6);
    out.brew_set = nullptr;
    out.boiler_set = nullptr;

    lv_obj_t* card = build_scale_card(parent, &lv_font_montserrat_48, sub_font, card_pad, out);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_flex_grow(card, 1);

    lv_obj_t* actions = lv_obj_create(parent);
    lv_obj_remove_style_all(actions);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(actions, lv_pct(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, gap, 0);
    build_tare_button(actions, btn_font, out);
    lv_obj_set_flex_grow(out.tare_btn, 1);
    lv_obj_set_height(out.tare_btn, btn_h);
    build_power_button(actions, btn_h, btn_font, out);
    lv_obj_set_width(out.power_btn, 0);
    lv_obj_set_flex_grow(out.power_btn, 1);
    return;
  }

  // Large screens: one short readout row (brew/boiler/scale), then the flow graph
  // as the hero (majority of the screen, with X/Y axes), then Tare + Power.
  const int read_h = xl ? 104 : 84;
  const lv_font_t* read_cap = &lv_font_montserrat_14;
  const lv_font_t* read_val = &lv_font_montserrat_28;

  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, read_h);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, gap, 0);
  // Each card is caption / value / sub, where the sub is the set point (brew,
  // boiler) or the target weight (scale).
  auto add_sub = [&](lv_obj_t* card) {
    lv_obj_t* l = lv_label_create(card);
    lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(l, read_cap, 0);
    return l;
  };
  lv_obj_t* bcard = nullptr;
  lv_obj_t* ocard = nullptr;
  lv_obj_t* scard = nullptr;
  out.brew_value = make_readout_card(row, "BREW", read_cap, read_val, card_pad, &bcard);
  out.brew_set = add_sub(bcard);
  out.boiler_value = make_readout_card(row, "BOILER", read_cap, read_val, card_pad, &ocard);
  out.boiler_set = add_sub(ocard);
  out.scale_weight = make_readout_card(row, "SCALE", read_cap, read_val, card_pad, &scard);
  out.scale_target = add_sub(scard);

  // The flow graph fills the rest — the hero. (Paddle status pill removed; it
  // returns in Phase 4 when there's real paddle hardware/state to show.) The card
  // is created here for correct flex order, but its canvas is sized only after the
  // actions row exists and the layout is final (populate_flow_graph below).
  lv_obj_t* graph_card = make_flow_graph_card(parent);

  // Tare beside the power button at the bottom.
  lv_obj_t* actions = lv_obj_create(parent);
  lv_obj_remove_style_all(actions);
  lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(actions, lv_pct(100));
  lv_obj_set_height(actions, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(actions, gap, 0);
  build_tare_button(actions, btn_font, out);
  lv_obj_set_height(out.tare_btn, btn_h);
  lv_obj_set_width(out.tare_btn, 0);
  lv_obj_set_flex_grow(out.tare_btn, 1);  // equal halves with the power button
  build_power_button(actions, btn_h, btn_font, out);
  lv_obj_set_width(out.power_btn, 0);
  lv_obj_set_flex_grow(out.power_btn, 1);

  // Everything's in place: resolve the final layout, then size + paint the graph
  // to the card's real content box.
  lv_obj_update_layout(parent);
  populate_flow_graph(graph_card, out);
}

void update_home(HomeWidgets& w, const core::MachineSnapshot& state,
                 const core::BatteryState& battery, const core::WallTime& clock,
                 bool clock_24h, bool fahrenheit, const core::ScaleSnapshot& scale,
                 const core::BrewSnapshot& brew) {
  const bool connected = state.link == core::Link::Connected;
  const bool on = state.power == core::Power::On;

  // Temperatures: real values when connected, placeholders otherwise.
  char buf[24];
  if (connected) {
    format_now(buf, sizeof(buf), state.brew_temp_c, fahrenheit);
    lv_label_set_text(w.brew_value, buf);
    if (w.brew_set != nullptr) {
      format_set(buf, sizeof(buf), state.brew_target_c, fahrenheit);
      lv_label_set_text(w.brew_set, buf);
    }
    if (state.steam_enabled) {
      format_now(buf, sizeof(buf), state.boiler_temp_c, fahrenheit);
      lv_label_set_text(w.boiler_value, buf);
      if (w.boiler_set != nullptr) {
        format_set(buf, sizeof(buf), state.boiler_target_c, fahrenheit);
        lv_label_set_text(w.boiler_set, buf);
      }
    } else {
      lv_label_set_text(w.boiler_value, "Off");
      if (w.boiler_set != nullptr) lv_label_set_text(w.boiler_set, "");
    }
  } else {
    lv_label_set_text(w.brew_value, "--");
    if (w.brew_set != nullptr) lv_label_set_text(w.brew_set, "Set  --");
    lv_label_set_text(w.boiler_value, "--");
    if (w.boiler_set != nullptr) lv_label_set_text(w.boiler_set, "Set  --");
  }

  // Scale readout (scale-aware layout only).
  if (w.scale_weight != nullptr) {
    char wb[16];
    if (scale.connected) {
      std::snprintf(wb, sizeof(wb), "%.1f g", static_cast<double>(scale.weight_g));
    } else {
      std::snprintf(wb, sizeof(wb), "-- g");
    }
    lv_label_set_text(w.scale_weight, wb);
    char tb[16];
    std::snprintf(tb, sizeof(tb), "/ %.0f g", static_cast<double>(brew.target_weight_g));
    lv_label_set_text(w.scale_target, tb);

    // Paddle/brew pill: only meaningful when paddle hardware is present.
    if (w.paddle_pill != nullptr) {
      if (!brew.available) {
        lv_obj_add_flag(w.paddle_pill, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_remove_flag(w.paddle_pill, LV_OBJ_FLAG_HIDDEN);
        const char* txt = brew.brewing ? "Brewing"
                          : brew.paddle_pressed ? "Paddle"
                                                : "Ready";
        uint32_t col = brew.brewing ? ui::theme::ok()
                       : brew.paddle_pressed ? ui::theme::accent()
                                             : ui::theme::rail();
        lv_label_set_text(w.paddle_label, txt);
        lv_obj_set_style_bg_color(w.paddle_pill, lv_color_hex(col), 0);
      }
    }

    // The flow graph is fed separately by App::pump_scale_chart() at the scale's
    // native rate (much faster than this 2 Hz update), so it's smooth.
  }

  // Status (top-left): text + dot color from link + power.
  const char* status = "Disconnected";
  uint32_t dot = ui::theme::muted();
  switch (state.link) {
    case core::Link::Unconfigured: status = "Set up in Settings"; dot = ui::theme::muted(); break;
    case core::Link::NeedsToken:   status = "Token needed"; dot = ui::theme::warn(); break;
    case core::Link::Disconnected: status = "Disconnected"; dot = ui::theme::alert(); break;
    case core::Link::Connecting:   status = "Connecting..."; dot = ui::theme::warn(); break;
    case core::Link::Connected:
      status = on ? "Ready" : "Standby";
      dot = on ? ui::theme::ok() : ui::theme::warn();
      break;
  }
  lv_label_set_text(w.status_label, status);
  lv_obj_set_style_bg_color(w.status_dot, lv_color_hex(dot), 0);

  // Clock (top-right): "14:30" (24h) or "2:30 PM" (12h); dashes until set.
  if (clock.valid) {
    char cb[12];
    if (clock_24h) {
      std::snprintf(cb, sizeof(cb), "%d:%02d", clock.hour, clock.minute);
    } else {
      const int h12 = (clock.hour % 12 == 0) ? 12 : clock.hour % 12;
      std::snprintf(cb, sizeof(cb), "%d:%02d %s", h12, clock.minute,
                    clock.hour < 12 ? "AM" : "PM");
    }
    lv_label_set_text(w.clock_label, cb);
  } else {
    lv_label_set_text(w.clock_label, clock_24h ? "--:--" : "--:-- --");
  }

  // Battery / power (top-right):
  //   battery + USB  -> charging: bolt + looping fill animation (NO percent)
  //   battery only   -> percent + level icon
  //   USB, no cell   -> plug symbol (external power)
  //   nothing        -> blank
  w.charging = battery.present && battery.charging;
  char bb[24];
  if (w.charging) {
    lv_obj_set_style_text_color(w.battery_label, lv_color_hex(ui::theme::ok()), 0);
    std::snprintf(bb, sizeof(bb), LV_SYMBOL_CHARGE " %s", battery_fill_icon(w.charge_frame));
    lv_label_set_text(w.battery_label, bb);
  } else if (battery.present) {
    std::snprintf(bb, sizeof(bb), "%d%% %s", battery.percent, battery_icon(battery.percent));
    lv_label_set_text(w.battery_label, bb);
    lv_obj_set_style_text_color(
        w.battery_label,
        lv_color_hex(battery.percent < 15 ? ui::theme::alert() : ui::theme::muted()), 0);
  } else if (battery.usb) {
    lv_label_set_text(w.battery_label, LV_SYMBOL_USB);
    lv_obj_set_style_text_color(w.battery_label, lv_color_hex(ui::theme::muted()), 0);
  } else {
    lv_label_set_text(w.battery_label, "");
  }

  // Power button: only actionable when connected.
  if (connected) {
    lv_obj_remove_state(w.power_btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(
        w.power_btn, lv_color_hex(on ? ui::theme::card() : ui::theme::accent()), 0);
    lv_label_set_text(w.power_label,
                      on ? LV_SYMBOL_POWER "  Standby" : LV_SYMBOL_POWER "  Turn On");
  } else {
    lv_obj_add_state(w.power_btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(w.power_btn, lv_color_hex(ui::theme::card()), 0);
    lv_label_set_text(w.power_label, LV_SYMBOL_POWER "  Offline");
  }
}

void flow_graph_tick(HomeWidgets& w, const core::ScaleSnapshot& scale) {
  if (w.flow_canvas == nullptr || w.flow_buf == nullptr || w.flow_w <= 0) return;
  const uint32_t now = lv_tick_get();
  if (w.flow_tick == 0) {  // first call: start the clock, nothing to advance yet
    w.flow_tick = now;
    return;
  }
  w.flow_accum_ms += now - w.flow_tick;
  w.flow_tick = now;
  // One "step" scrolls kFlowStepPx pixels and triggers one whole-plot re-blit, so
  // its time budget is kFlowStepPx pixels' worth (same average speed, fewer blits).
  const uint32_t ms_per_step = static_cast<uint32_t>(kFlowStepPx) *
                               static_cast<uint32_t>(kFlowWindowS) * 1000u /
                               static_cast<uint32_t>(w.flow_w);
  if (ms_per_step == 0) return;

  // For each elapsed step, scroll left and drop the scale's current flow in at the
  // right edge. Time-based, so the scroll speed is constant no matter how fast (or
  // slowly) samples arrive. Usually 0 or 1 steps per call.
  const int y = flow_row(scale.connected ? scale.flow_gps : 0.0f, w.flow_h);
  const int rx = w.flow_w - 1;
  int steps = 0;
  while (w.flow_accum_ms >= ms_per_step && steps < w.flow_w) {
    w.flow_accum_ms -= ms_per_step;
    ++steps;
    for (int i = 0; i < kFlowStepPx; ++i) {  // kFlowStepPx px, then one invalidate
      shift_flow_left(w);
      clear_flow_column(w, rx);
      draw_flow_segment(w, rx, y);
      w.flow_prev_y = y;
    }
  }
  if (steps == 0) return;

  // The whole plot shifted, so re-blit it all. This is a straight bitmap copy
  // (memmove already did the work) — no polyline re-rasterization like lv_chart.
  lv_obj_invalidate(w.flow_canvas);
}

}  // namespace ui
