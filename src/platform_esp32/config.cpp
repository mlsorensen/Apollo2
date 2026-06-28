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

// Read a string key, returning "" if the namespace or key doesn't exist yet —
// without the ERROR log Preferences::getString() emits on a missing key.
namespace {
std::string read_key(const char* key) {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return "";  // namespace absent
  std::string out;
  if (p.isKey(key)) out = p.getString(key, "").c_str();
  p.end();
  return out;
}
}  // namespace

std::string Config::mac() const { return read_key(kMacKey); }

std::string Config::name() const { return read_key(kNameKey); }

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
