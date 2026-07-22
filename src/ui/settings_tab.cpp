#include "ui/settings_tab.h"

#include <string>

#include "ui/theme.h"
#include "ui/timezones.h"
#include "ui/widgets.h"

namespace {

// A muted group heading inside a menu page ("Connection", "Brew", ...).
void section_label(lv_obj_t* parent, const char* text, const lv_font_t* font) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(l, font, 0);
}

// Lay out a menu page as a padded vertical column (the page itself scrolls).
void page_column(lv_obj_t* page, bool compact) {
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(page, ui::dp(compact ? 8 : 12), 0);
  lv_obj_set_style_pad_row(page, ui::dp(compact ? 8 : 12), 0);
  // OPAQUE page background (same color as the screen, so visually identical):
  // a transparent scrollable forces LVGL to re-render every layer beneath it
  // on each scroll frame — the bulk of the settings-scroll CPU cost.
  lv_obj_set_style_bg_color(page, lv_color_hex(ui::theme::bg()), 0);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
}

// Connection panel (saved row + Scan + status + results list), shared by Micra
// and Scale. `out_setup` is the Micra-only token "Setup" button — pass nullptr to
// omit it (scales need no token). The list sizes to content so the page scrolls.
void build_connection_panel(lv_obj_t* panel, const lv_font_t* font, int btn_h,
                            lv_obj_t** out_saved_row, lv_obj_t** out_saved_label,
                            lv_obj_t** out_setup, lv_obj_t** out_connect,
                            lv_obj_t** out_connect_label, lv_obj_t** out_forget,
                            lv_obj_t** out_scan, lv_obj_t** out_status,
                            lv_obj_t** out_list, const char* scan_hint) {
  // Saved name + action buttons inline. The name (no "Saved:" prefix) may wrap to
  // two lines on compact — that's fine.
  *out_saved_row = lv_obj_create(panel);
  lv_obj_remove_style_all(*out_saved_row);
  lv_obj_set_width(*out_saved_row, lv_pct(100));
  lv_obj_set_height(*out_saved_row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(*out_saved_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(*out_saved_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(*out_saved_row, ui::dp(6), 0);
  lv_obj_add_flag(*out_saved_row, LV_OBJ_FLAG_HIDDEN);

  *out_saved_label = lv_label_create(*out_saved_row);
  lv_obj_set_style_text_color(*out_saved_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(*out_saved_label, font, 0);
  lv_obj_set_flex_grow(*out_saved_label, 1);

  if (out_setup != nullptr) {
    *out_setup = ui::make_button(*out_saved_row);
    lv_obj_set_height(*out_setup, btn_h);
    lv_obj_set_style_bg_color(*out_setup, lv_color_hex(ui::theme::accent()), 0);
    lv_obj_t* setup_lbl = lv_label_create(*out_setup);
    lv_label_set_text(setup_lbl, "Setup");
    lv_obj_set_style_text_color(setup_lbl, lv_color_hex(ui::theme::text()), 0);
    lv_obj_set_style_text_font(setup_lbl, font, 0);
    lv_obj_center(setup_lbl);
  }

  *out_connect = ui::make_button(*out_saved_row);
  lv_obj_set_height(*out_connect, btn_h);
  lv_obj_set_style_bg_color(*out_connect, lv_color_hex(ui::theme::accent()), 0);
  *out_connect_label = lv_label_create(*out_connect);
  lv_label_set_text(*out_connect_label, "Disconnect");
  lv_obj_set_style_text_color(*out_connect_label, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(*out_connect_label, font, 0);
  lv_obj_center(*out_connect_label);
  lv_obj_add_flag(*out_connect, LV_OBJ_FLAG_HIDDEN);

  *out_forget = ui::make_button(*out_saved_row);
  lv_obj_set_height(*out_forget, btn_h);
  lv_obj_set_style_bg_color(*out_forget, lv_color_hex(ui::theme::alert()), 0);
  lv_obj_t* forget_lbl = lv_label_create(*out_forget);
  lv_label_set_text(forget_lbl, "Forget");
  lv_obj_set_style_text_color(forget_lbl, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(forget_lbl, font, 0);
  lv_obj_center(forget_lbl);

  *out_scan = ui::make_button(panel);
  lv_obj_set_width(*out_scan, lv_pct(100));
  lv_obj_set_height(*out_scan, btn_h);
  lv_obj_set_style_bg_color(*out_scan, lv_color_hex(ui::theme::accent()), 0);
  lv_obj_t* btn_lbl = lv_label_create(*out_scan);
  lv_label_set_text(btn_lbl, LV_SYMBOL_REFRESH "  Scan");
  lv_obj_set_style_text_color(btn_lbl, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(btn_lbl, font, 0);
  lv_obj_center(btn_lbl);

  *out_status = lv_label_create(panel);
  lv_label_set_text(*out_status, scan_hint);
  lv_obj_set_style_text_color(*out_status, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(*out_status, font, 0);

  *out_list = lv_obj_create(panel);
  lv_obj_remove_style_all(*out_list);
  lv_obj_set_width(*out_list, lv_pct(100));
  lv_obj_set_height(*out_list, LV_SIZE_CONTENT);  // page scrolls, not the list
  lv_obj_set_flex_flow(*out_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(*out_list, ui::dp(6), 0);
}

// One settings row: a left-aligned label and a right-aligned control slot.
lv_obj_t* make_setting_row(lv_obj_t* parent, const char* label,
                           const lv_font_t* font) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
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
// "131 C" tight beneath it. symbol_font sizes the +/- glyphs.
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
  lv_obj_set_style_pad_column(grp, ui::dp(4), 0);

  *out_minus = ui::make_step_button(grp, LV_SYMBOL_MINUS, btn_size, symbol_font);

  lv_obj_t* stack = lv_obj_create(grp);
  lv_obj_remove_style_all(stack);
  lv_obj_remove_flag(stack, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(stack, btn_size * 3 / 2, LV_SIZE_CONTENT);
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

  *out_plus = ui::make_step_button(grp, LV_SYMBOL_PLUS, btn_size, symbol_font);
}

// Device rows: Brightness + clock (Hour/Minute) + 24-hour + Fahrenheit + Theme.
void build_device_rows(lv_obj_t* page, const lv_font_t* text_font,
                       const lv_font_t* symbol_font, int btn_size,
                       bool with_brightness, bool with_sound,
                       ui::SettingsWidgets& out) {
  // Brightness only where the backlight can dim; else the row is omitted and the
  // pointers stay null (App guards on them).
  if (with_brightness) {
    lv_obj_t* rb = make_setting_row(page, "Brightness", text_font);
    make_inline_stepper(rb, text_font, symbol_font, btn_size, &out.brightness_minus,
                        &out.brightness_value, &out.brightness_plus, nullptr);
  } else {
    out.brightness_minus = out.brightness_plus = out.brightness_value = nullptr;
  }

  // Screen dim: tap cycles Off / 15 min / 30 min. On every board — on/off-only
  // backlights just switch off instead of dimming.
  lv_obj_t* rd = make_setting_row(page, "Screen dim", text_font);
  out.dim_btn = ui::make_button(rd);
  lv_obj_set_height(out.dim_btn, btn_size);
  lv_obj_set_style_pad_hor(out.dim_btn, ui::dp(14), 0);
  lv_obj_set_style_bg_color(out.dim_btn, lv_color_hex(ui::theme::card()), 0);
  out.dim_value = lv_label_create(out.dim_btn);
  lv_obj_set_style_text_color(out.dim_value, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.dim_value, text_font, 0);
  lv_obj_center(out.dim_value);

  lv_obj_t* rh = make_setting_row(page, "Hour", text_font);
  make_inline_stepper(rh, text_font, symbol_font, btn_size, &out.hour_minus,
                      &out.hour_value, &out.hour_plus, nullptr);

  lv_obj_t* rm = make_setting_row(page, "Minute", text_font);
  make_inline_stepper(rm, text_font, symbol_font, btn_size, &out.minute_minus,
                      &out.minute_value, &out.minute_plus, nullptr);

  lv_obj_t* rc = make_setting_row(page, "24-hour", text_font);
  out.clock_mode_switch = lv_switch_create(rc);
  lv_obj_set_size(out.clock_mode_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));

  lv_obj_t* ru = make_setting_row(page, "Fahrenheit", text_font);
  out.units_switch = lv_switch_create(ru);
  lv_obj_set_size(out.units_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));

  lv_obj_t* rt = make_setting_row(page, "Theme", text_font);
  out.theme_btn = ui::make_button(rt);
  lv_obj_set_style_bg_color(out.theme_btn, lv_color_hex(ui::theme::card()), 0);
  out.theme_value = lv_label_create(out.theme_btn);
  lv_obj_set_style_text_color(out.theme_value, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(out.theme_value, text_font, 0);
  lv_obj_center(out.theme_value);

  lv_obj_t* rp = make_setting_row(page, "Performance overlay", text_font);
  out.perf_overlay_switch = lv_switch_create(rp);
  lv_obj_set_size(out.perf_overlay_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));

  // Button-press click, only on boards with a speaker (core::ISound).
  if (with_sound) {
    lv_obj_t* rs = make_setting_row(page, "Button sounds", text_font);
    out.click_sound_switch = lv_switch_create(rs);
    lv_obj_set_size(out.click_sound_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));
  } else {
    out.click_sound_switch = nullptr;
  }

  // --- WiFi: enable + status + setup/forget + timezone --------------------
  section_label(page, "WiFi", text_font);

  lv_obj_t* rw = make_setting_row(page, "Enable", text_font);
  out.wifi_switch = lv_switch_create(rw);
  lv_obj_set_size(out.wifi_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));

  lv_obj_t* rst = make_setting_row(page, "Status", text_font);
  out.wifi_status = lv_label_create(rst);
  lv_obj_set_style_text_color(out.wifi_status, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(out.wifi_status, text_font, 0);
  lv_label_set_text(out.wifi_status, "Off");

  // Set up / Forget buttons share a row (mirrors the connection panel's actions).
  lv_obj_t* rb = lv_obj_create(page);
  lv_obj_remove_style_all(rb);
  lv_obj_remove_flag(rb, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(rb, lv_pct(100));
  lv_obj_set_height(rb, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(rb, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(rb, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(rb, ui::dp(6), 0);

  out.wifi_setup_btn = ui::make_button(rb);
  lv_obj_set_flex_grow(out.wifi_setup_btn, 1);
  lv_obj_set_height(out.wifi_setup_btn, btn_size);
  lv_obj_set_style_bg_color(out.wifi_setup_btn, lv_color_hex(ui::theme::accent()), 0);
  lv_obj_t* su = lv_label_create(out.wifi_setup_btn);
  lv_label_set_text(su, "Set up WiFi");
  lv_obj_set_style_text_color(su, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(su, text_font, 0);
  lv_obj_center(su);

  out.wifi_forget_btn = ui::make_button(rb);
  lv_obj_set_height(out.wifi_forget_btn, btn_size);
  lv_obj_set_style_bg_color(out.wifi_forget_btn, lv_color_hex(ui::theme::alert()), 0);
  lv_obj_t* fg = lv_label_create(out.wifi_forget_btn);
  lv_label_set_text(fg, "Forget");
  lv_obj_set_style_text_color(fg, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(fg, text_font, 0);
  lv_obj_center(fg);

  lv_obj_t* rtz = make_setting_row(page, "Timezone", text_font);
  out.tz_dropdown = lv_dropdown_create(rtz);
  std::string opts;
  for (int i = 0; i < ui::kTimezoneCount; ++i) {
    if (i) opts += '\n';
    opts += ui::kTimezones[i].label;
  }
  lv_dropdown_set_options(out.tz_dropdown, opts.c_str());
  lv_obj_set_style_text_font(out.tz_dropdown, text_font, 0);

  lv_obj_t* rnt = make_setting_row(page, "Auto time (NTP)", text_font);
  out.ntp_switch = lv_switch_create(rnt);
  lv_obj_set_size(out.ntp_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));
}

// A root-page navigation entry: a card row "<label>  ›" that drills into `target`.
void root_entry(lv_obj_t* menu, lv_obj_t* root_page, lv_obj_t* target,
                const char* label, const lv_font_t* font, int btn_h) {
  lv_obj_t* cont = lv_menu_cont_create(root_page);
  lv_obj_set_style_bg_color(cont, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(cont, ui::dp(8), 0);
  lv_obj_set_style_pad_all(cont, ui::dp(10), 0);
  lv_obj_set_height(cont, btn_h);

  lv_obj_t* lbl = lv_label_create(cont);
  lv_label_set_text(lbl, label);
  lv_obj_set_style_text_color(lbl, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(lbl, font, 0);
  lv_obj_set_flex_grow(lbl, 1);

  lv_obj_t* chev = lv_label_create(cont);
  lv_label_set_text(chev, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_color(chev, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(chev, font, 0);

  lv_menu_set_load_page_event(menu, cont, target);
  // lv_menu's cont is not a ui::make_button — give it press feedback by hand.
  lv_obj_add_event_cb(
      cont, [](lv_event_t*) { ui::play_button_press(); }, LV_EVENT_PRESSED, nullptr);
}

}  // namespace

namespace ui {

void build_settings_tab(lv_obj_t* parent, const ScreenProfile& screen,
                        bool with_brightness, bool with_sound,
                        bool with_wired_paddle, SettingsWidgets& out) {
  const bool compact = is_compact(screen);
  const bool xl = is_xl(screen);
  const lv_font_t* font = ui::font_dp(compact ? 14 : xl ? 28 : 20);
  const lv_font_t* symbol_font = ui::font_dp(compact ? 20 : 28);
  const int btn_size = ui::dp(compact ? 36 : xl ? 64 : 50);
  // Match Home's action-button height so nav rows and connection buttons don't
  // read as cramped on the wider boards.
  const int btn_h = ui::dp(compact ? 40 : xl ? 92 : 64);

  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(parent, 0, 0);

  // Fresh widgets -> stale change-detection is a lie. `out` persists across
  // rebuilds (theme change, scale pair/forget), and if a refresh ran between
  // the trigger and the deferred rebuild, the counts already "match" — the
  // scan lists would stay empty and the stepper labels would keep LVGL's
  // default "Text" forever. Reset so the first update repopulates everything.
  out.last_count = -1;
  out.last_scanning = false;
  out.scale_last_count = -1;
  out.scale_last_scanning = false;

  lv_obj_t* menu = lv_menu_create(parent);
  out.menu = menu;
  lv_obj_set_size(menu, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(menu, lv_color_hex(ui::theme::bg()), 0);
  lv_obj_set_style_bg_opa(menu, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(menu, 0, 0);

  // lv_menu's default header back button is a near-invisible faint arrow on our
  // dark bg. Restyle it into a clear accent chip with a bright arrow + "Back",
  // and brighten the header title.
  if (lv_obj_t* hdr = lv_menu_get_main_header(menu)) {
    lv_obj_set_style_bg_color(hdr, lv_color_hex(ui::theme::bg()), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_text_color(hdr, lv_color_hex(ui::theme::text()), 0);
    lv_obj_set_style_text_font(hdr, font, 0);
    // The back button is pulled out of the header's layout (IGNORE_LAYOUT below),
    // so it can't grow the header itself — give the header room for a btn_h-tall
    // button plus a little breathing space.
    lv_obj_set_style_min_height(hdr, btn_h + ui::dp(16), 0);
    // The page title is the header's 2nd child ([0]=back button). Center it
    // across the full header (the back button floats over the left, below).
    if (lv_obj_get_child_count(hdr) >= 2) {
      lv_obj_t* title = lv_obj_get_child(hdr, 1);
      lv_obj_set_flex_grow(title, 1);
      lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    }
  }
  if (lv_obj_t* back = lv_menu_get_main_header_back_button(menu)) {
    // lv_menu's own widget — press feedback by hand (like the tab buttons).
    lv_obj_add_event_cb(
        back, [](lv_event_t*) { ui::play_button_press(); }, LV_EVENT_PRESSED, nullptr);
    // Pull the back button out of the header's flex flow and pin it left, so the
    // centered title isn't pushed off-center by it.
    lv_obj_add_flag(back, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_height(back, btn_h);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(ui::theme::accent()), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back, ui::dp(8), 0);
    lv_obj_set_style_pad_hor(back, ui::dp(12), 0);
    lv_obj_set_style_pad_ver(back, ui::dp(8), 0);
    // Center the arrow + "Back" label as a row (the button is btn_h tall now, so
    // without this its children sit at the top-left corner).
    lv_obj_set_flex_flow(back, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(back, ui::dp(4), 0);
    // The arrow is an image child; recolor it bright. Add a "Back" label too.
    for (uint32_t i = 0; i < lv_obj_get_child_count(back); ++i) {
      lv_obj_t* ch = lv_obj_get_child(back, i);
      lv_obj_set_style_image_recolor(ch, lv_color_hex(ui::theme::text()), 0);
      lv_obj_set_style_image_recolor_opa(ch, LV_OPA_COVER, 0);
    }
    lv_obj_t* back_lbl = lv_label_create(back);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(ui::theme::text()), 0);
    lv_obj_set_style_text_font(back_lbl, font, 0);
  }

  // --- Leaf pages (the actual content) -------------------------------------
  out.micra_bt_page = lv_menu_page_create(menu, "Bluetooth");
  out.micra_settings_page = lv_menu_page_create(menu, "Settings");
  out.scale_bt_page = lv_menu_page_create(menu, "Bluetooth");
  out.scale_settings_page = lv_menu_page_create(menu, "Settings");
  out.device_page = lv_menu_page_create(menu, "Device");
  page_column(out.micra_bt_page, compact);
  page_column(out.micra_settings_page, compact);
  page_column(out.scale_bt_page, compact);
  page_column(out.scale_settings_page, compact);
  page_column(out.device_page, compact);

  // Micra > Bluetooth: connection
  build_connection_panel(out.micra_bt_page, font, btn_h, &out.saved_row,
                         &out.saved_label, &out.setup_btn, &out.connect_btn,
                         &out.connect_label, &out.forget_btn, &out.scan_btn,
                         &out.status, &out.list, "Tap Scan to find your machine");
  // Micra > Settings: connection behavior + brew + steam boiler
  {
    // Connect to the saved machine at power-up. Off by default: a connected
    // remote occupies the Micra's single BLE slot.
    lv_obj_t* ra = make_setting_row(out.micra_settings_page, "Auto connect", font);
    out.auto_connect_switch = lv_switch_create(ra);
    lv_obj_set_size(out.auto_connect_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));
    // Paddle harness in use. OFF = "unwired": shots are detected from the
    // scale's weight stream instead of paddle edges. Only offered where the
    // paddle hardware exists — other boards are permanently unwired.
    if (with_wired_paddle) {
      lv_obj_t* rw = make_setting_row(out.micra_settings_page, "Wired paddle", font);
      out.wired_paddle_switch = lv_switch_create(rw);
      lv_obj_set_size(out.wired_paddle_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));
      // Post-shot auto-flush: tap cycles Off / 3 s / 6 s. Wired-relay boards
      // only — it drives the paddle line, so unwired-only boards can't flush.
      lv_obj_t* rf = make_setting_row(out.micra_settings_page, "Auto flush", font);
      out.flush_btn = ui::make_button(rf);
      lv_obj_set_height(out.flush_btn, btn_size);
      lv_obj_set_style_pad_hor(out.flush_btn, ui::dp(14), 0);
      lv_obj_set_style_bg_color(out.flush_btn, lv_color_hex(ui::theme::card()), 0);
      out.flush_value = lv_label_create(out.flush_btn);
      lv_obj_set_style_text_color(out.flush_value, lv_color_hex(ui::theme::text()), 0);
      lv_obj_set_style_text_font(out.flush_value, font, 0);
      lv_obj_center(out.flush_value);
    } else {
      out.wired_paddle_switch = nullptr;
      out.flush_btn = out.flush_value = nullptr;
    }
  }
  section_label(out.micra_settings_page, "Brew", font);
  {
    lv_obj_t* r = make_setting_row(out.micra_settings_page, "Temperature", font);
    make_inline_stepper(r, font, symbol_font, btn_size, &out.brew_minus,
                        &out.brew_value, &out.brew_plus, nullptr);
  }
  section_label(out.micra_settings_page, "Steam Boiler", font);
  {
    lv_obj_t* r1 = make_setting_row(out.micra_settings_page, "Enable", font);
    out.steam_switch = lv_switch_create(r1);
    lv_obj_set_size(out.steam_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));
    lv_obj_t* r2 = make_setting_row(out.micra_settings_page, "Temperature", font);
    make_inline_stepper(r2, font, symbol_font, btn_size, &out.boiler_minus,
                        &out.boiler_value, &out.boiler_plus, &out.boiler_sub);
  }

  // Scale > Bluetooth: connection
  build_connection_panel(out.scale_bt_page, font, btn_h, &out.scale_saved_row,
                         &out.scale_saved_label, nullptr, &out.scale_connect_btn,
                         &out.scale_connect_label, &out.scale_forget_btn,
                         &out.scale_scan_btn, &out.scale_status, &out.scale_list,
                         "Tap Scan to find your scale");
  // Scale > Settings: target weight + flow-graph options
  {
    lv_obj_t* r = make_setting_row(out.scale_settings_page, "Target", font);
    make_inline_stepper(r, font, symbol_font, btn_size, &out.target_minus,
                        &out.target_value, &out.target_plus, nullptr);
    // How long the frozen shot-review graph lingers before auto-resetting.
    lv_obj_t* rr = make_setting_row(out.scale_settings_page, "Review hold", font);
    make_inline_stepper(rr, font, symbol_font, btn_size, &out.review_minus,
                        &out.review_value, &out.review_plus, nullptr);
    // Shot-graph line smoothing: tap cycles Off / Light / Medium / Strong.
    lv_obj_t* rs2 = make_setting_row(out.scale_settings_page, "Smoothing", font);
    out.smooth_btn = ui::make_button(rs2);
    lv_obj_set_height(out.smooth_btn, btn_size);
    lv_obj_set_style_pad_hor(out.smooth_btn, ui::dp(14), 0);
    lv_obj_set_style_bg_color(out.smooth_btn, lv_color_hex(ui::theme::card()), 0);
    out.smooth_value = lv_label_create(out.smooth_btn);
    lv_obj_set_style_text_color(out.smooth_value, lv_color_hex(ui::theme::text()), 0);
    lv_obj_set_style_text_font(out.smooth_value, font, 0);
    lv_obj_center(out.smooth_value);
    lv_obj_t* rn = make_setting_row(out.scale_settings_page, "Drop negative g/s", font);
    out.drop_neg_flow_switch = lv_switch_create(rn);
    lv_obj_set_size(out.drop_neg_flow_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));
    lv_obj_t* rs = make_setting_row(out.scale_settings_page, "Oscilloscope graph", font);
    out.scope_graph_switch = lv_switch_create(rs);
    lv_obj_set_size(out.scope_graph_switch, btn_size + ui::dp(8), btn_size / 2 + ui::dp(6));
  }

  // Device (leaf)
  build_device_rows(out.device_page, font, symbol_font, btn_size, with_brightness,
                    with_sound, out);

  // --- Chooser pages: Bluetooth | Settings under each device ---------------
  out.micra_page = lv_menu_page_create(menu, "Micra");
  out.scale_page = lv_menu_page_create(menu, "Scale");
  page_column(out.micra_page, compact);
  page_column(out.scale_page, compact);
  root_entry(menu, out.micra_page, out.micra_bt_page, "Bluetooth", font, btn_h);
  root_entry(menu, out.micra_page, out.micra_settings_page, "Settings", font, btn_h);
  root_entry(menu, out.scale_page, out.scale_bt_page, "Bluetooth", font, btn_h);
  root_entry(menu, out.scale_page, out.scale_settings_page, "Settings", font, btn_h);

  // --- Root page: Micra / Scale / Device -----------------------------------
  out.root_page = lv_menu_page_create(menu, "Settings");
  page_column(out.root_page, compact);
  root_entry(menu, out.root_page, out.micra_page, "Micra", font, btn_h);
  root_entry(menu, out.root_page, out.scale_page, "Scale", font, btn_h);
  root_entry(menu, out.root_page, out.device_page, "Device", font, btn_h);

  // Root-level action row (not a drill-in): soft reboot — the escape hatch for
  // the RGB panel's occasional shifted-raster boot glitch (ghost lines ~10-20px
  // off). Same card styling as the entries, restart glyph instead of a chevron.
  {
    lv_obj_t* cont = lv_menu_cont_create(out.root_page);
    lv_obj_set_style_bg_color(cont, lv_color_hex(ui::theme::card()), 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cont, ui::dp(8), 0);
    lv_obj_set_style_pad_all(cont, ui::dp(10), 0);
    lv_obj_set_height(cont, btn_h);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    out.restart_btn = cont;

    lv_obj_t* lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Restart display");
    lv_obj_set_style_text_color(lbl, lv_color_hex(ui::theme::text()), 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_flex_grow(lbl, 1);

    lv_obj_t* glyph = lv_label_create(cont);
    lv_label_set_text(glyph, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(glyph, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(glyph, font, 0);
  }

  lv_menu_set_page(menu, out.root_page);
}

void settings_select_section(SettingsWidgets& w, int section) {
  if (w.menu == nullptr) return;
  lv_obj_t* page = w.root_page;
  switch (section) {
    case kSectionMicra:         page = w.micra_page; break;
    case kSectionMicraBt:       page = w.micra_bt_page; break;
    case kSectionMicraSettings: page = w.micra_settings_page; break;
    case kSectionScale:         page = w.scale_page; break;
    case kSectionScaleBt:       page = w.scale_bt_page; break;
    case kSectionScaleSettings: page = w.scale_settings_page; break;
    case kSectionDevice:        page = w.device_page; break;
    default:                    page = w.root_page; break;
  }
  lv_menu_set_page(w.menu, page);
}

}  // namespace ui
