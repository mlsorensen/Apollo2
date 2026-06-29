// Host simulator entry point. Renders the UI for each screen profile we support
// into a PNG for visual inspection, then exits. Run via tools/sim.sh.
//
// This file is the host counterpart of src/device/main.cpp: both wire a
// concrete platform (here: FakeMachine + PngDisplay) to the portable UI.

#include <cstdio>
#include <filesystem>

#include "platform_host/fake_battery.h"
#include "platform_host/fake_display_settings.h"
#include "platform_host/fake_machine.h"
#include "platform_host/fake_provisioner.h"
#include "platform_host/png_display.h"
#include "ui/app.h"
#include "ui/screen.h"

namespace {

bool render(core::IMachine& machine, core::IProvisioner& provisioner,
            core::IBattery& battery, core::IDisplaySettings& disp_settings,
            ui::ScreenProfile screen, const char* out_path, int tab = 0,
            int settings_section = -1, bool token_modal = false) {
  std::filesystem::path p(out_path);
  if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

  host::PngDisplay display(screen.width, screen.height);
  ui::App app;
  app.build(machine, provisioner, battery, disp_settings, screen);
  app.show_tab(tab);
  if (settings_section >= 0) app.select_settings_section(settings_section);
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

  // One PNG per supported layout. Add a line here when a new form factor lands.
  auto r = [&](ui::ScreenProfile s, const char* path, int tab = 0, int sec = -1,
               bool modal = false) {
    return render(machine, provisioner, battery, disp, s, path, tab, sec, modal);
  };
  bool ok = true;
  ok &= r({800, 480}, "renders/home_800x480.png");
  ok &= r({320, 240}, "renders/home_320x240.png");
  ok &= r({800, 480}, "renders/settings_800x480.png", 1);
  ok &= r({320, 240}, "renders/settings_320x240.png", 1);
  ok &= r({320, 240}, "renders/brew_320x240.png", 1, ui::kSectionBrew);
  ok &= r({320, 240}, "renders/boiler_320x240.png", 1, ui::kSectionBoiler);
  ok &= r({800, 480}, "renders/brew_800x480.png", 1, ui::kSectionBrew);
  ok &= r({800, 480}, "renders/boiler_800x480.png", 1, ui::kSectionBoiler);
  ok &= r({320, 240}, "renders/display_320x240.png", 1, ui::kSectionDisplay);
  ok &= r({320, 240}, "renders/token_modal_320x240.png", 1, ui::kSectionBluetooth, true);
  return ok ? 0 : 1;
}
