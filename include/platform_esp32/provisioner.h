#pragma once

#include <vector>

#include "core/provisioner.h"

namespace platform {

class MicraLink;
class Config;

// Implements the core::IProvisioner port for the device: scanning is delegated
// to the BLE link's task, and save persists the MAC to NVS and points the link
// at it. Keeps MicraLink free of NVS and Config free of BLE.
class Provisioner : public core::IProvisioner {
 public:
  Provisioner(MicraLink& link, Config& config);

  void start_scan() override;
  bool scanning() const override;
  std::vector<core::ScanResult> scan_results() const override;
  void save_device(const core::ScanResult& device) override;
  std::string saved_name() const override;
  void forget() override;

 private:
  MicraLink& link_;
  Config& config_;
};

}  // namespace platform
