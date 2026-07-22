#pragma once

#include <lvgl.h>

#include "ui/screen.h"

// Settings tab: an lv_menu drill-in, grouped by device with a Bluetooth/Settings
// split under each:
//   - Micra  -> Bluetooth (scan/save/connect/forget) | Settings (Brew + Boiler)
//   - Scale  -> Bluetooth (scan/save/connect/forget) | Settings (Target weight)
//   - Device -> brightness / clock / units / theme (a leaf)
// ui::App owns it — builds the frame here, navigates pages, and (re)populates the
// scan lists. lv_menu provides the page stack + back navigation so we don't
// hand-roll it.

namespace ui {

// Navigation targets (a page each). select_settings_section() loads the page;
// the sim + the post-theme-rebuild restore use these.
enum SettingsSection {
  kSectionMicra = 0,       // Micra chooser (Bluetooth | Settings)
  kSectionMicraBt,         // Micra > Bluetooth
  kSectionMicraSettings,   // Micra > Settings (Brew + Steam Boiler)
  kSectionScale,           // Scale chooser
  kSectionScaleBt,         // Scale > Bluetooth
  kSectionScaleSettings,   // Scale > Settings (Target weight)
  kSectionDevice,          // Device (leaf)
  kSectionCount
};

struct SettingsWidgets {
  lv_obj_t* menu = nullptr;
  lv_obj_t* root_page = nullptr;
  lv_obj_t* micra_page = nullptr;           // chooser: Bluetooth | Settings
  lv_obj_t* micra_bt_page = nullptr;        // connection
  lv_obj_t* micra_settings_page = nullptr;  // brew + boiler
  lv_obj_t* scale_page = nullptr;           // chooser: Bluetooth | Settings
  lv_obj_t* scale_bt_page = nullptr;        // connection
  lv_obj_t* scale_settings_page = nullptr;  // target weight
  lv_obj_t* device_page = nullptr;

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
  // Shot-graph smoothing cycle button (Off / Light / Medium / Strong)
  lv_obj_t* smooth_btn = nullptr;
  lv_obj_t* smooth_value = nullptr;
  lv_obj_t* drop_neg_flow_switch = nullptr;  // on = clamp negative g/s on the graph
  lv_obj_t* scope_graph_switch = nullptr;    // on = oscilloscope sweep, off = scroll
  lv_obj_t* perf_overlay_switch = nullptr;   // on = show LVGL FPS/CPU overlay
  lv_obj_t* restart_btn = nullptr;           // soft reboot (display-glitch escape hatch)
  lv_obj_t* auto_connect_switch = nullptr;   // Micra: connect to saved machine at boot
  lv_obj_t* wired_paddle_switch = nullptr;   // Micra: paddle harness in use (off =
                                             // detector-driven "unwired" shots);
                                             // only built on paddle-capable boards
  lv_obj_t* flush_btn = nullptr;             // Micra: auto-flush cycle (Off / 3 s / 6 s);
  lv_obj_t* flush_value = nullptr;           // only built on paddle-capable boards

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

  lv_obj_t* hour_minus = nullptr;
  lv_obj_t* hour_plus = nullptr;
  lv_obj_t* hour_value = nullptr;
  lv_obj_t* minute_minus = nullptr;
  lv_obj_t* minute_plus = nullptr;
  lv_obj_t* minute_value = nullptr;
  lv_obj_t* clock_mode_switch = nullptr;  // on = 24-hour, off = 12-hour
  lv_obj_t* units_switch = nullptr;       // on = Fahrenheit, off = Celsius
  int set_hour = 12;
  int set_minute = 0;
  bool clock_24h = true;

  lv_obj_t* theme_btn = nullptr;
  lv_obj_t* theme_value = nullptr;
  int theme_index = 0;

  // --- WiFi (station + NTP time) ---
  lv_obj_t* wifi_switch = nullptr;      // enable/disable joining home WiFi
  lv_obj_t* wifi_status = nullptr;      // "Connected  192.168.1.42" / "Not connected"
  lv_obj_t* wifi_setup_btn = nullptr;   // "Set up" -> AP credential portal
  lv_obj_t* wifi_forget_btn = nullptr;  // clear saved credentials
  lv_obj_t* tz_dropdown = nullptr;      // timezone picker (POSIX TZ under the hood)
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

}  // namespace ui
