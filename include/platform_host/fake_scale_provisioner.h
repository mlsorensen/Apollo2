#pragma once

#include "core/scale_provisioner.h"

// Host stand-in for the device scale provisioner: returns a couple of sample
// scales so the Settings "Scales" scan list can be rendered in the simulator.
// `set_saved` / `set_bench_saved` let the sim render the "scale configured"
// UI (Scale tab visible, scale-aware Home), the two-scale pages and the
// "no scale" baseline.

namespace host {

class FakeScaleProvisioner : public core::IScaleProvisioner {
 public:
  void start_scan() override {}
  bool scanning() const override { return false; }
  std::vector<core::ScanResult> scan_results() const override;
  bool save_scale(const core::ScanResult& /*scale*/, core::ScaleSlot /*slot*/,
                  bool /*partner_mode*/ = false) override {
    return true;
  }
  std::string saved_name(core::ScaleSlot slot = core::ScaleSlot::kEspresso) const override {
    if (slot == core::ScaleSlot::kBench) return bench_saved_ ? "BOOKOO_ULTRA" : "";
    return saved_ ? "BOOKOO_THEMIS" : "";
  }
  void swap_roles() override {}
  void forget(core::ScaleSlot /*slot*/ = core::ScaleSlot::kEspresso) override {}
  bool connect_enabled(core::ScaleSlot slot = core::ScaleSlot::kEspresso) const override {
    return slot == core::ScaleSlot::kBench ? bench_connect_enabled_ : connect_enabled_;
  }
  void set_connect_enabled(core::ScaleSlot slot, bool enabled) override {
    if (slot == core::ScaleSlot::kBench) bench_connect_enabled_ = enabled;
    else connect_enabled_ = enabled;
  }

  void set_saved(bool s) { saved_ = s; }
  void set_bench_saved(bool s) { bench_saved_ = s; }

 private:
  bool saved_ = true;
  bool bench_saved_ = false;  // renders are single-scale unless a scene says so
  bool connect_enabled_ = true;
  bool bench_connect_enabled_ = true;
};

}  // namespace host
