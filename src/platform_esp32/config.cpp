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
constexpr char kScreenTimeoutKey[] = "scrtimeout";
constexpr char kClock24Key[] = "clock24";
constexpr char kThemeKey[] = "theme";
constexpr char kFahrenheitKey[] = "fahr";
constexpr char kDropNegFlowKey[] = "dropnegf";
constexpr char kScopeGraphKey[] = "scopegraph";
constexpr char kPerfOverlayKey[] = "perfovl";
constexpr char kClickSoundKey[] = "clicksnd";
constexpr char kScaleMacKey[] = "smac";
constexpr char kScaleNameKey[] = "sname";
constexpr char kTargetKey[] = "tgtg";
constexpr char kShotModeKey[] = "shotmode";
constexpr char kOvershootKey[] = "ovshoot";
constexpr char kReviewHoldKey[] = "revhold";
constexpr char kAutoConnectKey[] = "autoconn";
constexpr char kWiredPaddleKey[] = "wiredpad";
constexpr char kFlowSmoothKey[] = "flowsmth";
constexpr char kWifiEnKey[] = "wifi_en";
constexpr char kWifiSsidKey[] = "ssid";
constexpr char kWifiPassKey[] = "wifipass";
constexpr char kTzKey[] = "tz";
constexpr char kNtpKey[] = "ntp";
constexpr char kNtpEnKey[] = "ntp_en";
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

void Config::begin() {
  // Opening read-write creates the namespace if absent, so later read-only
  // begins (brightness/theme/etc.) don't log "nvs_open failed: NOT_FOUND" on a
  // fresh device. The sentinel key guarantees the namespace persists.
  Preferences p;
  if (p.begin(kNamespace, /*readOnly=*/false)) {
    if (!p.isKey("_init")) p.putBool("_init", true);
    p.end();
  }
}

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

std::string Config::scale_mac() const { return read_key(kScaleMacKey); }

std::string Config::scale_name() const { return read_key(kScaleNameKey); }

void Config::save_scale(const std::string& mac, const std::string& name) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kScaleMacKey, mac.c_str());
  p.putString(kScaleNameKey, name.c_str());
  p.end();
}

void Config::clear_scale() {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.remove(kScaleMacKey);
  p.remove(kScaleNameKey);
  p.end();
}

float Config::target_weight_g() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 36.0f;
  const float v = p.isKey(kTargetKey) ? p.getFloat(kTargetKey, 36.0f) : 36.0f;
  p.end();
  return v;
}

float Config::overshoot_g() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 2.0f;
  const float v = p.isKey(kOvershootKey) ? p.getFloat(kOvershootKey, 2.0f) : 2.0f;
  p.end();
  return v;
}

void Config::set_overshoot_g(float grams) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putFloat(kOvershootKey, grams);
  p.end();
}

int Config::flow_smooth() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 1;
  const int v = p.isKey(kFlowSmoothKey) ? p.getInt(kFlowSmoothKey, 1) : 1;
  p.end();
  return v;
}

void Config::set_flow_smooth(int level) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kFlowSmoothKey, level);
  p.end();
}

bool Config::auto_connect() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.isKey(kAutoConnectKey) ? p.getBool(kAutoConnectKey, false) : false;
  p.end();
  return v;
}

void Config::set_auto_connect(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kAutoConnectKey, on);
  p.end();
}

bool Config::wired_paddle() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kWiredPaddleKey) ? p.getBool(kWiredPaddleKey, true) : true;
  p.end();
  return v;
}

void Config::set_wired_paddle(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kWiredPaddleKey, on);
  p.end();
}

int Config::review_hold_s() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 30;
  const int v = p.isKey(kReviewHoldKey) ? p.getInt(kReviewHoldKey, 30) : 30;
  p.end();
  return v;
}

void Config::set_review_hold_s(int seconds) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kReviewHoldKey, seconds);
  p.end();
}

bool Config::shot_mode() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kShotModeKey) ? p.getBool(kShotModeKey, true) : true;
  p.end();
  return v;
}

void Config::set_shot_mode(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kShotModeKey, on);
  p.end();
}

void Config::set_target_weight_g(float grams) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putFloat(kTargetKey, grams);
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

int Config::screen_timeout_min() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 30;
  const int v = p.isKey(kScreenTimeoutKey) ? p.getInt(kScreenTimeoutKey, 30) : 30;
  p.end();
  return v;
}

void Config::set_screen_timeout_min(int minutes) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putInt(kScreenTimeoutKey, minutes);
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

bool Config::use_fahrenheit() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.isKey(kFahrenheitKey) ? p.getBool(kFahrenheitKey, false) : false;
  p.end();
  return v;
}

void Config::set_use_fahrenheit(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kFahrenheitKey, on);
  p.end();
}

bool Config::drop_negative_flow() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kDropNegFlowKey) ? p.getBool(kDropNegFlowKey, true) : true;
  p.end();
  return v;
}

void Config::set_drop_negative_flow(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kDropNegFlowKey, on);
  p.end();
}

bool Config::scope_graph() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.isKey(kScopeGraphKey) ? p.getBool(kScopeGraphKey, true) : true;
  p.end();
  return v;
}

void Config::set_scope_graph(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kScopeGraphKey, on);
  p.end();
}

bool Config::perf_overlay() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.isKey(kPerfOverlayKey) ? p.getBool(kPerfOverlayKey, false) : false;
  p.end();
  return v;
}

void Config::set_perf_overlay(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kPerfOverlayKey, on);
  p.end();
}

bool Config::click_sound() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kClickSoundKey) ? p.getBool(kClickSoundKey, true) : true;
  p.end();
  return v;
}

void Config::set_click_sound(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kClickSoundKey, on);
  p.end();
}

bool Config::wifi_enabled() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return false;
  const bool v = p.isKey(kWifiEnKey) ? p.getBool(kWifiEnKey, false) : false;
  p.end();
  return v;
}

void Config::set_wifi_enabled(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kWifiEnKey, on);
  p.end();
}

std::string Config::wifi_ssid() const { return read_key(kWifiSsidKey); }

std::string Config::wifi_password() const { return read_key(kWifiPassKey); }

void Config::save_wifi(const std::string& ssid, const std::string& password) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kWifiSsidKey, ssid.c_str());
  p.putString(kWifiPassKey, password.c_str());  // plaintext, as ESP-IDF's own wifi store
  p.end();
}

void Config::clear_wifi() {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.remove(kWifiSsidKey);
  p.remove(kWifiPassKey);
  p.end();
}

std::string Config::timezone() const {
  const std::string tz = read_key(kTzKey);
  return tz.empty() ? "UTC0" : tz;
}

void Config::set_timezone(const std::string& tz) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kTzKey, tz.c_str());
  p.end();
}

std::string Config::ntp_server() const {
  const std::string host = read_key(kNtpKey);
  return host.empty() ? "pool.ntp.org" : host;
}

void Config::set_ntp_server(const std::string& host) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putString(kNtpKey, host.c_str());
  p.end();
}

bool Config::ntp_enabled() const {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return true;
  const bool v = p.isKey(kNtpEnKey) ? p.getBool(kNtpEnKey, true) : true;
  p.end();
  return v;
}

void Config::set_ntp_enabled(bool on) {
  Preferences p;
  p.begin(kNamespace, /*readOnly=*/false);
  p.putBool(kNtpEnKey, on);
  p.end();
}

}  // namespace platform
