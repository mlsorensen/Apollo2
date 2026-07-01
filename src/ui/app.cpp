#include "ui/app.h"

#include <cmath>
#include <cstdio>
#include <string>

#include <lvgl.h>

#include "ui/stats_tab.h"
#include "ui/theme.h"
#include "ui/units.h"
#include "ui/widgets.h"
#include "version.h"

namespace {

void style_tab_button(lv_obj_t* btn, const lv_font_t* font) {
  lv_obj_set_style_text_font(btn, font, 0);
  lv_obj_set_style_text_color(btn, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(btn, 10, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);  // kill LVGL's default (blue, unthemed) shadow

  lv_obj_set_style_text_color(btn, lv_color_hex(ui::theme::text()), LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(btn, lv_color_hex(ui::theme::accent()), LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_CHECKED);
  lv_obj_set_style_border_width(btn, 0, LV_STATE_CHECKED);  // kill the theme's blue indicator
  lv_obj_set_style_shadow_width(btn, 0, LV_STATE_CHECKED);
}

void on_power_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->toggle_power();
}

void on_tare_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->tare_scale();
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
  static_cast<ui::App*>(lv_event_get_user_data(e))->open_token_setup();
}
void on_connect_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->toggle_connection();
}
void on_token_retry(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->retry_pairing();
}
void on_token_wifi(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->start_token_setup();
}
void on_token_cancel(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->dismiss_modal();
}
void on_pairing_cancel(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cancel_pairing();
}
void on_wifi_cancel(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cancel_token_setup();
}

lv_obj_t* modal_button(lv_obj_t* parent, const char* label, uint32_t color,
                       lv_event_cb_t cb, void* app) {
  lv_obj_t* b = ui::make_button(parent);
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, app);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, label);
  lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_obj_center(l);
  return b;
}

void on_scale_scan_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->start_scale_scan();
}
void on_scale_result_clicked(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* row = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->save_scanned_scale(static_cast<int>(lv_obj_get_index(row)));
}
void on_scale_forget_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->forget_scale();
}
void on_scale_connect_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->toggle_scale_connection();
}
void on_target_minus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->target_adjust(-1);
}
void on_target_plus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->target_adjust(+1);
}

void on_stats_segment(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->select_stats_section(static_cast<int>(lv_obj_get_index(btn)));
}
void on_zoom_in(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->zoom_step(-1);  // shorter window
}
void on_zoom_out(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->zoom_step(+1);  // longer window
}

// Stats chart x-axis windows the zoom button cycles through.
struct ZoomLevel {
  uint32_t window_s;
  const char* label;
};
constexpr ZoomLevel kZooms[] = {
    {30u * 60, "30m"}, {60u * 60, "1h"},   {3u * 3600, "3h"},
    {6u * 3600, "6h"}, {12u * 3600, "12h"}, {24u * 3600, "24h"},
};
constexpr int kZoomCount = static_cast<int>(sizeof(kZooms) / sizeof(kZooms[0]));

// Brew +/- : short click steps 0.1; holding steps 0.5 snapped, throttled to ~2/s
// (LVGL's repeat fires ~10/s, which spun the value too fast).
void on_brew_step(lv_event_t* e, int dir) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  static uint32_t last_repeat = 0;
  constexpr uint32_t kRepeatMs = 500;
  switch (lv_event_get_code(e)) {
    case LV_EVENT_SHORT_CLICKED:
      app->brew_adjust(dir, /*half=*/false);
      break;
    case LV_EVENT_LONG_PRESSED:  // first coarse step when the hold begins
      last_repeat = lv_tick_get();
      app->brew_adjust(dir, /*half=*/true);
      break;
    case LV_EVENT_LONG_PRESSED_REPEAT:
      if (lv_tick_get() - last_repeat >= kRepeatMs) {
        last_repeat = lv_tick_get();
        app->brew_adjust(dir, /*half=*/true);
      }
      break;
    default:
      break;
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
void on_brightness_minus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->brightness_adjust(-1);
}
void on_brightness_plus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->brightness_adjust(+1);
}

void set_brightness_label(ui::SettingsWidgets& s) {
  if (s.brightness_value == nullptr) return;  // no brightness row on this board
  char b[12];
  std::snprintf(b, sizeof(b), "%d %%", s.brightness);
  lv_label_set_text(s.brightness_value, b);
}

void set_time_labels(ui::SettingsWidgets& s) {
  char b[8];
  if (s.clock_24h) {
    std::snprintf(b, sizeof(b), "%02d", s.set_hour);
  } else {
    const int h12 = (s.set_hour % 12 == 0) ? 12 : s.set_hour % 12;
    std::snprintf(b, sizeof(b), "%d %s", h12, s.set_hour < 12 ? "AM" : "PM");
  }
  lv_label_set_text(s.hour_value, b);
  std::snprintf(b, sizeof(b), "%02d", s.set_minute);
  lv_label_set_text(s.minute_value, b);
}

