#include "ui/app.h"

#include <cmath>
#include <cstdio>

#include <lvgl.h>

#include "ui/theme.h"

namespace {

void style_tab_button(lv_obj_t* btn, const lv_font_t* font) {
  lv_obj_set_style_text_font(btn, font, 0);
  lv_obj_set_style_text_color(btn, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(btn, 10, 0);
  lv_obj_set_style_border_width(btn, 0, 0);

  lv_obj_set_style_text_color(btn, lv_color_hex(ui::theme::text), LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ui::theme::accent), LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_CHECKED);
}

void build_placeholder(lv_obj_t* parent, const char* title, const lv_font_t* font) {
  lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* lbl = lv_label_create(parent);
  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_color(lbl, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_text_font(lbl, font, 0);
  lv_obj_center(lbl);
}

void on_power_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->toggle_power();
}

void on_scan_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->start_scan();
}

void on_result_clicked(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* row = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->save_scanned(static_cast<int>(lv_obj_get_index(row)));
}

void on_forget_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->forget();
}

void on_setup_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->start_token_setup();
}

void on_modal_cancel(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cancel_token_setup();
}

void on_segment_clicked(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->select_settings_section(static_cast<int>(lv_obj_get_index(btn)));
}

// Brew +/- : short click steps 0.1; long-press (and repeat) steps 0.5 snapped.
void on_brew_step(lv_event_t* e, int dir) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  switch (lv_event_get_code(e)) {
    case LV_EVENT_SHORT_CLICKED: app->brew_adjust(dir, /*half=*/false); break;
    case LV_EVENT_LONG_PRESSED:
    case LV_EVENT_LONG_PRESSED_REPEAT: app->brew_adjust(dir, /*half=*/true); break;
    default: break;
  }
}
void on_brew_minus(lv_event_t* e) { on_brew_step(e, -1); }
void on_brew_plus(lv_event_t* e) { on_brew_step(e, +1); }
void on_boiler_minus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->boiler_adjust(-1);
}
void on_boiler_plus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->boiler_adjust(+1);
}
void on_steam_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->steam_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

// Switching the main tab (e.g. leaving Settings) commits any pending temp edit.
void on_tab_changed(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->commit_temp_edits();
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int nearest_steam_level(float c) {
  int best = 0;
  float best_d = 1e9f;
  for (int i = 0; i < 3; ++i) {
    const float d = std::fabs(c - core::kSteamLevelsC[i]);
    if (d < best_d) { best_d = d; best = i; }
  }
  return best;
}

void set_brew_label(ui::SettingsWidgets& s, bool connected) {
  if (!connected) { lv_label_set_text(s.brew_value, "--"); return; }
  char b[16];
  std::snprintf(b, sizeof(b), "%.1f C", s.brew_target);
  lv_label_set_text(s.brew_value, b);
}

void set_boiler_label(ui::SettingsWidgets& s, bool connected) {
  if (!connected) {
    lv_label_set_text(s.boiler_value, "--");
    if (s.boiler_sub) lv_label_set_text(s.boiler_sub, "");
    return;
  }
  if (!s.steam_enabled) {
    lv_label_set_text(s.boiler_value, "Off");
    if (s.boiler_sub) lv_label_set_text(s.boiler_sub, "");
    return;
  }
  char b[16];
  std::snprintf(b, sizeof(b), "Level %d", s.boiler_level + 1);
  lv_label_set_text(s.boiler_value, b);
  char t[16];
  std::snprintf(t, sizeof(t), "%.0f C", core::kSteamLevelsC[s.boiler_level]);
  if (s.boiler_sub) lv_label_set_text(s.boiler_sub, t);
}

void set_clickable(lv_obj_t* o, bool en) {
  if (o == nullptr) return;
  if (en) {
    lv_obj_remove_state(o, LV_STATE_DISABLED);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_add_state(o, LV_STATE_DISABLED);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);  // DISABLED only dims; also block input
  }
}

void set_temp_controls_enabled(ui::SettingsWidgets& s, bool connected) {
  set_clickable(s.brew_minus, connected);
  set_clickable(s.brew_plus, connected);
  set_clickable(s.steam_switch, connected);
  const bool boiler_en = connected && s.steam_enabled;  // can't set level when off
  set_clickable(s.boiler_minus, boiler_en);
  set_clickable(s.boiler_plus, boiler_en);
}

}  // namespace

