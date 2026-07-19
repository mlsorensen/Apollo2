#include "core/micra_link.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/system.h"

namespace {

// GATT characteristic UUIDs (from pylamarzocco). The parent service UUID isn't
// published, so we discover characteristics by UUID across all services.
constexpr char kReadUuid[] = "0a0b7847-e12b-09a8-b04b-8e0922a9abab";   // state
constexpr char kWriteUuid[] = "0b0b7847-e12b-09a8-b04b-8e0922a9abab";  // command
constexpr char kTokenUuid[] = "0c0b7847-e12b-09a8-b04b-8e0922a9abab";  // pairing-mode token
constexpr char kAuthUuid[] = "0d0b7847-e12b-09a8-b04b-8e0922a9abab";   // auth

// Standard Device Information Service (0x180A) string characteristics.
constexpr char kDisManufacturer[] = "2a29";
constexpr char kDisModel[] = "2a24";
constexpr char kDisSerial[] = "2a25";
constexpr char kDisFirmware[] = "2a26";
constexpr char kDisSoftware[] = "2a28";

constexpr uint32_t kPollIntervalMs = 3000;
constexpr uint32_t kReconnectBackoffMs = 3000;
constexpr uint32_t kConnectTimeoutMs = 30000;  // per address-type attempt

bool write_with_nul(core::ble::ICentral& ble, const char* uuid,
                    const std::string& payload) {
  std::string buf = payload;
  buf.push_back('\0');
  return ble.write(uuid, buf.data(), buf.size(), /*with_response=*/true);
}

}  // namespace

namespace core {

bool MicraLink::read_setting(const char* name, std::string& out) {
  if (!write_with_nul(ble_, kReadUuid, name)) return false;
  sleep_ms(150);  // let the machine replace the characteristic value with its answer
  if (!ble_.read(kReadUuid, out)) return false;
  return !out.empty();
}

void MicraLink::set_address(std::string address) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    address_ = std::move(address);
  }
  token_bad_.store(false);  // new machine -> give its token a fresh chance
  reconnect_requested_.store(true);
}

void MicraLink::set_token(std::string token) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    token_ = std::move(token);
  }
  token_bad_.store(false);  // a new token deserves a fresh attempt
  reconnect_requested_.store(true);
}

void MicraLink::set_connect_enabled(bool enabled) {
  connect_enabled_.store(enabled);
  reconnect_requested_.store(true);  // nudge the loop to act now (drop or reconnect)
}

void MicraLink::set_name(std::string name) {
  std::lock_guard<std::mutex> lk(mutex_);
  name_ = name.empty() ? "Micra" : std::move(name);
}

void MicraLink::request_pairing_read() {
  set_link(Link::Connecting);  // reflect "working" at once (avoids a brief
  try_pairing_.store(true);    // NeedsToken flash before the loop acts)
}

void MicraLink::set_token_persister(std::function<void(std::string)> persister) {
  token_persister_ = std::move(persister);
}

void MicraLink::set_link(Link link) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (link != link_) {
    // Silent drops matter: the paddle's wake-vs-shot decision needs Connected,
    // and a stale power_ + dropped link reads as "shot path" at the next flip.
    static const char* const kNames[] = {"Unconfigured", "NeedsToken", "Disconnected",
                                         "Connecting", "Connected"};
    logf("MicraLink: link %s -> %s\n", kNames[static_cast<int>(link_)],
         kNames[static_cast<int>(link)]);
  }
  link_ = link;
}

