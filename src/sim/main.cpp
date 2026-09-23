// Host simulator entry point. Renders the UI for each screen profile we support
// into a PNG for visual inspection, then exits. Run via tools/sim.sh.
//
// This file is the host counterpart of src/device/main.cpp: both wire a
// concrete platform (here: FakeMachine + PngDisplay) to the portable UI.

#include <cstdio>
#include "lvgl.h"
#include "src/core/lv_obj_draw_private.h"  // lv_obj_get_ext_draw_size (saver check)
#include <filesystem>

#include "core/log_ring.h"
#include "core/shot_csv.h"
#include "core/system.h"

#include "platform_host/dir_shot_store.h"
#include "platform_host/fake_battery.h"
#include "platform_host/fake_brew_controller.h"
#include "platform_host/fake_clock.h"
#include "platform_host/fake_display_settings.h"
#include "platform_host/fake_history.h"
#include "platform_host/fake_machine.h"
#include "platform_host/fake_network.h"
#include "platform_host/fake_update_source.h"
#include "platform_host/fake_provisioner.h"
#include "platform_host/fake_scale.h"
#include "platform_host/fake_scale_provisioner.h"
#include "platform_host/fake_schedule.h"
#include "platform_host/fake_settings_backup.h"
#include "platform_host/fake_shot_store.h"
#include "platform_host/fake_sound.h"
#include "platform_host/png_display.h"
#include "ui/app.h"
#include "ui/screen.h"
#include "ui/theme.h"

