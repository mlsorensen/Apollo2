#pragma once

#include <vector>

#include "core/provisioner.h"

namespace platform {

class MicraLink;
class Config;
class TokenSetup;

// Implements the core::IProvisioner port for the device: scanning is delegated
// to the BLE link's task, save persists to NVS and points the link at it, and
// token setup is delegated to the WiFi portal. Keeps MicraLink free of NVS,
// Config free of BLE, and the UI free of all three.
class Provisioner : public core::IProvisioner {
 public:
  Provisioner(MicraLink& link, Config& config, TokenSetup& token_setup);

  void start_scan() override;
  bool scanning() const override;
  std::vector<core::ScanResult> scan_results() const override;
  void save_device(const core::ScanResult& device) override;
  void retry_pairing() override;
  std::string saved_name() const override;
  void forget() override;

  bool has_token() const override;
  void start_token_setup() override;
  void stop_token_setup() override;
  bool token_setup_active() const override;
  const char* setup_ssid() const override;
  const char* setup_url() const override;

 private:
  MicraLink& link_;
  Config& config_;
  TokenSetup& token_setup_;
};

}  // namespace platform
