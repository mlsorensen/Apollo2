#pragma once

#include "core/scale_provisioner.h"

// Host stand-in for the device scale provisioner: returns a couple of sample
// scales so the Settings "Scales" scan list can be rendered in the simulator.
// `set_saved` lets the sim render both the "scale configured" UI (Scale tab
// visible, scale-aware Home) and the "no scale" baseline.

namespace host {

class FakeScaleProvisioner : public core::IScaleProvisioner {
 public:
  void start_scan() override {}
  bool scanning() const override { return false; }
  std::vector<core::ScanResult> scan_results() const override;
  void save_scale(const core::ScanResult& /*scale*/) override {}
  std::string saved_name() const override { return saved_ ? "BOOKOO_THEMIS" : ""; }
  void forget() override {}
  bool connect_enabled() const override { return connect_enabled_; }
  void set_connect_enabled(bool enabled) override { connect_enabled_ = enabled; }

  void set_saved(bool s) { saved_ = s; }

 private:
  bool saved_ = true;
  bool connect_enabled_ = true;
};

}  // namespace host
