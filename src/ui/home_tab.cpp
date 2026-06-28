#include "ui/home_tab.h"

#include <cstdio>

#include "ui/theme.h"

namespace {

// NOTE: the built-in Montserrat fonts don't carry the degree glyph (U+00B0),
// so we spell the unit plainly for now. A subset font with the degree sign is
// a later polish item.
void format_temp(char* out, size_t n, float c) {
  std::snprintf(out, n, "%.1f C", c);
}

// One temperature panel: caption on top, big current value, set point below.
// Sized by the parent flex row (grows to share the width equally); fonts scale
// with the form factor via the passed-in font handles.
lv_obj_t* make_temp_card(lv_obj_t* parent, const char* caption, float now,
                         float target, const lv_font_t* value_font,
                         const lv_font_t* sub_font, int pad) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_height(card, lv_pct(100));
  lv_obj_set_flex_grow(card, 1);
  lv_obj_set_style_bg_color(card, lv_color_hex(ui::theme::card), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_pad_all(card, pad, 0);

  lv_obj_t* cap = lv_label_create(card);
  lv_label_set_text(cap, caption);
  lv_obj_set_style_text_color(cap, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
  lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);

  char buf[24];
  format_temp(buf, sizeof(buf), now);
  lv_obj_t* val = lv_label_create(card);
  lv_label_set_text(val, buf);
  lv_obj_set_style_text_color(val, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(val, value_font, 0);
  lv_obj_align(val, LV_ALIGN_CENTER, 0, 0);

  char tbuf[24];
  char line[40];
  format_temp(tbuf, sizeof(tbuf), target);
  std::snprintf(line, sizeof(line), "Set  %s", tbuf);
  lv_obj_t* set = lv_label_create(card);
  lv_label_set_text(set, line);
  lv_obj_set_style_text_color(set, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_text_font(set, sub_font, 0);
  lv_obj_align(set, LV_ALIGN_BOTTOM_MID, 0, 0);

  return card;
}

}  // namespace

namespace ui {

void build_home_tab(lv_obj_t* parent, const core::MachineSnapshot& state,
                    const ScreenProfile& screen) {
  const bool compact = is_compact(screen);

  // Form-factor-dependent metrics, gathered up front.
  const int pad = compact ? 8 : 20;
  const int gap = compact ? 6 : 16;
  const int row_h = compact ? 96 : 210;
  const int card_pad = compact ? 8 : 16;
  const int btn_h = compact ? 40 : 64;
  const lv_font_t* value_font = compact ? &lv_font_montserrat_28 : &lv_font_montserrat_48;
  const lv_font_t* sub_font = compact ? &lv_font_montserrat_14 : &lv_font_montserrat_20;
  const lv_font_t* btn_font = compact ? &lv_font_montserrat_14 : &lv_font_montserrat_20;

  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, pad, 0);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(parent, gap, 0);

  // --- Two temperature panels, side by side -------------------------------
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, row_h);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, gap, 0);

  make_temp_card(row, "BREW", state.brew_temp_c, state.brew_target_c, value_font,
                 sub_font, card_pad);
  make_temp_card(row, "BOILER", state.boiler_temp_c, state.boiler_target_c,
                 value_font, sub_font, card_pad);

  // --- Status line ---------------------------------------------------------
  lv_obj_t* status = lv_obj_create(parent);
  lv_obj_remove_style_all(status);
  lv_obj_remove_flag(status, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(status, 8, 0);

  const bool on = state.power == core::Power::On;
  lv_obj_t* dot = lv_obj_create(status);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 12, 12);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(
      dot, lv_color_hex(on ? ui::theme::ok : ui::theme::warn), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

  lv_obj_t* status_lbl = lv_label_create(status);
  lv_label_set_text(status_lbl, state.status ? state.status : "");
  lv_obj_set_style_text_color(status_lbl, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(status_lbl, sub_font, 0);

  // --- Spacer pushes the action button to the bottom ----------------------
  lv_obj_t* spacer = lv_obj_create(parent);
  lv_obj_remove_style_all(spacer);
  lv_obj_set_width(spacer, lv_pct(100));
  lv_obj_set_height(spacer, 0);
  lv_obj_set_flex_grow(spacer, 1);

  // --- Power toggle (placeholder: not yet wired to a command) -------------
  lv_obj_t* power = lv_button_create(parent);
  lv_obj_set_width(power, lv_pct(100));
  lv_obj_set_height(power, btn_h);
  lv_obj_set_style_radius(power, 14, 0);
  lv_obj_set_style_bg_color(
      power, lv_color_hex(on ? ui::theme::card : ui::theme::accent), 0);

  lv_obj_t* power_lbl = lv_label_create(power);
  lv_label_set_text(power_lbl,
                    on ? LV_SYMBOL_POWER "  STANDBY" : LV_SYMBOL_POWER "  TURN ON");
  lv_obj_set_style_text_color(power_lbl, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(power_lbl, btn_font, 0);
  lv_obj_center(power_lbl);
}

}  // namespace ui