void MicraLink::run() {
  bool connected = false;
  uint32_t last_refresh = 0;

  for (;;) {
    if (scan_requested_.exchange(false)) do_scan();  // works in any link state

    std::string addr;
    std::string token;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      addr = address_;
      token = token_;
    }

    // Not fully provisioned: idle in the matching state (don't spin on connect).
    if (addr.empty()) {
      if (connected) { ble_.disconnect(); connected = false; }
      set_link(Link::Unconfigured);
      sleep_ms(500);
      continue;
    }
    if (token.empty()) {
      if (connected) { ble_.disconnect(); connected = false; }
      // On request (learn / retry), try to read the token from pairing mode.
      if (try_pairing_.exchange(false)) {
        set_link(Link::Connecting);
        const std::string t = do_read_pairing_token(addr);
        if (!t.empty()) {
          { std::lock_guard<std::mutex> lk(mutex_); token_ = t; }
          if (token_persister_) token_persister_(t);
          continue;  // token adopted -> next iteration connects + authenticates
        }
      }
      set_link(Link::NeedsToken);
      sleep_ms(500);
      continue;
    }

    // Address changed (e.g. user saved a new MAC): drop the old connection.
    // Token authed at the BLE level but reads were rejected: it's a bad token,
    // not a flaky link. Idle in NeedsToken (don't hammer the machine) until a new
    // token arrives (set_token / set_address clear this).
    if (token_bad_.load()) {
      if (connected) { ble_.disconnect(); connected = false; }
      set_link(Link::NeedsToken);
      sleep_ms(500);
      continue;
    }

    // User disconnected: drop the link and idle (don't auto-reconnect) so another
    // remote can take the Micra's single BLE slot.
    if (!connect_enabled_.load()) {
      if (connected) { ble_.disconnect(); connected = false; }
      set_link(Link::Disconnected);
      sleep_ms(500);
      continue;
    }

    if (reconnect_requested_.exchange(false) && connected) {
      ble_.disconnect();
      connected = false;
    }

    if (!connected) {
      set_link(Link::Connecting);
      if (!do_connect(addr, token)) {
        set_link(Link::Disconnected);  // unreachable -> back off and retry
        sleep_ms(kReconnectBackoffMs);
        continue;
      }
      // Connected over BLE; the auth WRITE always "succeeds", so verify the token
      // really works with a read. Retry a few times to ride out a transient glitch
      // before concluding the token is bad.
      bool verified = false;
      for (int i = 0; i < 3 && !verified; ++i) {
        if (do_refresh()) verified = true;  // also seeds temps before Connected
        else sleep_ms(300);
      }
      if (!verified) {
        logf("MicraLink: token rejected (reads failed) -> NeedsToken\n");
        token_bad_.store(true);
        ble_.disconnect();
        connected = false;
        set_link(Link::NeedsToken);
        continue;
      }
      connected = true;
      set_link(Link::Connected);
      last_refresh = now_ms();
    }

    if (!ble_.connected()) {
      connected = false;
      set_link(Link::Disconnected);
      continue;
    }

    bool cmd_sent = false;
    const int p = pending_power_.exchange(-1);
    if (p >= 0) { do_set_power(p == 1); cmd_sent = true; }
    const int b = pending_brew_tenths_.exchange(-1);
    if (b >= 0) {
      char v[8];
      std::snprintf(v, sizeof(v), "%.1f", b / 10.0f);
      do_set_boiler_target("CoffeeBoiler1", v);
      cmd_sent = true;
    }
    const int st = pending_steam_whole_.exchange(-1);
    if (st >= 0) {
      char v[8];
      std::snprintf(v, sizeof(v), "%d", st);
      do_set_boiler_target("SteamBoiler", v);
      cmd_sent = true;
    }
    const int se = pending_steam_enable_.exchange(-1);
    if (se >= 0) { do_set_steam_enabled(se == 1); cmd_sent = true; }
    if (cmd_sent || now_ms() - last_refresh > kPollIntervalMs) {
      do_refresh();  // a failed read just means we re-detect the drop next loop
      last_refresh = now_ms();
    }

    sleep_ms(100);
  }
}

bool MicraLink::do_connect(const std::string& address, const std::string& token) {
  // Micras advertise a public address more often than not; the transport still
  // falls back to the other type.
  if (!ble_.connect(address, /*prefer_random=*/false, kConnectTimeoutMs)) {
    logf("MicraLink: connect failed\n");
    return false;
  }

  if (!ble_.has_characteristic(kReadUuid) || !ble_.has_characteristic(kWriteUuid) ||
      !ble_.has_characteristic(kAuthUuid)) {
    logf("MicraLink: missing characteristics\n");
    ble_.disconnect();
    return false;
  }

  // Authenticate: token as raw UTF-8, NO trailing NUL, write-with-response.
  if (!ble_.write(kAuthUuid, token.data(), token.size(), /*with_response=*/true)) {
    logf("MicraLink: auth write failed\n");
    ble_.disconnect();
    return false;
  }

  // Read the Device Information Service strings once (read-only standard chars).
  {
    std::lock_guard<std::mutex> lk(mutex_);
    ble_.read(kDisManufacturer, dis_manufacturer_);
    ble_.read(kDisModel, dis_model_);
    ble_.read(kDisSerial, dis_serial_);
    ble_.read(kDisFirmware, dis_firmware_);
    ble_.read(kDisSoftware, dis_software_);
  }

  logf("MicraLink: connected + authenticated\n");
  return true;
}

