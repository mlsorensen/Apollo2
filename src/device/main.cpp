#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <lvgl.h>

#include "platform_esp32/battery.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/brew_controller.h"
#include "platform_esp32/clock.h"
#include "platform_esp32/config.h"
#include "platform_esp32/display.h"
#include "platform_esp32/display_settings.h"
#include "platform_esp32/history.h"
#include "platform_esp32/io_extension.h"
#include "platform_esp32/micra_link.h"
#include "platform_esp32/network.h"
#include "platform_esp32/provisioner.h"
#include "platform_esp32/scale_link.h"
#include "platform_esp32/scale_provisioner.h"
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
platform::Network g_network{g_config, g_clock, g_token_setup};  // WiFi station + NTP
platform::History g_history;
platform::ScaleLink g_scale;            // NimBLE Bluetooth scale (Bookoo Themis)
platform::ScaleProvisioner g_scale_provisioner{g_scale, g_config};
platform::BrewController g_brew;        // available() iff board has paddle pins; Phase 4 = logic
ui::App g_app;

constexpr uint32_t kUiRefreshMs = 500;
constexpr uint32_t kSampleMs =
    platform::History::kSampleIntervalS * 1000;  // temperature history cadence

// Survives deep sleep (cleared only on a cold boot / power loss): set when we park
// for low battery so the next wake does the dark battery check in setup() before
// powering anything up.
RTC_DATA_ATTR uint32_t g_lowbatt_sleep = 0;

// Deep sleep until a screen touch (or external reset). True power-down (~uA) — no
// timer poll. The touch controller's INT line (idles high, pulls low on touch) is
// the wake source where it's on an RTC GPIO; the 2-inch has no INT wired
// (kTouchInt < 0), so there it wakes only on reset.
void enter_lowbatt_sleep() {
  g_lowbatt_sleep = 1;
  if (board::kTouchInt >= 0) {
    const auto pin = static_cast<gpio_num_t>(board::kTouchInt);
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
    esp_sleep_enable_ext0_wakeup(pin, 0);  // wake when INT goes low (a touch)
  }
  esp_deep_sleep_start();  // never returns
}

// Minimal init to read the pack WITHOUT lighting the screen (the dark check on a
// low-battery wake). The 2-inch reads a direct ADC (no init); the 7B reads via the
// I2C IO-extension, so bring that up but force the backlight off.
void batt_only_init() {
#if defined(BOARD_HAS_IO_EXTENSION)
  Wire.begin(board::kI2cSda, board::kI2cScl);
  platform::io_extension().begin(board::kIoExtAddr);
  platform::io_extension().set(board::kIoExtBacklight, false);
#endif
}

}  // namespace

void setup() {
  Serial.begin(115200);
  // Native USB-CDC: with no serial host attached, writes otherwise BLOCK on a TX
  // timeout until the buffer drains — which stalls the main loop (and thus touch/
  // rendering) whenever anything logs. 0 = never block (drop if no reader). This
  // is why the UI felt sluggish until a serial monitor was connected.
  Serial.setTxTimeoutMs(0);

  // Woke from a low-battery park (a touch or a reset)? Read the pack WITHOUT
  // powering up the panel/LVGL/BLE and REFUSE to fully boot unless it has charged
  // past the resume threshold (hysteresis above the cutoff, so the at-rest voltage
  // rebound doesn't count) — otherwise go straight back to sleep. This avoids a
  // half-up, backlight-off zombie that would only brown out again; the device
  // stays dark until there's genuinely enough power to run.
  if (g_lowbatt_sleep) {
    batt_only_init();
    const core::BatteryState b = g_battery.battery();
    if (!(b.charging || b.volts >= board::kBatteryResumeVolts)) enter_lowbatt_sleep();
    g_lowbatt_sleep = 0;  // charged enough -> fall through to a normal boot
  }

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
  g_clock.begin();  // seed wall-clock from the RTC (if any); I2C is up via Display
  // Restore saved brightness where dimmable; otherwise hold the backlight at max
  // (an on/off-only board has no brightness control in the UI).
  g_display.set_brightness(board::kSupportsBrightness ? g_config.brightness() : 100);

  // Build the UI bound to the machine + provisioner + battery + display.
  const ui::ScreenProfile screen{g_display.width(), g_display.height()};
  g_app.build(g_micra, g_provisioner, g_battery, g_display_settings, g_clock, g_history,
              g_scale, g_scale_provisioner, g_brew, g_network, screen);

  // Critically-low battery -> deep sleep instead of brown-out thrashing. Kill the
  // backlight first (dominant load, can latch on in sleep), then park (~uA) until a
  // touch. The dark check above gates the next boot on actually being charged.
  g_app.set_low_battery_handler(board::kBatteryCutoffVolts, [] {
    Serial.println("Battery critical -> deep sleep; charge, then touch to wake");
    Serial.flush();
    g_display.set_brightness(0);
    enter_lowbatt_sleep();
  });

  // Bring up NimBLE once here (single-threaded), so the Micra + scale link tasks
  // — which each guard on isInitialized() — share one host without racing init.
  NimBLEDevice::init("micra-remote");

  // Seed the link from saved config, then start the background BLE task. With
  // no MAC -> Unconfigured; MAC but no token -> NeedsToken (Settings "Setup").
  const std::string mac = g_config.mac();
  g_micra.set_name(g_config.name());
  g_micra.set_token(g_config.token());
  g_micra.set_token_persister([](std::string t) { g_config.set_token(t); });  // pairing-read
  Serial.printf("Saved machine: mac=%s token=%s\n", mac.empty() ? "(none)" : mac.c_str(),
                g_config.token().empty() ? "(none)" : "set");
  g_micra.begin(mac);

  // Start the Bluetooth scale link from its saved MAC (empty -> idles).
  const std::string scale_mac = g_config.scale_mac();
  g_scale.set_name(g_config.scale_name());
  Serial.printf("Saved scale: mac=%s\n", scale_mac.empty() ? "(none)" : scale_mac.c_str());
  g_scale.begin(scale_mac);

  // Join home WiFi (if enabled) for NTP time; idles otherwise. WiFi coexists with
  // NimBLE on the S3, so this is safe alongside the BLE links.
  g_network.begin();
}

void loop() {
  g_app.pump_scale_chart();  // drain the scale's flow stream into the graph (fast)
  lv_timer_handler();        // LVGL render/input
  g_token_setup.handle();    // pump the WiFi setup web server when active
  g_network.poll();          // drive the WiFi station state machine + NTP->RTC

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
