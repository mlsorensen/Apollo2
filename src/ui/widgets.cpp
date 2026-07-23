#include "ui/widgets.h"

#include <cstring>
#include <utility>

#include "ui/screen.h"
#include "ui/theme.h"

namespace {

std::function<void()> g_press_hook;

void on_button_pressed(lv_event_t* /*e*/) {
  if (g_press_hook) g_press_hook();
}

}  // namespace

namespace ui {

void set_button_press_hook(std::function<void()> hook) {
  g_press_hook = std::move(hook);
}

void play_button_press() {
  if (g_press_hook) g_press_hook();
}

lv_obj_t* make_button(lv_obj_t* parent) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_style_shadow_width(btn, 0, 0);  // drop LVGL's default drop shadow
  lv_obj_add_event_cb(btn, on_button_pressed, LV_EVENT_PRESSED, nullptr);
  return btn;
}

const lv_font_t* font_dp(int px) {
  struct Face {
    int size;
    const lv_font_t* font;
  };
  // Must mirror the LV_FONT_MONTSERRAT_* set enabled in lv_conf.h.
  static const Face kFaces[] = {
      {14, &lv_font_montserrat_14}, {16, &lv_font_montserrat_16},
      {20, &lv_font_montserrat_20}, {24, &lv_font_montserrat_24},
      {28, &lv_font_montserrat_28}, {32, &lv_font_montserrat_32},
      {36, &lv_font_montserrat_36}, {40, &lv_font_montserrat_40},
      {48, &lv_font_montserrat_48},
  };
  const float want = px * ui::scale();
  const Face* best = &kFaces[0];
  for (const Face& f : kFaces) {
    const float d = want - f.size;
    const float bd = want - best->size;
    if ((d < 0 ? -d : d) <= (bd < 0 ? -bd : bd)) best = &f;  // <=: ties go larger
  }
  return best->font;
}

void set_text(lv_obj_t* label, const char* text) {
  if (label == nullptr || text == nullptr) return;
  const char* cur = lv_label_get_text(label);
  if (cur != nullptr && std::strcmp(cur, text) == 0) return;
  lv_label_set_text(label, text);
}

void set_bg_color(lv_obj_t* obj, uint32_t hex) {
  if (obj == nullptr) return;
  const lv_color_t c = lv_color_hex(hex);
  if (lv_color_eq(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), c)) return;
  lv_obj_set_style_bg_color(obj, c, 0);
}

void set_text_color(lv_obj_t* obj, uint32_t hex) {
  if (obj == nullptr) return;
  const lv_color_t c = lv_color_hex(hex);
  if (lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), c)) return;
  lv_obj_set_style_text_color(obj, c, 0);
}

void set_border_color(lv_obj_t* obj, uint32_t hex) {
  if (obj == nullptr) return;
  const lv_color_t c = lv_color_hex(hex);
  if (lv_color_eq(lv_obj_get_style_border_color(obj, LV_PART_MAIN), c)) return;
  lv_obj_set_style_border_color(obj, c, 0);
}

lv_obj_t* make_step_button(lv_obj_t* parent, const char* symbol, int size,
                           const lv_font_t* font) {
  lv_obj_t* btn = make_button(parent);  // shadow already stripped
  lv_obj_set_size(btn, size, size);
  lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ui::theme::card()), 0);
  // Outline so the circle reads even when the fill matches the surface behind it
  // (e.g. on a card()-colored Home temperature card).
  lv_obj_set_style_border_width(btn, ui::dp(2), 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(ui::theme::scrollbar()), 0);
  lv_obj_set_style_opa(btn, LV_OPA_40, LV_STATE_DISABLED);  // greyed at a min/max limit
  lv_obj_t* l = lv_label_create(btn);
  lv_label_set_text(l, symbol);
  lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_center(l);
  return btn;
}

}  // namespace ui
