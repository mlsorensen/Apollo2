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
//   /api/shot.png?id=N   one shot's rendered card (download, streamed off SD)
//
// The server runs from boot and is pumped from the main loop; with no network
// up, handleClient is a cheap no-op. Handlers therefore run on the LVGL
// thread — fine for the small JSON endpoints, a brief pause for the bundle
// and PNG streams.
struct WebTheme {
  uint32_t bg, rail, card, text, muted, accent, ok, warn, alert;
};

class WebUi {
 public:
  // `theme` is read per request so the page always reflects the palette the
  // screen is showing right now. `name` must outlive this object.
  void begin(TokenSetup& setup, core::IShotStore& shots, core::IClock& clock,
             const char* name, std::function<WebTheme()> theme);
  void poll();  // pump from loop()

  // NOTE: begin() only registers routes. The listening socket is opened
  // lazily on the first poll() with WiFi in ANY active mode — binding at
  // boot, before the network stack exists, asserts inside lwip
  // ("Invalid mbox", seen on HW).

 private:
  void handle_root();
  void handle_summary();
  void handle_shots();
  void handle_shot_csv();
  void handle_shot_png();

  WebServer server_{80};
  TokenSetup* setup_ = nullptr;
  core::IShotStore* shots_ = nullptr;
  core::IClock* clock_ = nullptr;
  const char* name_ = "Apollo 2";
  bool started_ = false;  // socket bound (needs the network stack up)
  std::function<WebTheme()> theme_;
};

}  // namespace platform
