#include "platform_esp32/nimble_central.h"

#include <NimBLEDevice.h>

#include <cstring>
#include <strings.h>

#include "core/system.h"

namespace {

// The NimBLE host is shared by every central instance. Device main inits it
// up-front (single-threaded); this guard covers any other entry order.
void ensure_host() {
  if (!NimBLEDevice::isInitialized()) NimBLEDevice::init("micra-remote");
  NimBLEDevice::setMTU(247);  // machineCapabilities (~180B) exceeds default MTU
}

// Finder-chunk matcher: the scan singleton calls onResult for every
// advertisement; a target address ends the scan at once. Address compare is
// case-insensitive text — both sides come from NimBLEAddress::toString().
class FinderCallbacks final : public NimBLEScanCallbacks {
 public:
  explicit FinderCallbacks(const core::FinderTargets& targets) : targets_(targets) {}
  core::Sighting sighting;

  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (sighting.found) return;
    const std::string addr = d->getAddress().toString();
    const core::AddrType seen = d->getAddress().getType() == BLE_ADDR_RANDOM
                                    ? core::AddrType::kRandom
                                    : core::AddrType::kPublic;
    for (int i = 0; i < targets_.count; ++i) {
      const core::FinderTarget& t = targets_.t[i];
      if (strcasecmp(addr.c_str(), t.addr) != 0) continue;
      if (t.type != core::AddrType::kUnknown && t.type != seen) continue;
      sighting.found = true;
      sighting.slot = t.slot;
      sighting.type = seen;
      NimBLEDevice::getScan()->stop();  // releases the blocked getResults()
      return;
    }
  }

 private:
  const core::FinderTargets& targets_;
};

}  // namespace

namespace {
// Evidence for link drops (owner, 2026-10-31: four Micra drops in 14 min
// on the X-8 with an Umbra connected as a peripheral): the HCI reason the
// controller gives for every disconnect, and the parameters each link
// settled on. Supervision timeout = the radio lost it; "remote terminated"
// = the peer closed it; "local host terminated" = we did.
const char* hci_reason_name(int reason) {
  switch (reason >= 0x200 ? reason - 0x200 : reason) {  // BLE_HS_HCI_ERR() base
    case 0x08: return "supervision timeout";
    case 0x13: return "remote terminated";
    case 0x15: return "remote powered off";
    case 0x16: return "local host terminated";
    case 0x22: return "LMP response timeout";
    case 0x3B: return "unacceptable conn interval";
    case 0x3E: return "failed to establish";
    default: return "?";
  }
}

class LinkCallbacks : public NimBLEClientCallbacks {
 public:
  void onConnect(NimBLEClient* c) override {
    const NimBLEConnInfo info = c->getConnInfo();
    core::logf("NimbleCentral: %s connected, interval %.2f ms, latency %u, timeout %u ms\n",
               c->getPeerAddress().toString().c_str(),
               static_cast<double>(info.getConnInterval()) * 1.25,
               static_cast<unsigned>(info.getConnLatency()),
               static_cast<unsigned>(info.getConnTimeout()) * 10);
  }
  void onDisconnect(NimBLEClient* c, int reason) override {
    core::logf("NimbleCentral: %s disconnected, reason 0x%x (%s)\n",
               c->getPeerAddress().toString().c_str(), static_cast<unsigned>(reason),
               hci_reason_name(reason));
  }
};
LinkCallbacks g_link_callbacks;  // stateless; shared by every client

// NimBLE's GAP events the client/server callbacks don't surface: the result
// of a connection parameter update (ours or the peer's) with the parameters
// in force afterwards, and a peer's own update request.
int gap_event_log(ble_gap_event* event, void*) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONN_UPDATE: {
      ble_gap_conn_desc d{};
      const bool have = ble_gap_conn_find(event->conn_update.conn_handle, &d) == 0;
      core::logf("BLE: conn update on handle %u: status %d%s%.2f ms, latency %u, timeout %u ms\n",
                 static_cast<unsigned>(event->conn_update.conn_handle), event->conn_update.status,
                 have ? " -> interval " : " (no desc) ", have ? d.conn_itvl * 1.25 : 0.0,
                 have ? static_cast<unsigned>(d.conn_latency) : 0u,
                 have ? static_cast<unsigned>(d.supervision_timeout) * 10 : 0u);
      break;
    }
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
      core::logf("BLE: peer requests conn params on handle %u: interval %.2f-%.2f ms, latency %u\n",
                 static_cast<unsigned>(event->conn_update_req.conn_handle),
                 event->conn_update_req.peer_params->itvl_min * 1.25,
                 event->conn_update_req.peer_params->itvl_max * 1.25,
                 static_cast<unsigned>(event->conn_update_req.peer_params->latency));
      break;
    default:
      break;
  }
  return 0;
}
}  // namespace

namespace platform {
void install_gap_event_log() { NimBLEDevice::setCustomGapHandler(gap_event_log); }
}  // namespace platform

