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

void ScaleProvisioner::save_scale(const core::ScanResult& scale) {
  config_.save_scale(scale.mac, scale.name);
  link_.set_name(scale.name);
  link_.set_connect_enabled(true);
  link_.set_address(scale.mac);  // points the link at it -> connects
}

std::string ScaleProvisioner::saved_name() const { return config_.scale_name(); }

void ScaleProvisioner::forget() {
  config_.clear_scale();
  link_.set_address("");  // drop the link + idle
}

bool ScaleProvisioner::connect_enabled() const { return link_.connect_enabled(); }

void ScaleProvisioner::set_connect_enabled(bool enabled) {
  link_.set_connect_enabled(enabled);
}

}  // namespace platform
