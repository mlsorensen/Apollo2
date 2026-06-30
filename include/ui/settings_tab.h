#pragma once

#include <lvgl.h>

#include "ui/screen.h"

// Settings tab: a segmented selector (Bluetooth / Brew / Boiler) that switches
// between section panels. ui::App owns it — builds the frame here, switches
// sections, and (re)populates the Bluetooth panel's scan list.

namespace ui {

enum SettingsSection {
  kSectionBluetooth = 0,
  kSectionBrew,
  kSectionBoiler,
  kSectionDevice,  // device prefs: brightness now, time/theme later
  kSectionCount
};

struct SettingsWidgets {
  lv_obj_t* seg[kSectionCount] = {nullptr, nullptr, nullptr};      // selector buttons
  lv_obj_t* panel[kSectionCount] = {nullptr, nullptr, nullptr};    // section panels
  int active = kSectionBluetooth;

  // Bluetooth panel widgets:
  lv_obj_t* saved_row = nullptr;    // "Saved: <name>  [Setup] [Forget]" if saved
  lv_obj_t* saved_label = nullptr;
  lv_obj_t* setup_btn = nullptr;    // token WiFi setup — shown if no token yet
  lv_obj_t* connect_btn = nullptr;  // Connect/Disconnect — shown once tokened
  lv_obj_t* connect_label = nullptr;
  lv_obj_t* forget_btn = nullptr;
  lv_obj_t* scan_btn = nullptr;
  lv_obj_t* status = nullptr;
  lv_obj_t* list = nullptr;  // container the result rows are added to

  // change detection so the list is only rebuilt when results change
  int last_count = -1;
  bool last_scanning = false;

  // Brew temp stepper (continuous, tenths)
  lv_obj_t* brew_minus = nullptr;
  lv_obj_t* brew_plus = nullptr;
  lv_obj_t* brew_value = nullptr;
  float brew_target = 0.0f;
  bool brew_dirty = false;  // uncommitted local edit; until set, the panel tracks
                            // the machine's polled target

  // Boiler/steam stepper (3 discrete levels) + on/off switch
  lv_obj_t* steam_switch = nullptr;
  lv_obj_t* boiler_minus = nullptr;
  lv_obj_t* boiler_plus = nullptr;
  lv_obj_t* boiler_value = nullptr;
  lv_obj_t* boiler_sub = nullptr;  // "Level N"
  int boiler_level = 0;            // 0..2 -> kSteamLevelsC
  bool boiler_dirty = false;       // uncommitted local target edit
  bool steam_enabled = true;       // local on/off (tracks machine unless dirty)
  bool steam_enable_dirty = false; // toggled, awaiting machine confirmation

  // Device section: brightness + a clock setter (hour/minute steppers)
  lv_obj_t* brightness_minus = nullptr;
  lv_obj_t* brightness_plus = nullptr;
  lv_obj_t* brightness_value = nullptr;
  int brightness = 100;

  lv_obj_t* hour_minus = nullptr;
  lv_obj_t* hour_plus = nullptr;
  lv_obj_t* hour_value = nullptr;
  lv_obj_t* minute_minus = nullptr;
  lv_obj_t* minute_plus = nullptr;
  lv_obj_t* minute_value = nullptr;
  lv_obj_t* clock_mode_switch = nullptr;  // on = 24-hour, off = 12-hour
  lv_obj_t* units_switch = nullptr;       // on = Fahrenheit, off = Celsius
  int set_hour = 12;    // local edit buffer, seeded from the clock on section entry
  int set_minute = 0;
  bool clock_24h = true;  // display pref; mirrors IClock::use_24h()

  lv_obj_t* theme_btn = nullptr;    // color scheme: tap the name to cycle
  lv_obj_t* theme_value = nullptr;
  int theme_index = 0;
};

void build_settings_tab(lv_obj_t* parent, const ScreenProfile& screen,
                        SettingsWidgets& out);

// Show one section's panel and highlight its selector button.
void settings_select_section(SettingsWidgets& w, int section);

}  // namespace ui
