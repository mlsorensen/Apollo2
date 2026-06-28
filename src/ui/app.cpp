#include "ui/app.h"

#include <lvgl.h>

#include "ui/home_tab.h"
#include "ui/theme.h"

namespace {

// Style one tab-bar button: transparent normally, accent-filled when its tab is
// active. `font` scales the glyph to the form factor.
void style_tab_button(lv_obj_t* btn, const lv_font_t* font) {
  lv_obj_set_style_text_font(btn, font, 0);
  lv_obj_set_style_text_color(btn, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(btn, 10, 0);
  lv_obj_set_style_border_width(btn, 0, 0);

  lv_obj_set_style_text_color(btn, lv_color_hex(ui::theme::text),
                              LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ui::theme::accent),
                            LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_CHECKED);
}

// A simple centered-label placeholder for tabs we haven't built yet.
void build_placeholder(lv_obj_t* parent, const char* title,
                       const lv_font_t* font) {
  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* lbl = lv_label_create(parent);
  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_color(lbl, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_text_font(lbl, font, 0);
  lv_obj_center(lbl);
}

}  // namespace

namespace ui {

void create_app(const core::MachineSnapshot& state, const ScreenProfile& screen) {
  const bool compact = is_compact(screen);

  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(ui::theme::bg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // Compact panels get a bottom tab bar (more usable width); wide panels get a
  // left rail. Sizes/fonts scale with the form factor.
  const lv_font_t* tab_font = compact ? &lv_font_montserrat_20 : &lv_font_montserrat_28;

  lv_obj_t* tv = lv_tabview_create(scr);
  lv_tabview_set_tab_bar_position(tv, compact ? LV_DIR_BOTTOM : LV_DIR_LEFT);
  lv_tabview_set_tab_bar_size(tv, compact ? 44 : 88);
  lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tv, 0, 0);

  lv_obj_t* content = lv_tabview_get_content(tv);
  lv_obj_set_style_bg_color(content, lv_color_hex(ui::theme::bg), 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);

  lv_obj_t* rail = lv_tabview_get_tab_bar(tv);
  lv_obj_set_style_bg_color(rail, lv_color_hex(ui::theme::rail), 0);
  lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(rail, compact ? 4 : 8, 0);
  lv_obj_set_style_pad_row(rail, compact ? 4 : 8, 0);
  lv_obj_set_style_pad_column(rail, compact ? 4 : 8, 0);

  lv_obj_t* home = lv_tabview_add_tab(tv, LV_SYMBOL_HOME);
  lv_obj_t* settings = lv_tabview_add_tab(tv, LV_SYMBOL_SETTINGS);
  lv_obj_t* stats = lv_tabview_add_tab(tv, LV_SYMBOL_LIST);

  for (uint32_t i = 0; i < lv_tabview_get_tab_count(tv); ++i) {
    style_tab_button(lv_tabview_get_tab_button(tv, i), tab_font);
  }

  build_home_tab(home, state, screen);
  build_placeholder(settings, "Settings", tab_font);
  build_placeholder(stats, "Stats", tab_font);
}

}  // namespace ui
