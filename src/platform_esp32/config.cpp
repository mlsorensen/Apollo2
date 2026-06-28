#include "platform_esp32/config.h"

#include <Preferences.h>

// NVS is initialized by the Arduino-ESP32 core at boot, so we just open the
// namespace per call (cheap, and avoids holding the handle open).
namespace {
constexpr char kNamespace[] = "micra";
constexpr char kMacKey[] = "mac";
constexpr char kNameKey[] = "name";
}  // namespace

namespace platform {

std::string Config::mac() const {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/true);
  const String v = p.getString(kMacKey, "");
  p.end();
  return std::string(v.c_str());
}

std::string Config::name() const {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/true);
  const String v = p.getString(kNameKey, "");
  p.end();
  return std::string(v.c_str());
}

void Config::save(const std::string& mac, const std::string& name) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kMacKey, mac.c_str());
  p.putString(kNameKey, name.c_str());
  p.end();
}

void Config::clear() {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.remove(kMacKey);
  p.remove(kNameKey);
  p.end();
}

}  // namespace platform
