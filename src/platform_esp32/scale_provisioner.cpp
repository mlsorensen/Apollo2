#include "platform_esp32/scale_provisioner.h"

#include "core/system.h"
#include "platform_esp32/config.h"
#include "platform_esp32/scale_link.h"

namespace platform {

using core::ScaleSlot;

ScaleProvisioner::ScaleProvisioner(core::ScaleRoles& roles, Config& config)
    : roles_(roles), config_(config) {}

// The roles hold the links as core::IScale; on the device they are the two
// platform::ScaleLinks by construction (device main builds g_roles over
// them), so the downcast is exact.
ScaleLink& ScaleProvisioner::link_for(ScaleSlot slot) const {
  return static_cast<ScaleLink&>(roles_.link_for(slot));
}

ScaleLink& ScaleProvisioner::link_a() const {
  return static_cast<ScaleLink&>(roles_.link_a());
}

core::ScaleSlot ScaleProvisioner::slot_of(const ScaleLink& link) const {
  return roles_.slot_of(link);
}

// Scans run on link A's task (either would do: the arbiter grants the user
// scan to whichever slot asks, and A's task always runs); results are read
// back from it.
void ScaleProvisioner::start_scan() { link_a().request_scan(); }

bool ScaleProvisioner::scanning() const { return link_a().scanning(); }

std::vector<core::ScanResult> ScaleProvisioner::scan_results() const {
  return link_a().scan_results();
}

void ScaleProvisioner::apply_slot(ScaleSlot slot) {
  ScaleLink& l = link(slot);
  const std::string mac = config_.scale_mac(slot);
  l.set_name(config_.scale_name(slot));
  l.set_partner_mode(config_.scale_partner_mode(slot));
  l.set_connect_enabled(!mac.empty());
  l.set_address(mac, config_.scale_addr_type(slot));
}

bool ScaleProvisioner::save_scale(const core::ScanResult& scale, ScaleSlot slot,
                                  bool partner_mode) {
  // One partner-mode Umbra at a time: the device has one partner GATT
  // service and one inbound session.
  const int ps = config_.scale_partner_slot();
  if (partner_mode && ps >= 0 && ps != static_cast<int>(slot)) {
    core::logf("ScaleProvisioner: partner mode refused for %s (%s already has it)\n",
               core::scale_slot_name(slot),
               core::scale_slot_name(static_cast<ScaleSlot>(ps)));
    return false;
  }
  config_.save_scale(slot, scale.mac, scale.name, partner_mode, scale.addr_type);
  apply_slot(slot);
  return true;
}

std::string ScaleProvisioner::saved_name(ScaleSlot slot) const {
  return config_.scale_name(slot);
}

void ScaleProvisioner::swap_roles() {
  // The role pointers swap and the saved keys swap with them (so a rolled-
  // back build reads the espresso keys as the espresso scale); the links —
  // their connections, names, learned types, a paired Umbra's partner
  // session — stay exactly where they are. Nothing reconnects, nothing
  // restarts. The next boot seeds link A from the espresso keys, i.e. the
  // scale that is espresso NOW (link B's), with the identity mapping: the
  // two views agree because the keys and the pointers moved together.
  // ONE LOCK across both steps: a link's per-link persister (on its BLE
  // task) resolves its slot through the roles and writes that slot's keys.
  // Between the two steps the pointers and the keys disagree in EITHER
  // order — a write landing there stamps the link's value onto the other
  // scale's record — so persisters run under the same lock (persist_for).
  std::lock_guard<std::mutex> lk(swap_mu_);
  roles_.swap();
  config_.swap_scales();
}

bool ScaleProvisioner::partner_pairing_complete() const {
  const int ps = config_.scale_partner_slot();
  return ps >= 0 && link(static_cast<ScaleSlot>(ps)).partner_pairing_complete();
}

bool ScaleProvisioner::partner_ready() const {
  const int ps = config_.scale_partner_slot();
  return ps >= 0 && link(static_cast<ScaleSlot>(ps)).partner_registered();
}

int ScaleProvisioner::partner_slot() const { return config_.scale_partner_slot(); }

bool ScaleProvisioner::partner_pair_pending() const {
  return config_.scale_partner_pending(ScaleSlot::kEspresso) ||
         config_.scale_partner_pending(ScaleSlot::kBench);
}

void ScaleProvisioner::set_partner_pair_pending(bool pending) {
  // Set on the partner slot; cleared on both (the prompt is one per boot).
  if (pending) {
    const int ps = config_.scale_partner_slot();
    if (ps >= 0) config_.set_scale_partner_pending(static_cast<ScaleSlot>(ps), true);
    return;
  }
  config_.set_scale_partner_pending(ScaleSlot::kEspresso, false);
  config_.set_scale_partner_pending(ScaleSlot::kBench, false);
}

void ScaleProvisioner::set_partner_mode(bool enabled) {
  // Addresses the slot in partner mode; with none, "enable" means the
  // espresso slot (the pre-v1.2 behaviour).
  int ps = config_.scale_partner_slot();
  if (ps < 0 && !enabled) return;
  const ScaleSlot slot = ps < 0 ? ScaleSlot::kEspresso : static_cast<ScaleSlot>(ps);
  config_.save_scale(slot, config_.scale_mac(slot), config_.scale_name(slot), enabled,
                     config_.scale_addr_type(slot));
  link(slot).set_partner_mode(enabled);  // address unchanged: central resumes on it
}

void ScaleProvisioner::hold_partner(bool held) {
  const int ps = config_.scale_partner_slot();
  if (ps >= 0) link(static_cast<ScaleSlot>(ps)).hold_partner(held);
}

void ScaleProvisioner::forget(ScaleSlot slot) {
  config_.clear_scale(slot);
  ScaleLink& l = link(slot);
  l.set_name("");
  l.set_partner_mode(false);
  l.set_address("");  // drop the link + idle
  l.set_connect_enabled(false);
}

bool ScaleProvisioner::connect_enabled(ScaleSlot slot) const {
  return link(slot).connect_enabled();
}

void ScaleProvisioner::set_connect_enabled(ScaleSlot slot, bool enabled) {
  link(slot).set_connect_enabled(enabled);
}

}  // namespace platform
