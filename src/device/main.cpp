#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstdlib>
#include <cstring>
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

#include <esp_heap_caps.h>

#include "core/brew_controller.h"
#include "core/log_ring.h"
#include "core/system.h"
#include "platform_esp32/battery.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/clock.h"
#include "platform_esp32/config.h"
#include "platform_esp32/display.h"
#include "platform_esp32/display_settings.h"
#include "platform_esp32/history.h"
#include "platform_esp32/io_extension.h"
#include "platform_esp32/log_setup.h"
#include "platform_esp32/micra_link.h"
#include "platform_esp32/network.h"
#include "platform_esp32/paddle.h"
#include "platform_esp32/provisioner.h"
#include "platform_esp32/scale_link.h"
#include "platform_esp32/shot_store.h"
#include "platform_esp32/sound.h"
#include "platform_esp32/scale_provisioner.h"
#include "platform_esp32/token_setup.h"
#include "platform_esp32/touch.h"
#include "platform_esp32/web_ui.h"
#include "ui/app.h"
#include "ui/theme.h"
#include "version.h"

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
// Shot history: SD-backed on boards with a card slot; NullShotStore elsewhere
// (the History tab shows guidance instead).
#if defined(BOARD_HAS_SD_MMC)
platform::ShotStore g_shots;
#else
core::NullShotStore g_shots;
#endif
platform::WebUi g_web_ui;  // the ONE HTTP server (setup portal + history app)
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

  // Log ring before the first diagnostic, so the banner onward is replayable
  // from Stats > Info or http://<ip>/log after the fact.
  platform::log_init();

  delay(300);  // let USB-CDC enumerate
  core::logf("\n");
  core::logf("Micra remote — %s\n", board::kName);
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
    core::logf("reset reason: %s (%d)\n", name, static_cast<int>(rr));
  }
  g_config.begin();  // create NVS namespace on first boot (quiets read errors)

#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
  // Hosted-radio boards (P4 + C6 over SDIO): the transport must be brought up
  // through Arduino's hosted HAL (esp_hosted_init + connect_to_slave + BT
  // controller RPC) before any NimBLE call. esp-nimble-cpp's init alone lands
  // in the vhci driver with the SDIO link down -> "card init failed" + abort.
  core::logf("hosted radio: bringing up SDIO link to co-processor...\n");
  if (!hostedInitBLE()) {
    core::logf("ERROR: hosted radio init failed; BLE unavailable\n");
  } else {
    core::logf("hosted radio: link up\n");
  }
