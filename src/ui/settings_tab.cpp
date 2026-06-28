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
  lv_obj_set_style_text_color(out.saved_label, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(out.saved_label, font, 0);
  lv_obj_set_flex_grow(out.saved_label, 1);

  out.setup_btn = lv_button_create(out.saved_row);
  lv_obj_set_style_bg_color(out.setup_btn, lv_color_hex(ui::theme::accent), 0);
  lv_obj_t* setup_lbl = lv_label_create(out.setup_btn);
  lv_label_set_text(setup_lbl, "Setup");
  lv_obj_set_style_text_color(setup_lbl, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(setup_lbl, font, 0);
  lv_obj_center(setup_lbl);

  out.forget_btn = lv_button_create(out.saved_row);
  lv_obj_set_style_bg_color(out.forget_btn, lv_color_hex(ui::theme::alert), 0);
  lv_obj_t* forget_lbl = lv_label_create(out.forget_btn);
  lv_label_set_text(forget_lbl, "Forget");
  lv_obj_set_style_text_color(forget_lbl, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(forget_lbl, font, 0);
  lv_obj_center(forget_lbl);

  out.scan_btn = lv_button_create(panel);
  lv_obj_set_width(out.scan_btn, lv_pct(100));
  lv_obj_set_style_bg_color(out.scan_btn, lv_color_hex(ui::theme::accent), 0);
  lv_obj_t* btn_lbl = lv_label_create(out.scan_btn);
  lv_label_set_text(btn_lbl, LV_SYMBOL_REFRESH "  Scan");
  lv_obj_set_style_text_color(btn_lbl, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(btn_lbl, font, 0);
  lv_obj_center(btn_lbl);

  out.status = lv_label_create(panel);
  lv_label_set_text(out.status, "Tap Scan to find your machine");
  lv_obj_set_style_text_color(out.status, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_text_font(out.status, font, 0);

  out.list = lv_obj_create(panel);
  lv_obj_remove_style_all(out.list);
  lv_obj_set_width(out.list, lv_pct(100));
  lv_obj_set_flex_grow(out.list, 1);  // fills remaining height -> scrolls on overflow
  lv_obj_set_flex_flow(out.list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(out.list, 6, 0);

  // A visible scrollbar so it's obvious the list scrolls when there are more
  // machines than fit (remove_style_all stripped the default one).
  lv_obj_set_scrollbar_mode(out.list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(out.list, lv_color_hex(ui::theme::accent), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(out.list, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(out.list, 6, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(out.list, 3, LV_PART_SCROLLBAR);
}

// A placeholder section until the capabilities-driven temperature controls land.
void build_placeholder_panel(lv_obj_t* panel, const char* text,
                             const lv_font_t* font) {
  lv_obj_t* lbl = lv_label_create(panel);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_text_font(lbl, font, 0);
  lv_obj_center(lbl);
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

  const char* labels[kSectionCount] = {LV_SYMBOL_BLUETOOTH, "Brew", "Boiler"};
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
  build_bluetooth_panel(out.panel[kSectionBluetooth], font, out);
  build_placeholder_panel(out.panel[kSectionBrew], "Brew temperature\n(coming soon)", font);
  build_placeholder_panel(out.panel[kSectionBoiler], "Boiler temperature\n(coming soon)", font);

  settings_select_section(out, kSectionBluetooth);
}

void settings_select_section(SettingsWidgets& w, int section) {
  w.active = section;
  for (int i = 0; i < kSectionCount; ++i) {
    const bool on = (i == section);
    lv_obj_set_style_bg_color(
        w.seg[i], lv_color_hex(on ? ui::theme::accent : ui::theme::rail), 0);
    lv_obj_set_style_text_color(
        w.seg[i], lv_color_hex(on ? ui::theme::text : ui::theme::muted), 0);
    if (on) {
      lv_obj_remove_flag(w.panel[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(w.panel[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

}  // namespace ui
