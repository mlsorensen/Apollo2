#include "platform_esp32/micra_link.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

#include <cstring>

namespace {

// GATT characteristic UUIDs (from pylamarzocco). The parent service UUID isn't
// published, so we discover characteristics by UUID across all services.
constexpr char kReadUuid[] = "0a0b7847-e12b-09a8-b04b-8e0922a9abab";   // state
constexpr char kWriteUuid[] = "0b0b7847-e12b-09a8-b04b-8e0922a9abab";  // command
constexpr char kAuthUuid[] = "0d0b7847-e12b-09a8-b04b-8e0922a9abab";   // token

// One machine, one link: the NimBLE handles live here (mirrors how display.cpp
// and touch.cpp keep their hardware handles at file scope).
NimBLEClient* g_client = nullptr;
NimBLERemoteCharacteristic* g_read = nullptr;
NimBLERemoteCharacteristic* g_write = nullptr;
NimBLERemoteCharacteristic* g_auth = nullptr;

// Write `payload` (with response) and, for read/command characteristics, append
// the single trailing NUL the firmware expects. (The auth write is the one
// exception — see authenticate() — so it doesn't use this.)
bool write_with_nul(NimBLERemoteCharacteristic* chr, const std::string& payload) {
  std::string buf = payload;
  buf.push_back('\0');
  return chr->writeValue(reinterpret_cast<const uint8_t*>(buf.data()), buf.size(),
                         true);
}

// Write a setting name to the READ characteristic, then read the JSON response.
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

MicraLink::MicraLink(std::string auth_token) : token_(std::move(auth_token)) {}

bool MicraLink::connect(const std::string& address) {
  NimBLEDevice::init("micra-remote");
  NimBLEDevice::setMTU(247);  // machineCapabilities (~180B) exceeds default MTU

  // --- Connect directly to the (known/saved) address ---
  Serial.printf("MicraLink: connecting to %s ...\n", address.c_str());
  g_client = NimBLEDevice::createClient();

  // Try a public address first, then random — saves us needing to know/store
  // the address type. (A failed connect leaves the client reusable.)
  if (!g_client->connect(NimBLEAddress(address, BLE_ADDR_PUBLIC)) &&
      !g_client->connect(NimBLEAddress(address, BLE_ADDR_RANDOM))) {
    Serial.println("MicraLink: connect failed");
    return false;
  }
  Serial.printf("MicraLink: connected, mtu=%d\n", g_client->getMTU());

  // --- Discover our characteristics by UUID across all services ---
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
    Serial.printf("MicraLink: missing characteristics (read=%d write=%d auth=%d)\n",
                  g_read != nullptr, g_write != nullptr, g_auth != nullptr);
    g_client->disconnect();
    return false;
  }

  // --- Authenticate: token as raw UTF-8, NO trailing NUL, write-with-response ---
  if (!g_auth->writeValue(reinterpret_cast<const uint8_t*>(token_.data()),
                          token_.size(), true)) {
    Serial.println("MicraLink: auth write failed");
    g_client->disconnect();
    return false;
  }
  Serial.println("MicraLink: authenticated");
  connected_ = true;
  return true;
}

bool MicraLink::isConnected() const {
  return connected_ && g_client != nullptr && g_client->isConnected();
}

bool MicraLink::refresh() {
  if (!isConnected()) return false;

  std::string mode_json;
  std::string boilers_json;
  bool ok = true;
  if (read_setting("machineMode", mode_json)) {
    Serial.printf("  raw machineMode (%d): '%s'\n", (int)mode_json.size(),
                  mode_json.c_str());
    parse_mode(mode_json);
  } else {
    Serial.println("  machineMode: read failed");
    ok = false;
  }
  if (read_setting("boilers", boilers_json)) {
    Serial.printf("  raw boilers (%d): '%s'\n", (int)boilers_json.size(),
                  boilers_json.c_str());
    parse_boilers(boilers_json);
  } else {
    Serial.println("  boilers: read failed");
    ok = false;
  }

  status_ = (power_ == core::Power::On) ? "Ready" : "Standby";
  return ok;
}

void MicraLink::parse_mode(const std::string& json) {
  // machineMode is a JSON string, e.g. "BrewingMode".
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  const char* mode = doc.as<const char*>();
  if (mode == nullptr) return;
  if (std::strcmp(mode, "BrewingMode") == 0) power_ = core::Power::On;
  else power_ = core::Power::Standby;  // StandBy / EcoMode
}

void MicraLink::parse_boilers(const std::string& json) {
  // boilers is a JSON array of {id,isEnabled,target,current}.
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;
  for (JsonObject b : doc.as<JsonArray>()) {
    const char* id = b["id"] | "";
    const float current = b["current"] | 0.0f;
    const float target = b["target"] | 0.0f;
    if (std::strcmp(id, "CoffeeBoiler1") == 0) {
      brew_temp_c_ = current;
      brew_target_c_ = target;
    } else if (std::strcmp(id, "SteamBoiler") == 0) {
      boiler_temp_c_ = current;
      boiler_target_c_ = target;
    }
  }
}

bool MicraLink::setPower(bool on) {
  if (!isConnected() || g_write == nullptr) return false;
  const std::string json =
      on ? R"({"name":"MachineChangeMode","parameter":{"mode":"BrewingMode"}})"
         : R"({"name":"MachineChangeMode","parameter":{"mode":"StandBy"}})";
  return write_with_nul(g_write, json);
}

core::MachineSnapshot MicraLink::snapshot() const {
  return core::MachineSnapshot{
      .name = name_.c_str(),
      .power = power_,
      .brew_temp_c = brew_temp_c_,
      .brew_target_c = brew_target_c_,
      .boiler_temp_c = boiler_temp_c_,
      .boiler_target_c = boiler_target_c_,
      .brewing = brewing_,
      .status = status_.empty() ? "" : status_.c_str(),
  };
}

}  // namespace platform
