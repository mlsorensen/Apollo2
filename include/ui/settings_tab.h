#pragma once

#include <lvgl.h>

#include "ui/screen.h"

// Settings tab: an lv_menu drill-in, grouped by device with short leaf pages
// under each (short = little to no scrolling, the whole point of the split):
//   - Micra  -> Bluetooth (scan/save/connect/forget/auto-connect)
//               | Controls (Brew + Boiler) | Cleaning
//   - Scale  -> Bluetooth (scan/save/connect/forget) | Settings (Target weight)
//   - Device -> Display (brightness/theme/units) | Time & date | WiFi
// ui::App owns it — builds the frame here, navigates pages, and (re)populates the
// scan lists. lv_menu provides the page stack + back navigation so we don't
// hand-roll it.

namespace ui {

// Navigation targets (a page each). select_settings_section() loads the page;
// the sim + the post-theme-rebuild restore use these.
enum SettingsSection {
  kSectionMicra = 0,       // Micra chooser (Bluetooth | Controls | Cleaning)
  kSectionMicraBt,         // Micra > Bluetooth (connection + auto connect)
  kSectionMicraControls,   // Micra > Controls (Brew + Steam Boiler)
  kSectionMicraCleaning,   // Micra > Cleaning (flush settings + backflush)
  kSectionScale,           // Scale chooser
  kSectionScaleBt,         // Scale > Bluetooth
  kSectionScaleSettings,   // Scale > Settings (Target weight)
  kSectionDevice,          // Device chooser (Display | Time & date | WiFi)
  kSectionDeviceDisplay,   // Device > Display (brightness/dim/theme/units/sound)
  kSectionDeviceTime,      // Device > Time & date (clock + calendar steppers)
  kSectionDeviceWifi,      // Device > WiFi (enable/setup/timezone/NTP)
  kSectionCount
};

struct SettingsWidgets {
  lv_obj_t* menu = nullptr;
  lv_obj_t* root_page = nullptr;
  lv_obj_t* micra_page = nullptr;           // chooser: Bluetooth | Controls | Cleaning
  lv_obj_t* micra_bt_page = nullptr;        // connection + auto connect
  lv_obj_t* micra_controls_page = nullptr;  // brew + boiler
  lv_obj_t* micra_cleaning_page = nullptr;  // flush settings + backflush
                                            // (paddle-capable boards only)
  lv_obj_t* scale_page = nullptr;           // chooser: Bluetooth | Settings
  lv_obj_t* scale_bt_page = nullptr;        // connection
  lv_obj_t* scale_settings_page = nullptr;  // target weight
  lv_obj_t* device_page = nullptr;          // chooser: Display | Time & date | WiFi
  lv_obj_t* device_display_page = nullptr;  // brightness/dim/theme/units/sound
  lv_obj_t* device_time_page = nullptr;     // clock + calendar steppers
  lv_obj_t* device_wifi_page = nullptr;     // enable/status/setup/timezone/NTP

  // --- Micra connection (Bluetooth) ---------------------------------------
  lv_obj_t* saved_row = nullptr;    // "Saved: <name>  [Setup/Connect] [Forget]"
  lv_obj_t* saved_label = nullptr;
  lv_obj_t* setup_btn = nullptr;    // token WiFi setup — shown if no token yet
  lv_obj_t* connect_btn = nullptr;  // Connect/Disconnect — shown once tokened
  lv_obj_t* connect_label = nullptr;
  lv_obj_t* forget_btn = nullptr;
  lv_obj_t* scan_btn = nullptr;
  lv_obj_t* status = nullptr;
  lv_obj_t* list = nullptr;  // container the result rows are added to
  int last_count = -1;       // change detection so the list only rebuilds on change
  bool last_scanning = false;
  bool scan_done = false;    // a scan has finished (shows "none found" vs the hint)

  // --- Scale connection (mirrors the Micra rows; no token) -----------------
  lv_obj_t* scale_saved_row = nullptr;
  lv_obj_t* scale_saved_label = nullptr;
  lv_obj_t* scale_connect_btn = nullptr;
  lv_obj_t* scale_connect_label = nullptr;
  lv_obj_t* scale_forget_btn = nullptr;
  lv_obj_t* scale_scan_btn = nullptr;
  lv_obj_t* scale_status = nullptr;
  lv_obj_t* scale_list = nullptr;
  int scale_last_count = -1;
  bool scale_last_scanning = false;
  bool scale_scan_done = false;

