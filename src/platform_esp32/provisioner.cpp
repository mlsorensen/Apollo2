#include "platform_esp32/provisioner.h"

#include <string>

#include "platform_esp32/config.h"
#include "platform_esp32/micra_link.h"

namespace platform {

Provisioner::Provisioner(MicraLink& link, Config& config)
    : link_(link), config_(config) {}

void Provisioner::start_scan() { link_.request_scan(); }

bool Provisioner::scanning() const { return link_.scanning(); }

std::vector<core::ScanResult> Provisioner::scan_results() const {
  return link_.scan_results();
}

void Provisioner::save_device(const core::ScanResult& device) {
  config_.save(device.mac, device.name);  // persist for next boot
  link_.set_address(device.mac);          // connect now
}

std::string Provisioner::saved_name() const {
  const std::string name = config_.name();
  return name.empty() ? config_.mac() : name;  // fall back to MAC if unnamed
}

void Provisioner::forget() {
  config_.clear();
  link_.set_address("");  // -> Unconfigured
}

}  // namespace platform
