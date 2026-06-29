#include "platform_esp32/micra_link.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace {

// GATT characteristic UUIDs (from pylamarzocco). The parent service UUID isn't
// published, so we discover characteristics by UUID across all services.
constexpr char kReadUuid[] = "0a0b7847-e12b-09a8-b04b-8e0922a9abab";   // state
constexpr char kWriteUuid[] = "0b0b7847-e12b-09a8-b04b-8e0922a9abab";  // command
constexpr char kTokenUuid[] = "0c0b7847-e12b-09a8-b04b-8e0922a9abab";  // pairing-mode token
constexpr char kAuthUuid[] = "0d0b7847-e12b-09a8-b04b-8e0922a9abab";   // auth

constexpr uint32_t kPollIntervalMs = 3000;
constexpr uint32_t kReconnectBackoffMs = 3000;

// One link: the NimBLE handles live at file scope and are touched ONLY by the
// connection task (mirrors display.cpp / touch.cpp).
NimBLEClient* g_client = nullptr;
NimBLERemoteCharacteristic* g_read = nullptr;
NimBLERemoteCharacteristic* g_write = nullptr;
NimBLERemoteCharacteristic* g_auth = nullptr;

bool write_with_nul(NimBLERemoteCharacteristic* chr, const std::string& payload) {
  std::string buf = payload;
  buf.push_back('\0');
  return chr->writeValue(reinterpret_cast<const uint8_t*>(buf.data()), buf.size(),
                         true);
}

bool read_setting(const char* name, std::string& out) {
  if (g_read == nullptr) return false;
  if (!write_with_nul(g_read, name)) return false;
  delay(150);  // let the machine replace the characteristic value with its answer
  NimBLEAttValue v = g_read->readValue();
  out.assign(reinterpret_cast<const char*>(v.data()), v.length());
  return !out.empty();
}

}  // namespace

