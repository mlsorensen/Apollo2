#include "ui/widgets.h"

namespace ui {

lv_obj_t* make_button(lv_obj_t* parent) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_style_shadow_width(btn, 0, 0);  // drop LVGL's default drop shadow
  return btn;
}

}  // namespace ui
