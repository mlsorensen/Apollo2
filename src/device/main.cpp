#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Wire.h>
#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
#include <esp32-hal-hosted.h>  // hostedInitBLE(): SDIO link to the radio co-processor
#endif
#include <esp_sleep.h>
#include <soc/soc_caps.h>
#if SOC_PM_SUPPORT_EXT0_WAKEUP
#include <driver/rtc_io.h>
#endif
#include <lvgl.h>

#include "core/brew_controller.h"
#include "platform_esp32/battery.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/clock.h"
#include "platform_esp32/config.h"
#include "platform_esp32/display.h"
#include "platform_esp32/display_settings.h"
#include "platform_esp32/history.h"
#include "platform_esp32/io_extension.h"
#include "platform_esp32/micra_link.h"
#include "platform_esp32/network.h"
#include "platform_esp32/paddle.h"
#include "platform_esp32/provisioner.h"
#include "platform_esp32/scale_link.h"
#include "platform_esp32/sound.h"
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
platform::ScaleLink g_scale;            // NimBLE Bluetooth scale (Bookoo/Acaia)
platform::ScaleProvisioner g_scale_provisioner{g_scale, g_config};
// Brew-by-weight: paddle relay + shot state machine over the paddle + scale
// ports (core logic; polled from loop()).
core::BrewController g_brew{platform::paddle(), g_scale};
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
#if SOC_PM_SUPPORT_EXT0_WAKEUP
    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);
    esp_sleep_enable_ext0_wakeup(pin, 0);  // wake when INT goes low (a touch)