namespace ui {

App::~App() {
  if (home_.batt_timer != nullptr) lv_timer_delete(home_.batt_timer);
}

void App::build(core::IMachine& machine, core::IProvisioner& provisioner,
                core::IBattery& battery, const ScreenProfile& screen) {
  machine_ = &machine;
  provisioner_ = &provisioner;
  battery_ = &battery;
  const bool compact = is_compact(screen);

  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(ui::theme::bg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  const lv_font_t* tab_font = compact ? &lv_font_montserrat_20 : &lv_font_montserrat_28;

  lv_obj_t* tv = lv_tabview_create(scr);
  tabview_ = tv;
  lv_obj_add_event_cb(tv, on_tab_changed, LV_EVENT_VALUE_CHANGED, this);  // commit on tab exit
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

  build_home_tab(home, screen, home_);
  lv_obj_add_event_cb(home_.power_btn, on_power_clicked, LV_EVENT_CLICKED, this);
  update_home(home_, machine_->snapshot(), battery_->battery());

  build_settings_tab(settings, screen, settings_);
  for (int i = 0; i < kSectionCount; ++i) {
    lv_obj_add_event_cb(settings_.seg[i], on_segment_clicked, LV_EVENT_CLICKED, this);
  }
  lv_obj_add_event_cb(settings_.scan_btn, on_scan_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.setup_btn, on_setup_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.forget_btn, on_forget_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.brew_minus, on_brew_minus, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(settings_.brew_plus, on_brew_plus, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(settings_.boiler_minus, on_boiler_minus, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.boiler_plus, on_boiler_plus, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.steam_switch, on_steam_switch, LV_EVENT_VALUE_CHANGED, this);

  build_placeholder(stats, "Stats", tab_font);

  update_settings_view();
  update_temp_panels(machine_->snapshot());
}

void App::refresh() {
  if (machine_ != nullptr) {
    const core::MachineSnapshot snap = machine_->snapshot();
    if (battery_ != nullptr) update_home(home_, snap, battery_->battery());
    update_temp_panels(snap);
  }
  update_settings_view();
}

// Stepping only changes the LOCAL value/display; the BLE write is deferred to
// commit_temp_edits() (on exit) so we don't write on every press.
void App::brew_adjust(int dir, bool half) {
  float v = settings_.brew_target;
  if (half) {  // long-press: snap to the next 0.5 grid point in the direction
    v = (dir > 0) ? std::ceil((v + 0.01f) / 0.5f) * 0.5f
                  : std::floor((v - 0.01f) / 0.5f) * 0.5f;
  } else {
    v += dir * 0.1f;
  }
  settings_.brew_target = clampf(v, core::kBrewTargetMinC, core::kBrewTargetMaxC);
  settings_.brew_dirty = true;
  set_brew_label(settings_, true);
}

void App::boiler_adjust(int dir) {
  int lvl = settings_.boiler_level + dir;
  lvl = lvl < 0 ? 0 : (lvl > 2 ? 2 : lvl);
  settings_.boiler_level = lvl;
  settings_.boiler_dirty = true;
  set_boiler_label(settings_, true);
}

void App::steam_set_enabled(bool on) {
  settings_.steam_enabled = on;
  settings_.steam_enable_dirty = true;  // hold until the machine confirms
  set_boiler_label(settings_, true);    // reflect Off / level
  set_temp_controls_enabled(settings_, true);
  if (machine_ != nullptr) machine_->set_steam_enabled(on);  // immediate (single toggle)
}

void App::commit_temp_edits() {
  if (machine_ == nullptr) return;
  if (settings_.brew_dirty) {
    machine_->set_brew_target(settings_.brew_target);
    settings_.brew_dirty = false;
  }
  if (settings_.boiler_dirty) {
    machine_->set_steam_target(core::kSteamLevelsC[settings_.boiler_level]);
    settings_.boiler_dirty = false;
  }
}

void App::update_temp_panels(const core::MachineSnapshot& s) {
  const bool connected = s.link == core::Link::Connected;
  if (connected) {
    // Track the machine's polled values whenever we're not mid-edit, so an
    // app-side change flows in as the new value/starting point (and the early-
    // connect race where the first read hasn't populated temps self-corrects).
    if (!settings_.steam_enable_dirty) {
      settings_.steam_enabled = s.steam_enabled;
    } else if (s.steam_enabled == settings_.steam_enabled) {
      settings_.steam_enable_dirty = false;  // machine confirmed the toggle
    }
    if (settings_.steam_enabled) lv_obj_add_state(settings_.steam_switch, LV_STATE_CHECKED);
    else lv_obj_remove_state(settings_.steam_switch, LV_STATE_CHECKED);

    if (!settings_.brew_dirty) {
      settings_.brew_target = s.brew_target_c;
      set_brew_label(settings_, true);
    }
    if (!settings_.boiler_dirty) {
      settings_.boiler_level = nearest_steam_level(s.boiler_target_c);
    }
    set_boiler_label(settings_, true);  // reflects level + steam on/off
    set_temp_controls_enabled(settings_, true);
  } else {
    settings_.brew_dirty = false;
    settings_.boiler_dirty = false;
    settings_.steam_enable_dirty = false;
    set_brew_label(settings_, false);
    set_boiler_label(settings_, false);
    set_temp_controls_enabled(settings_, false);
  }
}

void App::show_tab(int index) {
  if (tabview_ != nullptr) lv_tabview_set_active(tabview_, index, LV_ANIM_OFF);
}

void App::toggle_power() {
  if (machine_ == nullptr) return;
  const bool on = machine_->snapshot().power == core::Power::On;
  machine_->set_power(!on);
  refresh();
}

void App::start_scan() {
  if (provisioner_ == nullptr) return;
  provisioner_->start_scan();
  settings_.last_count = -1;  // force the list to rebuild on next refresh
}

void App::save_scanned(int index) {
  if (provisioner_ == nullptr) return;
  const std::vector<core::ScanResult> results = provisioner_->scan_results();
  if (index < 0 || index >= static_cast<int>(results.size())) return;

  provisioner_->save_device(results[index]);
  lv_label_set_text(settings_.status, "Saved - connecting...");
  if (tabview_ != nullptr) lv_tabview_set_active(tabview_, 0, LV_ANIM_ON);  // Home
}

void App::forget() {
  if (provisioner_ == nullptr) return;
  provisioner_->forget();
  settings_.last_count = -1;  // force a refresh of the settings view
}

void App::start_token_setup() {
  if (provisioner_ == nullptr) return;
  provisioner_->start_token_setup();
  show_setup_modal();
}

void App::cancel_token_setup() {
  if (provisioner_ != nullptr) provisioner_->stop_token_setup();
  close_setup_modal();
}

void App::show_setup_modal() {
  if (setup_modal_ != nullptr || provisioner_ == nullptr) return;

  lv_obj_t* bg = lv_obj_create(lv_layer_top());  // full-screen dimmed overlay
  lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(bg, LV_OPA_70, 0);
  setup_modal_ = bg;

  lv_obj_t* card = lv_obj_create(bg);
  lv_obj_set_size(card, lv_pct(88), LV_SIZE_CONTENT);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, lv_color_hex(ui::theme::card), 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(card, 12, 0);
  lv_obj_set_style_pad_row(card, 8, 0);

  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "Enter token");
  lv_obj_set_style_text_color(title, lv_color_hex(ui::theme::text), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

  lv_obj_t* steps = lv_label_create(card);
  char buf[160];
  std::snprintf(buf, sizeof(buf), "1. Join WiFi: %s\n2. Open: %s\n3. Paste token, Save",
                provisioner_->setup_ssid(), provisioner_->setup_url());
  lv_label_set_text(steps, buf);
  lv_obj_set_style_text_color(steps, lv_color_hex(ui::theme::muted), 0);
  lv_obj_set_style_text_font(steps, &lv_font_montserrat_14, 0);

  lv_obj_t* cancel = lv_button_create(card);
  lv_obj_set_style_bg_color(cancel, lv_color_hex(ui::theme::rail), 0);
  lv_obj_add_event_cb(cancel, on_modal_cancel, LV_EVENT_CLICKED, this);
  lv_obj_t* cl = lv_label_create(cancel);
  lv_label_set_text(cl, "Cancel");
  lv_obj_set_style_text_color(cl, lv_color_hex(ui::theme::text), 0);
  lv_obj_center(cl);
}

void App::close_setup_modal() {
  if (setup_modal_ != nullptr) {
    lv_obj_delete(setup_modal_);
    setup_modal_ = nullptr;
  }
}

void App::select_settings_section(int section) {
  commit_temp_edits();  // write pending edits from the section we're leaving
  settings_select_section(settings_, section);
}

void App::update_settings_view() {
  if (provisioner_ == nullptr) return;

  // Saved-machine row: name + (Setup if no token) + Forget, when a machine is saved.
  const std::string saved = provisioner_->saved_name();
  if (saved.empty()) {
    lv_obj_add_flag(settings_.saved_row, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(settings_.saved_row, LV_OBJ_FLAG_HIDDEN);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Saved: %s", saved.c_str());
    lv_label_set_text(settings_.saved_label, buf);
    if (provisioner_->has_token()) {
      lv_obj_add_flag(settings_.setup_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(settings_.setup_btn, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Auto-close the setup modal once the token's been received (portal ended).
  if (setup_modal_ != nullptr && !provisioner_->token_setup_active()) {
    close_setup_modal();
    if (tabview_ != nullptr) lv_tabview_set_active(tabview_, 0, LV_ANIM_ON);  // Home
  }

  const bool scanning = provisioner_->scanning();
  const std::vector<core::ScanResult> results = provisioner_->scan_results();
  const int count = static_cast<int>(results.size());

  if (scanning) {
    lv_label_set_text(settings_.status, "Scanning...");
  } else if (count == 0) {
    lv_label_set_text(settings_.status, "Tap Scan to find your machine");
  } else {
    lv_label_set_text(settings_.status, "Tap a machine to save it");
  }

  // Only rebuild the row list when results actually change (avoids flicker).
  if (count == settings_.last_count && scanning == settings_.last_scanning) return;
  settings_.last_count = count;
  settings_.last_scanning = scanning;

  lv_obj_clean(settings_.list);
  for (int i = 0; i < count; ++i) {
    lv_obj_t* row = lv_button_create(settings_.list);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_style_bg_color(row, lv_color_hex(ui::theme::card), 0);
    lv_obj_add_event_cb(row, on_result_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* lbl = lv_label_create(row);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s   %d dBm", results[i].name, results[i].rssi);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ui::theme::text), 0);
    lv_obj_center(lbl);
  }
}

}  // namespace ui
