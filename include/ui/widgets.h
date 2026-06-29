#pragma once

#include <lvgl.h>

// Shared UI widget factories. Centralizes our tweaks to LVGL's defaults so every
// screen stays consistent (and we don't repeat per-widget overrides).

namespace ui {

// Our button: an LVGL button with the default theme's (unthemed, blueish) drop
// shadow removed. Use this instead of lv_button_create everywhere.
lv_obj_t* make_button(lv_obj_t* parent);

}  // namespace ui