#elif SOC_PM_SUPPORT_EXT1_WAKEUP
    // Chips without EXT0 (e.g. the P4): EXT1 with a one-pin mask, wake on low.
    esp_sleep_enable_ext1_wakeup(1ULL << pin, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
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
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // Native USB-CDC: with no serial host attached, writes otherwise BLOCK on a TX
  // timeout until the buffer drains — which stalls the main loop (and thus touch/
  // rendering) whenever anything logs. 0 = never block (drop if no reader). This
  // is why the UI felt sluggish until a serial monitor was connected. (Boards on
  // a real UART bridge — e.g. the P4 — don't have or need this.)
  Serial.setTxTimeoutMs(0);
#endif

  // Woke from a low-battery park (a touch or a reset)? Read the pack WITHOUT
  // powering up the panel/LVGL/BLE and REFUSE to fully boot unless it has charged
  // past the resume threshold (hysteresis above the cutoff, so the at-rest voltage
  // rebound doesn't count) — otherwise go straight back to sleep. This avoids a
  // half-up, backlight-off zombie that would only brown out again; the device
  // stays dark until there's genuinely enough power to run.
  if (g_lowbatt_sleep) {
    batt_only_init();
    const core::BatteryState b = g_battery.battery();
    // On USB (node at/above the USB threshold) or rested past the resume level
    // -> boot. A pack still charging below the resume level stays parked; it'll
    // pass the threshold as it charges.
    if (!(b.usb || b.volts >= board::kBatteryResumeVolts)) enter_lowbatt_sleep();
    g_lowbatt_sleep = 0;  // charged enough -> fall through to a normal boot
  }

  delay(300);  // let USB-CDC enumerate
  Serial.println();
  Serial.printf("Micra remote — %s\n", board::kName);
  {
    // Why did we boot? Distinguishes power-rail dips (BROWNOUT/POWERON — e.g.
    // the USB<->battery switchover glitch on the P4 4.3) from firmware faults.
    const esp_reset_reason_t rr = esp_reset_reason();
    const char* name = "other";
    switch (rr) {
      case ESP_RST_POWERON:   name = "power-on"; break;
      case ESP_RST_SW:        name = "software"; break;
      case ESP_RST_PANIC:     name = "panic"; break;
      case ESP_RST_INT_WDT:
      case ESP_RST_TASK_WDT:
      case ESP_RST_WDT:       name = "watchdog"; break;
      case ESP_RST_BROWNOUT:  name = "brownout"; break;
      case ESP_RST_DEEPSLEEP: name = "deep-sleep wake"; break;
      case ESP_RST_USB:       name = "usb"; break;
      default: break;
    }
    Serial.printf("reset reason: %s (%d)\n", name, static_cast<int>(rr));
  }
  g_config.begin();  // create NVS namespace on first boot (quiets read errors)

#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
  // Hosted-radio boards (P4 + C6 over SDIO): the transport must be brought up
  // through Arduino's hosted HAL (esp_hosted_init + connect_to_slave + BT
  // controller RPC) before any NimBLE call. esp-nimble-cpp's init alone lands
  // in the vhci driver with the SDIO link down -> "card init failed" + abort.
  Serial.println("hosted radio: bringing up SDIO link to co-processor...");
  if (!hostedInitBLE()) {
    Serial.println("ERROR: hosted radio init failed; BLE unavailable");
  } else {
    Serial.println("hosted radio: link up");
  }
#endif

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

  // Paddle hardware (after the display so the IO extension is begun where the
  // paddle rides it). Seed the shot config from NVS and wire the persisters.
  platform::paddle().begin();
  g_brew.seed(g_config.target_weight_g(), g_config.shot_mode(), g_config.overshoot_g(),
              g_config.review_hold_s());
  // A paddle flip while the connected Micra sits in standby only WAKES it (no
  // water moves) — tell the controller so it passes the flip through without
  // starting a phantom shot. Only a KNOWN not-on state counts; disconnected or
  // unknown falls back to normal shot handling.
  g_brew.set_standby_provider([] {
    const core::MachineSnapshot s = g_micra.snapshot();
    return s.link == core::Link::Connected && s.power != core::Power::On;
  });
  g_brew.set_target_persister([](float g) { g_config.set_target_weight_g(g); });
  g_brew.set_shot_mode_persister([](bool on) { g_config.set_shot_mode(on); });
  g_brew.set_overshoot_persister([](float g) { g_config.set_overshoot_g(g); });
  g_brew.set_review_hold_persister([](int s) { g_config.set_review_hold_s(s); });
  Serial.printf("Paddle: %s\n",
                platform::paddle().available() ? "available" : "not wired on this board");
  // Restore saved brightness where dimmable; otherwise hold the backlight at max
  // (an on/off-only board has no brightness control in the UI).
  g_display.set_brightness(board::kSupportsBrightness ? g_config.brightness() : 100);

  // Speaker (audio boards): codec + I2S set up front so button clicks are
  // instant. Needs Wire + the IO extension, which the display init above
  // brought up. Gated on the setting so "Button sounds" OFF + Restart leaves
  // the whole audio stack cold — the escape hatch if audio ever interferes
  // with BLE again (the always-clocking first cut broke connects).
  if (g_config.click_sound()) platform::sound_begin();

  // Build the UI bound to the machine + provisioner + battery + display.
  const ui::ScreenProfile screen{g_display.width(), g_display.height()};
  g_app.build(g_micra, g_provisioner, g_battery, g_display_settings, g_clock, g_history,
              g_scale, g_scale_provisioner, g_brew, g_network, platform::sound(), screen);

  // Settings > Device "Restart": soft reboot — re-runs the whole panel init,
  // the escape hatch for the RGB panel's occasional shifted-raster boot glitch.
  g_app.set_restart_handler([] {
    Serial.println("User-requested restart");
    Serial.flush();
    esp_restart();
  });

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
  // Opt-in auto-connect (Micra > Settings): grab the saved machine at boot
  // instead of waiting for a Connect tap. Default off — a connected remote
  // occupies the Micra's single BLE slot.
  if (!mac.empty() && g_config.auto_connect()) g_micra.set_connect_enabled(true);
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
  g_brew.poll(millis());     // paddle relay + shot state machine (edge-critical)
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
