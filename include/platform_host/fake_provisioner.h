#pragma once

#include "core/provisioner.h"

// Host stand-in for the device provisioner: returns a couple of sample devices
// so the Settings scan list can be designed/rendered in the simulator. Save is
// a no-op.

namespace host {

class FakeProvisioner : public core::IProvisioner {
 public:
  void start_scan() override {}
  bool scanning() const override { return false; }
  std::vector<core::ScanResult> scan_results() const override;
  void save_device(const core::ScanResult& /*device*/) override {}
  std::string saved_name() const override { return ""; }
  void forget() override {}
};

}  // namespace host
