#pragma once

#include <lvgl.h>

#include <functional>

// Shared UI widget factories. Centralizes our tweaks to LVGL's defaults so every
// screen stays consistent (and we don't repeat per-widget overrides).

namespace ui {

// Press-feedback hook: every make_button() button calls this on LV_EVENT_PRESSED
// (the moment the button activates — a scroll passing over a button never
// presses it). App points it at core::ISound so audio boards click; nullptr
// (the default) disables it. Evaluated at event time, so it can be set before
// or after buttons are built.
void set_button_press_hook(std::function<void()> hook);

// Fire the press hook manually — for buttons NOT built by make_button (e.g.
// the tabview's own tab-bar buttons) that should still give press feedback.
void play_button_press();

// Our button: an LVGL button with the default theme's (unthemed, blueish) drop
// shadow removed. Use this instead of lv_button_create everywhere.
lv_obj_t* make_button(lv_obj_t* parent);

// A round +/- stepper button: `size`x`size` circle on the card color, centered
// `symbol` glyph in `font`. Shared by Settings and Home.
lv_obj_t* make_step_button(lv_obj_t* parent, const char* symbol, int size,
                           const lv_font_t* font);

}  // namespace ui
