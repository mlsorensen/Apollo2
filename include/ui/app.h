#pragma once

#include "core/battery.h"
#include "core/clock.h"
#include "core/display_settings.h"
#include "core/history.h"
#include "core/machine.h"
#include "core/provisioner.h"
#include "ui/home_tab.h"
#include "ui/screen.h"
#include "ui/settings_tab.h"
#include "ui/stats_tab.h"

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
             core::IBattery& battery, core::IDisplaySettings& display,
             core::IClock& clock, core::IHistory& history,
             const ScreenProfile& screen);

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
  void open_token_setup();     // Settings "Setup" -> token-choice modal
  void retry_pairing();        // modal "Retry pairing"
  void cancel_pairing();       // pairing-spinner "Cancel"
  void start_token_setup();    // modal "WiFi" -> WiFi portal + instructions
  void cancel_token_setup();   // WiFi-modal "Cancel"
  void dismiss_modal();        // token-modal "Cancel"
  void select_settings_section(int section);  // Settings segmented selector
  void brew_adjust(int dir, bool half);  // Brew +/- (half: 0.5 snap, long-press)
  void boiler_adjust(int dir);           // Boiler level +/-
  void steam_set_enabled(bool on);       // steam boiler on/off switch
  void brightness_adjust(int dir);       // Display brightness +/-
  void hour_adjust(int dir);             // Device clock hour +/- (wraps)
  void minute_adjust(int dir);           // Device clock minute +/- (wraps)
  void set_clock_24h(bool on);           // Device "24-hour" switch
  void theme_select(int index);          // Device theme roller selection
  void apply_pending_theme();            // deferred rebuild (from lv_async_call)
  void select_stats_section(int section); // Stats segmented selector
  void cycle_zoom();                      // Stats chart zoom button
  void commit_temp_edits();              // write pending temp edits (on exit)

 private:
  void update_settings_view();
  void update_stats_view();   // refill the chart / info from history
  void update_temp_panels(const core::MachineSnapshot& state);
  void sync_home_setpoints(bool connected);  // mirror set-points to the Home steppers
  void seed_time_steppers();  // load the clock into the Hour/Minute steppers
  void rebuild();             // tear down + rebuild the UI (e.g. after a theme change)
  void handle_pairing(core::Link link);
  lv_obj_t* open_modal(const char* title, const char* body);  // returns the card
  void close_modal();
  void show_pairing_modal();  // spinner while the pairing read runs
  void show_token_modal(bool fetch_failed);  // Retry pairing / WiFi / Cancel
  void show_wifi_modal();   // WiFi instructions + Cancel

  core::IMachine* machine_ = nullptr;
  core::IProvisioner* provisioner_ = nullptr;
  core::IBattery* battery_ = nullptr;
  core::IDisplaySettings* display_ = nullptr;
  core::IClock* clock_ = nullptr;
  core::IHistory* history_ = nullptr;
  lv_obj_t* tabview_ = nullptr;
  ScreenProfile screen_{};          // stored so we can rebuild on a theme change
  lv_obj_t* modal_ = nullptr;       // current overlay modal, if open
  bool pairing_active_ = false;     // waiting on a pairing-read outcome
  bool wifi_setup_shown_ = false;   // WiFi instructions modal is open
  bool theme_rebuild_pending_ = false;  // coalesce rapid theme cycling into one rebuild
  HomeWidgets home_{};
  SettingsWidgets settings_{};
  StatsWidgets stats_{};
};

}  // namespace ui
