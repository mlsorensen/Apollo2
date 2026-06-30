#pragma once

#include <vector>

#include "core/scale_provisioner.h"

namespace platform {

class ScaleLink;
class Config;

// Implements core::IScaleProvisioner for the device: scanning is delegated to the
// ScaleLink's BLE task, save persists the scale (MAC + name) to NVS and points the
// link at it. Mirrors Provisioner, minus the token machinery (scales need none).
class ScaleProvisioner : public core::IScaleProvisioner {
 public:
  ScaleProvisioner(ScaleLink& link, Config& config);

  void start_scan() override;
  bool scanning() const override;
  std::vector<core::ScanResult> scan_results() const override;
  void save_scale(const core::ScanResult& scale) override;
  std::string saved_name() const override;
  void forget() override;
  bool connect_enabled() const override;
  void set_connect_enabled(bool enabled) override;

 private:
  ScaleLink& link_;
  Config& config_;
};

}  // namespace platform
