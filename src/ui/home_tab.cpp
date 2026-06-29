#include "ui/home_tab.h"

#include <cstdio>

#include "ui/theme.h"

namespace {

// NOTE: the built-in Montserrat fonts don't carry the degree glyph (U+00B0),
// so we spell the unit plainly for now.
void format_now(char* out, size_t n, float c) { std::snprintf(out, n, "%.1f C", c); }
void format_set(char* out, size_t n, float c) { std::snprintf(out, n, "Set  %.1f C", c); }

// A temperature panel: caption (static) on top, big current value, set point
// below. The value/set labels are returned for live updates.
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

// Loops the battery fill while charging (no percent — SoC is unknowable then).
void battery_anim_cb(lv_timer_t* t) {
  auto* w = static_cast<ui::HomeWidgets*>(lv_timer_get_user_data(t));
  if (!w->charging) return;
  w->charge_frame = (w->charge_frame + 1) % 5;
  char buf[24];
  std::snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %s",
                battery_fill_icon(w->charge_frame));
  lv_label_set_text(w->battery_label, buf);
}

}  // namespace

namespace ui {

void build_home_tab(lv_obj_t* parent, const ScreenProfile& screen, HomeWidgets& out) {
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

  // --- Top bar: status (left) + clock & battery (right) -------------------
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
  lv_obj_set_style_text_color(out.status_label, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(out.status_label, sub_font, 0);

  // Right group: battery then clock, so the clock sits farthest right like a
  // macOS menu bar (battery just to its left).
  lv_obj_t* right_group = lv_obj_create(bar);
  lv_obj_remove_style_all(right_group);
  lv_obj_set_size(right_group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(right_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(right_group, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(right_group, 10, 0);

  out.battery_label = lv_label_create(right_group);
  lv_obj_set_style_text_color(out.battery_label, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_text_font(out.battery_label, sub_font, 0);

  out.clock_label = lv_label_create(right_group);
  lv_obj_set_style_text_color(out.clock_label, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(out.clock_label, sub_font, 0);
  out.batt_timer = lv_timer_create(battery_anim_cb, 350, &out);  // drives charge anim

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
}

void update_home(HomeWidgets& w, const core::MachineSnapshot& state,
                 const core::BatteryState& battery, const core::WallTime& clock,
                 bool clock_24h) {
  const bool connected = state.link == core::Link::Connected;
  const bool on = state.power == core::Power::On;

  // Temperatures: real values when connected, placeholders otherwise.
  char buf[24];
  if (connected) {
    format_now(buf, sizeof(buf), state.brew_temp_c);
    lv_label_set_text(w.brew_value, buf);
    format_set(buf, sizeof(buf), state.brew_target_c);
    lv_label_set_text(w.brew_set, buf);
    if (state.steam_enabled) {
      format_now(buf, sizeof(buf), state.boiler_temp_c);
      lv_label_set_text(w.boiler_value, buf);
      format_set(buf, sizeof(buf), state.boiler_target_c);
      lv_label_set_text(w.boiler_set, buf);
    } else {
      lv_label_set_text(w.boiler_value, "Off");
      lv_label_set_text(w.boiler_set, "");
    }
  } else {
    lv_label_set_text(w.brew_value, "--");
    lv_label_set_text(w.brew_set, "Set  --");
    lv_label_set_text(w.boiler_value, "--");
    lv_label_set_text(w.boiler_set, "Set  --");
  }

  // Status (top-left): text + dot color from link + power.
  const char* status = "Disconnected";
  uint32_t dot = ui::theme::muted;
  switch (state.link) {
    case core::Link::Unconfigured: status = "Set up in Settings"; dot = ui::theme::muted; break;
    case core::Link::NeedsToken:   status = "Token needed"; dot = ui::theme::warn; break;
    case core::Link::Disconnected: status = "Disconnected"; dot = ui::theme::alert; break;
    case core::Link::Connecting:   status = "Connecting..."; dot = ui::theme::warn; break;
    case core::Link::Connected:
      status = on ? "Ready" : "Standby";
      dot = on ? ui::theme::ok : ui::theme::warn;
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

  // Battery (top-right). Charging: bolt + a looping fill animation, NO percent
  // (terminal voltage is elevated under charge, so SoC is unknowable). Idle:
  // level icon + percent.
  w.charging = battery.present && battery.charging;
  if (!battery.present) {
    lv_label_set_text(w.battery_label, "");
  } else if (battery.charging) {
    lv_obj_set_style_text_color(w.battery_label, lv_color_hex(ui::theme::ok), 0);
    char bb[24];
    std::snprintf(bb, sizeof(bb), LV_SYMBOL_CHARGE " %s",
                  battery_fill_icon(w.charge_frame));  // timer advances frames
    lv_label_set_text(w.battery_label, bb);
  } else {
    char bb[24];
    std::snprintf(bb, sizeof(bb), "%d%% %s", battery.percent,
                  battery_icon(battery.percent));
    lv_label_set_text(w.battery_label, bb);
    lv_obj_set_style_text_color(
        w.battery_label,
        lv_color_hex(battery.percent < 15 ? ui::theme::alert : ui::theme::muted), 0);
  }

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
