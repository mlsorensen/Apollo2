#include "ui/settings_tab.h"

#include "ui/theme.h"

namespace {

// Build the Bluetooth section: saved-machine row (name + Setup + Forget), Scan
// button, status line, and the scrollable results list.
void build_bluetooth_panel(lv_obj_t* panel, const lv_font_t* font,
                           ui::SettingsWidgets& out) {
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(panel, 8, 0);

  out.saved_row = lv_obj_create(panel);
  lv_obj_remove_style_all(out.saved_row);
  lv_obj_set_width(out.saved_row, lv_pct(100));
  lv_obj_set_height(out.saved_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(out.saved_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(out.saved_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(out.saved_row, 6, 0);
  lv_obj_add_flag(out.saved_row, LV_OBJ_FLAG_HIDDEN);

  out.saved_label = lv_label_create(out.saved_row);
  lv_obj_set_style_text_color(out.saved_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.saved_label, font, 0);
  lv_obj_set_flex_grow(out.saved_label, 1);

  out.setup_btn = lv_button_create(out.saved_row);
  lv_obj_set_style_bg_color(out.setup_btn, lv_color_hex(ui::theme::accent()), 0);
  lv_obj_t* setup_lbl = lv_label_create(out.setup_btn);
  lv_label_set_text(setup_lbl, "Setup");
  lv_obj_set_style_text_color(setup_lbl, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(setup_lbl, font, 0);
  lv_obj_center(setup_lbl);

  out.forget_btn = lv_button_create(out.saved_row);
  lv_obj_set_style_bg_color(out.forget_btn, lv_color_hex(ui::theme::alert()), 0);
  lv_obj_t* forget_lbl = lv_label_create(out.forget_btn);
  lv_label_set_text(forget_lbl, "Forget");
  lv_obj_set_style_text_color(forget_lbl, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(forget_lbl, font, 0);
  lv_obj_center(forget_lbl);

  out.scan_btn = lv_button_create(panel);
  lv_obj_set_width(out.scan_btn, lv_pct(100));
  lv_obj_set_style_bg_color(out.scan_btn, lv_color_hex(ui::theme::accent()), 0);
  lv_obj_t* btn_lbl = lv_label_create(out.scan_btn);
  lv_label_set_text(btn_lbl, LV_SYMBOL_REFRESH "  Scan");
  lv_obj_set_style_text_color(btn_lbl, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(btn_lbl, font, 0);
  lv_obj_center(btn_lbl);

  out.status = lv_label_create(panel);
  lv_label_set_text(out.status, "Tap Scan to find your machine");
  lv_obj_set_style_text_color(out.status, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.status, font, 0);

  out.list = lv_obj_create(panel);
  lv_obj_remove_style_all(out.list);
  lv_obj_set_width(out.list, lv_pct(100));
  lv_obj_set_flex_grow(out.list, 1);  // fills remaining height -> scrolls on overflow
  lv_obj_set_flex_flow(out.list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(out.list, 6, 0);
  lv_obj_set_style_pad_hor(out.list, 12, 0);  // symmetric gutters; rows clear the scrollbar

  // A visible scrollbar in the gutter (AUTO: only when the list overflows).
  lv_obj_set_scrollbar_mode(out.list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(out.list, lv_color_hex(ui::theme::scrollbar()), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(out.list, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(out.list, 5, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(out.list, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(out.list, 3, LV_PART_SCROLLBAR);  // inset from the edge
}

lv_obj_t* make_step_button(lv_obj_t* parent, const char* symbol, int size,
                           const lv_font_t* font) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_size(btn, size, size);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);  // theme's drop shadow flattens the circle
  lv_obj_t* l = lv_label_create(btn);
  lv_label_set_text(l, symbol);
  lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_center(l);
  return btn;
}

// One settings row: a left-aligned label and a right-aligned control slot. The
// caller adds the control (switch / stepper / ...) and it lands on the right.
lv_obj_t* make_setting_row(lv_obj_t* parent, const char* label,
                           const lv_font_t* font) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);  // layout only
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_t* lbl = lv_label_create(row);
  lv_label_set_text(lbl, label);
  lv_obj_set_style_text_color(lbl, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(lbl, font, 0);
  return row;
}

// A [-] value [+] control for the right of a setting row. Value is a small
// vertical stack (value + optional sub) so the boiler can show "Level 3" with
// "131 C" tight beneath it; both lines use the small text_font (value white,
// sub muted). symbol_font sizes the +/- glyphs.
void make_inline_stepper(lv_obj_t* row, const lv_font_t* text_font,
                         const lv_font_t* symbol_font, int btn_size,
                         lv_obj_t** out_minus, lv_obj_t** out_value,
                         lv_obj_t** out_plus, lv_obj_t** out_sub) {
  lv_obj_t* grp = lv_obj_create(row);
  lv_obj_remove_style_all(grp);
  lv_obj_remove_flag(grp, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(grp, 4, 0);  // buttons close to the value

  *out_minus = make_step_button(grp, LV_SYMBOL_MINUS, btn_size, symbol_font);

  lv_obj_t* stack = lv_obj_create(grp);
  lv_obj_remove_style_all(stack);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(stack, btn_size * 3 / 2, LV_SIZE_CONTENT);  // stable width
  lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  *out_value = lv_label_create(stack);
  lv_obj_set_style_text_align(*out_value, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(*out_value, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(*out_value, text_font, 0);

  if (out_sub != nullptr) {
    *out_sub = lv_label_create(stack);
    lv_obj_set_style_text_color(*out_sub, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(*out_sub, text_font, 0);
  }

  *out_plus = make_step_button(grp, LV_SYMBOL_PLUS, btn_size, symbol_font);
}

void make_settings_list(lv_obj_t* panel) {
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(panel, 16, 0);
  lv_obj_set_style_pad_hor(panel, 4, 0);
}

// Brew section: one "Temperature" row with a stepper.
void build_brew_panel(lv_obj_t* panel, const lv_font_t* text_font,
                      const lv_font_t* symbol_font, int btn_size,
                      ui::SettingsWidgets& out) {
  make_settings_list(panel);
  lv_obj_t* r = make_setting_row(panel, "Temperature", text_font);
  make_inline_stepper(r, text_font, symbol_font, btn_size, &out.brew_minus,
                      &out.brew_value, &out.brew_plus, nullptr);
}

// Steam boiler section: an "Enable" switch row + a "Temperature" stepper row.
void build_boiler_panel(lv_obj_t* panel, const lv_font_t* text_font,
                        const lv_font_t* symbol_font, int btn_size,
                        ui::SettingsWidgets& out) {
  make_settings_list(panel);

  lv_obj_t* r1 = make_setting_row(panel, "Enable", text_font);
  out.steam_switch = lv_switch_create(r1);
  lv_obj_set_size(out.steam_switch, btn_size + 8, btn_size / 2 + 6);

  lv_obj_t* r2 = make_setting_row(panel, "Temperature", text_font);
  make_inline_stepper(r2, text_font, symbol_font, btn_size, &out.boiler_minus,
                      &out.boiler_value, &out.boiler_plus, &out.boiler_sub);
}

// Device section: Brightness + a clock setter (Hour / Minute steppers).
void build_device_panel(lv_obj_t* panel, const lv_font_t* text_font,
                         const lv_font_t* symbol_font, int btn_size,
                         ui::SettingsWidgets& out) {
  make_settings_list(panel);
  // More rows than fit on the compact screen: top-align + scroll (a centered flex
  // that overflows can oscillate, esp. with a focusable control under a modal).
  lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(panel, LV_DIR_VER);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(panel, 12, 0);
  lv_obj_set_style_pad_ver(panel, 4, 0);
  lv_obj_set_style_pad_right(panel, 10, 0);  // gutter for the scrollbar
  lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ON);
  lv_obj_set_style_bg_color(panel, lv_color_hex(ui::theme::scrollbar()), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(panel, 6, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(panel, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(panel, 2, LV_PART_SCROLLBAR);

  lv_obj_t* rb = make_setting_row(panel, "Brightness", text_font);
  make_inline_stepper(rb, text_font, symbol_font, btn_size, &out.brightness_minus,
                      &out.brightness_value, &out.brightness_plus, nullptr);

  lv_obj_t* rh = make_setting_row(panel, "Hour", text_font);
  make_inline_stepper(rh, text_font, symbol_font, btn_size, &out.hour_minus,
                      &out.hour_value, &out.hour_plus, nullptr);

  lv_obj_t* rm = make_setting_row(panel, "Minute", text_font);
  make_inline_stepper(rm, text_font, symbol_font, btn_size, &out.minute_minus,
                      &out.minute_value, &out.minute_plus, nullptr);

  lv_obj_t* rc = make_setting_row(panel, "24-hour", text_font);
  out.clock_mode_switch = lv_switch_create(rc);
  lv_obj_set_size(out.clock_mode_switch, btn_size + 8, btn_size / 2 + 6);

  // Theme: a button showing the current scheme; tapping it cycles to the next.
  lv_obj_t* rt = make_setting_row(panel, "Theme", text_font);
  out.theme_btn = lv_button_create(rt);
  lv_obj_set_style_bg_color(out.theme_btn, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_shadow_width(out.theme_btn, 0, 0);
  out.theme_value = lv_label_create(out.theme_btn);
  lv_obj_set_style_text_color(out.theme_value, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.theme_value, text_font, 0);
  lv_obj_center(out.theme_value);
}

}  // namespace

namespace ui {

void build_settings_tab(lv_obj_t* parent, const ScreenProfile& screen,
                        SettingsWidgets& out) {
  const bool compact = is_compact(screen);
  const lv_font_t* font = compact ? &lv_font_montserrat_14 : &lv_font_montserrat_20;

  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, compact ? 8 : 16, 0);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 8, 0);

  // --- Segmented selector: Bluetooth / Brew / Boiler ----------------------
  lv_obj_t* seg_row = lv_obj_create(parent);
  lv_obj_remove_style_all(seg_row);
  lv_obj_set_width(seg_row, lv_pct(100));
  lv_obj_set_height(seg_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(seg_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(seg_row, 6, 0);

  const char* labels[kSectionCount] = {LV_SYMBOL_BLUETOOTH, "Brew", "Boiler", "Device"};
  for (int i = 0; i < kSectionCount; ++i) {
    out.seg[i] = lv_button_create(seg_row);
    lv_obj_set_flex_grow(out.seg[i], 1);
    lv_obj_set_style_radius(out.seg[i], 8, 0);
    lv_obj_t* l = lv_label_create(out.seg[i]);
    lv_label_set_text(l, labels[i]);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
  }

  // --- Section panels (only the active one is shown) ----------------------
  for (int i = 0; i < kSectionCount; ++i) {
    out.panel[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(out.panel[i]);
    lv_obj_set_width(out.panel[i], lv_pct(100));
    lv_obj_set_flex_grow(out.panel[i], 1);
  }
  // Small value text (matching the sub) + a slightly larger +/- glyph; compact
  // stepper buttons.
  const lv_font_t* symbol_font = compact ? &lv_font_montserrat_20 : &lv_font_montserrat_28;
  const int btn_size = compact ? 36 : 50;

  build_bluetooth_panel(out.panel[kSectionBluetooth], font, out);
  build_brew_panel(out.panel[kSectionBrew], font, symbol_font, btn_size, out);
  build_boiler_panel(out.panel[kSectionBoiler], font, symbol_font, btn_size, out);
  build_device_panel(out.panel[kSectionDevice], font, symbol_font, btn_size, out);

  settings_select_section(out, kSectionBluetooth);
}

void settings_select_section(SettingsWidgets& w, int section) {
  w.active = section;
  for (int i = 0; i < kSectionCount; ++i) {
    const bool on = (i == section);
    lv_obj_set_style_bg_color(
        w.seg[i], lv_color_hex(on ? ui::theme::accent() : ui::theme::rail()), 0);
    lv_obj_set_style_text_color(
        w.seg[i], lv_color_hex(on ? ui::theme::text() : ui::theme::muted()), 0);
    if (on) {
      lv_obj_remove_flag(w.panel[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(w.panel[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

}  // namespace ui
