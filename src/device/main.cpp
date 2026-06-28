#include <Arduino.h>
#include <lvgl.h>

#include "platform_esp32/board_config.h"
#include "platform_esp32/config.h"
#include "platform_esp32/display.h"
#include "platform_esp32/micra_link.h"
#include "platform_esp32/provisioner.h"
#include "platform_esp32/touch.h"
#include "secrets.h"
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
platform::MicraLink g_micra{MICRA_BLE_TOKEN};
platform::Provisioner g_provisioner{g_micra, g_config};
ui::App g_app;

constexpr uint32_t kUiRefreshMs = 500;

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);  // let USB-CDC enumerate
  Serial.println();
  Serial.printf("Micra remote — %s\n", board::kName);

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

  // Build the UI bound to the machine (renders the initial Disconnected state
  // and routes the power button to g_micra.set_power()).
  const ui::ScreenProfile screen{g_display.width(), g_display.height()};
  g_app.build(g_micra, g_provisioner, screen);

  // Start the background BLE task using the saved MAC (empty on first boot ->
  // the UI shows "Set up in Settings" until one is saved via the scan).
  const std::string mac = g_config.mac();
  Serial.printf("Saved machine MAC: %s\n", mac.empty() ? "(none)" : mac.c_str());
  g_micra.begin(mac);
}

void loop() {
  lv_timer_handler();  // LVGL render/input

  // Reflect the latest cached machine state in the UI (cheap; no BLE here).
  static uint32_t last = 0;
  if (millis() - last > kUiRefreshMs) {
    last = millis();
    g_app.refresh();
  }

  delay(5);
}
