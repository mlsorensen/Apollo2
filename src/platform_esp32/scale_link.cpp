#include "platform_esp32/scale_link.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

// Bookoo Themis Mini. Characteristics are discovered by UUID across all services
// (like MicraLink), so the parent service UUID doesn't matter. Notify = weight
// stream, write = commands.
constexpr uint16_t kNotifyUuid = 0xFF11;
constexpr uint16_t kWriteUuid = 0xFF12;
constexpr uint8_t kTareCmd[] = {0x03, 0x0a, 0x01, 0x00, 0x00, 0x08};

constexpr uint32_t kReconnectBackoffMs = 3000;

// One link's NimBLE handles, touched only by the scale task / its notify callback.
NimBLEClient* g_client = nullptr;
NimBLERemoteCharacteristic* g_notify = nullptr;
NimBLERemoteCharacteristic* g_write = nullptr;

bool name_has_prefix(const std::string& name, const char* prefix) {
  const size_t n = std::strlen(prefix);
  if (name.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::toupper(static_cast<unsigned char>(name[i])) != prefix[i]) return false;
  }
  return true;
}

// Decode a 20-byte Bookoo Themis notification (per goscale themis/comms):
//   [2..4] ms timer (24-bit BE); [6] sign ('-'=neg); [7..9] weight 24-bit BE /100;
//   [11..12] flow 16-bit BE /100; [13] battery %.
void decode_and_publish(platform::ScaleLink* link, const uint8_t* d, size_t len) {
  if (d == nullptr || len < 20) return;
  const uint32_t ms =
      (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 8) | d[4];
  const int sign = (d[6] == 0x2D) ? -1 : 1;  // 0x2D == '-'
  const uint32_t raw_w =
      (static_cast<uint32_t>(d[7]) << 16) | (static_cast<uint32_t>(d[8]) << 8) | d[9];
  const float weight = sign * static_cast<float>(raw_w) / 100.0f;
  const uint16_t raw_f = (static_cast<uint16_t>(d[11]) << 8) | d[12];
  const float flow = static_cast<float>(raw_f) / 100.0f;
  const int batt = d[13];
  link->publish_sample(weight, flow, ms, batt);
}

}  // namespace

namespace platform {

void ScaleLink::begin(std::string address) {
  address_ = std::move(address);  // task not started yet — no lock needed
  xTaskCreatePinnedToCore(&ScaleLink::task_entry, "scale_link", 8192, this,
                          /*priority=*/1, nullptr, /*core=*/1);
}

void ScaleLink::set_address(std::string address) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    address_ = std::move(address);
  }
  reconnect_requested_.store(true);
}

void ScaleLink::set_name(std::string name) {
  std::lock_guard<std::mutex> lk(mutex_);
  name_ = name.empty() ? "Scale" : std::move(name);
}

void ScaleLink::set_connect_enabled(bool enabled) {
  connect_enabled_.store(enabled);
  reconnect_requested_.store(true);
}

void ScaleLink::set_connected(bool c) {
  std::lock_guard<std::mutex> lk(mutex_);
  connected_ = c;
  if (!c) {
    weight_g_ = 0.0f;
    flow_gps_ = 0.0f;
    pending_flow_.clear();  // don't plot stale samples after a drop
  }
}

void ScaleLink::publish_sample(float weight_g, float flow_gps, uint32_t timer_ms,
                               int battery_pct) {
  std::lock_guard<std::mutex> lk(mutex_);
  weight_g_ = weight_g;
  flow_gps_ = flow_gps;
  timer_ms_ = timer_ms;
  battery_pct_ = battery_pct;
  battery_valid_ = true;
  // Buffer every notification's flow value for the chart (the UI drains it each
  // loop). Cap so it can't grow unbounded if nothing is draining.
  if (pending_flow_.size() < 512) pending_flow_.push_back(flow_gps);
}

size_t ScaleLink::drain_flow(float* out, size_t max) {
  std::lock_guard<std::mutex> lk(mutex_);
  const size_t n = std::min(max, pending_flow_.size());
  for (size_t i = 0; i < n; ++i) out[i] = pending_flow_[i];
  pending_flow_.erase(pending_flow_.begin(), pending_flow_.begin() + n);
  return n;
}

