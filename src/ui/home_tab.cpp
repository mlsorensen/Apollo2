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
constexpr int kFlowWindowS = 45;       // seconds spanned by the full plot width
// Pixels scrolled per re-blit. The whole-plot copy is the cost, so scrolling N px
// at 1/N the cadence keeps the same average speed for ~1/N the CPU (chunkier
// motion). 1 = smoothest/most expensive; bump to trade smoothness for CPU.
constexpr int kFlowStepPx = 1;
// We derive flow ourselves as the rate of weight gain (scales reliably stream
// weight, not flow). Measure it over this window of the weight history so per-
// sample jitter averages out instead of producing a spiky line.
// 750ms window: ~7 scale events at the Bookoo's 10Hz — enough that any real
// flow spans multiple resolution steps (no fake zero-dips down to ~0.13 g/s),
// while reacting twice as fast as the earlier 1.5s window.
constexpr uint32_t kFlowRateWindowMs = 750;   // trailing derivative window
constexpr uint32_t kFlowHistSampleMs = 50;    // weight-history sampling cadence
constexpr float kFlowMinSpanS = 0.4f;         // need this much history for a rate
constexpr uint32_t kShotSampleMs = 100;       // shot-plot cadence == scale event rate
constexpr uint32_t kShotMinWindowMs = 15000;   // shot plot's smallest X window
constexpr uint32_t kShotWindowStepMs = 15000;  // window grows in snaps (stable mapping
                                               // between snaps -> cheap edge painting):
                                               // 15s -> 30s -> 45s -> 60s, wide enough
                                               // that the rescale stays infrequent
constexpr uint32_t kShotMaxWindowMs = 60000;   // then the window slides
constexpr uint32_t kShotSlideStepMs = 2000;   // past the cap, the slide also snaps
// Oscilloscope mode: blank columns kept just ahead of the write head so the sweep
// point reads clearly (fresh trace behind it, gap in front).
constexpr int kFlowScopeGapPx = 4;

// Modes: 0 = flow rate (g/s), 1 = weight (g). Each has a unit + a default axis
// full-scale, which is also the floor the axis shrinks back to.
const char* flow_unit(int mode) { return mode == 1 ? "g" : "g/s"; }
float flow_default_max(int mode) { return mode == 1 ? 10.0f : 6.0f; }

// Smallest "nice" axis >= v for the mode (even / multiple of 10 so the labels
// 0, max/2, max stay clean), floored at the mode default.
float flow_nice_max(int mode, float v) {
  const float floor = flow_default_max(mode);
  if (v <= floor) return floor;
  const float step = (mode == 1) ? 15.0f : 3.0f;  // multiples of 3 -> clean thirds
  return step * std::ceil(v / step);
}

// Map a value to a canvas row against the current axis: 0 = top = ymax, h-1 = 0.
// Returns a float row: the fractional part carries sub-pixel position, which the
// anti-aliased segment renderer turns into edge coverage instead of a whole-pixel
// jump (a hard 1px line visibly "steps" as the plot scrolls).
float flow_row(float v, int h, float ymax) {
  if (v < 0.0f) v = 0.0f;
  float frac = (ymax > 0.0f) ? v / ymax : 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  float y = (1.0f - frac) * static_cast<float>(h - 1);
  if (y < 0.0f) y = 0.0f;
  if (y > static_cast<float>(h - 1)) y = static_cast<float>(h - 1);
  return y;
}