namespace {

// The bouncing lion is the one thing that redraws every frame for hours, so
// what it invalidates IS the saver's frame cost -- and a PNG can't show it.
// Guard the two things that have each regressed it once: the widget's layout
// box must equal the drawn size (a scaled lv_image otherwise keeps its native
// box), and LVGL's ext-draw padding must be zero (it pads a scaled image by
// its box*(scale-1) -- a 6-7x dirty area on the P4 panels that upscale; see
// start_screensaver). Fails the sim run rather than silently rendering.
bool check_saver_dirty_area(const ui::ScreenProfile& screen) {
  // The artwork saver is its own loaded screen holding just the image.
  lv_obj_t* scr = lv_screen_active();
  lv_obj_t* img = lv_obj_get_child_count(scr) == 1 ? lv_obj_get_child(scr, 0) : nullptr;
  if (img == nullptr) {
    std::fprintf(stderr, "error: screensaver image not found on the saver screen\n");
    return false;
  }
  const int w = lv_obj_get_width(img), h = lv_obj_get_height(img);
  const int ext = lv_obj_get_ext_draw_size(img);
  const int drawn_w = static_cast<int>(lv_image_get_src_width(img)) *
                      static_cast<int>(lv_image_get_scale_x(img)) / LV_SCALE_NONE;
  const int drawn_h = static_cast<int>(lv_image_get_src_height(img)) *
                      static_cast<int>(lv_image_get_scale_y(img)) / LV_SCALE_NONE;
  std::printf("saver %dx%d: art box %dx%d drawn %dx%d ext_draw %d -> %d px/frame\n",
              screen.width, screen.height, w, h, drawn_w, drawn_h, ext,
              (w + 2 * ext) * (h + 2 * ext));
  const bool box_ok = drawn_w >= w - 1 && drawn_w <= w && drawn_h >= h - 1 && drawn_h <= h;
  if (!box_ok || ext != 0) {
    std::fprintf(stderr, "error: screensaver art invalidates more than it draws\n");
    return false;
  }
  return true;
}

bool render(core::IMachine& machine, core::IProvisioner& provisioner,
            core::IBattery& battery, core::IDisplaySettings& disp_settings,
            core::IClock& clock, core::IHistory& history, core::IScale& scale,
            core::IScaleProvisioner& scale_provisioner, core::IBrewController& brew,
            core::INetwork& network, core::IShotStore& shots, ui::ScreenProfile screen,
            const char* out_path, int tab = 0, int settings_section = -1,
            bool token_modal = false, int theme = 0, int stats_section = -1,
            bool clean_lock = false, int shot_modal_id = -1, int history_ym = 0,
            bool backflush = false, bool log_modal = false,
            uint32_t unwired_midshot_ms = 0, bool toast = false,
            bool join_modal = false, bool screensaver = false,
            bool update_modal = false, core::IUpdateSource* updates = nullptr,
            core::ISettingsBackup* backup = nullptr, int backup_modal = 0,
            int schedule_day = -1, core::ISchedule* schedule = nullptr,
            bool welcome_modal = false) {
  std::filesystem::path p(out_path);
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

  disp_settings.set_theme(theme);  // build() reads this into ui::theme::set_active
  host::PngDisplay display(screen.width, screen.height);
  static host::FakeSound fake_sound;  // stateless; shared across renders
  ui::App app;
  static host::FakeSchedule fallback_schedule;  // renders that don't pose one
  app.build(machine, provisioner, battery, disp_settings, clock, history, scale,
            scale_provisioner, brew, network, fake_sound, shots, screen, updates,
            backup, schedule != nullptr ? *schedule : fallback_schedule);
  app.show_tab(tab);
  if (settings_section >= 0) app.select_settings_section(settings_section);  if (schedule_day >= 0) app.schedule_pick_day(schedule_day);  // per-day chip pose
  if (welcome_modal) app.open_welcome_modal();
  if (stats_section >= 0) app.select_stats_section(stats_section);
  if (history_ym != 0) app.set_history_filter(history_ym);
  if (token_modal) app.open_token_setup();
  if (join_modal) app.start_token_setup();  // the QR + join-instructions modal
  if (clean_lock) app.start_clean_lock();
  if (backflush) app.open_backflush();
  if (shot_modal_id >= 0) app.open_shot_card(static_cast<uint32_t>(shot_modal_id));
  if (log_modal) app.open_log_modal();
  if (unwired_midshot_ms != 0) app.pose_unwired_midshot(unwired_midshot_ms);
  if (toast)
    app.show_toast("Shot not started: Auto shot is enabled. "
                   "Connect the scale or switch to Manual mode.");
  if (screensaver) app.pose_screensaver();  // bouncing-art saver, start pose
  if (update_modal) app.open_update_modal();
  // 1 = back-up confirm, 2 = the same with WiFi opted out, 3 = restore confirm
  // (4 = its "backup is newer than this firmware" refusal, posed by the fake),
  // 5 = backup done, 6 = restore done, 7 = no card in the slot, 8 = the
  // "Backing up" notice itself (it now holds for kWorkingFloorMs).
  if (backup_modal == 1 || backup_modal == 5 || backup_modal == 7 ||
      backup_modal == 8)
    app.open_backup_modal();
  if (backup_modal == 2) {  // both credentials opted out
    app.open_backup_modal();
    app.set_backup_include_wifi(false);
    app.set_backup_include_token(false);
  }
  if (backup_modal == 3 || backup_modal == 4) app.open_restore_modal();
  if (backup_modal == 8) app.confirm_backup();  // stops at the working notice
  if (backup_modal == 5) {
    app.confirm_backup();
    app.pose_backup_result();
  }
  if (backup_modal == 6) {
    app.open_restore_modal();
    app.confirm_restore();
    app.pose_backup_result();  // no reboot handler in the sim: the notice stays
  }
  display.render_frame();
  if (screensaver && !check_saver_dirty_area(screen)) return false;
  if (!display.save_png(out_path)) {
    std::fprintf(stderr, "error: failed to write %s\n", out_path);
    return false;
  }
  std::printf("wrote %s\n", out_path);
  return true;
}

}  // namespace

