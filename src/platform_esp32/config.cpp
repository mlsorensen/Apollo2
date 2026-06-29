#include "platform_esp32/config.h"

#include <Preferences.h>

// NVS is initialized by the Arduino-ESP32 core at boot, so we just open the
// namespace per call (cheap, and avoids holding the handle open).
namespace {
constexpr char kNamespace[] = "micra";
constexpr char kMacKey[] = "mac";
constexpr char kNameKey[] = "name";
constexpr char kTokenKey[] = "token";
constexpr char kBrightnessKey[] = "bright";
constexpr char kClock24Key[] = "clock24";
constexpr char kThemeKey[] = "theme";
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

std::string Config::token() const { return read_key(kTokenKey); }

void Config::save(const std::string& mac, const std::string& name) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kMacKey, mac.c_str());
  p.putString(kNameKey, name.c_str());
  p.end();
}

void Config::set_token(const std::string& token) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kTokenKey, token.c_str());
  p.end();
}

void Config::clear() {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.remove(kMacKey);
  p.remove(kNameKey);
  p.remove(kTokenKey);  // wipe the secret too
  p.end();
}

int Config::brightness() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 100;
  const int v = p.isKey(kBrightnessKey) ? p.getInt(kBrightnessKey, 100) : 100;
  p.end();
  return v;
}

void Config::set_brightness(int percent) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kBrightnessKey, percent);
  p.end();
}

bool Config::clock_24h() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kClock24Key) ? p.getBool(kClock24Key, true) : true;
  p.end();
  return v;
}

void Config::set_clock_24h(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kClock24Key, on);
  p.end();
}

int Config::theme() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 0;
  const int v = p.isKey(kThemeKey) ? p.getInt(kThemeKey, 0) : 0;
  p.end();
  return v;
}

void Config::set_theme(int index) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kThemeKey, index);
  p.end();
}

}  // namespace platform