// Hour/Minute steppers: tap = +/-1, hold = repeat (spin through values).
bool is_step_event(lv_event_t* e) {
  const lv_event_code_t c = lv_event_get_code(e);
  return c == LV_EVENT_SHORT_CLICKED || c == LV_EVENT_LONG_PRESSED_REPEAT;
}
void on_hour_minus(lv_event_t* e) {
  if (is_step_event(e)) static_cast<ui::App*>(lv_event_get_user_data(e))->hour_adjust(-1);
}
void on_hour_plus(lv_event_t* e) {
  if (is_step_event(e)) static_cast<ui::App*>(lv_event_get_user_data(e))->hour_adjust(+1);
}
void on_minute_minus(lv_event_t* e) {
  if (is_step_event(e)) static_cast<ui::App*>(lv_event_get_user_data(e))->minute_adjust(-1);
}
void on_minute_plus(lv_event_t* e) {
  if (is_step_event(e)) static_cast<ui::App*>(lv_event_get_user_data(e))->minute_adjust(+1);
}
void on_clock_mode_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->set_clock_24h(lv_obj_has_state(sw, LV_STATE_CHECKED));
}
void on_units_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->set_use_fahrenheit(lv_obj_has_state(sw, LV_STATE_CHECKED));
}
void on_theme_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->theme_select(/*next=*/-1);
}
// Runs from lv_async_call (after the event handler returns), so it's safe to
// delete the very widget the click came from and rebuild the screen.
void theme_rebuild_cb(void* app) {
  static_cast<ui::App*>(app)->apply_pending_theme();
}

// Deferred UI rebuild after a scale is paired/forgotten (Home swaps between the
// classic and scale-aware layouts). Same lv_async_call safety as the theme path.
void layout_rebuild_cb(void* app) {
  static_cast<ui::App*>(app)->apply_layout_rebuild();
}

