#include "platform_esp32/network.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <cstdlib>  // setenv
#include <ctime>

#include "core/system.h"
#include "platform_esp32/clock.h"
#include "platform_esp32/config.h"
#include "platform_esp32/token_setup.h"

namespace platform {

namespace {
constexpr uint32_t kConnectTimeoutMs = 15000;   // give up on an association attempt
constexpr uint32_t kRetryMs = 10000;            // re-attempt after a failure/outage
constexpr uint32_t kRtcResyncMs = 60UL * 60 * 1000;  // refresh the RTC from NTP hourly
constexpr uint32_t kNtpSyncIntervalMs = 60UL * 60 * 1000;  // steady SNTP cadence
constexpr uint32_t kNtpInitialSyncMs = 30UL * 1000;  // retry fast until the 1st
                                                     // real sync, then relax
constexpr int kBaseYear = 2024;  // system time is "real" once past this (matches Clock)

// Set the moment a REAL SNTP sync lands (via the notification callback below);
// 0 = none this boot. File-scope so the C callback can reach it. Read through
// Network::ntp_seconds_since_sync() for the Info page's honest NTP readout.
volatile uint32_t s_last_ntp_sync_ms = 0;
void on_ntp_synced(struct timeval*) { s_last_ntp_sync_ms = millis(); }

// The low TX power TokenSetup uses to avoid brown-outs applies to the station too
// — the router may be further than a phone, but on a USB-powered board a full-power
// WiFi burst can brown out the rail, so keep it modest.
constexpr wifi_power_t kTxPower = WIFI_POWER_11dBm;
}  // namespace

Network::Network(Config& config, Clock& clock, TokenSetup& token_setup)
    : config_(config), clock_(clock), token_setup_(token_setup) {}

void Network::begin() {
  if (config_.wifi_enabled() && !config_.wifi_ssid().empty()) {
    start_station();
  } else {
    status_ = core::NetState::Disabled;
  }
}

void Network::start_station() {
  const std::string ssid = config_.wifi_ssid();
  const std::string pass = config_.wifi_password();
  if (ssid.empty()) {
    status_ = core::NetState::Disabled;
    return;
  }
  ip_.clear();
  // WiFi-NO_MEM diagnosis (esp_wifi_init fails with ESP_ERR_NO_MEM on the
  // 4.3C): log what internal RAM the stack actually has to work with at init
  // time — total free + largest contiguous block (fragmentation shows up as a
  // big gap between them).
  core::logf("Network: internal heap free=%u largest=%u\n",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(kTxPower);
  WiFi.begin(ssid.c_str(), pass.c_str());
  status_ = core::NetState::Connecting;
  connect_deadline_ms_ = millis() + kConnectTimeoutMs;
  core::logf("Network: connecting to '%s'\n", ssid.c_str());
}

void Network::stop_station() {
  WiFi.disconnect(/*wifioff=*/true);
  ip_.clear();
  status_ = core::NetState::Disabled;
}

void Network::on_connected() {
  ip_ = WiFi.localIP().toString().c_str();
  status_ = core::NetState::Connected;
  from_portal_ = false;
  core::logf("Network: connected, IP=%s\n", ip_.c_str());
  if (config_.ntp_enabled()) start_ntp();
}

void Network::start_ntp() {
  time_persisted_ = false;  // force an RTC write on the first sync
  ntp_relaxed_ = false;     // start on the fast retry until a real sync lands
  const std::string tz = config_.timezone();
  const std::string ntp = config_.ntp_server();
  // Honest "did SNTP actually sync?" signal — fires only on a real server reply,
  // unlike ntp_synced() which the RTC-provided boot time already satisfies.
  sntp_set_time_sync_notification_cb(on_ntp_synced);
  // configTzTime sets the system clock from SNTP asynchronously and applies TZ for
  // local display; poll() then persists the synced time to the RTC (see below).
  configTzTime(tz.c_str(), ntp.c_str(), "pool.ntp.org");
  // Poll FAST until the first real sync (a missed boot sync mustn't cost an hour),
  // then poll() relaxes to the steady hourly cadence.
  sntp_set_sync_interval(kNtpInitialSyncMs);
  sntp_restart();
  core::logf("Network: NTP started (server=%s, retry %us until synced)\n",
             ntp.c_str(), static_cast<unsigned>(kNtpInitialSyncMs / 1000));
}

void Network::poll() {
  // While the setup portal (AP) is up, the station is down; the portal owns the
  // radio. Once the user saves new credentials, reconnect from poll() context
  // (not mid-request) so tearing the AP down is safe.
  if (token_setup_.active()) {
    if (token_setup_.take_wifi_saved()) {
      token_setup_.stop();
      from_portal_ = true;
      start_station();
    }
    return;
  }

  if (!config_.wifi_enabled()) return;

  switch (status_) {
    case core::NetState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        on_connected();
      } else if (millis() > connect_deadline_ms_) {
        status_ = core::NetState::Failed;
        retry_at_ms_ = millis() + kRetryMs;
        core::logf("Network: connect timed out\n");
        // Credentials just entered via the portal are probably wrong — reopen the
        // AP immediately so the user can correct them without touching the device.
        if (from_portal_) {
          from_portal_ = false;
          begin_setup_portal();
        }
      }
      break;

    case core::NetState::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        start_station();  // dropped (or radio was taken by the token portal); reconnect
        break;
      }
      // Persist NTP-synced time to the RTC on first valid sync, then hourly. Only
      // when NTP is on — otherwise the system clock isn't NTP-derived and we'd just
      // be echoing a manual time back to the RTC it already came from.
      if (config_.ntp_enabled()) {
        if (const std::time_t now = time(nullptr);
            now > 0 && (localtime(&now)->tm_year + 1900) >= kBaseYear &&
            (!time_persisted_ || millis() - last_rtc_sync_ms_ > kRtcResyncMs)) {
          clock_.set_unix(now);  // writes the full date to the PCF85063 RTC where present
          time_persisted_ = true;
          last_rtc_sync_ms_ = millis();
        }
      }
      // Once a real SNTP sync has landed, relax from the fast initial retry to
      // the steady hourly cadence.
      if (!ntp_relaxed_ && s_last_ntp_sync_ms != 0) {
        sntp_set_sync_interval(kNtpSyncIntervalMs);
        ntp_relaxed_ = true;
        core::logf("Network: NTP synced; relaxing resync to %us\n",
                   static_cast<unsigned>(kNtpSyncIntervalMs / 1000));
      }
      break;

    case core::NetState::Failed:
    case core::NetState::Disabled:
      if (status_ == core::NetState::Failed && millis() > retry_at_ms_) {
        start_station();  // keep trying so a transient outage recovers on its own
      }
      break;
  }
}

