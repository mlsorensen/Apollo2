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
  virtual void save_scale(const ScanResult& scale) = 0;

  // The saved scale's display name, or "" if none is saved. The UI uses this to
  // decide whether to show the Scale tab + scale-aware Home at all.
  virtual std::string saved_name() const = 0;

  // Clear the saved scale -> no scale configured.
  virtual void forget() = 0;

  // Manual connect control (mirrors IProvisioner): a battery remote can drop the
  // scale link to save power. Meaningful once a scale is saved.
  virtual bool connect_enabled() const = 0;
  virtual void set_connect_enabled(bool enabled) = 0;
};

}  // namespace core
