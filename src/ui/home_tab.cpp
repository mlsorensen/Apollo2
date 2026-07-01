#include "ui/home_tab.h"

#include <cmath>
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

// Flow strip chart. The plot scrolls right->left by wall-clock time (see
// flow_graph_tick) over kFlowWindowS seconds. The Y axis auto-ranges (per-mode
// default/floor, grows to the window peak, shrinks back with hysteresis).
constexpr int kFlowWindowS = 60;       // seconds spanned by the full plot width
// Pixels scrolled per re-blit. The whole-plot copy is the cost, so scrolling N px
// at 1/N the cadence keeps the same average speed for ~1/N the CPU (chunkier
// motion). 1 = smoothest/most expensive; bump to trade smoothness for CPU.
constexpr int kFlowStepPx = 1;
// We derive flow ourselves as the rate of weight gain (scales reliably stream
// weight, not flow). Measure it over this window of the weight history so per-
// sample jitter averages out instead of producing a spiky line.
constexpr uint32_t kFlowRateWindowMs = 700;
// Oscilloscope mode: blank columns kept just ahead of the write head so the sweep
// point reads clearly (fresh trace behind it, gap in front).
constexpr int kFlowScopeGapPx = 4;

// Modes: 0 = flow rate (g/s), 1 = weight (g). Each has a unit + a default axis
// full-scale, which is also the floor the axis shrinks back to.
const char* flow_unit(int mode) { return mode == 1 ? "g" : "g/s"; }
float flow_default_max(int mode) { return mode == 1 ? 30.0f : 6.0f; }

// Smallest "nice" axis >= v for the mode (even / multiple of 10 so the labels
// 0, max/2, max stay clean), floored at the mode default.
float flow_nice_max(int mode, float v) {
  const float floor = flow_default_max(mode);
  if (v <= floor) return floor;
  const float step = (mode == 1) ? 15.0f : 3.0f;  // multiples of 3 -> clean thirds
  return step * std::ceil(v / step);
}

// Map a value to a canvas row against the current axis: 0 = top = ymax, h-1 = 0.
int flow_row(float v, int h, float ymax) {
  if (v < 0.0f) v = 0.0f;
  float frac = (ymax > 0.0f) ? v / ymax : 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  int y = static_cast<int>((1.0f - frac) * (h - 1) + 0.5f);
  if (y < 0) y = 0;
  if (y >= h) y = h - 1;
  return y;
}

// Repaint one canvas column to the background + a mid gridline (half scale) so the
// sweep leaves a clean grid behind it. Writes the RGB565 buffer directly
// (lv_canvas_set_px is far too slow per-pixel).
void clear_flow_column(HomeWidgets& w, int x) {
  const uint16_t bg = lv_color_to_u16(lv_color_hex(ui::theme::card()));
  const uint16_t grid = lv_color_to_u16(lv_color_hex(ui::theme::scrollbar()));
  uint16_t* col = w.flow_buf + x;
  for (int y = 0; y < w.flow_h; ++y) col[y * w.flow_stride] = bg;
  for (int t = 1; t <= 2; ++t) {  // gridlines at 1/3 and 2/3 of full scale
    const int gy = w.flow_h * t / 3;
    if (gy >= 0 && gy < w.flow_h) col[gy * w.flow_stride] = grid;
  }
}

// Trace colors. Bright = the accent; dim = the accent faded toward the plot
// background, used for the aged (previous-sweep) trace in oscilloscope mode so the
// fresh sweep edge stands out against it.
uint16_t flow_bright_color() {
  return lv_color_to_u16(lv_color_hex(ui::theme::accent()));
}
uint16_t flow_dim_color() {
  const lv_color_t a = lv_color_hex(ui::theme::accent());
  const lv_color_t bg = lv_color_hex(ui::theme::card());
  return lv_color_to_u16(lv_color_mix(a, bg, 100));  // ~40% accent over the bg
}

