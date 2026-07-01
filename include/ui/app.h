#pragma once

#include <cstdint>
#include <functional>

#include "core/battery.h"
#include "core/brew.h"
#include "core/clock.h"
#include "core/display_settings.h"
#include "core/history.h"
#include "core/machine.h"
#include "core/provisioner.h"
#include "core/scale.h"
#include "core/scale_provisioner.h"
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
             core::IClock& clock, core::IHistory& history, core::IScale& scale,
             core::IScaleProvisioner& scale_provisioner, core::IBrewController& brew,
             const ScreenProfile& screen);

  // Reflect the latest machine state and scan results in the UI (no I/O).
  void refresh();

  // Drain the scale's native flow-rate stream into the Home flow graph. Called
  // every device-loop iteration (much faster than refresh()) so the line plots
  // the scale's real sample rate smoothly. Cheap no-op when nothing is pending.
  void pump_scale_chart();

  // Called (once, after a sustained reading) when the pack drops to/below
  // cutoff_volts on battery — the device wires this to deep sleep.
  void set_low_battery_handler(float cutoff_volts, std::function<void()> on_critical);

  // Switch the active tab by index (0=Home, 1=Settings, 2=Stats). Mainly for
  // the simulator to render a specific tab.
  void show_tab(int index);

  // Bound to UI events:
  void toggle_power();         // power button
  void tare_scale();           // Home "Tare" button
  void toggle_flow_units();    // Home graph unit button (g/s <-> g)
  void start_scan();           // Settings "Scan" button
  void save_scanned(int index);  // a result row in the Settings list
  void forget();               // Settings "Forget" button
  void toggle_connection();    // Settings "Connect"/"Disconnect" button
  void start_scale_scan();        // Scale page "Scan" button
  void save_scanned_scale(int index);  // a result row in the Scale scan list
  void forget_scale();            // Scale page "Forget" button
  void toggle_scale_connection(); // Scale page "Connect"/"Disconnect"
  void target_adjust(int dir);    // Scale target weight +/- (grams)
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
  void set_use_fahrenheit(bool on);      // Device "Fahrenheit" switch
  void set_drop_negative_flow(bool on);  // Scale "Drop negative g/s" switch
  void set_scope_graph(bool on);         // Scale "Oscilloscope graph" switch
  void theme_select(int index);          // Device theme roller selection
  void apply_pending_theme();            // deferred rebuild (from lv_async_call)
  void apply_layout_rebuild();           // deferred rebuild after scale pair/forget
  void select_stats_section(int section); // Stats segmented selector
  void zoom_step(int dir);                // Stats time-axis zoom: -1 in, +1 out
  void commit_temp_edits();              // write pending temp edits (on exit)

 private:
  void update_settings_view();
  void update_scale_view();   // refresh the Scale page (connection + target)
  void update_stats_view();   // refill the chart / info from history
  void update_temp_panels(const core::MachineSnapshot& state);
  void sync_home_setpoints(bool connected);  // mirror set-points to the Home steppers
  void update_battery_runtime(const core::BatteryState& b);  // track drain for the estimate
  void seed_time_steppers();  // load the clock into the Hour/Minute steppers
  void rebuild();             // tear down + rebuild the UI (e.g. after a theme change)
  void request_layout_rebuild(int section);  // defer a rebuild, returning to `section`
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
  core::IScale* scale_ = nullptr;
  core::IScaleProvisioner* scale_provisioner_ = nullptr;
  core::IBrewController* brew_ = nullptr;
  lv_obj_t* tabview_ = nullptr;
  ScreenProfile screen_{};          // stored so we can rebuild on a theme change
  lv_obj_t* modal_ = nullptr;       // current overlay modal, if open
  bool pairing_active_ = false;     // waiting on a pairing-read outcome
  bool wifi_setup_shown_ = false;   // WiFi instructions modal is open
  uint32_t scale_readout_tick_ = 0;     // throttles the fast (~10 Hz) weight readout
  bool theme_rebuild_pending_ = false;  // coalesce rapid theme cycling into one rebuild
  bool layout_rebuild_pending_ = false; // coalesce a scale pair/forget rebuild
  int rebuild_section_ = kSectionDevice;  // Settings section to return to after rebuild()
  HomeWidgets home_{};
  SettingsWidgets settings_{};
  StatsWidgets stats_{};

  // Battery runtime estimate: a ~10-min sliding window of percent-over-time; the
  // drain rate (oldest-in-window vs now) extrapolates the remaining runtime. The
  // window is cleared while charging / on USB / no battery (no estimate).
  core::BatteryState battery_state_{};  // latest read (for Stats > Info)
  struct BattSample {
    uint32_t t_ms;
    int pct;
  };
  static constexpr int kBattHist = 12;  // ~11 min at 1 sample/min
  BattSample batt_hist_[kBattHist] = {};
  int batt_hist_count_ = 0;
  int batt_hist_head_ = 0;
  uint32_t batt_last_sample_ms_ = 0;
  char batt_runtime_text_[24] = "-";  // cached estimate, recomputed every ~5 s
  uint32_t batt_runtime_calc_ms_ = 0;

  // Low-battery cutoff -> deep sleep (handler provided by the device).
  std::function<void()> batt_low_handler_;
  float batt_cutoff_volts_ = 0.0f;
  int batt_low_count_ = 0;  // consecutive sub-cutoff reads (debounce)
};

}  // namespace ui
