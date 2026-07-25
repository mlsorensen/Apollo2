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
// Serving model is PER-BOARD, learned on hardware:
//   - P4 (DSI) boards: a dedicated FreeRTOS task, so a browser pulling the
//     bundle never stalls LVGL. The DSI panel is self-clocked and the radio
//     is a separate chip — concurrent serving costs nothing visible.
//   - S3 (RGB) boards: pumped from the MAIN LOOP. The RGB raster streams
//     continuously from PSRAM, which shares a bus with flash; serving the
//     flash-resident bundle WHILE LVGL renders starved the panel's bounce
//     buffer and the image visibly jumped around (seen on the 4.3C). The
//     loop-pump "freeze during a request" is protective there: it idles
//     LVGL's bus traffic for the duration.
// Thread notes (task mode): the shot store is mutex-guarded (it already
// serves the SD writer task); clock reads are plain time(); the theme
// callback reads nine palette words (a torn read during a theme switch
// mis-colors one response, harmlessly); the portal handlers write NVS
// (thread-safe) and MicraLink's setters, which already tolerate cross-task
// callers (the BLE task).
struct WebTheme {
  uint32_t bg, rail, card, text, muted, accent, ok, warn, alert;
};

class WebUi {
 public:
  // `theme` is read per request so the page always reflects the palette the
  // screen is showing right now. `name` must outlive this object.
  // Registers routes; on DSI boards also spawns the server task. In both
  // models the listening socket is opened lazily once WiFi is in ANY active
  // mode — binding before the network stack exists asserts inside lwip
  // ("Invalid mbox", seen on HW).
  void begin(TokenSetup& setup, core::IShotStore& shots, core::IClock& clock,
             const char* name, std::function<WebTheme()> theme);

  // Main-loop pump. No-op on DSI boards (the task owns the server there);
  // on S3 boards this is where all serving happens.
  void poll();

 private:
  static void task_entry(void* self);
  void run();        // DSI: server task loop
  bool ensure_bound();  // lazy socket bind once WiFi is up

  void handle_root();
  void handle_summary();
  void handle_shots();
  void handle_shot_csv();

  WebServer server_{80};
  TokenSetup* setup_ = nullptr;
  core::IShotStore* shots_ = nullptr;
  core::IClock* clock_ = nullptr;
  const char* name_ = "Apollo 2";
  bool bound_ = false;  // listening socket open (needs the network stack)
  std::function<WebTheme()> theme_;
};

}  // namespace platform
