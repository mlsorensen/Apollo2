#pragma once

#include <string>
#include <vector>

// Discovery + setup port: how the UI scans for machines and saves the chosen
// one, without knowing anything about BLE or flash. Implemented on the device
// by glue over the BLE link + NVS; faked on the host. Mirrors how IMachine is
// the port for control/state.

namespace core {

// A discovered device. Fixed char buffers (not pointers) so a result is
// trivially copyable across the scan-task / UI-thread boundary.
struct ScanResult {
  char name[24];
  char mac[18];  // "aa:bb:cc:dd:ee:ff" + NUL
  int rssi;
};

class IProvisioner {
 public:
  virtual ~IProvisioner() = default;

  virtual void start_scan() = 0;                       // non-blocking
  virtual bool scanning() const = 0;
  virtual std::vector<ScanResult> scan_results() const = 0;

  // Persist the chosen device (mac + name) and start pairing: it first tries to
  // read the token from the machine (pairing mode); if that fails the link
  // settles in NeedsToken and the UI offers WiFi entry.
  virtual void save_device(const ScanResult& device) = 0;

  // Re-attempt the pairing-mode token read (e.g. after the user enabled pairing).
  virtual void retry_pairing() = 0;

  // The saved machine's display name, or "" if none is saved.
  virtual std::string saved_name() const = 0;

  // Clear the saved machine (and its token) -> back to Unconfigured.
  virtual void forget() = 0;

  // --- Token setup (WiFi portal) ---
  virtual bool has_token() const = 0;          // is a token saved for the machine?
  virtual void start_token_setup() = 0;        // bring up the WiFi paste page
  virtual void stop_token_setup() = 0;         // cancel it
  virtual bool token_setup_active() const = 0;
  virtual const char* setup_ssid() const = 0;  // WiFi name to join
  virtual const char* setup_url() const = 0;   // URL to open
};

}  // namespace core
