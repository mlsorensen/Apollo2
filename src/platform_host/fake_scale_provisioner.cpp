#include "platform_host/fake_scale_provisioner.h"

#include <cstring>

namespace host {

std::vector<core::ScanResult> FakeScaleProvisioner::scan_results() const {
  std::vector<core::ScanResult> out;
  auto add = [&](const char* name, const char* mac, int rssi) {
    core::ScanResult r{};
    std::strncpy(r.name, name, sizeof(r.name) - 1);
    std::strncpy(r.mac, mac, sizeof(r.mac) - 1);
    r.rssi = rssi;
    out.push_back(r);
  };
  // Synthetic entries (not real devices).
  add("BOOKOO_THEMIS", "60:55:f9:00:00:01", -58);
  add("BOOKOO_THEMIS", "60:55:f9:00:00:02", -74);
  return out;
}

}  // namespace host
