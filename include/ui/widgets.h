#pragma once

#include <lvgl.h>

// Shared UI widget factories. Centralizes our tweaks to LVGL's defaults so every
// screen stays consistent (and we don't repeat per-widget overrides).

namespace ui {

// Our button: an LVGL button with the default theme's (unthemed, blueish) drop
// shadow removed. Use this instead of lv_button_create everywhere.
lv_obj_t* make_button(lv_obj_t* parent);

// A round +/- stepper button: `size`x`size` circle on the card color, centered
// `symbol` glyph in `font`. Shared by Settings and Home.
lv_obj_t* make_step_button(lv_obj_t* parent, const char* symbol, int size,
                           const lv_font_t* font);

}  // namespace ui
