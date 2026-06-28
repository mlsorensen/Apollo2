#pragma once

#include "core/battery.h"
#include "core/machine.h"
#include "core/provisioner.h"
#include "ui/home_tab.h"
#include "ui/screen.h"
#include "ui/settings_tab.h"

// The on-screen application: the tab shell plus each tab's content, built into
// the active LVGL screen and laid out for a screen profile. Holds references to
// the core ports (IMachine for control/state, IProvisioner for setup) and
// nothing more — it never sees the concrete transport.
//
// Usage: build() once, then refresh() on a timer to reflect the latest state.

namespace ui {

class App {
 public:
  ~App();

  void build(core::IMachine& machine, core::IProvisioner& provisioner,
             core::IBattery& battery, const ScreenProfile& screen);

  // Reflect the latest machine state and scan results in the UI (no I/O).
  void refresh();

  // Switch the active tab by index (0=Home, 1=Settings, 2=Stats). Mainly for
  // the simulator to render a specific tab.
  void show_tab(int index);

  // Bound to UI events:
  void toggle_power();         // power button
  void start_scan();           // Settings "Scan" button
  void save_scanned(int index);  // a result row in the Settings list
  void forget();               // Settings "Forget" button
  void start_token_setup();    // Settings "Setup" button -> WiFi portal + modal
  void cancel_token_setup();   // modal "Cancel"
  void select_settings_section(int section);  // Settings segmented selector

 private:
  void update_settings_view();
  void show_setup_modal();
  void close_setup_modal();

  core::IMachine* machine_ = nullptr;
  core::IProvisioner* provisioner_ = nullptr;
  core::IBattery* battery_ = nullptr;
  lv_obj_t* tabview_ = nullptr;
  lv_obj_t* setup_modal_ = nullptr;  // token-setup instructions overlay, if open
  HomeWidgets home_{};
  SettingsWidgets settings_{};
};

}  // namespace ui
