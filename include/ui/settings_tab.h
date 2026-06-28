#pragma once

#include <lvgl.h>

#include "ui/screen.h"

// Settings tab: a segmented selector (Bluetooth / Brew / Boiler) that switches
// between section panels. ui::App owns it — builds the frame here, switches
// sections, and (re)populates the Bluetooth panel's scan list.

namespace ui {

enum SettingsSection { kSectionBluetooth = 0, kSectionBrew, kSectionBoiler, kSectionCount };

struct SettingsWidgets {
  lv_obj_t* seg[kSectionCount] = {nullptr, nullptr, nullptr};      // selector buttons
  lv_obj_t* panel[kSectionCount] = {nullptr, nullptr, nullptr};    // section panels
  int active = kSectionBluetooth;

  // Bluetooth panel widgets:
  lv_obj_t* saved_row = nullptr;    // "Saved: <name>  [Setup] [Forget]" if saved
  lv_obj_t* saved_label = nullptr;
  lv_obj_t* setup_btn = nullptr;    // token WiFi setup — shown if no token yet
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
  bool brew_edited = false;  // once true, local value is authoritative (stop syncing)
  bool brew_dirty = false;   // edited but not yet written to the machine

  // Boiler/steam stepper (3 discrete levels)
  lv_obj_t* boiler_minus = nullptr;
  lv_obj_t* boiler_plus = nullptr;
  lv_obj_t* boiler_value = nullptr;
  lv_obj_t* boiler_sub = nullptr;  // "Level N"
  int boiler_level = 0;            // 0..2 -> kSteamLevelsC
  bool boiler_edited = false;      // once true, local value is authoritative
  bool boiler_dirty = false;       // edited but not yet written to the machine
};

void build_settings_tab(lv_obj_t* parent, const ScreenProfile& screen,
                        SettingsWidgets& out);

// Show one section's panel and highlight its selector button.
void settings_select_section(SettingsWidgets& w, int section);

}  // namespace ui
