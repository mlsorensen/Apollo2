#include "platform_esp32/provisioner.h"

#include <string>

#include "platform_esp32/config.h"
#include "platform_esp32/micra_link.h"
#include "platform_esp32/token_setup.h"

namespace platform {

Provisioner::Provisioner(MicraLink& link, Config& config, TokenSetup& token_setup)
    : link_(link), config_(config), token_setup_(token_setup) {}

void Provisioner::start_scan() { link_.request_scan(); }

bool Provisioner::scanning() const { return link_.scanning(); }

std::vector<core::ScanResult> Provisioner::scan_results() const {
  return link_.scan_results();
}

void Provisioner::save_device(const core::ScanResult& device) {
  config_.save(device.mac, device.name);  // persist for next boot
  link_.set_address(device.mac);
  link_.request_pairing_read();           // try to grab the token from pairing mode
}

void Provisioner::retry_pairing() { link_.request_pairing_read(); }

std::string Provisioner::saved_name() const {
  const std::string name = config_.name();
  return name.empty() ? config_.mac() : name;  // fall back to MAC if unnamed
}

void Provisioner::forget() {
  config_.clear();         // wipes MAC + name + token
  link_.set_token("");
  link_.set_address("");   // -> Unconfigured
}

bool Provisioner::has_token() const { return !config_.token().empty(); }

void Provisioner::start_token_setup() { token_setup_.start(); }

void Provisioner::stop_token_setup() { token_setup_.stop(); }

bool Provisioner::token_setup_active() const { return token_setup_.active(); }

const char* Provisioner::setup_ssid() const { return token_setup_.ssid(); }

const char* Provisioner::setup_url() const { return token_setup_.url(); }

}  // namespace platform