bool MicraLink::do_refresh() {
  std::string mode_json;
  std::string boilers_json;
  if (!read_setting("machineMode", mode_json)) return false;
  if (!read_setting("boilers", boilers_json)) return false;

  // An authenticated machine answers with parseable JSON; a rejected token
  // yields a 1-byte NUL / garbage. Unparseable reads => NOT really authenticated,
  // so this doubles as the token check that gates Connected.
  JsonDocument mode_doc;
  if (deserializeJson(mode_doc, mode_json)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, boilers_json) || !doc.is<JsonArray>()) return false;

  const char* mode = mode_doc.as<const char*>();
  const Power power = (mode != nullptr && std::strcmp(mode, "BrewingMode") == 0)
                          ? Power::On
                          : Power::Standby;

  float brew_c = 0, brew_t = 0, boiler_c = 0, boiler_t = 0;
  bool steam_en = true;
  for (JsonObject b : doc.as<JsonArray>()) {
    const char* id = b["id"] | "";
    const float current = b["current"] | 0.0f;
    const float target = b["target"] | 0.0f;
    if (std::strcmp(id, "CoffeeBoiler1") == 0) { brew_c = current; brew_t = target; }
    else if (std::strcmp(id, "SteamBoiler") == 0) {
      boiler_c = current;
      boiler_t = target;
      steam_en = b["isEnabled"] | true;
    }
  }

  // Commit the parsed values to the shared cache under lock.
  std::lock_guard<std::mutex> lk(mutex_);
  if (power != power_) {
    // Power transitions are load-bearing for the paddle's wake-vs-shot
    // decision (BrewController standby provider) — log the raw mode string.
    logf("MicraLink: mode '%s' -> power %s\n", mode != nullptr ? mode : "(null)",
         power == Power::On ? "On" : "Standby");
  }
  power_ = power;
  brew_temp_c_ = brew_c;
  brew_target_c_ = brew_t;
  boiler_temp_c_ = boiler_c;
  boiler_target_c_ = boiler_t;
  steam_enabled_ = steam_en;
  return true;
}

void MicraLink::do_set_power(bool on) {
  const std::string json =
      on ? R"({"name":"MachineChangeMode","parameter":{"mode":"BrewingMode"}})"
         : R"({"name":"MachineChangeMode","parameter":{"mode":"StandBy"}})";
  write_with_nul(ble_, kWriteUuid, json);
}

MachineSnapshot MicraLink::snapshot() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return MachineSnapshot{
      .name = name_.c_str(),
      .manufacturer = dis_manufacturer_.c_str(),
      .model = dis_model_.c_str(),
      .serial = dis_serial_.c_str(),
      .firmware = dis_firmware_.c_str(),
      .software = dis_software_.c_str(),
      .link = link_,
      .power = power_,
      .brew_temp_c = brew_temp_c_,
      .brew_target_c = brew_target_c_,
      .boiler_temp_c = boiler_temp_c_,
      .boiler_target_c = boiler_target_c_,
      .steam_enabled = steam_enabled_,
      .brewing = brewing_,
  };
}

void MicraLink::set_power(bool on) { pending_power_.store(on ? 1 : 0); }

void MicraLink::set_brew_target(float celsius) {
  pending_brew_tenths_.store(static_cast<int>(lroundf(celsius * 10.0f)));
}

void MicraLink::set_steam_target(float celsius) {
  pending_steam_whole_.store(static_cast<int>(lroundf(celsius)));
}

void MicraLink::set_steam_enabled(bool enabled) {
  pending_steam_enable_.store(enabled ? 1 : 0);
}

void MicraLink::do_set_boiler_target(const char* identifier, const char* value) {
  std::string json =
      std::string(R"({"name":"SettingBoilerTarget","parameter":{"identifier":")") +
      identifier + R"(","value":)" + value + "}}";
  write_with_nul(ble_, kWriteUuid, json);
}

void MicraLink::do_set_steam_enabled(bool enabled) {
  std::string json =
      std::string(R"({"name":"SettingBoilerEnable","parameter":{"identifier":"SteamBoiler","state":)") +
      (enabled ? "true" : "false") + "}}";
  write_with_nul(ble_, kWriteUuid, json);
}

void MicraLink::request_scan() {
  scanning_.store(true);        // reflect immediately in the UI
  scan_requested_.store(true);  // the loop performs the actual scan
}

bool MicraLink::scanning() const { return scanning_.load(); }

std::vector<ScanResult> MicraLink::scan_results() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return scan_results_;
}

void MicraLink::do_scan() {
  std::vector<ScanResult> found;
  for (const ScanResult& r : ble_.scan(5000)) {
    if (std::strncmp(r.name, "MICRA", 5) != 0) continue;  // La Marzocco name prefix
    found.push_back(r);
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    scan_results_ = std::move(found);
  }
  scanning_.store(false);
}

std::string MicraLink::do_read_pairing_token(const std::string& address) {
  if (!ble_.connect(address, /*prefer_random=*/false, kConnectTimeoutMs)) {
    logf("MicraLink: pairing-read connect failed\n");
    return "";
  }

  std::string out;
  ble_.read(kTokenUuid, out);
  ble_.disconnect();

  // A valid token is exactly 64 hex chars (32 bytes). Outside pairing mode this
  // characteristic is absent or returns empty/garbage, which fails the check.
  if (out.size() != 64) return "";
  for (char ch : out) {
    const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                     (ch >= 'A' && ch <= 'F');
    if (!hex) return "";
  }
  logf("MicraLink: read token from pairing mode\n");
  return out;
}

}  // namespace core