#endif

  if (!g_display.begin()) {
    core::logf("ERROR: display init failed\n");
    return;
  }
  core::logf("Display up: %d x %d\n", g_display.width(), g_display.height());

  if (g_touch.begin(g_display.width(), g_display.height())) {
    core::logf("Touch up: CST816\n");
  } else {
    core::logf("WARN: CST816 touch not detected on I2C\n");
  }

  g_battery.begin();
  g_clock.begin();  // seed wall-clock from the RTC (if any); I2C is up via Display
  core::log_ring().set_clock(&g_clock);  // wall-clock stamps from here on

  // Paddle hardware (after the display so the IO extension is begun where the
  // paddle rides it; pins honor the per-unit NVS "padsense"/"paddrive" repair
  // overrides — see poll_serial_id). Seed the shot config from NVS and wire
  // the persisters.
  platform::paddle().begin(g_config.paddle_sense_pin(), g_config.paddle_drive_pin());
  g_brew.seed(g_config.target_weight_g(), g_config.shot_mode(), g_config.overshoot_g(),
              g_config.review_hold_s(), g_config.wired_paddle(), g_config.flush_s(),
              g_config.flush_delay_s(), g_config.hint_overshoot_g());
  // A paddle flip while the connected Micra sits in standby only WAKES it (no
  // water moves) — tell the controller so it passes the flip through without
  // starting a phantom shot. Only a KNOWN not-on state counts; disconnected or
  // unknown falls back to normal shot handling.
  g_brew.set_standby_provider([] {
    const core::MachineSnapshot s = g_micra.snapshot();
    const bool wake_only = s.link == core::Link::Connected && s.power != core::Power::On;
    // This is a PREDICATE, not an edge hook: BrewController::snapshot() asks it
    // (via can_clean) on every call, and pump_scale_chart takes a snapshot every
    // loop iteration — so logging unconditionally printed at frame rate for as
    // long as the machine sat connected in standby. On a UART that write BLOCKS,
    // which is milliseconds stolen from every frame (see the DSI sync's own note
    // in display.cpp) and enough to visibly stall rendering.
    //
    // Log the TRANSITIONS instead: a "shot instead of wake" report still hinges
    // on what link/power read here, and that answer only changes when they do.
    static int last_link = -1, last_power = -1;
    if (static_cast<int>(s.link) != last_link || static_cast<int>(s.power) != last_power) {
      last_link = static_cast<int>(s.link);
      last_power = static_cast<int>(s.power);
      core::logf("Paddle standby check: link=%d power=%s -> %s\n", last_link,
                 s.power == core::Power::On        ? "On"
                 : s.power == core::Power::Standby ? "Standby"
                                                   : "Off",
                 wake_only ? "wake only" : "shot path");
    }
    return wake_only;
  });
  g_brew.set_target_persister([](float g) { g_config.set_target_weight_g(g); });
  g_brew.set_shot_mode_persister([](int mode) { g_config.set_shot_mode(mode); });
  g_brew.set_overshoot_persister([](float g) { g_config.set_overshoot_g(g); });
  g_brew.set_hint_overshoot_persister([](float g) { g_config.set_hint_overshoot_g(g); });
  g_brew.set_review_hold_persister([](int s) { g_config.set_review_hold_s(s); });
  g_brew.set_wired_paddle_persister([](bool on) { g_config.set_wired_paddle(on); });
  g_brew.set_flush_persister([](int s) { g_config.set_flush_s(s); });
  g_brew.set_flush_delay_persister([](int s) { g_config.set_flush_delay_s(s); });
  core::logf("Paddle: %s\n",
             platform::paddle().available() ? "available" : "not wired on this board");
  // Restore saved brightness where dimmable; otherwise hold the backlight at max
  // (an on/off-only board has no brightness control in the UI).
  g_display.set_brightness(board::kSupportsBrightness ? g_config.brightness() : 100);

  // Speaker (audio boards): codec + I2S set up front so button clicks are
  // instant. Needs Wire + the IO extension, which the display init above
  // brought up. Gated on the sound settings so turning them ALL off + Restart
  // leaves the whole audio stack cold — the escape hatch if audio ever
  // interferes with BLE again (the always-clocking first cut broke connects).
  // Report what the audio stack costs in INTERNAL ram. The P4 5" runs close to
  // the ceiling (720x1280 framebuffers + hosted WiFi/BLE + the DSI sync's DMA
  // descriptor lists), and there a few KB decides whether esp_lcd can allocate
  // a GDMA link list at all. A silent starvation bug is the expensive kind.
  {
    const unsigned before = static_cast<unsigned>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (g_config.click_sound() || g_config.ready_chime_volume() > 0) platform::sound_begin();
    const unsigned after = static_cast<unsigned>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    core::logf("heap: internal free=%u after sound (cost %d, largest=%u)\n", after,
               static_cast<int>(before) - static_cast<int>(after),
               static_cast<unsigned>(heap_caps_get_largest_free_block(
                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  }

  // Build the UI bound to the machine + provisioner + battery + display.
  const ui::ScreenProfile screen{g_display.width(), g_display.height(),
                                 board::kUiScale};
#if defined(BOARD_HAS_SD_MMC)
  g_shots.begin();  // writer task; mounts lazily, so no card is fine
#endif
  // One web server for everything: the setup portal's pages ride it during
  // AP sessions; on the home network it serves the shot-history app + API.
  // The theme callback reads the ACTIVE palette per request, so the web page
  // always matches whatever the screen is showing.
  g_web_ui.begin(g_token_setup, g_shots, g_clock, "Apollo 2", [] {
    return platform::WebTheme{
        ui::theme::bg(),   ui::theme::rail(),  ui::theme::card(),
        ui::theme::text(), ui::theme::muted(), ui::theme::accent(),
        ui::theme::ok(),   ui::theme::warn(),  ui::theme::alert()};
  });
  g_app.build(g_micra, g_provisioner, g_battery, g_display_settings, g_clock, g_history,
              g_scale, g_scale_provisioner, g_brew, g_network, platform::sound(), g_shots,
              screen);

  // Settings "Restart display": on RGB boards this is a panel DMA resync, not
  // a reboot — the shifted/ghosted raster is a latched bounce-buffer underrun,
  // and esp_lcd_rgb_panel_restart() realigns it at the next VSYNC in place.
  // Non-RGB boards (and a missing panel handle) fall back to the old full
  // soft reboot.
  g_app.set_restart_handler([] {
    if (g_display.rgb_resync()) return;
    core::logf("User-requested restart\n");
    Serial.flush();
    esp_restart();
  });

  // Critically-low battery -> deep sleep instead of brown-out thrashing. Kill the
  // backlight first (dominant load, can latch on in sleep), then park (~uA) until a
  // touch. The dark check above gates the next boot on actually being charged.
  g_app.set_low_battery_handler(board::kBatteryCutoffVolts, [] {
    core::logf("Battery critical -> deep sleep; charge, then touch to wake\n");
    Serial.flush();
    g_display.set_brightness(0);
    enter_lowbatt_sleep();
  });

  // Bring up NimBLE once here (single-threaded), so the Micra + scale link tasks
  // — which each guard on isInitialized() — share one host without racing init.
  NimBLEDevice::init("micra-remote");

  // Radio arbitration: the host refuses to scan while ANY connect is pending,
  // so each link's scan preempts the OTHER link's connect attempts (cancels an
  // in-flight one, holds new ones off until the scan finishes). Without this,
  // a Settings scan silently returned nothing whenever the other device sat in
  // "Connecting" (user-reported on the 4.3C).
  g_micra.set_scan_peer_pauser([](bool on) { g_scale.pause_connects(on); });
  g_scale.set_scan_peer_pauser([](bool on) { g_micra.pause_connects(on); });

  // Seed the link from saved config, then start the background BLE task. With
  // no MAC -> Unconfigured; MAC but no token -> NeedsToken (Settings "Setup").
  const std::string mac = g_config.mac();
  g_micra.set_name(g_config.name());
  g_micra.set_token(g_config.token());
  g_micra.set_token_persister([](std::string t) { g_config.set_token(t); });  // pairing-read
  core::logf("Saved machine: mac=%s token=%s\n", mac.empty() ? "(none)" : mac.c_str(),
             g_config.token().empty() ? "(none)" : "set");
  // Opt-in auto-connect (Micra > Settings): grab the saved machine at boot
  // instead of waiting for a Connect tap. Default off — a connected remote
  // occupies the Micra's single BLE slot.
  if (!mac.empty() && g_config.auto_connect()) g_micra.set_connect_enabled(true);
  g_micra.begin(mac);

  // Start the Bluetooth scale link from its saved MAC (empty -> idles).
  const std::string scale_mac = g_config.scale_mac();
  g_scale.set_name(g_config.scale_name());
  core::logf("Saved scale: mac=%s\n", scale_mac.empty() ? "(none)" : scale_mac.c_str());
  g_scale.begin(scale_mac);

  // Join home WiFi (if enabled) for NTP time; idles otherwise. WiFi coexists with
  // NimBLE on the S3, so this is safe alongside the BLE links.
  g_network.begin();
}

namespace {

// Serial identity query: the web flasher (site/) writes "id?" down the serial
// port to learn which board it's talking to before offering a firmware image
// (the boards are indistinguishable over USB otherwise). Keep the reply a
// single stable line — the page substring-matches the board name out of it,
// exactly like tools/flash.sh does with the boot banner.
void poll_serial_id() {
  static char buf[16];
  static size_t n = 0;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      buf[n] = '\0';
      if (n == 3 && std::strcmp(buf, "id?") == 0) {
        core::logf("APOLLO2 BOARD=\"%s\" FW=%s\n", board::kName, fw::kVersion);
      } else if (std::strncmp(buf, "padsense=", 9) == 0 ||
                 std::strncmp(buf, "paddrive=", 9) == 0) {
        // Per-UNIT paddle GPIO overrides (NVS; survive reflashes). Repair
        // knobs for a unit with a damaged pad: "padsense=50" / "paddrive=49"
        // move that wire's pin, "=-1" reverts to the board default. Applied
        // at next boot (paddle().begin() reads them once).
        const bool sense = buf[3] == 's';
        const int pin = std::atoi(buf + 9);
        if (sense) {
          g_config.set_paddle_sense_pin(pin);
        } else {
          g_config.set_paddle_drive_pin(pin);
        }
        const char* name = sense ? "padsense" : "paddrive";
        const int fallback = sense ? board::kPaddleSensePin : board::kPaddleDrivePin;
        if (pin >= 0) {
          core::logf("%s: override saved (GPIO%d) — applying now\n", name, pin);
        } else {
          core::logf("%s: override cleared (board default GPIO%d) — "
                     "applying now\n", name, fallback);
        }
        // Re-run begin() with the resolved pins so the change is live
        // immediately (also releases the drive line; harmless outside a shot).
        platform::paddle().begin(g_config.paddle_sense_pin(),
                                 g_config.paddle_drive_pin());
      }
      n = 0;
    } else if (n < sizeof(buf) - 1) {
      buf[n++] = c;
    } else {
      n = 0;  // overlong line: not our query
    }
  }
}

}  // namespace

