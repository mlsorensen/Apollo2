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
  add("MICRA_MR000011", "30:c6:f7:00:00:13", -61);
  add("MICRA_MR000012", "30:c6:f7:00:00:12", -78);
  return out;
}

}  // namespace host
