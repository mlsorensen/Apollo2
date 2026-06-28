#pragma once

#include "core/machine.h"
#include "ui/screen.h"

// Public UI entry point. Builds the whole on-screen application — the tab bar
// and every tab's content — into the active LVGL screen, laid out for the given
// screen profile and populated from a machine snapshot. This is the one UI
// function the platform layers (sim or device) call; everything else under ui/
// is an implementation detail.

namespace ui {

void create_app(const core::MachineSnapshot& state, const ScreenProfile& screen);

}  // namespace ui