int main() {
  host::FakeMachine machine;
  host::FakeProvisioner provisioner;
  host::FakeBattery battery;
  host::FakeDisplaySettings disp;
  host::FakeClock clock;
  host::FakeHistory history;
  host::FakeScale scale;
  host::FakeScaleProvisioner scale_provisioner;
  host::FakeBrewController brew;
  host::FakeNetwork network;
  host::FakeUpdateSource updates;
  host::FakeShotStore shots;
  host::FakeSettingsBackup backup;
  host::FakeSchedule schedule;

  // One PNG per supported layout. Add a line here when a new form factor lands.
  auto r = [&](ui::ScreenProfile s, const char* path, int tab = 0, int sec = -1,
               bool modal = false, int theme = 0, int stats = -1, bool clean_lock = false,
               int shot_id = -1, int history_ym = 0, bool backflush = false,
               bool log_modal = false, uint32_t unwired_midshot_ms = 0,
               bool toast = false, bool join_modal = false, bool screensaver = false,
               bool update_modal = false, int backup_modal = 0, int schedule_day = -1,
               bool welcome_modal = false) {
    return render(machine, provisioner, battery, disp, clock, history, scale,
                  scale_provisioner, brew, network, shots, s, path, tab, sec, modal, theme,
                  stats, clean_lock, shot_id, history_ym, backflush, log_modal,
                  unwired_midshot_ms, toast, join_modal, screensaver, update_modal,
                  &updates, &backup, backup_modal, schedule_day, &schedule, welcome_modal);
  };
  bool ok = true;
  ok &= r({800, 480}, "renders/home_800x480.png");
  ok &= r({320, 240}, "renders/home_320x240.png");
  // Scrolling strip-chart graph (the non-default style; scope is the default).
  disp.set_scope_graph(false);
  ok &= r({800, 480}, "renders/home_scroll_800x480.png");
  disp.set_scope_graph(true);
  // Sleeping Umbra: a sleep-capable scale with its link switched off shows the
  // SCALE card as "Sleeping" (muted dot) with Connect armed to wake it.
  scale.set_umbra(true);
  scale.set_connected(false);
  scale_provisioner.set_connect_enabled(false);
  ok &= r({800, 480}, "renders/home_sleeping_800x480.png");
  scale_provisioner.set_connect_enabled(true);
  scale.set_connected(true);
  scale.set_umbra(false);
  // Heating: machine on but boilers still below target — the inferred state
  // shows an amber (pulsing on-device) dot + "Heating" instead of "Ready".
  machine.set_temps(78.0f, 96.0f);
  ok &= r({800, 480}, "renders/home_heating_800x480.png");
  ok &= r({320, 240}, "renders/home_heating_320x240.png");
  machine.set_temps(93.0f, 123.0f);
  // Unwired mode (paddle harness not in use): the shot button arms the weight-
  // stream detector ("Detect") instead of the auto-stop, pill shows Ready.
  brew.set_wired_paddle(false);
  ok &= r({800, 480}, "renders/home_unwired_800x480.png");
  // Mid-shot handoff: the detector just confirmed — the graph back-fills the
  // shot (3 s lead-in + ramp) from the capture ring (see pose_unwired_midshot).
  brew.set_phase(core::ShotPhase::kBrewing);
  brew.set_shot_ms(9000);
  ok &= r({800, 480}, "renders/home_unwired_midshot_800x480.png", 0, -1, false, 0,
          -1, false, -1, 0, false, false, 9000);
  // The same shot at 24 s: under the default Continuous window growth the
  // trace fills the width and the caption reads the elapsed seconds (Snap
  // would show it at the 30 s step). Smooth's ease can't be caught in a still.
  brew.set_shot_ms(24000);
  ok &= r({800, 480}, "renders/home_shot_continuous_800x480.png", 0, -1, false, 0,
          -1, false, -1, 0, false, false, 24000);
  brew.set_phase(core::ShotPhase::kIdle);
  brew.set_shot_ms(27000);
  brew.set_wired_paddle(true);
  // Shot-refused toast (paddle flipped, Auto/Detect armed, no scale): the
  // transient message card over the live Home.
  ok &= r({800, 480}, "renders/toast_refuse_800x480.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, true);
  ok &= r({320, 240}, "renders/toast_refuse_320x240.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, true);
  // No-scale Home (classic layout) — toggle the fake to "no scale saved".
  scale_provisioner.set_saved(false);
  ok &= r({320, 240}, "renders/home_noscale_320x240.png");
  ok &= r({800, 480}, "renders/home_noscale_800x480.png");
  ok &= r({1024, 600}, "renders/home_noscale_1024x600.png");
  scale_provisioner.set_saved(true);
  ok &= r({800, 480}, "renders/settings_800x480.png", 1);
  // Cleaning lock: full-screen touch-lockout countdown (over Home, like the
  // token modal — the overlay hides the tabview either way).
  ok &= r({800, 480}, "renders/clean_lock_800x480.png", 0, -1, false, 0, -1, true);
  ok &= r({320, 240}, "renders/clean_lock_320x240.png", 0, -1, false, 0, -1, true);
  // Bouncing-logo screensaver (start pose; on-device it drifts + recolors).
  // Also at the P4 sizes that scale the lion (its check above is what
  // caught the v0.12.0 saver regression there).
  ok &= r({1280, 720, 1.5f}, "renders/screensaver_1280x720.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, false, true);
  ok &= r({1280, 800, 1.6f}, "renders/screensaver_1280x800.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, false, true);
  ok &= r({320, 240}, "renders/screensaver_320x240.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, false, true);
  // No such board: a height between tiers that has to UPSCALE the nearest
  // lion, so the stretch fallback (and its ext-draw clamp) stays exercised.
  ok &= r({1280, 960}, "renders/screensaver_fallback_1280x960.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, false, true);
  ok &= r({800, 480}, "renders/screensaver_800x480.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, false, true);
  // The Apollo artwork (Idle screen: Apollo) at the same tiers -- taller
  // aspect than the lion, so its widths and dirty areas differ.
  disp.set_screensaver_style(core::IDisplaySettings::kSaverApollo);
  ok &= r({800, 480}, "renders/screensaver_apollo_800x480.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, false, true);
  ok &= r({1280, 720, 1.5f}, "renders/screensaver_apollo_1280x720.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, false, true);
  ok &= r({320, 240}, "renders/screensaver_apollo_320x240.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, false, true);
  disp.set_screensaver_style(core::IDisplaySettings::kSaverLion);
  // Update-available modal (canned notes from FakeUpdateSource).
  updates.set_available(true);
  ok &= r({800, 480}, "renders/update_modal_800x480.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, false, false, true);
  updates.set_available(false);
  // Backflush cleaning (Settings > Micra): the prompt screen, and mid-sequence
  // with the cycle readout (the fake poses a running sequence the real
  // controller would advance from its poll).
  ok &= r({800, 480}, "renders/backflush_prompt_800x480.png", 1, -1, false, 0, -1,
          false, -1, 0, true);
  ok &= r({320, 240}, "renders/backflush_prompt_320x240.png", 1, -1, false, 0, -1,
          false, -1, 0, true);
  brew.set_backflush(true, 3, true, 2400);
  ok &= r({800, 480}, "renders/backflush_running_800x480.png", 1, -1, false, 0, -1,
          false, -1, 0, true);
  ok &= r({320, 240}, "renders/backflush_running_320x240.png", 1, -1, false, 0, -1,
          false, -1, 0, true);
  brew.set_backflush(false, 0, false, 0);
  ok &= r({320, 240}, "renders/settings_320x240.png", 1);
  ok &= r({320, 240}, "renders/micra_320x240.png", 1, ui::kSectionMicra);  // chooser
  ok &= r({320, 240}, "renders/micra_bt_320x240.png", 1, ui::kSectionMicraBt);
  ok &= r({320, 240}, "renders/micra_controls_320x240.png", 1, ui::kSectionMicraControls);
  ok &= r({800, 480}, "renders/micra_controls_800x480.png", 1, ui::kSectionMicraControls);
  ok &= r({320, 240}, "renders/micra_cleaning_320x240.png", 1, ui::kSectionMicraCleaning);
  ok &= r({800, 480}, "renders/micra_cleaning_800x480.png", 1, ui::kSectionMicraCleaning);
  // ...and with the wired paddle off: the page greys with the reason on top.
  brew.set_wired_paddle(false);
  ok &= r({800, 480}, "renders/micra_cleaning_unwired_800x480.png", 1, ui::kSectionMicraCleaning);
  brew.set_wired_paddle(true);
  ok &= r({320, 240}, "renders/scale_bt_320x240.png", 1, ui::kSectionScaleBt);
  ok &= r({800, 480}, "renders/scale_bt_800x480.png", 1, ui::kSectionScaleBt);
  // Cleaning page with Auto flush on, so the Flush delay row is visible (the
  // user manual's screenshot needs all three rows).
  brew.set_flush_s(3);
  ok &= r({800, 480}, "renders/micra_cleaning_on_800x480.png", 1, ui::kSectionMicraCleaning);
  brew.set_flush_s(0);
  // Micra > Schedule. The gate needs a synced clock, which the fake network
  // only reports while posed (so Stats > Info keeps its honest "never").
  network.set_synced(true);
  schedule.pose_daily();
  ok &= r({800, 480}, "renders/micra_schedule_800x480.png", 1, ui::kSectionMicraSchedule);
  ok &= r({320, 240}, "renders/micra_schedule_320x240.png", 1, ui::kSectionMicraSchedule);
  // Schedule > Configure schedule: the times page, daily and per weekday.
  ok &= r({800, 480}, "renders/micra_schedule_times_800x480.png", 1,
          ui::kSectionMicraScheduleTimes);
  ok &= r({320, 240}, "renders/micra_schedule_times_320x240.png", 1,
          ui::kSectionMicraScheduleTimes);
  schedule.pose_per_day();
  ok &= r({800, 480}, "renders/micra_schedule_perday_800x480.png", 1, ui::kSectionMicraSchedule);
  ok &= r({800, 480}, "renders/micra_schedule_days_800x480.png", 1,
          ui::kSectionMicraScheduleTimes);
  ok &= r({320, 240}, "renders/micra_schedule_days_320x240.png", 1,
          ui::kSectionMicraScheduleTimes);
  schedule.pose_per_day();  // Wednesday off: selecting it greys its window
  ok &= r({800, 480}, "renders/micra_schedule_dayoff_800x480.png", 1,
          ui::kSectionMicraScheduleTimes, false, 0, -1, false, -1, 0, false, false, 0, false,
          false, false, false, 0, 2);
  schedule.pose_daily();
  // First visit: the "turn off the cloud app's schedule" notice (opened from
  // the page-shown hook, so no extra pose is needed).
  schedule.set_warning_dismissed(false);
  ok &= r({800, 480}, "renders/schedule_warn_modal_800x480.png", 1, ui::kSectionMicraSchedule);
  ok &= r({320, 240}, "renders/schedule_warn_modal_320x240.png", 1, ui::kSectionMicraSchedule);
  schedule.set_warning_dismissed(true);
  // Enabled off: everything but the switch greys.
  schedule.pose_daily();
  schedule.pose_disabled();
  ok &= r({800, 480}, "renders/micra_schedule_off_800x480.png", 1, ui::kSectionMicraSchedule);
  schedule.pose_daily();
  // Gated: no trusted time, then no paired machine.
  network.set_synced(false);
  ok &= r({800, 480}, "renders/micra_schedule_nontp_800x480.png", 1, ui::kSectionMicraSchedule);
  network.set_synced(true);
  machine.set_link(core::Link::Unconfigured);
  ok &= r({800, 480}, "renders/micra_schedule_unpaired_800x480.png", 1, ui::kSectionMicraSchedule);
  machine.set_link(core::Link::Connected);
  network.set_synced(false);
  ok &= r({320, 240}, "renders/scale_settings_320x240.png", 1, ui::kSectionScaleSettings);
  ok &= r({800, 480}, "renders/scale_settings_800x480.png", 1, ui::kSectionScaleSettings);
  // Scale > Device settings (stored on the scale): live values, and the
  // link-down state where the rows disable ("--") behind a Connect prompt.
  ok &= r({800, 480}, "renders/scale_device_800x480.png", 1, ui::kSectionScaleDevice);
  scale.set_connected(false);
  scale_provisioner.set_connect_enabled(false);
  ok &= r({800, 480}, "renders/scale_device_disc_800x480.png", 1,
          ui::kSectionScaleDevice);
  scale_provisioner.set_connect_enabled(true);
  scale.set_connected(true);
  // Lunar persona: Unit row + the read-only Mode row (bare text, no chip).
  scale.set_lunar(true);
  ok &= r({800, 480}, "renders/scale_device_lunar_800x480.png", 1,
          ui::kSectionScaleDevice);
  scale.set_lunar(false);
  ok &= r({800, 480}, "renders/micra_bt_800x480.png", 1, ui::kSectionMicraBt);
  ok &= r({320, 240}, "renders/device_320x240.png", 1, ui::kSectionDevice);  // chooser
  ok &= r({800, 480}, "renders/device_800x480.png", 1, ui::kSectionDevice);
  ok &= r({320, 240}, "renders/device_display_320x240.png", 1, ui::kSectionDeviceDisplay);
  ok &= r({320, 240}, "renders/device_time_320x240.png", 1, ui::kSectionDeviceTime);
  ok &= r({320, 240}, "renders/device_wifi_320x240.png", 1, ui::kSectionDeviceWifi);
  ok &= r({800, 480}, "renders/device_display_800x480.png", 1, ui::kSectionDeviceDisplay);
  ok &= r({800, 480}, "renders/device_time_800x480.png", 1, ui::kSectionDeviceTime);
  ok &= r({800, 480}, "renders/device_wifi_800x480.png", 1, ui::kSectionDeviceWifi);

  // Settings > Apollo > Backup: the page in its three card states, then the two
  // confirmations (the whole point of the flow is what those two say).
  backup.pose_empty_card();
  ok &= r({800, 480}, "renders/device_backup_empty_800x480.png", 1, ui::kSectionDeviceBackup);
  backup.pose_backup_present();
  ok &= r({800, 480}, "renders/device_backup_800x480.png", 1, ui::kSectionDeviceBackup);
  ok &= r({1280, 720, 1.5f}, "renders/device_backup_1280x720.png", 1, ui::kSectionDeviceBackup);
  ok &= r({1280, 720, 1.5f}, "renders/backup_modal_1280x720.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 1);
  ok &= r({800, 480}, "renders/backup_modal_800x480.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 1);
  ok &= r({800, 480}, "renders/backup_modal_optout_800x480.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 2);
  ok &= r({800, 480}, "renders/restore_modal_800x480.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 3);
  ok &= r({1280, 720, 1.5f}, "renders/restore_modal_1280x720.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 3);
  backup.pose_backup_present(/*with_wifi=*/false, /*newer=*/false, /*with_token=*/false);
  ok &= r({800, 480}, "renders/restore_modal_optout_800x480.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 3);
  backup.pose_backup_present();
  ok &= r({800, 480}, "renders/backup_working_800x480.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 8);
  ok &= r({800, 480}, "renders/backup_done_800x480.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 5);
  ok &= r({800, 480}, "renders/restore_done_800x480.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 6);
  backup.pose_no_card();
  ok &= r({800, 480}, "renders/backup_no_card_800x480.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 7);
  backup.pose_backup_present(/*with_wifi=*/false, /*newer=*/true);
  ok &= r({800, 480}, "renders/restore_too_new_800x480.png", 1, ui::kSectionDeviceBackup,
          false, 0, -1, false, -1, 0, false, false, false, false, false, false, false, 4);
  backup.pose_backup_present();

  // 7" 1024x600 (ESP32-S3-Touch-LCD-7B): the XL tier.
  ok &= r({1024, 600}, "renders/home_1024x600.png");
  ok &= r({1024, 600}, "renders/settings_1024x600.png", 1);
  ok &= r({1024, 600}, "renders/micra_bt_1024x600.png", 1, ui::kSectionMicraBt);
  ok &= r({1024, 600}, "renders/scale_settings_1024x600.png", 1, ui::kSectionScaleSettings);
  ok &= r({1024, 600}, "renders/device_1024x600.png", 1, ui::kSectionDevice);
  ok &= r({1024, 600}, "renders/device_display_1024x600.png", 1, ui::kSectionDeviceDisplay);

  // 5" 1280x720 (ESP32-P4-WIFI6-Touch-LCD-5): the wide (800x480) layout at
  // 1.5x — high pixel density, so elements keep (slightly exceed) the 4.3"'s
  // physical size instead of shrinking.
  const ui::ScreenProfile p5{1280, 720, 1.5f};
  ok &= r(p5, "renders/home_1280x720.png");
  ok &= r(p5, "renders/settings_1280x720.png", 1);
  ok &= r(p5, "renders/micra_bt_1280x720.png", 1, ui::kSectionMicraBt);
  ok &= r(p5, "renders/device_1280x720.png", 1, ui::kSectionDevice);
  ok &= r(p5, "renders/device_display_1280x720.png", 1, ui::kSectionDeviceDisplay);
  network.set_synced(true);
  schedule.pose_per_day();
  ok &= r(p5, "renders/micra_schedule_days_1280x720.png", 1, ui::kSectionMicraScheduleTimes);
  network.set_synced(false);
  ok &= r(p5, "renders/stats_brew_1280x720.png", 2, -1, false, 0, ui::kStatsBrew);
  // Token modal over Home (modal over Settings hits a known LVGL draw loop).
  ok &= r(p5, "renders/token_modal_1280x720.png", 0, -1, true);
  ok &= r({800, 480}, "renders/token_modal_800x480.png", 0, -1, true);
  // First boot: the welcome over Home, nothing done yet (the fake network
  // reports a saved SSID, so pose it away); then with WiFi done, which drops
  // that button. The button row stacks on compact.
  network.set_saved(false);
  scale_provisioner.set_saved(false);
  ok &= r({800, 480}, "renders/welcome_modal_800x480.png", 0, -1, false, 0, -1, false, -1, 0,
          false, false, 0, false, false, false, false, 0, -1, true);
  ok &= r({320, 240}, "renders/welcome_modal_320x240.png", 0, -1, false, 0, -1, false, -1, 0,
          false, false, 0, false, false, false, false, 0, -1, true);
  network.set_saved(true);
  ok &= r({800, 480}, "renders/welcome_modal_wifi_800x480.png", 0, -1, false, 0, -1, false, -1, 0,
          false, false, 0, false, false, false, false, 0, -1, true);
  scale_provisioner.set_saved(true);
  // Portal-join modal: WIFI: QR + manual instructions (token flow; the WiFi
  // flow shows the same layout with different copy).
  ok &= r({800, 480}, "renders/join_modal_800x480.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, true);
  ok &= r({320, 240}, "renders/join_modal_320x240.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, true);
  ok &= r(p5, "renders/join_modal_1280x720.png", 0, -1, false, 0, -1,
          false, -1, 0, false, false, false, false, true);
  scale_provisioner.set_saved(false);
  ok &= r(p5, "renders/home_noscale_1280x720.png");
  scale_provisioner.set_saved(true);

  // 8" 1280x800 (ESP32-P4-WIFI6-Touch-LCD-X 8"): 1.6x for a logical 800x500 —
  // the 4.3-class width exactly; the extra 20dp of height goes to the
  // flex-grow regions (home hero card, graphs, history list). The X 7" is
  // pixel-identical to the p5 profile above (same 1280x720 @ 1.5x).
  const ui::ScreenProfile x8{1280, 800, 1.6f};
  ok &= r(x8, "renders/home_1280x800.png");
  ok &= r(x8, "renders/settings_1280x800.png", 1);
  ok &= r(x8, "renders/stats_brew_1280x800.png", 2, -1, false, 0, ui::kStatsBrew);
  ok &= r(x8, "renders/stats_history_1280x800.png", 2, -1, false, 0, ui::kStatsHistory);

  // Stats tab (tab 2): graph sections + info.
  ok &= r({320, 240}, "renders/stats_brew_320x240.png", 2, -1, false, 0, ui::kStatsBrew);
  ok &= r({800, 480}, "renders/stats_brew_800x480.png", 2, -1, false, 0, ui::kStatsBrew);
  ok &= r({320, 240}, "renders/stats_info_320x240.png", 2, -1, false, 0, ui::kStatsInfo);
  ok &= r({800, 480}, "renders/stats_info_800x480.png", 2, -1, false, 0, ui::kStatsInfo);
  // Log-viewer modal (Info > Diagnostic log): seed the ring with a plausible
  // boot-and-brew trace so the render shows real content with stamps.
  core::log_ring().set_clock(&clock);
  core::logf("log: ring 64 KB (PSRAM)\n");
  core::logf("Micra remote: sim\n");
  core::logf("reset reason: power-on (1)\n");
  core::logf("Display up: 800 x 480\n");
  core::logf("BLE: connecting to saved Micra F0:E1:D2:C3:B4:A5\n");
  core::logf("Micra connected; machine ON\n");
  core::logf("SCALE: Umbra connected, battery 78%%\n");
  core::logf("Brew: paddle ON edge (phase 0)\n");
  core::logf("Brew: target 36.0g reached in 27.4s -> paddle off\n");
  core::logf("Brew: paddle OFF edge (phase 2)\n");
  core::logf("ShotStore: saved shot 15 (548 samples)\n");
  ok &= r({800, 480}, "renders/stats_log_800x480.png", 2, -1, false, 0, ui::kStatsInfo,
          false, -1, 0, false, true);
  ok &= r({1024, 600}, "renders/stats_brew_1024x600.png", 2, -1, false, 0, ui::kStatsBrew);
  ok &= r({1024, 600}, "renders/stats_boiler_1024x600.png", 2, -1, false, 0, ui::kStatsBoiler);
  ok &= r({1024, 600}, "renders/stats_info_1024x600.png", 2, -1, false, 0, ui::kStatsInfo);

  // Shot history (Stats > History): metrics + filters + list, the guidance
  // card (no SD), and the full-screen shot-card modal.
  ok &= r({800, 480}, "renders/stats_history_800x480.png", 2, -1, false, 0,
          ui::kStatsHistory);
  ok &= r({320, 240}, "renders/stats_history_320x240.png", 2, -1, false, 0,
          ui::kStatsHistory);
  ok &= r({1024, 600}, "renders/stats_history_1024x600.png", 2, -1, false, 0,
          ui::kStatsHistory);
  ok &= r(p5, "renders/stats_history_1280x720.png", 2, -1, false, 0, ui::kStatsHistory);
  shots.set_available(false);
  shots.set_medium(core::MediumState::kNone, "");
  ok &= r({800, 480}, "renders/stats_history_nosd_800x480.png", 2, -1, false, 0,
          ui::kStatsHistory);
  // Unsupported card (e.g. a 64GB+ card still on its factory exFAT).
  shots.set_medium(core::MediumState::kBadFormat, "exFAT");
  ok &= r({800, 480}, "renders/stats_history_exfat_800x480.png", 2, -1, false, 0,
          ui::kStatsHistory);
  shots.set_medium(core::MediumState::kOk, "FAT");
  shots.set_available(true);
  // Month filter engaged (May 2026 from the canned data).
  ok &= r({800, 480}, "renders/stats_history_may_800x480.png", 2, -1, false, 0,
          ui::kStatsHistory, false, -1, 202605);
  // Stats reset 3 days ago: Total card caption flips to "Since ...".
  shots.set_stats_since(1781528100ll - 3 * 86400);
  ok &= r({800, 480}, "renders/stats_history_reset_800x480.png", 2, -1, false, 0,
          ui::kStatsHistory);
  shots.set_stats_since(0);
  // Card full: the capacity footer goes loud (saves are being dropped).
  shots.set_storage(32000000000ull, 1000000ull, true);
  ok &= r({800, 480}, "renders/stats_history_full_800x480.png", 2, -1, false, 0,
          ui::kStatsHistory);
  shots.set_storage(32000000000ull, 12400000000ull, false);
  ok &= r({800, 480}, "renders/shot_card_800x480.png", 2, -1, false, 0,
          ui::kStatsHistory, false, 14);
  ok &= r({320, 240}, "renders/shot_card_320x240.png", 2, -1, false, 0,
          ui::kStatsHistory, false, 14);

  // Theme previews: Home in every color scheme, plus a Device panel in one alt
  // scheme to show themed controls + scrollbar.
  for (int i = 0; i < ui::theme::count(); ++i) {
    char path[64];
    std::snprintf(path, sizeof(path), "renders/theme%d_320x240.png", i);
    ok &= r({320, 240}, path, 0, -1, false, i);
  }
  ok &= r({320, 240}, "renders/device_espresso_320x240.png", 1,
          ui::kSectionDeviceDisplay, false, 2);

  // Exercise the on-disk store format end-to-end: push one canned record
  // through DirShotStore -> sim_sd/Apollo2 — the exact CSV files the device
  // writes, inspectable on the laptop.
  {
    core::ShotRecord rec;
    if (shots.read(14, rec)) {
      host::DirShotStore dir_store("sim_sd");
      dir_store.save(rec);
      std::printf("wrote sim_sd/%s (index + samples)\n", core::kShotDirName);
      // Exercise remove(): save a second record and delete it again — the
      // samples unlink + index rewrite is the same flow the device store runs.
      const int before = dir_store.count();
      dir_store.save(rec);
      core::ShotSummary newest;
      if (dir_store.list(&newest, 1, 0) != 1 || !dir_store.remove(newest.id) ||
          dir_store.count() != before) {
        std::fprintf(stderr, "error: DirShotStore remove() round-trip failed\n");
        ok = false;
      }
    } else {
      ok = false;
    }
  }
  return ok ? 0 : 1;
}
