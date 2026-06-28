#pragma once

#include <string>

// Persistent device configuration in NVS flash: the saved machine (BLE MAC +
// display name) chosen via the Settings scan. The auth token will move here
// later; it is compile-time (secrets.h) for now.

namespace platform {

class Config {
 public:
  std::string mac() const;    // saved MAC, or "" if none
  std::string name() const;   // saved display name, or "" if none
  std::string token() const;  // saved BLE auth token, or "" if none

  void save(const std::string& mac, const std::string& name);
  void set_token(const std::string& token);
  void clear();               // forget the saved machine (incl. its token)
};

}  // namespace platform
