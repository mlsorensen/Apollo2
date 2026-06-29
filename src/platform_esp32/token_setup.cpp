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
    "<p style='font-size:.82em;color:#888'>No token? Run the <b>lmtoken</b> tool "
    "on a computer to fetch it from your La Marzocco account.</p>"
    "<form id=f onsubmit='return submitTok()'>"
    "<input id=tok name='token' autocomplete='off' autocapitalize='off' "
    "spellcheck='false' style='width:100%;padding:10px;box-sizing:border-box'>"
    "<p id=msg style='font-size:.9em'></p>"
    "<p><button style='padding:10px 24px;font-size:1em'>Save</button></p>"
    "</form>"
    // Submit over fetch() and stay on this page, so a refresh just reloads the
    // form (GET) instead of re-POSTing a stale token. Token = 64 hex chars.
    "<script>function submitTok(){"
    "var t=document.getElementById('tok');var v=t.value.trim();t.value=v;"
    "var m=document.getElementById('msg');"
    "if(!/^[0-9a-fA-F]{64}$/.test(v)){m.style.color='#c00';"
    "m.textContent='That does not look like a token \\u2014 it should be 64 "
    "hexadecimal characters (0-9, a-f).';return false;}"
    "m.style.color='#888';m.textContent='Saving\\u2026';"
    "fetch('/save',{method:'POST',headers:{'Content-Type':"
    "'application/x-www-form-urlencoded'},body:'token='+encodeURIComponent(v)})"
    ".then(function(r){return r.text();})"
    ".then(function(s){m.style.color='#0a0';m.textContent=s;})"
    ".catch(function(){m.style.color='#c00';"
    "m.textContent='Could not reach the device \\u2014 try again.';});"
    "return false;}</script>"
    "</body></html>";

// The token is a 32-byte value rendered as exactly 64 hex characters.
bool looks_like_token(const String& t) {
  if (t.length() != 64) return false;
  for (size_t i = 0; i < t.length(); ++i) {
    const char c = t[i];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F');
    if (!hex) return false;
  }
  return true;
}
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
  stop_pending_ = true;                     // safety net: auto-close if unused, so the
  stop_at_ms_ = millis() + 5 * 60 * 1000;   // AP can't linger (the device closes it on connect)
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
  if (!looks_like_token(token)) {
    server_.send(200, "text/plain",
                 "That token is not valid (it should be 64 hex characters).");
    return;
  }
  const std::string t(token.c_str());
  config_.set_token(t);   // persist
  link_.set_token(t);     // connect now (clears the bad-token latch)
  Serial.println("TokenSetup: token saved");
  // Plain-text result for the async form; the AP stays up so a rejected token
  // can be corrected and resubmitted. The device closes it once the link
  // connects (or the safety timeout fires).
  server_.send(200, "text/plain",
               "Saved. The device is connecting -- this page stays open in case "
               "the token is wrong; you can paste a new one and Save again.");
}

}  // namespace platform