namespace platform {

void MicraLink::begin(std::string address) {
  address_ = std::move(address);  // task not started yet — no lock needed
  xTaskCreatePinnedToCore(&MicraLink::task_entry, "micra_link", 8192, this,
                          /*priority=*/1, nullptr, /*core=*/1);
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

void MicraLink::set_name(std::string name) {
  std::lock_guard<std::mutex> lk(mutex_);
  name_ = name.empty() ? "Micra" : std::move(name);
}

void MicraLink::request_pairing_read() {
  set_link(core::Link::Connecting);  // reflect "working" at once (avoids a brief
  try_pairing_.store(true);          // NeedsToken flash before the task starts)
}

void MicraLink::set_token_persister(std::function<void(std::string)> persister) {
  token_persister_ = std::move(persister);
}

void MicraLink::task_entry(void* arg) { static_cast<MicraLink*>(arg)->task_loop(); }

void MicraLink::set_link(core::Link link) {
  std::lock_guard<std::mutex> lk(mutex_);
  link_ = link;
}

void MicraLink::task_loop() {
  NimBLEDevice::init("micra-remote");
  NimBLEDevice::setMTU(247);  // machineCapabilities (~180B) exceeds default MTU

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
      if (connected && g_client != nullptr) { g_client->disconnect(); connected = false; }
      set_link(core::Link::Unconfigured);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (token.empty()) {
      if (connected && g_client != nullptr) { g_client->disconnect(); connected = false; }
      // On request (learn / retry), try to read the token from pairing mode.
      if (try_pairing_.exchange(false)) {
        set_link(core::Link::Connecting);
        const std::string t = do_read_pairing_token(addr);
        if (!t.empty()) {
          { std::lock_guard<std::mutex> lk(mutex_); token_ = t; }
          if (token_persister_) token_persister_(t);
          continue;  // token adopted -> next iteration connects + authenticates
        }
      }
      set_link(core::Link::NeedsToken);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // Address changed (e.g. user saved a new MAC): drop the old connection.
    // Token authed at the BLE level but reads were rejected: it's a bad token,
    // not a flaky link. Idle in NeedsToken (don't hammer the machine) until a new
    // token arrives (set_token / set_address clear this).
    if (token_bad_.load()) {
      if (connected && g_client != nullptr) { g_client->disconnect(); connected = false; }
      set_link(core::Link::NeedsToken);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (reconnect_requested_.exchange(false) && connected && g_client != nullptr) {
      g_client->disconnect();
      connected = false;
    }

    if (!connected) {
      set_link(core::Link::Connecting);
      if (!do_connect(addr, token)) {
        set_link(core::Link::Disconnected);  // unreachable -> back off and retry
        vTaskDelay(pdMS_TO_TICKS(kReconnectBackoffMs));
        continue;
      }
      // Connected over BLE; the auth WRITE always "succeeds", so verify the token
      // really works with a read. Retry a few times to ride out a transient glitch
      // before concluding the token is bad.
      bool verified = false;
      for (int i = 0; i < 3 && !verified; ++i) {
        if (do_refresh()) verified = true;  // also seeds temps before Connected
        else vTaskDelay(pdMS_TO_TICKS(300));
      }
      if (!verified) {
        Serial.println("MicraLink: token rejected (reads failed) -> NeedsToken");
        token_bad_.store(true);
        if (g_client != nullptr) g_client->disconnect();
        connected = false;
        set_link(core::Link::NeedsToken);
        continue;
      }
      connected = true;
      set_link(core::Link::Connected);
      last_refresh = millis();
    }

    if (g_client == nullptr || !g_client->isConnected()) {
      connected = false;
      set_link(core::Link::Disconnected);
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
    if (cmd_sent || millis() - last_refresh > kPollIntervalMs) {
      do_refresh();  // a failed read just means we re-detect the drop next loop
      last_refresh = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

bool MicraLink::do_connect(const std::string& address, const std::string& token) {
  if (g_client == nullptr) g_client = NimBLEDevice::createClient();

  // Try public then random address type so we needn't store/know which it is.
  if (!g_client->connect(NimBLEAddress(address, BLE_ADDR_PUBLIC)) &&
      !g_client->connect(NimBLEAddress(address, BLE_ADDR_RANDOM))) {
    Serial.println("MicraLink: connect failed");
    return false;
  }

  g_read = g_write = g_auth = nullptr;
  // Standard Device Information Service (0x180A) string characteristics.
  NimBLERemoteCharacteristic* dis_mfr = nullptr;
  NimBLERemoteCharacteristic* dis_model = nullptr;
  NimBLERemoteCharacteristic* dis_serial = nullptr;
  NimBLERemoteCharacteristic* dis_fw = nullptr;
  NimBLERemoteCharacteristic* dis_sw = nullptr;
  for (NimBLERemoteService* svc : g_client->getServices(true)) {
    for (NimBLERemoteCharacteristic* c : svc->getCharacteristics(true)) {
      const NimBLEUUID u = c->getUUID();
      if (u == NimBLEUUID(kReadUuid)) g_read = c;
      else if (u == NimBLEUUID(kWriteUuid)) g_write = c;
      else if (u == NimBLEUUID(kAuthUuid)) g_auth = c;
      else if (u == NimBLEUUID(static_cast<uint16_t>(0x2A29))) dis_mfr = c;
      else if (u == NimBLEUUID(static_cast<uint16_t>(0x2A24))) dis_model = c;
      else if (u == NimBLEUUID(static_cast<uint16_t>(0x2A25))) dis_serial = c;
      else if (u == NimBLEUUID(static_cast<uint16_t>(0x2A26))) dis_fw = c;
      else if (u == NimBLEUUID(static_cast<uint16_t>(0x2A28))) dis_sw = c;
    }
  }
  if (g_read == nullptr || g_write == nullptr || g_auth == nullptr) {
    Serial.println("MicraLink: missing characteristics");
    g_client->disconnect();
    return false;
  }

  // Authenticate: token as raw UTF-8, NO trailing NUL, write-with-response.
  if (!g_auth->writeValue(reinterpret_cast<const uint8_t*>(token.data()),
                          token.size(), true)) {
    Serial.println("MicraLink: auth write failed");
    g_client->disconnect();
    return false;
  }

  // Read the Device Information Service strings once (read-only standard chars).
  auto read_dis = [](NimBLERemoteCharacteristic* c, std::string& out) {
    if (c == nullptr) return;
    NimBLEAttValue v = c->readValue();
    out.assign(reinterpret_cast<const char*>(v.data()), v.length());
  };
  {
    std::lock_guard<std::mutex> lk(mutex_);
    read_dis(dis_mfr, dis_manufacturer_);
    read_dis(dis_model, dis_model_);
    read_dis(dis_serial, dis_serial_);
    read_dis(dis_fw, dis_firmware_);
    read_dis(dis_sw, dis_software_);
  }

  Serial.println("MicraLink: connected + authenticated");
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
  const core::Power power =
      (mode != nullptr && std::strcmp(mode, "BrewingMode") == 0) ? core::Power::On
                                                                 : core::Power::Standby;

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
  power_ = power;
  brew_temp_c_ = brew_c;
  brew_target_c_ = brew_t;
  boiler_temp_c_ = boiler_c;
  boiler_target_c_ = boiler_t;
  steam_enabled_ = steam_en;
  return true;
}

void MicraLink::do_set_power(bool on) {
  if (g_write == nullptr) return;
  const std::string json =
      on ? R"({"name":"MachineChangeMode","parameter":{"mode":"BrewingMode"}})"
         : R"({"name":"MachineChangeMode","parameter":{"mode":"StandBy"}})";
  write_with_nul(g_write, json);
}

core::MachineSnapshot MicraLink::snapshot() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return core::MachineSnapshot{
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
  if (g_write == nullptr) return;
  std::string json =
      std::string(R"({"name":"SettingBoilerTarget","parameter":{"identifier":")") +
      identifier + R"(","value":)" + value + "}}";
  write_with_nul(g_write, json);
}

void MicraLink::do_set_steam_enabled(bool enabled) {
  if (g_write == nullptr) return;
  std::string json =
      std::string(R"({"name":"SettingBoilerEnable","parameter":{"identifier":"SteamBoiler","state":)") +
      (enabled ? "true" : "false") + "}}";
  write_with_nul(g_write, json);
}

void MicraLink::request_scan() {
  scanning_.store(true);       // reflect immediately in the UI
  scan_requested_.store(true);  // the task performs the actual scan
}

bool MicraLink::scanning() const { return scanning_.load(); }

std::vector<core::ScanResult> MicraLink::scan_results() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return scan_results_;
}

void MicraLink::do_scan() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);  // also fetch scan-response for the full name
  NimBLEScanResults results = scan->getResults(5000, false);

  std::vector<core::ScanResult> found;
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);
    const std::string name = d->getName();
    if (name.rfind("MICRA", 0) != 0) continue;  // La Marzocco name prefix

