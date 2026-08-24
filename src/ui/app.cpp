#include "ui/app.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <lvgl.h>

#include "core/log_ring.h"
#include "ui/shot_card.h"
#include "ui/stats_tab.h"
#include "ui/theme.h"
#include "ui/timezones.h"
#include "ui/units.h"
#include "ui/widgets.h"
#include "version.h"

namespace {

void style_tab_button(lv_obj_t* btn, const lv_font_t* font) {
  lv_obj_set_style_text_font(btn, font, 0);
  lv_obj_set_style_text_color(btn, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(btn, ui::dp(10), 0);
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

void on_shot_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->shot_button();
}

void on_restart_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->restart_device();
}

void on_clean_lock_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->start_clean_lock();
}

void on_clean_lock_timer(lv_timer_t* t) {
  static_cast<ui::App*>(lv_timer_get_user_data(t))->clean_lock_tick();
}

void on_flush_clicked_home(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->toggle_manual_flush();
}

void on_backflush_open(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->open_backflush();
}
void on_backflush_go(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->backflush_go();
}
void on_backflush_cancel(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->backflush_cancel();
}
void on_backflush_back(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->close_backflush();
}
void on_backflush_timer(lv_timer_t* t) {
  static_cast<ui::App*>(lv_timer_get_user_data(t))->backflush_tick();
}

void on_flow_unit_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->toggle_flow_units();
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
void on_token_wifi(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->start_token_setup();
}
void on_token_cancel(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->dismiss_modal();
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
  lv_obj_set_style_text_font(l, ui::font_dp(14), 0);
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
void on_scale_dev_setting0(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cycle_scale_device_setting(0);
}
void on_scale_dev_setting1(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cycle_scale_device_setting(1);
}
void on_scale_dev_setting2(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cycle_scale_device_setting(2);
}
void on_scale_dev_setting3(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cycle_scale_device_setting(3);
}
void on_target_minus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->target_adjust(-1);
}
void on_review_minus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->review_hold_adjust(-1);
}
void on_review_plus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->review_hold_adjust(+1);
}
void on_lead_minus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->detect_lead_in_adjust(-1);
}
void on_lead_plus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->detect_lead_in_adjust(+1);
}
void on_toast_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->dismiss_toast();
}
void on_toast_timeout(lv_timer_t* t) {
  static_cast<ui::App*>(lv_timer_get_user_data(t))->dismiss_toast();
}
void on_target_plus(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->target_adjust(+1);
}

void on_history_row(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* row = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  const uint32_t id =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(row)));
  app->open_shot_card(id);
}

void on_history_filter(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  // Button user data = year*100 + month (0 = All).
  app->set_history_filter(
      static_cast<int>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(btn))));
}

// year*100 + month (1-12) of a shot's end time, in LOCAL time — the month
// bucket the user saw the shot in.
int shot_month_key(int64_t unix_time) {
  const time_t t = static_cast<time_t>(unix_time);
  struct tm tm;
  localtime_r(&t, &tm);
  return (tm.tm_year + 1900) * 100 + tm.tm_mon + 1;
}

constexpr const char* kMonthNames[12] = {"Jan", "Feb", "Mar", "Apr",
                                         "May", "Jun", "Jul", "Aug",
                                         "Sep", "Oct", "Nov", "Dec"};

void on_shot_modal_close(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->dismiss_modal();
}

void on_hist_metric_card(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->open_reset_stats_modal();
}

void on_info_log_row(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->open_log_modal();
}

void on_log_modal_close(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->dismiss_modal();
}

void on_shot_delete(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->open_delete_shot_modal();
}

void on_delete_shot_confirm(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->confirm_delete_shot();
}

void on_delete_shot_cancel(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->dismiss_modal();
}

void on_reset_stats_confirm(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->confirm_reset_stats();
}

void on_reset_stats_cancel(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->dismiss_modal();
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

// (Re)fill the hour dropdown's options for the active clock format. Option
// index == hour 0-23 in BOTH formats ("12 AM" sits at index 0), so the
// selection code never maps between them.
void set_hour_dd_options(ui::SettingsWidgets& s) {
  if (s.hour_dd == nullptr) return;
  std::string opts;
  char b[8];
  for (int h = 0; h < 24; ++h) {
    if (s.clock_24h) {
      std::snprintf(b, sizeof(b), "%02d", h);
    } else {
      const int h12 = (h % 12 == 0) ? 12 : h % 12;
      std::snprintf(b, sizeof(b), "%d %s", h12, h < 12 ? "AM" : "PM");
    }
    if (h) opts += '\n';
    opts += b;
  }
  lv_dropdown_set_options(s.hour_dd, opts.c_str());
}

// Day options track the selected month's length (index = day - 1).
void set_day_dd_options(ui::SettingsWidgets& s, int days) {
  if (s.day_dd == nullptr) return;
  std::string opts;
  char b[4];
  for (int d = 1; d <= days; ++d) {
    std::snprintf(b, sizeof(b), "%d", d);
    if (d > 1) opts += '\n';
    opts += b;
  }
  lv_dropdown_set_options(s.day_dd, opts.c_str());
}

// Static option sets: minutes 00-59, month names, years 2024-2099.
void set_static_time_dd_options(ui::SettingsWidgets& s) {
  if (s.minute_dd == nullptr) return;
  std::string opts;
  char b[8];
  for (int m = 0; m < 60; ++m) {
    std::snprintf(b, sizeof(b), "%02d", m);
    if (m) opts += '\n';
    opts += b;
  }
  lv_dropdown_set_options(s.minute_dd, opts.c_str());
  opts.clear();
  for (int m = 0; m < 12; ++m) {
    if (m) opts += '\n';
    opts += kMonthNames[m];
  }
  lv_dropdown_set_options(s.month_dd, opts.c_str());
  opts.clear();
  for (int y = core::kClockBaseYear; y <= 2099; ++y) {
    std::snprintf(b, sizeof(b), "%d", y);
    if (y > core::kClockBaseYear) opts += '\n';
    opts += b;
  }
  lv_dropdown_set_options(s.year_dd, opts.c_str());
}

int days_in_month(int year, int month) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leap ? 29 : 28;
  }
  return kDays[month - 1];
}

// Time/date dropdowns: the selected index maps straight to the field value
// (hour index == hour, minute index == minute, month/day are 1-based, year is
// offset from kClockBaseYear).
int dd_selected(lv_event_t* e) {
  auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
  return static_cast<int>(lv_dropdown_get_selected(dd));
}
void on_hour_dd(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->hour_select(dd_selected(e));
}
void on_minute_dd(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->minute_select(dd_selected(e));
}
void on_month_dd(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->month_select(dd_selected(e));
}
void on_day_dd(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->day_select(dd_selected(e));
}
void on_year_dd(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->year_select(dd_selected(e));
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
void on_drop_neg_flow_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->set_drop_negative_flow(lv_obj_has_state(sw, LV_STATE_CHECKED));
}
void on_scope_graph_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->set_scope_graph(lv_obj_has_state(sw, LV_STATE_CHECKED));
}
void on_perf_overlay_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->set_perf_overlay(lv_obj_has_state(sw, LV_STATE_CHECKED));
}
void on_click_sound_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->set_click_sound(lv_obj_has_state(sw, LV_STATE_CHECKED));
}
void on_chime_vol_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cycle_ready_chime();
}
void on_chime_mel_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cycle_ready_melody();
}
void on_wifi_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->set_wifi_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}
void on_auto_connect_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->set_auto_connect(lv_obj_has_state(sw, LV_STATE_CHECKED));
}
void on_wired_paddle_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->set_wired_paddle(lv_obj_has_state(sw, LV_STATE_CHECKED));
}
void on_smooth_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cycle_flow_smooth();
}
void on_flush_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cycle_flush();
}
void on_flush_delay_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cycle_flush_delay();
}

// Auto-flush run-time choices (seconds; 0 = off). Also the pulse length for
// the Home Flush button and the backflush cycles, so the range runs long
// enough to outlast a machine's preinfusion.
constexpr int kFlushChoices[] = {0, 3, 6, 9, 15};
constexpr int kFlushCount = static_cast<int>(sizeof(kFlushChoices) / sizeof(kFlushChoices[0]));
// Cup-off -> flush pause choices (seconds).
constexpr int kFlushDelayChoices[] = {3, 6, 9, 15};
constexpr int kFlushDelayCount =
    static_cast<int>(sizeof(kFlushDelayChoices) / sizeof(kFlushDelayChoices[0]));
// Ready-chime levels (percent; 0 = off). Linear amplitude, so the label means
// what it says.
constexpr int kChimeVolChoices[] = {0, 25, 50, 75, 100};
constexpr int kChimeVolCount =
    static_cast<int>(sizeof(kChimeVolChoices) / sizeof(kChimeVolChoices[0]));

void set_chime_vol_label(ui::SettingsWidgets& s, int percent) {
  if (s.chime_vol_value == nullptr) return;
  if (percent <= 0) {
    lv_label_set_text(s.chime_vol_value, "Off");
  } else {
    char b[8];
    std::snprintf(b, sizeof(b), "%d%%", percent);
    lv_label_set_text(s.chime_vol_value, b);
  }
}

void set_chime_mel_label(ui::SettingsWidgets& s, int melody) {
  if (s.chime_mel_value == nullptr) return;
  lv_label_set_text(s.chime_mel_value,
                    melody <= 0                            ? "Off"
                    : melody == core::kReadyMelodyRandom   ? "Random"
                    : core::ready_melody_name(melody - 1));
}

// The variant a melody setting plays RIGHT NOW: Random rolls fresh each call
// (that's the point — a different tune per chime), everything else maps 1..N
// to its table. Callers guarantee melody > 0.
int melody_variant(int melody) {
  if (melody == core::kReadyMelodyRandom)
    return static_cast<int>(lv_rand(0, static_cast<uint32_t>(core::ready_melody_count()) - 1));
  return melody - 1;
}

