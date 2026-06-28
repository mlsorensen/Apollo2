#pragma once

#include <lvgl.h>

#include "ui/screen.h"

// Settings tab: a Scan button, a status line, and a scrollable list of found
// machines. ui::App owns it — it builds the static frame here and (re)populates
// the list from scan results, wiring each row to save its MAC.

namespace ui {

struct SettingsWidgets {
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
};

void build_settings_tab(lv_obj_t* parent, const ScreenProfile& screen,
                        SettingsWidgets& out);

}  // namespace ui