    core::ScanResult r{};
    std::strncpy(r.name, name.c_str(), sizeof(r.name) - 1);
    std::strncpy(r.mac, d->getAddress().toString().c_str(), sizeof(r.mac) - 1);
    r.rssi = d->getRSSI();
    found.push_back(r);
  }
  scan->clearResults();

  {
    std::lock_guard<std::mutex> lk(mutex_);
    scan_results_ = std::move(found);
  }
  scanning_.store(false);
}

std::string MicraLink::do_read_pairing_token(const std::string& address) {
  if (g_client == nullptr) g_client = NimBLEDevice::createClient();
  if (!g_client->connect(NimBLEAddress(address, BLE_ADDR_PUBLIC)) &&
      !g_client->connect(NimBLEAddress(address, BLE_ADDR_RANDOM))) {
    Serial.println("MicraLink: pairing-read connect failed");
    return "";
  }

  NimBLERemoteCharacteristic* token_char = nullptr;
  for (NimBLERemoteService* svc : g_client->getServices(true)) {
    for (NimBLERemoteCharacteristic* c : svc->getCharacteristics(true)) {
      if (c->getUUID() == NimBLEUUID(kTokenUuid)) { token_char = c; break; }
    }
    if (token_char != nullptr) break;
  }

  std::string out;
  if (token_char != nullptr && token_char->canRead()) {
    NimBLEAttValue v = token_char->readValue();
    out.assign(reinterpret_cast<const char*>(v.data()), v.length());
  }
  g_client->disconnect();

  // A valid token is exactly 64 hex chars (32 bytes). Outside pairing mode this
  // characteristic is absent or returns empty/garbage, which fails the check.
  if (out.size() != 64) return "";
  for (char ch : out) {
    const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                     (ch >= 'A' && ch <= 'F');
    if (!hex) return "";
  }
  Serial.println("MicraLink: read token from pairing mode");
  return out;
}

}  // namespace platform
