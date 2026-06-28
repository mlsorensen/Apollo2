#include "ui/home_tab.h"

#include <cstdio>

#include "ui/theme.h"

namespace {

// NOTE: the built-in Montserrat fonts don't carry the degree glyph (U+00B0),
// so we spell the unit plainly for now. A subset font with the degree sign is
// a later polish item.
void format_now(char* out, size_t n, float c) { std::snprintf(out, n, "%.1f C", c); }
void format_set(char* out, size_t n, float c) { std::snprintf(out, n, "Set  %.1f C", c); }

// One temperature panel: caption (static) on top, big current value, set point
// below. The value and set labels are returned so update_home can refresh them.
void make_temp_card(lv_obj_t* parent, const char* caption,
                    const lv_font_t* value_font, const lv_font_t* sub_font,
                    int pad, lv_obj_t** out_value, lv_obj_t** out_set) {
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

  lv_obj_t* val = lv_label_create(card);
  lv_obj_set_style_text_color(val, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(val, value_font, 0);
  lv_obj_align(val, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* set = lv_label_create(card);
  lv_obj_set_style_text_color(set, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_text_font(set, sub_font, 0);
  lv_obj_align(set, LV_ALIGN_BOTTOM_MID, 0, 0);

  *out_value = val;
  *out_set = set;
}

}  // namespace

namespace ui {

void build_home_tab(lv_obj_t* parent, const core::MachineSnapshot& state,
                    const ScreenProfile& screen, HomeWidgets& out) {
  const bool compact = is_compact(screen);

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

  make_temp_card(row, "BREW", value_font, sub_font, card_pad, &out.brew_value,
                 &out.brew_set);
  make_temp_card(row, "BOILER", value_font, sub_font, card_pad, &out.boiler_value,
                 &out.boiler_set);

  // --- Status line ---------------------------------------------------------
  lv_obj_t* status = lv_obj_create(parent);
  lv_obj_remove_style_all(status);
  lv_obj_remove_flag(status, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(status, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(status, 8, 0);

  out.status_dot = lv_obj_create(status);
  lv_obj_remove_style_all(out.status_dot);
  lv_obj_set_size(out.status_dot, 12, 12);
  lv_obj_set_style_radius(out.status_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(out.status_dot, LV_OPA_COVER, 0);

  out.status_label = lv_label_create(status);
  lv_obj_set_style_text_color(out.status_label, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(out.status_label, sub_font, 0);

  // --- Spacer pushes the action button to the bottom ----------------------
  lv_obj_t* spacer = lv_obj_create(parent);
  lv_obj_remove_style_all(spacer);
  lv_obj_set_width(spacer, lv_pct(100));
  lv_obj_set_height(spacer, 0);
  lv_obj_set_flex_grow(spacer, 1);

  // --- Power toggle --------------------------------------------------------
  out.power_btn = lv_button_create(parent);
  lv_obj_set_width(out.power_btn, lv_pct(100));
  lv_obj_set_height(out.power_btn, btn_h);
  lv_obj_set_style_radius(out.power_btn, 14, 0);

  out.power_label = lv_label_create(out.power_btn);
  lv_obj_set_style_text_color(out.power_label, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(out.power_label, btn_font, 0);
  lv_obj_center(out.power_label);

  update_home(out, state);  // set initial values/colors
}

void update_home(const HomeWidgets& w, const core::MachineSnapshot& state) {
  const bool connected = state.link == core::Link::Connected;
  const bool on = state.power == core::Power::On;

  // Temperatures: real values when connected, placeholders otherwise.
  char buf[24];
  if (connected) {
    format_now(buf, sizeof(buf), state.brew_temp_c);
    lv_label_set_text(w.brew_value, buf);
    format_set(buf, sizeof(buf), state.brew_target_c);
    lv_label_set_text(w.brew_set, buf);
    format_now(buf, sizeof(buf), state.boiler_temp_c);
    lv_label_set_text(w.boiler_value, buf);
    format_set(buf, sizeof(buf), state.boiler_target_c);
    lv_label_set_text(w.boiler_set, buf);
  } else {
    lv_label_set_text(w.brew_value, "--");
    lv_label_set_text(w.brew_set, "Set  --");
    lv_label_set_text(w.boiler_value, "--");
    lv_label_set_text(w.boiler_set, "Set  --");
  }

  // Status line: text + dot color derived from link + power.
  const char* status = "Disconnected";
  uint32_t dot = ui::theme::muted;
  switch (state.link) {
    case core::Link::Unconfigured: status = "Set up in Settings"; dot = ui::theme::muted; break;
    case core::Link::NeedsToken:   status = "Token needed - Settings"; dot = ui::theme::warn; break;
    case core::Link::Disconnected: status = "Disconnected"; dot = ui::theme::alert; break;
    case core::Link::Connecting:   status = "Connecting..."; dot = ui::theme::warn; break;
    case core::Link::Connected:
      status = on ? "Ready" : "Standby";
      dot = on ? ui::theme::ok : ui::theme::warn;
      break;
  }
  lv_label_set_text(w.status_label, status);
  lv_obj_set_style_bg_color(w.status_dot, lv_color_hex(dot), 0);

  // Power button: only actionable when connected.
  if (connected) {
    lv_obj_remove_state(w.power_btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(
        w.power_btn, lv_color_hex(on ? ui::theme::card : ui::theme::accent), 0);
    lv_label_set_text(w.power_label,
                      on ? LV_SYMBOL_POWER "  STANDBY" : LV_SYMBOL_POWER "  TURN ON");
  } else {
    lv_obj_add_state(w.power_btn, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(w.power_btn, lv_color_hex(ui::theme::card), 0);
    lv_label_set_text(w.power_label, LV_SYMBOL_POWER "  OFFLINE");
  }
}

}  // namespace ui