// Draw the flow line into column x in `color`: a vertical run joining the previous
// pen row to the new one, so consecutive samples connect into a continuous line.
void draw_flow_segment(HomeWidgets& w, int x, int y, uint16_t color) {
  const int y0 = (w.flow_prev_y < 0) ? y : w.flow_prev_y;
  const int lo = y0 < y ? y0 : y;
  const int hi = y0 < y ? y : y0;
  uint16_t* col = w.flow_buf + x;
  for (int yy = lo; yy <= hi; ++yy) {
    if (yy >= 0 && yy < w.flow_h) col[yy * w.flow_stride] = color;
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

// Update the three dynamic Y labels (max, 2max/3, max/3, top->down) on a rescale.
void set_flow_ylabels(HomeWidgets& w) {
  for (int i = 0; i < 3; ++i) {
    if (w.flow_ylabels[i] == nullptr) continue;
    const float v = w.flow_ymax * static_cast<float>(3 - i) / 3.0f;
    lv_label_set_text_fmt(w.flow_ylabels[i], "%d", static_cast<int>(v + 0.5f));
  }
}

// The value ring for the current mode (weight in g, or flow rate in g/s).
float* flow_active_ring(HomeWidgets& w) {
  return (w.flow_mode == 1) ? w.flow_weights : w.flow_flows;
}

// Redraw the whole plot from the active value ring at the current ymax — used when
// the axis rescales or the unit toggles (the pixels already drawn are at the old
// scale / other unit). Column x maps to screen-x in both styles; in scope mode the
// oldest sample sits just after the cursor, so we lift the pen there to avoid a
// false vertical line across the newest/oldest seam.
void redraw_flow_from_ring(HomeWidgets& w) {
  const float* ring = flow_active_ring(w);
  const uint16_t bright = flow_bright_color();
  const uint16_t dim = flow_dim_color();
  const int seam = w.flow_scope_mode ? (w.flow_cursor + 1) % w.flow_w : -1;
  w.flow_prev_y = -1;
  for (int x = 0; x < w.flow_w; ++x) {
    if (x == seam) w.flow_prev_y = -1;  // pen up: don't connect across the sweep seam
    const int y = flow_row(ring[x], w.flow_h, w.flow_ymax);
    clear_flow_column(w, x);
    // Scope mode: columns past the cursor belong to the previous sweep -> dim them.
    const uint16_t color = (w.flow_scope_mode && x > w.flow_cursor) ? dim : bright;
    draw_flow_segment(w, x, y, color);
    w.flow_prev_y = y;
  }
}

// Invalidate `n` plot columns starting at screen-column c0 (wrapping past the right
// edge), mapped to the canvas's absolute box. In scope mode n is tiny, so LVGL
// renders + flushes only that band instead of the whole plot.
void invalidate_flow_span(HomeWidgets& w, int c0, int n) {
  if (w.flow_canvas == nullptr || n <= 0) return;
  if (n > w.flow_w) n = w.flow_w;
  lv_area_t box;
  lv_obj_get_coords(w.flow_canvas, &box);
  while (n > 0) {
    const int start = ((c0 % w.flow_w) + w.flow_w) % w.flow_w;
    int run = w.flow_w - start;  // stop at the right edge; the loop handles the wrap
    if (run > n) run = n;
    lv_area_t a = {box.x1 + start, box.y1, box.x1 + start + run - 1, box.y2};
    lv_obj_invalidate_area(w.flow_canvas, &a);
    c0 = start + run;
    n -= run;
  }
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
  out.flow_mode = 0;
  out.flow_ymax = flow_default_max(out.flow_mode);
  out.flow_weights = static_cast<float*>(lv_malloc(static_cast<size_t>(pw) * sizeof(float)));
  out.flow_flows = static_cast<float*>(lv_malloc(static_cast<size_t>(pw) * sizeof(float)));
  if (out.flow_weights == nullptr || out.flow_flows == nullptr) {  // OOM: tick no-ops
    if (out.flow_weights != nullptr) lv_free(out.flow_weights);
    if (out.flow_flows != nullptr) lv_free(out.flow_flows);
    out.flow_weights = out.flow_flows = nullptr;
    return;
  }
  for (int x = 0; x < pw; ++x) out.flow_weights[x] = out.flow_flows[x] = 0.0f;

  for (int x = 0; x < pw; ++x) clear_flow_column(out, x);  // background + gridlines

  // Unit toggle button in the plot's top-left corner (clear of the Y-label column,
  // which sits in the left margin). Tap flips g/s <-> g, re-scaling and redrawing
  // the existing trace in the new unit.
  lv_obj_t* btn = lv_button_create(card);
  lv_obj_remove_style_all(btn);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ui::theme::bg()), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_70, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_pad_hor(btn, 12, 0);
  lv_obj_set_style_pad_ver(btn, 7, 0);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 6, 6);
  lv_obj_t* blab = lv_label_create(btn);
  lv_label_set_text(blab, flow_unit(out.flow_mode));
  lv_obj_set_style_text_color(blab, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(blab, &lv_font_montserrat_20, 0);
  out.flow_unit_btn = btn;
  out.flow_unit_label = blab;

  // Four Y labels (0, max/3, 2max/3, max) in the left margin, aligned to the plot
  // rows. The top three are dynamic (updated on rescale); the bottom "0" is static.
  auto make_ylabel = [&](int y) {
    lv_obj_t* l = lv_label_create(card);
    lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, -26, y - 8);
    return l;
  };
  for (int t = 0; t < 3; ++t) out.flow_ylabels[t] = make_ylabel(ph * t / 3);  // max..max/3
  lv_label_set_text(make_ylabel(ph), "0");                                     // bottom, static
  set_flow_ylabels(out);

  // Static X age labels (window..now) below the plot — meaningful only in scroll
  // mode, where screen-x maps to a fixed age (right edge = now).
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
    out.flow_xlabels[t] = xl;
  }

  // Scope-mode alternative: a single centered "<window> s" caption (x no longer maps
  // to age there — the sweep gap marks "now"). Hidden until scope mode is active.
  out.flow_xspan_label = lv_label_create(card);
  lv_label_set_text_fmt(out.flow_xspan_label, "%d s window", kFlowWindowS);
  lv_obj_set_style_text_color(out.flow_xspan_label, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.flow_xspan_label, &lv_font_montserrat_14, 0);
  lv_obj_align(out.flow_xspan_label, LV_ALIGN_TOP_LEFT, pw / 2 - 44, ph + 4);
  lv_obj_add_flag(out.flow_xspan_label, LV_OBJ_FLAG_HIDDEN);

