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

  int brightness() const;     // screen brightness 0..100 (default 100)
  void set_brightness(int percent);

  bool clock_24h() const;     // 24-hour clock display (default true)
  void set_clock_24h(bool on);

  int theme() const;          // color-scheme index (default 0)
  void set_theme(int index);

  bool use_fahrenheit() const;  // temperature units (default false = Celsius)
  void set_use_fahrenheit(bool on);
};

}  // namespace platform