  // Scale target weight stepper (grams)
  lv_obj_t* target_minus = nullptr;
  lv_obj_t* target_plus = nullptr;
  lv_obj_t* target_value = nullptr;
  float target_g = 36.0f;
  // Shot-review hold stepper (seconds the frozen graph lingers before auto-reset)
  lv_obj_t* review_minus = nullptr;
  lv_obj_t* review_plus = nullptr;
  lv_obj_t* review_value = nullptr;
  int review_hold_s = 30;
  // Detect lead-in stepper (seconds of preinfusion before the detector's flow
  // onset; backdates detect-mode shots to lever-on)
  lv_obj_t* lead_minus = nullptr;
  lv_obj_t* lead_plus = nullptr;
  lv_obj_t* lead_value = nullptr;
  int detect_lead_in_s = 3;
  // Shot-graph smoothing cycle button (Off / Light / Medium / Strong)
  lv_obj_t* smooth_btn = nullptr;
  lv_obj_t* smooth_value = nullptr;
  lv_obj_t* drop_neg_flow_switch = nullptr;  // on = clamp negative g/s on the graph
  lv_obj_t* scope_graph_switch = nullptr;    // on = oscilloscope sweep, off = scroll
  lv_obj_t* perf_overlay_switch = nullptr;   // on = show LVGL FPS/CPU overlay
  lv_obj_t* restart_btn = nullptr;           // soft reboot (display-glitch escape hatch)
  lv_obj_t* clean_lock_btn = nullptr;        // 30 s touch lockout for wiping the screen
  lv_obj_t* auto_connect_switch = nullptr;   // Micra > Bluetooth: connect to the
                                             // saved machine at boot
  lv_obj_t* chime_vol_btn = nullptr;         // Micra > Controls: warm-up chime
  lv_obj_t* chime_vol_value = nullptr;       // level cycle (Off / 25 / 50 / 75
                                             // / 100%); audio boards only
  lv_obj_t* chime_mel_btn = nullptr;         // Micra > Controls: warm-up chime
  lv_obj_t* chime_mel_value = nullptr;       // melody cycle (Off / Blue / Pink
                                             // / ...); audio boards only
  lv_obj_t* wired_paddle_switch = nullptr;   // Micra: paddle harness in use (off =
                                             // detector-driven "unwired" shots);
                                             // only built on paddle-capable boards
  lv_obj_t* flush_btn = nullptr;             // Micra: auto-flush cycle (Off / 3 s / 6 s);
  lv_obj_t* flush_value = nullptr;           // only built on paddle-capable boards
  lv_obj_t* flush_delay_row = nullptr;       // Micra: cup-off -> flush pause cycle
  lv_obj_t* flush_delay_btn = nullptr;       // (3/6/9/15 s); row hidden while the
  lv_obj_t* flush_delay_value = nullptr;     // flush itself is Off
  lv_obj_t* backflush_btn = nullptr;         // Micra > Cleaning: opens the
                                             // backflush screen

  // --- Brew temp stepper (continuous, tenths) ------------------------------
  lv_obj_t* brew_minus = nullptr;
  lv_obj_t* brew_plus = nullptr;
  lv_obj_t* brew_value = nullptr;
  float brew_target = 0.0f;
  bool brew_dirty = false;  // uncommitted local edit; until set, tracks the machine

  // --- Boiler/steam stepper (3 discrete levels) + on/off switch ------------
  lv_obj_t* steam_switch = nullptr;
  lv_obj_t* boiler_minus = nullptr;
  lv_obj_t* boiler_plus = nullptr;
  lv_obj_t* boiler_value = nullptr;
  lv_obj_t* boiler_sub = nullptr;  // "Level N"
  int boiler_level = 0;            // 0..2 -> kSteamLevelsC
  bool boiler_dirty = false;
  bool steam_enabled = true;
  bool steam_enable_dirty = false;

  // --- Device section ------------------------------------------------------
  lv_obj_t* brightness_minus = nullptr;
  lv_obj_t* brightness_plus = nullptr;
  lv_obj_t* brightness_value = nullptr;
  int brightness = 100;

  // Screen-dim timeout cycle button (Off / 15 min / 30 min)
  lv_obj_t* dim_btn = nullptr;
  lv_obj_t* dim_value = nullptr;
  int screen_timeout_min = 0;

  // Time & date dropdowns ("Time" = hour/minute, "Date" = month/day/year; a
  // stepper per field made the page scroll). App owns the options + selection
  // (hour labels depend on 12/24h, day count on the month; shot history needs
  // a real date — NTP sets all of this automatically when WiFi is on, these
  // are the offline path).
  lv_obj_t* hour_dd = nullptr;
  lv_obj_t* minute_dd = nullptr;
  lv_obj_t* month_dd = nullptr;
  lv_obj_t* day_dd = nullptr;
  lv_obj_t* year_dd = nullptr;
  lv_obj_t* clock_mode_switch = nullptr;  // on = 24-hour, off = 12-hour
  lv_obj_t* units_switch = nullptr;       // on = Fahrenheit, off = Celsius
  int set_hour = 12;
  int set_minute = 0;
  int set_year = 2026;  // seed when the clock has no real date yet
  int set_month = 1;
  int set_day = 1;
  bool clock_24h = true;

  lv_obj_t* theme_btn = nullptr;
  lv_obj_t* theme_value = nullptr;
  int theme_index = 0;

  // --- WiFi (station + NTP time) ---
  lv_obj_t* wifi_switch = nullptr;      // enable/disable joining home WiFi
  lv_obj_t* wifi_status = nullptr;      // "Connected  192.168.1.42" / "Not connected"
  lv_obj_t* wifi_setup_btn = nullptr;   // "Set up" -> AP credential portal
  lv_obj_t* wifi_forget_btn = nullptr;  // clear saved credentials
  lv_obj_t* tz_dropdown = nullptr;      // timezone picker (POSIX TZ under the
                                        // hood; lives on the Time & date page)
  lv_obj_t* ntp_switch = nullptr;       // sync clock from NTP while connected (default on)
  lv_obj_t* click_sound_switch = nullptr;  // button-press click (audio boards only)
};

// with_wired_paddle: build the Micra "Wired paddle" switch (paddle-capable
// boards only — elsewhere unwired mode isn't a choice, it's all there is).
void build_settings_tab(lv_obj_t* parent, const ScreenProfile& screen,
                        bool with_brightness, bool with_sound,
                        bool with_wired_paddle, SettingsWidgets& out);

// Navigate to a section's page (kSectionMicra / kSectionScale / kSectionDevice).
void settings_select_section(SettingsWidgets& w, int section);

// The lv_menu page a section navigates to (nullptr before build). App uses it
// to save/restore a page's scroll position across a theme rebuild.
lv_obj_t* settings_section_page(const SettingsWidgets& w, int section);

}  // namespace ui