void loop() {
  poll_serial_id();          // web-flasher "which board is this?" responder
  g_brew.poll(millis());     // paddle relay + shot state machine (edge-critical)
  g_app.pump_scale_chart();  // drain the scale's flow stream into the graph (fast)
  lv_timer_handler();        // LVGL render/input
  g_token_setup.handle();    // portal auto-close timeout
  g_web_ui.poll();           // S3 boards serve here; no-op on the P4s (task)

#if defined(BOARD_DISPLAY_RGB)
  // One-shot raster resync at 2 s for the boot ghost (~1/4 of cold boots come
  // up latched a few px off from underruns while setup floods the PSRAM bus).
  // The resync is VSYNC-aligned (armed here, executed inside vertical
  // blanking by the display's resync task) but still shows a one-frame ~5 px
  // jump — the bounce-buffer refill runs with the timing engine stopped —
  // which is why it's NOT on a periodic timer: the user found a 5 s cadence
  // distracting. Runtime ghosts are healed manually via Settings > "Restart
  // display" instead. (An earlier mid-frame, non-VSYNC-aligned version was
  // far worse: full blank AND could itself latch a fresh ghost — keep the
  // alignment if refactoring.)
  static bool boot_resync_done = false;
  if (!boot_resync_done && millis() >= 2000) {
    boot_resync_done = true;
    g_display.rgb_resync(/*verbose=*/false);
  }
#endif

  // Internal-heap telltale, once a minute: the S3 boards live close to the
  // internal-RAM ceiling (on-chip WiFi+BLE + task stacks + DMA buffers), and
  // every starvation bug so far surfaced as some SILENT downstream failure.
  // With this line in the log, the next one arrives with numbers attached.
  static uint32_t last_heap_log_ms = 0;
  if (millis() - last_heap_log_ms >= 60u * 1000u) {
    last_heap_log_ms = millis();
    core::logf("heap: internal free=%u largest=%u\n",
               static_cast<unsigned>(heap_caps_get_free_size(
                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
               static_cast<unsigned>(heap_caps_get_largest_free_block(
                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  }

  // Hourly last-known-time snapshot -> NVS, so the next boot without a
  // surviving RTC starts approximately right instead of at 1970.
  static uint32_t last_time_save_ms = 0;
  if (millis() - last_time_save_ms >= 3600u * 1000u) {
    last_time_save_ms = millis();
    const std::time_t now_unix = g_clock.now_unix();
    if (now_unix != 0) g_config.set_last_unix(now_unix);
  }
  g_network.poll();          // drive the WiFi station state machine + NTP->RTC

  // Reflect the latest cached machine state in the UI (cheap; no BLE here).
  // NOTE this path re-sets many Home widgets — everything it touches must go
  // through the ui::set_* change-detecting setters (widgets.h): LVGL setters
  // invalidate even for identical values, and the resulting no-op churn
  // doubled every ~500ms render pass on the S3 (visible scroll stutter).
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

  // 1ms (was 5): under load the loop runs exactly once per LVGL frame, so the
  // whole delay lands on every frame's cadence — profiled at ~5ms/frame of
  // pure idle on the 5". 1ms still yields to lower-priority tasks.
  delay(1);
}