void set_theme_label(ui::SettingsWidgets& s) {
  if (s.theme_value != nullptr) lv_label_set_text(s.theme_value, ui::theme::name(s.theme_index));
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

void set_brew_label(ui::SettingsWidgets& s, bool connected, bool f) {
  if (!connected) { lv_label_set_text(s.brew_value, "--"); return; }
  char b[16];
  std::snprintf(b, sizeof(b), "%.1f %s", ui::temp_disp(s.brew_target, f), ui::temp_unit(f));
  lv_label_set_text(s.brew_value, b);
}

void set_boiler_label(ui::SettingsWidgets& s, bool connected, bool f) {
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
  std::snprintf(t, sizeof(t), "%.0f %s",
                ui::temp_disp(core::kSteamLevelsC[s.boiler_level], f), ui::temp_unit(f));
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

// Whether a step is still in range (so we can grey out a stepper at its limit —
// otherwise pressing + at max gives no feedback).
bool brew_can_dec(const ui::SettingsWidgets& s) {
  return s.brew_target > core::kBrewTargetMinC + 0.001f;
}
bool brew_can_inc(const ui::SettingsWidgets& s) {
  return s.brew_target < core::kBrewTargetMaxC - 0.001f;
}
bool boiler_can_dec(const ui::SettingsWidgets& s) { return s.boiler_level > 0; }
bool boiler_can_inc(const ui::SettingsWidgets& s) { return s.boiler_level < 2; }

void set_temp_controls_enabled(ui::SettingsWidgets& s, bool connected) {
  set_clickable(s.brew_minus, connected && brew_can_dec(s));
  set_clickable(s.brew_plus, connected && brew_can_inc(s));
  set_clickable(s.steam_switch, connected);
  const bool boiler_en = connected && s.steam_enabled;  // can't set level when off
  set_clickable(s.boiler_minus, boiler_en && boiler_can_dec(s));
  set_clickable(s.boiler_plus, boiler_en && boiler_can_inc(s));
}

}  // namespace

namespace ui {

App::~App() {
  if (home_.batt_timer != nullptr) lv_timer_delete(home_.batt_timer);
}

void App::build(core::IMachine& machine, core::IProvisioner& provisioner,
                core::IBattery& battery, core::IDisplaySettings& display,
                core::IClock& clock, core::IHistory& history, core::IScale& scale,
                core::IScaleProvisioner& scale_provisioner, core::IBrewController& brew,
                const ScreenProfile& screen) {
  machine_ = &machine;
  provisioner_ = &provisioner;
  battery_ = &battery;
  display_ = &display;
  clock_ = &clock;
  history_ = &history;
  scale_ = &scale;
  scale_provisioner_ = &scale_provisioner;
  brew_ = &brew;
  screen_ = screen;
  const bool compact = is_compact(screen);
  const bool xl = is_xl(screen);

  ui::theme::set_active(display_->theme());  // pick the palette before any widget is colored

  // A rebuild (theme change) recreates everything; drop the old charge timer
  // first so we don't leak it across rebuilds.
  if (home_.batt_timer != nullptr) {
    lv_timer_delete(home_.batt_timer);
    home_.batt_timer = nullptr;
  }

  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(ui::theme::bg()), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  const lv_font_t* tab_font = compact ? &lv_font_montserrat_20 : &lv_font_montserrat_28;
  const int rail_pad = compact ? 4 : xl ? 12 : 8;

  lv_obj_t* tv = lv_tabview_create(scr);
  tabview_ = tv;
  lv_obj_add_event_cb(tv, on_tab_changed, LV_EVENT_VALUE_CHANGED, this);  // commit on tab exit
  lv_tabview_set_tab_bar_position(tv, compact ? LV_DIR_BOTTOM : LV_DIR_LEFT);
  lv_tabview_set_tab_bar_size(tv, compact ? 44 : xl ? 120 : 88);
  lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tv, 0, 0);

  lv_obj_t* content = lv_tabview_get_content(tv);
  lv_obj_set_style_bg_color(content, lv_color_hex(ui::theme::bg()), 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);

  lv_obj_t* rail = lv_tabview_get_tab_bar(tv);
  lv_obj_set_style_bg_color(rail, lv_color_hex(ui::theme::rail()), 0);
  lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(rail, rail_pad, 0);
  lv_obj_set_style_pad_row(rail, rail_pad, 0);
  lv_obj_set_style_pad_column(rail, rail_pad, 0);

  lv_obj_t* home = lv_tabview_add_tab(tv, LV_SYMBOL_HOME);
  lv_obj_t* settings = lv_tabview_add_tab(tv, LV_SYMBOL_SETTINGS);
  lv_obj_t* stats = lv_tabview_add_tab(tv, LV_SYMBOL_LIST);

  for (uint32_t i = 0; i < lv_tabview_get_tab_count(tv); ++i) {
    style_tab_button(lv_tabview_get_tab_button(tv, i), tab_font);
  }

  const bool scale_on =
      scale_provisioner_ != nullptr && !scale_provisioner_->saved_name().empty();
  build_home_tab(home, screen, scale_on, home_);
  lv_obj_add_event_cb(home_.power_btn, on_power_clicked, LV_EVENT_CLICKED, this);
  if (home_.tare_btn != nullptr)
    lv_obj_add_event_cb(home_.tare_btn, on_tare_clicked, LV_EVENT_CLICKED, this);
  // Inline set-point steppers (large screens): reuse the Settings handlers so Home
  // edits flow through the same brew_adjust/boiler_adjust + deferred-commit path.
  if (home_.brew_minus != nullptr) {
    lv_obj_add_event_cb(home_.brew_minus, on_brew_minus, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(home_.brew_plus, on_brew_plus, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(home_.boiler_minus, on_boiler_minus, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(home_.boiler_plus, on_boiler_plus, LV_EVENT_CLICKED, this);
  }
  battery_state_ = battery_->battery();
  update_home(home_, machine_->snapshot(), battery_state_, clock_->now(),
              clock_->use_24h(), display_->use_fahrenheit(), scale_->snapshot(),
              brew_->snapshot());
  update_battery_runtime(battery_state_);

  build_settings_tab(settings, screen, display_->supports_brightness(), settings_);
  // lv_menu handles page navigation (root <-> Micra/Scale/Device) itself.
  // Micra connection:
  lv_obj_add_event_cb(settings_.scan_btn, on_scan_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.setup_btn, on_setup_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.connect_btn, on_connect_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.forget_btn, on_forget_clicked, LV_EVENT_CLICKED, this);
  // Scale connection + target:
  lv_obj_add_event_cb(settings_.scale_scan_btn, on_scale_scan_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.scale_connect_btn, on_scale_connect_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.scale_forget_btn, on_scale_forget_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.target_minus, on_target_minus, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.target_plus, on_target_plus, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.brew_minus, on_brew_minus, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(settings_.brew_plus, on_brew_plus, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(settings_.boiler_minus, on_boiler_minus, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.boiler_plus, on_boiler_plus, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.steam_switch, on_steam_switch, LV_EVENT_VALUE_CHANGED, this);
  if (settings_.brightness_minus != nullptr) {  // absent on boards that can't dim
    lv_obj_add_event_cb(settings_.brightness_minus, on_brightness_minus, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(settings_.brightness_plus, on_brightness_plus, LV_EVENT_CLICKED, this);
  }
  lv_obj_add_event_cb(settings_.hour_minus, on_hour_minus, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(settings_.hour_plus, on_hour_plus, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(settings_.minute_minus, on_minute_minus, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(settings_.minute_plus, on_minute_plus, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(settings_.clock_mode_switch, on_clock_mode_switch,
                      LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.units_switch, on_units_switch, LV_EVENT_VALUE_CHANGED, this);
  if (settings_.theme_btn != nullptr)
    lv_obj_add_event_cb(settings_.theme_btn, on_theme_clicked, LV_EVENT_CLICKED, this);

  build_stats_tab(stats, screen, stats_);
  for (int i = 0; i < kStatsCount; ++i) {
    lv_obj_add_event_cb(stats_.seg[i], on_stats_segment, LV_EVENT_CLICKED, this);
  }
  lv_obj_add_event_cb(stats_.zoom_in, on_zoom_in, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(stats_.zoom_out, on_zoom_out, LV_EVENT_CLICKED, this);

  if (brew_ != nullptr) settings_.target_g = brew_->snapshot().target_weight_g;
  update_settings_view();
  update_temp_panels(machine_->snapshot());

  settings_.brightness = display_->brightness();
  set_brightness_label(settings_);
  settings_.clock_24h = clock_->use_24h();
  if (settings_.clock_24h) lv_obj_add_state(settings_.clock_mode_switch, LV_STATE_CHECKED);
  if (display_->use_fahrenheit()) lv_obj_add_state(settings_.units_switch, LV_STATE_CHECKED);
  seed_time_steppers();

  settings_.theme_index = ui::theme::active_index();
  set_theme_label(settings_);
}

void App::refresh() {
  if (machine_ != nullptr) {
    const core::MachineSnapshot snap = machine_->snapshot();
    if (battery_ != nullptr) {
      battery_state_ = battery_->battery();
      update_home(home_, snap, battery_state_, clock_->now(), clock_->use_24h(),
                  display_->use_fahrenheit(), scale_->snapshot(), brew_->snapshot());
      update_battery_runtime(battery_state_);

      // Critically-low pack (on battery, sustained) -> hand off to deep sleep.
      if (batt_low_handler_ && battery_state_.present && !battery_state_.charging &&
          battery_state_.volts > 0.0f && battery_state_.volts <= batt_cutoff_volts_) {
        if (++batt_low_count_ >= 6) {  // ~3 s at the 500 ms refresh -> not a transient sag
          batt_low_count_ = 0;
          batt_low_handler_();
        }
      } else {
        batt_low_count_ = 0;
      }
    }
    update_temp_panels(snap);
    handle_pairing(snap.link);

    // WiFi token portal: close it once the token verifies (Connected). A bad
    // token leaves the link in NeedsToken with the portal still up, so the user
    // can paste a corrected token and resubmit; the AP's safety timeout covers a
    // walk-away (we close the modal if the portal ends on its own).
    if (wifi_setup_shown_) {
      if (snap.link == core::Link::Connected) {
        if (provisioner_ != nullptr) provisioner_->stop_token_setup();
        close_modal();
        if (tabview_ != nullptr) lv_tabview_set_active(tabview_, 0, LV_ANIM_ON);  // Home
      } else if (provisioner_ != nullptr && !provisioner_->token_setup_active()) {
        close_modal();  // portal ended on its own (safety timeout)
      }
    }
  }
  update_settings_view();
  if (tabview_ != nullptr && lv_tabview_get_tab_active(tabview_) == 2) {
    update_stats_view();  // only while the Stats tab is showing
  }
}

// A press updates the local value AND writes it to the machine right away (the
// link coalesces rapid writes via an atomic, so this isn't BLE spam). "dirty" is
// held until the machine confirms the new value (update_temp_panels) so the
// displayed set-point doesn't flicker back during the write round-trip.
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
  set_brew_label(settings_, true, display_->use_fahrenheit());
  set_temp_controls_enabled(settings_, true);  // re-grey at the new extreme
  sync_home_setpoints(true);
  if (machine_ != nullptr) machine_->set_brew_target(settings_.brew_target);
}

void App::boiler_adjust(int dir) {
  int lvl = settings_.boiler_level + dir;
  lvl = lvl < 0 ? 0 : (lvl > 2 ? 2 : lvl);
  settings_.boiler_level = lvl;
  settings_.boiler_dirty = true;
  set_boiler_label(settings_, true, display_->use_fahrenheit());
  set_temp_controls_enabled(settings_, true);  // re-grey at level 1/3
  sync_home_setpoints(true);
  if (machine_ != nullptr) machine_->set_steam_target(core::kSteamLevelsC[lvl]);
}

void App::steam_set_enabled(bool on) {
  settings_.steam_enabled = on;
  settings_.steam_enable_dirty = true;  // hold until the machine confirms
  set_boiler_label(settings_, true, display_->use_fahrenheit());    // reflect Off / level
  set_temp_controls_enabled(settings_, true);
  sync_home_setpoints(true);
  if (machine_ != nullptr) machine_->set_steam_enabled(on);  // immediate (single toggle)
}

// Mirror the editable set-points onto the Home tab's inline steppers (large
// screens; no-op on compact, where Home has no steppers and update_home owns the
// labels). Same dirty-aware source as the Settings labels, so an edit shows on
// both tabs and the deferred commit still applies on tab exit.
void App::sync_home_setpoints(bool connected) {
  if (home_.brew_minus == nullptr) return;  // compact: no Home steppers
  const bool steam = settings_.steam_enabled;

  const bool f = display_->use_fahrenheit();
  char b[16];
  if (!connected) {
    lv_label_set_text(home_.brew_set, "--");
  } else {
    std::snprintf(b, sizeof(b), "%.1f %s", ui::temp_disp(settings_.brew_target, f),
                  ui::temp_unit(f));
    lv_label_set_text(home_.brew_set, b);
  }
  if (!connected) {
    lv_label_set_text(home_.boiler_set, "--");
  } else if (!steam) {
    lv_label_set_text(home_.boiler_set, "Off");
  } else {
    std::snprintf(b, sizeof(b), "%.0f %s",
                  ui::temp_disp(core::kSteamLevelsC[settings_.boiler_level], f),
                  ui::temp_unit(f));
    lv_label_set_text(home_.boiler_set, b);
  }

  set_clickable(home_.brew_minus, connected && brew_can_dec(settings_));
  set_clickable(home_.brew_plus, connected && brew_can_inc(settings_));
  set_clickable(home_.boiler_minus, connected && steam && boiler_can_dec(settings_));
  set_clickable(home_.boiler_plus, connected && steam && boiler_can_inc(settings_));
}

void App::brightness_adjust(int dir) {
  int b = settings_.brightness + dir * 10;
  if (b < 10) b = 10;  // never fully dark (can't see to turn it back up)
  if (b > 100) b = 100;
  settings_.brightness = b;
  set_brightness_label(settings_);
  if (display_ != nullptr) display_->set_brightness(b);  // live + persisted
}

void App::seed_time_steppers() {
  if (clock_ == nullptr) return;
  const core::WallTime t = clock_->now();
  settings_.set_hour = t.valid ? t.hour : 12;
  settings_.set_minute = t.valid ? t.minute : 0;
  set_time_labels(settings_);
}

void App::hour_adjust(int dir) {
  settings_.set_hour = (settings_.set_hour + dir + 24) % 24;
  set_time_labels(settings_);
  if (clock_ != nullptr) clock_->set(settings_.set_hour, settings_.set_minute);
}

void App::minute_adjust(int dir) {
  settings_.set_minute = (settings_.set_minute + dir + 60) % 60;
  set_time_labels(settings_);
  if (clock_ != nullptr) clock_->set(settings_.set_hour, settings_.set_minute);
}

void App::set_clock_24h(bool on) {
  settings_.clock_24h = on;
  if (clock_ != nullptr) clock_->set_24h(on);
  set_time_labels(settings_);  // re-render the Hour stepper; Home updates on refresh
}

void App::set_use_fahrenheit(bool on) {
  if (display_ != nullptr) display_->set_use_fahrenheit(on);  // persist
  // Repaint temps now (Home, Settings, Stats) instead of waiting for the next tick.
  if (machine_ != nullptr) {
    const core::MachineSnapshot snap = machine_->snapshot();
    update_temp_panels(snap);
    update_home(home_, snap, battery_state_, clock_->now(), clock_->use_24h(), on,
                scale_->snapshot(), brew_->snapshot());
    update_stats_view();
  }
}

void App::theme_select(int idx) {
  const int n = ui::theme::count();
  if (idx < 0) idx = settings_.theme_index + 1;  // tap cycles to the next scheme
  settings_.theme_index = (idx % n + n) % n;
  set_theme_label(settings_);  // instant name feedback before the recolor
  // Defer the recolor: rebuilding the screen here would delete the button that's
  // mid-click. lv_async_call runs it after this handler returns.
  if (!theme_rebuild_pending_) {
    theme_rebuild_pending_ = true;
    lv_async_call(theme_rebuild_cb, this);
  }
}

void App::apply_pending_theme() {
  theme_rebuild_pending_ = false;
  if (display_ != nullptr) display_->set_theme(settings_.theme_index);  // persist
  rebuild();
}

void App::apply_layout_rebuild() {
  layout_rebuild_pending_ = false;
  rebuild();
}

void App::request_layout_rebuild(int section) {
  rebuild_section_ = section;
  if (!layout_rebuild_pending_) {
    layout_rebuild_pending_ = true;
    lv_async_call(layout_rebuild_cb, this);  // safe to rebuild after this handler
  }
}

void App::rebuild() {
  if (machine_ == nullptr) return;  // never built yet
  const int section = rebuild_section_;
  rebuild_section_ = kSectionDevice;  // reset to the default for the next rebuild
  // Preserve the Device panel's scroll position (the theme button sits below the
  // fold, so a naive rebuild would bounce the user back to the top each cycle).
  int32_t scroll_y = 0;
  if (section == kSectionDevice && settings_.device_page != nullptr)
    scroll_y = lv_obj_get_scroll_y(settings_.device_page);

  build(*machine_, *provisioner_, *battery_, *display_, *clock_, *history_, *scale_,
        *scale_provisioner_, *brew_, screen_);
  show_tab(1);                       // back to Settings...
  select_settings_section(section);  // ...on the section that triggered the rebuild

  if (section == kSectionDevice && settings_.device_page != nullptr) {
    lv_obj_update_layout(settings_.device_page);  // compute the scroll range first
    lv_obj_scroll_to_y(settings_.device_page, scroll_y, LV_ANIM_OFF);
  }
}

void App::commit_temp_edits() {
  // Edits are written on each press now; this is just a safety re-assert of the
  // latest value when leaving a screen. We do NOT clear dirty here — that happens
  // on machine confirmation (update_temp_panels), so the value can't revert.
  if (machine_ == nullptr) return;
  if (settings_.brew_dirty) machine_->set_brew_target(settings_.brew_target);
  if (settings_.boiler_dirty)
    machine_->set_steam_target(core::kSteamLevelsC[settings_.boiler_level]);
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

    // Track the machine's value unless we have an unconfirmed local edit; clear
    // the edit once the machine reports it back (then resume tracking).
    if (!settings_.brew_dirty) {
      settings_.brew_target = s.brew_target_c;
    } else if (std::fabs(s.brew_target_c - settings_.brew_target) < 0.05f) {
      settings_.brew_dirty = false;
    }
    set_brew_label(settings_, true, display_->use_fahrenheit());

    if (!settings_.boiler_dirty) {
      settings_.boiler_level = nearest_steam_level(s.boiler_target_c);
    } else if (nearest_steam_level(s.boiler_target_c) == settings_.boiler_level) {
      settings_.boiler_dirty = false;
    }
    set_boiler_label(settings_, true, display_->use_fahrenheit());  // reflects level + steam on/off
    set_temp_controls_enabled(settings_, true);
    sync_home_setpoints(true);
  } else {
    settings_.brew_dirty = false;
    settings_.boiler_dirty = false;
    settings_.steam_enable_dirty = false;
    set_brew_label(settings_, false, display_->use_fahrenheit());
    set_boiler_label(settings_, false, display_->use_fahrenheit());
    set_temp_controls_enabled(settings_, false);
    sync_home_setpoints(false);
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

void App::tare_scale() {
  if (scale_ != nullptr) scale_->tare();
}

void App::pump_scale_chart() {
  if (scale_ == nullptr) return;
  float buf[64];
  const size_t n = scale_->drain_flow(buf, 64);  // also keeps the buffer drained
  if (home_.flow_chart == nullptr || home_.flow_series == nullptr) return;
  for (size_t i = 0; i < n; ++i) {
    lv_chart_set_next_value(home_.flow_chart, home_.flow_series,
                            static_cast<int32_t>(buf[i] * 100.0f));
  }
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

  provisioner_->save_device(results[index]);  // saves + starts pairing-mode read
  pairing_active_ = true;                      // wait for the outcome (Connected / NeedsToken)
  show_pairing_modal();
}

void App::forget() {
  if (provisioner_ == nullptr) return;
  provisioner_->forget();
  settings_.last_count = -1;  // force a refresh of the settings view
}

void App::toggle_connection() {
  if (provisioner_ == nullptr) return;
  provisioner_->set_connect_enabled(!provisioner_->connect_enabled());
  update_settings_view();  // reflect the new label/colour immediately
}

lv_obj_t* App::open_modal(const char* title, const char* body) {
  close_modal();
  lv_obj_t* bg = lv_obj_create(lv_layer_top());  // full-screen dimmed overlay
  lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(bg, LV_OPA_70, 0);
  modal_ = bg;
  // Hide the underlying UI while the modal is up: it's fully covered by the
  // overlay anyway, and not rendering interactive background widgets avoids a
  // pathological LVGL redraw loop (and saves work — nothing behind is visible).
  if (tabview_ != nullptr) lv_obj_add_flag(tabview_, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* card = lv_obj_create(bg);
  lv_obj_set_width(card, lv_pct(88));
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_set_style_pad_row(card, 10, 0);

  lv_obj_t* t = lv_label_create(card);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_color(t, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);

  lv_obj_t* b = lv_label_create(card);
  lv_label_set_text(b, body);
  lv_obj_set_width(b, lv_pct(100));
  lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(b, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
  return card;
}

void App::close_modal() {
  if (modal_ != nullptr) {
    lv_obj_delete(modal_);
    modal_ = nullptr;
  }
  if (tabview_ != nullptr) lv_obj_remove_flag(tabview_, LV_OBJ_FLAG_HIDDEN);  // reveal the UI
  wifi_setup_shown_ = false;
}

// Spinner shown while the pairing-mode token read runs (gives "it's working"
// feedback). Replaced by success (Home) or the token-choice modal on failure.
void App::show_pairing_modal() {
  lv_obj_t* card = open_modal("Pairing", "Reading the token from the machine...");
  lv_obj_t* spinner = lv_spinner_create(card);
  lv_obj_set_size(spinner, 44, 44);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(ui::theme::rail()), LV_PART_MAIN);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(ui::theme::accent()), LV_PART_INDICATOR);
  lv_spinner_set_anim_params(spinner, 1000, 60);
  modal_button(card, "Cancel", ui::theme::rail(), on_pairing_cancel, this);
}

// Token-setup choice: try pairing mode again, or fall back to WiFi entry.
// fetch_failed prepends a note when we got here after a failed pairing-mode read.
void App::show_token_modal(bool fetch_failed) {
  char body[200];
  const char* base =
      "Put the machine in pairing mode and Retry, or enter the token over WiFi.";
  if (fetch_failed) {
    std::snprintf(body, sizeof(body), "Unable to fetch token from the machine.\n\n%s",
                  base);
  } else {
    std::snprintf(body, sizeof(body), "%s", base);
  }
  lv_obj_t* card = open_modal("Set up token", body);
  lv_obj_t* row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  modal_button(row, "Retry", ui::theme::accent(), on_token_retry, this);
  modal_button(row, "WiFi", ui::theme::rail(), on_token_wifi, this);
  modal_button(row, "Cancel", ui::theme::rail(), on_token_cancel, this);
}

void App::show_wifi_modal() {
  char body[160];
  std::snprintf(body, sizeof(body), "Join WiFi: %s\nOpen: %s\nthen paste your token",
                provisioner_->setup_ssid(), provisioner_->setup_url());
  lv_obj_t* card = open_modal("Enter token over WiFi", body);
  modal_button(card, "Cancel", ui::theme::rail(), on_wifi_cancel, this);
  wifi_setup_shown_ = true;
}

void App::open_token_setup() { show_token_modal(/*fetch_failed=*/false); }

void App::retry_pairing() {
  if (provisioner_ != nullptr) provisioner_->retry_pairing();
  pairing_active_ = true;
  show_pairing_modal();  // (open_modal closes the previous one)
}

void App::cancel_pairing() {
  pairing_active_ = false;  // ignore the in-flight outcome
  close_modal();
}

void App::start_token_setup() {
  if (provisioner_ == nullptr) return;
  close_modal();
  provisioner_->start_token_setup();
  show_wifi_modal();
}

void App::cancel_token_setup() {
  if (provisioner_ != nullptr) provisioner_->stop_token_setup();
  close_modal();
}

void App::dismiss_modal() { close_modal(); }

void App::handle_pairing(core::Link link) {
  if (!pairing_active_) return;
  if (link == core::Link::Connected) {
    pairing_active_ = false;
    close_modal();
    if (tabview_ != nullptr) lv_tabview_set_active(tabview_, 0, LV_ANIM_ON);  // Home
  } else if (link == core::Link::NeedsToken) {
    pairing_active_ = false;
    show_token_modal(/*fetch_failed=*/true);  // pairing read failed; let the user choose
  }
}

void App::select_settings_section(int section) {
  commit_temp_edits();  // write pending edits from the section we're leaving
  settings_select_section(settings_, section);
  if (section == kSectionDevice) seed_time_steppers();  // show the current time
}

void App::select_stats_section(int section) {
  stats_select_section(stats_, section);
  update_stats_view();
}

void App::zoom_step(int dir) {
  int i = stats_.zoom_idx + dir;
  if (i < 0) i = 0;
  if (i >= kZoomCount) i = kZoomCount - 1;  // clamp (no wrap)
  stats_.zoom_idx = i;
  update_stats_view();
}

void App::set_low_battery_handler(float cutoff_volts, std::function<void()> on_critical) {
  batt_cutoff_volts_ = cutoff_volts;
  batt_low_handler_ = std::move(on_critical);
}

void App::update_battery_runtime(const core::BatteryState& b) {
  const uint32_t now = lv_tick_get();

  // --- Ring maintenance (every refresh, to catch the minute boundary) ---
  if (!b.present || b.charging) {
    batt_hist_count_ = 0;  // charging / USB / no battery -> no window
    batt_hist_head_ = 0;
    batt_last_sample_ms_ = 0;
  } else if (batt_hist_count_ == 0 || now - batt_last_sample_ms_ >= 60000u) {
    batt_hist_[batt_hist_head_] = {now, b.percent};  // one sample / minute
    batt_hist_head_ = (batt_hist_head_ + 1) % kBattHist;
    if (batt_hist_count_ < kBattHist) ++batt_hist_count_;
    batt_last_sample_ms_ = now;
  }

  // --- Recompute the displayed estimate at most every 5 s (no need to churn it
  //     every 500 ms; the rate comes from ring samples that change once a min). ---
  if (batt_runtime_calc_ms_ != 0 && now - batt_runtime_calc_ms_ < 5000u) return;
  batt_runtime_calc_ms_ = now;

  char* rt = batt_runtime_text_;
  const size_t n = sizeof(batt_runtime_text_);
  if (!b.present) {
    std::snprintf(rt, n, "-");
  } else if (b.charging) {
    std::snprintf(rt, n, "Charging");
  } else if (batt_hist_count_ < 2) {
    std::snprintf(rt, n, "Estimating...");
  } else {
    // Rate from ring samples only (oldest vs newest) — a ~10-min average.
    const int oldest = (batt_hist_head_ - batt_hist_count_ + 2 * kBattHist) % kBattHist;
    const int newest = (batt_hist_head_ - 1 + kBattHist) % kBattHist;
    const uint32_t span = batt_hist_[newest].t_ms - batt_hist_[oldest].t_ms;
    const int drop = batt_hist_[oldest].pct - batt_hist_[newest].pct;
    const int cur = batt_hist_[newest].pct;
    if (span < 3u * 60 * 1000) {
      std::snprintf(rt, n, "Estimating...");
    } else if (drop <= 0) {
      std::snprintf(rt, n, ">24h");  // not dropping -> effectively infinite
    } else {
      const uint64_t rem_min =
          static_cast<uint64_t>(cur) * span / (static_cast<uint32_t>(drop) * 60000u);
      if (rem_min >= 24 * 60)
        std::snprintf(rt, n, ">24h");
      else if (rem_min >= 60)
        std::snprintf(rt, n, "%uh %um", static_cast<unsigned>(rem_min / 60),
                      static_cast<unsigned>(rem_min % 60));
      else
        std::snprintf(rt, n, "%um", static_cast<unsigned>(rem_min));
    }
  }
}

void App::update_stats_view() {
  if (history_ == nullptr || stats_.chart == nullptr) return;

  const core::MachineSnapshot snap =
      (machine_ != nullptr) ? machine_->snapshot() : core::MachineSnapshot{};
  const bool connected = snap.link == core::Link::Connected;

  if (stats_.active == kStatsInfo) {
    // Row 0 is THIS remote's firmware; the rest are the machine's Device
    // Information Service fields (read on connect; "-" until populated).
    char rfw[40];
    if (fw::kGitRev[0] != '\0')
      std::snprintf(rfw, sizeof(rfw), "v%s (%s)", fw::kVersion, fw::kGitRev);
    else
      std::snprintf(rfw, sizeof(rfw), "v%s", fw::kVersion);

    // Battery runtime is computed (throttled, ~10-min average) in
    // update_battery_runtime; just display the cached string here.
    const char* vals[kStatsInfoRows] = {rfw,
                                        batt_runtime_text_,
                                        snap.manufacturer,
                                        snap.model,
                                        snap.serial,
                                        snap.firmware,
                                        snap.software};
    for (int i = 0; i < kStatsInfoRows; ++i) {
      if (stats_.info_val[i] == nullptr) continue;
      const bool have = vals[i] != nullptr && vals[i][0] != '\0';
      lv_label_set_text(stats_.info_val[i], have ? vals[i] : "-");
    }
    return;
  }

  const ZoomLevel& z = kZooms[stats_.zoom_idx];
  stats_.window_s = z.window_s;
  // Grey the zoom buttons at the ends of the window range (+ = shorter window).
  set_clickable(stats_.zoom_in, stats_.zoom_idx > 0);
  set_clickable(stats_.zoom_out, stats_.zoom_idx < kZoomCount - 1);
  // Tight ranges (start near ambient, not 0) so the curve fills the plot and the
  // 5 drawn Y labels land on round numbers: brew 20/40/60/80/100, steam
  // 20/50/80/110/140.
  stats_.y_min = 20;
  stats_.y_max = (stats_.active == kStatsBoiler) ? 140 : 100;
  stats_.fahrenheit = display_->use_fahrenheit();  // chart stays C; labels convert
  lv_chart_set_range(stats_.chart, LV_CHART_AXIS_PRIMARY_Y, stats_.y_min, stats_.y_max);

  // Set-point reference line: the current target for this boiler (none when
  // disconnected, or for the steam boiler while steam is off).
  if (!connected || (stats_.active == kStatsBoiler && !snap.steam_enabled)) {
    stats_.target = NAN;
  } else {
    stats_.target = (stats_.active == kStatsBoiler) ? snap.boiler_target_c : snap.brew_target_c;
  }

  // Size the chart to ~one bucket per 2 sample intervals so a bucket reliably
  // holds a sample — no sparsity gaps (hence no interpolation, which warped the
  // line as samples shifted buckets). Empty buckets are then real disconnects.
  const uint32_t interval = history_->sample_interval_s();
  int n = (interval > 0) ? static_cast<int>(z.window_s / (interval * 2)) : kStatsPoints;
  if (n < 8) n = 8;
  if (n > kStatsPoints) n = kStatsPoints;
  if (static_cast<int>(lv_chart_get_point_count(stats_.chart)) != n)
    lv_chart_set_point_count(stats_.chart, n);

  float brew[kStatsPoints];
  float boiler[kStatsPoints];
  history_->series(z.window_s, brew, boiler, n);
  const float* data = (stats_.active == kStatsBoiler) ? boiler : brew;

  bool any = false;
  for (int i = 0; i < n; ++i) {
    if (std::isnan(data[i])) {
      lv_chart_set_value_by_id(stats_.chart, stats_.series, i, LV_CHART_POINT_NONE);
    } else {
      lv_chart_set_value_by_id(stats_.chart, stats_.series, i,
                               static_cast<int32_t>(std::lroundf(data[i])));
      any = true;
    }
  }
  if (stats_.empty_label != nullptr) {
    if (any) lv_obj_add_flag(stats_.empty_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(stats_.empty_label, LV_OBJ_FLAG_HIDDEN);
  }
  lv_chart_refresh(stats_.chart);
  lv_obj_invalidate(stats_.chart);  // redraw the overlay (axes, set-point, gaps)
}

void App::update_settings_view() {
  update_scale_view();  // refresh the Scale page (independent change-detection)
  if (provisioner_ == nullptr) return;

  // Saved-machine row: name + Forget, plus one of: Setup (no token yet) or
  // Connect/Disconnect (tokened) in the same slot.
  const std::string saved = provisioner_->saved_name();
  if (saved.empty()) {
    lv_obj_add_flag(settings_.saved_row, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(settings_.saved_row, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(settings_.saved_label, saved.c_str());
    const bool tokened = provisioner_->has_token();
    if (tokened) {
      lv_obj_add_flag(settings_.setup_btn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(settings_.connect_btn, LV_OBJ_FLAG_HIDDEN);
      const bool on = provisioner_->connect_enabled();
      lv_label_set_text(settings_.connect_label, on ? "Disconnect" : "Connect");
      lv_obj_set_style_bg_color(
          settings_.connect_btn,
          lv_color_hex(on ? ui::theme::rail() : ui::theme::accent()), 0);
    } else {
      lv_obj_remove_flag(settings_.setup_btn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(settings_.connect_btn, LV_OBJ_FLAG_HIDDEN);
    }
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
    lv_obj_t* row = ui::make_button(settings_.list);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_style_bg_color(row, lv_color_hex(ui::theme::card()), 0);
    lv_obj_add_event_cb(row, on_result_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* lbl = lv_label_create(row);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s   %d dBm", results[i].name, results[i].rssi);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ui::theme::text()), 0);
    lv_obj_center(lbl);
  }
}

// --- Scale settings (Scale page: connection + target weight) ----------------

namespace {
void set_target_label(ui::SettingsWidgets& s) {
  if (s.target_value == nullptr) return;
  char b[16];
  std::snprintf(b, sizeof(b), "%.0f g", static_cast<double>(s.target_g));
  lv_label_set_text(s.target_value, b);
}
}  // namespace

void App::update_scale_view() {
  if (scale_provisioner_ == nullptr) return;

  // Saved-scale row: name + Connect/Disconnect + Forget (no token for scales).
  const std::string saved = scale_provisioner_->saved_name();
  if (saved.empty()) {
    lv_obj_add_flag(settings_.scale_saved_row, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(settings_.scale_saved_row, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(settings_.scale_saved_label, saved.c_str());
    lv_obj_remove_flag(settings_.scale_connect_btn, LV_OBJ_FLAG_HIDDEN);
    const bool on = scale_provisioner_->connect_enabled();
    lv_label_set_text(settings_.scale_connect_label, on ? "Disconnect" : "Connect");
    lv_obj_set_style_bg_color(
        settings_.scale_connect_btn,
        lv_color_hex(on ? ui::theme::rail() : ui::theme::accent()), 0);
  }

  const bool scanning = scale_provisioner_->scanning();
  const std::vector<core::ScanResult> results = scale_provisioner_->scan_results();
  const int count = static_cast<int>(results.size());

  if (scanning) {
    lv_label_set_text(settings_.scale_status, "Scanning...");
  } else if (count == 0) {
    lv_label_set_text(settings_.scale_status, "Tap Scan to find your scale");
  } else {
    lv_label_set_text(settings_.scale_status, "Tap a scale to save it");
  }

  if (count == settings_.scale_last_count && scanning == settings_.scale_last_scanning)
    return;
  settings_.scale_last_count = count;
  settings_.scale_last_scanning = scanning;

  lv_obj_clean(settings_.scale_list);
  for (int i = 0; i < count; ++i) {
    lv_obj_t* row = ui::make_button(settings_.scale_list);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_style_bg_color(row, lv_color_hex(ui::theme::card()), 0);
    lv_obj_add_event_cb(row, on_scale_result_clicked, LV_EVENT_CLICKED, this);

    lv_obj_t* lbl = lv_label_create(row);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s   %d dBm", results[i].name, results[i].rssi);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, lv_color_hex(ui::theme::text()), 0);
    lv_obj_center(lbl);
  }
  set_target_label(settings_);
}

void App::start_scale_scan() {
  if (scale_provisioner_ == nullptr) return;
  scale_provisioner_->start_scan();
  settings_.scale_last_count = -1;  // force the list to rebuild next refresh
}

void App::save_scanned_scale(int index) {
  if (scale_provisioner_ == nullptr) return;
  const std::vector<core::ScanResult> results = scale_provisioner_->scan_results();
  if (index < 0 || index >= static_cast<int>(results.size())) return;
  scale_provisioner_->save_scale(results[index]);
  settings_.scale_last_count = -1;
  request_layout_rebuild(kSectionScaleBt);  // Home swaps to the scale-aware layout
}

void App::forget_scale() {
  if (scale_provisioner_ == nullptr) return;
  scale_provisioner_->forget();
  settings_.scale_last_count = -1;
  request_layout_rebuild(kSectionScaleBt);  // Home reverts to the classic layout
}

void App::toggle_scale_connection() {
  if (scale_provisioner_ == nullptr) return;
  scale_provisioner_->set_connect_enabled(!scale_provisioner_->connect_enabled());
  update_scale_view();
}

void App::target_adjust(int dir) {
  settings_.target_g = clampf(settings_.target_g + dir, 5.0f, 120.0f);
  set_target_label(settings_);
  if (brew_ != nullptr) brew_->set_target_weight_g(settings_.target_g);
}

}  // namespace ui
