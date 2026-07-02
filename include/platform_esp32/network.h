#pragma once

#include <cstdint>
#include <string>

#include "core/network.h"

namespace platform {

class Config;
class Clock;
class TokenSetup;

// WiFi station + NTP for the device. Optionally joins the saved home network in
// station mode, then keeps the Clock correct via SNTP (persisting to the RTC so
// real time survives a power-off). Credentials are (re)entered through the shared
// setup portal (TokenSetup's SoftAP): begin_setup_portal() drops the station and
// raises the AP; on save the station reconnects. The device screen — driven by
// status() — is the source of truth, so no concurrent AP/STA is needed.
//
// begin() once after Config/Clock are up; poll() every loop() to drive the
// connect state machine, NTP->RTC persistence, and the post-setup reconnect.
class Network : public core::INetwork {
 public:
  Network(Config& config, Clock& clock, TokenSetup& token_setup);

  void begin();
  void poll();

  core::NetState status() const override { return status_; }
  const char* ssid() const override;
  const char* ip() const override { return ip_.c_str(); }

  bool enabled() const override;
  void set_enabled(bool on) override;

  void begin_setup_portal() override;
  void stop_setup_portal() override;
  const char* setup_ssid() const override;
  const char* setup_url() const override;
  void forget() override;

  const char* timezone() const override;
  void set_timezone(const char* tz) override;
  const char* ntp_server() const override;
  void set_ntp_server(const char* host) override;
  bool ntp_enabled() const override;
  void set_ntp_enabled(bool on) override;

 private:
  void start_station();  // (re)connect STA from the saved credentials
  void stop_station();
  void on_connected();   // capture IP + kick off NTP
  void start_ntp();      // (re)start SNTP against the configured server

  Config& config_;
  Clock& clock_;
  TokenSetup& token_setup_;

  core::NetState status_ = core::NetState::Disabled;
  std::string ip_;
  mutable std::string str_cache_;  // backing for ssid()/timezone()/ntp_server()
  uint32_t connect_deadline_ms_ = 0;  // STA association timeout
  uint32_t retry_at_ms_ = 0;          // next reconnect attempt after a failure
  uint32_t last_rtc_sync_ms_ = 0;     // last time NTP was persisted to the RTC
  bool time_persisted_ = false;       // have we written a valid NTP time to the RTC?
  bool from_portal_ = false;          // connecting with creds just entered via the portal
};

}  // namespace platform
