#pragma once

#include <string>

#include "core/machine.h"

// BLE client for the La Marzocco Micra, implementing the device-agnostic
// core::IMachine port. The UI and the rest of the app talk to this exactly as
// they talk to the host FakeMachine — they never see NimBLE.
//
// Protocol (reverse-engineered, from pylamarzocco): connect to a device whose
// name starts with "MICRA", write the auth token to the AUTH characteristic,
// then read state by writing a setting name to the READ characteristic and
// reading the JSON back, and send commands as JSON to the WRITE characteristic.
// All NimBLE details live in the .cpp so this header stays transport-free.

namespace platform {

class MicraLink : public core::IMachine {
 public:
  explicit MicraLink(std::string auth_token);

  // Blocking: connect to the machine at `address` (e.g. "30:c6:f7:..."),
  // discover characteristics, and authenticate. No scan — the address is
  // expected to be known/saved. Progress and failures are logged to Serial.
  bool connect(const std::string& address);
  bool isConnected() const;

  // Read current state from the machine into the cached snapshot. Returns true
  // on a successful read + parse.
  bool refresh();

  // Command: machine on (BrewingMode) vs standby.
  bool setPower(bool on);

  // core::IMachine — returns the last refreshed state (cheap, no I/O).
  core::MachineSnapshot snapshot() const override;

 private:
  void parse_mode(const std::string& json);
  void parse_boilers(const std::string& json);

  std::string token_;

  // Cached state; snapshot() returns pointers into name_/status_, so they must
  // outlive the returned snapshot (they live as long as this object).
  std::string name_ = "Micra";
  std::string status_;
  core::Power power_ = core::Power::Off;
  float brew_temp_c_ = 0.0f;
  float brew_target_c_ = 0.0f;
  float boiler_temp_c_ = 0.0f;
  float boiler_target_c_ = 0.0f;
  bool brewing_ = false;
  bool connected_ = false;
};

}  // namespace platform
