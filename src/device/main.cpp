#include <Arduino.h>
#include <esp_sleep.h>
#include <lvgl.h>

#include "platform_esp32/battery.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/clock.h"
#include "platform_esp32/config.h"
#include "platform_esp32/display.h"
#include "platform_esp32/display_settings.h"
#include "platform_esp32/history.h"
#include "platform_esp32/micra_link.h"
#include "platform_esp32/provisioner.h"
#include "platform_esp32/token_setup.h"
#include "platform_esp32/touch.h"
#include "ui/app.h"

// Device entry. Brings up the panel + touch, builds the UI bound to the BLE
// machine, and starts MicraLink's background connection task. The main loop only
// runs LVGL and periodically refreshes the UI from MicraLink's cached snapshot —
// all BLE I/O (connect, poll, reconnect) happens off-thread, so the UI stays
// responsive whether the machine is connected, connecting, or offline.

namespace {

platform::Display g_display;
platform::Touch g_touch;
platform::Config g_config;
platform::MicraLink g_micra;
platform::TokenSetup g_token_setup{g_config, g_micra};
platform::Provisioner g_provisioner{g_micra, g_config, g_token_setup};
platform::Battery g_battery;
platform::DisplaySettings g_display_settings{g_display, g_config};
platform::Clock g_clock{g_config};
platform::History g_history;
ui::App g_app;

constexpr uint32_t kUiRefreshMs = 500;
constexpr uint32_t kSampleMs =
    platform::History::kSampleIntervalS * 1000;  // temperature history cadence

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);  // let USB-CDC enumerate
  Serial.println();
  Serial.printf("Micra remote — %s\n", board::kName);
  g_config.begin();  // create NVS namespace on first boot (quiets read errors)

  if (!g_display.begin()) {
    Serial.println("ERROR: display init failed");
    return;
  }
  Serial.printf("Display up: %d x %d\n", g_display.width(), g_display.height());

  if (g_touch.begin(g_display.width(), g_display.height())) {
    Serial.println("Touch up: CST816");
  } else {
    Serial.println("WARN: CST816 touch not detected on I2C");
  }

  g_battery.begin();
  g_display.set_brightness(g_config.brightness());  // restore saved brightness

  // Build the UI bound to the machine + provisioner + battery + display.
  const ui::ScreenProfile screen{g_display.width(), g_display.height()};
  g_app.build(g_micra, g_provisioner, g_battery, g_display_settings, g_clock, g_history,
              screen);

  // Critically-low battery -> deep sleep instead of brown-out thrashing. Wakes on
  // a timer; once plugged in the voltage recovers, so it stops re-sleeping. The
  // backlight is killed first (the dominant load, and it can latch on in sleep).
  g_app.set_low_battery_handler(board::kBatteryCutoffVolts, [] {
    Serial.println("Battery critical -> deep sleep; plug in to wake");
    Serial.flush();
    g_display.set_brightness(0);
    esp_sleep_enable_timer_wakeup(30ULL * 1000000);  // re-check in 30 s
    esp_deep_sleep_start();
  });

  // Seed the link from saved config, then start the background BLE task. With
  // no MAC -> Unconfigured; MAC but no token -> NeedsToken (Settings "Setup").
  const std::string mac = g_config.mac();
  g_micra.set_name(g_config.name());
  g_micra.set_token(g_config.token());
  g_micra.set_token_persister([](std::string t) { g_config.set_token(t); });  // pairing-read
  Serial.printf("Saved machine: mac=%s token=%s\n", mac.empty() ? "(none)" : mac.c_str(),
                g_config.token().empty() ? "(none)" : "set");
  g_micra.begin(mac);
}

void loop() {
  lv_timer_handler();        // LVGL render/input
  g_token_setup.handle();    // pump the WiFi setup web server when active

  // Reflect the latest cached machine state in the UI (cheap; no BLE here).
  static uint32_t last = 0;
  if (millis() - last > kUiRefreshMs) {
    last = millis();
    g_app.refresh();
  }

  // Sample temps into the history ring for the Stats charts (connected only).
  static uint32_t last_sample = 0;
  if (millis() - last_sample > kSampleMs) {
    last_sample = millis();
    const core::MachineSnapshot snap = g_micra.snapshot();
    if (snap.link == core::Link::Connected) {
      g_history.add(snap.brew_temp_c, snap.boiler_temp_c);
    }
  }

  delay(5);
}
