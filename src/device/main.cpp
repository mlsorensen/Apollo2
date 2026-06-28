#include <Arduino.h>
#include <lvgl.h>

#include "core/machine.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/display.h"
#include "platform_esp32/micra_link.h"
#include "platform_esp32/touch.h"
#include "secrets.h"
#include "ui/app.h"

// Device entry. Brings up the panel, then renders the real UI for this board's
// screen profile. Machine data is a placeholder until the BLE link (Stage 3)
// feeds in live values; touch is wired in a following increment.

namespace {

platform::Display g_display;
platform::Touch g_touch;
platform::MicraLink g_micra{MICRA_BLE_TOKEN};

// Placeholder until MicraLink (core::IMachine over BLE) provides real state.
core::MachineSnapshot demo_snapshot() {
  return core::MachineSnapshot{
      .name = "Linea Micra",
      .power = core::Power::On,
      .brew_temp_c = 93.0f,
      .brew_target_c = 93.0f,
      .boiler_temp_c = 123.0f,
      .boiler_target_c = 125.0f,
      .brewing = false,
      .status = "Ready (demo)",
  };
}

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

  const ui::ScreenProfile screen{g_display.width(), g_display.height()};
  ui::create_app(demo_snapshot(), screen);

  // Stage-3 step 2: connect to the known machine, authenticate, read state.
  // Serial-only validation of the protocol; UI integration comes next.
  if (g_micra.connect(MICRA_BLE_ADDRESS) && g_micra.refresh()) {
    const core::MachineSnapshot s = g_micra.snapshot();
    Serial.printf("Micra: power=%d brew=%.1f/%.1f boiler=%.1f/%.1f status=%s\n",
                  static_cast<int>(s.power), s.brew_temp_c, s.brew_target_c,
                  s.boiler_temp_c, s.boiler_target_c, s.status);
  } else {
    Serial.println("Micra: connect/auth/read did not complete (see log above)");
  }
}

void loop() {
  lv_timer_handler();  // let LVGL render/refresh

  // Poll machine state every 5 s and print, so we can watch live values.
  static uint32_t last = 0;
  if (g_micra.isConnected() && millis() - last > 5000) {
    last = millis();
    if (g_micra.refresh()) {
      const core::MachineSnapshot s = g_micra.snapshot();
      Serial.printf("Micra: brew %.1f/%.1f  boiler %.1f/%.1f  power=%d\n",
                    s.brew_temp_c, s.brew_target_c, s.boiler_temp_c,
                    s.boiler_target_c, static_cast<int>(s.power));
    }
  }
  delay(5);
}
