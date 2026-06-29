#include "ui/widgets.h"

#include "ui/theme.h"

namespace ui {

lv_obj_t* make_button(lv_obj_t* parent) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_style_shadow_width(btn, 0, 0);  // drop LVGL's default drop shadow
  return btn;
}

lv_obj_t* make_step_button(lv_obj_t* parent, const char* symbol, int size,
                           const lv_font_t* font) {
  lv_obj_t* btn = make_button(parent);  // shadow already stripped
  lv_obj_set_size(btn, size, size);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_opa(btn, LV_OPA_40, LV_STATE_DISABLED);  // greyed at a min/max limit
  lv_obj_t* l = lv_label_create(btn);
  lv_label_set_text(l, symbol);
  lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_center(l);
  return btn;
}

}  // namespace ui