bool Network::ntp_synced() const { return config_.ntp_enabled() && time_persisted_; }

int Network::ntp_seconds_since_sync() const {
  if (s_last_ntp_sync_ms == 0) return -1;  // no real sync since boot
  return static_cast<int>((millis() - s_last_ntp_sync_ms) / 1000);
}

const char* Network::ssid() const {
  str_cache_ = config_.wifi_ssid();
  return str_cache_.c_str();
}

bool Network::enabled() const { return config_.wifi_enabled(); }

void Network::set_enabled(bool on) {
  config_.set_wifi_enabled(on);
  if (on) {
    start_station();
  } else {
    stop_station();
  }
}

void Network::begin_setup_portal() {
  stop_station();          // free the radio for the AP
  token_setup_.start(TokenSetup::Mode::Wifi);  // credentials page (token has its own)
}

void Network::stop_setup_portal() {
  token_setup_.stop();
  if (config_.wifi_enabled()) start_station();  // resume the station if configured
}

const char* Network::setup_ssid() const { return token_setup_.ssid(); }
const char* Network::setup_url() const { return token_setup_.url(); }

void Network::forget() {
  config_.clear_wifi();
  stop_station();
}

const char* Network::timezone() const {
  str_cache_ = config_.timezone();
  return str_cache_.c_str();
}

void Network::set_timezone(const char* tz) {
  config_.set_timezone(tz);
  setenv("TZ", tz, 1);  // applies to local display immediately, NTP or not
  tzset();
  if (status_ == core::NetState::Connected && config_.ntp_enabled()) start_ntp();
}

const char* Network::ntp_server() const {
  str_cache_ = config_.ntp_server();
  return str_cache_.c_str();
}

void Network::set_ntp_server(const char* host) {
  config_.set_ntp_server(host);
  if (status_ == core::NetState::Connected && config_.ntp_enabled()) start_ntp();
}

bool Network::ntp_enabled() const { return config_.ntp_enabled(); }

void Network::set_ntp_enabled(bool on) {
  config_.set_ntp_enabled(on);
  if (status_ != core::NetState::Connected) return;
  if (on) {
    start_ntp();     // re-enabling forces an immediate resync (handy for testing)
  } else {
    esp_sntp_stop();  // stop polling; the clock holds its current value
  }
}

}  // namespace platform
