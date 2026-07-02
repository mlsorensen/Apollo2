// Host simulator entry point. Renders the UI for each screen profile we support
// into a PNG for visual inspection, then exits. Run via tools/sim.sh.
//
// This file is the host counterpart of src/device/main.cpp: both wire a
// concrete platform (here: FakeMachine + PngDisplay) to the portable UI.

#include <cstdio>
#include <filesystem>

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
#include "platform_host/png_display.h"
#include "ui/app.h"
#include "ui/screen.h"
#include "ui/theme.h"

namespace {

bool render(core::IMachine& machine, core::IProvisioner& provisioner,
            core::IBattery& battery, core::IDisplaySettings& disp_settings,
            core::IClock& clock, core::IHistory& history, core::IScale& scale,
            core::IScaleProvisioner& scale_provisioner, core::IBrewController& brew,
            core::INetwork& network, ui::ScreenProfile screen, const char* out_path,
            int tab = 0, int settings_section = -1, bool token_modal = false,
            int theme = 0, int stats_section = -1) {
  std::filesystem::path p(out_path);
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

  disp_settings.set_theme(theme);  // build() reads this into ui::theme::set_active
  host::PngDisplay display(screen.width, screen.height);
  ui::App app;
  app.build(machine, provisioner, battery, disp_settings, clock, history, scale,
            scale_provisioner, brew, network, screen);
  app.show_tab(tab);
  if (settings_section >= 0) app.select_settings_section(settings_section);
  if (stats_section >= 0) app.select_stats_section(stats_section);
  if (token_modal) app.open_token_setup();
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

  // One PNG per supported layout. Add a line here when a new form factor lands.
  auto r = [&](ui::ScreenProfile s, const char* path, int tab = 0, int sec = -1,
               bool modal = false, int theme = 0, int stats = -1) {
    return render(machine, provisioner, battery, disp, clock, history, scale,
                  scale_provisioner, brew, network, s, path, tab, sec, modal, theme, stats);
  };
  bool ok = true;
  ok &= r({800, 480}, "renders/home_800x480.png");
  ok &= r({320, 240}, "renders/home_320x240.png");
  // Oscilloscope-style graph: age ticks hidden, single window caption instead.
  disp.set_scope_graph(true);
  ok &= r({800, 480}, "renders/home_scope_800x480.png");
  disp.set_scope_graph(false);
  // No-scale Home (classic layout) — toggle the fake to "no scale saved".
  scale_provisioner.set_saved(false);
  ok &= r({320, 240}, "renders/home_noscale_320x240.png");
  ok &= r({800, 480}, "renders/home_noscale_800x480.png");
  ok &= r({1024, 600}, "renders/home_noscale_1024x600.png");
  scale_provisioner.set_saved(true);
  ok &= r({800, 480}, "renders/settings_800x480.png", 1);
  ok &= r({320, 240}, "renders/settings_320x240.png", 1);
  ok &= r({320, 240}, "renders/micra_320x240.png", 1, ui::kSectionMicra);  // chooser
  ok &= r({320, 240}, "renders/micra_bt_320x240.png", 1, ui::kSectionMicraBt);
  ok &= r({320, 240}, "renders/micra_settings_320x240.png", 1, ui::kSectionMicraSettings);
  ok &= r({320, 240}, "renders/scale_bt_320x240.png", 1, ui::kSectionScaleBt);
  ok &= r({320, 240}, "renders/scale_settings_320x240.png", 1, ui::kSectionScaleSettings);
  ok &= r({800, 480}, "renders/micra_bt_800x480.png", 1, ui::kSectionMicraBt);
  ok &= r({320, 240}, "renders/device_320x240.png", 1, ui::kSectionDevice);

  // 7" 1024x600 (ESP32-S3-Touch-LCD-7B): the XL tier.
  ok &= r({1024, 600}, "renders/home_1024x600.png");
  ok &= r({1024, 600}, "renders/settings_1024x600.png", 1);
  ok &= r({1024, 600}, "renders/micra_bt_1024x600.png", 1, ui::kSectionMicraBt);
  ok &= r({1024, 600}, "renders/scale_settings_1024x600.png", 1, ui::kSectionScaleSettings);
  ok &= r({1024, 600}, "renders/device_1024x600.png", 1, ui::kSectionDevice);

  // Stats tab (tab 2): graph sections + info.
  ok &= r({320, 240}, "renders/stats_brew_320x240.png", 2, -1, false, 0, ui::kStatsBrew);
  ok &= r({320, 240}, "renders/stats_info_320x240.png", 2, -1, false, 0, ui::kStatsInfo);
  ok &= r({1024, 600}, "renders/stats_brew_1024x600.png", 2, -1, false, 0, ui::kStatsBrew);
  ok &= r({1024, 600}, "renders/stats_boiler_1024x600.png", 2, -1, false, 0, ui::kStatsBoiler);
  ok &= r({1024, 600}, "renders/stats_info_1024x600.png", 2, -1, false, 0, ui::kStatsInfo);

  // Theme previews: Home in every color scheme, plus a Device panel in one alt
  // scheme to show themed controls + scrollbar.
  for (int i = 0; i < ui::theme::count(); ++i) {
    char path[64];
    std::snprintf(path, sizeof(path), "renders/theme%d_320x240.png", i);
    ok &= r({320, 240}, path, 0, -1, false, i);
  }
  ok &= r({320, 240}, "renders/device_espresso_320x240.png", 1, ui::kSectionDevice, false, 2);
  return ok ? 0 : 1;
}
