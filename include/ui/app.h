#pragma once

#include <cstdint>
#include <functional>

#include "core/battery.h"
#include "core/brew.h"
#include "core/clock.h"
#include "core/display_settings.h"
#include "core/history.h"
#include "core/machine.h"
#include "core/network.h"
#include "core/provisioner.h"
#include "core/settings_backup.h"
#include "core/update_check.h"
#include "core/ready_chime.h"
#include "core/schedule.h"
#include "core/scale.h"
#include "core/scale_provisioner.h"
#include "core/shot_store.h"
#include "core/sound.h"
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
             core::INetwork& network, core::ISound& sound, core::IShotStore& shots,
             const ScreenProfile& screen, core::IUpdateSource* updates,
             core::ISettingsBackup* backup, core::ISchedule& schedule);
  // NOTE: `updates` is deliberately NOT defaulted. It used to be, and the
  // internal layout-rebuild call omitted it -- so every rebuild silently set
  // updates_ = nullptr, hiding the Info page's "Check for updates" button and
  // the Settings cadence row, and no-oping manual checks, until the next boot.
  // Rebuilds fire on theme changes and scale connect/disconnect, so it looked
  // random. A defaulted parameter CHANGED an existing caller's behaviour
  // instead of breaking the build; requiring it makes the compiler catch this.
  // Pass nullptr explicitly if a caller genuinely has no update source.
  // `backup` is not defaulted for the same reason: null hides Apollo > Backup,
  // which is right on the boards without a card slot and a silent regression
  // everywhere else.

  // Reflect the latest machine state and scan results in the UI (no I/O).
  void refresh();

  // True while the idle screensaver is showing (no touch for the screen-timeout).
  // The device loop uses this to pause BLE reconnect scanning + run the daily
  // update check only when idle (both free/spare internal RAM when it matters).
  bool screensaver_active() const { return screensaver_on_; }

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
  // Scale > Settings "On the scale" rows: advance descriptor slot `index` to
  // its next option (writes to the scale; refused offline or mid-shot).
  void cycle_scale_device_setting(int index);
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
  void open_token_setup();     // Settings "Setup" -> token-entry modal
  void start_token_setup();    // modal "Enter token" -> WiFi portal + instructions
  void cancel_token_setup();   // WiFi-modal "Cancel"
  void set_wifi_enabled(bool on);  // Device "WiFi" enable switch
  void start_wifi_setup();     // Device "Set up WiFi" -> AP portal + instructions
  void cancel_wifi_setup();    // WiFi-setup-modal "Cancel"
  void forget_wifi();          // Device "Forget" (WiFi) button
  void timezone_select(int index);  // Device timezone dropdown
  void set_ntp_enabled(bool on);    // Device "Auto time (NTP)" switch
  void dismiss_modal();        // token-modal "Cancel"
  void select_settings_section(int section);  // Settings segmented selector
  void on_settings_page_shown();  // lv_menu navigated; re-seed time on the Device page
  void settings_tab_pressed();    // tab-bar Settings button: remember if it was already active
  void settings_tab_clicked();    // ...and if so, jump the drill-in back to its root
  void brew_adjust(int dir, bool half);  // Brew +/- (half: 0.5 snap, long-press)
  void boiler_adjust(int dir);           // Boiler level +/-
  void steam_set_enabled(bool on);       // steam boiler on/off switch
  void brightness_adjust(int dir);       // Display brightness +/-
  void cycle_screen_timeout();           // Device "Screen dim": Off / 1 / 5 / 15 / 30 min
  void cycle_screensaver_style();        // Device "Idle screen": Lion / Apollo / Alternate / Dim / Off
  void screensaver_tick();               // idle-dim poll (from an lv_timer, ~4 Hz)
  void saver_anim_tick();                // bouncing-logo step (from its lv_timer)
  void pose_screensaver();               // sim: force the saver on for a render
  void open_update_modal();              // update-available notice (also sim pose)
  void open_checking_modal();            // "checking..." spinner during a live check
  void open_no_update_modal();           // "up to date" notice (forced check, boot)
  void skip_update();                    // modal "Skip this version"
  void cycle_update_cadence();           // "Check for updates": Off/Boot/Daily/Hourly
  void set_beta_updates(bool on);        // "Beta updates" switch (pre-releases)
  void manual_update_check();            // Stats > Info button -> live check
  void update_result_poll();             // watch check_seq(); show the outcome
  void sync_beta_row();                  // show/hide "Beta updates" with the cadence
  bool update_ui_active() const { return modal_ != nullptr; }
  // "Install now" -> device main stashes the version and reboots into the
  // early-boot install mode (the download can't run with the full display up).
  void set_install_handler(std::function<void(const std::string&)> h) {
    install_handler_ = std::move(h);
  }
  void request_install();                // modal "Install now"
  // Settings > Apollo > Backup. Both directions confirm first: the backup modal
  // spells out that the card ends up holding the pairing token in plain text
  // (and, unless the switch is turned off, the WiFi password); the restore modal
  // spells out that the file REPLACES every setting and the device restarts.
  void open_backup_modal();               // "Back up to card" -> confirm + switches
  void set_backup_include_wifi(bool on);  // that modal's two credential switches
  void set_backup_include_token(bool on);
  void confirm_backup();                 // modal "Back up"
  void open_restore_modal();             // "Restore from card" -> confirm / refusal
  // Settings > Micra > Schedule (core::ScheduleConfig; the engine ticks from
  // refresh()). The page's controls all edit schedule_cfg_ and commit at once.
  void set_schedule_enabled(bool on);
  void set_schedule_same_daily(bool on);   // off seeds the seven days from the daily window
  void set_schedule_warmup(bool on);
  void schedule_pick_day(int weekday);     // chip tap: the day the times page edits
  void set_schedule_day_enabled(bool on);  // the "<Weekday>" switch under the chips
  void schedule_time_select(bool off, bool hour, int idx);  // On at / Off at dropdowns
  void schedule_slider_changed();          // range-slider drag: mirror into the pickers
  void schedule_slider_released();         // ...and persist on release
  void schedule_warmup_adjust(int dir);    // warm-up minutes [-]/[+]
  void set_schedule_auto_standby(bool on); // Auto-standby: standby N min after the last shot
  void schedule_auto_standby_adjust(int dir);  // ...its minutes, 10-min steps
  void schedule_copy_times();              // "Copy times to" [Copy]: edited day -> target
  // Fresh unit: "Welcome" with the setup order (WiFi + time zone first, then
  // the Micra). refresh() shows it each boot while nothing is configured and
  // the user hasn't opted out; also a sim pose.
  void open_welcome_modal();
  void welcome_choose(int choice);         // 0 WiFi, 1 Pair Micra, 2 Pair scale, 3 Later (this boot), 4 never again
  void open_schedule_warning_modal();      // "turn off the cloud app's schedule" notice
  void dismiss_schedule_warning(bool forever);
  void confirm_restore();                // modal "Restore"
  void backup_result_poll();             // watch state(); swap in the outcome modal
  void pose_backup_result();             // sim: skip the working notice's floor
  void reboot_after_restore();           // the restore modal's hold timer fired
  void hour_select(int idx);             // Time & date dropdowns: selection ->
  void minute_select(int idx);           // clock/date write (hour idx == hour,
  void month_select(int idx);            // month/day 1-based, year offset from
  void day_select(int idx);              // kClockBaseYear; day clamps + its
  void year_select(int idx);             // option list resizes with the month)
  void set_clock_24h(bool on);           // Device "24-hour" switch
  void set_use_fahrenheit(bool on);      // Device "Fahrenheit" switch
  void set_drop_negative_flow(bool on);  // Scale "Drop negative g/s" switch
  void set_scope_graph(bool on);         // Scale "Oscilloscope graph" switch
  void set_perf_overlay(bool on);        // Device "Performance overlay" switch
  void set_click_sound(bool on);         // Device "Button sounds" switch
  // Micra "Chime volume": linear 0-100 (0 = Off), slider + [-]/[+]. sound.cpp
  // maps it onto a dB curve so the steps are even by ear.
  void set_ready_chime_vol(int percent, bool audition);
  void ready_chime_vol_adjust(int dir);  // one step per [-]/[+] tap
  void cycle_ready_melody();             // Micra "Chime melody": Off/Blue/Pink/...
  void theme_select(int index);          // Device theme roller selection
  void apply_pending_theme();            // deferred rebuild (from lv_async_call)
  void apply_layout_rebuild();           // deferred rebuild after scale pair/forget
  void select_stats_section(int section); // Stats segmented selector
  void set_history_filter(int year_month); // History month filter: 0 = all,
                                           // else year*100 + month (1-12)
  void open_shot_card(uint32_t id);       // History row tap -> full-screen card
  void open_delete_shot_modal();          // shot-card trash tap -> confirm dialog
  void confirm_delete_shot();             // modal "Delete": remove + refresh
  void open_reset_stats_modal();          // metric-card tap -> confirm dialog
  void confirm_reset_stats();             // modal "Reset": set the marker
  void open_log_modal();                  // Info "Diagnostic log" row -> viewer
  void shot_button();  // shot-mode toggle, or Reset while a shot is in review
  // Settings "Restart display" handler: wired by device main to an RGB panel
  // DMA resync (falls back to esp_restart on other boards); no-op in the sim.
  void set_restart_handler(std::function<void()> h) { restart_handler_ = std::move(h); }
  void restart_device() { if (restart_handler_) restart_handler_(); }
  // A FULL soft reboot. Separate from restart_device() on purpose: that one
  // heals an RGB panel in place and never reboots, which is right for the
  // "Restart display" row and wrong after a settings restore.
  void set_reboot_handler(std::function<void()> h) { reboot_handler_ = std::move(h); }
  void reboot_device() { if (reboot_handler_) reboot_handler_(); }
  void start_clean_lock();  // Settings "Lock display for cleaning": 30 s touch lockout
  void clean_lock_tick();   // countdown update (from an lv_timer, 4 Hz)
  // Backflush cleaning (Settings > Micra): a full-screen mode that prompts for
  // the blind filter, then runs core's pulse sequence with a live cycle
  // readout. Cancel stops it; Back stops it AND leaves.
  void open_backflush();
  void close_backflush();
  void backflush_go();      // "Go" / "Run again"
  void backflush_cancel();  // "Cancel": stop, stay on the screen
  void backflush_tick();    // readout update (from an lv_timer, 4 Hz)
  void toggle_manual_flush();  // Home "Flush"/"Stop" button
  void review_hold_adjust(int dir);  // Scale settings: review-hold stepper (5s steps)
  void detect_lead_in_adjust(int dir);  // Scale settings: detect lead-in stepper (1s steps)
  void cycle_flow_smooth();          // Scale settings: Off/Light/Medium/Strong
  void cycle_shot_window_growth();   // Scale settings: Snap/Smooth/Continuous
  void cycle_flush();                // Micra settings "Auto flush": Off / 3 s / 6 s
  void cycle_flush_delay();          // Micra settings "Flush delay": 3 / 6 / 9 / 15 s
  void set_auto_connect(bool on) {   // Micra settings: connect to saved machine at boot
    if (provisioner_ != nullptr) provisioner_->set_auto_connect(on);
  }
  void set_wired_paddle(bool on);    // Micra settings: paddle harness vs shot detector
  // Transient auto-dismissing message over the current tab (the tabview stays
  // visible — unlike open_modal). Used for the "shot refused, no scale"
  // feedback; works on every layout incl. compact (which has no shot button
  // to flash). Tap or timeout dismisses.
  void show_toast(const char* msg);
  void dismiss_toast();
  // Sim-only pose: seed the unwired capture ring with a synthetic pre-shot
  // baseline + pour ramp and run the mid-shot handoff as if the detector just
  // confirmed — renders the back-filled live shot plot deterministically.
  void pose_unwired_midshot(uint32_t shot_ms = 9000);
  void zoom_step(int dir);                // Stats time-axis zoom: -1 in, +1 out
  void commit_temp_edits();              // write pending temp edits (on exit)

 private:
  void update_settings_view();
  void update_scale_view();   // refresh the Scale page (connection + target)
  void update_stats_view();   // refill the chart / info from history
  void refresh_log_modal();   // re-snapshot the ring into the open log modal
  void update_history_view(); // History section: guidance / metrics / list
  // Assemble + save a ShotRecord at the kReview freeze (no-op without storage
  // or a real date). Call AFTER finish/review_shot_plot rebased the ring.
  void capture_shot_record(const core::BrewSnapshot& bsnap,
                           const core::ScaleSnapshot& snap);
  void update_temp_panels(const core::MachineSnapshot& state);
  void sync_home_setpoints(bool connected);  // mirror set-points to the Home steppers
  void update_battery_runtime(const core::BatteryState& b);  // track drain for the estimate
  void seed_time_controls();   // load the clock into the time/date dropdowns
  // Schedule: engine feed + page state.
  bool ntp_ready() const;      // WiFi on + NTP on + a real sync landed this boot
  core::ScheduleInputs schedule_inputs(const core::MachineSnapshot& snap) const;
  void schedule_tick(const core::MachineSnapshot& snap);
  void apply_scheduled_power(bool on, bool auto_standby);
  core::DaySchedule& sched_edit_day();
  static void format_clock_min(char* out, size_t n, int min_of_day, bool h24);
  void schedule_commit();          // engine + persisted copy follow schedule_cfg_
  void sync_schedule_controls();   // widgets follow schedule_cfg_ (and the chip)
  void sync_schedule_gate();       // paired + NTP -> status line + greyed controls
  void apply_schedule_clickable();
  void set_schedule_axis_labels(bool h24);  // the hour axis under the slider
  void apply_date_selection(); // clamp day, resize its options, write set_date
  // Re-derive the inferred Heating state (hysteresis bit = heating_) and
  // start/stop the status-dot pulse. Call once per machine-snapshot pass,
  // before update_home.
  void update_heating(const core::MachineSnapshot& state);
  void rebuild();             // tear down + rebuild the UI (e.g. after a theme change)
  void request_layout_rebuild(int section);  // defer a rebuild, returning to `section`
  lv_obj_t* open_modal(const char* title, const char* body);  // returns the card
  lv_obj_t* open_modal_card(const char* title);  // card + title only (no body)
  lv_obj_t* modal_button_row(lv_obj_t* card);    // side-by-side button container
  // Portal instructions: WIFI: join QR + the manual ssid/url steps.
  lv_obj_t* open_join_modal(const char* title, const char* ssid, const char* url,
                            const char* then_line);
  void close_modal();
  void show_token_modal(bool fetch_failed);  // token-entry prompt (Enter token / Cancel)
  void show_wifi_modal();   // WiFi instructions + Cancel
  void show_wifi_setup_modal();  // WiFi-credential portal instructions + Cancel
  core::NetState net_status() const {
    return network_ != nullptr ? network_->status() : core::NetState::Disabled;
  }
  bool scale_connect_enabled() const {
    return scale_provisioner_ != nullptr && scale_provisioner_->connect_enabled();
  }

  core::IMachine* machine_ = nullptr;
  core::IProvisioner* provisioner_ = nullptr;
  core::IBattery* battery_ = nullptr;
  core::IDisplaySettings* display_ = nullptr;
  core::IClock* clock_ = nullptr;
  core::IHistory* history_ = nullptr;
  core::IScale* scale_ = nullptr;
  core::IScaleProvisioner* scale_provisioner_ = nullptr;
  core::IBrewController* brew_ = nullptr;
  core::INetwork* network_ = nullptr;
  core::IUpdateSource* updates_ = nullptr;  // optional; null = no update UI
  core::ISettingsBackup* backup_ = nullptr;  // optional; null = no Backup page
  bool backup_include_wifi_ = true;   // the backup modal's switches (per-open)
  bool backup_include_token_ = true;
  bool backup_pending_ = false;      // a request is out; poll for its outcome
  lv_obj_t* backup_wifi_switch_ = nullptr;   // "Include WiFi network" (modal-owned)
  lv_obj_t* backup_token_switch_ = nullptr;  // "Include pairing token"
  lv_obj_t* backup_note_ = nullptr;          // what an opted-out backup leaves out
  lv_timer_t* reboot_timer_ = nullptr;       // holds the restored notice on screen
  uint32_t backup_started_ms_ = 0;           // when the working modal went up
  void update_backup_note();                 // rewrite that line from the switches
  int update_last_seq_ = -1;         // last seen check_seq(); -1 = not baselined
  bool manual_check_pending_ = false;  // a user-initiated check awaits its result
  bool update_notice_pending_ = false;  // an auto notice deferred under the saver
  uint32_t manual_check_started_ = 0;  // lv_tick when the manual check began
  static constexpr uint32_t kManualCheckTimeoutMs = 30000;  // give up + report
  std::function<void(const std::string&)> install_handler_;  // "Install now"
  core::ISound* sound_ = nullptr;
  core::IShotStore* shots_ = nullptr;
  bool click_sound_on_ = true;  // cached from IDisplaySettings (checked per press)
  int ready_chime_vol_ = 80;    // cached likewise, 0..100 (0 = chime off)
  int ready_chime_mel_ = 1;     // cached likewise: 0 = off, 1.. = ready melody
  lv_obj_t* tabview_ = nullptr;
  ScreenProfile screen_{};          // stored so we can rebuild on a theme change
  lv_obj_t* modal_ = nullptr;       // current overlay modal, if open
  // Log-viewer modal (live tail): refreshed from core::log_ring() by a timer
  // while open; all three are torn down together in close_modal().
  lv_obj_t* log_box_ = nullptr;
  lv_obj_t* log_label_ = nullptr;
  lv_timer_t* log_timer_ = nullptr;
  uint32_t config_mode_seen_ = 0;   // last machine config-mode event toasted (edge-detect)
  bool wifi_setup_shown_ = false;   // token-over-WiFi instructions modal is open
  bool wifi_portal_shown_ = false;  // WiFi-credential setup modal is open
  uint32_t scale_readout_tick_ = 0;     // paces the shot-timer redraw (~10 Hz)
  float last_weight_g_ = 0.0f;          // last weight drawn (redraw on change only)
  bool last_scale_connected_ = false;
  core::ShotPhase shot_phase_ = core::ShotPhase::kIdle;  // last seen (graph reset/freeze edges)
  uint32_t brew_reject_seen_ = 0;  // last review_reject_seq (flash Reset on change)
  uint32_t scale_refuse_seen_ = 0;  // last scale_refuse_seq (toast on change)
  lv_obj_t* toast_ = nullptr;          // transient message card (lv_layer_top)
  lv_timer_t* toast_timer_ = nullptr;  // its auto-dismiss timer
  uint32_t unwired_shot_t0_ = 0;   // lv_tick of the detected shot's retro start
                                   // (captured on the kBrewing edge; drives the
                                   // shot-aligned review repaint)
  bool stop_hint_seen_ = false;    // last stop_hint (flash the pill on the rise)
  // Screensaver: dims the backlight after the configured idle time; any touch
  // restores it (LVGL's inactivity clock resets on input). Timer survives
  // rebuilds — it belongs to the app, not the widget tree.
  lv_timer_t* screensaver_timer_ = nullptr;
  bool screensaver_on_ = false;
  // Saver cover on lv_layer_top (swallows the waking tap): black + the
  // recolored lion drifting DVD-style (Logo), transparent (Dim), black (Off);
  // torn down on wake.
  lv_obj_t* saver_layer_ = nullptr;     // top-layer cover: Dim (transparent) and Off (black)
  lv_obj_t* saver_screen_ = nullptr;    // the artworks' own black screen (loaded in place of the UI)
  lv_obj_t* saver_prev_screen_ = nullptr;  // the UI screen to load back on wake
  lv_obj_t* saver_img_ = nullptr;       // null in the Dim and Off styles
  lv_timer_t* saver_anim_ = nullptr;
  int saver_vx_ = 0, saver_vy_ = 0;     // px per anim tick
  int saver_color_i_ = 0;               // palette index; advances on bounce
  int saver_art_ = 0;                   // ui::SaverArt currently on screen
  bool saver_alternate_ = false;        // Alternate style: swap the art on a timer
  uint32_t saver_art_since_ = 0;        // lv_tick when saver_art_ went up
  void start_screensaver(int style);  // core::IDisplaySettings::SaverStyle
  void set_saver_art(int art);        // (re)size + source the bouncing image
  void stop_screensaver();
  // Cleaning lock: a full-screen opaque overlay on the top layer swallows all
  // touch input for kCleanLockSecs while a countdown shows time remaining.
  void end_clean_lock();
  static constexpr int kCleanLockSecs = 30;
  lv_obj_t* clean_lock_overlay_ = nullptr;
  lv_obj_t* clean_lock_count_ = nullptr;  // the big remaining-seconds label
  lv_timer_t* clean_lock_timer_ = nullptr;
  uint32_t clean_lock_t0_ = 0;   // lv_tick at lock start
  int clean_lock_shown_s_ = -1;  // last rendered value (redraw on change only)
  // Backflush mode. The SEQUENCE lives in BrewController (so it can't outlive
  // or be orphaned by this UI); these are just the screen's widgets + which of
  // the four screens is showing.
  enum BackflushState { kBackflushPrompt = 0, kBackflushRunning, kBackflushDone,
                        kBackflushAborted };
  lv_obj_t* bf_overlay_ = nullptr;
  lv_obj_t* bf_msg_ = nullptr;
  lv_obj_t* bf_cycle_label_ = nullptr;   // "Cycle 3 of 10"
  lv_obj_t* bf_phase_label_ = nullptr;   // "Running 3s" / "Pause 2s"
  lv_obj_t* bf_go_btn_ = nullptr;
  lv_obj_t* bf_go_label_ = nullptr;      // "Go" / "Run again"
  lv_obj_t* bf_cancel_btn_ = nullptr;
  lv_obj_t* bf_back_btn_ = nullptr;
  lv_timer_t* bf_timer_ = nullptr;
  int bf_state_ = kBackflushPrompt;
  bool theme_rebuild_pending_ = false;  // coalesce rapid theme cycling into one rebuild
  bool layout_rebuild_pending_ = false; // coalesce a scale pair/forget rebuild
  int rebuild_section_ = kSectionDeviceDisplay;  // Settings section to return to
                                                 // after rebuild() (theme button)
  HomeWidgets home_{};
  SettingsWidgets settings_{};
  StatsWidgets stats_{};

  // Battery runtime estimate: a ~10-min sliding window of percent-over-time; the
  // drain rate (oldest-in-window vs now) extrapolates the remaining runtime. The
  // window is cleared while on USB power / no battery (no estimate).
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
  std::function<void()> restart_handler_;  // Settings > Device "Restart" (device only)
  std::function<void()> reboot_handler_;   // full reboot after a settings restore
  float batt_cutoff_volts_ = 0.0f;
  int batt_low_count_ = 0;  // consecutive sub-cutoff reads (debounce)

  // Inferred Heating state (see core::derive_heating); doubles as the
  // hysteresis "previous result" bit.
  bool heating_ = false;
  // Warm-up-complete latch behind the ready chime. A member (not a local) so
  // it survives refreshes AND theme rebuilds — its whole job is remembering
  // that this warm-up has already been announced.
  core::ReadyChime ready_chime_;
  // Scheduled on/standby. The engine is a member for the same reason: its
  // consumed-slot latches must outlive refreshes and theme rebuilds. ~80 B of
  // internal .bss for the engine + the working config copy; no heap, no task.
  core::ISchedule* schedule_ = nullptr;
  core::ScheduleEngine schedule_engine_;
  core::ScheduleConfig schedule_cfg_;      // the page's working copy
  bool schedule_loaded_ = false;           // schedule_cfg_ read from the port once
  bool schedule_warned_ = false;           // cloud-app notice shown this session
  bool settings_tab_was_active_ = false;   // Settings tab already showing when its button was pressed
  bool welcome_checked_ = false;           // the first refresh decided about the welcome
  int schedule_gate_ = -1;                 // change-detect: bit0 paired, bit1 NTP; -1 unknown

  // Shot-history view state. shot_view_ backs the open shot-card modal (the
  // card's graph paints from it on every redraw, so it must outlive the
  // modal); shot_capture_ stages save(). ~7 KB each — allocated from the
  // LVGL pool (PSRAM on device) in build(), NOT members: as members of a
  // global App they'd sit in internal .bss, and internal RAM is the S3
  // boards' scarcest resource. The built-rows fingerprint avoids rebuilding
  // the list every refresh tick.
  core::ShotRecord* shot_view_ = nullptr;
  core::ShotRecord* shot_capture_ = nullptr;
  int hist_built_count_ = -1;   // count() the rows were last built from
  int hist_built_filter_ = -1;  // filter the rows were last built from
  uint32_t pending_delete_id_ = 0;  // shot the delete-confirm modal is about
};

}  // namespace ui
