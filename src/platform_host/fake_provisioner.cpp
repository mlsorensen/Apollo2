#include "platform_host/fake_provisioner.h"

#include <cstring>

namespace host {

std::vector<core::ScanResult> FakeProvisioner::scan_results() const {
  std::vector<core::ScanResult> out;
  auto add = [&](const char* name, const char* mac, int rssi) {
    core::ScanResult r{};
    std::strncpy(r.name, name, sizeof(r.name) - 1);
    std::strncpy(r.mac, mac, sizeof(r.mac) - 1);
    r.rssi = rssi;
    out.push_back(r);
  };
  // Synthetic entries (not real devices).
  add("MICRA_MR000001", "30:c6:f7:00:00:01", -61);
  add("MICRA_MR000002", "30:c6:f7:00:00:02", -78);
  add("MICRA_MR000003", "30:c6:f7:00:00:03", -83);
  add("MICRA_MR000004", "30:c6:f7:00:00:04", -90);
  return out;
}

}  // namespace host