// Alpha-blend src over dst in RGB565 (a = 0..255). Integer per-channel mix; cheap
// enough for the few edge pixels per column the AA renderer touches.
uint16_t blend565(uint16_t dst, uint16_t src, uint8_t a) {
  const uint32_t sr = (src >> 11) & 0x1F, sg = (src >> 5) & 0x3F, sb = src & 0x1F;
  const uint32_t dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
  const uint32_t r = (sr * a + dr * (255 - a)) / 255;
  const uint32_t g = (sg * a + dg * (255 - a)) / 255;
  const uint32_t b = (sb * a + db * (255 - a)) / 255;
  return static_cast<uint16_t>((r << 11) | (g << 5) | b);
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

// Fill the area under the line in column x with a translucent wash of the trace
// color (~40% over the plot background). Painted after the column clear and
// before the line, so the line's AA feather blends onto the fill seamlessly.
// Gridline rows get the wash blended over the gridline color so the grid stays
// readable through the fill.
void fill_flow_below(HomeWidgets& w, int x, float yf, uint16_t trace_color) {
  const float y0f = (w.flow_prev_y < 0.0f) ? yf : w.flow_prev_y;
  const float b = (y0f < yf ? yf : y0f);  // bottom of the line span at this column
  int start = static_cast<int>(b) + 1;
  if (start < 0) start = 0;
  if (start >= w.flow_h) return;
  const uint16_t bg = lv_color_to_u16(lv_color_hex(ui::theme::card()));
  const uint16_t fill = blend565(bg, trace_color, 102);  // ~40% wash
  int gy[2];
  for (int t = 1; t <= 2; ++t) gy[t - 1] = w.flow_h * t / 3;  // gridline rows
  uint16_t* col = w.flow_buf + x;
  for (int yy = start; yy < w.flow_h; ++yy) {
    uint16_t* px = col + static_cast<size_t>(yy) * w.flow_stride;
    // Gridlines were just repainted by clear_flow_column: wash over them too.
    *px = (yy == gy[0] || yy == gy[1]) ? blend565(*px, trace_color, 102) : fill;
  }
}

// Draw the flow line into column x in `color`: a vertical run joining the previous
// pen row to the new one, so consecutive samples connect into a continuous line.
// Anti-aliased: the run covers [lo, hi] with a half-pixel of feather each side,
// and each row is painted at its fractional coverage (blended over what's already
// in the column). Sub-pixel motion then reads as intensity easing instead of the
// line snapping a whole pixel per step.
void draw_flow_segment(HomeWidgets& w, int x, float yf, uint16_t color) {
  // Line thickness: the trace reads as ~2*halfwidth px (solid core + one
  // feathered row each side). Scaled so the trace keeps its physical weight on
  // high-DPI panels.
  const float kHalfWidth = 1.25f * ui::scale();
  const float y0f = (w.flow_prev_y < 0.0f) ? yf : w.flow_prev_y;
  const float a = (y0f < yf ? y0f : yf) - kHalfWidth;
  const float b = (y0f < yf ? yf : y0f) + kHalfWidth;
  uint16_t* col = w.flow_buf + x;
  int lo = static_cast<int>(a);      // first candidate row (a >= -0.75 -> lo >= -1)
  int hi = static_cast<int>(b + 1.0f);
  if (lo < 0) lo = 0;
  if (hi >= w.flow_h) hi = w.flow_h - 1;
  for (int yy = lo; yy <= hi; ++yy) {
    // Coverage of pixel row [yy-0.5, yy+0.5] by the span [a, b], 0..1.
    float ov = (b < yy + 0.5f ? b : yy + 0.5f) - (a > yy - 0.5f ? a : yy - 0.5f);
    if (ov <= 0.0f) continue;
    if (ov > 1.0f) ov = 1.0f;
    uint16_t* px = col + static_cast<size_t>(yy) * w.flow_stride;
    if (ov >= 0.996f) {
      *px = color;
    } else {
      *px = blend565(*px, color, static_cast<uint8_t>(ov * 255.0f + 0.5f));
    }
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
    const float y = flow_row(ring[x], w.flow_h, w.flow_ymax);
    clear_flow_column(w, x);
    // Scope mode: columns past the cursor belong to the previous sweep -> dim them.
    const uint16_t color = (w.flow_scope_mode && x > w.flow_cursor) ? dim : bright;
    fill_flow_below(w, x, y, color);
    draw_flow_segment(w, x, y, color);
    w.flow_prev_y = y;
  }
  // Leave the pen at the LIVE head, not the ring's last column. In scope mode
  // the last column is the previous sweep's tail (usually ~0): a mid-sweep
  // rescale that returned with the pen there made the next painted column draw
  // a phantom vertical stab down to zero (baked into the canvas, absent from
  // the ring — the "line to zero and back" artifact).
  if (w.flow_scope_mode)
    w.flow_prev_y = flow_row(ring[w.flow_cursor], w.flow_h, w.flow_ymax);
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
  lv_obj_set_style_radius(card, ui::dp(16), 0);
  lv_obj_set_style_pad_left(card, ui::dp(30), 0);    // Y labels
  lv_obj_set_style_pad_bottom(card, ui::dp(22), 0);  // X labels
  lv_obj_set_style_pad_top(card, ui::dp(18), 0);
  lv_obj_set_style_pad_right(card, ui::dp(12), 0);
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
  lv_obj_set_style_radius(btn, ui::dp(8), 0);
  lv_obj_set_style_pad_hor(btn, ui::dp(12), 0);
  lv_obj_set_style_pad_ver(btn, ui::dp(7), 0);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, ui::dp(6), ui::dp(6));
  lv_obj_t* blab = lv_label_create(btn);
  lv_label_set_text(blab, flow_unit(out.flow_mode));
  lv_obj_set_style_text_color(blab, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(blab, ui::font_dp(20), 0);
  out.flow_unit_btn = btn;
  out.flow_unit_label = blab;

  // Four Y labels (0, max/3, 2max/3, max) in the left margin, aligned to the plot
  // rows. The top three are dynamic (updated on rescale); the bottom "0" is static.
  auto make_ylabel = [&](int y) {
    lv_obj_t* l = lv_label_create(card);
    lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(l, ui::font_dp(14), 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, ui::dp(-26), y - ui::dp(8));
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
    lv_obj_set_style_text_font(xl, ui::font_dp(14), 0);
    int x = pw * t / 3;
    if (t == 3) x -= ui::dp(24);
    else if (t > 0) x -= ui::dp(12);
    lv_obj_align(xl, LV_ALIGN_TOP_LEFT, x, ph + ui::dp(4));
    out.flow_xlabels[t] = xl;
  }

  // Scope-mode alternative: a single centered "<window> s" caption (x no longer maps
  // to age there — the sweep gap marks "now"). Hidden until scope mode is active.
  out.flow_xspan_label = lv_label_create(card);
  lv_label_set_text_fmt(out.flow_xspan_label, "%d s window", kFlowWindowS);
  lv_obj_set_style_text_color(out.flow_xspan_label, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.flow_xspan_label, ui::font_dp(14), 0);
  lv_obj_align(out.flow_xspan_label, LV_ALIGN_TOP_LEFT, pw / 2 - ui::dp(44), ph + ui::dp(4));
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

// The full-width power button at the bottom (both layouts).
void build_power_button(lv_obj_t* parent, int btn_h, const lv_font_t* btn_font,
                        ui::HomeWidgets& out) {
  out.power_btn = ui::make_button(parent);
  lv_obj_set_width(out.power_btn, lv_pct(100));
  lv_obj_set_height(out.power_btn, btn_h);
  lv_obj_set_style_radius(out.power_btn, ui::dp(14), 0);
  out.power_label = lv_label_create(out.power_btn);
  lv_obj_set_style_text_color(out.power_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.power_label, btn_font, 0);
  lv_obj_center(out.power_label);
}

// A Tare button (icon + label). Returns it (the caller sizes/places it).
void build_tare_button(lv_obj_t* parent, const lv_font_t* font, ui::HomeWidgets& out) {
  out.tare_btn = ui::make_button(parent);
  lv_obj_set_style_bg_color(out.tare_btn, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_radius(out.tare_btn, ui::dp(14), 0);
  lv_obj_set_style_opa(out.tare_btn, LV_OPA_40, LV_STATE_DISABLED);  // scale gone
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
  lv_obj_set_style_radius(card, ui::dp(16), 0);
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
  lv_obj_set_style_pad_column(sg, ui::dp(8), 0);

  lv_obj_t* dot = lv_obj_create(sg);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, ui::dp(12), ui::dp(12));
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
  lv_obj_set_style_pad_column(g, ui::dp(3), 0);
  *out_minus = ui::make_step_button(g, LV_SYMBOL_MINUS, btn_size, symbol_font);
  lv_obj_t* lbl = lv_label_create(g);
  // Fixed (not btn-relative) so the buttons hug the value at 14pt instead of
  // floating out; wide enough for the widest set value ("120 g" / "93.0°").
  lv_obj_set_width(lbl, ui::dp(46));  // stable width so the buttons don't shift on digit changes
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
  lv_obj_set_style_pad_row(col, ui::dp(6), 0);
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
// live value -> [-] set [+], with the power/standby button along the bottom
// (each device's controls live in its own card — no bottom action bar).
void build_micra_panel(lv_obj_t* parent, const lv_font_t* cap_font,
                       const lv_font_t* status_font, const lv_font_t* val_font,
                       const lv_font_t* set_font, int btn_size,
                       const lv_font_t* symbol_font, int pad, int action_h,
                       const lv_font_t* action_font, ui::HomeWidgets& out) {
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
  // Center the clusters in the body so the card's slack splits above and below
  // them — breathing room against the header AND the action button.
  lv_obj_set_flex_align(bcol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_flex_align(scol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  build_power_button(card, action_h, action_font, out);
}

// SCALE panel: centered WEIGHT + TIMER columns, with the target stepper tucked
// under the weight column (same caption/value/stepper pattern as MICRA), and
// Connect/Disconnect + Tare side by side along the bottom.
void build_scale_panel(lv_obj_t* parent, const lv_font_t* cap_font,
                       const lv_font_t* status_font, const lv_font_t* big_font,
                       const lv_font_t* set_font, int btn_size,
                       const lv_font_t* symbol_font, int pad, int action_h,
                       const lv_font_t* action_font, ui::HomeWidgets& out) {
  lv_obj_t* card = make_panel_card(parent, pad);
  make_panel_header(card, "SCALE", cap_font, status_font, &out.scale_status_dot,
                    &out.scale_status_label);
  // Scale battery: icon-only level estimate to the right of the status text
  // (same flex group as the dot + status). update_home shows/colors it when
  // the connected scale reports a level.
  out.scale_batt_label = lv_label_create(lv_obj_get_parent(out.scale_status_dot));
  lv_obj_set_style_text_font(out.scale_batt_label, status_font, 0);
  lv_obj_set_style_text_color(out.scale_batt_label, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_add_flag(out.scale_batt_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* body = make_panel_body(card);
  lv_obj_t* wcol = make_panel_column(body, "WEIGHT", cap_font, big_font, &out.scale_weight);
  make_stepper_group(wcol, btn_size, symbol_font, set_font, &out.scale_target,
                     &out.target_minus, &out.target_plus);
  lv_obj_set_flex_align(wcol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);  // centered like the MICRA columns
  lv_obj_t* tcol = make_panel_column(body, "TIMER", cap_font, big_font, &out.shot_timer_label);
  lv_obj_set_flex_align(tcol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  // Shot button in the stepper's slot under the timer: shot-mode toggle
  // (Auto shot / Detect / Manual), or Reset while a finished shot is up for
  // review. Always shown — every board has shot machinery now (paddle relay
  // or detector); sized to line up with the stepper.
  out.shot_btn = ui::make_button(tcol);
  lv_obj_set_size(out.shot_btn, LV_SIZE_CONTENT, btn_size);
  lv_obj_set_style_pad_hor(out.shot_btn, ui::dp(14), 0);
  lv_obj_set_style_radius(out.shot_btn, btn_size / 2, 0);
  // Outlined like the step buttons sharing this row (card fill + ring), so the
  // stepper slot reads as one family; update_home colors the ring + text by
  // state instead of flooding the fill.
  lv_obj_set_style_bg_color(out.shot_btn, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_border_width(out.shot_btn, ui::dp(2), 0);
  lv_obj_set_style_border_color(out.shot_btn, lv_color_hex(ui::theme::scrollbar()), 0);
  lv_obj_set_style_opa(out.shot_btn, LV_OPA_40, LV_STATE_DISABLED);
  out.shot_btn_label = lv_label_create(out.shot_btn);
  lv_label_set_text(out.shot_btn_label, "Auto Shot");
  lv_obj_set_style_text_color(out.shot_btn_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.shot_btn_label, set_font, 0);
  lv_obj_center(out.shot_btn_label);

  // Connect/Disconnect + Tare side by side at the card's bottom. For a
  // sleep-capable scale (Umbra) the connect toggle doubles as sleep/wake:
  // disconnecting lets it doze off, connecting wakes it.
  lv_obj_t* acts = lv_obj_create(card);
  lv_obj_remove_style_all(acts);
  lv_obj_remove_flag(acts, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(acts, lv_pct(100));
  lv_obj_set_height(acts, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(acts, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(acts, pad, 0);

  out.scale_connect_btn = ui::make_button(acts);
  lv_obj_set_height(out.scale_connect_btn, action_h);
  lv_obj_set_width(out.scale_connect_btn, 0);
  lv_obj_set_flex_grow(out.scale_connect_btn, 1);
  lv_obj_set_style_radius(out.scale_connect_btn, ui::dp(14), 0);
  out.scale_connect_label = lv_label_create(out.scale_connect_btn);
  lv_obj_set_style_text_color(out.scale_connect_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.scale_connect_label, action_font, 0);
  lv_obj_center(out.scale_connect_label);

  build_tare_button(acts, action_font, out);
  // On the card surface the neutral buttons use the rail tone (card-on-card is
  // invisible); update_home colors the connect button by state.
  lv_obj_set_style_bg_color(out.tare_btn, lv_color_hex(ui::theme::rail()), 0);
  lv_obj_set_height(out.tare_btn, action_h);
  lv_obj_set_width(out.tare_btn, 0);
  lv_obj_set_flex_grow(out.tare_btn, 1);
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
  lv_obj_set_style_pad_row(tray, ui::dp(4), 0);

  out.clock_label = lv_label_create(tray);
  lv_obj_set_style_text_color(out.clock_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.clock_label, font, 0);

  out.battery_label = lv_label_create(tray);
  lv_obj_set_style_text_color(out.battery_label, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.battery_label, font, 0);

  out.wifi_label = lv_label_create(tray);  // WiFi glyph; update_home shows/hides + colors it
  lv_obj_set_style_text_font(out.wifi_label, font, 0);
  lv_obj_add_flag(out.wifi_label, LV_OBJ_FLAG_HIDDEN);
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

  // Space is tight on the compact bottom bar, so WiFi + battery share one row below
  // the clock. The battery shows just its level icon (no %) — the icon shape plus
  // the red (low) coloring reads well enough at this size.
  lv_obj_t* icons = lv_obj_create(tray);
  lv_obj_remove_style_all(icons);
  lv_obj_remove_flag(icons, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(icons, ui::dp(6), 0);

  out.wifi_label = lv_label_create(icons);  // WiFi glyph; update_home shows/hides + colors it
  lv_obj_set_style_text_font(out.wifi_label, font, 0);
  lv_obj_add_flag(out.wifi_label, LV_OBJ_FLAG_HIDDEN);

  out.battery_label = lv_label_create(icons);
  lv_obj_set_style_text_color(out.battery_label, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.battery_label, font, 0);
}

void build_home_tab(lv_obj_t* parent, const ScreenProfile& screen, bool scale_enabled,
                    HomeWidgets& out) {
  const bool compact = is_compact(screen);
  const bool xl = is_xl(screen);
  out.scale_enabled = scale_enabled;
  out.compact = compact;

  // HomeWidgets is reused across rebuilds, and build() frees the old widgets. Null
  // every optional pointer up front so the layout branch that runs leaves the rest
  // null (not dangling) — update_home / pump_scale_chart null-check these. Without
  // this, forgetting a scale (rebuild scale-aware -> no-scale) crashes on the stale
  // flow_chart / scale_* pointers (LoadProhibited).
  out.brew_minus = out.brew_plus = out.boiler_minus = out.boiler_plus = nullptr;
  out.brew_set = out.boiler_set = nullptr;
  out.scale_weight = out.scale_target = nullptr;
  out.tare_btn = out.tare_label = nullptr;
  out.scale_connect_btn = out.scale_connect_label = nullptr;
  out.scale_batt_label = nullptr;
  out.status_dot = out.status_label = nullptr;
  out.micra_status_dot = out.micra_status_label = nullptr;
  out.scale_status_dot = out.scale_status_label = nullptr;
  out.shot_timer_label = out.target_minus = out.target_plus = nullptr;
  // All flow-graph widgets: only the large scale-aware layout builds them, so null
  // every one here. Otherwise a scale-aware -> no-scale rebuild leaves these pointing
  // at objects lv_obj_clean already freed, and apply_flow_xaxis_labels / update_home
  // dereference them (LoadProhibited on forget-scale).
  out.flow_canvas = nullptr;  // flow_buf is freed by App before each rebuild
  out.flow_unit_btn = out.flow_unit_label = nullptr;
  out.flow_xspan_label = nullptr;
  for (lv_obj_t*& yl : out.flow_ylabels) yl = nullptr;
  for (lv_obj_t*& xl : out.flow_xlabels) xl = nullptr;

  const int pad = ui::dp(compact ? 8 : xl ? 28 : 20);
  const int gap = ui::dp(compact ? 6 : xl ? 22 : 16);
  const int card_pad = ui::dp(compact ? 8 : xl ? 24 : 16);
  const int btn_h = ui::dp(compact ? 40 : xl ? 92 : 64);
  const lv_font_t* sub_font = ui::font_dp(compact ? 14 : xl ? 28 : 20);
  const lv_font_t* btn_font = ui::font_dp(compact ? 14 : xl ? 28 : 20);

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
    const lv_font_t* c_cap = ui::font_dp(14);
    lv_obj_t* cards = lv_obj_create(parent);
    lv_obj_remove_style_all(cards);
    lv_obj_remove_flag(cards, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(cards, lv_pct(100));
    lv_obj_set_flex_grow(cards, 1);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cards, gap, 0);

    if (scale_enabled) {
      // Two half-width cards with stacked readout rows (narrow -> smaller value).
      build_compact_micra_card(cards, card_pad, c_cap, ui::font_dp(20), out);
      build_compact_scale_card(cards, card_pad, c_cap, ui::font_dp(20), out);
    } else {
      // One full-width MICRA card: BREW / STEAM columns, each with the set point in
      // small grey beneath the value (room for a bigger value at full width).
      lv_obj_t* card = make_panel_card(cards, card_pad);
      make_panel_header(card, "MICRA", c_cap, sub_font, &out.micra_status_dot,
                        &out.micra_status_label);
      lv_obj_t* body = make_panel_body(card);
      lv_obj_t* bcol = make_panel_column(body, "BREW", c_cap, ui::font_dp(28),
                                         &out.brew_value);
      lv_obj_set_flex_align(bcol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                            LV_FLEX_ALIGN_CENTER);  // center the cluster vertically
      out.brew_set = add_sub_label(bcol, c_cap);
      lv_obj_t* scol = make_panel_column(body, "STEAM", c_cap, ui::font_dp(28),
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
  const lv_font_t* panel_cap = ui::font_dp(14);
  const lv_font_t* panel_status = sub_font;                       // 20 wide / 28 xl
  // One value size for BOTH panels so the value rows + steppers line up across
  // MICRA and SCALE (weight/timer are not a bigger "hero" — they match the temps).
  const lv_font_t* panel_val = ui::font_dp(xl ? 28 : 24);
  const lv_font_t* panel_set = ui::font_dp(14);
  const int panel_btn = ui::dp(xl ? 54 : 42);
  const lv_font_t* panel_sym = ui::font_dp(xl ? 28 : 20);

  if (!scale_enabled) {
    // --- Large no-scale Home: one MICRA card as the hero, filling the whole space
    // (no second card to share it), then Power. Bigger value + steppers than the
    // scale-aware panels, and the BREW/STEAM clusters are centered vertically so the
    // card doesn't read as a small strip floating in dead space. ---
    const lv_font_t* hero_val = ui::font_dp(xl ? 48 : 40);
    const lv_font_t* hero_cap = ui::font_dp(xl ? 28 : 20);
    const lv_font_t* hero_set = ui::font_dp(xl ? 24 : 20);
    const int hero_btn = ui::dp(xl ? 64 : 54);
    const lv_font_t* hero_sym = ui::font_dp(28);

    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_flex_grow(row, 1);  // fill the vertical space (no dead spacer)
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);

    lv_obj_t* card = make_panel_card(row, card_pad);
    make_panel_header(card, "MICRA", hero_cap, panel_status, &out.micra_status_dot,
                      &out.micra_status_label);
    lv_obj_t* body = make_panel_body(card);
    // BREW / STEAM, each: caption -> big live value -> [-] set [+], centered in the
    // tall card. The set label is widened for the larger set font.
    lv_obj_t* bcol = make_panel_column(body, "BREW", hero_cap, hero_val, &out.brew_value);
    lv_obj_set_flex_align(bcol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(bcol, ui::dp(xl ? 18 : 12), 0);
    make_stepper_group(bcol, hero_btn, hero_sym, hero_set, &out.brew_set,
                       &out.brew_minus, &out.brew_plus);
    lv_obj_set_width(out.brew_set, ui::dp(xl ? 96 : 78));
    lv_obj_t* scol = make_panel_column(body, "STEAM", hero_cap, hero_val, &out.boiler_value);
    lv_obj_set_flex_align(scol, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scol, ui::dp(xl ? 18 : 12), 0);
    make_stepper_group(scol, hero_btn, hero_sym, hero_set, &out.boiler_set,
                       &out.boiler_minus, &out.boiler_plus);
    lv_obj_set_width(out.boiler_set, ui::dp(xl ? 96 : 78));

    build_power_button(parent, btn_h, btn_font, out);
    return;
  }

  // Large scale-aware screens: the upper half is two device panels (MICRA /
  // SCALE), each carrying its own status + controls — power inside MICRA,
  // Connect/Tare inside SCALE (no bottom action bar) — and the flow graph is
  // the entire lower half. Clock/battery live in the rail tray (build_rail_tray).
  const int action_h = ui::dp(xl ? 56 : 48);  // in-card action buttons (power/connect/tare)
  // Tighter padding than the generic card_pad: each card now stacks header +
  // value/stepper body + an action button inside its half of the screen.
  const int panel_pad = ui::dp(xl ? 16 : 12);
  lv_obj_t* panels = lv_obj_create(parent);
  lv_obj_remove_style_all(panels);
  lv_obj_remove_flag(panels, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(panels, lv_pct(100));
  // 7:5 with the graph card below — a shade over half for the cards so their
  // content breathes (~35 px moved out of the graph at 800x480).
  lv_obj_set_flex_grow(panels, 7);
  lv_obj_set_flex_flow(panels, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(panels, gap, 0);
  build_micra_panel(panels, panel_cap, panel_status, panel_val, panel_set, panel_btn,
                    panel_sym, panel_pad, action_h, btn_font, out);
  build_scale_panel(panels, panel_cap, panel_status, panel_val, panel_set, panel_btn,
                    panel_sym, panel_pad, action_h, btn_font, out);

  // The flow graph fills the (slightly smaller) lower half. The card is created
  // here for correct flex order, but its canvas is sized only once the layout
  // is final (populate_flow_graph below).
  lv_obj_t* graph_card = make_flow_graph_card(parent);
  lv_obj_set_flex_grow(graph_card, 5);  // the other side of the 7:5 split

  // Everything's in place: resolve the final layout, then size + paint the graph
  // to the card's real content box.
  lv_obj_update_layout(parent);
  populate_flow_graph(graph_card, out);
}

void update_home(HomeWidgets& w, const core::MachineSnapshot& state,
                 const core::BatteryState& battery, const core::WallTime& clock,
                 bool clock_24h, bool fahrenheit, const core::ScaleSnapshot& scale,
                 const core::ScaleFeatures& scale_features, bool scale_connect_enabled,
                 const core::BrewSnapshot& brew, core::NetState net) {
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

  // Scale readout (scale-aware layout only). Frozen during shot review exactly
  // like pump_scale_chart's writer — this refresh-path write was overwriting
  // the held final weight with the live reading every 500ms, which broke the
  // freeze the moment the cup came off the scale.
  if (w.scale_weight != nullptr) {
    const bool weight_frozen = brew.phase == core::ShotPhase::kReview;
    if (!weight_frozen) {
      char wb[16];
      if (scale.connected) {
        std::snprintf(wb, sizeof(wb), "%.1f g", static_cast<double>(scale.weight_g));
      } else {
        std::snprintf(wb, sizeof(wb), "-- g");
      }
      lv_label_set_text(w.scale_weight, wb);
    }
    char tb[16];
    // Every layout now labels this "TARGET" (a stepper's value on large screens, a
    // read-only row on compact), so it's just the number of grams.
    std::snprintf(tb, sizeof(tb), "%.0f g", static_cast<double>(brew.target_weight_g));
    lv_label_set_text(w.scale_target, tb);

    // Shot timer (scale panel): the ESP-side timer when it is the authoritative
    // source (core::esp_shot_timer — shared with pump_scale_chart's 10Hz
    // writer), else the scale's built-in timer.
    if (w.shot_timer_label != nullptr) {
      char sb[16];
      if (core::esp_shot_timer(brew)) {
        std::snprintf(sb, sizeof(sb), "%.1f s",
                      static_cast<double>(brew.shot_ms) / 1000.0);
      } else if (scale.connected) {
        std::snprintf(sb, sizeof(sb), "%.1f s",
                      static_cast<double>(scale.timer_ms) / 1000.0);
      } else {
        std::snprintf(sb, sizeof(sb), "-- s");
      }
      lv_label_set_text(w.shot_timer_label, sb);
    }

    // Shot button (under the timer): Reset while a finished shot is frozen for
    // review; otherwise the shot-mode toggle (accent = armed, grey = off).
    // DISABLED while a shot runs/settles — a mid-shot mode flip wouldn't cancel
    // the running automation (the paddle is the mid-shot escape), and a tap
    // during settling could land on Reset the instant it appears.
    if (w.shot_btn != nullptr && w.stop_flash_count <= 0) {
      // (While the stop-early flash runs, it owns the button's look.)
      const bool review = brew.phase == core::ShotPhase::kReview;
      const bool active = brew.phase == core::ShotPhase::kBrewing ||
                          brew.phase == core::ShotPhase::kSettling;
      // Wired arms the auto-STOP ("Auto shot"); unwired arms auto-DETECTION.
      const char* armed = brew.paddle_wired ? "Auto shot" : "Detect";
      lv_label_set_text(w.shot_btn_label,
                        review ? "Reset" : brew.shot_mode ? armed : "Manual");
      // State lives in the outline + text: accent = auto armed, warn = Reset,
      // neutral ring = manual/busy (the fill stays card(), see build).
      const uint32_t ring = review          ? ui::theme::warn()
                            : active         ? ui::theme::scrollbar()
                            : brew.shot_mode ? ui::theme::accent()
                                             : ui::theme::scrollbar();
      lv_obj_set_style_border_color(w.shot_btn, lv_color_hex(ring), 0);
      const uint32_t txt = review          ? ui::theme::warn()
                           : active         ? ui::theme::muted()
                           : brew.shot_mode ? ui::theme::accent()
                                            : ui::theme::text();
      lv_obj_set_style_text_color(w.shot_btn_label, lv_color_hex(txt), 0);
      if (active) {
        lv_obj_add_state(w.shot_btn, LV_STATE_DISABLED);
      } else {
        lv_obj_remove_state(w.shot_btn, LV_STATE_DISABLED);
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
  // Scale status, sleep-aware: a deliberately-disconnected sleep-capable scale
  // (Umbra) is dozing but still connectable — show it "Sleeping" (calm, muted),
  // never as a missing device. With the link enabled but down we're actively
  // retrying, so that reads "Connecting...".
  const char* scale_txt;
  uint32_t scale_dot;
  if (scale.connected) {
    scale_txt = "Connected";
    scale_dot = ui::theme::ok();
  } else if (scale_connect_enabled) {
    scale_txt = "Connecting...";
    scale_dot = ui::theme::warn();
  } else if (scale_features.sleep) {
    scale_txt = "Sleeping";
    scale_dot = ui::theme::muted();
  } else {
    scale_txt = "Disconnected";
    scale_dot = ui::theme::muted();
  }
  if (w.scale_status_dot != nullptr)
    lv_obj_set_style_bg_color(w.scale_status_dot, lv_color_hex(scale_dot), 0);
  if (w.scale_status_label != nullptr)
    lv_label_set_text(w.scale_status_label, scale_txt);

  // Scale battery: icon-only estimate beside the status, when reported.
  if (w.scale_batt_label != nullptr) {
    if (scale.connected && scale.battery_valid) {
      lv_obj_remove_flag(w.scale_batt_label, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(w.scale_batt_label, battery_icon(scale.battery_pct));
      lv_obj_set_style_text_color(
          w.scale_batt_label,
          lv_color_hex(scale.battery_pct < 15 ? ui::theme::alert() : ui::theme::muted()),
          0);
    } else {
      lv_obj_add_flag(w.scale_batt_label, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // In-card scale actions: the connect toggle mirrors the link switch (accent =
  // "will connect/wake"), and Tare only works with a live link.
  if (w.scale_connect_btn != nullptr) {
    lv_label_set_text(w.scale_connect_label,
                      scale_connect_enabled ? "Disconnect" : "Connect");
    lv_obj_set_style_bg_color(
        w.scale_connect_btn,
        lv_color_hex(scale_connect_enabled ? ui::theme::rail() : ui::theme::accent()),
        0);
    if (scale.connected) {
      lv_obj_remove_state(w.tare_btn, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(w.tare_btn, LV_STATE_DISABLED);
    }
  }

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
  //   USB power (voltage at/above threshold, or no cell) -> USB symbol
  //   on battery                                         -> percent + level icon
  //   nothing known                                      -> blank
  char bb[24];
  if (battery.usb) {
    lv_label_set_text(w.battery_label, LV_SYMBOL_USB);
    lv_obj_set_style_text_color(w.battery_label, lv_color_hex(ui::theme::muted()), 0);
  } else if (battery.present) {
    // Compact: icon only (space is tight); larger screens also show the percent.
    if (w.compact) {
      std::snprintf(bb, sizeof(bb), "%s", battery_icon(battery.percent));
    } else {
      std::snprintf(bb, sizeof(bb), "%d%% %s", battery.percent, battery_icon(battery.percent));
    }
    lv_label_set_text(w.battery_label, bb);
    lv_obj_set_style_text_color(
        w.battery_label,
        lv_color_hex(battery.percent < 15 ? ui::theme::alert() : ui::theme::muted()), 0);
  } else {
    lv_label_set_text(w.battery_label, "");
  }

  // WiFi glyph: hidden when WiFi is off; muted while connecting, normal when
  // connected, alert-colored on a failed attempt. (Only present in the trays.)
  if (w.wifi_label != nullptr) {
    if (net == core::NetState::Disabled) {
      lv_obj_add_flag(w.wifi_label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(w.wifi_label, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(w.wifi_label, LV_SYMBOL_WIFI);
      uint32_t color = ui::theme::muted();       // Connecting
      if (net == core::NetState::Connected) color = ui::theme::text();
      else if (net == core::NetState::Failed) color = ui::theme::alert();
      lv_obj_set_style_text_color(w.wifi_label, lv_color_hex(color), 0);
    }
  }

  // Action button: Power when connected; otherwise it doubles as a connect /
  // setup affordance so the old dead "Offline" button does something. Disabled in
  // states where a tap can't help (mid-connect, or needs setup/token first).
  // Neutral fill: card() on the classic layouts (button sits on the bg), rail()
  // when the button lives inside the MICRA card (card-on-card is invisible).
  const uint32_t pow_neutral =
      (w.scale_connect_btn != nullptr) ? ui::theme::rail() : ui::theme::card();
  switch (state.link) {
    case core::Link::Connected:
      lv_obj_remove_state(w.power_btn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(
          w.power_btn, lv_color_hex(on ? pow_neutral : ui::theme::accent()), 0);
      lv_label_set_text(w.power_label,
                        on ? LV_SYMBOL_POWER "  Standby" : LV_SYMBOL_POWER "  Turn On");
      break;
    case core::Link::Disconnected:  // configured + tokened, just not connected -> connect
      lv_obj_remove_state(w.power_btn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(w.power_btn, lv_color_hex(ui::theme::accent()), 0);
      lv_label_set_text(w.power_label, LV_SYMBOL_REFRESH "  Connect");
      break;
    case core::Link::Connecting:
      lv_obj_add_state(w.power_btn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(w.power_btn, lv_color_hex(pow_neutral), 0);
      lv_label_set_text(w.power_label, LV_SYMBOL_REFRESH "  Connecting...");
      break;
    case core::Link::NeedsToken:
      lv_obj_add_state(w.power_btn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(w.power_btn, lv_color_hex(pow_neutral), 0);
      lv_label_set_text(w.power_label, LV_SYMBOL_WARNING "  Token needed");
      break;
    case core::Link::Unconfigured:
    default:
      lv_obj_add_state(w.power_btn, LV_STATE_DISABLED);
      lv_obj_set_style_bg_color(w.power_btn, lv_color_hex(pow_neutral), 0);
      lv_label_set_text(w.power_label, LV_SYMBOL_SETTINGS "  Set up in Settings");
      break;
  }
}

namespace {

// One pulse step of the shot-button attention flash. Alternates a warn fill
// with the button's normal outlined look, then restores + self-deletes. The
// 2 Hz update_home may repaint the label color mid-flash; the next 130 ms step
// simply reasserts it, and the final step restores the review state's colors.
void shot_flash_cb(lv_timer_t* t) {
  auto* w = static_cast<ui::HomeWidgets*>(lv_timer_get_user_data(t));
  const bool dead = w->shot_btn == nullptr || w->shot_flash_count <= 0;
  if (!dead) {
    --w->shot_flash_count;
    const bool lit = (w->shot_flash_count & 1) != 0;
    lv_obj_set_style_bg_color(
        w->shot_btn, lv_color_hex(lit ? ui::theme::warn() : ui::theme::card()), 0);
    lv_obj_set_style_text_color(
        w->shot_btn_label, lv_color_hex(lit ? ui::theme::bg() : ui::theme::warn()), 0);
  }
  if (dead || w->shot_flash_count == 0) {
    if (w->shot_flash_timer == t) w->shot_flash_timer = nullptr;
    lv_timer_delete(t);
  }
}

// One pulse of the stop-early flash: the shot button (idle mid-shot) alternates
// a warn "Stop" fill and its normal card look. Silent — flash_stop_hint plays
// the one audible cue up front (a click per pulse read as nagging beeps).
// update_home skips the button's styling while the countdown runs, so the
// pulses aren't overwritten; its next pass after the countdown repaints (and
// re-disables) the normal state.
void stop_flash_cb(lv_timer_t* t) {
  auto* w = static_cast<ui::HomeWidgets*>(lv_timer_get_user_data(t));
  const bool dead = w->shot_btn == nullptr || w->stop_flash_count <= 0;
  if (!dead) {
    --w->stop_flash_count;
    const bool lit = (w->stop_flash_count & 1) != 0;
    lv_label_set_text(w->shot_btn_label, "Stop");
    lv_obj_set_style_bg_color(
        w->shot_btn, lv_color_hex(lit ? ui::theme::warn() : ui::theme::card()), 0);
    lv_obj_set_style_text_color(
        w->shot_btn_label, lv_color_hex(lit ? ui::theme::bg() : ui::theme::warn()), 0);
  }
  if (dead || w->stop_flash_count == 0) {
    // Always end on the resting fill: update_home styles the border/text but
    // NEVER the bg (it assumes card()), so a warn fill left behind here would
    // stick — and with review's warn text on it, render the label invisible.
    if (w->shot_btn != nullptr)
      lv_obj_set_style_bg_color(w->shot_btn, lv_color_hex(ui::theme::card()), 0);
    if (w->stop_flash_timer == t) w->stop_flash_timer = nullptr;
    lv_timer_delete(t);
  }
}

}  // namespace

void flash_shot_button(HomeWidgets& w) {
  if (w.shot_btn == nullptr) return;
  w.shot_flash_count = 6;  // 3 warn pulses
  if (w.shot_flash_timer == nullptr)
    w.shot_flash_timer = lv_timer_create(shot_flash_cb, 130, &w);
}

void flash_stop_hint(HomeWidgets& w) {
  if (w.shot_btn == nullptr) return;
  // Mid-shot the button sits DISABLED, whose 40% style opacity muted the warn
  // pulses badly — lift the state for the flash. Taps stay inert either way
  // (App::shot_button ignores kBrewing/kSettling); update_home re-disables on
  // its first pass after the countdown ends.
  lv_obj_remove_state(w.shot_btn, LV_STATE_DISABLED);
  ui::play_button_press();  // one audible cue; the pulses themselves are silent
  // A strobe, not a blink: ~5Hz for ~3s reads as urgent in peripheral vision
  // (the whole point — the user is watching the cup, not the screen).
  w.stop_flash_count = 30;  // 15 warn strobes over ~3s
  if (w.stop_flash_timer == nullptr)
    w.stop_flash_timer = lv_timer_create(stop_flash_cb, 100, &w);
}

void cancel_stop_flash(HomeWidgets& w) {
  w.stop_flash_count = 0;
  if (w.stop_flash_timer != nullptr) {
    lv_timer_delete(w.stop_flash_timer);
    w.stop_flash_timer = nullptr;
  }
  // The cancel can land on a lit frame — restore the resting fill here, since
  // update_home only ever styles the border/text on top of an assumed card()
  // bg (a stuck warn fill made review's warn-on-warn label unreadable).
  if (w.shot_btn != nullptr)
    lv_obj_set_style_bg_color(w.shot_btn, lv_color_hex(ui::theme::card()), 0);
}

void reset_flow_graph(HomeWidgets& w) {
  if (w.flow_canvas == nullptr || w.flow_buf == nullptr || w.flow_weights == nullptr ||
      w.flow_flows == nullptr || w.flow_w <= 0)
    return;
  for (int x = 0; x < w.flow_w; ++x) {
    w.flow_weights[x] = 0.0f;
    w.flow_flows[x] = 0.0f;
    clear_flow_column(w, x);
  }
  w.flow_prev_y = -1;
  w.flow_tick = 0;      // restart the scroll clock cleanly on the next tick
  w.flow_accum_ms = 0;
  w.flow_cursor = 0;    // scope sweep restarts at the left edge
  w.flow_ymax = flow_default_max(w.flow_mode);
  set_flow_ylabels(w);
  w.flow_blanked = true;
  lv_obj_invalidate(w.flow_canvas);
}

void reset_flow_history(HomeWidgets& w) {
  w.flow_hist_n = 0;  // rate reads 0 for ~kFlowMinSpanS while it re-warms
  w.flow_hist_head = 0;
  w.flow_hist_last_ms = 0;
}

// Sample the weight into the rate history and return the current flow rate
// (trailing-window derivative + drop-negative policy — see the kFlowHist*
// comment in the header). Shared by the live sweep and the shot plot. The
// window adapts to the OBSERVED stream rate: ~5 updates wide, floored at
// kFlowRateWindowMs (fast scales) so slow scales don't re-admit quantization
// dips.
static float update_flow_rate(HomeWidgets& w, const core::ScaleSnapshot& scale,
                              uint32_t now) {
  const float rw = scale.weight_g;
  // Stream-rate EMA, fed by seq ticks (true update rate, not value changes).
  if (scale.seq != w.flow_seq_seen) {
    if (w.flow_seq_seen != 0 && w.flow_evt_last_ms != 0) {
      const uint32_t dt = now - w.flow_evt_last_ms;
      if (dt > 0 && dt < 2000)
        w.flow_evt_interval_ms += 0.2f * (static_cast<float>(dt) - w.flow_evt_interval_ms);
    }
    w.flow_seq_seen = scale.seq;
    w.flow_evt_last_ms = now;
  }
  uint32_t window_ms = static_cast<uint32_t>(5.0f * w.flow_evt_interval_ms);
  if (window_ms < kFlowRateWindowMs) window_ms = kFlowRateWindowMs;
  if (window_ms > 3000) window_ms = 3000;  // history ring holds 3.2s

  if (w.flow_hist_n == 0 || now - w.flow_hist_last_ms >= kFlowHistSampleMs) {
    w.flow_hist_t[w.flow_hist_head] = now;
    w.flow_hist_w[w.flow_hist_head] = rw;
    w.flow_hist_head = (w.flow_hist_head + 1) % ui::HomeWidgets::kFlowHistCap;
    if (w.flow_hist_n < ui::HomeWidgets::kFlowHistCap) ++w.flow_hist_n;
    w.flow_hist_last_ms = now;
  }
  float rf = 0.0f;
  int ref = -1;
  for (int i = 0; i < w.flow_hist_n; ++i) {
    const int idx = (w.flow_hist_head - w.flow_hist_n + i +
                     2 * ui::HomeWidgets::kFlowHistCap) %
                    ui::HomeWidgets::kFlowHistCap;
    ref = idx;
    if (now - w.flow_hist_t[idx] <= window_ms) break;
  }
  if (ref >= 0) {
    const float span_s = (now - w.flow_hist_t[ref]) / 1000.0f;
    if (span_s >= kFlowMinSpanS) rf = (rw - w.flow_hist_w[ref]) / span_s;
  }
  if (w.flow_drop_negative) { if (rf < 0.0f) rf = 0.0f; }
  else if (rf < 0.0f) rf = -rf;   // show outflow as positive activity when not dropping
  return rf;
}

// ---- Shot plot (dynamic X) --------------------------------------------------
// While a shot runs, the plot maps TIME to X: window = clamp(elapsed,
// kShotMinWindowMs, kShotMaxWindowMs) across the full width, so a young shot
// gets maximum horizontal resolution and the trace compresses as it lengthens;
// past the cap the window slides. Fully redrawn per sample (~7Hz).

// Snapped time->x mapping (see kShotWindowStepMs): stable between snaps so
// painting is incremental almost always.
static uint32_t shot_window_of(uint32_t elapsed) {
  if (elapsed <= kShotMinWindowMs) return kShotMinWindowMs;
  const uint32_t win =
      ((elapsed + kShotWindowStepMs - 1) / kShotWindowStepMs) * kShotWindowStepMs;
  return win > kShotMaxWindowMs ? kShotMaxWindowMs : win;
}
static uint32_t shot_tstart_of(uint32_t elapsed, uint32_t window) {
  if (elapsed <= window) return 0;
  return ((elapsed - window + kShotSlideStepMs - 1) / kShotSlideStepMs) *
         kShotSlideStepMs;  // sliding (past the cap) also snaps
}

// Physical slot of logical shot sample i (0 = oldest stored, shot_n-1 =
// newest). THE ring-index convention — every reader goes through this so a
// layout change can't drift one code path out of sync with the others.
static int shot_sample_idx(const HomeWidgets& w, int i) {
  return (w.shot_head - w.shot_n + i + 2 * ui::HomeWidgets::kShotCap) %
         ui::HomeWidgets::kShotCap;
}

// The newest stored sample's shot-time (0 when empty).
static uint32_t shot_t_last(const HomeWidgets& w) {
  if (w.shot_n == 0) return 0;
  return w.shot_ts[shot_sample_idx(w, w.shot_n - 1)];
}

// Logical sample i (0..shot_n-1) of `vals`, smoothed by the draw-time 3-point
// kernel [k, 1-2k, k] (neighbors clamped at the ends; k = shot_smooth_k, a
// user setting — stored samples stay raw). The 2.2-interval display lag
// guarantees a painted sample's next neighbor already exists, so the
// smoothing is stable — painted columns never need revisiting.
static float shot_val(const HomeWidgets& w, const float* vals, int i) {
  const float c = vals[shot_sample_idx(w, i)];
  if (w.shot_smooth_k <= 0.0f) return c;
  const float p = vals[shot_sample_idx(w, i > 0 ? i - 1 : i)];
  const float n = vals[shot_sample_idx(w, i + 1 < w.shot_n ? i + 1 : i)];
  return c * (1.0f - 2.0f * w.shot_smooth_k) + (p + n) * w.shot_smooth_k;
}

// Paint columns x0..x1 under the given mapping, advancing the sample cursor
// (w.shot_si) and the pen (w.flow_prev_y) — callable incrementally. Columns
// past t_limit (the one-event-behind display time; see shot_plot_tick) or past
// the recorded samples stay bare.
static void shot_plot_paint_columns(HomeWidgets& w, int x0, int x1, uint32_t window,
                                    uint32_t t_start, uint32_t t_limit) {
  const float* vals = (w.flow_mode == 1) ? w.shot_weights : w.shot_flows;
  auto sample_idx = [&w](int i) { return shot_sample_idx(w, i); };
  const uint16_t bright = flow_bright_color();
  uint32_t t_last = w.shot_n > 0 ? w.shot_ts[sample_idx(w.shot_n - 1)] : 0;
  if (t_last > t_limit) t_last = t_limit;
  for (int x = x0; x <= x1; ++x) {
    const uint32_t t =
        t_start + static_cast<uint32_t>(static_cast<uint64_t>(window) *
                                        static_cast<uint32_t>(x) /
                                        static_cast<uint32_t>(w.flow_w - 1));
    clear_flow_column(w, x);
    if (w.shot_n == 0 || t > t_last) {
      w.flow_prev_y = -1.0f;  // beyond the recorded trace: bare grid
      continue;
    }
    while (w.shot_si + 1 < w.shot_n && w.shot_ts[sample_idx(w.shot_si + 1)] <= t)
      ++w.shot_si;
    const int i0 = sample_idx(w.shot_si);
    float v = shot_val(w, vals, w.shot_si);
    if (w.shot_si + 1 < w.shot_n) {  // linear interpolation to the next sample
      const int i1 = sample_idx(w.shot_si + 1);
      const uint32_t t0 = w.shot_ts[i0], t1 = w.shot_ts[i1];
      if (t1 > t0 && t >= t0) {
        const float f = static_cast<float>(t - t0) / static_cast<float>(t1 - t0);
        const float v1 = shot_val(w, vals, w.shot_si + 1);
        v = v + (v1 - v) * f;
      }
    }
    const float y = flow_row(v, w.flow_h, w.flow_ymax);
    fill_flow_below(w, x, y, bright);
    draw_flow_segment(w, x, y, bright);
    w.flow_prev_y = y;
  }
}

// The column of shot-time `t` under a mapping.
static int shot_col_of(const HomeWidgets& w, uint32_t t, uint32_t window,
                       uint32_t t_start) {
  const uint32_t t_rel = t > t_start ? t - t_start : 0;
  int x = static_cast<int>(static_cast<uint64_t>(t_rel) *
                           static_cast<uint32_t>(w.flow_w - 1) / window);
  if (x > w.flow_w - 1) x = w.flow_w - 1;
  return x;
}


// One-event-behind display time: the edge animates toward the LAST-received
// sample instead of jumping when each new one arrives — smooth growth for a
// fixed lag of ~2.2 STORED-sample intervals (imperceptible for espresso).
// ALWAYS capped strictly BEFORE the second-newest sample: painted columns are
// final (x_painted only moves forward, they're never revisited), so both their
// interpolation endpoints must have COMPLETE smoothing kernels — a column at
// time t interpolates samples si..si+1 and shot_val(si+1) needs si+2. Capping
// at the newest sample let the edge paint one-sided-smoothed values under BLE
// jitter, leaving a permanent kink per occurrence (visible at medium/strong).
// The wall-clock lag does the smooth event-behind animation; the sample cap is
// the data-availability guard (a stall just pauses the edge).
//
// The lag is sized off the STORED cadence (shot_store_interval_ms), NOT the
// raw event rate: the ring stores at most one sample per kShotSampleMs, so on
// a fast scale (Umbra: ~20Hz, delivered in bursts per BLE connection event)
// an event-rate lag clamps to less than the storage interval and the edge
// alternates between gliding and waiting on the sample cap — the
// half-smooth/half-jerky motion. Keeping the lag past the stored cadence
// makes the wall clock the binding constraint the whole shot.
static uint32_t shot_display_time(const HomeWidgets& w) {
  float interval = w.shot_store_interval_ms;
  if (interval < static_cast<float>(kShotSampleMs))
    interval = static_cast<float>(kShotSampleMs);
  uint32_t lag = static_cast<uint32_t>(2.2f * interval);
  if (lag < 150) lag = 150;
  if (lag > 1200) lag = 1200;
  uint32_t t = w.shot_elapsed_ms > lag ? w.shot_elapsed_ms - lag : 0;
  uint32_t t_safe = 0;
  if (w.shot_n >= 3) {
    const int idx2 = (w.shot_head - 2 + 2 * ui::HomeWidgets::kShotCap) %
                     ui::HomeWidgets::kShotCap;  // second-newest sample
    const uint32_t ts2 = w.shot_ts[idx2];
    t_safe = ts2 > 0 ? ts2 - 1 : 0;
  }
  if (t > t_safe) t = t_safe;
  return t;
}

// Full repaint: re-fit the Y axis over the visible samples, repaint everything,
// stamp the mapping, whole-canvas invalidate. Used on window snaps, rescales,
// and the mode toggle; the per-frame path is incremental (shot_plot_tick).
// t_limit caps how far the trace draws (the display lag); UINT32_MAX = all.
static void shot_plot_redraw_full(HomeWidgets& w, uint32_t t_limit) {
  if (w.flow_canvas == nullptr || w.flow_buf == nullptr || w.flow_w <= 1) return;
  uint32_t window, t_start;
  if (w.shot_exact_fit) {
    // Frozen review: the window IS the shot (plus lead-in/settle), so the
    // trace fills the width — no snapping, no dead-grid tail. Past the ring's
    // coverage the window pins to the cap and the start slides, unsnapped.
    window = w.shot_elapsed_ms;
    if (window < 1000) window = 1000;
    if (window > kShotMaxWindowMs) window = kShotMaxWindowMs;
    t_start = w.shot_elapsed_ms > window ? w.shot_elapsed_ms - window : 0;
  } else {
    window = shot_window_of(w.shot_elapsed_ms);
    t_start = shot_tstart_of(w.shot_elapsed_ms, window);
  }
  w.shot_map_window_ms = window;
  w.shot_map_tstart_ms = t_start;

  const float* vals = (w.flow_mode == 1) ? w.shot_weights : w.shot_flows;
  auto sample_idx = [&w](int i) { return shot_sample_idx(w, i); };
  // Y range over the visible samples (grow immediately, shrink with the same
  // half-scale hysteresis as the sweep so the axis doesn't pulse).
  float wmax = 0.0f;
  for (int i = 0; i < w.shot_n; ++i) {
    const int idx = sample_idx(i);
    if (w.shot_ts[idx] < t_start) continue;
    if (vals[idx] > wmax) wmax = vals[idx];
  }
  const float want = flow_nice_max(w.flow_mode, wmax);
  if (want > w.flow_ymax || (want < w.flow_ymax && wmax < w.flow_ymax * 0.5f)) {
    w.flow_ymax = want;
    set_flow_ylabels(w);
  }

  w.shot_si = 0;
  w.flow_prev_y = -1.0f;
  shot_plot_paint_columns(w, 0, w.flow_w - 1, window, t_start, t_limit);
  // The painted frontier: never past the newest sample (see shot_display_time).
  uint32_t t_edge = t_limit < w.shot_elapsed_ms ? t_limit : w.shot_elapsed_ms;
  const uint32_t t_last = shot_t_last(w);
  if (t_edge > t_last) t_edge = t_last;
  w.shot_x_painted = shot_col_of(w, t_edge, window, t_start);
  lv_obj_invalidate(w.flow_canvas);

  if (w.flow_xspan_label != nullptr) {
    lv_label_set_text_fmt(w.flow_xspan_label, "%u s window",
                          static_cast<unsigned>((window + 500) / 1000));
  }
}

void begin_shot_plot(HomeWidgets& w) {
  if (w.flow_canvas == nullptr) return;
  reset_flow_graph(w);
  w.flow_shot_plot = true;
  w.shot_exact_fit = false;  // live plot: back to the snapped windows
  w.unwired_ring = false;    // wired shot takes the arrays (relative stamps)
  w.shot_n = 0;
  w.shot_head = 0;
  w.shot_t0 = lv_tick_get();
  w.shot_elapsed_ms = 0;
  w.shot_last_sample_ms = 0;
  w.shot_store_interval_ms = 150.0f;  // re-learn the stored cadence per shot
  w.shot_x_painted = -1;  // first tick paints full
  w.shot_si = 0;
  w.shot_map_window_ms = 0;
  w.shot_map_tstart_ms = 0;
  w.shot_stall_since = 0;
  // X labels: the shot plot's x maps time-from-shot-start, so the scroll
  // style's age ticks would lie — show the single window caption regardless
  // of the sweep style.
  for (lv_obj_t* l : w.flow_xlabels)
    if (l != nullptr) lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
  if (w.flow_xspan_label != nullptr) {
    lv_obj_remove_flag(w.flow_xspan_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(w.flow_xspan_label, "%u s window",
                          static_cast<unsigned>(kShotMinWindowMs / 1000));
  }
}

void end_shot_plot(HomeWidgets& w) {
  if (!w.flow_shot_plot) return;
  w.flow_shot_plot = false;
  w.shot_exact_fit = false;
  reset_flow_graph(w);  // back to the live sweep on a fresh grid
  if (w.flow_xspan_label != nullptr)
    lv_label_set_text_fmt(w.flow_xspan_label, "%d s window", kFlowWindowS);
  apply_flow_xaxis_labels(w);  // restore the labels for the active sweep style
}

void shot_plot_tick(HomeWidgets& w, const core::ScaleSnapshot& scale) {
  if (!w.flow_shot_plot || w.flow_canvas == nullptr || w.flow_buf == nullptr ||
      w.flow_w <= 1)
    return;
  if (!scale.connected) return;  // controller cancels the shot; App ends the plot
  const uint32_t now = lv_tick_get();
  const float rw = scale.weight_g;
  const float rf = update_flow_rate(w, scale, now);  // keep the rate window fed every call
  w.shot_elapsed_ms = now - w.shot_t0;

  // Sample storage is event-locked: record when the scale actually publishes
  // (whatever its stream rate), capped at kShotSampleMs spacing so a fast
  // scale can't outrun the ring's 60s coverage.
  bool full = w.shot_x_painted < 0;
  if (scale.seq != w.shot_seq_seen) {
    w.shot_seq_seen = scale.seq;
    if (w.shot_n == 0 || now - w.shot_last_sample_ms >= kShotSampleMs) {
      if (w.shot_n > 0) {  // track the achieved storage cadence (sizes the edge lag)
        const uint32_t dt = now - w.shot_last_sample_ms;
        if (dt < 2000)
          w.shot_store_interval_ms +=
              0.2f * (static_cast<float>(dt) - w.shot_store_interval_ms);
      }
      w.shot_last_sample_ms = now;
      w.shot_weights[w.shot_head] = rw;
      w.shot_flows[w.shot_head] = rf;
      w.shot_ts[w.shot_head] = w.shot_elapsed_ms;
      w.shot_head = (w.shot_head + 1) % ui::HomeWidgets::kShotCap;
      if (w.shot_n < ui::HomeWidgets::kShotCap) ++w.shot_n;
      const float want = flow_nice_max(w.flow_mode, (w.flow_mode == 1) ? rw : rf);
      if (want > w.flow_ymax) {  // Y grows on the new sample
        w.flow_ymax = want;
        set_flow_ylabels(w);
        full = true;
      }
    }
  }

  // Painting runs every call (not per event): the edge chases the one-behind
  // display time, so it glides a column or two per frame instead of jumping a
  // whole event-interval span at once. Full repaint only when the mapping
  // snaps or the Y axis grew.
  const uint32_t window = shot_window_of(w.shot_elapsed_ms);
  const uint32_t t_start = shot_tstart_of(w.shot_elapsed_ms, window);
  if (window != w.shot_map_window_ms || t_start != w.shot_map_tstart_ms) full = true;
  const uint32_t t_disp = shot_display_time(w);
  if (full) {
    shot_plot_redraw_full(w, t_disp);
    return;
  }
  const int x_new = shot_col_of(w, t_disp, window, t_start);
  if (x_new > w.shot_x_painted) {
    shot_plot_paint_columns(w, w.shot_x_painted + 1, x_new, window, t_start, t_disp);
    invalidate_flow_span(w, w.shot_x_painted + 1, x_new - w.shot_x_painted);
    w.shot_x_painted = x_new;
    w.shot_stall_since = 0;
  } else if (w.shot_stall_since == 0) {
    w.shot_stall_since = now;
  } else if (now - w.shot_stall_since > 2000) {
    // Self-heal watchdog: a live shot's frontier should never sit still for 2s
    // (even pre-infusion advances columns with time). A rare un-diagnosed
    // wedge froze the chart mid-shot with the stream healthy — a full repaint
    // re-derives ALL frontier state (mapping, sample cursor, pen, frontier)
    // from scratch, turning any such wedge into a <=2s hiccup.
    w.shot_stall_since = 0;
    shot_plot_redraw_full(w, t_disp);
  }
}

void finish_shot_plot(HomeWidgets& w) {
  if (!w.flow_shot_plot) return;
  // The edge runs one event interval behind while live — flush the trailing
  // sliver so the frozen review plot shows the complete shot, window fitted
  // to the shot instead of the live snaps.
  w.shot_exact_fit = true;
  shot_plot_redraw_full(w, UINT32_MAX);
}

void unwired_ring_tick(HomeWidgets& w, const core::ScaleSnapshot& scale) {
  if (w.flow_canvas == nullptr || w.flow_buf == nullptr || w.flow_w <= 1) return;
  if (w.flow_shot_plot) return;  // review owns the arrays (rebased) — don't feed
  const uint32_t now = lv_tick_get();
  if (!w.unwired_ring) {
    // (Re)start the capture: after a review the arrays hold rebased leftovers.
    w.unwired_ring = true;
    w.shot_n = 0;
    w.shot_head = 0;
    w.shot_last_sample_ms = 0;
  }
  if (!scale.connected) return;  // gap in the ring; the detector aborts long ones
  // Event-locked storage at <=kShotSampleMs, exactly like shot_plot_tick. The
  // rate is computed only when a sample is actually stored (<=10Hz) — the live
  // sweep already fed the shared history this pass, so an every-call
  // recomputation would be pure redundancy at loop() rate.
  if (scale.seq == w.shot_seq_seen) return;
  w.shot_seq_seen = scale.seq;
  if (w.shot_n > 0 && now - w.shot_last_sample_ms < kShotSampleMs) return;
  const float rf = update_flow_rate(w, scale, now);
  w.shot_last_sample_ms = now;
  w.shot_weights[w.shot_head] = scale.weight_g;
  w.shot_flows[w.shot_head] = rf;
  w.shot_ts[w.shot_head] = now;  // ABSOLUTE — review_shot_plot rebases
  w.shot_head = (w.shot_head + 1) % ui::HomeWidgets::kShotCap;
  if (w.shot_n < ui::HomeWidgets::kShotCap) ++w.shot_n;
}

void review_shot_plot(HomeWidgets& w, uint32_t t_start, uint32_t t_end,
                      float baseline_g) {
  if (w.flow_canvas == nullptr || w.flow_buf == nullptr || w.flow_w <= 1) return;
  // A little pre-shot context so the trace shows the quiet baseline before the
  // first-drip ramp (the detector's t_start is the flow onset).
  constexpr uint32_t kLeadInMs = 3000;
  const uint32_t t0 = t_start - kLeadInMs;

  // Trim the ring to [t0, t_end] and rebase in place: the kept samples are a
  // chronological suffix, so dropping the old ones is just shrinking shot_n —
  // the head stays put and the (head - n + i) indexing still works.
  auto sample_idx = [&w](int i) { return shot_sample_idx(w, i); };
  int first = w.shot_n;  // first kept logical index
  for (int i = 0; i < w.shot_n; ++i) {
    if (static_cast<int32_t>(w.shot_ts[sample_idx(i)] - t0) >= 0) {
      first = i;
      break;
    }
  }
  int kept = 0;
  for (int i = first; i < w.shot_n; ++i) {
    const int idx = sample_idx(i);
    if (static_cast<int32_t>(w.shot_ts[idx] - t_end) > 0) break;
    w.shot_ts[idx] -= t0;                // absolute -> shot-relative
    w.shot_weights[idx] -= baseline_g;   // untared baseline -> shot grams
    ++kept;
  }
  // Head sits after the last KEPT sample now (anything newer than t_end is
  // discarded by walking the head back over it). kept == 0 (scale dark the
  // whole shot) still freezes — a bare grid — so the review UX stays coherent.
  if (kept > 0)
    w.shot_head = (sample_idx(first + kept - 1) + 1) % ui::HomeWidgets::kShotCap;
  w.shot_n = kept;
  w.unwired_ring = false;  // arrays are shot-relative until the next ring start

  // Enter the frozen shot-plot state and paint once, complete (no display lag).
  w.flow_shot_plot = true;
  w.shot_exact_fit = true;  // window = the shot, not the live snaps
  w.shot_t0 = t0;
  w.shot_elapsed_ms = t_end - t0;
  w.shot_si = 0;
  w.shot_x_painted = -1;
  w.shot_map_window_ms = 0;
  w.shot_map_tstart_ms = 0;
  w.shot_stall_since = 0;
  for (lv_obj_t* l : w.flow_xlabels)
    if (l != nullptr) lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
  if (w.flow_xspan_label != nullptr)
    lv_obj_remove_flag(w.flow_xspan_label, LV_OBJ_FLAG_HIDDEN);
  // Re-fit the axis from scratch: the live sweep may have left it elevated,
  // and redraw_full's shrink hysteresis would otherwise keep that scale.
  w.flow_ymax = flow_default_max(w.flow_mode);
  set_flow_ylabels(w);
  shot_plot_redraw_full(w, UINT32_MAX);
}

void set_shot_smoothing(HomeWidgets& w, float k) {
  w.shot_smooth_k = k;
  // Repaint an on-screen shot plot in place (frozen review, or mid-shot — the
  // live edge briefly runs unlagged, then resumes normally).
  if (w.flow_shot_plot) shot_plot_redraw_full(w, UINT32_MAX);
}

void flow_graph_tick(HomeWidgets& w, const core::ScaleSnapshot& scale) {
  if (w.flow_canvas == nullptr || w.flow_buf == nullptr || w.flow_weights == nullptr ||
      w.flow_flows == nullptr || w.flow_w <= 0)
    return;

  // Scale disconnected: blank the plot instead of scrolling a fake zero line in.
  // Clear once on the transition to an empty grid, then idle until it reconnects.
  if (!scale.connected) {
    if (!w.flow_blanked) {
      reset_flow_graph(w);
      reset_flow_history(w);  // reconnect weights won't relate to these samples
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

  // Flow = trailing-window derivative over the time-stamped weight history
  // (update_flow_rate — shared with the shot plot). A rising weight is real
  // flow; a falling one (cup removed) is negative, handled per drop-negative.
  const float rw = scale.weight_g;   // g (signed; the scale streams weight, not flow)
  const float rf = update_flow_rate(w, scale, now);
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
        const float y = flow_row(raw, w.flow_h, w.flow_ymax);
        clear_flow_column(w, rx);
        fill_flow_below(w, rx, y, bright);
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
        const float y = flow_row(raw, w.flow_h, w.flow_ymax);
        clear_flow_column(w, c);
        fill_flow_below(w, c, y, bright);
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
  if (w.flow_shot_plot) {
    // The shot plot redraws wholesale from its own samples (works frozen in
    // review too): re-fit the axis for the new quantity and repaint.
    w.flow_ymax = flow_default_max(w.flow_mode);
    set_flow_ylabels(w);
    shot_plot_redraw_full(w, UINT32_MAX);
    return;
  }
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
