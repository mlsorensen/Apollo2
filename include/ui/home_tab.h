#pragma once

#include <lvgl.h>

#include "core/machine.h"
#include "ui/screen.h"

// Builds the Home tab's content into `parent` (the tab page handed back by
// lv_tabview_add_tab), sized for the given screen profile. Internal to the ui/
// layer — app.cpp owns the tab shell and calls this. Kept in its own file so
// each tab stays small and testable.

namespace ui {

void build_home_tab(lv_obj_t* parent, const core::MachineSnapshot& state,
                    const ScreenProfile& screen);

}  // namespace ui
