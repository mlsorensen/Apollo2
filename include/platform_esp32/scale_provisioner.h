#pragma once

#include <vector>

#include "core/scale_provisioner.h"
#include "core/scale_roles.h"

namespace platform {

class ScaleLink;
class Config;

// Implements core::IScaleProvisioner for the device over the TWO scale links
// (link A + link B) and the ROLES they hold (core::ScaleRoles): scanning is
// delegated to link A's BLE task, save persists the scale (MAC + name) to the
// slot's NVS keys and points the link currently holding that role at it.
// Swap roles swaps the keys and the role pointers; the links keep their
// connections. Mirrors Provisioner, minus the token machinery (scales need
// none).
class ScaleProvisioner : public core::IScaleProvisioner {
 public:
  // `roles` was built over the device's two platform::ScaleLinks (link A =
  // espresso at boot); the provisioner recovers them from it.
  ScaleProvisioner(core::ScaleRoles& roles, Config& config);

  // The link holding a role right now (boot: A = espresso, B = bench), and
  // the role a link holds — device main needs both (partner registration,
  // the learned-type persister, the shot park).
  ScaleLink& link_for(core::ScaleSlot slot) const;
  core::ScaleSlot slot_of(const ScaleLink& link) const;

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
  ScaleLink& link(core::ScaleSlot slot) const { return link_for(slot); }
  ScaleLink& link_a() const;  // the always-running link (scans run on its task)
  // Seed the link holding a role from that slot's saved config (save, forget).
  void apply_slot(core::ScaleSlot slot);

  core::ScaleRoles& roles_;
  Config& config_;
};

}  // namespace platform
