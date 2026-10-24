#pragma once

#include <vector>

#include "core/scale_provisioner.h"

namespace platform {

class ScaleLink;
class Config;

// Implements core::IScaleProvisioner for the device over the TWO scale links
// (espresso + bench): scanning is delegated to the espresso link's BLE task,
// save persists the scale (MAC + name) to the slot's NVS keys and points that
// slot's link at it. Mirrors Provisioner, minus the token machinery (scales
// need none).
class ScaleProvisioner : public core::IScaleProvisioner {
 public:
  ScaleProvisioner(ScaleLink& espresso, ScaleLink& bench, Config& config);

  void start_scan() override;
  bool scanning() const override;
  std::vector<core::ScanResult> scan_results() const override;
  bool save_scale(const core::ScanResult& scale, core::ScaleSlot slot,
                  bool partner_mode = false) override;
  std::string saved_name(core::ScaleSlot slot = core::ScaleSlot::kEspresso) const override;
  void swap_roles() override;
  bool partner_pairing_complete() const override;
  bool partner_ready() const override;
  int partner_slot() const override;
  bool partner_pair_pending() const override;
  void set_partner_pair_pending(bool pending) override;
  void set_partner_mode(bool enabled) override;
  void hold_partner(bool held) override;
  void forget(core::ScaleSlot slot = core::ScaleSlot::kEspresso) override;
  bool connect_enabled(core::ScaleSlot slot = core::ScaleSlot::kEspresso) const override;
  void set_connect_enabled(core::ScaleSlot slot, bool enabled) override;

 private:
  ScaleLink& link(core::ScaleSlot slot) const;
  // Seed a link from its slot's saved config (boot, save, swap, forget).
  void apply_slot(core::ScaleSlot slot);

  ScaleLink& espresso_;
  ScaleLink& bench_;
  Config& config_;
};

}  // namespace platform
