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
constexpr char kAuthUuid[] = "0d0b7847-e12b-09a8-b04b-8e0922a9abab";   // token

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

core::Power parse_mode(const std::string& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return core::Power::Standby;
  const char* mode = doc.as<const char*>();
  if (mode != nullptr && std::strcmp(mode, "BrewingMode") == 0) return core::Power::On;
  return core::Power::Standby;  // StandBy / EcoMode
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
  reconnect_requested_.store(true);
}

void MicraLink::set_token(std::string token) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    token_ = std::move(token);
  }
  reconnect_requested_.store(true);
}

void MicraLink::set_name(std::string name) {
  std::lock_guard<std::mutex> lk(mutex_);
  name_ = name.empty() ? "Micra" : std::move(name);
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
    //   no address -> Unconfigured;  address but no token -> NeedsToken.
    if (addr.empty() || token.empty()) {
      if (connected && g_client != nullptr) {
        g_client->disconnect();
        connected = false;
      }
      set_link(addr.empty() ? core::Link::Unconfigured : core::Link::NeedsToken);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    // Address changed (e.g. user saved a new MAC): drop the old connection.
    if (reconnect_requested_.exchange(false) && connected && g_client != nullptr) {
      g_client->disconnect();
      connected = false;
    }

    if (!connected) {
      set_link(core::Link::Connecting);
      if (do_connect(addr, token)) {
        connected = true;
        do_refresh();  // populate state BEFORE announcing Connected (so the UI
        set_link(core::Link::Connected);  // seeds temps from real values, not 0)
        last_refresh = millis();
      } else {
        set_link(core::Link::Disconnected);
        vTaskDelay(pdMS_TO_TICKS(kReconnectBackoffMs));
        continue;
      }
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
  for (NimBLERemoteService* svc : g_client->getServices(true)) {
    for (NimBLERemoteCharacteristic* c : svc->getCharacteristics(true)) {
      const NimBLEUUID u = c->getUUID();
      if (u == NimBLEUUID(kReadUuid)) g_read = c;
      else if (u == NimBLEUUID(kWriteUuid)) g_write = c;
      else if (u == NimBLEUUID(kAuthUuid)) g_auth = c;
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
  Serial.println("MicraLink: connected + authenticated");
  return true;
}

bool MicraLink::do_refresh() {
  std::string mode_json;
  std::string boilers_json;
  if (!read_setting("machineMode", mode_json)) return false;
  if (!read_setting("boilers", boilers_json)) return false;

  const core::Power power = parse_mode(mode_json);

  float brew_c = 0, brew_t = 0, boiler_c = 0, boiler_t = 0;
  bool steam_en = true;
  JsonDocument doc;
  if (!deserializeJson(doc, boilers_json)) {
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

}  // namespace platform
