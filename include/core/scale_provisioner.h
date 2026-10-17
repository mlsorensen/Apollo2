#pragma once

#include <string>
#include <vector>

#include "core/provisioner.h"  // reuse core::ScanResult

// Discovery + setup port for Bluetooth scales — the scale-side analogue of
// IProvisioner. The UI scans for supported scales, saves the chosen one (its MAC
// to NVS), and the device reconnects to it on boot. Unlike the Micra, a scale
// needs no auth token, so this port is a trimmed IProvisioner.

namespace core {

class IScaleProvisioner {
 public:
  virtual ~IScaleProvisioner() = default;

  virtual void start_scan() = 0;                       // non-blocking
  virtual bool scanning() const = 0;
  virtual std::vector<ScanResult> scan_results() const = 0;

  // Persist the chosen scale (mac + name) and start connecting to it.
  virtual void save_scale(const ScanResult& scale, bool partner_mode = false) = 0;

  // The saved scale's display name, or "" if none is saved. The UI uses this to
  // decide whether to show the Scale tab + scale-aware Home at all.
  virtual std::string saved_name() const = 0;

  // Umbra partner mode (fw 1.0.8). The partner GATT service can only be
  // registered at boot, before any BLE link is active, and only when partner
  // mode is saved — so choosing an Umbra may need ONE restart to get ready.
  virtual bool partner_pairing_complete() const { return false; }
  virtual bool partner_ready() const { return false; }  // registered this boot
  // "Show the pairing prompt after the restart": set before rebooting for an
  // Umbra, read + cleared by the UI on the next boot.
  virtual bool partner_pair_pending() const { return false; }
  virtual void set_partner_pair_pending(bool /*pending*/) {}
  // Switch the SAVED scale in/out of partner mode (e.g. "Use unpaired").
  virtual void set_partner_mode(bool /*enabled*/) {}
  // Hold off partner advertising while the pairing prompt's grace period runs.
  virtual void hold_partner(bool /*held*/) {}

  // Clear the saved scale -> no scale configured.
  virtual void forget() = 0;

  // Manual connect control (mirrors IProvisioner): a battery remote can drop the
  // scale link to save power. Meaningful once a scale is saved.
  virtual bool connect_enabled() const = 0;
  virtual void set_connect_enabled(bool enabled) = 0;
};

}  // namespace core
