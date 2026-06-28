#pragma once

#include <lvgl.h>

#include "core/machine.h"
#include "ui/screen.h"

// Home tab: built once, then updated in place from new snapshots (no rebuild,
// so tab state and touch targets are preserved). Internal to the ui/ layer —
// ui::App owns the tab shell and wires the power button to a command.

namespace ui {

// Handles to the Home tab's dynamic widgets, retained for live updates.
struct HomeWidgets {
  lv_obj_t* brew_value = nullptr;
  lv_obj_t* brew_set = nullptr;
  lv_obj_t* boiler_value = nullptr;
  lv_obj_t* boiler_set = nullptr;
  lv_obj_t* status_label = nullptr;
  lv_obj_t* status_dot = nullptr;
  lv_obj_t* power_btn = nullptr;
  lv_obj_t* power_label = nullptr;
};

// Build the Home tab content into `parent`, sized for `screen`; fills `out`
// with widget handles and sets initial values from `state`.
void build_home_tab(lv_obj_t* parent, const core::MachineSnapshot& state,
                    const ScreenProfile& screen, HomeWidgets& out);

// Apply a snapshot to already-built widgets (live update, no rebuild).
void update_home(const HomeWidgets& w, const core::MachineSnapshot& state);

}  // namespace ui
