#pragma once

#include "core/machine.h"
#include "ui/home_tab.h"
#include "ui/screen.h"

// The on-screen application: the tab shell plus every tab's content, built into
// the active LVGL screen and laid out for a screen profile. Holds a reference
// to a core::IMachine so it can render its state and issue commands — and
// nothing more; it never sees the concrete transport (BLE, fake, ...).
//
// Usage: build() once, then refresh() whenever new machine state is available
// (e.g. on a poll). The power button issues commands through the same interface.

namespace ui {

class App {
 public:
  void build(core::IMachine& machine, const ScreenProfile& screen);

  // Re-read the machine snapshot and update the on-screen widgets in place.
  void refresh();

  // Command bound to the power button: flip power, then refresh.
  void toggle_power();

 private:
  core::IMachine* machine_ = nullptr;
  HomeWidgets home_{};
};

}  // namespace ui