// The delay row only matters (and only shows) while the flush itself is on.
void set_flush_label(ui::SettingsWidgets& s, int flush_s) {
  if (s.flush_value == nullptr) return;
  if (flush_s <= 0) {
    lv_label_set_text(s.flush_value, "Off");
  } else {
    char b[12];
    std::snprintf(b, sizeof(b), "%d s", flush_s);
    lv_label_set_text(s.flush_value, b);
  }
  if (s.flush_delay_row != nullptr) {
    if (flush_s > 0) {
      lv_obj_remove_flag(s.flush_delay_row, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s.flush_delay_row, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void set_flush_delay_label(ui::SettingsWidgets& s, int delay_s) {
  if (s.flush_delay_value == nullptr) return;
  char b[12];
  std::snprintf(b, sizeof(b), "%d s", delay_s);
  lv_label_set_text(s.flush_delay_value, b);
}

// Graph smoothing levels (IDisplaySettings::flow_smooth 0..3). The kernels
// themselves live with the painters in home_tab.cpp (kSmoothLevels).
constexpr const char* kSmoothName[] = {"Off", "Light", "Medium", "Strong"};

// Screen-dim timeout choices (IDisplaySettings::screen_timeout_min).
constexpr int kDimMinutes[] = {0, 15, 30};
constexpr int kDimCount = static_cast<int>(sizeof(kDimMinutes) / sizeof(kDimMinutes[0]));
void on_dim_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cycle_screen_timeout();
}

void set_dim_label(ui::SettingsWidgets& s) {
  if (s.dim_value == nullptr) return;
  if (s.screen_timeout_min <= 0) {
    lv_label_set_text(s.dim_value, "Off");
  } else {
    char b[12];
    std::snprintf(b, sizeof(b), "%d min", s.screen_timeout_min);
    lv_label_set_text(s.dim_value, b);
  }
}

void on_screensaver_timer(lv_timer_t* t) {
  static_cast<ui::App*>(lv_timer_get_user_data(t))->screensaver_tick();
}
void on_wifi_setup_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->start_wifi_setup();
}
void on_wifi_forget_clicked(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->forget_wifi();
}
void on_tz_dropdown(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->timezone_select(lv_dropdown_get_selected(dd));
}
void on_ntp_switch(lv_event_t* e) {
  auto* app = static_cast<ui::App*>(lv_event_get_user_data(e));
  auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  app->set_ntp_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}
void on_wifi_setup_cancel(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->cancel_wifi_setup();
}
// Show/hide LVGL's performance monitor (FPS/CPU) on the default display. Compiled
// out when the sysmon isn't built, so the toggle just persists a no-op preference.
void apply_perf_overlay(bool on) {
#if LV_USE_PERF_MONITOR
  if (on) lv_sysmon_show_performance(nullptr);
  else lv_sysmon_hide_performance(nullptr);
#else
  (void)on;
#endif
}
// lv_menu fires VALUE_CHANGED on every page navigation; re-seed the clock steppers
// when the Device page comes into view so Hour/Minute show the current time.
void on_menu_page_changed(lv_event_t* e) {
  static_cast<ui::App*>(lv_event_get_user_data(e))->on_settings_page_shown();
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
  if (toast_timer_ != nullptr) lv_timer_delete(toast_timer_);
  if (screensaver_timer_ != nullptr) lv_timer_delete(screensaver_timer_);
  if (clean_lock_timer_ != nullptr) lv_timer_delete(clean_lock_timer_);
  if (bf_timer_ != nullptr) lv_timer_delete(bf_timer_);
  if (home_.shot_flash_timer != nullptr) lv_timer_delete(home_.shot_flash_timer);
  if (home_.stop_flash_timer != nullptr) lv_timer_delete(home_.stop_flash_timer);
  if (home_.heat_pulse_timer != nullptr) lv_timer_delete(home_.heat_pulse_timer);
  if (home_.flow_buf != nullptr) lv_free(home_.flow_buf);
  if (home_.flow_weights != nullptr) lv_free(home_.flow_weights);
  if (home_.flow_flows != nullptr) lv_free(home_.flow_flows);
}

void App::build(core::IMachine& machine, core::IProvisioner& provisioner,
                core::IBattery& battery, core::IDisplaySettings& display,
                core::IClock& clock, core::IHistory& history, core::IScale& scale,
                core::IScaleProvisioner& scale_provisioner, core::IBrewController& brew,
                core::INetwork& network, core::ISound& sound, core::IShotStore& shots,
                const ScreenProfile& screen) {
  machine_ = &machine;
  shots_ = &shots;
  hist_built_count_ = -1;  // a rebuild recreates the list; force a row refill
  // Shot-record staging lives in the LVGL pool (PSRAM on device) — see the
  // member comment. Allocated once; survives rebuilds.
  if (shot_view_ == nullptr)
    shot_view_ = static_cast<core::ShotRecord*>(lv_malloc(sizeof(core::ShotRecord)));
  if (shot_capture_ == nullptr)
    shot_capture_ = static_cast<core::ShotRecord*>(lv_malloc(sizeof(core::ShotRecord)));
  provisioner_ = &provisioner;
  battery_ = &battery;
  display_ = &display;
  clock_ = &clock;
  history_ = &history;
  scale_ = &scale;
  scale_provisioner_ = &scale_provisioner;
  brew_ = &brew;
  network_ = &network;
  sound_ = &sound;
  screen_ = screen;
  ui::set_scale(screen.scale);  // before any widget: ui::dp/font_dp read this
  const bool compact = is_compact(screen);
  const bool xl = is_xl(screen);

  ui::theme::set_active(display_->theme());  // pick the palette before any widget is colored

  // Button-press click on audio boards. The hook is read at event time and
  // checks the cached setting, so the switch takes effect immediately.
  click_sound_on_ = display_->click_sound();
  ready_chime_vol_ = display_->ready_chime_volume();
  ready_chime_mel_ = display_->ready_chime_melody();
  if (sound_->available()) {
    ui::set_button_press_hook([this] {
      if (click_sound_on_ && sound_ != nullptr) sound_->play(core::Cue::ButtonPress);
    });
  } else {
    ui::set_button_press_hook(nullptr);
  }

  // A rebuild (theme change) recreates everything; drop the old flash timer and
  // flow-graph canvas buffer first so we don't leak them across rebuilds
  // (lv_obj_clean deletes the widgets, but these are heap allocations we own).
  // The toast lives on lv_layer_top (never cleaned by build) and captured the
  // old theme's colors — kill it rather than carry it across.
  dismiss_toast();
  if (home_.shot_flash_timer != nullptr) {
    lv_timer_delete(home_.shot_flash_timer);
    home_.shot_flash_timer = nullptr;
    home_.shot_flash_count = 0;
  }
  if (home_.stop_flash_timer != nullptr) {
    lv_timer_delete(home_.stop_flash_timer);
    home_.stop_flash_timer = nullptr;
    home_.stop_flash_count = 0;
  }
  if (home_.heat_pulse_timer != nullptr) {
    lv_timer_delete(home_.heat_pulse_timer);
    home_.heat_pulse_timer = nullptr;
    // heating_ keeps its value: update_heating re-arms the pulse on the first
    // refresh after the rebuild.
  }
  if (home_.flow_buf != nullptr) {
    lv_free(home_.flow_buf);
    home_.flow_buf = nullptr;
  }
  if (home_.flow_weights != nullptr) {
    lv_free(home_.flow_weights);
    home_.flow_weights = nullptr;
  }
  if (home_.flow_flows != nullptr) {
    lv_free(home_.flow_flows);
    home_.flow_flows = nullptr;
  }

  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(ui::theme::bg()), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  const lv_font_t* tab_font = ui::font_dp(28);  // bigger icons on every tier
  const int rail_pad = ui::dp(compact ? 4 : xl ? 12 : 8);

  lv_obj_t* tv = lv_tabview_create(scr);
  tabview_ = tv;
  lv_obj_add_event_cb(tv, on_tab_changed, LV_EVENT_VALUE_CHANGED, this);  // commit on tab exit
  lv_tabview_set_tab_bar_position(tv, compact ? LV_DIR_BOTTOM : LV_DIR_LEFT);
  lv_tabview_set_tab_bar_size(tv, ui::dp(compact ? 44 : xl ? 120 : 96));
  lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tv, 0, 0);

  lv_obj_t* content = lv_tabview_get_content(tv);
  lv_obj_set_style_bg_color(content, lv_color_hex(ui::theme::bg()), 0);
  lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
  // Don't switch tabs on a content swipe: it fights with scrolling a long Settings
  // page (a vertical drag would flip tabs instead of scrolling). Tabs change via the
  // rail buttons only. (Removing SCROLLABLE from the tabview content disables the
  // swipe gesture; the pages inside each tab keep their own scrolling.)
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

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
    lv_obj_t* tb = lv_tabview_get_tab_button(tv, i);
    style_tab_button(tb, tab_font);
    // Tab buttons are the tabview's own (not ui::make_button), so give them the
    // same press feedback by hand.
    lv_obj_add_event_cb(
        tb, [](lv_event_t*) { ui::play_button_press(); }, LV_EVENT_PRESSED, nullptr);
    // Compact: the tab bar is horizontal and shares the row with the clock/battery
    // tray. Grow the buttons so they fill the bar (big touch targets) instead of
    // sitting small with dead space; the non-grow tray labels stay at the right.
    if (compact) lv_obj_set_flex_grow(tb, 1);
  }

  const bool scale_on =
      scale_provisioner_ != nullptr && !scale_provisioner_->saved_name().empty();
  build_home_tab(home, screen, scale_on, home_);
  // Every layout except large-no-scale drops the top bar and moves clock/battery to
  // a tray (visible on every tab). Compact -> a horizontal tray on the bottom tab
  // bar; large scale-aware -> the side rail (tabs spread via SPACE_BETWEEN so the
  // tray anchors the bottom, not a void). Must precede update_home, which populates
  // home_.clock_label / battery_label.
  if (compact) {
    ui::build_bottom_tray(rail, ui::font_dp(14), home_);
  } else {
    lv_obj_set_flex_align(rail, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    // Font 16 (not 20) so a 2-digit 12h clock ("12:55 PM") + "100%" battery fit the
    // narrow rail without clipping (font metrics differ on-device vs sim).
    ui::build_rail_tray(rail, ui::font_dp(16), home_);
  }
  lv_obj_add_event_cb(home_.power_btn, on_power_clicked, LV_EVENT_CLICKED, this);
  if (home_.tare_btn != nullptr)
    lv_obj_add_event_cb(home_.tare_btn, on_tare_clicked, LV_EVENT_CLICKED, this);
  if (home_.scale_connect_btn != nullptr)  // in-card connect toggle (sleep/wake)
    lv_obj_add_event_cb(home_.scale_connect_btn, on_scale_connect_clicked,
                        LV_EVENT_CLICKED, this);
  if (home_.flush_btn != nullptr)  // manual group flush (large layouts)
    lv_obj_add_event_cb(home_.flush_btn, on_flush_clicked_home, LV_EVENT_CLICKED, this);
  if (home_.shot_btn != nullptr)
    lv_obj_add_event_cb(home_.shot_btn, on_shot_clicked, LV_EVENT_CLICKED, this);
  if (home_.flow_unit_btn != nullptr)
    lv_obj_add_event_cb(home_.flow_unit_btn, on_flow_unit_clicked, LV_EVENT_CLICKED, this);
  // Inline set-point steppers (large screens): reuse the Settings handlers so Home
  // edits flow through the same brew_adjust/boiler_adjust + deferred-commit path.
  if (home_.brew_minus != nullptr) {
    lv_obj_add_event_cb(home_.brew_minus, on_brew_minus, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(home_.brew_plus, on_brew_plus, LV_EVENT_ALL, this);
    lv_obj_add_event_cb(home_.boiler_minus, on_boiler_minus, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(home_.boiler_plus, on_boiler_plus, LV_EVENT_CLICKED, this);
  }
  // Home weight-target steppers (scale panel) reuse the Settings target handlers.
  if (home_.target_minus != nullptr) {
    lv_obj_add_event_cb(home_.target_minus, on_target_minus, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(home_.target_plus, on_target_plus, LV_EVENT_CLICKED, this);
  }
  battery_state_ = battery_->battery();
  {
    const core::MachineSnapshot msnap = machine_->snapshot();
    update_heating(msnap);
    update_home(home_, msnap, battery_state_, clock_->now(),
                clock_->use_24h(), display_->use_fahrenheit(), scale_->snapshot(),
                scale_->features(), scale_connect_enabled(), brew_->snapshot(),
                net_status(), heating_);
  }
  update_battery_runtime(battery_state_);

  build_settings_tab(settings, screen, display_->supports_brightness(),
                     sound_->available(), brew_->snapshot().paddle_hw, settings_);
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
  // "On the scale" group: the connect prompt shares the link toggle; the value
  // buttons cycle their descriptor slot (one handler per fixed slot).
  if (settings_.scale_dev_connect_btn != nullptr)
    lv_obj_add_event_cb(settings_.scale_dev_connect_btn, on_scale_connect_clicked,
                        LV_EVENT_CLICKED, this);
  static constexpr lv_event_cb_t kDevSettingCbs[core::kMaxScaleSettings] = {
      on_scale_dev_setting0, on_scale_dev_setting1, on_scale_dev_setting2,
      on_scale_dev_setting3};
  for (int i = 0; i < core::kMaxScaleSettings; ++i) {
    if (settings_.scale_dev_btns[i] != nullptr)
      lv_obj_add_event_cb(settings_.scale_dev_btns[i], kDevSettingCbs[i],
                          LV_EVENT_CLICKED, this);
  }
  lv_obj_add_event_cb(settings_.target_minus, on_target_minus, LV_EVENT_CLICKED, this);
  if (settings_.review_minus != nullptr) {
    lv_obj_add_event_cb(settings_.review_minus, on_review_minus, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(settings_.review_plus, on_review_plus, LV_EVENT_CLICKED, this);
  }
  if (settings_.lead_minus != nullptr) {
    lv_obj_add_event_cb(settings_.lead_minus, on_lead_minus, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(settings_.lead_plus, on_lead_plus, LV_EVENT_CLICKED, this);
  }
  if (settings_.smooth_btn != nullptr) {
    const int level = display_ != nullptr ? display_->flow_smooth() : 1;
    if (settings_.smooth_value != nullptr)
      lv_label_set_text(settings_.smooth_value, kSmoothName[level & 3]);
    ui::set_shot_smoothing(home_, level & 3);
    lv_obj_add_event_cb(settings_.smooth_btn, on_smooth_clicked, LV_EVENT_CLICKED, this);
  }
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
  if (settings_.dim_btn != nullptr)
    lv_obj_add_event_cb(settings_.dim_btn, on_dim_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.hour_dd, on_hour_dd, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.minute_dd, on_minute_dd, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.month_dd, on_month_dd, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.day_dd, on_day_dd, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.year_dd, on_year_dd, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.clock_mode_switch, on_clock_mode_switch,
                      LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.units_switch, on_units_switch, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.drop_neg_flow_switch, on_drop_neg_flow_switch,
                      LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.scope_graph_switch, on_scope_graph_switch,
                      LV_EVENT_VALUE_CHANGED, this);
  if (settings_.restart_btn != nullptr)
    lv_obj_add_event_cb(settings_.restart_btn, on_restart_clicked, LV_EVENT_CLICKED, this);
  if (settings_.clean_lock_btn != nullptr)
    lv_obj_add_event_cb(settings_.clean_lock_btn, on_clean_lock_clicked, LV_EVENT_CLICKED,
                        this);
  if (settings_.auto_connect_switch != nullptr) {
    if (provisioner_ != nullptr && provisioner_->auto_connect())
      lv_obj_add_state(settings_.auto_connect_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(settings_.auto_connect_switch, on_auto_connect_switch,
                        LV_EVENT_VALUE_CHANGED, this);
  }
  if (settings_.wired_paddle_switch != nullptr) {  // paddle-capable boards only
    if (brew_ != nullptr && brew_->snapshot().wired_setting)
      lv_obj_add_state(settings_.wired_paddle_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(settings_.wired_paddle_switch, on_wired_paddle_switch,
                        LV_EVENT_VALUE_CHANGED, this);
  }
  if (settings_.flush_btn != nullptr) {  // paddle-capable boards only
    set_flush_label(settings_, brew_ != nullptr ? brew_->snapshot().flush_s : 0);
    lv_obj_add_event_cb(settings_.flush_btn, on_flush_clicked, LV_EVENT_CLICKED, this);
  }
  if (settings_.flush_delay_btn != nullptr) {
    set_flush_delay_label(settings_, brew_ != nullptr ? brew_->snapshot().flush_delay_s : 3);
    lv_obj_add_event_cb(settings_.flush_delay_btn, on_flush_delay_clicked,
                        LV_EVENT_CLICKED, this);
  }
  if (settings_.backflush_btn != nullptr)  // paddle-capable boards only
    lv_obj_add_event_cb(settings_.backflush_btn, on_backflush_open, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.perf_overlay_switch, on_perf_overlay_switch,
                      LV_EVENT_VALUE_CHANGED, this);
  if (settings_.click_sound_switch != nullptr) {
    if (click_sound_on_) lv_obj_add_state(settings_.click_sound_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(settings_.click_sound_switch, on_click_sound_switch,
                        LV_EVENT_VALUE_CHANGED, this);
  }
  if (settings_.chime_mel_btn != nullptr) {  // audio boards only
    set_chime_mel_label(settings_, ready_chime_mel_);
    lv_obj_add_event_cb(settings_.chime_mel_btn, on_chime_mel_clicked,
                        LV_EVENT_CLICKED, this);
  }
  if (settings_.chime_vol_btn != nullptr) {  // audio boards only
    set_chime_vol_label(settings_, ready_chime_vol_);
    lv_obj_add_event_cb(settings_.chime_vol_btn, on_chime_vol_clicked,
                        LV_EVENT_CLICKED, this);
  }
  if (settings_.theme_btn != nullptr)
    lv_obj_add_event_cb(settings_.theme_btn, on_theme_clicked, LV_EVENT_CLICKED, this);
  // WiFi (Device section):
  lv_obj_add_event_cb(settings_.wifi_switch, on_wifi_switch, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.wifi_setup_btn, on_wifi_setup_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.wifi_forget_btn, on_wifi_forget_clicked, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(settings_.tz_dropdown, on_tz_dropdown, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.ntp_switch, on_ntp_switch, LV_EVENT_VALUE_CHANGED, this);
  lv_obj_add_event_cb(settings_.menu, on_menu_page_changed, LV_EVENT_VALUE_CHANGED, this);

  build_stats_tab(stats, screen, stats_);
  for (int i = 0; i < kStatsCount; ++i) {
    lv_obj_add_event_cb(stats_.seg[i], on_stats_segment, LV_EVENT_CLICKED, this);
  }
  lv_obj_add_event_cb(stats_.zoom_in, on_zoom_in, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(stats_.zoom_out, on_zoom_out, LV_EVENT_CLICKED, this);
  // (History month-filter buttons are created dynamically in
  // update_history_view and wired there.)
  for (lv_obj_t* c : stats_.hist_metric_cards) {
    if (c != nullptr)
      lv_obj_add_event_cb(c, on_hist_metric_card, LV_EVENT_CLICKED, this);
  }
  if (stats_.info_log_btn != nullptr)
    lv_obj_add_event_cb(stats_.info_log_btn, on_info_log_row, LV_EVENT_CLICKED, this);

  if (brew_ != nullptr) {
    const core::BrewSnapshot b = brew_->snapshot();
    settings_.target_g = b.target_weight_g;
    settings_.review_hold_s = b.review_hold_s;
    settings_.detect_lead_in_s = b.detect_lead_in_s;
  }
  update_settings_view();
  update_temp_panels(machine_->snapshot());

  settings_.brightness = display_->brightness();
  set_brightness_label(settings_);
  settings_.screen_timeout_min = display_->screen_timeout_min();
  set_dim_label(settings_);
  // Idle-dim poll. Created once (build() reruns on theme rebuilds; the timer
  // isn't part of the widget tree, so it survives them).
  if (screensaver_timer_ == nullptr)
    screensaver_timer_ = lv_timer_create(on_screensaver_timer, 250, this);
  settings_.clock_24h = clock_->use_24h();
  if (settings_.clock_24h) lv_obj_add_state(settings_.clock_mode_switch, LV_STATE_CHECKED);
  if (display_->use_fahrenheit()) lv_obj_add_state(settings_.units_switch, LV_STATE_CHECKED);
  const bool drop_neg = display_->drop_negative_flow();
  if (drop_neg) lv_obj_add_state(settings_.drop_neg_flow_switch, LV_STATE_CHECKED);
  home_.flow_drop_negative = drop_neg;
  const bool scope = display_->scope_graph();
  if (scope) lv_obj_add_state(settings_.scope_graph_switch, LV_STATE_CHECKED);
  home_.flow_scope_mode = scope;
  const bool perf = display_->perf_overlay();
  if (perf) lv_obj_add_state(settings_.perf_overlay_switch, LV_STATE_CHECKED);
  apply_perf_overlay(perf);  // match the saved preference (LVGL auto-shows it at init)
  if (network_ != nullptr) {
    if (network_->enabled()) lv_obj_add_state(settings_.wifi_switch, LV_STATE_CHECKED);
    if (network_->ntp_enabled()) lv_obj_add_state(settings_.ntp_switch, LV_STATE_CHECKED);
    // Select the dropdown row whose POSIX string matches the saved timezone.
    const char* tz = network_->timezone();
    for (int i = 0; i < ui::kTimezoneCount; ++i) {
      if (std::strcmp(tz, ui::kTimezones[i].posix) == 0) {
        lv_dropdown_set_selected(settings_.tz_dropdown, i);
        break;
      }
    }
  }
  ui::apply_flow_xaxis_labels(home_);  // initial x-axis label set (no plot reset)
  seed_time_controls();

  settings_.theme_index = ui::theme::active_index();
  set_theme_label(settings_);
}

void App::update_heating(const core::MachineSnapshot& state) {
  heating_ = core::derive_heating(state, heating_);
  ui::set_heating_pulse(home_, heating_);  // idempotent; re-arms after rebuild
  // Fed on every refresh whether or not the chime is enabled, so the latch
  // always reflects the machine rather than the setting: enabling it on an
  // already-warm machine stays silent instead of firing a warm-up that
  // finished an hour ago.
  const bool ready = ready_chime_.update(state, heating_);
  if (ready && ready_chime_vol_ > 0 && ready_chime_mel_ > 0 && sound_ != nullptr)
    sound_->play(core::ready_melody_playback(melody_variant(ready_chime_mel_),
                                             ready_chime_vol_));
}

void App::refresh() {
  if (machine_ != nullptr) {
    const core::MachineSnapshot snap = machine_->snapshot();
    if (battery_ != nullptr) {
      battery_state_ = battery_->battery();
      update_heating(snap);
      update_home(home_, snap, battery_state_, clock_->now(), clock_->use_24h(),
                  display_->use_fahrenheit(), scale_->snapshot(), scale_->features(),
                  scale_connect_enabled(), brew_->snapshot(), net_status(), heating_);
      update_battery_runtime(battery_state_);

      // Critically-low pack (on battery, sustained) -> hand off to deep sleep.
      if (batt_low_handler_ && battery_state_.present &&
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

    // Machine seen in configuration/pairing mode (e.g. left there after setting up
    // a token): it can't be used until restarted. Nudge the user once per event
    // instead of silently looping on "Disconnected".
    if (snap.config_mode_seq != config_mode_seen_) {
      config_mode_seen_ = snap.config_mode_seq;
      show_toast("Machine is in pairing mode. Restart it (power off/on) to connect.");
    }

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

    // WiFi credential portal: once the station connects with the entered network,
    // close the instructions modal and return to Home. A failed attempt reopens
    // the AP (Network handles that), so the modal stays up for a retry.
    if (wifi_portal_shown_ && network_ != nullptr &&
        network_->status() == core::NetState::Connected) {
      wifi_portal_shown_ = false;
      close_modal();
      if (tabview_ != nullptr) lv_tabview_set_active(tabview_, 0, LV_ANIM_ON);  // Home
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
  // The large scale-aware panels sit the set point between [-]/[+], so they drop
  // the unit ("93.0°") to fit; the no-scale layout has room for "93.0 °C".
  const bool panel = home_.micra_status_label != nullptr;
  char b[16];
  if (!connected) {
    ui::set_text(home_.brew_set, "--");
  } else if (panel) {
    std::snprintf(b, sizeof(b), "%.1f°", ui::temp_disp(settings_.brew_target, f));
    ui::set_text(home_.brew_set, b);
  } else {
    std::snprintf(b, sizeof(b), "%.1f %s", ui::temp_disp(settings_.brew_target, f),
                  ui::temp_unit(f));
    ui::set_text(home_.brew_set, b);
  }
  if (!connected) {
    ui::set_text(home_.boiler_set, "--");
  } else if (!steam) {
    ui::set_text(home_.boiler_set, "Off");
  } else {
    std::snprintf(b, sizeof(b), panel ? "%.0f°" : "%.0f %s",
                  ui::temp_disp(core::kSteamLevelsC[settings_.boiler_level], f),
                  ui::temp_unit(f));
    ui::set_text(home_.boiler_set, b);
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

void App::seed_time_controls() {
  if (clock_ == nullptr || settings_.hour_dd == nullptr) return;
  const core::WallTime t = clock_->now();
  settings_.set_hour = t.valid ? t.hour : 12;
  settings_.set_minute = t.valid ? t.minute : 0;
  if (t.date_valid) {
    settings_.set_year = t.year;
    settings_.set_month = t.month;
    settings_.set_day = t.day;
  }  // else keep the SettingsWidgets seed defaults — a plausible starting point
  // Rebuild all option sets (hour labels follow 12/24h, day count the month;
  // the static ones are cheap) and select the current values. Programmatic
  // selection fires no VALUE_CHANGED, so nothing loops back into the clock.
  set_hour_dd_options(settings_);
  set_static_time_dd_options(settings_);
  set_day_dd_options(settings_,
                     days_in_month(settings_.set_year, settings_.set_month));
  lv_dropdown_set_selected(settings_.hour_dd, settings_.set_hour);
  lv_dropdown_set_selected(settings_.minute_dd, settings_.set_minute);
  lv_dropdown_set_selected(settings_.month_dd, settings_.set_month - 1);
  lv_dropdown_set_selected(settings_.day_dd, settings_.set_day - 1);
  lv_dropdown_set_selected(settings_.year_dd,
                           settings_.set_year - core::kClockBaseYear);
}

void App::on_settings_page_shown() {
  // Re-seed the Hour/Minute steppers from the live clock whenever the Time & date
  // page is opened, so they reflect the current time instead of the boot-time seed.
  if (settings_.menu != nullptr &&
      lv_menu_get_cur_main_page(settings_.menu) == settings_.device_time_page) {
    seed_time_controls();
  }
}

void App::hour_select(int idx) {
  settings_.set_hour = idx;  // option index == hour in both clock formats
  if (clock_ != nullptr) clock_->set(settings_.set_hour, settings_.set_minute);
}

void App::minute_select(int idx) {
  settings_.set_minute = idx;
  if (clock_ != nullptr) clock_->set(settings_.set_hour, settings_.set_minute);
}

// Any date selection writes the full date (like the time dropdowns). Day
// re-clamps — and its option list resizes — whenever a month/year change
// shortens the month.
void App::apply_date_selection() {
  const int dim = days_in_month(settings_.set_year, settings_.set_month);
  if (settings_.set_day > dim) settings_.set_day = dim;
  set_day_dd_options(settings_, dim);
  if (settings_.day_dd != nullptr)
    lv_dropdown_set_selected(settings_.day_dd, settings_.set_day - 1);
  if (clock_ != nullptr)
    clock_->set_date(settings_.set_year, settings_.set_month, settings_.set_day);
}

void App::month_select(int idx) {
  settings_.set_month = idx + 1;
  apply_date_selection();
}

void App::day_select(int idx) {
  settings_.set_day = idx + 1;
  apply_date_selection();
}

void App::year_select(int idx) {
  settings_.set_year = core::kClockBaseYear + idx;
  apply_date_selection();
}

void App::set_clock_24h(bool on) {
  settings_.clock_24h = on;
  if (clock_ != nullptr) clock_->set_24h(on);
  set_hour_dd_options(settings_);  // relabel the hour options; index == hour
  if (settings_.hour_dd != nullptr)
    lv_dropdown_set_selected(settings_.hour_dd, settings_.set_hour);
}

void App::set_use_fahrenheit(bool on) {
  if (display_ != nullptr) display_->set_use_fahrenheit(on);  // persist
  // Repaint temps now (Home, Settings, Stats) instead of waiting for the next tick.
  if (machine_ != nullptr) {
    const core::MachineSnapshot snap = machine_->snapshot();
    update_temp_panels(snap);
    update_home(home_, snap, battery_state_, clock_->now(), clock_->use_24h(), on,
                scale_->snapshot(), scale_->features(), scale_connect_enabled(),
                brew_->snapshot(), net_status(), heating_);
    update_stats_view();
  }
}

void App::set_drop_negative_flow(bool on) {
  if (display_ != nullptr) display_->set_drop_negative_flow(on);  // persist
  ui::set_flow_drop_negative(home_, on);  // apply + clear the g/s ring
}

void App::set_scope_graph(bool on) {
  if (display_ != nullptr) display_->set_scope_graph(on);  // persist
  ui::set_flow_scope_mode(home_, on);  // apply + reset the plot to the new style
}

void App::set_click_sound(bool on) {
  click_sound_on_ = on;
  if (display_ != nullptr) display_->set_click_sound(on);  // persist
}

void App::cycle_ready_melody() {
  const int count = core::ready_melody_count();
  // Off -> each melody -> Random -> Off.
  int next;
  if (ready_chime_mel_ == core::kReadyMelodyRandom) next = 0;
  else if (ready_chime_mel_ >= count) next = core::kReadyMelodyRandom;
  else next = ready_chime_mel_ + 1;
  ready_chime_mel_ = next;
  if (display_ != nullptr) display_->set_ready_chime_melody(next);  // persist
  set_chime_mel_label(settings_, next);
  // Audition the WHOLE tune — one note says nothing about a melody. The
  // priority rides one above the real chime's so each tap INTERRUPTS the
  // previous audition (equal priority would be dropped, and a long melody
  // would swallow every tap made while it rings). A muted volume setting
  // still auditions at 50% so the choice is audible; the real chime stays
  // governed by both settings.
  if (next > 0 && sound_ != nullptr) {
    core::Playback p = core::ready_melody_playback(
        melody_variant(next), ready_chime_vol_ > 0 ? ready_chime_vol_ : 50);
    ++p.priority;
    sound_->play(p);
  }
}

void App::cycle_ready_chime() {
  int i = 0;
  while (i < kChimeVolCount && kChimeVolChoices[i] != ready_chime_vol_) ++i;
  const int next = kChimeVolChoices[(i + 1) % kChimeVolCount];  // unknown -> Off
  ready_chime_vol_ = next;
  if (display_ != nullptr) display_->set_ready_chime_volume(next);  // persist
  set_chime_vol_label(settings_, next);
  // Audition the level you just landed on — one note, so cycling stays quick.
  // Play the SELECTED melody's note (Blue's when the melody is Off, so the
  // level is still audible while choosing). Priority bumped like the melody
  // audition so it cuts through a still-ringing melody preview.
  if (next > 0 && sound_ != nullptr) {
    core::Playback p = core::ready_melody_sample(
        ready_chime_mel_ > 0 ? melody_variant(ready_chime_mel_) : 0, next);
    ++p.priority;
    sound_->play(p);
  }
}

void App::set_perf_overlay(bool on) {
  if (display_ != nullptr) display_->set_perf_overlay(on);  // persist
  apply_perf_overlay(on);  // show/hide the LVGL sysmon label now
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
  rebuild_section_ = kSectionDeviceDisplay;  // default (the theme button's page)
  // Preserve the section page's scroll position so a rebuild (theme cycling)
  // doesn't bounce the user back to the top each tap.
  int32_t scroll_y = 0;
  if (lv_obj_t* old_page = settings_section_page(settings_, section))
    scroll_y = lv_obj_get_scroll_y(old_page);

  build(*machine_, *provisioner_, *battery_, *display_, *clock_, *history_, *scale_,
        *scale_provisioner_, *brew_, *network_, *sound_, *shots_, screen_);
  show_tab(1);                       // back to Settings...
  select_settings_section(section);  // ...on the section that triggered the rebuild

  if (lv_obj_t* new_page = settings_section_page(settings_, section)) {
    lv_obj_update_layout(new_page);  // compute the scroll range first
    lv_obj_scroll_to_y(new_page, scroll_y, LV_ANIM_OFF);
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
  const core::Link link = machine_->snapshot().link;
  if (link == core::Link::Connected) {
    const core::Power prev = machine_->snapshot().power;
    machine_->set_power(prev != core::Power::On);
    // Immediate feedback for the 1-3 s command->report lag: the button
    // disables and reads "Working..." until the reported power changes (see
    // HomeWidgets::power_pending_until). 8 s deadline = a couple of poll
    // rounds past the normal confirm, so a swallowed command can't lock it.
    home_.power_pending_from = prev;
    home_.power_pending_until = lv_tick_get() + 8000;
    refresh();
    return;
  }
  // Repurposed "Offline" button: when configured but disconnected, a tap starts the
  // BLE connection (mirrors the Settings Connect button). Other states show the
  // button disabled, so this is a defensive no-op for them.
  if (link == core::Link::Disconnected && provisioner_ != nullptr) {
    provisioner_->set_connect_enabled(true);
    refresh();  // reflect Connecting on Home + the Settings Connect/Disconnect button
  }
}

void App::tare_scale() {
  // Refused mid-shot: covers the tap that lands between a shot starting and
  // the next refresh visibly disabling the button.
  if (brew_ != nullptr && core::shot_in_flight(brew_->snapshot())) return;
  if (scale_ != nullptr) scale_->tare();
}

void App::cycle_scale_device_setting(int index) {
  if (scale_ == nullptr || !scale_->snapshot().connected) return;
  if (brew_ != nullptr && core::shot_in_flight(brew_->snapshot())) return;
  if (index < 0 || index >= scale_->device_setting_count()) return;
  const core::ScaleSettingDesc d = scale_->device_setting(index);
  if (d.option_count <= 0 || d.read_only) return;
  const int cur = scale_->device_setting_value(index);
  // Cycle only through the WRITABLE options (some values display but can't
  // be set over BLE — writable_count). -1 ("--": not read back yet, or a
  // value we don't offer) starts at the first option rather than guessing.
  const int writable = (d.writable_count > 0 && d.writable_count < d.option_count)
                           ? d.writable_count
                           : d.option_count;
  const int next = (cur < 0 || cur + 1 >= writable) ? 0 : cur + 1;
  scale_->set_device_setting(index, next);
  update_scale_view();  // show the optimistic value now, not next refresh
}

void App::cycle_flush() {
  if (brew_ == nullptr) return;
  const int cur = brew_->snapshot().flush_s;
  int i = 0;
  while (i < kFlushCount && kFlushChoices[i] != cur) ++i;
  const int next = kFlushChoices[(i + 1) % kFlushCount];  // unknown value -> wraps to Off
  brew_->set_flush_s(next);
  set_flush_label(settings_, next);  // also shows/hides the delay row
}

void App::cycle_flush_delay() {
  if (brew_ == nullptr) return;
  const int cur = brew_->snapshot().flush_delay_s;
  int i = 0;
  while (i < kFlushDelayCount && kFlushDelayChoices[i] != cur) ++i;
  const int next = kFlushDelayChoices[(i + 1) % kFlushDelayCount];
  brew_->set_flush_delay_s(next);
  set_flush_delay_label(settings_, next);
}

void App::cycle_flow_smooth() {
  if (display_ == nullptr) return;
  int level = display_->flow_smooth();
  level = (level + 1) % 4;
  display_->set_flow_smooth(level);
  if (settings_.smooth_value != nullptr)
    lv_label_set_text(settings_.smooth_value, kSmoothName[level]);
  ui::set_shot_smoothing(home_, level);
}

void App::cycle_screen_timeout() {
  if (display_ == nullptr) return;
  int i = 0;
  while (i < kDimCount && kDimMinutes[i] != settings_.screen_timeout_min) ++i;
  const int mins = kDimMinutes[(i + 1) % kDimCount];  // unknown value -> wraps to Off
  settings_.screen_timeout_min = mins;
  display_->set_screen_timeout_min(mins);
  set_dim_label(settings_);
}

void App::screensaver_tick() {
  if (display_ == nullptr) return;
  const int mins = settings_.screen_timeout_min;
  const bool idle = mins > 0 && lv_display_get_inactive_time(nullptr) >=
                                    static_cast<uint32_t>(mins) * 60000u;
  if (idle == screensaver_on_) return;  // touch resets LVGL's inactivity clock
  screensaver_on_ = idle;
  display_->set_screensaver(idle);
}

void App::shot_button() {
  if (brew_ == nullptr) return;
  const core::BrewSnapshot b = brew_->snapshot();
  // Guard against a tap sneaking through while the button is disabled mid-shot
  // (state applied on the 2 Hz refresh, so there's a small window).
  if (b.phase == core::ShotPhase::kBrewing || b.phase == core::ShotPhase::kSettling) return;
  if (b.phase == core::ShotPhase::kReview) {
    brew_->dismiss_review();  // back to live monitoring; graph resumes next tick
  } else {
    // Cycle the shot mode: Auto shot -> Shot detect -> Manual -> ... Auto is
    // only offered where the wired relay exists (elsewhere the snapshot's mode
    // never reads kAuto, so the cycle is just Detect <-> Manual).
    const bool auto_ok = b.paddle_hw && b.wired_setting;
    core::ShotMode next;
    switch (b.mode) {
      case core::ShotMode::kAuto:   next = core::ShotMode::kDetect; break;
      case core::ShotMode::kDetect: next = core::ShotMode::kManual; break;
      default:  next = auto_ok ? core::ShotMode::kAuto : core::ShotMode::kDetect; break;
    }
    brew_->set_shot_mode(next);
  }
  refresh();  // reflect the new button label/color immediately, not at the next 2 Hz
}

void App::set_wired_paddle(bool on) {
  if (brew_ == nullptr) return;
  brew_->set_wired_paddle(on);  // cancels any in-flight shot to kIdle
  // pump_scale_chart's phase tracking then returns a mid-shot/frozen plot to
  // the live sweep on its next pass; nothing else to clean up here.
  refresh();
}

void App::toggle_flow_units() { ui::toggle_flow_mode(home_); }

void App::show_toast(const char* msg) {
  dismiss_toast();
  lv_obj_t* card = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(card);
  lv_obj_set_width(card, lv_pct(88));
  lv_obj_set_height(card, LV_SIZE_CONTENT);
  const bool compact = is_compact(screen_);
  // Above the compact bottom tab bar; over the graph's dead space elsewhere.
  lv_obj_align(card, LV_ALIGN_BOTTOM_MID, 0, -ui::dp(compact ? 50 : 16));
  lv_obj_set_style_bg_color(card, lv_color_hex(ui::theme::card()), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, ui::dp(10), 0);
  lv_obj_set_style_border_width(card, ui::dp(2), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(ui::theme::warn()), 0);
  lv_obj_set_style_pad_all(card, ui::dp(compact ? 8 : 12), 0);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card, on_toast_clicked, LV_EVENT_CLICKED, this);
  lv_obj_t* l = lv_label_create(card);
  lv_label_set_text(l, msg);
  lv_obj_set_width(l, lv_pct(100));
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(l, ui::font_dp(compact ? 14 : 16), 0);
  toast_ = card;
  // Long enough to READ, not just notice — the refusal message is two
  // sentences and the reader just looked up from the paddle.
  toast_timer_ = lv_timer_create(on_toast_timeout, 7000, this);
}

void App::dismiss_toast() {
  // Callable from the timeout timer's own callback — LVGL supports a timer
  // deleting itself mid-callback (same idiom as the flash countdowns).
  if (toast_timer_ != nullptr) {
    lv_timer_delete(toast_timer_);
    toast_timer_ = nullptr;
  }
  if (toast_ != nullptr) {
    lv_obj_delete(toast_);
    toast_ = nullptr;
  }
}

void App::pose_unwired_midshot() {
  // Sim-only. Fill the ring exactly as unwired_ring_tick would have — absolute
  // stamps, raw grams (cup on the untared scale) — covering a quiet baseline,
  // then lever-on at now-9s, flow onset (first drip) at now-6s, a ~2 g/s pour
  // since. Then run the handoff the entering-brew branch runs at detection.
  // The sim's lv_tick is nearly 0 at render time, so these absolute stamps
  // wrap negative — fine: every consumer diffs them modularly, exactly like a
  // device tick rollover mid-ring.
  const uint32_t now = lv_tick_get();
  const float baseline = 315.2f;
  const uint32_t t_lever = now - 9000, t_onset = now - 6000;
  constexpr int kN = 200;  // 20 s at the ring's 100 ms cadence (< kShotCap)
  for (int i = 0; i < kN; ++i) {
    const uint32_t t = now - 20000 + static_cast<uint32_t>(i + 1) * 100;
    const int32_t since_onset = static_cast<int32_t>(t - t_onset);
    home_.shot_ts[i] = t;
    home_.shot_weights[i] =
        baseline +
        (since_onset > 0 ? 2.0f * static_cast<float>(since_onset) / 1000.0f : 0.0f);
    home_.shot_flows[i] = since_onset > 0 ? 2.0f : 0.0f;
  }
  home_.shot_n = kN;
  home_.shot_head = kN % ui::HomeWidgets::kShotCap;
  home_.unwired_ring = true;
  ui::enter_shot_plot_live(home_, t_lever, 3000, baseline);
}

void App::pump_scale_chart() {
  if (scale_ == nullptr) return;

  // Sweep the flow graph left->right by wall-clock time (smooth regardless of the
  // sample rate); flow is derived from the streamed weight. Cheap no-op without a graph.
  const core::ScaleSnapshot snap = scale_->snapshot();

  // Shot lifecycle -> graph: clear the plot when a shot starts (it records exactly
  // one shot), freeze it during review (no ticking = the trace stays put), resume
  // after. The weight readout freezes with it, so pulling the cup during review
  // doesn't wipe the final weight off the display; it goes live again on reset.
  bool graph_frozen = false;
  const bool have_brew = brew_ != nullptr;
  core::BrewSnapshot bsnap{};
  if (have_brew) {
    bsnap = brew_->snapshot();
    // A paddle flip swallowed during review: point the user at the Reset
    // button (the flip did NOT start the machine — see BrewController).
    if (bsnap.review_reject_seq != brew_reject_seen_) {
      brew_reject_seen_ = bsnap.review_reject_seq;
      ui::flash_shot_button(home_);
    }
    // A paddle flip refused because the armed mode needs the scale (Auto shot
    // / Shot detect with no scale connected): the machine stayed off — say
    // why, on every layout (compact has no shot button to flash). The click
    // gives the flip an audible acknowledgement too.
    if (bsnap.scale_refuse_seq != scale_refuse_seen_) {
      scale_refuse_seen_ = bsnap.scale_refuse_seq;
      ui::play_button_press();
      char tb[128];
      std::snprintf(tb, sizeof(tb),
                    "Shot not started: %s needs the scale. Switch to Manual mode "
                    "to pull without one, or connect a scale.",
                    bsnap.mode == core::ShotMode::kAuto ? "Auto shot" : "Shot detect");
      show_toast(tb);
    }
    // Unwired stop-early signal: fire the pill flash once per shot, on the
    // controller's latch going high (see BrewSnapshot::stop_hint).
    if (bsnap.stop_hint && !stop_hint_seen_) ui::flash_stop_hint(home_);
    stop_hint_seen_ = bsnap.stop_hint;
    const bool entering_brew =
        bsnap.phase == core::ShotPhase::kBrewing && shot_phase_ != core::ShotPhase::kBrewing;
    if (entering_brew) {
      if (bsnap.paddle_wired) {
        ui::begin_shot_plot(home_);  // dynamic-X plot owns the canvas for the shot
        // A tare was sent only if the baseline isn't already confirmed (the scale
        // wasn't pre-tared): the old weight samples are then in pre-tare units and
        // must go. Pre-tared shots keep the history -> rate is live from t=0.
        if (!bsnap.baseline_set) ui::reset_flow_history(home_);
      } else {
        // Unwired: the detector confirmed retroactively — pin the shot's retro
        // start on the UI clock (shot_ms is running, so now - shot_ms IS the
        // backdated start incl. the preinfusion lead-in) and hand the always-on
        // ring to the live shot plot: back-filled from pre-detection history,
        // rebased to shot start and shot grams, then fed live exactly like a
        // wired shot.
        unwired_shot_t0_ = lv_tick_get() - bsnap.shot_ms;
        ui::enter_shot_plot_live(
            home_, unwired_shot_t0_,
            static_cast<uint32_t>(bsnap.detect_lead_in_s) * 1000u,
            bsnap.start_weight_g);
        last_weight_g_ = -10000.0f;  // force the readout onto the net convention
      }
    } else if (home_.flow_shot_plot && bsnap.phase == core::ShotPhase::kIdle) {
      ui::end_shot_plot(home_);  // review dismissed/timed out -> live sweep
    } else if (bsnap.phase == core::ShotPhase::kReview &&
               shot_phase_ != core::ShotPhase::kReview) {
      if (bsnap.paddle_wired) {
        ui::finish_shot_plot(home_);  // flush the display lag before the freeze
      } else {
        if (home_.flow_shot_plot) {
          // The mid-shot handoff already rebased the ring (shot-relative
          // stamps, shot grams) — finish exactly like wired. Re-running
          // review_shot_plot would double-subtract the baseline and
          // double-shift the timestamps.
          ui::finish_shot_plot(home_);
        } else {
          // Handoff never happened (no canvas on this layout at detection):
          // the ring still holds absolute stamps and raw grams — one-shot,
          // shot-aligned repaint (through now, so the settle plateau shows).
          ui::review_shot_plot(
              home_, unwired_shot_t0_, lv_tick_get(),
              static_cast<uint32_t>(bsnap.detect_lead_in_s) * 1000u,
              bsnap.start_weight_g);
        }
        // The frozen weight readout must agree with the repainted plot: unwired
        // shots never tare, so the raw reading includes the cup — show the shot
        // grams (net of the detector's baseline) instead. The sentinel forces
        // the live writer to repaint the raw value on dismiss even if the scale
        // reading hasn't moved since the freeze.
        if (home_.scale_weight != nullptr && snap.connected) {
          char wb[16];
          std::snprintf(wb, sizeof(wb), "%.1f g",
                        static_cast<double>(snap.weight_g - bsnap.start_weight_g));
          lv_label_set_text(home_.scale_weight, wb);
          last_weight_g_ = -10000.0f;
        }
      }
      capture_shot_record(bsnap, snap);  // after the freeze: rings are shot-relative
    }
    // The stop-early flash must not outlive the shot it belongs to: the user
    // already flipped the paddle (that's what ended it) — kill any remaining
    // pulses the moment the phase leaves kBrewing.
    if (shot_phase_ == core::ShotPhase::kBrewing &&
        bsnap.phase != core::ShotPhase::kBrewing) {
      ui::cancel_stop_flash(home_);
      // An aborted unwired shot flips the readout back from net to raw; the
      // sentinel forces that repaint even if the reading hasn't moved. (A
      // normal end goes to kSettling, where the net convention holds until
      // the review freeze writes its own sentinel. Harmless extra repaint
      // for wired shots.)
      last_weight_g_ = -10000.0f;
    }
    shot_phase_ = bsnap.phase;
    // Hold until the post-tare baseline is confirmed (the tare window's readings
    // are garbage — usually a ~200ms blink now); freeze during review. kSettling
    // keeps ticking so the drip tail settles to zero on-screen.
    if (bsnap.phase == core::ShotPhase::kBrewing && !bsnap.baseline_set) {
      graph_frozen = true;
    } else if (bsnap.phase == core::ShotPhase::kReview) {
      graph_frozen = true;
    }
  }
  if (!graph_frozen) {
    if (home_.flow_shot_plot) {
      ui::shot_plot_tick(home_, snap);
    } else {
      ui::flow_graph_tick(home_, snap);
      // Unwired: keep the always-on capture ring fed alongside the live sweep —
      // a detected shot's samples must already exist when review replays them.
      if (have_brew && !bsnap.paddle_wired) ui::unwired_ring_tick(home_, snap);
    }
  }

  // Weight readout: redraw the moment the cached snapshot carries a new value, so
  // the display runs at the scale's own notify rate (the snapshot is just the last
  // BLE notify). lv_label_set_text no-ops when the formatted text is unchanged.
  // Frozen during review (like the graph and the stopped timer): the label holds
  // the settled final weight even if the cup is pulled. On dismiss the change
  // check sees live-vs-held differ and the readout resets with the rest.
  const bool weight_frozen = have_brew && bsnap.phase == core::ShotPhase::kReview;
  if (home_.scale_weight != nullptr && !weight_frozen &&
      (snap.weight_g != last_weight_g_ || snap.connected != last_scale_connected_)) {
    last_weight_g_ = snap.weight_g;
    last_scale_connected_ = snap.connected;
    // Unwired shots never tare — show shot grams (net of the detector's
    // baseline) while one runs/settles, matching the shot plot. The change
    // check stays keyed on the RAW reading (net differs by a constant);
    // convention flips are forced by the sentinel writes at shot start/end.
    const float shown =
        snap.weight_g - (have_brew && core::unwired_net_weight(bsnap)
                             ? bsnap.start_weight_g
                             : 0.0f);
    char wb[16];
    if (snap.connected) std::snprintf(wb, sizeof(wb), "%.1f g", static_cast<double>(shown));
    else std::snprintf(wb, sizeof(wb), "-- g");
    lv_label_set_text(home_.scale_weight, wb);
  }

  // Shot timer: continuous (ms) value, so redraw on a fixed ~10 Hz cadence that
  // matches its 0.1 s display resolution — faster would render invisible changes.
  // Only while the ESP timer is the source (core::esp_shot_timer, shared with
  // update_home — unwired with detection off shows the scale's own timer).
  if (have_brew && core::esp_shot_timer(bsnap) && home_.shot_timer_label != nullptr) {
    const uint32_t t = lv_tick_get();
    if (t - scale_readout_tick_ >= 100) {
      scale_readout_tick_ = t;
      char sb[16];
      std::snprintf(sb, sizeof(sb), "%.1f s", static_cast<double>(bsnap.shot_ms) / 1000.0);
      lv_label_set_text(home_.shot_timer_label, sb);
    }
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

  provisioner_->save_device(results[index]);  // saved; no token yet -> NeedsToken
  show_token_modal(/*fetch_failed=*/false);    // prompt the user to enter a token
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
  lv_obj_set_style_pad_all(card, ui::dp(14), 0);
  lv_obj_set_style_pad_row(card, ui::dp(10), 0);

  // Larger faces on the non-compact tiers: the WiFi/token instructions carry
  // an SSID + URL the user has to read across the room while holding a phone.
  const bool modal_compact = is_compact(screen_);
  lv_obj_t* t = lv_label_create(card);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_color(t, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(t, ui::font_dp(modal_compact ? 20 : 24), 0);

  lv_obj_t* b = lv_label_create(card);
  lv_label_set_text(b, body);
  lv_obj_set_width(b, lv_pct(100));
  lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(b, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(b, ui::font_dp(modal_compact ? 14 : 16), 0);
  return card;
}

void App::close_modal() {
  if (log_timer_ != nullptr) {  // log viewer: stop tailing before the widgets go
    lv_timer_delete(log_timer_);
    log_timer_ = nullptr;
  }
  log_box_ = nullptr;
  log_label_ = nullptr;
  if (modal_ != nullptr) {
    lv_obj_delete(modal_);
    modal_ = nullptr;
  }
  if (tabview_ != nullptr) lv_obj_remove_flag(tabview_, LV_OBJ_FLAG_HIDDEN);  // reveal the UI
  wifi_setup_shown_ = false;
}

// Settings > "Lock display for cleaning": a full-screen opaque overlay on the
// top layer swallows every touch for kCleanLockSecs so the glass can be wiped
// without pressing buttons. Auto-dismisses; there is deliberately no way to
// end it early by touch.
void App::start_clean_lock() {
  if (clean_lock_overlay_ != nullptr) return;  // already locked

  lv_obj_t* bg = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(bg, lv_color_hex(ui::theme::bg()), 0);
  lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);  // eat touches (remove_style_all cleared it)
  lv_obj_set_flex_flow(bg, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(bg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(bg, ui::dp(8), 0);
  clean_lock_overlay_ = bg;
  // Nothing behind the opaque overlay is visible; don't render it (same
  // reasoning as open_modal).
  if (tabview_ != nullptr) lv_obj_add_flag(tabview_, LV_OBJ_FLAG_HIDDEN);

  const bool compact = is_compact(screen_);
  lv_obj_t* t = lv_label_create(bg);
  lv_label_set_text(t, "Cleaning");
  lv_obj_set_style_text_color(t, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(t, ui::font_dp(compact ? 20 : 24), 0);

  clean_lock_count_ = lv_label_create(bg);
  lv_obj_set_style_text_color(clean_lock_count_, lv_color_hex(ui::theme::accent()), 0);
  lv_obj_set_style_text_font(clean_lock_count_, ui::font_dp(compact ? 40 : 48), 0);

  lv_obj_t* hint = lv_label_create(bg);
  lv_label_set_text(hint, "Touch disabled");
  lv_obj_set_style_text_color(hint, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(hint, ui::font_dp(compact ? 14 : 16), 0);

  clean_lock_t0_ = lv_tick_get();
  clean_lock_shown_s_ = -1;
  clean_lock_tick();  // paint "30" now, not a timer period later
  clean_lock_timer_ = lv_timer_create(on_clean_lock_timer, 250, this);
}

void App::clean_lock_tick() {
  if (clean_lock_overlay_ == nullptr) return;
  const uint32_t elapsed_ms = lv_tick_elaps(clean_lock_t0_);
  if (elapsed_ms >= static_cast<uint32_t>(kCleanLockSecs) * 1000u) {
    end_clean_lock();
    return;
  }
  const int remaining = kCleanLockSecs - static_cast<int>(elapsed_ms / 1000u);
  if (remaining != clean_lock_shown_s_) {
    clean_lock_shown_s_ = remaining;
    lv_label_set_text_fmt(clean_lock_count_, "%d", remaining);
  }
}

void App::end_clean_lock() {
  if (clean_lock_timer_ != nullptr) {
    lv_timer_delete(clean_lock_timer_);
    clean_lock_timer_ = nullptr;
  }
  if (clean_lock_overlay_ != nullptr) {
    lv_obj_delete(clean_lock_overlay_);
    clean_lock_overlay_ = nullptr;
    clean_lock_count_ = nullptr;
  }
  // Reveal the UI again — unless a modal is up (it hides/reveals on its own).
  if (tabview_ != nullptr && modal_ == nullptr)
    lv_obj_remove_flag(tabview_, LV_OBJ_FLAG_HIDDEN);
}

// --- Backflush cleaning (Settings > Micra) ---------------------------------
// A full-screen mode like the cleaning lock, but interactive: it prompts for
// the blind filter, runs core's pulse sequence, and shows the cycle/countdown.
// The sequence itself lives in BrewController — this only starts, cancels, and
// reports it, so a UI teardown can never leave the group running.

void App::open_backflush() {
  if (brew_ == nullptr || bf_overlay_ != nullptr) return;
  close_modal();  // never stack this over another overlay

  const bool compact = is_compact(screen_);
  const bool xl = is_xl(screen_);
  const lv_font_t* body_font = ui::font_dp(compact ? 14 : xl ? 24 : 18);
  const lv_font_t* btn_font = ui::font_dp(compact ? 14 : xl ? 24 : 20);
  const int btn_h = ui::dp(compact ? 38 : xl ? 72 : 54);

  lv_obj_t* bg = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(bg, lv_color_hex(ui::theme::bg()), 0);
  lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);  // swallow taps outside the buttons
  lv_obj_set_flex_flow(bg, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(bg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(bg, ui::dp(compact ? 10 : 24), 0);
  lv_obj_set_style_pad_row(bg, ui::dp(compact ? 6 : 12), 0);
  bf_overlay_ = bg;
  // Nothing behind an opaque overlay is visible; don't render it (same
  // reasoning as open_modal / the cleaning lock).
  if (tabview_ != nullptr) lv_obj_add_flag(tabview_, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* title = lv_label_create(bg);
  lv_label_set_text(title, "Backflush cleaning");
  lv_obj_set_style_text_color(title, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(title, ui::font_dp(compact ? 20 : xl ? 36 : 28), 0);

  // Cycle counter + phase countdown: the whole point of the running screen, so
  // they get the biggest type on it.
  bf_cycle_label_ = lv_label_create(bg);
  lv_obj_set_style_text_color(bf_cycle_label_, lv_color_hex(ui::theme::accent()), 0);
  lv_obj_set_style_text_font(bf_cycle_label_, ui::font_dp(compact ? 24 : xl ? 48 : 40), 0);

  bf_phase_label_ = lv_label_create(bg);
  lv_obj_set_style_text_color(bf_phase_label_, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(bf_phase_label_, ui::font_dp(compact ? 16 : xl ? 28 : 22), 0);

  bf_msg_ = lv_label_create(bg);
  lv_obj_set_width(bf_msg_, lv_pct(compact ? 96 : 80));
  lv_label_set_long_mode(bf_msg_, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(bf_msg_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(bf_msg_, lv_color_hex(ui::theme::muted()), 0);
  lv_obj_set_style_text_font(bf_msg_, body_font, 0);

  lv_obj_t* row = lv_obj_create(bg);
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, ui::dp(compact ? 8 : 16), 0);
  lv_obj_set_style_pad_top(row, ui::dp(compact ? 4 : 10), 0);

  auto big_button = [&](const char* text, uint32_t color, lv_event_cb_t cb,
                        lv_obj_t** out_label) {
    lv_obj_t* b = ui::make_button(row);
    lv_obj_set_height(b, btn_h);
    lv_obj_set_style_pad_hor(b, ui::dp(compact ? 14 : 28), 0);
    lv_obj_set_style_radius(b, ui::dp(12), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, this);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(ui::theme::text()), 0);
    lv_obj_set_style_text_font(l, btn_font, 0);
    lv_obj_center(l);
    if (out_label != nullptr) *out_label = l;
    return b;
  };
  bf_go_btn_ = big_button("Go", ui::theme::accent(), on_backflush_go, &bf_go_label_);
  bf_cancel_btn_ = big_button("Cancel", ui::theme::alert(), on_backflush_cancel, nullptr);
  // Back always leaves the mode (stopping the sequence on its way out), so the
  // user is never trapped on this screen.
  bf_back_btn_ = big_button("Back", ui::theme::rail(), on_backflush_back, nullptr);

  // Reopening while a sequence runs (left and came back) picks it up live.
  bf_state_ = brew_->snapshot().backflush_active ? kBackflushRunning : kBackflushPrompt;
  if (bf_timer_ == nullptr) bf_timer_ = lv_timer_create(on_backflush_timer, 250, this);
  backflush_tick();  // paint the initial state now, not a tick later
}

void App::close_backflush() {
  if (brew_ != nullptr) brew_->cancel_backflush();  // never leave it running
  if (bf_timer_ != nullptr) {
    lv_timer_delete(bf_timer_);
    bf_timer_ = nullptr;
  }
  if (bf_overlay_ != nullptr) {
    lv_obj_delete(bf_overlay_);
    bf_overlay_ = nullptr;
    bf_msg_ = bf_cycle_label_ = bf_phase_label_ = nullptr;
    bf_go_btn_ = bf_go_label_ = bf_cancel_btn_ = bf_back_btn_ = nullptr;
  }
  if (tabview_ != nullptr && modal_ == nullptr)
    lv_obj_remove_flag(tabview_, LV_OBJ_FLAG_HIDDEN);
}

void App::backflush_go() {
  if (brew_ == nullptr) return;
  if (brew_->start_backflush()) bf_state_ = kBackflushRunning;
  backflush_tick();  // reflect the new state immediately
}

void App::backflush_cancel() {
  if (brew_ != nullptr) brew_->cancel_backflush();
  bf_state_ = kBackflushPrompt;  // back to the prompt, ready to run again
  backflush_tick();
}

void App::backflush_tick() {
  if (bf_overlay_ == nullptr || brew_ == nullptr) return;
  const core::BrewSnapshot b = brew_->snapshot();

  // The sequence ending on its own while we're showing "running" is either
  // completion or a takeover (a paddle flip, or the harness setting going off
  // mid-run) — the two deserve different words.
  if (bf_state_ == kBackflushRunning && !b.backflush_active)
    bf_state_ = b.backflush_done ? kBackflushDone : kBackflushAborted;

  char cycle[32] = "";
  char phase[40] = "";
  const char* msg = "";
  const bool running = bf_state_ == kBackflushRunning;

  switch (bf_state_) {
    case kBackflushRunning:
      std::snprintf(cycle, sizeof(cycle), "Cycle %d of %d", b.backflush_cycle,
                    core::kBackflushCycles);
      std::snprintf(phase, sizeof(phase), "%s  %us",
                    b.backflush_on ? "Running" : "Pause",
                    static_cast<unsigned>((b.backflush_phase_ms + 999) / 1000));
      msg = "Leave the portafilter in place until the cycles finish.";
      break;
    case kBackflushDone:
      msg = "Backflush complete.\nRinse the basket, then run it again with "
            "plain water to clear any detergent.";
      break;
    case kBackflushAborted:
      msg = "Backflush stopped: the paddle was used, or the wired-paddle "
            "setting changed.";
      break;
    case kBackflushPrompt:
    default:
      if (!b.relay) {
        msg = "Backflushing needs the paddle harness. Turn on Wired paddle in "
              "Micra settings first.";
      } else if (!b.clean_ready) {
        msg = "Turn the machine on (and finish any shot) first.";
      } else {
        // Pulse length follows the Auto flush setting, so spell out what THIS
        // machine will actually do. Plain ASCII punctuation only: the bundled
        // Montserrat subset has no em-dash (it renders as a missing-glyph box).
        const uint32_t on_ms = core::flush_run_ms(b.flush_s);
        static char prompt[220];
        std::snprintf(prompt, sizeof(prompt),
                      "Fit the blind filter with cleaning detergent, then tap "
                      "Go.\n\n%d cycles of %us on, %us off. About %us total.\n"
                      "Pulse length follows Auto flush.",
                      core::kBackflushCycles,
                      static_cast<unsigned>(on_ms / 1000),
                      static_cast<unsigned>(core::kBackflushOffMs / 1000),
                      static_cast<unsigned>(core::kBackflushCycles *
                                            (on_ms + core::kBackflushOffMs) / 1000));
        msg = prompt;
      }
      break;
  }

  // Empty labels still occupy a line box; hide them so the idle screens don't
  // carry a gap where the running readout goes.
  auto set_or_hide = [](lv_obj_t* label, const char* text) {
    if (text[0] == '\0') {
      lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(label, text);
      lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
  };
  set_or_hide(bf_cycle_label_, cycle);
  set_or_hide(bf_phase_label_, phase);
  lv_label_set_text(bf_msg_, msg);

  // Go and Cancel swap places by state; Back is always available.
  if (running) {
    lv_obj_add_flag(bf_go_btn_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(bf_cancel_btn_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(bf_go_btn_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bf_cancel_btn_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(bf_go_label_,
                      bf_state_ == kBackflushPrompt ? "Go" : "Run again");
    set_clickable(bf_go_btn_, b.clean_ready);
  }
}

void App::toggle_manual_flush() {
  if (brew_ == nullptr) return;
  brew_->toggle_manual_flush();
  refresh();  // flip the button to Stop (or back) now, not at the next 2 Hz tick
}

// Spinner shown while the pairing-mode token read runs (gives "it's working"
// feedback). Replaced by success (Home) or the token-choice modal on failure.
// Token-setup prompt: the machine's BLE token is issued by the La Marzocco cloud
// (get it with the lmtoken app) — it can't be read off the machine — so the only
// path is to enter it over WiFi. fetch_failed is kept for call-site symmetry.
void App::show_token_modal(bool /*fetch_failed*/) {
  lv_obj_t* card = open_modal(
      "Set up token",
      "This machine needs its Bluetooth token. Get it with the lmtoken app, "
      "then enter it over WiFi.");
  lv_obj_t* row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  modal_button(row, "Enter token", ui::theme::accent(), on_token_wifi, this);
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

void App::set_wifi_enabled(bool on) {
  if (network_ != nullptr) network_->set_enabled(on);
  update_settings_view();  // refresh the status line right away
}

void App::show_wifi_setup_modal() {
  if (network_ == nullptr) return;
  char body[192];
  std::snprintf(body, sizeof(body),
                "Join WiFi: %s\nOpen: %s\nenter your home network name + password",
                network_->setup_ssid(), network_->setup_url());
  lv_obj_t* card = open_modal("Set up WiFi", body);
  modal_button(card, "Cancel", ui::theme::rail(), on_wifi_setup_cancel, this);
  wifi_portal_shown_ = true;
}

void App::start_wifi_setup() {
  if (network_ == nullptr) return;
  close_modal();
  network_->begin_setup_portal();
  show_wifi_setup_modal();
}

void App::cancel_wifi_setup() {
  if (network_ != nullptr) network_->stop_setup_portal();
  wifi_portal_shown_ = false;
  close_modal();
}

void App::forget_wifi() {
  if (network_ != nullptr) network_->forget();
  update_settings_view();  // syncs the enable switch off too
}

void App::timezone_select(int index) {
  if (network_ == nullptr || index < 0 || index >= ui::kTimezoneCount) return;
  network_->set_timezone(ui::kTimezones[index].posix);
}

void App::set_ntp_enabled(bool on) {
  if (network_ != nullptr) network_->set_ntp_enabled(on);
}

void App::dismiss_modal() { close_modal(); }

void App::select_settings_section(int section) {
  commit_temp_edits();  // write pending edits from the section we're leaving
  settings_select_section(settings_, section);
  if (section == kSectionDeviceTime) seed_time_controls();  // show the current time
}

void App::select_stats_section(int section) {
  stats_select_section(stats_, section);
  update_stats_view();
}

void App::set_history_filter(int year_month) {
  if (year_month == stats_.history_filter_ym) return;
  stats_.history_filter_ym = year_month;
  update_history_view();
}

void App::update_history_view() {
  if (shots_ == nullptr || stats_.history_box == nullptr) return;

  // Prerequisites: storage and a real calendar date (records are stamped from
  // now_unix). Without either, the section is a how-to card instead of data.
  if (!shots_->available()) {
    // Distinguish "empty slot" from "card we can't use" — the store probes
    // an unmountable card's boot sector and names its filesystem.
    const core::StorageInfo si = shots_->storage();
    if (si.state == core::MediumState::kBadFormat) {
      char msg[160];
      std::snprintf(msg, sizeof(msg),
                    "This SD card is %s-formatted, which isn't supported.\n"
                    "Reformat it as FAT32 to record shot history.",
                    si.fs_type[0] != '\0' && si.fs_type[0] != '?'
                        ? si.fs_type
                        : "not FAT32");
      ui::history_show_guidance(stats_, msg);
    } else {
      ui::history_show_guidance(
          stats_,
          "Insert a FAT32-formatted SD card to record shot history.\n"
          "Each finished shot is saved with its stats and graph.");
    }
    return;
  }
  // An unset clock does NOT block viewing — stored shots carry their own
  // timestamps (NTP can take minutes after a cold boot). It only blocks
  // CAPTURE (which gates on now_unix itself); the footer says so below.
  const core::WallTime now_wall = clock_ != nullptr ? clock_->now() : core::WallTime{};
  ui::history_show_content(stats_);

  const int64_t now_unix = clock_ != nullptr ? clock_->now_unix() : 0;
  const core::ShotStats st = shots_->stats(now_unix);
  char b[24];
  std::snprintf(b, sizeof(b), "%d", st.total);
  lv_label_set_text(stats_.hist_stat_total, b);
  // After a stats reset the Total card says so ("Since 6/15/26") — the list
  // below still shows everything, only the headline numbers restarted.
  if (stats_.hist_stat_total_cap != nullptr) {
    const int64_t since = shots_->stats_since();
    if (since > 0) {
      const time_t t = static_cast<time_t>(since);
      struct tm tm;
      localtime_r(&t, &tm);
      char cap[20];
      std::snprintf(cap, sizeof(cap), "Since %d/%d/%02d", tm.tm_mon + 1,
                    tm.tm_mday, (tm.tm_year + 1900) % 100);
      lv_label_set_text(stats_.hist_stat_total_cap, cap);
    } else {
      lv_label_set_text(stats_.hist_stat_total_cap,
                        is_compact(screen_) ? "Shots" : "Total shots");
    }
  }
  if (st.acc_lifetime_pct > 0.0f)
    std::snprintf(b, sizeof(b), "%.1f%%", static_cast<double>(st.acc_lifetime_pct));
  else
    std::snprintf(b, sizeof(b), "-");
  lv_label_set_text(stats_.hist_stat_life, b);
  if (st.acc_30d_pct > 0.0f)
    std::snprintf(b, sizeof(b), "%.1f%%", static_cast<double>(st.acc_30d_pct));
  else
    std::snprintf(b, sizeof(b), "-");
  lv_label_set_text(stats_.hist_stat_30, b);

  // Footer center: normally the browse URL (only while the station is
  // connected — the IP means nothing otherwise). An unset clock takes the
  // slot over as a warning: new shots are NOT being recorded until it's set.
  if (stats_.history_url_label != nullptr) {
    if (!now_wall.date_valid) {
      lv_label_set_text(stats_.history_url_label,
                        "Clock not set - new shots won't be saved");
      lv_obj_set_style_text_color(stats_.history_url_label,
                                  lv_color_hex(ui::theme::alert()), 0);
    } else if (network_ != nullptr &&
               network_->status() == core::NetState::Connected &&
               network_->ip()[0] != '\0') {
      char url[48];
      std::snprintf(url, sizeof(url), "View at http://%s", network_->ip());
      lv_label_set_text(stats_.history_url_label, url);
      lv_obj_set_style_text_color(stats_.history_url_label,
                                  lv_color_hex(ui::theme::muted()), 0);
    } else {
      lv_label_set_text(stats_.history_url_label, "");
    }
    lv_obj_align(stats_.history_url_label, LV_ALIGN_CENTER, 0, 0);  // re-center
  }

  // Capacity footer (updates every pass — the cache is cheap to read). FULL
  // is loud: saves are being dropped and the user should know why.
  if (stats_.history_sd_label != nullptr) {
    const core::StorageInfo si = shots_->storage();
    if (si.total_bytes == 0) {
      lv_label_set_text(stats_.history_sd_label, "");
    } else if (si.full) {
      lv_label_set_text(stats_.history_sd_label,
                        "SD card FULL - shots are not being saved");
      lv_obj_set_style_text_color(stats_.history_sd_label,
                                  lv_color_hex(ui::theme::alert()), 0);
    } else {
      char cap[64];
      auto fmt_gb = [](uint64_t bytes, char* out, size_t n) {
        if (bytes >= 1000000000ull)
          std::snprintf(out, n, "%.1f GB", static_cast<double>(bytes) / 1e9);
        else
          std::snprintf(out, n, "%u MB",
                        static_cast<unsigned>(bytes / 1000000ull));
      };
      char free_s[20], total_s[20];
      fmt_gb(si.free_bytes, free_s, sizeof(free_s));
      fmt_gb(si.total_bytes, total_s, sizeof(total_s));
      std::snprintf(cap, sizeof(cap), "SD card: %s free of %s", free_s, total_s);
      lv_label_set_text(stats_.history_sd_label, cap);
      lv_obj_set_style_text_color(stats_.history_sd_label,
                                  lv_color_hex(ui::theme::muted()), 0);
    }
    lv_obj_align(stats_.history_sd_label, LV_ALIGN_RIGHT_MID, 0, 0);
  }

  // Rebuild the month filter + rows only when the data or filter changed —
  // this runs on the 2 Hz refresh while the tab is visible.
  if (shots_->count() == hist_built_count_ &&
      stats_.history_filter_ym == hist_built_filter_)
    return;
  hist_built_count_ = shots_->count();
  hist_built_filter_ = stats_.history_filter_ym;

  // One pass over the summaries (paged, newest first): collect the distinct
  // months that have shots (for the calendar filter) and the rows matching
  // the active month. The row cap keeps LVGL's widget count sane; the month
  // filter is how a long history stays navigable without deep scrolling.
  constexpr int kMaxRows = 60;
  constexpr int kMaxMonths = 60;  // 5 years of distinct months — plenty
  constexpr int kPage = 16;
  const int filter_ym = stats_.history_filter_ym;
  int months[kMaxMonths];
  int n_months = 0;
  bool filter_month_seen = filter_ym == 0;
  core::ShotSummary page[kPage];
  int offset = 0, rows = 0;
  bool truncated = false;

  ui::history_clear_rows(stats_);
  for (;;) {
    const int n = shots_->list(page, kPage, offset);
    if (n == 0) break;
    offset += n;
    for (int i = 0; i < n; ++i) {
      const core::ShotSummary& s = page[i];
      const int ym = shot_month_key(s.unix_time);
      // Newest-first input keeps this ordered; dedupe against the last entry.
      if (n_months < kMaxMonths && (n_months == 0 || months[n_months - 1] != ym))
        months[n_months++] = ym;
      if (ym == filter_ym) filter_month_seen = true;

      if (filter_ym != 0 && ym != filter_ym) continue;
      if (rows >= kMaxRows) {
        truncated = true;
        continue;  // keep scanning: the month list needs the full history
      }
      char when[40];
      ui::format_shot_datetime(when, sizeof(when), s.unix_time,
                               clock_ != nullptr && clock_->use_24h(),
                               !is_compact(screen_));
      char result_txt[24], diff_txt[16], dur_txt[12], mode_txt[24];
      std::snprintf(dur_txt, sizeof(dur_txt), "%us",
                    static_cast<unsigned>((s.duration_ms + 500) / 1000));
      ui::format_shot_mode_tag(mode_txt, sizeof(mode_txt), s,
                               ui::shot_mode_tag_glyph_only(screen_));
      uint32_t diff_color = ui::theme::muted();
      if (s.target_g > 0.0f) {
        std::snprintf(result_txt, sizeof(result_txt), "%.1f/%.0fg",
                      static_cast<double>(s.final_g),
                      static_cast<double>(s.target_g));
        const float diff = s.final_g - s.target_g;
        std::snprintf(diff_txt, sizeof(diff_txt), "(%+.1f)",
                      static_cast<double>(diff));
        // At-a-glance verdict: within 2 g of target reads good, beyond warns.
        diff_color = (diff < 0 ? -diff : diff) <= 2.0f ? ui::theme::ok()
                                                       : ui::theme::warn();
      } else {
        std::snprintf(result_txt, sizeof(result_txt), "%.1fg",
                      static_cast<double>(s.final_g));
        diff_txt[0] = '\0';
      }
      lv_obj_t* row = ui::history_add_row(stats_, when, mode_txt, result_txt,
                                          diff_txt, diff_color, dur_txt,
                                          screen_);
      lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(s.id)));
      lv_obj_add_event_cb(row, on_history_row, LV_EVENT_CLICKED, this);
      ++rows;
    }
  }

  // A filter pointing at a month that no longer exists falls back to All.
  if (!filter_month_seen) {
    stats_.history_filter_ym = 0;
    hist_built_filter_ = -1;  // force a rebuild on the next pass
  }

  // Calendar filter: "All" + one button per month with shots, newest first.
  ui::history_clear_filter_buttons(stats_);
  if (stats_.history_filter_list != nullptr) {
    lv_obj_t* all = ui::history_add_filter_button(
        stats_, "All", stats_.history_filter_ym == 0, screen_);
    if (all != nullptr) {
      lv_obj_set_user_data(all, nullptr);  // ym 0
      lv_obj_add_event_cb(all, on_history_filter, LV_EVENT_CLICKED, this);
    }
    for (int i = 0; i < n_months; ++i) {
      char label[16];
      std::snprintf(label, sizeof(label), "%s %d", kMonthNames[months[i] % 100 - 1],
                    months[i] / 100);
      lv_obj_t* btn = ui::history_add_filter_button(
          stats_, label, stats_.history_filter_ym == months[i], screen_);
      if (btn == nullptr) continue;
      lv_obj_set_user_data(btn,
                           reinterpret_cast<void*>(static_cast<uintptr_t>(months[i])));
      lv_obj_add_event_cb(btn, on_history_filter, LV_EVENT_CLICKED, this);
    }
  }

  if (rows == 0) {
    lv_obj_t* none = lv_label_create(stats_.history_list);
    lv_label_set_text(none, filter_ym == 0 ? "No shots recorded yet"
                                           : "No shots in this month");
    lv_obj_set_style_text_color(none, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(none, ui::font_dp(is_compact(screen_) ? 14 : 18), 0);
  } else if (truncated) {
    lv_obj_t* more = lv_label_create(stats_.history_list);
    lv_label_set_text(more, "Showing the latest 60");
    lv_obj_set_style_text_color(more, lv_color_hex(ui::theme::muted()), 0);
    lv_obj_set_style_text_font(more, ui::font_dp(is_compact(screen_) ? 12 : 14), 0);
    lv_obj_set_style_pad_ver(more, ui::dp(8), 0);
  }
}

void App::capture_shot_record(const core::BrewSnapshot& bsnap,
                              const core::ScaleSnapshot& snap) {
  if (shots_ == nullptr || !shots_->available()) return;
  const int64_t now_unix = clock_ != nullptr ? clock_->now_unix() : 0;
  if (now_unix == 0) return;  // no real date -> nothing sane to stamp

  if (shot_capture_ == nullptr) return;
  core::ShotRecord& r = *shot_capture_;
  r.n_samples = ui::export_shot_series(home_, r.samples,
                                       core::ShotRecord::kSampleCap);
  if (r.n_samples < 2) return;  // scale dark the whole shot -> nothing to keep

  // Wired shots tare at start (weight_g is already net); unwired shots never
  // tare, so subtract the detector's untared baseline — same convention as the
  // frozen review readout above.
  const float final_g =
      snap.weight_g - (bsnap.paddle_wired ? 0.0f : bsnap.start_weight_g);
  r.summary.id = 0;  // store-assigned
  r.summary.unix_time = now_unix;
  r.summary.duration_ms = bsnap.shot_ms;
  // Manual-mode shots have no target to score against; store 0 = untargeted.
  r.summary.target_g =
      bsnap.mode == core::ShotMode::kManual ? 0.0f : bsnap.target_weight_g;
  r.summary.final_g = final_g;
  r.summary.avg_gps = bsnap.shot_ms > 0
                          ? final_g / (static_cast<float>(bsnap.shot_ms) / 1000.0f)
                          : 0.0f;
  r.summary.mode = bsnap.mode;
  r.summary.wired = bsnap.paddle_wired;
  shots_->save(r);
  // The History list notices count() changed on its next visible refresh.
}

void App::open_reset_stats_modal() {
  if (shots_ == nullptr || !shots_->available()) return;
  lv_obj_t* card = open_modal(
      "Reset stats?",
      "The headline stats restart from now. Recorded shots stay on the SD "
      "card untouched.\n(Undo: delete Apollo2/stats_since.txt on a computer.)");
  lv_obj_t* row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, ui::dp(10), 0);
  modal_button(row, "Reset", ui::theme::warn(), on_reset_stats_confirm, this);
  modal_button(row, "Cancel", ui::theme::rail(), on_reset_stats_cancel, this);
}

// Stats > Info > "Diagnostic log": the tail of the in-RAM log ring in a
// scrollable box that LIVE-TAILS — a 1s timer re-snapshots the ring, and the
// view stays pinned to the newest lines unless the user has scrolled up to
// read. (The full ring is at http://<ip>/log.)
void App::open_log_modal() {
  lv_obj_t* card = open_modal("Diagnostic log", "");
  lv_obj_set_width(card, lv_pct(94));  // log lines are wide; use the screen

  lv_obj_t* box = lv_obj_create(card);
  lv_obj_remove_style_all(box);
  lv_obj_set_width(box, lv_pct(100));
  lv_obj_set_height(box, screen_.height * 58 / 100);
  lv_obj_set_style_bg_color(box, lv_color_hex(ui::theme::bg()), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(box, ui::dp(6), 0);
  lv_obj_set_style_pad_all(box, ui::dp(8), 0);
  lv_obj_add_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(box, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t* text = lv_label_create(box);
  lv_obj_set_width(text, lv_pct(100));
  lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(text, lv_color_hex(ui::theme::text()), 0);
  lv_obj_set_style_text_font(text, ui::font_dp(12), 0);

  log_box_ = box;
  log_label_ = text;
  refresh_log_modal();  // paint now, not a timer period later
  log_timer_ = lv_timer_create(
      [](lv_timer_t* t) {
        static_cast<App*>(lv_timer_get_user_data(t))->refresh_log_modal();
      },
      1000, this);

  modal_button(card, "Close", ui::theme::rail(), on_log_modal_close, this);
}

void App::refresh_log_modal() {
  if (log_box_ == nullptr || log_label_ == nullptr) return;
  // A few KB is plenty for on-glass reading and keeps the label's layout
  // cost down; the label copies the text, so the staging buffer is transient.
  constexpr size_t kTailBytes = 4 * 1024;
  char* tail = static_cast<char*>(lv_malloc(kTailBytes));
  if (tail == nullptr) return;
  const size_t n = core::log_ring().snapshot_tail(tail, kTailBytes);
  // Follow the tail only while the user is already at (or near) the bottom —
  // scrolling up to read pauses the auto-scroll until they return.
  const bool follow = lv_obj_get_scroll_bottom(log_box_) <= ui::dp(12);
  ui::set_text(log_label_, n > 0 ? tail : "(log is empty)");  // no-op if unchanged
  lv_free(tail);
  if (follow) {
    lv_obj_update_layout(log_box_);
    lv_obj_scroll_to_y(log_box_, LV_COORD_MAX, LV_ANIM_OFF);
  }
}

void App::confirm_reset_stats() {
  if (shots_ != nullptr && clock_ != nullptr) {
    const int64_t now = clock_->now_unix();
    if (now != 0) shots_->set_stats_since(now);
  }
  close_modal();
  hist_built_count_ = -1;  // repaint metrics + caption on the next pass
  update_history_view();
}

void App::open_shot_card(uint32_t id) {
  if (shots_ == nullptr || shot_view_ == nullptr ||
      !shots_->read(id, *shot_view_)) return;
  close_modal();
  // Near-full-screen overlay; tap anywhere outside (or on the header) closes.
  lv_obj_t* bg = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(bg);
  lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
  lv_obj_set_style_bg_color(bg, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(bg, LV_OPA_70, 0);
  lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(bg, on_shot_modal_close, LV_EVENT_CLICKED, this);
  modal_ = bg;
  if (tabview_ != nullptr) lv_obj_add_flag(tabview_, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* holder = lv_obj_create(bg);
  lv_obj_remove_style_all(holder);
  lv_obj_add_flag(holder, LV_OBJ_FLAG_EVENT_BUBBLE);  // card taps bubble to bg
  lv_obj_set_size(holder, lv_pct(96), lv_pct(94));
  lv_obj_center(holder);
  lv_obj_t* delete_btn = nullptr;
  ui::build_shot_card(holder, *shot_view_, screen_,
                      clock_ != nullptr && clock_->use_24h(), &delete_btn);
  pending_delete_id_ = id;
  if (delete_btn != nullptr)
    lv_obj_add_event_cb(delete_btn, on_shot_delete, LV_EVENT_CLICKED, this);
}

void App::open_delete_shot_modal() {
  if (shots_ == nullptr) return;
  // open_modal closes the shot-card modal; pending_delete_id_ carries the id.
  lv_obj_t* card = open_modal(
      "Delete this shot?",
      "Removes it from the SD card. The headline stats recalculate without "
      "it. This can't be undone.");
  lv_obj_t* row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, ui::dp(10), 0);
  modal_button(row, "Delete", ui::theme::alert(), on_delete_shot_confirm, this);
  modal_button(row, "Cancel", ui::theme::rail(), on_delete_shot_cancel, this);
}

void App::confirm_delete_shot() {
  if (shots_ != nullptr && pending_delete_id_ != 0)
    shots_->remove(pending_delete_id_);
  pending_delete_id_ = 0;
  close_modal();
  hist_built_count_ = -1;  // rebuild the list + metrics on the next pass
  update_history_view();
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
  if (!b.present) {
    batt_hist_count_ = 0;  // USB power / no battery -> no window
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
    std::snprintf(rt, n, b.usb ? "USB power" : "-");
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
    // Uptime is time-since-boot off the LVGL tick (wraps at ~49 days).
    char up[20];
    const uint32_t up_s = lv_tick_get() / 1000u;
    if (up_s >= 86400u)
      std::snprintf(up, sizeof(up), "%ud %uh", static_cast<unsigned>(up_s / 86400u),
                    static_cast<unsigned>(up_s % 86400u / 3600u));
    else if (up_s >= 3600u)
      std::snprintf(up, sizeof(up), "%uh %um", static_cast<unsigned>(up_s / 3600u),
                    static_cast<unsigned>(up_s % 3600u / 60u));
    else if (up_s >= 60u)
      std::snprintf(up, sizeof(up), "%um %us", static_cast<unsigned>(up_s / 60u),
                    static_cast<unsigned>(up_s % 60u));
    else
      std::snprintf(up, sizeof(up), "%us", static_cast<unsigned>(up_s));

    // IP only means anything while the station is actually connected.
    const char* ip = (network_ != nullptr &&
                      network_->status() == core::NetState::Connected)
                         ? network_->ip()
                         : "";
    const char* vals[kStatsInfoRows] = {rfw,
                                        batt_runtime_text_,
                                        up,
                                        ip,
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

  if (stats_.active == kStatsHistory) {
    update_history_view();
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

  // Backflush needs the drive line: grey the entry when the harness setting is
  // off (the screen itself explains why if they get there another way).
  if (settings_.backflush_btn != nullptr && brew_ != nullptr)
    set_clickable(settings_.backflush_btn, brew_->snapshot().relay);

  // WiFi status line (Device page): reflect the live connection state.
  if (network_ != nullptr && settings_.wifi_status != nullptr) {
    // The enable switch can change behind the UI's back: the setup portal
    // turns WiFi on when credentials are saved. Sync it here (guarded — a
    // programmatic state change fires no VALUE_CHANGED, so no feedback loop).
    if (settings_.wifi_switch != nullptr) {
      const bool en = network_->enabled();
      if (en != lv_obj_has_state(settings_.wifi_switch, LV_STATE_CHECKED)) {
        if (en) lv_obj_add_state(settings_.wifi_switch, LV_STATE_CHECKED);
        else lv_obj_remove_state(settings_.wifi_switch, LV_STATE_CHECKED);
      }
    }
    char buf[64];
    uint32_t color = ui::theme::muted();
    switch (network_->status()) {
      case core::NetState::Disabled:
        std::snprintf(buf, sizeof(buf), "Off");
        break;
      case core::NetState::Connecting:
        std::snprintf(buf, sizeof(buf), "Connecting" LV_SYMBOL_WIFI);
        break;
      case core::NetState::Connected:
        std::snprintf(buf, sizeof(buf), "%s  %s", network_->ssid(), network_->ip());
        color = ui::theme::ok();
        break;
      case core::NetState::Failed:
        std::snprintf(buf, sizeof(buf), "Connection failed");
        color = ui::theme::alert();
        break;
    }
    ui::set_text(settings_.wifi_status, buf);
    ui::set_text_color(settings_.wifi_status, color);
  }

  if (provisioner_ == nullptr) return;

  // Saved-machine row: name + Forget, plus one of: Setup (no token yet) or
  // Connect/Disconnect (tokened) in the same slot.
  const std::string saved = provisioner_->saved_name();
  if (saved.empty()) {
    lv_obj_add_flag(settings_.saved_row, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(settings_.saved_row, LV_OBJ_FLAG_HIDDEN);
    ui::set_text(settings_.saved_label, saved.c_str());
    const bool tokened = provisioner_->has_token();
    if (tokened) {
      lv_obj_add_flag(settings_.setup_btn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(settings_.connect_btn, LV_OBJ_FLAG_HIDDEN);
      const bool on = provisioner_->connect_enabled();
      ui::set_text(settings_.connect_label, on ? "Disconnect" : "Connect");
      ui::set_bg_color(settings_.connect_btn,
                       on ? ui::theme::rail() : ui::theme::accent());
    } else {
      lv_obj_remove_flag(settings_.setup_btn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(settings_.connect_btn, LV_OBJ_FLAG_HIDDEN);
    }
  }


  const bool scanning = provisioner_->scanning();
  const std::vector<core::ScanResult> results = provisioner_->scan_results();
  const int count = static_cast<int>(results.size());
  if (settings_.last_scanning && !scanning) settings_.scan_done = true;

  if (scanning) {
    ui::set_text(settings_.status, "Scanning...");
  } else if (count == 0) {
    // A finished-but-empty scan needs to say so — silently reverting to the
    // idle hint reads as "the scan never ran" (user-reported while the scale
    // link had the radio; the scan preempts it now, but the answer can still
    // honestly be "nothing found").
    ui::set_text(settings_.status, settings_.scan_done
                                       ? "No machines found - power it on nearby, "
                                         "then Scan again"
                                       : "Tap Scan to find your machine");
  } else {
    ui::set_text(settings_.status, "Tap a machine to save it");
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
    lv_obj_set_style_text_font(lbl, ui::font_dp(14), 0);
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
void set_review_label(ui::SettingsWidgets& s) {
  if (s.review_value == nullptr) return;
  char b[16];
  std::snprintf(b, sizeof(b), "%d s", s.review_hold_s);
  lv_label_set_text(s.review_value, b);
}
void set_lead_in_label(ui::SettingsWidgets& s) {
  if (s.lead_value == nullptr) return;
  char b[16];
  std::snprintf(b, sizeof(b), "%d s", s.detect_lead_in_s);
  lv_label_set_text(s.lead_value, b);
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
    ui::set_text(settings_.scale_saved_label, saved.c_str());
    lv_obj_remove_flag(settings_.scale_connect_btn, LV_OBJ_FLAG_HIDDEN);
    const bool on = scale_provisioner_->connect_enabled();
    ui::set_text(settings_.scale_connect_label, on ? "Disconnect" : "Connect");
    ui::set_bg_color(settings_.scale_connect_btn,
                     on ? ui::theme::rail() : ui::theme::accent());
  }

  // Scale > Device settings page: discoverable in every state.
  // Nothing saved -> pairing hint; saved but offline -> the model's rows,
  // disabled with "--", plus a Connect prompt; connected -> live values.
  // Recomputed from live state on every refresh (like the Home shot lockout),
  // so no path can leave a row stuck disabled or a stale value shown.
  if (settings_.scale_dev_hint != nullptr && scale_ != nullptr) {
    const auto show = [](lv_obj_t* obj, bool on) {
      if (on == lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        if (on)
          lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else
          lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
      }
    };
    const bool have_scale = !saved.empty();
    const bool connected = scale_->snapshot().connected;
    const int count = have_scale ? scale_->device_setting_count() : 0;
    const bool shot = brew_ != nullptr && core::shot_in_flight(brew_->snapshot());
    show(settings_.scale_dev_hint, !have_scale);
    show(settings_.scale_dev_connect_row, have_scale && !connected);
    if (have_scale && !connected)
      ui::set_text(settings_.scale_dev_connect_label,
                   scale_provisioner_->connect_enabled() ? "Connecting..." : "Connect");
    for (int i = 0; i < core::kMaxScaleSettings; ++i) {
      const bool vis = i < count;
      show(settings_.scale_dev_rows[i], vis);
      if (!vis) continue;
      const core::ScaleSettingDesc d = scale_->device_setting(i);
      ui::set_text(settings_.scale_dev_labels[i], d.label);
      const int v = connected ? scale_->device_setting_value(i) : -1;
      ui::set_text(settings_.scale_dev_values[i],
                   (v >= 0 && v < d.option_count) ? d.option_labels[v] : "--");
      // Read-only rows (e.g. the Lunar's Mode) drop the button chrome
      // entirely — bare right-aligned text reads as information, while the
      // card-background chip is the affordance for "tap to change".
      if (d.read_only) {
        lv_obj_set_style_bg_opa(settings_.scale_dev_btns[i], LV_OPA_TRANSP, 0);
        lv_obj_remove_state(settings_.scale_dev_btns[i], LV_STATE_DISABLED);
        lv_obj_remove_flag(settings_.scale_dev_btns[i], LV_OBJ_FLAG_CLICKABLE);
      } else {
        lv_obj_set_style_bg_opa(settings_.scale_dev_btns[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(settings_.scale_dev_btns[i], LV_OBJ_FLAG_CLICKABLE);
        if (connected && !shot)
          lv_obj_remove_state(settings_.scale_dev_btns[i], LV_STATE_DISABLED);
        else
          lv_obj_add_state(settings_.scale_dev_btns[i], LV_STATE_DISABLED);
      }
    }
  }

  const bool scanning = scale_provisioner_->scanning();
  const std::vector<core::ScanResult> results = scale_provisioner_->scan_results();
  const int count = static_cast<int>(results.size());
  if (settings_.scale_last_scanning && !scanning) settings_.scale_scan_done = true;

  if (scanning) {
    ui::set_text(settings_.scale_status, "Scanning...");
  } else if (count == 0) {
    ui::set_text(settings_.scale_status, settings_.scale_scan_done
                                             ? "No scales found - wake the scale, "
                                               "then Scan again"
                                             : "Tap Scan to find your scale");
  } else {
    ui::set_text(settings_.scale_status, "Tap a scale to save it");
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
    lv_obj_set_style_text_font(lbl, ui::font_dp(14), 0);
    lv_obj_center(lbl);
  }
  set_target_label(settings_);
  set_review_label(settings_);
  set_lead_in_label(settings_);
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
  // Same mid-shot refusal as tare_scale — also covers the Settings-tab
  // Connect/Disconnect button, which shares this path.
  if (brew_ != nullptr && core::shot_in_flight(brew_->snapshot())) return;
  scale_provisioner_->set_connect_enabled(!scale_provisioner_->connect_enabled());
  update_scale_view();
}

void App::target_adjust(int dir) {
  settings_.target_g = clampf(settings_.target_g + dir, 5.0f, 120.0f);
  set_target_label(settings_);
  // Reflect on the Home scale panel immediately (don't wait for the next refresh).
  if (home_.scale_target != nullptr && home_.target_minus != nullptr) {
    char tb[16];
    std::snprintf(tb, sizeof(tb), "%.0f g", static_cast<double>(settings_.target_g));
    lv_label_set_text(home_.scale_target, tb);
  }
  if (brew_ != nullptr) brew_->set_target_weight_g(settings_.target_g);
}

void App::review_hold_adjust(int dir) {
  int v = settings_.review_hold_s + dir * 5;  // 5s steps
  if (v < 5) v = 5;
  if (v > 120) v = 120;
  settings_.review_hold_s = v;
  set_review_label(settings_);
  if (brew_ != nullptr) brew_->set_review_hold_s(v);
}

void App::detect_lead_in_adjust(int dir) {
  int v = settings_.detect_lead_in_s + dir;
  if (v < 0) v = 0;
  if (v > 10) v = 10;
  settings_.detect_lead_in_s = v;
  set_lead_in_label(settings_);
  if (brew_ != nullptr) brew_->set_detect_lead_in_s(v);
}

}  // namespace ui
