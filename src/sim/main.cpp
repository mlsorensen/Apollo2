// Host simulator entry point. Renders the UI for each screen profile we support
// into a PNG for visual inspection, then exits. Run via tools/sim.sh.
//
// This file is the host counterpart of src/device/main.cpp: both wire a
// concrete platform (here: FakeMachine + PngDisplay) to the portable UI.

#include <cstdio>
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
#include "platform_host/fake_provisioner.h"
#include "platform_host/fake_scale.h"
#include "platform_host/fake_scale_provisioner.h"
#include "platform_host/fake_shot_store.h"
#include "platform_host/fake_sound.h"
#include "platform_host/png_display.h"
#include "ui/app.h"
#include "ui/screen.h"
#include "ui/theme.h"

namespace {

bool render(core::IMachine& machine, core::IProvisioner& provisioner,
            core::IBattery& battery, core::IDisplaySettings& disp_settings,
            core::IClock& clock, core::IHistory& history, core::IScale& scale,
            core::IScaleProvisioner& scale_provisioner, core::IBrewController& brew,
            core::INetwork& network, core::IShotStore& shots, ui::ScreenProfile screen,
            const char* out_path, int tab = 0, int settings_section = -1,
            bool token_modal = false, int theme = 0, int stats_section = -1,
            bool clean_lock = false, int shot_modal_id = -1, int history_ym = 0,
            bool backflush = false, bool log_modal = false,
            bool unwired_midshot = false, bool toast = false,
            bool scroll_bottom = false) {
  std::filesystem::path p(out_path);
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

  disp_settings.set_theme(theme);  // build() reads this into ui::theme::set_active
  host::PngDisplay display(screen.width, screen.height);
  static host::FakeSound fake_sound;  // stateless; shared across renders
  ui::App app;
  app.build(machine, provisioner, battery, disp_settings, clock, history, scale,
            scale_provisioner, brew, network, fake_sound, shots, screen);
  app.show_tab(tab);
  if (settings_section >= 0) {
    app.select_settings_section(settings_section);
    if (scroll_bottom) app.scroll_settings_to_bottom(settings_section);
  }
  if (stats_section >= 0) app.select_stats_section(stats_section);
  if (history_ym != 0) app.set_history_filter(history_ym);
  if (token_modal) app.open_token_setup();
  if (clean_lock) app.start_clean_lock();
  if (backflush) app.open_backflush();
  if (shot_modal_id >= 0) app.open_shot_card(static_cast<uint32_t>(shot_modal_id));
  if (log_modal) app.open_log_modal();
  if (unwired_midshot) app.pose_unwired_midshot();
  if (toast)
    app.show_toast("Shot not started: Auto shot is enabled. "
                   "Connect the scale or switch to Manual mode.");
  display.render_frame();
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
  host::FakeShotStore shots;

  // One PNG per supported layout. Add a line here when a new form factor lands.
  auto r = [&](ui::ScreenProfile s, const char* path, int tab = 0, int sec = -1,
               bool modal = false, int theme = 0, int stats = -1, bool clean_lock = false,
               int shot_id = -1, int history_ym = 0, bool backflush = false,
               bool log_modal = false, bool unwired_midshot = false,
               bool toast = false, bool scroll_bottom = false) {
    return render(machine, provisioner, battery, disp, clock, history, scale,
                  scale_provisioner, brew, network, shots, s, path, tab, sec, modal, theme,
                  stats, clean_lock, shot_id, history_ym, backflush, log_modal,
                  unwired_midshot, toast, scroll_bottom);
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
          -1, false, -1, 0, false, false, true);
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
  ok &= r({320, 240}, "renders/scale_bt_320x240.png", 1, ui::kSectionScaleBt);
  ok &= r({320, 240}, "renders/scale_settings_320x240.png", 1, ui::kSectionScaleSettings);
  // Scrolled to the tail so the "On the scale" group (beep / auto-off, stored
  // on the scale) is in frame.
  ok &= r({800, 480}, "renders/scale_settings_800x480.png", 1, ui::kSectionScaleSettings,
          false, 0, -1, false, -1, 0, false, false, false, false, true);
  // Saved scale with the link down: the "On the scale" rows disable ("--")
  // behind the Connect prompt.
  scale.set_connected(false);
  scale_provisioner.set_connect_enabled(false);
  ok &= r({800, 480}, "renders/scale_settings_disc_800x480.png", 1,
          ui::kSectionScaleSettings, false, 0, -1, false, -1, 0, false, false,
          false, false, true);
  scale_provisioner.set_connect_enabled(true);
  scale.set_connected(true);
  ok &= r({800, 480}, "renders/micra_bt_800x480.png", 1, ui::kSectionMicraBt);
  ok &= r({320, 240}, "renders/device_320x240.png", 1, ui::kSectionDevice);  // chooser
  ok &= r({800, 480}, "renders/device_800x480.png", 1, ui::kSectionDevice);
  ok &= r({320, 240}, "renders/device_display_320x240.png", 1, ui::kSectionDeviceDisplay);
  ok &= r({320, 240}, "renders/device_time_320x240.png", 1, ui::kSectionDeviceTime);
  ok &= r({320, 240}, "renders/device_wifi_320x240.png", 1, ui::kSectionDeviceWifi);
  ok &= r({800, 480}, "renders/device_display_800x480.png", 1, ui::kSectionDeviceDisplay);
  ok &= r({800, 480}, "renders/device_time_800x480.png", 1, ui::kSectionDeviceTime);
  ok &= r({800, 480}, "renders/device_wifi_800x480.png", 1, ui::kSectionDeviceWifi);

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
  ok &= r(p5, "renders/stats_brew_1280x720.png", 2, -1, false, 0, ui::kStatsBrew);
  // Token modal over Home (modal over Settings hits a known LVGL draw loop).
  ok &= r(p5, "renders/token_modal_1280x720.png", 0, -1, true);
  scale_provisioner.set_saved(false);
  ok &= r(p5, "renders/home_noscale_1280x720.png");
  scale_provisioner.set_saved(true);

  // Stats tab (tab 2): graph sections + info.
  ok &= r({320, 240}, "renders/stats_brew_320x240.png", 2, -1, false, 0, ui::kStatsBrew);
  ok &= r({800, 480}, "renders/stats_brew_800x480.png", 2, -1, false, 0, ui::kStatsBrew);
  ok &= r({320, 240}, "renders/stats_info_320x240.png", 2, -1, false, 0, ui::kStatsInfo);
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
