#include <Arduino.h>
#include <lvgl.h>

#include "platform_esp32/board_config.h"
#include "platform_esp32/display.h"
#include "platform_esp32/micra_link.h"
#include "platform_esp32/touch.h"
#include "secrets.h"
#include "ui/app.h"

// Device entry. Brings up the panel + touch, builds the UI bound to the BLE
// machine, connects, and polls the machine on a timer to keep the UI live.
//
// Connection is still blocking in setup() and there's no disconnected-state UI
// or reconnect yet — that's the next step (connection manager).

namespace {

platform::Display g_display;
platform::Touch g_touch;
platform::MicraLink g_micra{MICRA_BLE_TOKEN};
ui::App g_app;

constexpr uint32_t kPollIntervalMs = 3000;

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

  // Build the UI bound to the machine (renders its current — initially empty —
  // state, and routes the power button to g_micra.set_power()).
  const ui::ScreenProfile screen{g_display.width(), g_display.height()};
  g_app.build(g_micra, screen);

  // Connect, then push the first real reading into the UI.
  if (g_micra.connect(MICRA_BLE_ADDRESS) && g_micra.refresh()) {
    Serial.println("Micra: connected");
    g_app.refresh();
  } else {
    Serial.println("Micra: connect/auth/read did not complete (see log above)");
  }
}

void loop() {
  lv_timer_handler();  // LVGL render/input

  // Poll the machine periodically and push fresh values into the UI.
  static uint32_t last = 0;
  if (g_micra.isConnected() && millis() - last > kPollIntervalMs) {
    last = millis();
    if (g_micra.refresh()) g_app.refresh();
  }

  delay(5);
}