namespace platform {

std::vector<core::ScanResult> NimbleCentral::scan(uint32_t ms) {
  ensure_host();
  NimBLEScan* scan = NimBLEDevice::getScan();
  // The scan object is a process-wide singleton that scan_for() also drives:
  // set EVERYTHING this mode needs, never rely on a previous caller's state.
  scan->setScanCallbacks(nullptr, false);
  scan->setMaxResults(0xFF);
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
    r.addr_type = d->getAddress().getType() == BLE_ADDR_RANDOM
                      ? core::AddrType::kRandom
                      : core::AddrType::kPublic;
    found.push_back(r);
  }
  scan->clearResults();
  return found;
}

core::Sighting NimbleCentral::scan_for(const core::FinderTargets& targets, uint32_t ms) {
  ensure_host();
  if (targets.count <= 0) return core::Sighting{};
  NimBLEScan* scan = NimBLEDevice::getScan();
  FinderCallbacks cb(targets);
  // Passive (no scan-request round trips; the address is all we need), no
  // stored results (a Settings scan in a busy flat keeps 3-6 KB of devices;
  // a background chunk every few seconds must keep nothing), matched in the
  // callback which stops the scan on the first hit.
  scan->setScanCallbacks(&cb, false);
  scan->setMaxResults(0);
  scan->setActiveScan(false);
  scan->getResults(ms, false);  // blocks until the duration, stop() or cancel_scan()
  scan->setScanCallbacks(nullptr, false);  // cb goes out of scope below
  scan->clearResults();
  return cb.sighting;
}

void NimbleCentral::cancel_scan() {
  if (!NimBLEDevice::isInitialized()) return;
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) scan->stop();  // the blocked getResults() returns
}

bool NimbleCentral::connect(const std::string& mac, core::AddrType type,
                            bool prefer_random, uint32_t timeout_ms) {
  ensure_host();
  if (client_ == nullptr) {
    client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(&g_link_callbacks, /*deleteCallbacks=*/false);
  }
  if (timeout_ms > 0) client_->setConnectTimeout(timeout_ms);
  if (pref_min_ != 0)
    client_->setConnectionParams(pref_min_, pref_max_, pref_latency_, pref_timeout_);
  conn_type_ = core::AddrType::kUnknown;
  // Arbiter telltale: a connect starting while a scan runs means two links
  // hold the radio at once (the connect would silently kill the scan).
  if (NimBLEDevice::getScan()->isScanning())
    core::logf("NimbleCentral: connect while scanning (arbiter bug?)\n");

  // A known type is ONE try; unknown tries both, preferred type first.
  // hold_ is checked before EACH attempt: a peer link about to scan cancels
  // the in-flight try, and without this gate the cancelled first attempt
  // would fall straight through into the second one — camping on the radio
  // for another full timeout and starving the scan it just yielded to.
  const bool random_first = type == core::AddrType::kRandom ||
                            (type == core::AddrType::kUnknown && prefer_random);
  const uint8_t first = random_first ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
  const uint8_t second = random_first ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM;
  if (hold_.load()) return false;
  cancelled_.store(false);  // a stale one-shot cancel must not kill this attempt
  uint8_t used = first;
  connecting_.store(true);
  bool ok = client_->connect(NimBLEAddress(mac, first));
  if (!ok && type == core::AddrType::kUnknown && !hold_.load() && !cancelled_.load()) {
    used = second;
    ok = client_->connect(NimBLEAddress(mac, second));
  }
  connecting_.store(false);
  if (!ok) return false;
  conn_type_ = used == BLE_ADDR_RANDOM ? core::AddrType::kRandom
                                       : core::AddrType::kPublic;

  // Discover everything now so the by-UUID lookups below hit the cache.
  for (NimBLERemoteService* svc : client_->getServices(true)) {
    for (NimBLERemoteCharacteristic* c : svc->getCharacteristics(true)) {
      // Detailed log: which characteristics would take a write WITHOUT a
      // response (a request the host never waits on — the fallback if the
      // Micra's acknowledged writes keep stalling).
      if (core::log_detail() && c->canWriteNoResponse())
        core::logf("NimbleCentral: %s char %s: write%s, write-no-rsp\n", mac.c_str(),
                   c->getUUID().toString().c_str(), c->canWrite() ? "" : "(no)");
    }
  }
  return true;
}

void NimbleCentral::disconnect() {
  if (client_ != nullptr) client_->disconnect();
}

void NimbleCentral::cancel_connect() {
  // Aborts a pending client_->connect(); the blocked call returns false. Safe
  // cross-thread (posts a cancel into the NimBLE host). No-op when idle.
  // The flag makes the cancel cover the WHOLE connect() call — without it the
  // aborted first address-type attempt falls through to the second, and the
  // "cancelled" connect keeps the radio for another full timeout.
  cancelled_.store(true);
  // NimBLE's cancel is GLOBAL (ble_gap_conn_cancel aborts whichever client is
  // initiating): only issue it while THIS instance is the one connecting.
  if (client_ != nullptr && connecting_.load()) client_->cancelConnect();
}

void NimbleCentral::hold_connects(bool on) {
  hold_.store(on);
  if (on) cancel_connect();  // the level gates NEW attempts; this kills the current one
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
