#include "platform_esp32/token_setup.h"

#include <Arduino.h>
#include <WiFi.h>

#include <string>

#include "platform_esp32/config.h"
#include "platform_esp32/micra_link.h"

namespace platform {

namespace {
const char kForm[] =
    "<!DOCTYPE html><html><head><meta name=viewport "
    "content='width=device-width,initial-scale=1'></head>"
    "<body style='font-family:sans-serif;max-width:480px;margin:2em auto;padding:0 1em'>"
    "<h2>Micra Remote</h2><p>Paste your machine's BLE token, then Save:</p>"
    "<form method='POST' action='/save'>"
    "<input name='token' autocomplete='off' autocapitalize='off' "
    "spellcheck='false' style='width:100%;padding:10px;box-sizing:border-box'>"
    "<p><button style='padding:10px 24px;font-size:1em'>Save</button></p>"
    "</form></body></html>";
}  // namespace

TokenSetup::TokenSetup(Config& config, MicraLink& link)
    : config_(config), link_(link) {}

void TokenSetup::start() {
  if (active_) return;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid());  // open network at 192.168.4.1
  server_.on("/", [this]() { handle_root(); });
  server_.on("/save", HTTP_POST, [this]() { handle_save(); });
  server_.begin();
  active_ = true;
  Serial.printf("TokenSetup: AP '%s' up at %s\n", ssid(), url());
}

void TokenSetup::stop() {
  if (!active_) return;
  server_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  active_ = false;
  stop_pending_ = false;
  Serial.println("TokenSetup: AP down");
}

void TokenSetup::handle() {
  if (!active_) return;
  server_.handleClient();
  if (stop_pending_ && millis() > stop_at_ms_) stop();
}

void TokenSetup::handle_root() { server_.send(200, "text/html", kForm); }

void TokenSetup::handle_save() {
  String token = server_.arg("token");
  token.trim();
  if (token.length() > 0) {
    const std::string t(token.c_str());
    config_.set_token(t);   // persist
    link_.set_token(t);     // connect now
    Serial.println("TokenSetup: token saved");
  }
  server_.send(200, "text/html",
               "<body style='font-family:sans-serif;text-align:center;margin-top:3em'>"
               "<h2>Saved.</h2><p>You can disconnect from this WiFi.</p></body>");
  // Keep the AP up briefly so the response flushes, then tear it down.
  stop_pending_ = true;
  stop_at_ms_ = millis() + 2000;
}

}  // namespace platform
