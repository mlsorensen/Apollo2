#pragma once

#include <WebServer.h>

#include <functional>

#include "core/clock.h"
#include "core/shot_store.h"
#include "platform_esp32/token_setup.h"

namespace platform {

// The device's ONE web server (port 80), serving every HTTP surface on
// different routes so features can't fight over the socket:
//   /            the shot-history app (embedded React bundle) — or, while a
//                setup portal is active, that portal's page (token / WiFi)
//   /save /wifi  the setup portal's POST endpoints (delegated to TokenSetup)
//   /api/summary device name/version, ACTIVE THEME palette, storage, stats
//   /api/shots   the shot index as JSON (chunked; newest first)
//   /api/shot.csv?id=N   one shot's sample series (download)
//
// The server runs on its OWN FreeRTOS task, so a browser pulling the bundle
// or a long chunked response never stalls LVGL (it used to freeze the flow
// graph for the duration of every request when pumped from the main loop).
// Thread notes: the shot store is mutex-guarded (it already serves the SD
// writer task); clock reads are plain time(); the theme callback reads nine
// palette words (a torn read during a theme switch mis-colors one response,
// harmlessly); the portal handlers write NVS (thread-safe) and MicraLink's
// setters, which already tolerate cross-task callers (the BLE task).
struct WebTheme {
  uint32_t bg, rail, card, text, muted, accent, ok, warn, alert;
};

class WebUi {
 public:
  // `theme` is read per request so the page always reflects the palette the
  // screen is showing right now. `name` must outlive this object.
  // Registers routes and spawns the server task. The listening socket is
  // opened lazily by the task once WiFi is in ANY active mode — binding
  // before the network stack exists asserts inside lwip ("Invalid mbox",
  // seen on HW).
  void begin(TokenSetup& setup, core::IShotStore& shots, core::IClock& clock,
             const char* name, std::function<WebTheme()> theme);

 private:
  static void task_entry(void* self);
  void run();  // server task loop: lazy bind + handleClient

  void handle_root();
  void handle_summary();
  void handle_shots();
  void handle_shot_csv();

  WebServer server_{80};
  TokenSetup* setup_ = nullptr;
  core::IShotStore* shots_ = nullptr;
  core::IClock* clock_ = nullptr;
  const char* name_ = "Apollo 2";
  std::function<WebTheme()> theme_;
};

}  // namespace platform
