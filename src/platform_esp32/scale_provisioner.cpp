#include "platform_esp32/scale_provisioner.h"

#include "platform_esp32/config.h"
#include "platform_esp32/scale_link.h"

namespace platform {

ScaleProvisioner::ScaleProvisioner(ScaleLink& link, Config& config)
    : link_(link), config_(config) {}

void ScaleProvisioner::start_scan() { link_.request_scan(); }

bool ScaleProvisioner::scanning() const { return link_.scanning(); }

std::vector<core::ScanResult> ScaleProvisioner::scan_results() const {
  return link_.scan_results();
}

void ScaleProvisioner::save_scale(const core::ScanResult& scale, bool partner_mode) {
  config_.save_scale(scale.mac, scale.name, partner_mode, scale.addr_type);
  link_.set_name(scale.name);
  link_.set_partner_mode(partner_mode);
  link_.set_connect_enabled(true);
  link_.set_address(scale.mac, scale.addr_type);
}

std::string ScaleProvisioner::saved_name() const { return config_.scale_name(); }

bool ScaleProvisioner::partner_pairing_complete() const {
  return link_.partner_pairing_complete();
}

bool ScaleProvisioner::partner_ready() const { return link_.partner_registered(); }

bool ScaleProvisioner::partner_pair_pending() const {
  return config_.scale_partner_pending();
}

void ScaleProvisioner::set_partner_pair_pending(bool pending) {
  config_.set_scale_partner_pending(pending);
}

void ScaleProvisioner::set_partner_mode(bool enabled) {
  config_.save_scale(config_.scale_mac(), config_.scale_name(), enabled,
                     config_.scale_addr_type());
  link_.set_partner_mode(enabled);  // address unchanged: central resumes on it
}

void ScaleProvisioner::hold_partner(bool held) { link_.hold_partner(held); }

void ScaleProvisioner::forget() {
  config_.clear_scale();
  link_.set_name("");
  link_.set_partner_mode(false);
  link_.set_address("");  // drop the link + idle
  link_.set_connect_enabled(false);
}

bool ScaleProvisioner::connect_enabled() const { return link_.connect_enabled(); }

void ScaleProvisioner::set_connect_enabled(bool enabled) {
  link_.set_connect_enabled(enabled);
}

}  // namespace platform