#ifndef ESP_PLATFORM
  // Sim only: seed a representative espresso flow curve into the rings so the graph
  // reads at a glance in the rendered PNGs. On hardware the plot starts empty. Weight
  // is the running integral of the flow rate, so a toggle to g renders coherently.
  const float dt = static_cast<float>(kFlowWindowS) / static_cast<float>(pw);  // s/column
  float wsum = 0.0f;  // accumulated weight (g)
  for (int x = 0; x < pw; ++x) {
    const float t = x / static_cast<float>(pw - 1);
    float f;  // g/s
    if (t < 0.18f) f = 2.7f * (t / 0.18f);          // pre-infuse ramp
    else if (t < 0.82f) f = 2.7f - 0.5f * ((t - 0.18f) / 0.64f);  // gentle decline
    else f = 2.2f * (1.0f - (t - 0.82f) / 0.18f);   // taper to 0
    wsum += f * dt;
    out.flow_flows[x] = f;
    out.flow_weights[x] = wsum;
  }
  redraw_flow_from_ring(out);
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

// ---- Large scale-aware layout: MICRA / SCALE device panels -----------------
// Two side-by-side cards replace the old top bar + brew/boiler/scale readout row.
// Each carries its OWN status line (so "Disconnected" is unambiguous), and the
// clock/battery move to the rail tray (see build_rail_tray). This whole section
// is large-screen + scale-enabled only; compact and the no-scale classic layout
// are untouched.

