#include "core/scale_link.h"

#include "core/system.h"

namespace {

constexpr uint32_t kReconnectBackoffMs = 3000;
// Connect timeout per address-type attempt. We connect by MAC and don't persist
// the address type, so the transport tries both — a wrong-type attempt at the
// stack's 30 s default would stall the fallback, so cap it low.
constexpr uint32_t kConnectTimeoutMs = 5000;

}  // namespace

namespace core {

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
  // Classify eagerly so features() reflects the saved model before the first
  // connect. do_connect() makes a fresh instance per connection anyway.
  driver_ = make_scale_driver(name_.c_str());
}

void ScaleLink::set_connect_enabled(bool enabled) {
  connect_enabled_.store(enabled);
  reconnect_requested_.store(true);
}

void ScaleLink::set_connected(bool c) {
  std::lock_guard<std::mutex> lk(mutex_);
  connected_ = c;
  if (!c) weight_g_ = 0.0f;
}

void ScaleLink::on_weight(float grams) {
  std::lock_guard<std::mutex> lk(mutex_);
  weight_g_ = grams;
  ++seq_;  // stream-rate probe: one tick per received weight update
}

void ScaleLink::on_timer(uint32_t timer_ms) {
  std::lock_guard<std::mutex> lk(mutex_);
  timer_ms_ = timer_ms;
}

void ScaleLink::on_battery(int pct) {
  std::lock_guard<std::mutex> lk(mutex_);
  battery_pct_ = pct;
  battery_valid_ = true;
}

void ScaleLink::run() {
  bool connected = false;
  for (;;) {
    if (scan_requested_.exchange(false)) do_scan();  // works in any state

    std::string addr;
    std::shared_ptr<IScaleDriver> drv;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      addr = address_;
      drv = driver_;
    }

    // No scale saved, or user disabled it: idle (drop any link).
    if (addr.empty() || !connect_enabled_.load()) {
      if (connected) ble_.disconnect();
      if (connected) { connected = false; set_connected(false); }
      sleep_ms(500);
      continue;
    }

    if (reconnect_requested_.exchange(false) && connected) {
      ble_.disconnect();
      connected = false;
      set_connected(false);
    }

    if (!connected) {
      if (!do_connect(addr)) {
        set_connected(false);
        sleep_ms(kReconnectBackoffMs);
        continue;
      }
      connected = true;
      set_connected(true);
      continue;  // re-fetch the freshly created driver next pass
    }

    if (!ble_.connected()) {
      connected = false;
      set_connected(false);
      continue;
    }

    if (pending_tare_.exchange(false) && drv) drv->tare(ble_);

    // Driver housekeeping: heartbeats, stream watchdogs. false = link is dead.
    if (drv && !drv->tick(ble_)) {
      ble_.disconnect();
      connected = false;
      set_connected(false);
      continue;
    }

    sleep_ms(100);  // weight arrives async via the notify callback
  }
}

bool ScaleLink::do_connect(const std::string& address) {
  // Fresh driver per attempt: per-connection state starts clean, and the name
  // may have changed since the last connect.
  std::shared_ptr<IScaleDriver> drv;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    drv = make_scale_driver(name_.c_str());
    driver_ = drv;
  }
  if (!drv) {
    logf("ScaleLink: no driver for saved scale\n");
    return false;
  }

  // Random first: the Bookoo (and most scales) advertise a random address, so the
  // saved-MAC reconnect after reboot then succeeds on the first try (was ~30 s: a
  // PUBLIC-first attempt timed out fully before falling back to RANDOM).
  if (!ble_.connect(address, /*prefer_random=*/true, kConnectTimeoutMs)) {
    logf("ScaleLink: connect failed\n");
    return false;
  }

  const char* notify_uuid = drv->select_notify(ble_);
  if (notify_uuid == nullptr) {
    logf("ScaleLink: expected characteristics not found\n");
    ble_.disconnect();
    return false;
  }

  // The lambda owns a driver reference: a driver replaced mid-notification
  // stays alive until the transport drops the callback.
  ScaleLink* self = this;
  ble_.subscribe(notify_uuid, [self, drv](const uint8_t* data, size_t len) {
    drv->on_notify(data, len, *self);
  });

  if (!drv->start(ble_)) {
    logf("ScaleLink: %s init failed\n", drv->model());
    ble_.disconnect();
    return false;
  }

  logf("ScaleLink: %s connected + subscribed\n", drv->model());
  return true;
}

ScaleSnapshot ScaleLink::snapshot() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return ScaleSnapshot{
      .name = name_.c_str(),
      .connected = connected_,
      .weight_g = weight_g_,
      .timer_ms = timer_ms_,
      .battery_valid = battery_valid_,
      .battery_pct = battery_pct_,
      .seq = seq_,
  };
}

ScaleFeatures ScaleLink::features() const {
  std::lock_guard<std::mutex> lk(mutex_);
  if (driver_) return driver_->features();
  return ScaleFeatures{
      .tare = true, .flow = true, .timer = true, .battery = true, .beep = false};
}

void ScaleLink::tare() { pending_tare_.store(true); }

void ScaleLink::request_scan() {
  scanning_.store(true);        // reflect immediately in the UI
  scan_requested_.store(true);  // the loop performs the actual scan
}

bool ScaleLink::scanning() const { return scanning_.load(); }

std::vector<ScanResult> ScaleLink::scan_results() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return scan_results_;
}

void ScaleLink::do_scan() {
  std::vector<ScanResult> found;
  for (const ScanResult& r : ble_.scan(5000)) {
    if (!scale_name_supported(r.name)) continue;  // Bookoo + Acaia families
    found.push_back(r);
  }

  {
    std::lock_guard<std::mutex> lk(mutex_);
    scan_results_ = std::move(found);
  }
  scanning_.store(false);
}

}  // namespace core
