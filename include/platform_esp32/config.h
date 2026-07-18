#pragma once

#include <string>

// Persistent device configuration in NVS flash: the saved machine (BLE MAC +
// display name) chosen via the Settings scan, and the auth token entered via
// the WiFi setup page. Nothing is compiled in.

namespace platform {

class Config {
 public:
  void begin();               // create the NVS namespace on first boot (quiets logs)

  std::string mac() const;    // saved MAC, or "" if none
  std::string name() const;   // saved display name, or "" if none
  std::string token() const;  // saved BLE auth token, or "" if none

  void save(const std::string& mac, const std::string& name);
  void set_token(const std::string& token);
  void clear();               // forget the saved machine (incl. its token)

  // Saved Bluetooth scale (separate from the machine; no token needed).
  std::string scale_mac() const;
  std::string scale_name() const;
  void save_scale(const std::string& mac, const std::string& name);
  void clear_scale();

  // Brew-by-weight target (grams) — persisted so it survives reboots.
  float target_weight_g() const;       // default 36
  void set_target_weight_g(float grams);
  bool shot_mode() const;              // brew-by-weight automation armed (default true)
  void set_shot_mode(bool on);
  float overshoot_g() const;           // learned drip/lag compensation (default 2.0)
  void set_overshoot_g(float grams);
  int review_hold_s() const;           // shot-review linger before auto-reset (default 30)
  void set_review_hold_s(int seconds);
  bool auto_connect() const;           // connect to the saved Micra at boot (default false)
  void set_auto_connect(bool on);
  int flow_smooth() const;             // shot-graph smoothing level 0..3 (default 1)
  void set_flow_smooth(int level);

  int brightness() const;     // screen brightness 0..100 (default 100)
  void set_brightness(int percent);

  bool clock_24h() const;     // 24-hour clock display (default true)
  void set_clock_24h(bool on);

  int theme() const;          // color-scheme index (default 0)
  void set_theme(int index);

  bool use_fahrenheit() const;  // temperature units (default false = Celsius)
  void set_use_fahrenheit(bool on);

  bool drop_negative_flow() const;  // clamp negative g/s on the flow graph (default true)
  void set_drop_negative_flow(bool on);

  bool scope_graph() const;  // oscilloscope-style flow graph (default true; false = scroll)
  void set_scope_graph(bool on);

  bool perf_overlay() const;  // LVGL FPS/CPU overlay (default false)
  void set_perf_overlay(bool on);
  bool click_sound() const;  // button-press click (default true; audio boards)
  void set_click_sound(bool on);

  // WiFi station (home network join + NTP time sync). Credentials are entered via
  // the setup portal; timezone/NTP server via Settings.
  bool wifi_enabled() const;  // master WiFi on/off (default false)
  void set_wifi_enabled(bool on);
  std::string wifi_ssid() const;      // saved home SSID, or "" if none
  std::string wifi_password() const;  // saved PSK (plaintext in NVS), or ""
  void save_wifi(const std::string& ssid, const std::string& password);
  void clear_wifi();          // forget the saved network (incl. password)
  std::string timezone() const;    // POSIX TZ string (default "UTC0")
  void set_timezone(const std::string& tz);
  std::string ntp_server() const;  // NTP host (default "pool.ntp.org")
  void set_ntp_server(const std::string& host);
  bool ntp_enabled() const;        // sync time from NTP when connected (default true)
  void set_ntp_enabled(bool on);
};

}  // namespace platform
