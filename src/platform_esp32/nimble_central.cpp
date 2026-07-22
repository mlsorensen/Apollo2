#include "platform_esp32/nimble_central.h"

#include <NimBLEDevice.h>

#include <cstring>

namespace {

// The NimBLE host is shared by every central instance. Device main inits it
// up-front (single-threaded); this guard covers any other entry order.
void ensure_host() {
  if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("micra-remote");
  NimBLEDevice::setMTU(247);  // machineCapabilities (~180B) exceeds default MTU
}

}  // namespace

namespace platform {

std::vector<core::ScanResult> NimbleCentral::scan(uint32_t ms) {
  ensure_host();
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);  // also fetch scan-response for the full name
  NimBLEScanResults results = scan->getResults(ms, false);

  std::vector<core::ScanResult> found;
  for (int i = 0; i < results.getCount(); ++i) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);
    const std::string name = d->getName();
    if (name.empty()) continue;

    core::ScanResult r{};
    std::strncpy(r.name, name.c_str(), sizeof(r.name) - 1);
    std::strncpy(r.mac, d->getAddress().toString().c_str(), sizeof(r.mac) - 1);
    r.rssi = d->getRSSI();
    found.push_back(r);
  }
  scan->clearResults();
  return found;
}

bool NimbleCentral::connect(const std::string& mac, bool prefer_random,
                            uint32_t timeout_ms) {
  ensure_host();
  if (client_ == nullptr) client_ = NimBLEDevice::createClient();
  if (timeout_ms > 0) client_->setConnectTimeout(timeout_ms);

  // The address type isn't persisted, so try both, preferred type first.
  const uint8_t first = prefer_random ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
  const uint8_t second = prefer_random ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM;
  if (!client_->connect(NimBLEAddress(mac, first)) &&
      !client_->connect(NimBLEAddress(mac, second))) {
    return false;
  }

  // Discover everything now so the by-UUID lookups below hit the cache.
  for (NimBLERemoteService* svc : client_->getServices(true)) {
    svc->getCharacteristics(true);
  }
  return true;
}

void NimbleCentral::disconnect() {
  if (client_ != nullptr) client_->disconnect();
}

void NimbleCentral::cancel_connect() {
  // Aborts a pending client_->connect(); the blocked call returns false. Safe
  // cross-thread (posts a cancel into the NimBLE host). No-op when idle.
  if (client_ != nullptr) client_->cancelConnect();
}

bool NimbleCentral::connected() {
  return client_ != nullptr && client_->isConnected();
}

// Find a discovered characteristic by UUID string ("2a29", "ff11", or 128-bit
// form) across all services — the Micra's parent service UUID isn't published,
// so lookups are service-agnostic. Cached discovery: no I/O here.
static NimBLERemoteCharacteristic* find_char(NimBLEClient* client, const char* uuid) {
  if (client == nullptr || !client->isConnected()) return nullptr;
  const NimBLEUUID want(uuid);
  for (NimBLERemoteService* svc : client->getServices(false)) {
    for (NimBLERemoteCharacteristic* c : svc->getCharacteristics(false)) {
      if (c->getUUID() == want) return c;
    }
  }
  return nullptr;
}

bool NimbleCentral::has_characteristic(const char* uuid) {
  return find_char(client_, uuid) != nullptr;
}

bool NimbleCentral::read(const char* uuid, std::string& out) {
  NimBLERemoteCharacteristic* c = find_char(client_, uuid);
  if (c == nullptr) return false;
  NimBLEAttValue v = c->readValue();
  out.assign(reinterpret_cast<const char*>(v.data()), v.length());
  return true;
}

bool NimbleCentral::write(const char* uuid, const void* data, size_t len,
                          bool with_response) {
  NimBLERemoteCharacteristic* c = find_char(client_, uuid);
  if (c == nullptr) return false;
  // Downgrade to write-without-response where the characteristic doesn't
  // support a response (e.g. scale command chars).
  return c->writeValue(static_cast<const uint8_t*>(data), len,
                       with_response && c->canWrite());
}

bool NimbleCentral::subscribe(const char* uuid, core::ble::NotifyCallback cb) {
  NimBLERemoteCharacteristic* c = find_char(client_, uuid);
  if (c == nullptr) return false;
  return c->subscribe(true, [cb = std::move(cb)](NimBLERemoteCharacteristic* /*c*/,
                                                 uint8_t* data, size_t len,
                                                 bool /*is_notify*/) {
    cb(data, len);
  });
}

}  // namespace platform
