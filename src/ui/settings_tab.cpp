#include "ui/settings_tab.h"

#include "ui/theme.h"

namespace ui {

void build_settings_tab(lv_obj_t* parent, const ScreenProfile& screen,
                        SettingsWidgets& out) {
  const bool compact = is_compact(screen);
  const lv_font_t* font = compact ? &lv_font_montserrat_14 : &lv_font_montserrat_20;

  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, compact ? 8 : 16, 0);
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 8, 0);

  // Saved-machine row (hidden until a machine is saved): name + Forget button.
  out.saved_row = lv_obj_create(parent);
  lv_obj_remove_style_all(out.saved_row);
  lv_obj_set_width(out.saved_row, lv_pct(100));
  lv_obj_set_height(out.saved_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(out.saved_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(out.saved_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(out.saved_row, LV_OBJ_FLAG_HIDDEN);

  out.saved_label = lv_label_create(out.saved_row);
  lv_obj_set_style_text_color(out.saved_label, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(out.saved_label, font, 0);
  lv_obj_set_flex_grow(out.saved_label, 1);

  out.forget_btn = lv_button_create(out.saved_row);
  lv_obj_set_style_bg_color(out.forget_btn, lv_color_hex(ui::theme::alert), 0);
  lv_obj_t* forget_lbl = lv_label_create(out.forget_btn);
  lv_label_set_text(forget_lbl, "Forget");
  lv_obj_set_style_text_color(forget_lbl, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(forget_lbl, font, 0);
  lv_obj_center(forget_lbl);

  out.scan_btn = lv_button_create(parent);
  lv_obj_set_width(out.scan_btn, lv_pct(100));
  lv_obj_set_style_bg_color(out.scan_btn, lv_color_hex(ui::theme::accent), 0);
  lv_obj_t* btn_lbl = lv_label_create(out.scan_btn);
  lv_label_set_text(btn_lbl, LV_SYMBOL_REFRESH "  Scan");
  lv_obj_set_style_text_color(btn_lbl, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(btn_lbl, font, 0);
  lv_obj_center(btn_lbl);

  out.status = lv_label_create(parent);
  lv_label_set_text(out.status, "Tap Scan to find your machine");
  lv_obj_set_style_text_color(out.status, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_text_font(out.status, font, 0);

  // Scrollable container the result rows get added to.
  out.list = lv_obj_create(parent);
  lv_obj_remove_style_all(out.list);
  lv_obj_set_width(out.list, lv_pct(100));
  lv_obj_set_flex_grow(out.list, 1);
  lv_obj_set_flex_flow(out.list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(out.list, 6, 0);
}

}  // namespace ui
