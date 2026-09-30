#include "platform_esp32/token_setup.h"

#include "core/timezones.h"

#include <Arduino.h>
#include <WiFi.h>

#include <string>

#include "core/system.h"
#include "platform_esp32/config.h"
#include "platform_esp32/micra_link.h"

namespace platform {

namespace {
// The portal serves ONE of these two pages per session (see Mode in the
// header): the token page from Micra pairing, the WiFi page from WiFi settings.
const char kPageHead[] =
    "<!DOCTYPE html><html><head><meta name=viewport "
    "content='width=device-width,initial-scale=1'></head>"
    "<body style='font-family:sans-serif;max-width:480px;margin:2em auto;padding:0 1em'>"
    "<h2>Micra Remote</h2>";

const char kPageTail[] = "</body></html>";

const char kTokenPage[] =
    "<p>Paste your machine's BLE token, then Save:</p>"
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
    // Point at the separate WiFi flow instead of offering it here: this page's
    // AP closes the moment the Micra link connects, which would leave an
    // embedded WiFi form dead on the phone.
    "<hr style='margin:2em 0'>"
    "<p style='font-size:.82em;color:#888'>To join your home WiFi for automatic "
    "time, use <b>Settings &rarr; WiFi &rarr; Setup</b> on the device after "
    "pairing &mdash; it opens its own setup page.</p>";

// WiFi credentials page: join a home network for automatic time (NTP). On save
// the device leaves this AP and connects to that network, so this page closes —
// the device screen then shows the connection status + its local IP.
const char kWifiPage[] =
    "<h3>WiFi</h3>"
    "<p style='font-size:.9em'>Join your home WiFi for automatic time. The device "
    "will leave this setup network and connect &mdash; watch its screen for the "
    "result and its local IP.</p>"
    "<form onsubmit='return submitWifi()'>"
    "<input id=ssid name='ssid' placeholder='Network name' autocomplete='off' "
    "autocapitalize='off' spellcheck='false' "
    "style='width:100%;padding:10px;box-sizing:border-box;margin-bottom:8px'>"
    "<input id=pass name='pass' type='password' placeholder='Password' "
    "autocomplete='off' style='width:100%;padding:10px;box-sizing:border-box;"
    "margin-bottom:8px'>"
    // Time zone: optional, and the natural place for it -- a long list is
    // comfortable on a phone and it belongs with "automatic time". handle_root
    // splices the <option>s in from core::kTimezones (the Settings picker's
    // table), so the two stay one list.
    "<select id=tz name='tz' style='width:100%;padding:10px;box-sizing:border-box'>"
    "<option value=''>Time zone (optional &mdash; set later under Settings)</option>";
// Network address: DHCP (the default) or a static address. Optional and
// rarely needed, so it sits below the essentials and its fields only appear
// once Static is picked. There is no on-device editor for these -- changing
// them means running Set up WiFi again, which pre-fills what is stored
// (handle_root splices the values in).
const char kWifiPageAfterTz[] =
    "</select>"
    "<select id=ipmode onchange='ipm()' style='width:100%;padding:10px;"
    "box-sizing:border-box;margin-top:8px'>"
    "<option value='dhcp'>Network address: automatic (DHCP)</option>"
    "<option value='static'>Network address: static</option></select>"
    "<div id=stat style='display:none;margin-top:8px'>"
    "<input id=ip placeholder='IP address, e.g. 192.168.1.50' inputmode='decimal' "
    "autocomplete='off' style='width:100%;padding:10px;box-sizing:border-box;margin-bottom:8px'>"
    "<input id=mask placeholder='Subnet mask, e.g. 255.255.255.0' inputmode='decimal' "
    "autocomplete='off' style='width:100%;padding:10px;box-sizing:border-box;margin-bottom:8px'>"
    "<input id=gw placeholder='Gateway (router), e.g. 192.168.1.1' inputmode='decimal' "
    "autocomplete='off' style='width:100%;padding:10px;box-sizing:border-box;margin-bottom:8px'>"
    "<input id=dns placeholder='DNS server (optional; the gateway if blank)' "
    "inputmode='decimal' autocomplete='off' "
    "style='width:100%;padding:10px;box-sizing:border-box'>"
    "</div>"
    "<p id=wmsg style='font-size:.9em'></p>"
    "<p><button style='padding:10px 24px;font-size:1em'>Save WiFi</button></p>"
    "</form>"
    "<script>function ipm(){document.getElementById('stat').style.display="
    "document.getElementById('ipmode').value=='static'?'block':'none';}"
    "function q(v){return /^(25[0-5]|2[0-4]\\d|1?\\d?\\d)(\\.(25[0-5]|2[0-4]\\d|1?\\d?\\d)){3}$/"
    ".test(v);}"
    "function submitWifi(){"
    "var s=document.getElementById('ssid').value.trim();"
    "var p=document.getElementById('pass').value;"
    "var z=document.getElementById('tz').value;"
    "var m=document.getElementById('wmsg');"
    "if(!s){m.style.color='#c00';m.textContent='Enter a network name.';return false;}"
    "var st=document.getElementById('ipmode').value=='static';"
    "var ip=document.getElementById('ip').value.trim(),"
    "mk=document.getElementById('mask').value.trim(),"
    "gw=document.getElementById('gw').value.trim(),"
    "dn=document.getElementById('dns').value.trim();"
    "if(st&&!(q(ip)&&q(mk)&&q(gw)&&(!dn||q(dn)))){m.style.color='#c00';"
    "m.textContent='Static address: fill IP, subnet mask and gateway as four numbers, "
    "e.g. 192.168.1.50.';return false;}"
    "m.style.color='#888';m.textContent='Saving\\u2026';"
    "fetch('/wifi',{method:'POST',headers:{'Content-Type':"
    "'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(s)+"
    "'&pass='+encodeURIComponent(p)+(z?'&tz='+encodeURIComponent(z):'')+"
    "'&ipmode='+(st?'static':'dhcp')+(st?'&ip='+encodeURIComponent(ip)+'&mask='+"
    "encodeURIComponent(mk)+'&gw='+encodeURIComponent(gw)+'&dns='+encodeURIComponent(dn):'')})"
    ".then(function(r){return r.text();})"
    ".then(function(t){m.style.color='#0a0';m.textContent=t;})"
    ".catch(function(){m.style.color='#c00';"
    "m.textContent='Saved \\u2014 the device is leaving this network. Watch its "
    "screen.';});"
    "return false;}</script>";

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

void TokenSetup::start(Mode mode) {
  mode_ = mode;      // an already-open portal switches pages on the next load
  if (active_) return;
  token_submitted_ = false;
  // Cleanly drop any live station BEFORE switching to AP. Going STA->AP directly
  // (e.g. opening this portal while joined to home WiFi + serving the web app)
  // leaves the softAP's DHCP unable to hand out leases, so clients see the SSID
  // but can't join. This mirrors what the WiFi-credentials portal already does via
  // Network::stop_station(); Network keeps status_==Connected, so it reconnects on
  // its own once the portal closes.
  WiFi.disconnect(/*wifioff=*/true);
  WiFi.mode(WIFI_AP);
  // Pin the AP IP + subnet (192.168.4.1/24) before bringing it up. Without this
  // the built-in DHCP server sometimes doesn't hand out a lease (seen on the 7B,
  // where the RGB panel leaves less internal RAM), so clients never get an IP.
  const IPAddress ip(192, 168, 4, 1);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  const bool ap_ok = WiFi.softAP(ssid());
  // Captive portal: answer EVERY DNS query with our IP. The phone's
  // connectivity probe (captive.apple.com / generate_204) then hits the web
  // server, whose not-found handler 302s to '/', and the OS pops the setup
  // page on its own — no typing 192.168.4.1. Async (AsyncUDP); no pump.
  const bool dns_ok = dns_.start(53, "*", ip);
  // The phone is inches away, so run low TX power — smaller current spikes, which
  // matters on a USB-powered 7" board where a WiFi burst can brown out the rail.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  // Routes live on the shared server (WebUi) — nothing to register here.
  active_ = true;
  stop_pending_ = true;                     // safety net: auto-close if unused, so the
  stop_at_ms_ = millis() + 5 * 60 * 1000;   // AP can't linger (the device closes it on connect)
  core::logf("TokenSetup: AP '%s' %s, IP=%s, DNS %s, free heap=%u\n", ssid(),
             ap_ok ? "up" : "FAILED", WiFi.softAPIP().toString().c_str(),
             dns_ok ? "up" : "FAILED",
             static_cast<unsigned>(ESP.getFreeHeap()));
}

void TokenSetup::stop() {
  if (!active_) return;
  dns_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  active_ = false;
  stop_pending_ = false;
  token_submitted_ = false;
  core::logf("TokenSetup: AP down\n");
}

void TokenSetup::handle() {
  if (!active_) return;  // client pumping lives in WebUi::poll()
  if (stop_pending_ && millis() > stop_at_ms_) stop();
}

void TokenSetup::handle_root() {
  String page(kPageHead);
  if (mode_ == Mode::Token) {
    page += kTokenPage;
  } else {
    page += kWifiPage;
    // One <option> per zone; the currently stored zone is preselected so a
    // revisit shows what is set (the default "UTC0" reads as unset).
    const std::string cur = config_.timezone();
    for (int i = 0; i < core::kTimezoneCount; ++i) {
      const core::Timezone& z = core::kTimezones[i];
      page += "<option value='";
      page += z.posix;
      page += (cur == z.posix && cur != "UTC0") ? "' selected>" : "'>";
      page += z.label;
      page += "</option>";
    }
    page += kWifiPageAfterTz;
    // A revisit (the network is already saved) shows the saved SSID and makes
    // the password optional — blank keeps the saved one for that SSID — so a
    // one-field change (a static address, the time zone) doesn't mean
    // retyping credentials. The password itself is never sent to the page.
    const std::string saved = config_.wifi_ssid();
    if (!saved.empty()) {
      page += "<script>document.getElementById('ssid').value='";
      for (char c : saved) {  // SSIDs are free text: escape for a JS single-quoted literal
        if (c == '\\' || c == '\'') page += '\\';
        if (c == '<') { page += "\\x3c"; continue; }
        page += c;
      }
      page += "';document.getElementById('pass').placeholder="
              "'Password (leave blank to keep the saved one)';</script>";
    }
    // Pre-fill the address block from what is stored, so a revisit to change
    // one field doesn't mean retyping the rest.
    if (config_.static_ip()) {
      page += "<script>document.getElementById('ipmode').value='static';ipm();";
      const struct { const char* id; std::string val; } fields[] = {
          {"ip", config_.static_ip_addr()}, {"mask", config_.static_ip_mask()},
          {"gw", config_.static_ip_gateway()}, {"dns", config_.static_ip_dns()}};
      for (const auto& f : fields) {
        if (f.val.empty()) continue;
        page += "document.getElementById('";
        page += f.id;
        page += "').value='";
        page += f.val.c_str();  // a stored dotted quad: digits and dots only
        page += "';";
      }
      page += "</script>";
    }
  }
  page += kPageTail;
  server_->send(200, "text/html", page);
}

void TokenSetup::handle_save() {
  String token = server_->arg("token");
  token.trim();
  if (!looks_like_token(token)) {
    server_->send(200, "text/plain",
                  "That token is not valid (it should be 64 hex characters).");
    return;
  }
  const std::string t(token.c_str());
  config_.set_token(t);   // persist
  token_submitted_ = true;  // lets main.cpp un-park the Micra link (see .h)
  link_.set_token(t);     // connect now (clears the bad-token latch)
  core::logf("TokenSetup: token saved\n");
  // Plain-text result for the async form; the AP stays up so a rejected token
  // can be corrected and resubmitted. The device closes it once the link
  // connects (or the safety timeout fires).
  server_->send(200, "text/plain",
                "Saved. The device is connecting -- this page stays open in case "
               "the token is wrong; you can paste a new one and Save again. Once "
               "connected, the device closes this setup network.");
}

void TokenSetup::handle_wifi() {
  String ssid = server_->arg("ssid");
  String pass = server_->arg("pass");
  ssid.trim();
  if (ssid.length() == 0) {
    server_->send(200, "text/plain", "Enter a network name.");
    return;
  }
  // Network address: DHCP unless the form said static with four parseable
  // quads (the page validates too; this guards a hand-made POST). A bad static
  // set is refused outright rather than half-saved.
  const bool want_static = server_->arg("ipmode") == "static";
  String ip = server_->arg("ip"), mask = server_->arg("mask"), gw = server_->arg("gw"),
         dns = server_->arg("dns");
  ip.trim(); mask.trim(); gw.trim(); dns.trim();
  if (want_static) {
    IPAddress a, m, g, d;
    if (!a.fromString(ip) || !m.fromString(mask) || !g.fromString(gw) ||
        (dns.length() > 0 && !d.fromString(dns))) {
      server_->send(200, "text/plain",
                    "Static address not saved: IP, subnet mask and gateway must each be "
                    "four numbers like 192.168.1.50. Nothing was changed.");
      return;
    }
  }
  // Blank password + the SSID unchanged = keep the saved password (the page
  // says so once a network is saved). A different SSID with a blank password
  // is an open network, as it always was.
  std::string new_pass(pass.c_str());
  const std::string saved_ssid = config_.wifi_ssid();
  const bool keep_pass = pass.length() == 0 && !saved_ssid.empty() && saved_ssid == ssid.c_str();
  if (keep_pass) new_pass = config_.wifi_password();
  config_.save_wifi(std::string(ssid.c_str()), new_pass);
  config_.set_wifi_enabled(true);
  if (keep_pass) core::logf("TokenSetup: password kept for the saved network\n");
  config_.save_static_ip(want_static, want_static ? ip.c_str() : "", want_static ? mask.c_str() : "",
                         want_static ? gw.c_str() : "", want_static ? dns.c_str() : "");
  core::logf("TokenSetup: network address %s\n", want_static ? "static" : "DHCP");
  // Optional time zone from the same form. Only values from our own table are
  // accepted (the page offers nothing else; this guards a hand-made POST).
  const String tz = server_->arg("tz");
  if (tz.length() > 0) {
    for (int i = 0; i < core::kTimezoneCount; ++i) {
      if (tz == core::kTimezones[i].posix) {
        config_.set_timezone(std::string(tz.c_str()));  // Network applies TZ on reconnect
        core::logf("TokenSetup: time zone set to %s\n", core::kTimezones[i].label);
        break;
      }
    }
  }
  wifi_saved_ = true;  // Network tears down the AP + connects from loop() context
  core::logf("TokenSetup: WiFi credentials saved for '%s'\n", ssid.c_str());
  // Reply before the AP is torn down (next loop) so the phone gets this message.
  server_->send(200, "text/plain",
                "Saved. The device is leaving this setup network to connect to your "
               "WiFi -- watch the device screen for the status and its local IP.");
}

}  // namespace platform
