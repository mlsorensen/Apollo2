#pragma once

#include <WebServer.h>

namespace platform {

class Config;
class MicraLink;

// A one-shot WiFi setup portal: brings up an open SoftAP serving a paste form.
// The page served depends on which flow opened it (Mode) — the Micra pairing
// flow gets the BLE-token form, the WiFi settings flow gets the credentials
// form. They are intentionally NOT one combined page: each flow tears the AP
// down at a different moment (token -> when the Micra link connects, WiFi ->
// when the credentials are saved), and a combined page left the other half
// dead on the phone after the AP closed under it.
//
// When active, handle() must be pumped from the main loop. BLE is idle while
// this runs (the link sits in NeedsToken until a token arrives), so WiFi/BLE
// don't contend.

class TokenSetup {
 public:
  enum class Mode { Token, Wifi };

  TokenSetup(Config& config, MicraLink& link);

  void start(Mode mode);
  void stop();
  void handle();  // pump from loop(); no-op when inactive
  bool active() const { return active_; }

  // True once (consumed) after the user submits WiFi credentials via the portal,
  // so Network can tear the AP down and reconnect the station from loop() context.
  bool take_wifi_saved() {
    const bool v = wifi_saved_;
    wifi_saved_ = false;
    return v;
  }

  const char* ssid() const { return "Micra-Setup"; }
  const char* url() const { return "http://192.168.4.1"; }

 private:
  void handle_root();
  void handle_save();
  void handle_wifi();

  Config& config_;
  MicraLink& link_;
  WebServer server_{80};
  Mode mode_ = Mode::Token;
  bool active_ = false;
  bool stop_pending_ = false;
  bool wifi_saved_ = false;
  uint32_t stop_at_ms_ = 0;
};

}  // namespace platform