void ScaleLink::task_entry(void* arg) { static_cast<ScaleLink*>(arg)->task_loop(); }

void ScaleLink::task_loop() {
  // MicraLink (or device main) inits NimBLE first; guard so we don't double-init.
  if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("micra-remote");

  bool connected = false;
  for (;;) {
    if (scan_requested_.exchange(false)) do_scan();  // works in any state

    std::string addr;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      addr = address_;
    }

    // No scale saved, or user disabled it: idle (drop any link).
    if (addr.empty() || !connect_enabled_.load()) {
      if (connected && g_client != nullptr) g_client->disconnect();
      if (connected) { connected = false; set_connected(false); }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (reconnect_requested_.exchange(false) && connected && g_client != nullptr) {
      g_client->disconnect();
      connected = false;
      set_connected(false);
    }

    if (!connected) {
      if (!do_connect(addr)) {
        set_connected(false);
        vTaskDelay(pdMS_TO_TICKS(kReconnectBackoffMs));
        continue;
      }
      connected = true;
      set_connected(true);
    }

    if (g_client == nullptr || !g_client->isConnected()) {
      connected = false;
      set_connected(false);
      continue;
    }

    if (pending_tare_.exchange(false) && g_write != nullptr) {
      g_write->writeValue(kTareCmd, sizeof(kTareCmd), g_write->canWrite());
    }

    vTaskDelay(pdMS_TO_TICKS(100));  // weight arrives async via the notify callback
  }
}

bool ScaleLink::do_connect(const std::string& address) {
  if (g_client == nullptr) g_client = NimBLEDevice::createClient();

  if (!g_client->connect(NimBLEAddress(address, BLE_ADDR_PUBLIC)) &&
      !g_client->connect(NimBLEAddress(address, BLE_ADDR_RANDOM))) {
    Serial.println("ScaleLink: connect failed");
    return false;
  }

  g_notify = g_write = nullptr;
  for (NimBLERemoteService* svc : g_client->getServices(true)) {
    for (NimBLERemoteCharacteristic* c : svc->getCharacteristics(true)) {
      const NimBLEUUID u = c->getUUID();
      if (u == NimBLEUUID(kNotifyUuid)) g_notify = c;
      else if (u == NimBLEUUID(kWriteUuid)) g_write = c;
    }
  }
  if (g_notify == nullptr) {
    Serial.println("ScaleLink: notify characteristic not found");
    g_client->disconnect();
    return false;
  }

  ScaleLink* self = this;
  g_notify->subscribe(
      true,
      [self](NimBLERemoteCharacteristic* /*c*/, uint8_t* data, size_t len, bool /*notify*/) {
        decode_and_publish(self, data, len);
      });

  Serial.println("ScaleLink: connected + subscribed");
  return true;
}

core::ScaleSnapshot ScaleLink::snapshot() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return core::ScaleSnapshot{
      .name = name_.c_str(),
      .connected = connected_,
      .weight_g = weight_g_,
      .flow_gps = flow_gps_,
      .timer_ms = timer_ms_,
      .battery_valid = battery_valid_,
      .battery_pct = battery_pct_,
  };
}

core::ScaleFeatures ScaleLink::features() const {
  return core::ScaleFeatures{
      .tare = true, .flow = true, .timer = true, .battery = true, .beep = false};
}

void ScaleLink::tare() { pending_tare_.store(true); }

void ScaleLink::request_scan() {
  scanning_.store(true);        // reflect immediately in the UI
  scan_requested_.store(true);  // the task performs the actual scan
}

bool ScaleLink::scanning() const { return scanning_.load(); }

std::vector<core::ScanResult> ScaleLink::scan_results() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return scan_results_;
}

void ScaleLink::do_scan() {
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);  // fetch scan-response for the full name
  NimBLEScanResults results = scan->getResults(5000, false);

  std::vector<core::ScanResult> found;
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);
    const std::string name = d->getName();
    if (!name_has_prefix(name, "BOOKOO")) continue;  // Themis advertises "BOOKOO..."

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