// A flex-column card that fills its share of the panel row.
lv_obj_t* make_panel_card(lv_obj_t* parent, int pad) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(card, lv_pct(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(card, pad, 0);
  lv_obj_set_style_pad_row(card, pad, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 16, 0);
  return card;
}

// Panel header: caption on the left, a status dot on the right, with status text
// beside the dot unless out_status is null (compact cards are too narrow for it —
// the dot color carries the state there).
void make_panel_header(lv_obj_t* card, const char* caption, const lv_font_t* cap_font,
                       const lv_font_t* status_font, lv_obj_t** out_dot,
                       lv_obj_t** out_status) {
  lv_obj_t* hdr = lv_obj_create(card);
  lv_obj_remove_style_all(hdr);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(hdr, lv_pct(100));
  lv_obj_set_height(hdr, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t* cap = lv_label_create(hdr);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(cap, cap_font, 0);

  lv_obj_t* sg = lv_obj_create(hdr);
  lv_obj_remove_style_all(sg);
  lv_obj_remove_flag(sg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(sg, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(sg, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(sg, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(sg, 8, 0);

  lv_obj_t* dot = lv_obj_create(sg);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 12, 12);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  *out_dot = dot;

  if (out_status != nullptr) {
    lv_obj_t* st = lv_label_create(sg);
    lv_obj_set_style_text_color(st, lv_color_hex(ui::theme::text()), 0);
    lv_obj_set_style_text_font(st, status_font, 0);
    *out_status = st;
  }
}

// A [-] value [+] stepper group: the two buttons flank a centered value label
// (the set point / target). Returns the label + buttons for updates + wiring.
void make_stepper_group(lv_obj_t* parent, int btn_size, const lv_font_t* symbol_font,
                        const lv_font_t* label_font, lv_obj_t** out_label,
                        lv_obj_t** out_minus, lv_obj_t** out_plus) {
  lv_obj_t* g = lv_obj_create(parent);
  lv_obj_remove_style_all(g);
  lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(g, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(g, 3, 0);
  *out_minus = ui::make_step_button(g, LV_SYMBOL_MINUS, btn_size, symbol_font);
  lv_obj_t* lbl = lv_label_create(g);
  // Fixed (not btn-relative) so the buttons hug the value at 14pt instead of
  // floating out; wide enough for the widest set value ("120 g" / "93.0°").
  lv_obj_set_width(lbl, 46);  // stable width so the buttons don't shift on digit changes
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(lbl, label_font, 0);
  *out_plus = ui::make_step_button(g, LV_SYMBOL_PLUS, btn_size, symbol_font);
  *out_label = lbl;
}

// The two-column body of a device panel: columns fill it equally, packed to the
// top (so captions across both panels align) and centered horizontally.
lv_obj_t* make_panel_body(lv_obj_t* card) {
  lv_obj_t* row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_flex_grow(row, 1);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  return row;
}

// One centered column (caption over value, then optional steppers below). Fills
// half the body. Returns the column so the caller can drop steppers into it.
lv_obj_t* make_panel_column(lv_obj_t* body, const char* caption, const lv_font_t* cap_font,
                            const lv_font_t* val_font, lv_obj_t** out_val) {
  lv_obj_t* col = lv_obj_create(body);
  lv_obj_remove_style_all(col);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, lv_pct(100));
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 6, 0);
  lv_obj_t* cap = lv_label_create(col);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(cap, cap_font, 0);
  lv_obj_t* val = lv_label_create(col);
  lv_obj_set_style_text_color(val, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(val, val_font, 0);
  *out_val = val;
  return col;
}

// A small, muted sub-label (set point / target shown under a value). Returns it.
lv_obj_t* add_sub_label(lv_obj_t* parent, const lv_font_t* font) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(l, font, 0);
  return l;
}

// A compact stat block: a "CAPTION ....... value" line (caption + value aligned on
// the same baseline), with a small grey sub-value (set point / target) right-
// aligned just beneath the value when out_sub is non-null. Blocks flex-grow so the
// card distributes them evenly. Returns the value label.
lv_obj_t* add_stat_row(lv_obj_t* card, const char* caption, const lv_font_t* cap_font,
                       const lv_font_t* val_font, lv_obj_t** out_sub) {
  lv_obj_t* outer = lv_obj_create(card);
  lv_obj_remove_style_all(outer);
  lv_obj_remove_flag(outer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(outer, lv_pct(100));
  lv_obj_set_flex_grow(outer, 1);
  lv_obj_set_flex_flow(outer, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(outer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                        LV_FLEX_ALIGN_CENTER);  // block centered vertically; sub right-aligned

  lv_obj_t* line = lv_obj_create(outer);
  lv_obj_remove_style_all(line);
  lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(line, lv_pct(100));
  lv_obj_set_height(line, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(line, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);  // caption + value share one line
  lv_obj_t* cap = lv_label_create(line);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(cap, cap_font, 0);
  lv_obj_t* val = lv_label_create(line);
  lv_obj_set_style_text_color(val, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(val, val_font, 0);

  if (out_sub != nullptr) *out_sub = add_sub_label(outer, cap_font);
  return val;
}

// Compact MICRA card: dot-only header + stacked BREW / STEAM rows, each with the
// set point in small grey beneath the live value (no steppers — adjust in Settings).
void build_compact_micra_card(lv_obj_t* parent, int pad, const lv_font_t* cap_font,
                              const lv_font_t* val_font, ui::HomeWidgets& out) {
  lv_obj_t* card = make_panel_card(parent, pad);
  make_panel_header(card, "MICRA", cap_font, cap_font, &out.micra_status_dot, nullptr);
  out.brew_value = add_stat_row(card, "BREW", cap_font, val_font, &out.brew_set);
  out.boiler_value = add_stat_row(card, "STEAM", cap_font, val_font, &out.boiler_set);
}

// Compact SCALE card: dot-only header + WEIGHT (with the target in small grey
// beneath, read-only — adjust in Scale settings) and TIMER.
void build_compact_scale_card(lv_obj_t* parent, int pad, const lv_font_t* cap_font,
                              const lv_font_t* val_font, ui::HomeWidgets& out) {
  lv_obj_t* card = make_panel_card(parent, pad);
  make_panel_header(card, "SCALE", cap_font, cap_font, &out.scale_status_dot, nullptr);
  out.scale_weight = add_stat_row(card, "WEIGHT", cap_font, val_font, &out.scale_target);
  out.shot_timer_label = add_stat_row(card, "TIMER", cap_font, val_font, nullptr);
}

// MICRA panel: two centered temperature columns (BREW / STEAM), each caption ->
// live value -> [-] set [+].
void build_micra_panel(lv_obj_t* parent, const lv_font_t* cap_font,
                       const lv_font_t* status_font, const lv_font_t* val_font,
                       const lv_font_t* set_font, int btn_size,
                       const lv_font_t* symbol_font, int pad, ui::HomeWidgets& out) {
  lv_obj_t* card = make_panel_card(parent, pad);
  make_panel_header(card, "MICRA", cap_font, status_font, &out.micra_status_dot,
                    &out.micra_status_label);
  lv_obj_t* body = make_panel_body(card);
  lv_obj_t* bcol = make_panel_column(body, "BREW", cap_font, val_font, &out.brew_value);
  make_stepper_group(bcol, btn_size, symbol_font, set_font, &out.brew_set,
                     &out.brew_minus, &out.brew_plus);
  lv_obj_t* scol = make_panel_column(body, "STEAM", cap_font, val_font, &out.boiler_value);
  make_stepper_group(scol, btn_size, symbol_font, set_font, &out.boiler_set,
                     &out.boiler_minus, &out.boiler_plus);
}

// SCALE panel: centered WEIGHT + TIMER columns, with the target stepper tucked
// under the weight column (same caption/value/stepper pattern as MICRA).
void build_scale_panel(lv_obj_t* parent, const lv_font_t* cap_font,
                       const lv_font_t* status_font, const lv_font_t* big_font,
                       const lv_font_t* set_font, int btn_size,
                       const lv_font_t* symbol_font, int pad, ui::HomeWidgets& out) {
  lv_obj_t* card = make_panel_card(parent, pad);
  make_panel_header(card, "SCALE", cap_font, status_font, &out.scale_status_dot,
                    &out.scale_status_label);
  lv_obj_t* body = make_panel_body(card);
  lv_obj_t* wcol = make_panel_column(body, "WEIGHT", cap_font, big_font, &out.scale_weight);
  make_stepper_group(wcol, btn_size, symbol_font, set_font, &out.scale_target,
                     &out.target_minus, &out.target_plus);
  make_panel_column(body, "TIMER", cap_font, big_font, &out.shot_timer_label);
}

}  // namespace

namespace ui {

void build_rail_tray(lv_obj_t* rail, const lv_font_t* font, HomeWidgets& out) {
  // The caller switches the rail to SPACE_BETWEEN, so the three tab buttons spread
  // down the rail and this tray anchors the bottom — the previously-dead middle
  // space is reclaimed as tab spacing instead of a void. The tray is a plain object
  // (not lv_button), so the tabview — which counts/indexes tabs by button class —
  // ignores it entirely.
  lv_obj_t* tray = lv_obj_create(rail);
  lv_obj_remove_style_all(tray);
  lv_obj_remove_flag(tray, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(tray, lv_pct(100));
  lv_obj_set_height(tray, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(tray, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tray, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(tray, 4, 0);

  out.clock_label = lv_label_create(tray);
  lv_obj_set_style_text_color(out.clock_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.clock_label, font, 0);

  out.battery_label = lv_label_create(tray);
  lv_obj_set_style_text_color(out.battery_label, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.battery_label, font, 0);

  out.batt_timer = lv_timer_create(battery_anim_cb, 350, &out);  // drives charge anim
}

void build_bottom_tray(lv_obj_t* bar, const lv_font_t* font, HomeWidgets& out) {
  // Compact tray: the tab bar runs along the bottom. The tab buttons flex-grow to
  // fill the bar (App sizes them), so this tray — a plain, non-grow object the
  // tabview ignores for tab counting — gets pushed to the right. Clock over battery,
  // stacked, right-aligned (fits the ~44px bar at this font).
  lv_obj_t* tray = lv_obj_create(bar);
  lv_obj_remove_style_all(tray);
  lv_obj_remove_flag(tray, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(tray, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(tray, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tray, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                        LV_FLEX_ALIGN_END);

  out.clock_label = lv_label_create(tray);
  lv_obj_set_style_text_color(out.clock_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.clock_label, font, 0);

  out.battery_label = lv_label_create(tray);
  lv_obj_set_style_text_color(out.battery_label, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.battery_label, font, 0);

  out.batt_timer = lv_timer_create(battery_anim_cb, 350, &out);  // drives charge anim
}

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
  out.status_dot = out.status_label = nullptr;
  out.micra_status_dot = out.micra_status_label = nullptr;
  out.scale_status_dot = out.scale_status_label = nullptr;
  out.shot_timer_label = out.target_minus = out.target_plus = nullptr;
  out.flow_canvas = nullptr;  // flow_buf is freed by App before each rebuild

  const int pad = compact ? 8 : xl ? 28 : 20;
  const int gap = compact ? 6 : xl ? 22 : 16;
  const int card_pad = compact ? 8 : xl ? 24 : 16;
  const int btn_h = compact ? 40 : xl ? 92 : 64;
  const lv_font_t* sub_font =
      compact ? &lv_font_montserrat_14 : xl ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
  const lv_font_t* btn_font =
      compact ? &lv_font_montserrat_14 : xl ? &lv_font_montserrat_28 : &lv_font_montserrat_20;

  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, pad, 0);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(parent, gap, 0);

  // No top bar in any layout now: status lives in card headers, clock/battery in a
  // tray (built by App — bottom bar on compact, side rail on large).

  // --- Compact Home: MICRA (+ SCALE) card(s) + actions; clock/battery live in the
  // bottom-bar tray. No on-Home steppers — temp/target adjust stay in Settings. ---
  if (compact) {
    const lv_font_t* c_cap = &lv_font_montserrat_14;
    lv_obj_t* cards = lv_obj_create(parent);
    lv_obj_remove_style_all(cards);
    lv_obj_remove_flag(cards, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(cards, lv_pct(100));
    lv_obj_set_flex_grow(cards, 1);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cards, gap, 0);

    if (scale_enabled) {
      // Two half-width cards with stacked readout rows (narrow -> smaller value).
      build_compact_micra_card(cards, card_pad, c_cap, &lv_font_montserrat_20, out);
      build_compact_scale_card(cards, card_pad, c_cap, &lv_font_montserrat_20, out);
    } else {
      // One full-width MICRA card: BREW / STEAM columns, each with the set point in
      // small grey beneath the value (room for a bigger value at full width).
      lv_obj_t* card = make_panel_card(cards, card_pad);
      make_panel_header(card, "MICRA", c_cap, sub_font, &out.micra_status_dot,
                        &out.micra_status_label);
      lv_obj_t* body = make_panel_body(card);
      lv_obj_t* bcol = make_panel_column(body, "BREW", c_cap, &lv_font_montserrat_28,
                                         &out.brew_value);
      lv_obj_set_flex_align(bcol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);  // center the cluster vertically
      out.brew_set = add_sub_label(bcol, c_cap);
      lv_obj_t* scol = make_panel_column(body, "STEAM", c_cap, &lv_font_montserrat_28,
                                         &out.boiler_value);
      lv_obj_set_flex_align(scol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);
      out.boiler_set = add_sub_label(scol, c_cap);
    }

    // Actions: Power (under MICRA, left) then Tare (under SCALE, right); just Power
    // when there's no scale.
    lv_obj_t* actions = lv_obj_create(parent);
    lv_obj_remove_style_all(actions);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(actions, lv_pct(100));
    lv_obj_set_height(actions, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, gap, 0);
    build_power_button(actions, btn_h, btn_font, out);
    lv_obj_set_width(out.power_btn, 0);
    lv_obj_set_flex_grow(out.power_btn, 1);
    if (scale_enabled) {
      build_tare_button(actions, btn_font, out);
      lv_obj_set_flex_grow(out.tare_btn, 1);
      lv_obj_set_height(out.tare_btn, btn_h);
    }
    return;
  }

  // Shared large-screen panel sizing (the MICRA / SCALE cards + their steppers).
  const int panel_h = xl ? 210 : 168;
  const lv_font_t* panel_cap = &lv_font_montserrat_14;
  const lv_font_t* panel_status = sub_font;                       // 20 wide / 28 xl
  // One value size for BOTH panels so the value rows + steppers line up across
  // MICRA and SCALE (weight/timer are not a bigger "hero" — they match the temps).
  const lv_font_t* panel_val = xl ? &lv_font_montserrat_28 : &lv_font_montserrat_24;
  const lv_font_t* panel_set = &lv_font_montserrat_14;
  const int panel_btn = xl ? 48 : 36;
  const lv_font_t* panel_sym = xl ? &lv_font_montserrat_28 : &lv_font_montserrat_20;

  if (!scale_enabled) {
    // --- Large no-scale Home: one MICRA card (BREW / STEAM + steppers) + power;
    // clock/battery in the rail tray, same as the scale-aware layout. ---
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, xl ? 300 : 220);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    build_micra_panel(row, panel_cap, panel_status, panel_val, panel_set, panel_btn,
                      panel_sym, card_pad, out);

    lv_obj_t* spacer = lv_obj_create(parent);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, lv_pct(100));
    lv_obj_set_height(spacer, 0);
    lv_obj_set_flex_grow(spacer, 1);

    build_power_button(parent, btn_h, btn_font, out);
    return;
  }

  // Large scale-aware screens: two device panels (MICRA / SCALE) with their own
  // status + steppers, weight and shot timer; then the flow graph as the hero;
  // then Tare + Power. Clock/battery live in the rail tray (build_rail_tray).
  lv_obj_t* panels = lv_obj_create(parent);
  lv_obj_remove_style_all(panels);
  lv_obj_remove_flag(panels, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(panels, lv_pct(100));
  lv_obj_set_height(panels, panel_h);
  lv_obj_set_flex_flow(panels, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(panels, gap, 0);
  build_micra_panel(panels, panel_cap, panel_status, panel_val, panel_set, panel_btn,
                    panel_sym, card_pad, out);
  build_scale_panel(panels, panel_cap, panel_status, panel_val, panel_set, panel_btn,
                    panel_sym, card_pad, out);

  // The flow graph fills the rest — the hero. The card is created here for correct
  // flex order, but its canvas is sized only after the actions row exists and the
  // layout is final (populate_flow_graph below).
  lv_obj_t* graph_card = make_flow_graph_card(parent);

  // Power (under MICRA, left) beside Tare (under SCALE, right) — each action sits
  // below the card it belongs to.
  lv_obj_t* actions = lv_obj_create(parent);
  lv_obj_remove_style_all(actions);
  lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(actions, lv_pct(100));
  lv_obj_set_height(actions, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(actions, gap, 0);
  build_power_button(actions, btn_h, btn_font, out);
  lv_obj_set_width(out.power_btn, 0);
  lv_obj_set_flex_grow(out.power_btn, 1);  // equal halves with the tare button
  build_tare_button(actions, btn_font, out);
  lv_obj_set_height(out.tare_btn, btn_h);
  lv_obj_set_width(out.tare_btn, 0);
  lv_obj_set_flex_grow(out.tare_btn, 1);

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

  // Temperatures: real values when connected, placeholders otherwise. The set point
  // is always compact ("93.0°" — the live value above/beside it carries the unit),
  // whether it's a stepper's value (large) or a grey sub-value (compact).
  char buf[24];
  if (connected) {
    format_now(buf, sizeof(buf), state.brew_temp_c, fahrenheit);
    lv_label_set_text(w.brew_value, buf);
    if (w.brew_set != nullptr) {
      std::snprintf(buf, sizeof(buf), "%.1f°",
                    static_cast<double>(ui::temp_disp(state.brew_target_c, fahrenheit)));
      lv_label_set_text(w.brew_set, buf);
    }
    if (state.steam_enabled) {
      format_now(buf, sizeof(buf), state.boiler_temp_c, fahrenheit);
      lv_label_set_text(w.boiler_value, buf);
      if (w.boiler_set != nullptr) {
        std::snprintf(buf, sizeof(buf), "%.0f°",
                      static_cast<double>(ui::temp_disp(state.boiler_target_c, fahrenheit)));
        lv_label_set_text(w.boiler_set, buf);
      }
    } else {
      lv_label_set_text(w.boiler_value, "Off");
      if (w.boiler_set != nullptr) lv_label_set_text(w.boiler_set, "");
    }
  } else {
    lv_label_set_text(w.brew_value, "--");
    if (w.brew_set != nullptr) lv_label_set_text(w.brew_set, "--");
    lv_label_set_text(w.boiler_value, "--");
    if (w.boiler_set != nullptr) lv_label_set_text(w.boiler_set, "--");
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
    // Every layout now labels this "TARGET" (a stepper's value on large screens, a
    // read-only row on compact), so it's just the number of grams.
    std::snprintf(tb, sizeof(tb), "%.0f g", static_cast<double>(brew.target_weight_g));
    lv_label_set_text(w.scale_target, tb);

    // Shot timer (scale panel): the scale's built-in timer, mm:ss-free seconds.
    if (w.shot_timer_label != nullptr) {
      char sb[16];
      if (scale.connected) {
        std::snprintf(sb, sizeof(sb), "%.1f s",
                      static_cast<double>(scale.timer_ms) / 1000.0);
      } else {
        std::snprintf(sb, sizeof(sb), "-- s");
      }
      lv_label_set_text(w.shot_timer_label, sb);
    }

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

  // Micra status: text + dot color from link + power. Shown in the top-bar status
  // (compact / no-scale) or the MICRA panel header (large scale-aware).
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
  if (w.status_label != nullptr) {
    lv_label_set_text(w.status_label, status);
    lv_obj_set_style_bg_color(w.status_dot, lv_color_hex(dot), 0);
  }
  // Panel/card dots + optional status text. Compact card headers are dot-only
  // (label null), so the dot is set independently of the label.
  if (w.micra_status_dot != nullptr)
    lv_obj_set_style_bg_color(w.micra_status_dot, lv_color_hex(dot), 0);
  if (w.micra_status_label != nullptr) {
    // Header caption is already "MICRA", so drop the wordy Unconfigured hint.
    const char* mtxt = state.link == core::Link::Unconfigured ? "Not set up" : status;
    lv_label_set_text(w.micra_status_label, mtxt);
  }
  const uint32_t scale_dot = scale.connected ? ui::theme::ok() : ui::theme::alert();
  if (w.scale_status_dot != nullptr)
    lv_obj_set_style_bg_color(w.scale_status_dot, lv_color_hex(scale_dot), 0);
  if (w.scale_status_label != nullptr)
    lv_label_set_text(w.scale_status_label, scale.connected ? "Connected" : "Disconnected");

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
  if (w.flow_canvas == nullptr || w.flow_buf == nullptr || w.flow_weights == nullptr ||
      w.flow_flows == nullptr || w.flow_w <= 0)
    return;

  // Scale disconnected: blank the plot instead of scrolling a fake zero line in.
  // Clear once on the transition to an empty grid, then idle until it reconnects.
  if (!scale.connected) {
    if (!w.flow_blanked) {
      for (int x = 0; x < w.flow_w; ++x) {
        w.flow_weights[x] = 0.0f;
        w.flow_flows[x] = 0.0f;
        clear_flow_column(w, x);
      }
      w.flow_prev_y = -1;
      w.flow_tick = 0;      // restart the scroll clock cleanly on reconnect
      w.flow_accum_ms = 0;
      w.flow_ymax = flow_default_max(w.flow_mode);
      set_flow_ylabels(w);
      w.flow_blanked = true;
      lv_obj_invalidate(w.flow_canvas);
    }
    return;
  }
  w.flow_blanked = false;

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

  // Flow = signed rate of weight gain, measured over kFlowRateWindowMs of weight
  // history (rw vs the weight that many steps back). A rising weight is real flow; a
  // falling weight (cup removed) is negative. drop-negative floors that to zero so it
  // never shows as an upswing; with it off we show the magnitude (the raw activity).
  // The most-recent stored sample is at the right edge (scroll) or the cursor (scope).
  const float rw = scale.weight_g;   // g (signed; the scale streams weight, not flow)
  const int last_idx = w.flow_scope_mode ? w.flow_cursor : (w.flow_w - 1);
  int back = static_cast<int>(kFlowRateWindowMs / ms_per_step);
  if (back < 1) back = 1;
  if (back > w.flow_w - 1) back = w.flow_w - 1;
  int back_idx = last_idx - back;
  if (back_idx < 0) back_idx += w.flow_w;  // wraps in scope mode; a no-op in scroll
  const float win_s = back * static_cast<float>(ms_per_step) / 1000.0f;
  float rf = (win_s > 0.0f) ? (rw - w.flow_weights[back_idx]) / win_s : 0.0f;
  if (w.flow_drop_negative) { if (rf < 0.0f) rf = 0.0f; }
  else if (rf < 0.0f) rf = -rf;   // show outflow as positive activity when not dropping
  const float raw = (w.flow_mode == 1) ? rw : rf;

  const uint16_t bright = flow_bright_color();
  const int scope_start = w.flow_cursor;  // for the scope-mode partial invalidation
  bool scope_wrapped = false;             // a wrap forces a full re-blit (dim redraw)
  int steps = 0;
  if (!w.flow_scope_mode) {
    // Scroll: memmove the whole plot + both rings one step left, newest at the right.
    const int rx = w.flow_w - 1;
    while (w.flow_accum_ms >= ms_per_step && steps < w.flow_w) {
      w.flow_accum_ms -= ms_per_step;
      ++steps;
      for (int i = 0; i < kFlowStepPx; ++i) {
        shift_flow_left(w);
        std::memmove(w.flow_weights, w.flow_weights + 1,
                     static_cast<size_t>(w.flow_w - 1) * sizeof(float));
        std::memmove(w.flow_flows, w.flow_flows + 1,
                     static_cast<size_t>(w.flow_w - 1) * sizeof(float));
        w.flow_weights[rx] = rw;
        w.flow_flows[rx] = rf;
        const int y = flow_row(raw, w.flow_h, w.flow_ymax);
        clear_flow_column(w, rx);
        draw_flow_segment(w, rx, y, bright);
        w.flow_prev_y = y;
      }
    }
  } else {
    // Scope: advance a wrapping write head, paint only its column bright + a blank gap
    // ahead. On wrap, re-draw the finished sweep dim so the fresh edge stands out.
    while (w.flow_accum_ms >= ms_per_step && steps < w.flow_w) {
      w.flow_accum_ms -= ms_per_step;
      ++steps;
      for (int i = 0; i < kFlowStepPx; ++i) {
        int c = w.flow_cursor + 1;
        if (c >= w.flow_w) c = 0;
        w.flow_cursor = c;
        w.flow_weights[c] = rw;
        w.flow_flows[c] = rf;
        if (c == 0) {  // wrapped: age the just-finished sweep to dim, pen up for the new
          redraw_flow_from_ring(w);
          scope_wrapped = true;
          w.flow_prev_y = -1;
        }
        const int y = flow_row(raw, w.flow_h, w.flow_ymax);
        clear_flow_column(w, c);
        draw_flow_segment(w, c, y, bright);
        w.flow_prev_y = y;
        for (int g = 1; g <= kFlowScopeGapPx; ++g) {  // blank gap just ahead of the head
          int gc = c + g;
          if (gc >= w.flow_w) gc -= w.flow_w;
          clear_flow_column(w, gc);
        }
      }
    }
  }
  if (steps == 0) return;

  // Auto-range the Y axis: grow immediately to fit the window's peak; shrink back
  // toward the mode default only once the peak drops below half scale (hysteresis,
  // so it settles instead of flip-flopping at a threshold). On a change, redraw the
  // whole plot from the ring (existing pixels are at the old scale).
  float wmax = 0.0f;
  const float* ring = flow_active_ring(w);
  for (int x = 0; x < w.flow_w; ++x)
    if (ring[x] > wmax) wmax = ring[x];
  const float want = flow_nice_max(w.flow_mode, wmax);
  const bool grow = want > w.flow_ymax;
  const bool shrink = want < w.flow_ymax && wmax < w.flow_ymax * 0.5f;
  bool rescaled = false;
  if (grow || shrink) {
    w.flow_ymax = want;
    redraw_flow_from_ring(w);
    set_flow_ylabels(w);
    rescaled = true;
  }

  if (!w.flow_scope_mode || rescaled || scope_wrapped) {
    // Scroll shifts everything; a rescale/wrap redrew everything: re-blit the whole plot.
    lv_obj_invalidate(w.flow_canvas);
  } else {
    // Scope: only the columns written this tick (+ the gap ahead) changed.
    invalidate_flow_span(w, scope_start + 1, steps * kFlowStepPx + kFlowScopeGapPx);
  }
}

void toggle_flow_mode(HomeWidgets& w) {
  if (w.flow_canvas == nullptr || w.flow_buf == nullptr || w.flow_weights == nullptr ||
      w.flow_flows == nullptr)
    return;
  w.flow_mode ^= 1;
  // Both rings are kept live by flow_graph_tick, so the existing trace survives the
  // switch: re-scale the axis to the now-active quantity's window peak and redraw in
  // the new unit (unless blanked — leave the empty grid until the scale reconnects).
  if (w.flow_unit_label != nullptr) lv_label_set_text(w.flow_unit_label, flow_unit(w.flow_mode));
  if (w.flow_blanked) {
    w.flow_ymax = flow_default_max(w.flow_mode);
    set_flow_ylabels(w);
    lv_obj_invalidate(w.flow_canvas);
    return;
  }
  const float* ring = flow_active_ring(w);
  float wmax = 0.0f;
  for (int x = 0; x < w.flow_w; ++x)
    if (ring[x] > wmax) wmax = ring[x];
  w.flow_ymax = flow_nice_max(w.flow_mode, wmax);
  set_flow_ylabels(w);
  redraw_flow_from_ring(w);
  lv_obj_invalidate(w.flow_canvas);
}

void set_flow_drop_negative(HomeWidgets& w, bool on) {
  w.flow_drop_negative = on;
  if (w.flow_flows == nullptr) return;
  // The g/s ring was recorded under the old policy — clear it so the trace doesn't
  // mix clamped and unclamped history. The weight ring is unaffected.
  for (int x = 0; x < w.flow_w; ++x) w.flow_flows[x] = 0.0f;
  if (w.flow_mode == 0 && !w.flow_blanked) {  // g/s shown -> redraw the now-empty plot
    w.flow_ymax = flow_default_max(0);
    set_flow_ylabels(w);
    redraw_flow_from_ring(w);
    lv_obj_invalidate(w.flow_canvas);
  }
}

void apply_flow_xaxis_labels(HomeWidgets& w) {
  const bool scope = w.flow_scope_mode;
  for (lv_obj_t* xl : w.flow_xlabels) {
    if (xl == nullptr) continue;
    if (scope) lv_obj_add_flag(xl, LV_OBJ_FLAG_HIDDEN);   // age ticks lie in scope mode
    else lv_obj_remove_flag(xl, LV_OBJ_FLAG_HIDDEN);
  }
  if (w.flow_xspan_label != nullptr) {
    if (scope) lv_obj_remove_flag(w.flow_xspan_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(w.flow_xspan_label, LV_OBJ_FLAG_HIDDEN);
  }
}

void set_flow_scope_mode(HomeWidgets& w, bool on) {
  w.flow_scope_mode = on;
  apply_flow_xaxis_labels(w);
  if (w.flow_weights == nullptr || w.flow_flows == nullptr) return;
  // Scroll and scope index the ring/pixels differently, so the existing trace can't
  // carry over — reset to an empty grid and let the tick refill it in the new style.
  w.flow_cursor = 0;
  w.flow_prev_y = -1;
  w.flow_tick = 0;
  w.flow_accum_ms = 0;
  w.flow_blanked = false;
  w.flow_ymax = flow_default_max(w.flow_mode);
  for (int x = 0; x < w.flow_w; ++x) {
    w.flow_weights[x] = 0.0f;
    w.flow_flows[x] = 0.0f;
    clear_flow_column(w, x);
  }
  set_flow_ylabels(w);
  if (w.flow_canvas != nullptr) lv_obj_invalidate(w.flow_canvas);
}

}  // namespace ui
